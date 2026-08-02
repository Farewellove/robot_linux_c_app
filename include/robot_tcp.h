#ifndef ROBOT_TCP_H
#define ROBOT_TCP_H

#include <stddef.h>

int  tcp_connect_to_server(const char *ip, int port);
int  tcp_send_all(int fd, const void *buf, size_t len);
int  tcp_recv_some(int fd, void *buf, size_t len);
int  tcp_set_no_delay(int fd);
int  tcp_set_keepalive(int fd);
int  tcp_close(int fd);

#endif
