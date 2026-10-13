/*
 * robot_linux_c_app —— 机器人上位机 TCP 客户端
 *
 * 程序结构：
 *   main()            解析命令行参数 → 建立 TCP 连接 → 打开 CSV 日志
 *                     → 创建发送/接收两个线程 → 等它们退出 → 统一清理
 *   sender_thread     按固定周期组 20 字节控制帧下发（下行，0x77 帧）
 *   receiver_thread   阻塞 recv，把字节流喂给粘包状态机（上行，0x55 0x56 帧）
 *   on_parsed_frame   状态机回调：打印到控制台 + 写 CSV
 *
 * 线程间的共享状态：
 *   g_running    退出标志，SIGINT/SIGTERM 置 0，两个线程轮询它来收尾
 *   g_sockfd     连接 fd，发送/接收线程共用同一连接（一线程读、一线程写）
 *   g_log_fp     CSV 文件句柄，只有接收线程回调会写，但打印区仍加锁
 *   g_print_mutex 串行化"printf + 写日志"，避免控制台输出与文件内容交错
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <sys/socket.h>   /* shutdown()：join receiver 之前唤醒阻塞的 recv */

#include "app_config.h"
#include "robot_protocol.h"
#include "robot_tcp.h"
#include "logger.h"
#include "metrics.h"

