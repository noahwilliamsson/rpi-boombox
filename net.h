/**
 * net.h
 *
 */

#ifndef NET_H
#define NET_H

/* Accept commands on TCP port 1234 */
#define CTRL_TCP_PORT 1234

int net_create(int port);
int net_poll(int listen_fd, int timeout);
void net_release(int fd);

#endif
