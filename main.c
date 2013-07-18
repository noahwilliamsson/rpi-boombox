/**
 * Example automatic playback of "starred" playlist
 * Tested on OS X and Raspberry Pi (raspbian)
 * --
 *
 * OSX:
 * $ curl -LO http://developer.spotify.com/download/libspotify/libspotify-12.1.51-Darwin-universal.zip
 * $ unzip libspotify-12.1.51-Darwin-universal.zip
 * $ cd libspotify-12.1.51-Darwin-universal
 * $ tar c libspotify.framework | sudo tar xv -C /Library/Frameworks
 *
 * Raspbian:
 * $ curl -LO http://developer.spotify.com/download/libspotify/libspotify-12.1.103-Linux-armv6-bcm2708hardfp-release.tar.gz
 * $ tar zxf libspotify-12.1.103-Linux-armv6-bcm2708hardfp-release.tar.gz
 * $ cd libspotify-12.1.103-Linux-armv6-bcm2708hardfp-release
 * $ sudo make install prefix=/usr
 * $ sudo apt-get -y install libopenal-dev
 *
 * BUILDING
 * $ make
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>

#include <libspotify/api.h>

#include "app.h"
#include "net.h"
#include "player.h"


#define LIBSPOTIFY_USERAGENT "pi-boombox"
#define LIBSPOTIFY_CACHE_DIR "./tmp"
#define LIBSPOTIFY_AUTH_BLOB LIBSPOTIFY_CACHE_DIR "/libspotify.creds"


static char blob[1024];
pthread_t thread_main;
static const unsigned char g_appkey[] = {
	/* despotify */
	0x01, 0x56, 0xFC, 0x14, 0x35, 0x86, 0x20, 0xF1, 0x69, 0xC6, 0x9B, 0x7C, 0xD3, 0x11, 0xAB, 0x56,
	0x3E, 0x1F, 0xF3, 0xB1, 0x58, 0xD4, 0x07, 0xF3, 0x51, 0xCF, 0xC1, 0x1D, 0xF8, 0xCF, 0x49, 0x73,
	0x9F, 0xFC, 0x66, 0x02, 0xA1, 0xCE, 0x82, 0x08, 0xBE, 0xF3, 0x89, 0xAA, 0xBD, 0x75, 0x42, 0x19,
	0x60, 0x45, 0xBF, 0x39, 0x70, 0x8C, 0x6E, 0xA9, 0x37, 0xE1, 0x5B, 0x54, 0xD9, 0x29, 0x1D, 0xEE,
	0xBF, 0x2B, 0x11, 0xD2, 0xF0, 0x28, 0xF3, 0xD4, 0x1D, 0x26, 0x99, 0xA6, 0x8A, 0xC8, 0xA8, 0xAE,
	0xC1, 0x98, 0x87, 0x4B, 0x4A, 0xB9, 0xD6, 0x6A, 0x90, 0x51, 0xA0, 0x4D, 0x4D, 0xA5, 0xCB, 0x66,
	0xC8, 0x5D, 0x3F, 0xE8, 0x1B, 0x6E, 0x22, 0xFF, 0x4F, 0xA5, 0x5C, 0x06, 0x14, 0x25, 0xD0, 0x74,
	0xBD, 0x81, 0x48, 0xDE, 0x47, 0x69, 0x4D, 0xF4, 0xE5, 0x6E, 0xB8, 0x26, 0x3B, 0x06, 0xFE, 0x0D,
	0x84, 0x55, 0x3F, 0x37, 0x67, 0x11, 0x14, 0xF3, 0x4A, 0x17, 0xC0, 0x50, 0x9D, 0x48, 0x9D, 0x95,
	0x93, 0xB4, 0x27, 0xB6, 0x27, 0x51, 0x99, 0xCA, 0xA7, 0xB3, 0xE9, 0x1C, 0x3B, 0x89, 0x2A, 0xE7,
	0x18, 0xFF, 0xF6, 0xB6, 0xAE, 0xB2, 0x17, 0x5A, 0x33, 0x61, 0x08, 0x9D, 0xE3, 0x03, 0xFD, 0x7D,
	0x12, 0x68, 0x24, 0x6D, 0xCF, 0x6F, 0xA8, 0x87, 0x06, 0x27, 0xED, 0x4A, 0xB7, 0x13, 0x23, 0xAA,
	0x62, 0xA2, 0x21, 0xC0, 0x0E, 0x2F, 0xF3, 0x47, 0x1D, 0xFD, 0x3D, 0x06, 0x10, 0x7D, 0xA2, 0xFB,
	0x63, 0xF9, 0x04, 0x20, 0x20, 0xE7, 0x28, 0x6B, 0x6F, 0xD6, 0x7A, 0x61, 0x33, 0x76, 0x2A, 0xA4,
	0x3E, 0xEE, 0x40, 0xE8, 0x07, 0x99, 0xDA, 0xEA, 0x63, 0x65, 0x21, 0x22, 0x30, 0x0A, 0xF1, 0xD5,
	0x46, 0xAA, 0x8C, 0x06, 0x57, 0xB7, 0xB4, 0x8A, 0xDE, 0xFE, 0xA9, 0xB8, 0xA3, 0x03, 0xF0, 0xDB,
	0x4C, 0x38, 0xC0, 0x57, 0xC1, 0x47, 0xBD, 0xC7, 0x24, 0x7E, 0xBB, 0x37, 0xD2, 0xFA, 0x4D, 0x5F,
	0x03, 0x23, 0xC6, 0x53, 0xD9, 0x43, 0xCA, 0xDF, 0x84, 0x72, 0x1A, 0x06, 0xF1, 0x93, 0xAB, 0x2A,
	0x52, 0xAB, 0xEB, 0x79, 0x9F, 0x74, 0xBF, 0xE7, 0xAC, 0x95, 0xCB, 0x63, 0xCE, 0x18, 0x08, 0x99,
	0x19, 0x17, 0x36, 0x9D, 0x9C, 0x7E, 0x82, 0xDC, 0x83, 0xDC, 0xA8, 0x8D, 0x30, 0x2D, 0xF4, 0xC7,
	0xD6,
};


