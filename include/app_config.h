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
#define LOG_DIR               "logs"   /* CSV 日志输出目录 */
#define RECV_BUF_SIZE         4096     /* 单次 recv 的暂存区大小 */

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
} AppConfig;

/* 填默认值（会先 memset 清零） */
void app_config_set_defaults(AppConfig *cfg);

/* 打印最终生效的参数 */
void app_config_print(const AppConfig *cfg);

#endif
