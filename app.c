/**
 * app.c
 *
 * Application event handling
 *
 */

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "app.h"
#include "player.h"
#include "playlist.h"
#include "rpi-gpio.h"

static app_event_t app_next_event(void);
static const char *app_event_name(app_event_t event);


/* Application global state */
typedef struct {
	app_event_t events;

	sp_session *session;
	sp_track *track;
	sp_link *link;
	sp_playlist *inbox;
	sp_playlist *starred;

	/* Active playlist and track */
	sp_playlist *active_playlist;
	int playlist_track_idx;

	/* Audio fifo buffer */
	audio_fifo_t audio_fifo;

	/* Main thread signaling */
	int signal_fds[2];

	/* GPIO */
	int gpio_fd;

} app_private_t;
static app_private_t *g_app;


void *app_create(void) {

	g_app = calloc(1, sizeof(app_private_t));
	audio_init(&g_app->audio_fifo);

	if(pipe(g_app->signal_fds) < 0)
		syslog(LOG_ERR, "app_create: pipe() failed with error: %s",
			strerror(errno));

	g_app->gpio_fd = rpi_gpio_init();

	return g_app;
}

int app_signal_write_fd(void) {

	return g_app->signal_fds[1];
}

int app_signal_read_fd(void) {

	return g_app->signal_fds[0];
}

int app_gpio_fd(void) {

	return g_app->gpio_fd;
}

audio_fifo_t *app_get_audio_fifo(void) {

	return &g_app->audio_fifo;
}

void app_set_session(sp_session *session) {

	if(g_app->session != NULL) {
		sp_session_player_play(g_app->session, 0);
		sp_session_player_unload(g_app->session);
		sp_session_release(g_app->session);
	}

	g_app->session = session;
}

void app_set_link(sp_link *link) {
	if(g_app->link != NULL)
		sp_link_release(g_app->link);

	g_app->link = link;
	if(g_app->link != NULL)
		sp_link_add_ref(g_app->link);
}

static void app_set_inbox(sp_session *session) {
	if(g_app->inbox) {
		playlist_monitor(g_app->inbox, 0);
		sp_playlist_release(g_app->inbox);
		g_app->inbox = NULL;
	}

	if(session) {
		g_app->inbox = sp_session_inbox_create(session);
		playlist_monitor(g_app->inbox, 1);
	}
}

static void app_set_starred(sp_session *session) {
	if(g_app->starred) {
		playlist_monitor(g_app->starred, 0);
		sp_playlist_release(g_app->starred);
		g_app->starred = NULL;
	}

	if(session) {
		g_app->starred = sp_session_starred_create(session);
		playlist_monitor(g_app->starred, 1);
		/* make available offline */
		sp_playlist_set_offline_mode(session, g_app->starred, 1);
	}
}

void app_set_track(sp_track *track) {

	if(g_app->track != NULL)
		sp_track_release(g_app->track);

	g_app->track = track;
	if(g_app->track != NULL)
		sp_track_add_ref(g_app->track);
}

sp_track *app_get_track(void) {

	return g_app->track;
}

static int app_playlist_is_special_kind(sp_playlist *pl) {
	if(pl == g_app->inbox || pl == g_app->starred)
		return 1;

	return 0;
}

/* Set playlist to select tracks from */
static sp_playlist *app_set_active_playlist(sp_playlist *pl) {
	if(g_app->active_playlist) {

		if(!app_playlist_is_special_kind(g_app->active_playlist)) {
			/* stop monitoring for track changes */
			playlist_monitor(g_app->active_playlist, 0);
		}

		sp_playlist_release(g_app->active_playlist);
		g_app->active_playlist = NULL;
	}

	if(pl == NULL)
		return NULL;

	sp_playlist_add_ref(pl);
	if(!app_playlist_is_special_kind(pl))
		playlist_monitor(pl, 1);

	g_app->active_playlist = pl;
	g_app->playlist_track_idx = 0;
	syslog(LOG_INFO, "App: Selected playlist '%s' with %d tracks (loaded: %d)",
		app_playlist_is_special_kind(pl)?
		"internal (inbox or starred)": sp_playlist_name(pl),
		sp_playlist_num_tracks(pl), sp_playlist_is_loaded(pl));

	return pl;
}

/* Public function to set playlist from sp_link* */
sp_playlist *app_set_active_playlist_link(sp_link *link) {
	sp_playlist *pl;

	pl = sp_playlist_create(g_app->session, link);
	return app_set_active_playlist(pl);
}

