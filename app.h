/**
 * app.h
 *
 */

#ifndef APP_H
#define APP_H

#include <libspotify/api.h>

#include "audio.h"

typedef enum {
	APP_EVENT_NONE	= 0,

	/* For event processing in app_process_events() */
	APP_LOGGED_IN	= 0x01,
	APP_DO_METADATA	= 0x02,
	APP_DO_NEXT_TRACK = 0x04,
	APP_DO_PLAY	= 0x08,
	APP_DO_PREFETCH	= 0x10,
	APP_DO_STOP	= 0x20,
	APP_DO_LOGOUT	= 0x40,
	APP_DO_EXIT	= 0x80,
	APP_MAX	= 0x100,

	/* For metadata processing; these are handled using APP_DO_METADATA */
	APP_WAIT_INBOX	= 0x01000,
	APP_WAIT_STARRED	= 0x02000,
	APP_WAIT_PLAY		= 0x08000,
	APP_WAIT_MAX		= 0x010000,
} app_event_t;

void *app_create(void);
int app_signal_write_fd(void);
int app_signal_read_fd(void);
int app_gpio_fd(void);

audio_fifo_t *app_get_audio_fifo(void);
void app_set_session(sp_session *session);
void app_set_link(sp_link *link);

void app_set_track(sp_track *track);
sp_track *app_get_track(void);
sp_playlist *app_set_active_playlist_link(sp_link *link);
void app_randomize_playlist_order(void);
sp_track *app_do_next_track(void);
void app_release(void);
void app_post_event(app_event_t event);
int app_process_events(void);

#endif
