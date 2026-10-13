/*
 * sensor_robot —— 板端「真实传感器」TCP 数据源
 *
 * 与 mock_robot 的区别：mock_robot 推的是公式生成的虚拟数据，
 * 本程序推的是开发板上真实 ICM20608 / AP3216C 的采样值 ——
 * 数据来自 IIO sysfs（/sys/bus/iio/devices/iio:deviceN/），不经过任何模拟。
 *
 * 角色与 mock_robot 完全一致：监听端口，接受上位机（robot_linux_app）连接，
 * 按周期推送 20 字节状态帧，同时校验上位机下发的控制帧（0x77 头 / 0x0A 尾 / seq 递增）。
 *
 * 前置条件（板端）：
 *   insmod icm20608.ko / ap3216c.ko（可用 ./load_drivers.sh 一次加载）
 *   ls /sys/bus/iio/devices/ 下一个 name 文件应能看到 icm20608 / ap3216c
 *
 * 选项：
 *   -b <ip>             监听地址（默认 0.0.0.0，跨机必需）
 *   -p <port>           监听端口（默认 19090）
 *   --period <ms>       采样推送周期（默认 10ms）
 *   --imu-every <n>     每 n 个周期推一帧 IMU 帧（默认 1；0 = 不推）
 *   --ap-every <n>      每 n 个周期推一帧 AP3216C 帧（默认 10；0 = 不推）
 *   --frames <n>        最多推送 n 帧后停止（0 = 不限）
 *   --accept-timeout <s> 等待上位机连接的秒数（默认 30）
 *   --wait <s>          等待传感器 IIO 节点就绪的秒数（默认 10）
 *   --iio-root <path>   IIO 根目录，默认 /sys/bus/iio/devices（可用假目录做离线自测）
 *   --print-every <n>   每采 n 次在控制台打印一次样本（默认 100；0 = 不打印）
 *   --csv <path>        统计结果输出路径
 *
 * 已知边界：
 *   - 采样节奏由本程序的 clock_nanosleep 决定，不是传感器的数据就绪中断，
 *     因此周期精度受用户态调度影响（与 mock_robot 同一量级）。
 *   - 每次采样要读 7 个（IMU）/ 3 个（AP）sysfs 文件，即 7 / 3 次 read 系统调用，
 *     不是驱动内部的批量读取；高频率下这是主要开销来源。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "robot_protocol.h"
#include "app_config.h"   /* DEFAULT_LOG_DIR：与其它程序共用同一套日志目录约定 */
#include "iio_util.h"
#include "icm20608.h"
#include "ap3216c.h"

#define CTRL_FRAME_LEN 20

static volatile sig_atomic_t g_stop = 0;

static void on_sig(int s) { (void)s; g_stop = 1; }