static char *get_auth_blob() {
	size_t len;
	FILE *fd;

	fd = fopen(LIBSPOTIFY_AUTH_BLOB, "r");
	if(fd == NULL)
		return NULL;

	memset(blob, 0, sizeof(blob));
	len = fread(blob, 1, sizeof(blob), fd);
	fclose(fd);
	if(len <= 0)
		return NULL;

	return blob;
}

static const char *update_auth_blob(const char *token) {
	FILE *fd;
	size_t n_written;

	fd = fopen(LIBSPOTIFY_AUTH_BLOB, "w");
	if(fd == NULL)
		return NULL;

	strcpy(blob, token);
	n_written = fwrite(blob, 1, strlen(token), fd);
	fclose(fd);

	if(n_written != strlen(token))
		return NULL;

	return token;
}

static void sess_callback_credentials_blob_updated(sp_session *session, const char *token) {

	syslog(LOG_INFO, "AUTH: credentials blob updated for %s: %s",
			sp_session_user_name(session), token);
	update_auth_blob(token);
}

static void sess_callback_logged_in(sp_session *session, sp_error error) {
	sp_user *user;

	if(error != SP_ERROR_OK) {
		syslog(LOG_ERR, "Session: Login failed with error: %s", sp_error_message(error));

		/* The logout callback is called automatically in libspotify 12 */
		return;
	}

	user = sp_session_user(session);
	if(user != NULL) {
		syslog(LOG_INFO, "Session: Successfully logged in as user %s <%s>",
			sp_user_display_name(user), sp_user_canonical_name(user));
	}

	/* Notify app of login to hook up rootlist callbacks */
	app_post_event(APP_LOGGED_IN);
}


