/*
 * mock_robot —— 本机 loopback 模拟机器人 TCP 服务端
 *
 * 用途：在没有真实机器人的情况下，为 robot_linux_app 提供可复现的端到端对端，
 * 用于验证：
 *   1) 上位机下行控制帧格式与序列号连续性（服务端逐帧校验 0x77 头 / 0x0A 尾 / seq 递增）；
 *   2) 上位机上行粘包解析（服务端按指定速率推送 UWB/CAR 状态帧）；
 *   3) 断线检测行为（--close-after N 触发 RST 或 FIN，观察上位机是否及时退出）。
 *
 * 重要边界：这是 PC 上的离线 TCP 模拟，不是真实机器人闭环控制测试。
 *
 * 选项：
 *   -p <port>          监听端口（默认 19090）
 *   --period <ms>      状态帧推送周期（默认 10ms，即每周期推 1 帧）
 *   --frames <n>       最多推送 n 帧后停止（0 = 不限）
 *   --silent           只接收不发送（模拟"对端静默"，用于测上位机退出时序）
 *   --close-after <n>  推送到第 n 帧后主动断开连接（0 = 不断开）
 *   --rst              断开时使用 SO_LINGER=0 发送 RST（默认优雅 FIN）
 *   --accept-timeout <s>  等待上位机连接的秒数（默认 20）
 *   --csv <path>       校验结果 CSV 输出路径
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define CTRL_FRAME_LEN 20
#define STAT_FRAME_LEN 20

static volatile sig_atomic_t g_stop = 0;

static void on_sig(int s) { (void)s; g_stop = 1; }

static int ensure_parent_dir(const char *path)
{
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return 0;
    *slash = '\0';

    /* 目录已存在时必须视为成功：mkdir() 对已存在目录返回 -1/EEXIST */
    struct stat st;
    if (stat(tmp, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(tmp, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static void put_i32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void put_f32(uint8_t *p, float v)   { memcpy(p, &v, 4); }
static void put_i16(uint8_t *p, int16_t v) { memcpy(p, &v, 2); }

/* 生成第 i 帧状态数据：偶数 UWB，奇数车体状态 */
static void build_status_frame(uint8_t *b, int i)
{
    memset(b, 0, STAT_FRAME_LEN);
    if (i % 2 == 0) {
        b[0] = 'u';
        b[1] = 'p';
        put_i32(b + 2, i);
        put_f32(b + 6,  (float)i * 0.1f);
        put_f32(b + 10, (float)i * -0.2f);
        put_f32(b + 14, 1.5f);
    } else {
        b[0] = 'c';
        b[1] = (uint8_t)(int8_t)(i % 100);
        b[2] = (uint8_t)(int8_t)(-(i % 100));
        b[3] = 0;
        put_i16(b + 4, (int16_t)(i % 1000));
        put_i16(b + 6, (int16_t)(i % 200));
        put_f32(b + 8, (float)i * 0.01f);
        put_i16(b + 12, (int16_t)(i % 1000));
        put_i16(b + 14, 0);
        put_i16(b + 16, 0);
    }
    b[18] = 0x55;
    b[19] = 0x56;
}

int main(int argc, char *argv[])
{
    int port = 19090;
    int period_ms = 10;
    long frames = 0;          /* 0 = 不限 */
    int silent = 0;
    long close_after = 0;
    int use_rst = 0;
    int accept_timeout_s = 20;
    const char *csv_path = "performance_evaluation/results/03_mock_server.csv";

    /*
     * 绑定地址：默认 0.0.0.0，这样开发板能通过网络连进来（跨机测试必需）。
     * 仅本机自测时可用 -b 127.0.0.1 收紧监听范围。
     */
    const char *bind_ip = "0.0.0.0";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc)              port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc)         bind_ip = argv[++i];
        else if (!strcmp(argv[i], "--period") && i + 1 < argc)   period_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)   frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--silent"))                   silent = 1;
        else if (!strcmp(argv[i], "--close-after") && i + 1 < argc) close_after = atol(argv[++i]);
        else if (!strcmp(argv[i], "--rst"))                      use_rst = 1;
        else if (!strcmp(argv[i], "--accept-timeout") && i + 1 < argc) accept_timeout_s = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc)      csv_path = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: mock_robot [-b <bind_ip>] [-p <port>] [--period <ms>] [--frames <n>]\n"
                   "                  [--silent] [--close-after <n>] [--rst]\n"
                   "                  [--accept-timeout <s>] [--csv <path>]\n"
                   "  -b <ip>   监听地址，默认 0.0.0.0（跨机测试）；本机自测可用 127.0.0.1\n"
                   "  默认监听端口 19090，每 10ms 推送一帧 20B 状态帧（偶数 UWB / 奇数 CAR）\n");
            return 0;
        }
        else {
            fprintf(stderr, "unknown option: %s (try --help)\n", argv[i]);
            return 1;
        }
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (strcmp(bind_ip, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid bind ip: %s\n", bind_ip);
        close(lfd);
        return 1;
    }

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); close(lfd); return 1; }
    if (listen(lfd, 1) < 0) { perror("listen"); close(lfd); return 1; }
    printf("[MOCK] listening on %s:%d (period=%dms frames=%ld silent=%d)\n",
           bind_ip, port, period_ms, frames, silent);
    if (strcmp(bind_ip, "0.0.0.0") == 0) {
        printf("[MOCK] 提示: 监听所有网卡，开发板可通过本机 IP 连接（跨机测试必需）\n");
    }
    fflush(stdout);

    /* 带超时的 accept，避免脚本卡死 */
    struct timeval tv = { accept_timeout_s, 0 };
    setsockopt(lfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int cfd = accept(lfd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0) {
        fprintf(stderr, "[MOCK] accept failed/timed out: %s\n", strerror(errno));
        close(lfd);
        return 2;
    }
    printf("[MOCK] client connected from %s:%d\n",
           inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    fflush(stdout);

    int nodelay = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    /* 控制帧接收统计 */
    long rx_ctrl = 0, rx_bad = 0, rx_seq_gap = 0;
    int  have_prev_seq = 0;
    int  prev_seq = 0;
    int  first_seq = -1;

    uint8_t accum[4096];      /* 控制帧字节流累积缓冲，用于跨 recv 拼帧 */
    size_t  accum_len = 0;

    /* 用 select 同时处理"周期推状态帧"和"接收控制帧" */
    struct timespec next_send;
    clock_gettime(CLOCK_MONOTONIC, &next_send);

    long tx_frames = 0;
    int connected = 1;

    while (!g_stop && connected) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(cfd, &rfds);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long remain_ms = (next_send.tv_sec - now.tv_sec) * 1000L +
                         (next_send.tv_nsec - now.tv_nsec) / 1000000L;
        if (remain_ms < 0) remain_ms = 0;
        if (silent || (frames > 0 && tx_frames >= frames)) remain_ms = 200;

        struct timeval wt = { remain_ms / 1000, (remain_ms % 1000) * 1000 };

        int r = select(cfd + 1, &rfds, NULL, NULL, &wt);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (r > 0 && FD_ISSET(cfd, &rfds)) {
            uint8_t buf[256];
            ssize_t n = recv(cfd, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    /* EAGAIN 来自 SO_RCVTIMEO，继续 */
                } else {
                    perror("recv");
                    connected = 0;
                    break;
                }
            } else if (n == 0) {
                printf("[MOCK] client closed connection\n");
                connected = 0;
                break;
            } else {
                /*
                 * TCP 是字节流，收到的可能不是完整帧。先追加到累积缓冲区，
                 * 再按 0x77 头 / 0x0A 尾做滑动重同步：帧完整且合法则计数，
                 * 否则丢弃 1 字节继续找，避免半包被误判为坏帧。
                 */
                if (accum_len + (size_t)n > sizeof(accum)) {
                    /* 异常堆积：丢弃旧数据保护边界 */
                    accum_len = 0;
                }
                memcpy(accum + accum_len, buf, (size_t)n);
                accum_len += (size_t)n;

                size_t off = 0;
                while (accum_len - off >= CTRL_FRAME_LEN) {
                    const uint8_t *f = accum + off;
                    if (f[0] == 0x77 && f[19] == 0x0A) {
                        int seq = f[18];
                        if (first_seq < 0) first_seq = seq;
                        if (have_prev_seq && seq != ((prev_seq + 1) & 0xFF)) rx_seq_gap++;
                        prev_seq = seq;
                        have_prev_seq = 1;
                        rx_ctrl++;
                        off += CTRL_FRAME_LEN;
                    } else {
                        rx_bad++;
                        off += 1;
                    }
                }
                if (off > 0) {
                    memmove(accum, accum + off, accum_len - off);
                    accum_len -= off;
                }
            }
        }

        if (!silent && (frames == 0 || tx_frames < frames)) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec > next_send.tv_sec ||
                (now.tv_sec == next_send.tv_sec && now.tv_nsec >= next_send.tv_nsec)) {

                uint8_t sf[STAT_FRAME_LEN];
                build_status_frame(sf, (int)tx_frames);

                ssize_t s = send(cfd, sf, sizeof(sf), MSG_NOSIGNAL);
                if (s < 0) {
                    perror("send status");
                    connected = 0;
                    break;
                }
                tx_frames++;

                next_send.tv_nsec += (long)period_ms * 1000000L;
                while (next_send.tv_nsec >= 1000000000L) {
                    next_send.tv_nsec -= 1000000000L;
                    next_send.tv_sec++;
                }

                if (close_after > 0 && tx_frames >= close_after) {
                    if (use_rst) {
                        struct linger lg = { 1, 0 };
                        setsockopt(cfd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
                        printf("[MOCK] aborting connection with RST after %ld frames\n", tx_frames);
                    } else {
                        printf("[MOCK] closing connection gracefully after %ld frames\n", tx_frames);
                    }
                    fflush(stdout);
                    connected = 0;
                    break;
                }
            }
        }
    }

    if (close(cfd) != 0) { /* 已关闭可忽略 */ }
    close(lfd);

    printf("[MOCK] summary: tx_status_frames=%ld rx_ctrl_frames=%ld rx_bad=%ld "
           "rx_seq_gap=%ld first_seq=%d\n",
           tx_frames, rx_ctrl, rx_bad, rx_seq_gap, first_seq);
    fflush(stdout);

    if (ensure_parent_dir(csv_path) == 0) {
        FILE *fp = fopen(csv_path, "w");
        if (fp) {
            fprintf(fp, "metric,value\n");
            fprintf(fp, "listen_port,%d\n", port);
            fprintf(fp, "status_period_ms,%d\n", period_ms);
            fprintf(fp, "tx_status_frames,%ld\n", tx_frames);
            fprintf(fp, "rx_ctrl_frames,%ld\n", rx_ctrl);
            fprintf(fp, "rx_bad_ctrl_frames,%ld\n", rx_bad);
            fprintf(fp, "rx_seq_gaps,%ld\n", rx_seq_gap);
            fprintf(fp, "first_ctrl_seq,%d\n", first_seq);
            fprintf(fp, "silent_mode,%d\n", silent);
            fprintf(fp, "close_after,%ld\n", close_after);
            fprintf(fp, "rst_close,%d\n", use_rst);
            fclose(fp);
        }
    }

    return 0;
}