/* timespec → 64 位纳秒（CLOCK_MONOTONIC 基准），仅用于周期测量 */
static uint64_t ts_to_ns(const struct timespec *ts)
{
    return (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
}

static volatile sig_atomic_t g_running = 1;
static int g_sockfd = -1;
static FILE *g_log_fp = NULL;
static pthread_mutex_t g_print_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 信号处理函数：只置标志，具体清理放在 main 的 cleanup 段做 */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/*
 * 状态机解析出一帧后的回调（在接收线程上下文同步执行）。
 * 按帧类型打印不同字段，并追加一行到 CSV；两者都在同一把锁内完成，
 * 保证控制台和文件里 UWB / CAR 两种记录的顺序一致、不会互相穿插。
 */
static void on_parsed_frame(const ParsedFrame *frame)
{
    double ts = logger_timestamp_now();

    pthread_mutex_lock(&g_print_mutex);

    switch (frame->type) {
    case FRAME_TYPE_UWB:
        printf("[UWB] tag=%d x=%.3f y=%.3f z=%.3f\n",
               frame->data.uwb.tag_num,
               frame->data.uwb.x,
               frame->data.uwb.y,
               frame->data.uwb.z);
        logger_write_uwb(g_log_fp, ts,
                         frame->data.uwb.tag_num,
                         frame->data.uwb.x,
                         frame->data.uwb.y,
                         frame->data.uwb.z);
        break;
    case FRAME_TYPE_CAR:
        printf("[CAR] vx=%.2f vy=%.2f vz=%.2f weld=%d dAngle=%.2f dDis=%.4f yaw=%.2f roll=%.2f pitch=%.2f\n",
               frame->data.car.vx,
               frame->data.car.vy,
               frame->data.car.vz,
               frame->data.car.weld_state,
               frame->data.car.d_angle,
               frame->data.car.d_dis,
               frame->data.car.yaw,
               frame->data.car.roll,
               frame->data.car.pitch);
        logger_write_car(g_log_fp, ts,
                         frame->data.car.vx,
                         frame->data.car.vy,
                         frame->data.car.vz,
                         frame->data.car.weld_state,
                         frame->data.car.d_angle,
                         frame->data.car.d_dis,
                         frame->data.car.yaw,
                         frame->data.car.roll,
                         frame->data.car.pitch);
        break;
    case FRAME_TYPE_IMU:
        printf("[IMU] ax=%+.3f ay=%+.3f az=%+.3f g | gx=%+.2f gy=%+.2f gz=%+.2f deg/s | "
               "temp=%.2f C | seq=%u\n",
               frame->data.imu.accel_g[0],
               frame->data.imu.accel_g[1],
               frame->data.imu.accel_g[2],
               frame->data.imu.gyro_dps[0],
               frame->data.imu.gyro_dps[1],
               frame->data.imu.gyro_dps[2],
               frame->data.imu.temp_c,
               frame->data.imu.seq);
        logger_write_imu(g_log_fp, ts, &frame->data.imu);
        break;
    case FRAME_TYPE_AP:
        printf("[AP ] als=%d ps=%d ir=%d | seq=%u\n",
               frame->data.ap.als,
               frame->data.ap.ps,
               frame->data.ap.ir,
               frame->data.ap.seq);
        logger_write_ap(g_log_fp, ts, &frame->data.ap);
        break;
    default:
        break;
    }

    pthread_mutex_unlock(&g_print_mutex);
}

/*
 * 发送线程：周期性下发控制帧。
 *
 * 参数在启动前一次性由命令行解析好（cfg 在 main 栈上，join 前一直有效），
 * 运行期各字段固定，只有 seq 每帧自增。周期用 clock_nanosleep(CLOCK_MONOTONIC)
 * 定时 —— 单调时钟不受系统时间跳变影响，比 sleep/usleep 更适合定周期任务。
 * 发送失败（对端关闭/出错）即置 g_running=0，通知接收线程和主线程一起退出。
 */
static void *sender_thread(void *arg)
{
    AppConfig *cfg = (AppConfig *)arg;
    uint8_t seq = 0;
    uint8_t frame[CONTROL_FRAME_LEN];

    long period_ns = (long)cfg->send_period_ms * 1000000L;

    /*
     * 可选测量：只有 --benchmark 打开时才预分配采样数组并逐周期打点。
     * 关闭时（默认）下面的 metrics_on 全为 0，不产生额外系统调用。
     */
    Metrics metrics;
    int metrics_on = 0;
    if (cfg->benchmark) {
        size_t cap = (cfg->max_cycles > 0) ? (size_t)cfg->max_cycles
                                           : (size_t)METRICS_DEFAULT_CAPACITY;
        if (metrics_init(&metrics, cap, (uint64_t)period_ns, cfg->abs_timer) != 0) {
            fprintf(stderr, "[METRICS] init failed, benchmark disabled.\n");
        } else {
            metrics_on = 1;
        }
    }

    /*
     * 绝对定时模式：以线程启动时刻为基准，为每个周期算出一个绝对目标时刻
     * next_sched_ns，用 TIMER_ABSTIME 睡到该时刻。这样单周期的超时不会
     * 累积到后续周期（相对睡眠会累积漂移）。该模式仅在 --abs-timer 时启用，
     * 默认仍走原来的相对睡眠路径，保证默认行为不变。
     */
    uint64_t next_sched_ns = 0;
    if (cfg->abs_timer) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        next_sched_ns = ts_to_ns(&now);
    }

    printf("[SEND] Thread started, period = %d ms, timer = %s%s\n",
           cfg->send_period_ms,
           cfg->abs_timer ? "absolute" : "relative",
           metrics_on ? ", metrics on" : "");

    uint64_t cycle = 0;

    while (g_running) {
        struct timespec t0, t1;
        int send_ret = 0;
        int send_err = 0;

        if (metrics_on) clock_gettime(CLOCK_MONOTONIC, &t0);

        build_control_frame(frame, sizeof(frame),
                            (uint8_t)cfg->enable, (uint8_t)cfg->mode,
                            (int8_t)cfg->vx, (int8_t)cfg->vy, (int8_t)cfg->vz,
                            seq);

        int rc = tcp_send_all(g_sockfd, frame, sizeof(frame));
        if (rc != 0) {
            send_ret = -1;
            send_err = errno;
        }

        if (metrics_on) {
            clock_gettime(CLOCK_MONOTONIC, &t1);

            CycleSample s;
            memset(&s, 0, sizeof(s));
            s.cycle_index  = cycle;
            s.scheduled_ns = cfg->abs_timer ? next_sched_ns : 0;
            s.start_ns     = ts_to_ns(&t0);
            s.end_ns       = ts_to_ns(&t1);
            s.send_ret     = send_ret;
            s.send_errno   = send_err;
            metrics_record(&metrics, &s);
        }

        if (rc != 0) {
            fprintf(stderr, "[SEND] Failed, stopping.\n");
            g_running = 0;
            break;
        }

        seq++;   /* 回绕是预期行为：uint8_t 溢出后从 0 继续 */
        cycle++;

        /* --cycles N：跑满 N 个周期后主动收尾，便于自动化测试定量采集 */
        if (cfg->max_cycles > 0 && cycle >= (uint64_t)cfg->max_cycles) {
            printf("[SEND] Reached cycle limit (%d), stopping.\n", cfg->max_cycles);
            g_running = 0;
            break;
        }

        if (cfg->abs_timer) {
            next_sched_ns += (uint64_t)period_ns;

            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (next_sched_ns > ts_to_ns(&now)) {
                struct timespec target;
                target.tv_sec  = (time_t)(next_sched_ns / 1000000000ULL);
                target.tv_nsec = (long)(next_sched_ns % 1000000000ULL);
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);
            }
            /* 若已经超过目标时刻则不再睡，直接进入下一周期（该周期会记为超期） */
        } else {
            struct timespec req;
            req.tv_sec  = period_ns / 1000000000L;
            req.tv_nsec = period_ns % 1000000000L;
            clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
        }
    }

    /* 采样由本线程独占写入，此处已无其他写者，直接导出即可，无需加锁 */
    if (metrics_on) {
        metrics_write_csv(&metrics, cfg->metrics_csv);
        metrics_print_summary(&metrics);
        metrics_free(&metrics);
    }

    printf("[SEND] Thread exiting.\n");
    return NULL;
}