static void sess_callback_logged_out(sp_session *session) {

	syslog(LOG_INFO, "Session: Logged out from Spotify");

	/* Signal exit of main loop */
	app_post_event(APP_DO_EXIT);
}

static void sess_callback_metadata_updated(sp_session *session) {

	syslog(LOG_DEBUG, "Session: New metadata available");

	/* Do periodic processing when new metadata is available */
	app_post_event(APP_DO_METADATA);
}

/* May be called from an internal thread */
static void sess_callback_notify(sp_session *session) {

	/* Write anything to piped fd in order to make net_poll()
	 * return so that sp_session_process_events() is called */
	write(app_signal_write_fd(), "NOTIFY", 6);
	syslog(LOG_DEBUG, "Session: Event processing requested by %s thread",
			pthread_equal(pthread_self(), thread_main)? "the main": "an internal");
}

static void sess_callback_log_message(sp_session *session, const char *data) {

	syslog(LOG_DEBUG, "libspotify: %s", data);
}

static void sess_callback_message_to_user(sp_session *session, const char *message) {

	syslog(LOG_NOTICE, "Session: Message to user: %s", message);
}

static void sess_callback_connectionstate_updated(sp_session *session) {
	char *state;
	switch(sp_session_connectionstate(session)) {
	case SP_CONNECTION_STATE_LOGGED_OUT:
		state = "logged out";
		break;
	case SP_CONNECTION_STATE_LOGGED_IN:
		state = "logged in";
		break;
	case SP_CONNECTION_STATE_DISCONNECTED:
		state = "disconnected";
		break;
	case SP_CONNECTION_STATE_UNDEFINED:
		state = "undefined";
		break;
	case SP_CONNECTION_STATE_OFFLINE:
		state = "offline";
		break;
	}

	syslog(LOG_NOTICE, "%s: Connection state changed to: %s", __func__, state);
}

static void sess_callback_offline_status_updated(sp_session *session) {

	syslog(LOG_INFO, "Session: offline status updated, %d playlists marked for offline usage, %d tracks left to sync, need to go online in %dd%02dh",
		sp_offline_tracks_to_sync(session),
		sp_offline_num_playlists(session),
		sp_offline_time_left(session) / 86400,
		sp_offline_time_left(session) % 3600);

}

int mainloop(sp_session *session, int listen_fd) {
	int event, timeout;
	int loops;
	event = 0;
	do {

		syslog(LOG_DEBUG, "EVENTLOOP [id %d]: Processing Spotify events", event);
		loops = 0;
		do {
			sp_session_process_events(session, &timeout);
			loops++;
		} while(timeout == 0);
		syslog(LOG_DEBUG, "EVENTLOOP [id %d]: Done processing %d Spotify events, next timeout %dms", event, loops, timeout);

		if(app_process_events() < 0) {
			syslog(LOG_INFO, "EVENTLOOP [id %d]: app_process_events() failed", event);
			break;
		}

		if(net_poll(listen_fd, timeout) < 0) {
			syslog(LOG_INFO, "EVENTLOOP [id %d]: net_poll() failed", event);
			break;
		}

		event++;
	} while(1);

	return 0;
}

