#include "robot_tcp.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

int tcp_connect_to_server(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        close(fd);
        return -1;
    }

    printf("Connecting to %s:%d ...\n", ip, port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    printf("Connected.\n");
    tcp_set_no_delay(fd);
    tcp_set_keepalive(fd);
    return fd;
}

int tcp_send_all(int fd, const void *buf, size_t len)
{
    const char *ptr = (const char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("send");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "send: connection closed by peer\n");
            return -1;
        }
        ptr      += n;
        remaining -= (size_t)n;
    }
    return 0;
}

int tcp_recv_some(int fd, void *buf, size_t len)
{
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EINTR) return 0;
        perror("recv");
        return -1;
    }
    if (n == 0) {
        fprintf(stderr, "recv: connection closed by peer\n");
        return -1;
    }
    return (int)n;
}

int tcp_set_no_delay(int fd)
{
    int opt = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

int tcp_set_keepalive(int fd)
{
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
}

int tcp_close(int fd)
{
    if (fd < 0) return 0;
    shutdown(fd, SHUT_RDWR);
    return close(fd);
}
