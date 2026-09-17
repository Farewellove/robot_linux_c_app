/*
 * TCP 传输层封装：把 socket 相关系统调用包成一组简单接口，
 * 让上层只关心"连上没连上""发完没发完"，不直接碰 errno/sockaddr。
 *
 * 约定：
 *   返回 -1 表示出错（已打印原因），返回 >=0 表示成功；
 *   tcp_close 幂等，传 -1 也安全。
 */
#include "robot_tcp.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/*
 * 建立到机器人的 TCP 连接。
 * 流程：socket → inet_pton 填地址 → connect；
 * 成功后顺手打开 TCP_NODELAY 与 SO_KEEPALIVE（见下），
 * 失败路径均 close(fd) 后再返回 -1，不留泄漏的描述符。
 */
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
    addr.sin_port   = htons((uint16_t)port);   /* 端口需转网络字节序 */

    /* inet_pton 比 inet_addr 多了错误返回，且支持严格格式校验 */
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

/*
 * 循环发送直到 len 字节全部写完。
 *
 * TCP 的 send 只保证"尽力写入发送缓冲"，返回值可能小于请求长度（部分写），
 * 20 字节的控制帧虽小也必须循环，否则会在网络拥塞时丢字节导致对端错帧。
 *   EINTR  —— 被信号打断，重试即可；
 *   MSG_NOSIGNAL —— 对端已关闭时返回 EPIPE 而不是触发 SIGPIPE 杀进程。
 */
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

/*
 * 接收一次数据，返回实际读到的字节数。
 *
 * 刻意不做"收满 len 字节"的保证 —— TCP 是字节流，一次 recv 可能只拿到半帧、
 * 也可能一次拿到好几帧，帧边界的拼接交给上层的粘包状态机处理。
 * 返回值语义特殊：>0 读到的字节数；0 本次可忽略（被信号打断，非错误）；
 *                     -1 连接已断开或出错，调用方应退出循环。
 */
int tcp_recv_some(int fd, void *buf, size_t len)
{
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EINTR) return 0;
        perror("recv");
        return -1;
    }
    if (n == 0) {
        /* recv 返回 0 表示对端正常关闭（四次挥手完成） */
        fprintf(stderr, "recv: connection closed by peer\n");
        return -1;
    }
    return (int)n;
}

/*
 * 关闭 Nagle 算法：小包立即发出，不再攒够 MSS 或等到 ACK 再发。
 * 控制帧只有 20 字节且要求周期稳定（默认 10ms 一帧），
 * 若开启 Nagle 会和延迟 ACK 相互等待，造成数十毫秒的抖动。
 */
int tcp_set_no_delay(int fd)
{
    int opt = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

/*
 * 开启 TCP 保活：连接空闲时由内核定期发探测包，
 * 用于察觉对端异常掉线（拔网线/断电）而不是一直傻等。
 */
int tcp_set_keepalive(int fd)
{
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
}

/*
 * 关闭连接：先 shutdown 通知对端不再收发（发 FIN），再 close 释放描述符。
 * 多线程场景下 shutdown 能让阻塞在 recv 的线程立即返回，
 * 比直接 close 更安全 —— 直接 close 不会唤醒阻塞中的对端读取。
 */
int tcp_close(int fd)
{
    if (fd < 0) return 0;
    shutdown(fd, SHUT_RDWR);
    return close(fd);
}