int main(int argc, char **argv) {
	int listen_fd;
	sp_session *session;
	static sp_session_config config;
	static sp_session_callbacks callbacks = {
		.logged_in		= &sess_callback_logged_in,
		.logged_out		= &sess_callback_logged_out,
		.metadata_updated	= &sess_callback_metadata_updated,
		.connection_error	= NULL,
		.message_to_user	= &sess_callback_message_to_user,
		.notify_main_thread	= &sess_callback_notify,
		.music_delivery		= &player_callback_frame_delivery,
		.play_token_lost	= &player_callback_playtoken_lost,
		.log_message		= &sess_callback_log_message,
		.end_of_track		= &player_callback_end_of_track,
		.streaming_error	= NULL,
		.userinfo_updated	= NULL,
		.start_playback		= &player_callback_start_playback,
		.stop_playback		= &player_callback_stop_playback,
		.get_audio_buffer_stats	= &player_callback_get_audio_buffer_stats,
		// libspotify 10
		.offline_status_updated	= &sess_callback_offline_status_updated,
		.offline_error		= NULL,
		// libspotify 11
		.credentials_blob_updated	= sess_callback_credentials_blob_updated,
		// libspotify 12
		.connectionstate_updated	= &sess_callback_connectionstate_updated,
		.scrobble_error			= NULL,
		.private_session_mode_changed	= NULL,
	};

	thread_main = pthread_self();

	/* Setup logging to stderr */
	openlog(LIBSPOTIFY_USERAGENT, LOG_PERROR, LOG_USER);

	/**
	 * Filter logging with one of these 
	 *
	setlogmask(LOG_UPTO(LOG_WARNING));
	setlogmask(LOG_UPTO(LOG_NOTICE));
	setlogmask(LOG_UPTO(LOG_INFO));
	 */
	setlogmask(LOG_UPTO(LOG_DEBUG));

	config.api_version = SPOTIFY_API_VERSION;
	config.cache_location = LIBSPOTIFY_CACHE_DIR;
	config.settings_location = LIBSPOTIFY_CACHE_DIR;
	config.application_key = g_appkey;
	config.application_key_size = sizeof(g_appkey);
	config.user_agent = LIBSPOTIFY_USERAGENT;
	config.callbacks = &callbacks;
	config.userdata = app_create();
	config.compress_playlists = 1;
	config.dont_save_metadata_for_playlists = 0;
	config.initially_unload_playlists = 0;
	config.device_id = NULL;

	syslog(LOG_DEBUG, "MAIN: Initializing libspotify");
	if(sp_session_create(&config, &session) != SP_ERROR_OK) {
		syslog(LOG_ERR, "MAIN: Unable to initialize libspotify");
		app_release();
		return -1;
	}

	app_set_session(session);
	if(argc == 4) {
		sp_link *link = sp_link_create_from_string(argv[3]);
		app_set_link(link);
	}

	/* This program will be run on mobile internet connections */
	sp_session_set_connection_type(session, SP_CONNECTION_TYPE_MOBILE_ROAMING);
	sp_session_set_connection_rules(session, SP_CONNECTION_RULE_NETWORK|SP_CONNECTION_RULE_NETWORK_IF_ROAMING);
	sp_session_preferred_offline_bitrate(session, SP_BITRATE_160k, 0);

	/* No point in sharing usage from this program */
	sp_session_set_private_session(session, 1);

	if(argc < 2) {
		char username[256];

		if(sp_session_remembered_user(session, username, sizeof(username)) > 0)
			syslog(LOG_DEBUG, "MAIN: Attempting to login using stored credentials for user '%s'", username);

		if(sp_session_relogin(session) == SP_ERROR_NO_CREDENTIALS) {
			syslog(LOG_ERR, "MAIN: No credentials stored. Please run: %s <username> <password>", argv[0]);
			app_release();
			return -1;
		}
	}
	else {
		syslog(LOG_DEBUG, "MAIN: Attempting to login using command line credentials");
		sp_session_login(session, argv[1], argc == 3? argv[2]: NULL, 1, get_auth_blob());
	}

	if((listen_fd = net_create(CTRL_TCP_PORT)) < 0) {
		syslog(LOG_ERR, "MAIN: Failed to initialize external network");
		app_release();
		return -1;
	}

	mainloop(session, listen_fd);
	syslog(LOG_INFO, "MAIN: Outside main event loop, good bye!");

	net_release(listen_fd);
	app_release();

	return 0;
}