/* Advance to next track and start playing */
sp_track *app_do_next_track(void) {
	sp_track *track;
	sp_playlist *pl;
	int num_tracks;

	pl = g_app->active_playlist;
	if(pl == NULL) {
		syslog(LOG_WARNING, "App: Attempted 'next track' without an active playlist");
		return NULL;
	}

	num_tracks = sp_playlist_num_tracks(pl);
	if(num_tracks == 0) {
		syslog(LOG_WARNING, "App: Attempted 'next track' on %s playlist with zero tracks",
			sp_playlist_is_loaded(pl)? "loaded": "not yet loaded");
		return NULL;
	}

	track = app_get_track();
	if(track != NULL) {

		syslog(LOG_DEBUG,"App: A track is selected so advancing playlist_track_idx from %d to %d",
			g_app->playlist_track_idx, g_app->playlist_track_idx+1);

		++g_app->playlist_track_idx;
	}

	g_app->playlist_track_idx %= num_tracks;

	track = sp_playlist_track(pl, g_app->playlist_track_idx);
	app_set_track(track);
	syslog(LOG_NOTICE, "App: Selected next track (%d/%d) in playlist: %s",
			g_app->playlist_track_idx + 1, num_tracks,
			sp_track_name(track));

	/* Trigger events to check if track can actually be played */
	app_post_event(APP_WAIT_PLAY);
	app_post_event(APP_DO_METADATA);

	return track;
}

void app_release(void) {
	syslog(LOG_DEBUG, "App: Releasing link");
	app_set_link(NULL);

	syslog(LOG_DEBUG, "App: Releasing active track");
	app_set_track(NULL);

	syslog(LOG_DEBUG, "App: Releasing active playlist");
	app_set_active_playlist(NULL);

	syslog(LOG_DEBUG, "App: Releasing inbox playlist");
	app_set_inbox(NULL);

	syslog(LOG_DEBUG, "App: Releasing starred playlist");
	app_set_starred(NULL);

	syslog(LOG_DEBUG, "App: Releasing Spotify session resources");
	app_set_session(NULL);

	syslog(LOG_DEBUG, "App: Shutting down synchronization sockets");
	close(g_app->signal_fds[0]);
	close(g_app->signal_fds[1]);

	if(g_app->gpio_fd != -1)
		rpi_gpio_release(g_app->gpio_fd);

	free(g_app);
	g_app = NULL;
}

void app_do_metadata(void) {

	if(g_app->events & APP_WAIT_INBOX && sp_playlist_is_loaded(g_app->inbox)) {
		syslog(LOG_DEBUG, "App event: Inbox is loaded");
		g_app->events ^= APP_WAIT_INBOX;
	}

	if(g_app->events & APP_WAIT_STARRED && sp_playlist_is_loaded(g_app->starred)) {
		syslog(LOG_DEBUG, "App event: Starred is loaded");
		g_app->events ^= APP_WAIT_STARRED;

		/* Select this as active playlist */
		app_set_active_playlist(g_app->starred);
		app_do_next_track();
	}

	if(g_app->events & APP_WAIT_PLAY) {
		sp_track *track = app_get_track();
		sp_error error;

		if(track == NULL) {
			syslog(LOG_WARNING, "App player: APP_WAIT_PLAY but no track loaded. Call next track first!");
			g_app->events ^= APP_WAIT_PLAY;
			return;
		}

		if(sp_track_is_loaded(track) == 0) {
			syslog(LOG_INFO, "App player: Waiting for track to become available");
			return;
		}

		error = sp_track_error(track);
		if(error != SP_ERROR_OK) {
			syslog(LOG_INFO, "App player: Track '%s' loaded but unavailable: %s",
					sp_track_name(track), sp_error_message(error));

			g_app->events ^= APP_WAIT_PLAY;

			/* Advance to next track and re-post APP_DO_METADATA|APP_WAIT_PLAY */
			app_do_next_track();
			return;
		}

		error = sp_session_player_load(g_app->session, track);
		if(error != SP_ERROR_OK) {
			syslog(LOG_NOTICE, "App player: Loading of track '%s' failed with error: %s",
					sp_track_name(track), sp_error_message(error));

			g_app->events ^= APP_WAIT_PLAY;

			/* Advance to next track and re-post APP_DO_METADATA|APP_WAIT_PLAY */
			app_do_next_track();
			return;
		}

		app_post_event(APP_DO_PLAY);
		g_app->events ^= APP_WAIT_PLAY;
	}
}