/*
 * 接收线程：阻塞 recv，把收到的字节流交给粘包状态机。
 *
 * tmp 只是本次 recv 的暂存区，边界无意义（可能装半帧也可能装好几帧），
 * 真正的帧边界由 recv_buffer_feed 内部的滑动窗口判定。
 * n<0（连接断开或出错）同样置退出标志，让发送线程和主线程收尾；
 * n==0（EINTR 被信号打断）不做处理，直接进入下一轮。
 */
static void *receiver_thread(void *arg)
{
    (void)arg;

    uint8_t tmp[RECV_BUF_SIZE];
    RecvBuffer rb;
    recv_buffer_init(&rb);

    printf("[RECV] Thread started.\n");

    while (g_running) {
        int n = tcp_recv_some(g_sockfd, tmp, sizeof(tmp));
        if (n < 0) {
            g_running = 0;
            break;
        }
        if (n > 0) {
            recv_buffer_feed(&rb, tmp, (size_t)n, on_parsed_frame);
        }
    }

    printf("[RECV] Thread exiting.\n");
    return NULL;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -i <ip>       Robot IP address   (default: %s)\n", DEFAULT_ROBOT_IP);
    printf("  -p <port>     Robot TCP port     (default: %d)\n", DEFAULT_ROBOT_PORT);
    printf("  -e <0|1>      Enable             (default: 0)\n");
    printf("  -m <0|1|2>    Mode               (default: 0)\n");
    printf("  --vx <val>    Velocity X          (default: 0)\n");
    printf("  --vy <val>    Velocity Y          (default: 0)\n");
    printf("  --vz <val>    Velocity Z          (default: 0)\n");
    printf("  --period <ms> Send period in ms   (default: %d)\n", DEFAULT_SEND_PERIOD_MS);
    printf("  --benchmark   Collect per-cycle timing metrics (default: off)\n");
    printf("  --metrics-csv <path>  Metrics CSV output (default: %s)\n", DEFAULT_METRICS_CSV);
    printf("  --cycles <n>  Exit after n send cycles, 0 = unlimited (default: 0)\n");
    printf("  --abs-timer   Use absolute CLOCK_MONOTONIC deadline instead of relative sleep\n");
    printf("  --log-dir <path>  CSV log output directory (default: %s)\n", DEFAULT_LOG_DIR);
    printf("  -h            Show this help\n");
}

