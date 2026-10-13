#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* 默认连接参数与运行参数（可被命令行覆盖，见 main.c 的 getopt_long） */
#define DEFAULT_ROBOT_IP      "192.168.1.103"
#define DEFAULT_ROBOT_PORT    9090
#define DEFAULT_SEND_PERIOD_MS 10 /*10ms发送一次 */
/* 注意：这两个宏与 robot_protocol.h 中的同名宏取值一致（20），
 * 修改协议帧长时两处都要同步改，否则会出现"缓冲区大小对不上"的问题 */
#define CONTROL_FRAME_LEN     20
#define STATUS_FRAME_LEN      20
#define RECV_BUF_SIZE         4096     /* 单次 recv 的暂存区大小 */

/*
 * 运行期日志统一放在 <LOG_ROOT>/<项目名>/ 下，不许散落在工作目录里。
 *
 * LOG_ROOT 相对当前工作目录：
 *   板端  从 /lib/modules/4.1.15 启动  → /lib/modules/4.1.15/log/robot_linux_c_app/
 *   PC    从仓库根目录启动            → ./log/robot_linux_c_app/
 *
 * 覆盖顺序（后者优先）：本文件默认值 < 环境变量 ROBOT_LOG_ROOT < --log-dir。
 */
#define LOG_ROOT              "log"
#define PROJECT_NAME          "robot_linux_c_app"
#define LOG_DIR               LOG_ROOT "/" PROJECT_NAME

/* CSV 日志输出目录（--log-dir 可覆盖） */
#define DEFAULT_LOG_DIR       LOG_DIR

/* 周期指标 CSV 默认输出路径（--benchmark 打开且未指定 --metrics-csv 时使用） */
#define DEFAULT_METRICS_CSV   LOG_DIR "/03_periodic_timing.csv"

/* 运行期配置：先填默认值，再被命令行参数覆盖 */
typedef struct {
    char robot_ip[64];     /* 机器人 IP（点分十进制），带结尾 '\0' */
    int  robot_port;       /* TCP 端口，校验范围 1~65535 */
    int  enable;           /* 使能位：0 失能 / 1 使能 */
    int  mode;             /* 控制模式：0=STOP 1=MANUAL 2=AUTO */
    int  vx;               /* 速度分量，下发时截断为 int8 */
    int  vy;
    int  vz;
    int  send_period_ms;   /* 控制帧发送周期，最小 1 ms */

    /* ---- 以下为测量用可选开关，默认全部关闭，不影响默认通信行为 ---- */
    int  benchmark;        /* 1 = 采集每周期调度指标（--benchmark） */
    int  abs_timer;        /* 1 = 绝对时间定时 TIMER_ABSTIME（--abs-timer） */
    int  max_cycles;       /* >0 = 跑满该周期数后退出；0 = 不限制（--cycles） */
    char metrics_csv[256]; /* 指标 CSV 输出路径（--metrics-csv） */
    char log_dir[256];     /* CSV 日志输出目录（--log-dir） */
} AppConfig;

/* 填默认值（会先 memset 清零） */
void app_config_set_defaults(AppConfig *cfg);

/* 打印最终生效的参数 */
void app_config_print(const AppConfig *cfg);

#endif
