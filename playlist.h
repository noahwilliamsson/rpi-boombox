/**
 * playlist.h
 *
 */

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <libspotify/api.h>

void playlistcontainer_monitor(sp_session *session, int monitor);
void playlist_monitor(sp_playlist *playlist, int monitor);

#endif
