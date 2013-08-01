/**
 * player.c
 *
 * Handle session callbacks for audio playback
 * - music_delivery
 * - get_audio_buffer_stats
 * - playback_start
 * - playback_stop
 * - playtoken_lost
 *
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>

#include "player.h"
#include "app.h"
#include "audio.h"

static void player_stats_update(int num_frames, int sample_rate);

uint32_t frames_sunk, frames_expected;


/* Called from libspotify's internal thread */
int player_callback_frame_delivery(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames) {
	audio_fifo_t *af = app_get_audio_fifo();
	audio_fifo_data_t *afd;
	size_t len;

	if(num_frames == 0)
		return 0; // Audio discontinuity, do nothing

	/* Buffer one second of audio */
	pthread_mutex_lock(&af->mutex);
	if(af->qlen > format->sample_rate) {
		pthread_mutex_unlock(&af->mutex);
		return 0;
	}

	len = num_frames * sizeof(int16_t) * format->channels;

	afd = malloc(sizeof(audio_fifo_data_t) + len);
	if(afd == NULL) {
		pthread_mutex_unlock(&af->mutex);
		return 0;
	}

	memcpy(afd->samples, frames, len);
	afd->nsamples = num_frames;

	afd->rate = format->sample_rate;
	afd->channels = format->channels;

	TAILQ_INSERT_TAIL(&af->q, afd, link);
	af->qlen += num_frames;

	pthread_cond_signal(&af->cond);
	pthread_mutex_unlock(&af->mutex);

	player_stats_update(num_frames, format->sample_rate);

#if 0
{

	int16_t *ptr = (int16_t *)frames;
	if(total_frames == 0) {
		sp_track *track = app_get_track();

		total_frames = format->sample_rate;
		total_frames *= sp_track_duration(track) / 1000;

		syslog(LOG_INFO, "Player: track duration %dms, sample rate:%d, channels:%d, total frames:%d",
			sp_track_duration(track), format->sample_rate, format->channels, total_frames);
	}

	current_frames += num_frames;
	syslog(LOG_DEBUG, "Player: num_frames=%d (played %d/%d), time left: %02d:%02d [%6d, %6d, %6d, %6d, %6d, %6d, %6d, %6d]",
			num_frames, current_frames, total_frames,
			((total_frames - current_frames) / format->sample_rate) / 60,
 			((total_frames - current_frames) / format->sample_rate) % 60,
 			ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7]);

}
#endif

	return num_frames;
}

/* Called from libspotify's internal thread */
void player_callback_start_playback(sp_session *session) {

	syslog(LOG_INFO, "Player: playback started");
	app_post_event(APP_DO_PREFETCH);
}

/* Called from libspotify's internal thread */
void player_callback_stop_playback(sp_session *session) {

	syslog(LOG_INFO, "Player: playback ended");
}

/* Called from libspotify's internal thread */
void player_callback_get_audio_buffer_stats(sp_session *session, sp_audio_buffer_stats *stats) {
	audio_fifo_t *af = app_get_audio_fifo();

	pthread_mutex_lock(&af->mutex);
	stats->samples = af->qlen;
	pthread_mutex_unlock(&af->mutex);
	stats->stutter = 0;

	//syslog(LOG_DEBUG, "%s: samples:%d, stutter:%d", __func__, stats->samples, stats->stutter);
}

/* Before libspotify 12, this was called from libspotify's internal thread */
void player_callback_end_of_track(sp_session *session) {
	sp_track *track = app_get_track();

	player_stats_reset();

	if(track) {
		syslog(LOG_INFO, "Player: finished playing track: %02d. %s - %s",
				sp_track_index(track),
				sp_artist_name(sp_track_artist(track, 0)),
				sp_track_name(track));

		/* Unload current track */
		audio_fifo_flush(app_get_audio_fifo());
		sp_session_player_unload(session);
	}

	/* Advance to next track and start playing */
	app_post_event(APP_DO_NEXT_TRACK);
}

void player_callback_playtoken_lost(sp_session *session) {

	syslog(LOG_NOTICE, "Player: playback stopped because Spotify is used elsewhere");
	audio_fifo_flush(app_get_audio_fifo());
}

void player_stats_reset(void) {
	frames_sunk = 0;
	frames_expected = 0;

	syslog(LOG_DEBUG, "Player: statistics reset");
}

static void player_stats_update(int num_frames, int sample_rate) {
	sp_track *track;

	if(frames_expected == 0) {
		track = app_get_track();
		if(track) {
			frames_expected = sample_rate;
			frames_expected *= sp_track_duration(track) / 1000;
		}
	}

	frames_sunk += num_frames;
}
