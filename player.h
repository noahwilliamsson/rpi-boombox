/**
 *
 */

#ifndef PLAYER_H
#define PLAYER_H

#include <libspotify/api.h>

int player_callback_frame_delivery(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames);
void player_callback_end_of_track(sp_session *session);
void player_callback_playtoken_lost(sp_session *session);
void player_callback_start_playback(sp_session *session);
void player_callback_stop_playback(sp_session *session);
void player_callback_get_audio_buffer_stats(sp_session *session, sp_audio_buffer_stats *stats);
void player_stats_reset(void);

#endif