int main(int argc, char *argv[])
{
    AppConfig cfg;
    app_config_set_defaults(&cfg);

    /*
     * 长选项的 flag 传 0，getopt_long 会返回 0 并把下标记到 long_idx，
     * 于是下面 case 0 里要靠 long_opts[long_idx].name 反查是哪个选项
     * —— 因为 vx/vy/vz/period 都是"带参数、按名字区分"的同类选项。
     */
    struct option long_opts[] = {
        {"vx",          required_argument, 0, 0},
        {"vy",          required_argument, 0, 0},
        {"vz",          required_argument, 0, 0},
        {"period",      required_argument, 0, 0},
        {"benchmark",   no_argument,       0, 0},
        {"metrics-csv", required_argument, 0, 0},
        {"cycles",      required_argument, 0, 0},
        {"abs-timer",   no_argument,       0, 0},
        {"log-dir",     required_argument, 0, 0},
        {0, 0, 0, 0}
    };

    int opt;
    int long_idx = 0;

    while ((opt = getopt_long(argc, argv, "i:p:e:m:h", long_opts, &long_idx)) != -1) {
        switch (opt) {
        case 'i':
            /* 留 1 字节给结尾的 '\0'，配合 set_defaults 里的 memset 保证有终止符 */
            strncpy(cfg.robot_ip, optarg, sizeof(cfg.robot_ip) - 1);
            break;
        case 'p':
            cfg.robot_port = atoi(optarg);
            break;
        case 'e':
            cfg.enable = atoi(optarg);
            break;
        case 'm':
            cfg.mode = atoi(optarg);
            break;
        case 0:
            /* 长选项分支：按名字匹配 */
            if (strcmp(long_opts[long_idx].name, "vx") == 0) {
                cfg.vx = atoi(optarg);
            } else if (strcmp(long_opts[long_idx].name, "vy") == 0) {
                cfg.vy = atoi(optarg);
            } else if (strcmp(long_opts[long_idx].name, "vz") == 0) {
                cfg.vz = atoi(optarg);
            } else if (strcmp(long_opts[long_idx].name, "period") == 0) {
                cfg.send_period_ms = atoi(optarg);
            } else if (strcmp(long_opts[long_idx].name, "benchmark") == 0) {
                cfg.benchmark = 1;
            } else if (strcmp(long_opts[long_idx].name, "abs-timer") == 0) {
                cfg.abs_timer = 1;
            } else if (strcmp(long_opts[long_idx].name, "metrics-csv") == 0) {
                strncpy(cfg.metrics_csv, optarg, sizeof(cfg.metrics_csv) - 1);
            } else if (strcmp(long_opts[long_idx].name, "cycles") == 0) {
                cfg.max_cycles = atoi(optarg);
            } else if (strcmp(long_opts[long_idx].name, "log-dir") == 0) {
                strncpy(cfg.log_dir, optarg, sizeof(cfg.log_dir) - 1);
            }
            break;
        case 'h':
        default:
            /* -h 以及无法识别的选项都走这里：打印用法并退出 */
            print_usage(argv[0]);
            return 0;
        }
    }

    /* 参数合法性校验：端口范围、发送周期下限 */
    if (cfg.robot_port <= 0 || cfg.robot_port > 65535) {
        fprintf(stderr, "Invalid port: %d\n", cfg.robot_port);
        return 1;
    }
    if (cfg.send_period_ms < 1) {
        fprintf(stderr, "Send period must be at least 1 ms\n");
        return 1;
    }
    if (cfg.max_cycles < 0) {
        fprintf(stderr, "Cycle limit must be >= 0\n");
        return 1;
    }
    /* 绝对定时模式只有配合指标采集才有意义，单独开启不做拦截，仅提示 */
    if (cfg.abs_timer && !cfg.benchmark) {
        fprintf(stderr, "Note: --abs-timer without --benchmark has no metrics output.\n");
    }

    app_config_print(&cfg);

    /*
     * 信号注册：
     *   SIGINT/SIGTERM 只置退出标志，让主循环自然收尾（信号处理函数里不做 IO）
     *   SIGPIPE 忽略 —— 对端已关闭时 send 会触发 SIGPIPE 直接杀死进程，
     *           忽略后 send 改为返回 -1/EPIPE，由 tcp_send_all 判断返回错误
     */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* 先建连接再开日志：连不上就没必要创建日志文件 */
    g_sockfd = tcp_connect_to_server(cfg.robot_ip, cfg.robot_port);
    if (g_sockfd < 0) {
        fprintf(stderr, "Failed to connect to robot.\n");
        return 1;
    }

    /* 日志打不开不算致命错误，降级为只打印到控制台继续跑 */
    g_log_fp = logger_open(cfg.log_dir, "robot_log");
    if (!g_log_fp) {
        fprintf(stderr, "Warning: logging disabled.\n");
    }

    pthread_t sender_tid, receiver_tid;

    /* cfg 在 main 栈上，发送线程持指针访问；线程创建失败则不再启动后续线程 */
    if (pthread_create(&sender_tid, NULL, sender_thread, &cfg) != 0) {
        perror("pthread_create sender");
        g_running = 0;
        goto cleanup;
    }
    if (pthread_create(&receiver_tid, NULL, receiver_thread, NULL) != 0) {
        perror("pthread_create receiver");
        g_running = 0;
        pthread_join(sender_tid, NULL);  /* 回收已经起来的发送线程 */
        goto cleanup;
    }

    printf("Application running. Press Ctrl+C to exit.\n");

    /*
     * 主线程在这里等两个线程结束：
     *   sender   —— 被 g_running=0 唤醒后（clock_nanosleep 返回 EINTR）退出循环；
     *   receiver —— 阻塞在 recv 上，靠信号打断 EINTR 后检查标志退出，
     *               或者对端关闭连接使 recv 返回 0 而退出。
     *
     * 退出时序加固（本次修复项）：
     *   Ctrl+C 的信号由内核挑一个未屏蔽它的线程处理，不保证落在 receiver 上。
     *   若信号被主线程或 sender 收走，而对端此时一直静默，receiver 会继续阻塞在
     *   recv 上，后面的 join 就会永久挂住（只能 kill -9）。因此在 join(receiver)
     *   之前先 shutdown(g_sockfd, SHUT_RDWR)：内核会让阻塞中的 recv 立即返回 0，
     *   receiver 随即检查 g_running 退出。shutdown 只影响本端读写，不改变协议行为。
     */
    pthread_join(sender_tid, NULL);

    if (g_sockfd >= 0) {
        shutdown(g_sockfd, SHUT_RDWR);
    }
    pthread_join(receiver_tid, NULL);

cleanup:
    /* 到这里线程都已回收，可以安全关闭共享资源 */
    printf("Shutting down...\n");

    if (g_sockfd >= 0) {
        tcp_close(g_sockfd);
        g_sockfd = -1;
    }

    if (g_log_fp) {
        logger_close(g_log_fp);
        g_log_fp = NULL;
    }

    pthread_mutex_destroy(&g_print_mutex);

    printf("Goodbye.\n");
    return 0;
}
