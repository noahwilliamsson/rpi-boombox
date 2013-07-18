/**
 * net.c
 * Network handling - currently only accepts 'next' and 'logout' on port 1234
 *
 */

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <syslog.h>
#include <errno.h>

#include "app.h"

static int client_fd;

static int net_accept_client(int listen_fd);
static int net_read_data(int fd);

int net_create(int port) {
	int fd;
	int opt;
	struct sockaddr_in sin;

	client_fd = -1;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = PF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);

	if(bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		syslog(LOG_ERR, "NET: Failed to bind to port %d: %s", ntohs(sin.sin_port), strerror(errno));
		close(fd);
		return -1;
	}

	if(listen(fd, 1) < 0) {
		syslog(LOG_ERR, "NET: Failed to listen on fd %d: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	syslog(LOG_NOTICE, "NET: Created listening fd %d", fd);

	return fd;
}

int net_poll(int listen_fd, int timeout) {
	int i, nfds = 0, ret;
	struct pollfd fdset[4];


	memset(fdset, 0, sizeof(fdset));
	fdset[nfds].fd = listen_fd;
	fdset[nfds].events = POLLIN;
	nfds++;

	fdset[nfds].fd = app_signal_read_fd();
	fdset[nfds].events = POLLIN;
	nfds++;

	fdset[nfds].fd = app_gpio_fd();
	if(fdset[nfds].fd != -1) {
		fdset[nfds].events = POLLPRI;
		nfds++;
	}

	if(client_fd > 0) {
		fdset[nfds].fd = client_fd;
		fdset[nfds].events = POLLIN;
		nfds++;
	}

	if((ret = poll(fdset, nfds, timeout)) < 0) {

		return 0;
	}
	else if(ret < 0 && errno != EINTR && errno != EAGAIN) {
		syslog(LOG_ERR, "NET: poll() returned error: %s", strerror(errno));

		return -1;
	}

	for(i = 0; i < nfds; i++) {
		if(fdset[i].revents & POLLPRI) {
			syslog(LOG_DEBUG, "NET: POLLPRI on fd %d", fdset[i].fd);
			if(fdset[i].fd == app_gpio_fd()) {
				lseek(fdset[i].fd, 0, SEEK_SET);
				net_read_data(fdset[i].fd);
			}
		}
		else if(fdset[i].revents & POLLIN) {
			syslog(LOG_DEBUG, "NET: POLLIN on fd %d", fdset[i].fd);
			if(fdset[i].fd == listen_fd)
				ret = net_accept_client(fdset[i].fd);
			else
				ret = net_read_data(fdset[i].fd);

			if(ret < 0) {
				syslog(LOG_DEBUG, "NET: Socket %d shutdown", fdset[i].fd);
				close(fdset[i].fd);
				if(fdset[i].fd == client_fd)
					client_fd = -1;
			}
		}
	}

	return 0;
}

static int net_accept_client(int listen_fd) {
	struct sockaddr_in sin;
	socklen_t sin_len = sizeof(struct sockaddr_in);

	client_fd = accept(listen_fd, (struct sockaddr *)&sin, &sin_len);
	if(client_fd < 0) {
		syslog(LOG_WARNING, "NET: Failed to accept() on fd %d: %s", listen_fd, strerror(errno));
		return -1;
	}

	syslog(LOG_INFO, "NET: Accepted client on fd %d, local address %s:%d", client_fd, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

	return 0;
}

static int net_write_string(int fd, char *buf) {
	size_t len, n;
	char *p;

	len = strlen(buf);
	p = buf;
	while(len) {
		n = write(fd, p, len);
		if(n <= 0)
			return -1;

		len -= n;
		p += n;
	}

	return 0;
} 

static int net_read_data(int fd) {
	char buf[1024], *p;
	size_t n;

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	syslog(LOG_DEBUG, "NET: Received %zd bytes from fd %d: '%s'", n, fd, buf);
	if(n <= 0)
		return -1;

	/* Remove trailing whitespace */
	for(p = buf; *p && *p != ' ' && *p != '\r' && *p != '\n'; p++);
	*p = 0;

	/* Remove leading whitespace */
	for(p = buf; *p && (*p == ' ' || *p == '\r' || *p == '\n'); p++);

	if(strstr(p, "spotify:") || strstr(p, "http://open.spotify.com")) {
		sp_link *link;


		link = sp_link_create_from_string(p);
		if(!link) {
			syslog(LOG_WARNING, "NET: Not a Spotify URI '%s'", p);
			return net_write_string(fd, "# ERR, not a Spotify URI\n");
		}

		switch(sp_link_type(link)) {
		case SP_LINKTYPE_INVALID:
			syslog(LOG_WARNING, "NET: Invalid Spotify URI '%s'", p);
			net_write_string(fd, "# ERR, invalid Spotify URI\n");
			break;
		case SP_LINKTYPE_PLAYLIST:
		case SP_LINKTYPE_STARRED:
			syslog(LOG_NOTICE, "NET: Spotify URI is of type playlist");
			if(app_set_active_playlist_link(link)) {
				app_set_track(NULL);
				net_write_string(fd, "# OK, playlist is now the active playlist\n");
			}
			else
				net_write_string(fd, "# ERR, failed to make playlist active\n");
			break;
		default:
			syslog(LOG_NOTICE, "NET: Unhandled Spotify URI with type: %d", sp_link_type(link));
			net_write_string(fd, "# ERR, only URIs of type playlist suppported\n");
			break;
		}

		sp_link_release(link);
	}
	else if(!strcmp(p, "next") || !strcmp(p, "0") /* GPIO */) {
		app_post_event(APP_DO_NEXT_TRACK);

		if(fd != app_gpio_fd())
			return net_write_string(fd, "# OK, playing next track\n");
	}
	else if(!strcmp(p, "play")) {
		app_post_event(APP_DO_PLAY);
		return net_write_string(fd, "# OK, starting playback\n");
	}
	else if(!strcmp(p, "stop")) {
		app_post_event(APP_DO_STOP);
		return net_write_string(fd, "# OK, stopping playback\n");
	}
	else if(!strcmp(p, "logout")) {
		app_post_event(APP_DO_LOGOUT);
		return net_write_string(fd, "# OK, logging out and exiting\n");
	}
	else if(fd != app_signal_read_fd() && fd != app_gpio_fd())
		return net_write_string(fd, "# ERR, unsupported command");

	return 0;
}

void net_release(int fd) {

	if(client_fd != -1) {
		close(client_fd);
		client_fd = -1;
	}

	close(fd);
}

