#ifndef ROBOT_TCP_H
#define ROBOT_TCP_H

/*
 * TCP 传输层封装。
 * 除 tcp_connect_to_server 返回 fd、tcp_recv_some 返回字节数外，
 * 其余函数均以 0 表示成功、-1 表示失败（失败时已打印原因）。
 */
#include <stddef.h>

/* 连接机器人，成功返回 fd 并开启 TCP_NODELAY / SO_KEEPALIVE；失败返回 -1 */
int  tcp_connect_to_server(const char *ip, int port);

/* 循环发送直到 len 字节全部写完，处理部分写与 EINTR；成功返回 0 */
int  tcp_send_all(int fd, const void *buf, size_t len);

/*
 * 接收一次数据：>0 为字节数；0 表示本次可忽略（被信号打断）；
 * -1 表示对端关闭或出错，调用方应结束接收循环。
 */
int  tcp_recv_some(int fd, void *buf, size_t len);

/* 关闭 Nagle 算法，小包（20 字节控制帧）即时发出，降低周期抖动 */
int  tcp_set_no_delay(int fd);

/* 开启 TCP 保活，用于感知对端异常掉线 */
int  tcp_set_keepalive(int fd);

/* 优雅关闭：先 shutdown 唤醒对端阻塞读，再 close；fd < 0 时安全 */
int  tcp_close(int fd);

#endif
