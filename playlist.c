/**
 * playlist.c
 * Playlist handling
 *
 */

#include <syslog.h>
#include <string.h>

#include "app.h"
#include "playlist.h"

static void pl_callback_tracks_added(sp_playlist *pl, sp_track *const *tracks, int num_tracks, int position, void *userdata);
static void pl_callback_tracks_removed(sp_playlist *pl, const int *tracks, int num_tracks, void *userdata);
static void pl_callback_tracks_moved(sp_playlist *pl, const int *tracks, int num_tracks, int new_position, void *userdata);
static void pl_callback_playlist_renamed(sp_playlist *pl, void *userdata);
static void pl_callback_playlist_state_changed(sp_playlist *pl, void *userdata);
static void pl_callback_playlist_update_in_progress(sp_playlist *pl, bool done, void *userdata);
static void pl_callback_playlist_metadata_updated(sp_playlist *pl, void *userdata);
static void pl_callback_track_message_changed(sp_playlist *pl, int position, const char *message, void *userdata);

static void pc_callback_playlist_added(sp_playlistcontainer *pc, sp_playlist *pl, int position, void *userdata);
static void pc_callback_playlist_removed(sp_playlistcontainer *pc, sp_playlist *pl, int position, void *userdata);
static void pc_callback_playlist_moved(sp_playlistcontainer *pc, sp_playlist *pl, int position, int new_position, void *userdata);
static void pc_callback_container_loaded(sp_playlistcontainer *pc, void *userdata);

static sp_playlistcontainer *app_pc;

void playlistcontainer_monitor(sp_session *session, int monitor) {
	sp_playlistcontainer_callbacks callbacks = {
		.playlist_added		= &pc_callback_playlist_added,
		.playlist_removed	= &pc_callback_playlist_removed,
		.playlist_moved = &pc_callback_playlist_moved,
		.container_loaded = &pc_callback_container_loaded
	};

	if(app_pc != NULL) {
		sp_playlistcontainer_remove_callbacks(app_pc, &callbacks, NULL);
		sp_playlistcontainer_release(app_pc);
		app_pc = NULL;
	}

	if(monitor) {
		app_pc = sp_session_playlistcontainer(session);
		sp_playlistcontainer_add_ref(app_pc);
		sp_playlistcontainer_add_callbacks(app_pc, &callbacks, NULL);
	}
}

void playlist_monitor(sp_playlist *playlist, int monitor) {
	sp_playlist_callbacks callbacks = {
		.tracks_added = &pl_callback_tracks_added,
		.tracks_removed = &pl_callback_tracks_removed,
		.tracks_moved = &pl_callback_tracks_moved,
		.playlist_renamed = &pl_callback_playlist_renamed,
		.playlist_state_changed = &pl_callback_playlist_state_changed,
		.playlist_update_in_progress = &pl_callback_playlist_update_in_progress,
		.playlist_metadata_updated = &pl_callback_playlist_metadata_updated,
		.track_message_changed = &pl_callback_track_message_changed
	};

	if(monitor)
		sp_playlist_add_callbacks(playlist, &callbacks, NULL);
	else
		sp_playlist_remove_callbacks(playlist, &callbacks, NULL);
}

static void pc_callback_playlist_added(sp_playlistcontainer *pc, sp_playlist *pl, int position, void *userdata) {

	syslog(LOG_DEBUG, "Playlist container: playlist %p of type %d inserted at position %d/%d",
			pl, sp_playlistcontainer_playlist_type(pc, position),
			position + 1, sp_playlistcontainer_num_playlists(pc));
}

static void pc_callback_playlist_removed(sp_playlistcontainer *pc, sp_playlist *pl, int position, void *userdata) {

	syslog(LOG_DEBUG, "Playlist container: playlist %p of type %d removed from position %d/%d",
			pl, sp_playlistcontainer_playlist_type(pc, position),
			position + 1, sp_playlistcontainer_num_playlists(pc));
}

static void pc_callback_playlist_moved(sp_playlistcontainer *pc, sp_playlist *pl, int position, int new_position, void *userdata) {

	syslog(LOG_DEBUG, "PL CONTAINER CALLBACK[%s]: playlist %p (%s) moved",
		__func__, pl, sp_playlist_name(pl));
}

static void pc_callback_container_loaded(sp_playlistcontainer *pc, void *userdata) {

	syslog(LOG_INFO, "Playlist container: loading completed, now have %d playlists",
			sp_playlistcontainer_num_playlists(pc));
}


static void pl_callback_tracks_added(sp_playlist *pl, sp_track *const *tracks, int num_tracks, int position, void *userdata) {

	syslog(LOG_DEBUG, "Playlist tracks added: %d tracks inserted at position %d in playlist: %s",
		num_tracks, position, sp_playlist_name(pl));

	/* resize */
	app_randomize_playlist_order();
}

static void pl_callback_tracks_removed(sp_playlist *pl, const int *tracks, int num_tracks, void *userdata) {

	syslog(LOG_DEBUG, "Playlist tracks removed: %d tracks removed in playlist: %s",
		num_tracks, sp_playlist_name(pl));

	/* resize */
	app_randomize_playlist_order();
}

static void pl_callback_tracks_moved(sp_playlist *pl, const int *tracks, int num_tracks, int new_position, void *userdata) {

	syslog(LOG_DEBUG, "Playlist tracks moved: %d tracks inserted at position %d in playlist: %s",
		num_tracks, new_position, sp_playlist_name(pl));
}

static void pl_callback_playlist_renamed(sp_playlist *pl, void *userdata) {

	syslog(LOG_DEBUG, "Playlist rename: pl:%p, new name: %s", pl, sp_playlist_name(pl));
}

static void pl_callback_playlist_state_changed(sp_playlist *pl, void *userdata) {

	syslog(LOG_DEBUG, "Playlist change: pl:%p, loaded:%d, collaborative:%d, tracks:%d, name:%s",
			pl, sp_playlist_is_loaded(pl), sp_playlist_is_collaborative(pl),
			sp_playlist_num_tracks(pl), sp_playlist_name(pl));
}

static void pl_callback_playlist_update_in_progress(sp_playlist *pl, bool done, void *userdata) {

	syslog(LOG_DEBUG, "Playlist update: pl:%p, done:%s, loaded:%d, collaborative:%d, tracks:%d, name:%s",
			pl, done? "yes": "no", sp_playlist_is_loaded(pl), sp_playlist_is_collaborative(pl),
			sp_playlist_num_tracks(pl), sp_playlist_name(pl));
}

static void pl_callback_playlist_metadata_updated(sp_playlist *pl, void *userdata) {

	syslog(LOG_DEBUG, "Playlist metadata: pl:%p, loaded:%d, collaborative:%d, tracks:%d, name:%s",
			pl, sp_playlist_is_loaded(pl), sp_playlist_is_collaborative(pl),
			sp_playlist_num_tracks(pl), sp_playlist_name(pl));

	app_post_event(APP_DO_METADATA);
}

static void pl_callback_track_message_changed(sp_playlist *pl, int position, const char *message, void *userdata) {
	sp_track *track;
	const char *title = "[not yet loaded]";

	track = sp_playlist_track(pl, position);
	if(sp_track_is_loaded(track))
		title = sp_track_name(track);

	syslog(LOG_DEBUG, "Playlist track message: track %d/%d with title '%s' in playlist '%s' has message: %s",
			position+1, sp_playlist_num_tracks(pl), title, sp_playlist_name(pl), message);
}