/* 递归创建目录（等价 mkdir -p）：日志目录是 log/<项目名>/ 这种多层结构 */
static int ensure_parent_dir(const char *path)
{
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);

    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return 0;
    *slash = '\0';
    len = strlen(tmp);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/') continue;

        char saved = tmp[i];
        tmp[i] = '\0';

        struct stat st;
        if (stat(tmp, &st) != 0) {
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                tmp[i] = saved;
                return -1;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            tmp[i] = saved;
            return -1;
        }
        tmp[i] = saved;
    }

    struct stat st;
    if (stat(tmp, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(tmp, 0755);
}

/* 带超时地等待两个 IIO 设备就绪：驱动是 insmod 的，节点出现可能晚于本程序启动 */
static int wait_sensors(const char *iio_root, Icm20608 *icm, Ap3216c *ap,
                        int want_imu, int want_ap, int wait_s)
{
    for (int t = 0; t <= wait_s * 10; t++) {
        int ok_imu = !want_imu || icm20608_open(icm, iio_root) == 0;
        int ok_ap  = !want_ap  || ap3216c_open(ap, iio_root) == 0;

        if (ok_imu && ok_ap) return 0;

        if (!ok_imu && want_imu) icm20608_close(icm);
        if (!ok_ap && want_ap)   ap3216c_close(ap);

        if (t == wait_s * 10) break;
        if (t % 10 == 0) {
            printf("[SENSOR] waiting for IIO devices (imu=%s ap=%s) ... %ds\n",
                   want_imu ? "yes" : "no", want_ap ? "yes" : "no", t / 10);
            fflush(stdout);
        }
        /* 用 clock_nanosleep 而非 usleep：后者在 POSIX.1-2008 下已不保证声明 */
        struct timespec req = { 0, 100000000L };   /* 100ms */
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    }
    return -1;
}

int main(int argc, char *argv[])
{
    const char *bind_ip    = "0.0.0.0";
    int  port              = 19090;
    int  period_ms         = 10;
    long imu_every         = 1;
    long ap_every          = 10;
    long frames            = 0;          /* 0 = 不限 */
    int  accept_timeout_s  = 30;
    int  wait_s            = 10;
    long print_every       = 100;
    const char *iio_root   = NULL;       /* NULL → IIO_DEFAULT_ROOT */
    const char *csv_path   = DEFAULT_LOG_DIR "/sensor_server.csv";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b") && i + 1 < argc)                    bind_ip = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc)               port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--period") && i + 1 < argc)         period_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--imu-every") && i + 1 < argc)      imu_every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--ap-every") && i + 1 < argc)       ap_every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)         frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--accept-timeout") && i + 1 < argc) accept_timeout_s = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc)           wait_s = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--print-every") && i + 1 < argc)    print_every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--iio-root") && i + 1 < argc)       iio_root = argv[++i];
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc)            csv_path = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: sensor_robot [-b <bind_ip>] [-p <port>] [--period <ms>]\n"
                   "                    [--imu-every <n>] [--ap-every <n>] [--frames <n>]\n"
                   "                    [--accept-timeout <s>] [--wait <s>] [--print-every <n>]\n"
                   "                    [--iio-root <path>] [--csv <path>]\n"
                   "  推真实传感器数据：ICM20608 六轴+温度 → 'i' 帧，AP3216C ALS/PS/IR → 'a' 帧\n"
                   "  前置：板端已 insmod icm20608.ko / ap3216c.ko\n");
            return 0;
        }
        else {
            fprintf(stderr, "unknown option: %s (try --help)\n", argv[i]);
            return 1;
        }
    }

    if (period_ms < 1)          { fprintf(stderr, "period must be >= 1 ms\n"); return 1; }
    if (imu_every < 0 || ap_every < 0) { fprintf(stderr, "every must be >= 0\n"); return 1; }
    if (imu_every == 0 && ap_every == 0) {
        fprintf(stderr, "both --imu-every and --ap-every are 0, nothing to send\n");
        return 1;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    /* ---- 打开真实传感器 ---- */
    Icm20608 icm;
    Ap3216c  ap;
    memset(&icm, 0, sizeof(icm));
    memset(&ap, 0, sizeof(ap));

    if (wait_sensors(iio_root, &icm, &ap, imu_every > 0, ap_every > 0, wait_s) != 0) {
        fprintf(stderr, "[SENSOR] sensor init failed under %s\n",
                iio_root ? iio_root : IIO_DEFAULT_ROOT);
        fprintf(stderr, "[SENSOR] 检查: insmod icm20608.ko / ap3216c.ko 是否成功，"
                        "或 lsmod | grep -E 'icm20608|ap3216c'\n");
        return 3;
    }
    if (imu_every > 0) printf("[SENSOR] ICM20608 ready: %s\n", icm.dir);
    if (ap_every  > 0) printf("[SENSOR] AP3216C  ready: %s\n", ap.dir);

    /* ---- 监听 ---- */
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
    printf("[SENSOR] listening on %s:%d (period=%dms imu_every=%ld ap_every=%ld frames=%ld)\n",
           bind_ip, port, period_ms, imu_every, ap_every, frames);
    fflush(stdout);

    struct timeval tv = { accept_timeout_s, 0 };
    setsockopt(lfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int cfd = accept(lfd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0) {
        fprintf(stderr, "[SENSOR] accept failed/timed out: %s\n", strerror(errno));
        close(lfd);
        icm20608_close(&icm);
        ap3216c_close(&ap);
        return 2;
    }
    printf("[SENSOR] client connected from %s:%d\n",
           inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    fflush(stdout);

    int nodelay = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    /* ---- 统计 ---- */
    long rx_ctrl = 0, rx_bad = 0, rx_seq_gap = 0;
    int  have_prev_seq = 0, prev_seq = 0, first_seq = -1;

    long tx_imu = 0, tx_ap = 0;
    long err_imu_read = 0, err_ap_read = 0, err_send = 0;
    long cycles = 0, samples = 0;
    uint16_t seq_imu = 0, seq_ap = 0;

    uint8_t accum[4096];
    size_t  accum_len = 0;

    struct timespec next_send;
    clock_gettime(CLOCK_MONOTONIC, &next_send);

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
        if (frames > 0 && (tx_imu + tx_ap) >= frames) remain_ms = 200;

        struct timeval wt = { remain_ms / 1000, (remain_ms % 1000) * 1000 };

        int r = select(cfd + 1, &rfds, NULL, NULL, &wt);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* 上位机下发的控制帧：同样按 0x77/0x0A 滑动重同步并校验 seq */
        if (r > 0 && FD_ISSET(cfd, &rfds)) {
            uint8_t buf[256];
            ssize_t n = recv(cfd, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("recv");
                    connected = 0;
                    break;
                }
            } else if (n == 0) {
                printf("[SENSOR] client closed connection\n");
                connected = 0;
                break;
            } else {
                if (accum_len + (size_t)n > sizeof(accum)) accum_len = 0;
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

        /* 到了推送时刻：读真实传感器 → 组帧 → 发送 */
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > next_send.tv_sec ||
            (now.tv_sec == next_send.tv_sec && now.tv_nsec >= next_send.tv_nsec)) {

            cycles++;
            int printed = 0;

            if (imu_every > 0 && (cycles % imu_every) == 0 &&
                (frames == 0 || (tx_imu + tx_ap) < frames)) {
                int16_t raw[ICM_RAW_LEN];
                uint8_t sf[STATUS_FRAME_LEN];

                if (icm20608_read(&icm, raw) != 0) {
                    err_imu_read++;
                } else if (build_imu_frame(sf, sizeof(sf), raw, seq_imu) != 0) {
                    err_send++;
                } else if (send(cfd, sf, sizeof(sf), MSG_NOSIGNAL) < 0) {
                    perror("send imu");
                    err_send++;
                    connected = 0;
                } else {
                    seq_imu++;
                    tx_imu++;
                    samples++;

                    if (print_every > 0 && samples % print_every == 0) {
                        ImuState st;
                        imu_raw_to_physical(raw, &st);
                        printf("[IMU ] ax=%+.3f ay=%+.3f az=%+.3f g | "
                               "gx=%+.2f gy=%+.2f gz=%+.2f deg/s | temp=%.2f C\n",
                               st.accel_g[0], st.accel_g[1], st.accel_g[2],
                               st.gyro_dps[0], st.gyro_dps[1], st.gyro_dps[2],
                               st.temp_c);
                        printed = 1;
                    }
                }
            }

            if (connected && ap_every > 0 && (cycles % ap_every) == 0 &&
                (frames == 0 || (tx_imu + tx_ap) < frames)) {
                int16_t raw[AP_RAW_LEN];
                uint8_t sf[STATUS_FRAME_LEN];

                if (ap3216c_read(&ap, raw) != 0) {
                    err_ap_read++;
                } else if (build_ap_frame(sf, sizeof(sf), raw[0], raw[1], raw[2], seq_ap) != 0) {
                    err_send++;
                } else if (send(cfd, sf, sizeof(sf), MSG_NOSIGNAL) < 0) {
                    perror("send ap");
                    err_send++;
                    connected = 0;
                } else {
                    seq_ap++;
                    tx_ap++;
                    samples++;

                    if (!printed && print_every > 0 && samples % print_every == 0) {
                        printf("[AP  ] als=%d ps=%d ir=%d\n", raw[0], raw[1], raw[2]);
                    }
                }
            }

            if (printed) fflush(stdout);

            /* 达到帧数上限主动收尾，便于脚本定量采集后自动退出 */
            if (frames > 0 && (tx_imu + tx_ap) >= frames) break;

            /* 绝对时间推进，避免 sleep 误差累积 */
            next_send.tv_nsec += (long)period_ms * 1000000L;
            while (next_send.tv_nsec >= 1000000000L) {
                next_send.tv_nsec -= 1000000000L;
                next_send.tv_sec++;
            }
        }
    }

    if (close(cfd) != 0) { /* 已关闭可忽略 */ }
    close(lfd);
    icm20608_close(&icm);
    ap3216c_close(&ap);

    printf("[SENSOR] summary: cycles=%ld tx_imu=%ld tx_ap=%ld "
           "err_imu_read=%ld err_ap_read=%ld err_send=%ld "
           "rx_ctrl=%ld rx_bad=%ld rx_seq_gap=%ld first_seq=%d\n",
           cycles, tx_imu, tx_ap, err_imu_read, err_ap_read, err_send,
           rx_ctrl, rx_bad, rx_seq_gap, first_seq);
    fflush(stdout);

    if (ensure_parent_dir(csv_path) == 0) {
        FILE *fp = fopen(csv_path, "w");
        if (fp) {
            fprintf(fp, "metric,value\n");
            fprintf(fp, "listen_port,%d\n", port);
            fprintf(fp, "sample_period_ms,%d\n", period_ms);
            fprintf(fp, "imu_every,%ld\n", imu_every);
            fprintf(fp, "ap_every,%ld\n", ap_every);
            fprintf(fp, "cycles,%ld\n", cycles);
            fprintf(fp, "tx_imu_frames,%ld\n", tx_imu);
            fprintf(fp, "tx_ap_frames,%ld\n", tx_ap);
            fprintf(fp, "err_imu_read,%ld\n", err_imu_read);
            fprintf(fp, "err_ap_read,%ld\n", err_ap_read);
            fprintf(fp, "err_send,%ld\n", err_send);
            fprintf(fp, "rx_ctrl_frames,%ld\n", rx_ctrl);
            fprintf(fp, "rx_bad_ctrl_frames,%ld\n", rx_bad);
            fprintf(fp, "rx_seq_gaps,%ld\n", rx_seq_gap);
            fprintf(fp, "first_ctrl_seq,%d\n", first_seq);
            fprintf(fp, "icm20608_dir,%s\n", imu_every > 0 ? icm.dir : "");
            fprintf(fp, "ap3216c_dir,%s\n", ap_every > 0 ? ap.dir : "");
            fclose(fp);
        }
    }

    return 0;
}