int app_process_events(void) {
	app_event_t event;

	/* Process application events */
	while((event = app_next_event()) != APP_EVENT_NONE) {
		syslog(LOG_INFO, "App event: dequeued event %s", app_event_name(event));

		switch(event) {
		case APP_LOGGED_IN:
			/* Monitor rootlist for changes */
			playlistcontainer_monitor(g_app->session, 1);

			/* Load inbox playlist */
			app_set_inbox(g_app->session);
			g_app->events |= APP_WAIT_INBOX;

			/* Load starred playlist */
			app_set_starred(g_app->session);
			g_app->events |= APP_WAIT_STARRED;
			break;

		case APP_DO_LOGOUT:
			playlistcontainer_monitor(g_app->session, 0);
			sp_session_logout(g_app->session);
			break;

		case APP_DO_NEXT_TRACK:
			/* Advance to next track in active playlist.
			   This will also post APP_DO_METADATA|APP_WAIT_PLAY */
			app_do_next_track();
			break;

		case APP_DO_PLAY:
			/* It is assumed there's an active track and that
			   APP_WAIT_PLAY already did sp_session_player_loader() */
			sp_session_player_play(g_app->session, 0);
			player_stats_reset();
			sp_session_player_play(g_app->session, 1);

			syslog(LOG_NOTICE, "App player: starting playback of track: %02d. %s - %s",
					sp_track_index(app_get_track()),
					sp_track_name(app_get_track()),
					sp_artist_name(sp_track_artist(app_get_track(), 0)));


			break;
		case APP_DO_PREFETCH:
			{
				sp_track *track;
				sp_playlist *pl = g_app->active_playlist;
				int num_tracks = sp_playlist_num_tracks(pl);
				/* Attempt to prefetch next track */
				track = sp_playlist_track(pl, (g_app->playlist_track_idx + 1) % num_tracks);
				if(sp_track_error(track) == SP_ERROR_OK) {
					sp_error error;

					error = sp_session_player_prefetch(g_app->session, track);
					syslog(LOG_NOTICE, "App: Prefetching of track '%s' returned error: %s", sp_track_name(track), sp_error_message(error));
				}
			}
			break;

		case APP_DO_STOP:
			sp_session_player_play(g_app->session, 0);
			break;

		case APP_DO_METADATA:
			/* Periodic attempts to do something hooked onto metadata processing */
			if((g_app->events & (APP_WAIT_MAX-1)) == 0)
				break;

			app_do_metadata();
			break;

		case APP_DO_EXIT:
			/* Abort main loop */
			return -1;
			break;

		default:
			syslog(LOG_INFO, "App event: No handler for event %s", app_event_name(event));
			break;
		}
	}

	return 0;
}

void app_post_event(app_event_t event) {
	if(g_app->events & event) {
		syslog(LOG_INFO, "App event: re-queued event %s (0x%02x, events:0x%02x)",
			app_event_name(event), event, g_app->events);
		return;
	}

	g_app->events |= event;
	syslog(LOG_INFO, "App event: queued event %s (0x%02x, events:0x%02x)",
			app_event_name(event), event, g_app->events);
}

static app_event_t app_next_event(void) {
	app_event_t i, event;

	for(i = APP_MAX >> 1; g_app->events & (APP_MAX-1); i >>= 1) {

		if((event = g_app->events & i) == APP_EVENT_NONE)
			continue;

		g_app->events ^= event;

		return event;
	}

	return APP_EVENT_NONE;
}

const char *app_event_name(app_event_t event) {
	switch(event) {
	case APP_EVENT_NONE:
		return "APP_EVENT_NONE";

	case APP_LOGGED_IN:
		return "APP_LOGGED_IN";
	case APP_DO_NEXT_TRACK:
		return "APP_DO_NEXT_TRACK";
	case APP_DO_PLAY:
		return "APP_DO_PLAY";
	case APP_DO_PREFETCH:
		return "APP_DO_PREFETCH";
	case APP_DO_STOP:
		return "APP_DO_STOP";
	case APP_DO_METADATA:
		return "APP_DO_METADATA";
	case APP_DO_LOGOUT:
		return "APP_DO_LOGOUT";
	case APP_DO_EXIT:
		return "APP_DO_EXIT";
	case APP_MAX:
		return "APP_MAX";

	case APP_WAIT_INBOX:
		return "APP_WAIT_INBOX";
	case APP_WAIT_STARRED:
		return "APP_WAIT_STARRED";
	case APP_WAIT_PLAY:
		return "APP_WAIT_PLAY";
	case APP_WAIT_MAX:
		return "APP_WAIT_MAX";
	}

	return NULL;
}
