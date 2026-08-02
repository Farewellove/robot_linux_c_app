#include "app_config.h"
#include <stdio.h>
#include <string.h>

void app_config_set_defaults(AppConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->robot_ip, DEFAULT_ROBOT_IP, sizeof(cfg->robot_ip) - 1);
    cfg->robot_port     = DEFAULT_ROBOT_PORT;
    cfg->enable         = 0;
    cfg->mode           = 0;
    cfg->vx             = 0;
    cfg->vy             = 0;
    cfg->vz             = 0;
    cfg->send_period_ms = DEFAULT_SEND_PERIOD_MS;
}

void app_config_print(const AppConfig *cfg)
{
    printf("============================================\n");
    printf("Robot Linux C App\n");
    printf("============================================\n");
    printf("  Robot IP   : %s\n", cfg->robot_ip);
    printf("  Robot Port : %d\n", cfg->robot_port);
    printf("  Enable     : %d\n", cfg->enable);
    printf("  Mode       : %d\n", cfg->mode);
    printf("  Vx         : %d\n", cfg->vx);
    printf("  Vy         : %d\n", cfg->vy);
    printf("  Vz         : %d\n", cfg->vz);
    printf("  Period     : %d ms\n", cfg->send_period_ms);
    printf("============================================\n");
}
