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

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

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

        seq++;

        struct timespec req;
        req.tv_sec  = period_ns / 1000000000L;
        req.tv_nsec = period_ns % 1000000000L;
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    }

    printf("[SEND] Thread exiting.\n");
    return NULL;
}

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
            print_usage(argv[0]);
            return 0;
        }
    }

    if (cfg.robot_port <= 0 || cfg.robot_port > 65535) {
        fprintf(stderr, "Invalid port: %d\n", cfg.robot_port);
        return 1;
    }
    if (cfg.send_period_ms < 1) {
        fprintf(stderr, "Send period must be at least 1 ms\n");
        return 1;
    }

    app_config_print(&cfg);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    g_sockfd = tcp_connect_to_server(cfg.robot_ip, cfg.robot_port);
    if (g_sockfd < 0) {
        fprintf(stderr, "Failed to connect to robot.\n");
        return 1;
    }

    g_log_fp = logger_open(LOG_DIR, "robot_log");
    if (!g_log_fp) {
        fprintf(stderr, "Warning: logging disabled.\n");
    }

    pthread_t sender_tid, receiver_tid;

    if (pthread_create(&sender_tid, NULL, sender_thread, &cfg) != 0) {
        perror("pthread_create sender");
        g_running = 0;
        goto cleanup;
    }
    if (pthread_create(&receiver_tid, NULL, receiver_thread, NULL) != 0) {
        perror("pthread_create receiver");
        g_running = 0;
        pthread_join(sender_tid, NULL);
        goto cleanup;
    }

    printf("Application running. Press Ctrl+C to exit.\n");

    pthread_join(sender_tid, NULL);
    pthread_join(receiver_tid, NULL);

cleanup:
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
