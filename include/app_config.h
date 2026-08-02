#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define DEFAULT_ROBOT_IP      "192.168.1.103"
#define DEFAULT_ROBOT_PORT    9090
#define DEFAULT_SEND_PERIOD_MS 10
#define CONTROL_FRAME_LEN     20
#define STATUS_FRAME_LEN      20
#define LOG_DIR               "logs"
#define RECV_BUF_SIZE         4096

typedef struct {
    char robot_ip[64];
    int  robot_port;
    int  enable;
    int  mode;
    int  vx;
    int  vy;
    int  vz;
    int  send_period_ms;
} AppConfig;

void app_config_set_defaults(AppConfig *cfg);
void app_config_print(const AppConfig *cfg);

#endif
