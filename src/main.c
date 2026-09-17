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

#include "app_config.h"
#include "robot_protocol.h"
#include "robot_tcp.h"
#include "logger.h"

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

    printf("[SEND] Thread started, period = %d ms\n", cfg->send_period_ms);

    while (g_running) {
        build_control_frame(frame, sizeof(frame),
                            (uint8_t)cfg->enable, (uint8_t)cfg->mode,
                            (int8_t)cfg->vx, (int8_t)cfg->vy, (int8_t)cfg->vz,
                            seq);

        if (tcp_send_all(g_sockfd, frame, sizeof(frame)) != 0) {
            fprintf(stderr, "[SEND] Failed, stopping.\n");
            g_running = 0;
            break;
        }

        seq++;   /* 回绕是预期行为：uint8_t 溢出后从 0 继续 */

        struct timespec req;
        req.tv_sec  = period_ns / 1000000000L;
        req.tv_nsec = period_ns % 1000000000L;
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
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
        {"vx",     required_argument, 0, 0},
        {"vy",     required_argument, 0, 0},
        {"vz",     required_argument, 0, 0},
        {"period", required_argument, 0, 0},
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
    g_log_fp = logger_open(LOG_DIR, "robot_log");
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
     * 注意：Ctrl+C 的信号是投递给进程的，由内核挑一个未屏蔽它的线程处理，
     * 不一定落在 receiver 上。如果信号被主线程或 sender 收走，而出数据的那一端
     * 一直静默，receiver 会继续阻塞在 recv 上，这里的 join 就会挂住 ——
     * 真正稳妥的做法是在 join 之前先 shutdown(g_sockfd) 唤醒它，或给 recv 加超时。
     */
    pthread_join(sender_tid, NULL);
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
