/*
 * 运行参数：默认值填充与启动信息打印。
 * 参数先在这里填默认值，再被 main() 的命令行解析覆盖，
 * 所以没写的选项就保持默认。
 */
#include "app_config.h"
#include <stdio.h>
#include <string.h>

/*
 * 填默认值。先整体 memset 清零 —— 这样 robot_ip 里必然有 '\0'，
 * 配合下面 strncpy 只拷 size-1 字节，保证字符串一定以 '\0' 结尾。
 */
void app_config_set_defaults(AppConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->robot_ip, DEFAULT_ROBOT_IP, sizeof(cfg->robot_ip) - 1);
    cfg->robot_port     = DEFAULT_ROBOT_PORT;
    cfg->enable         = 0;   /* 默认不使能，避免连上就动 */
    cfg->mode           = 0;   /* STOP */
    cfg->vx             = 0;
    cfg->vy             = 0;
    cfg->vz             = 0;
    cfg->send_period_ms = DEFAULT_SEND_PERIOD_MS;

    /* 测量开关默认关闭：默认运行路径与改动前完全一致 */
    cfg->benchmark  = 0;
    cfg->abs_timer  = 0;
    cfg->max_cycles = 0;
    strncpy(cfg->metrics_csv, DEFAULT_METRICS_CSV, sizeof(cfg->metrics_csv) - 1);
}

/* 打印最终生效的参数，方便确认命令行是否被正确解析 */
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
    printf("  Benchmark  : %s\n", cfg->benchmark ? "on" : "off");
    if (cfg->benchmark) {
        printf("  Timer mode : %s\n", cfg->abs_timer ? "absolute (TIMER_ABSTIME)"
                                                    : "relative (clock_nanosleep)");
        printf("  Max cycles : %s\n", cfg->max_cycles > 0 ? "(see below)" : "unlimited");
        if (cfg->max_cycles > 0) {
            printf("               %d\n", cfg->max_cycles);
        }
        printf("  Metrics CSV: %s\n", cfg->metrics_csv);
    }
    printf("============================================\n");
}
