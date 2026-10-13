/*
 * CSV 日志模块。
 *
 * 日志文件按启动时刻命名（logs/robot_log_YYYYMMDD_HHMMSS.csv），
 * 开头写两行表头 —— 第一行是 UWB 字段、第二行是 CAR 字段，
 * 之后每来一帧就追加一行；两种记录都以时间戳开头，按列数区分。
 *
 * fp 为 NULL 时所有写函数静默返回，所以"打不开日志"不会影响主流程。
 */
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * 递归创建目录（等价 mkdir -p）：日志目录是 "log/<项目名>/" 这种多层结构，
 * 单层 mkdir 在父目录不存在时会失败。已存在且是目录视为成功。
 */
static int ensure_dir(const char *dir)
{
    if (!dir || dir[0] == '\0') return -1;

    char tmp[512];
    size_t len = strlen(dir);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, dir, len + 1);

    /* 去掉结尾的斜杠，避免最后一段为空 */
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }

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
    if (stat(tmp, &st) != 0) {
        return mkdir(tmp, 0755);
    }
    return S_ISDIR(st.st_mode) ? 0 : -1;
}

/*
 * 打开日志文件：建目录 → 用当前本地时间拼文件名 → 写表头。
 * 文件名带时间戳，每次运行生成新文件，避免覆盖历史数据。
 * 失败返回 NULL（调用方会降级为仅控制台输出）。
 */
FILE *logger_open(const char *dir, const char *prefix)
{
    if (ensure_dir(dir) != 0) {
        fprintf(stderr, "Cannot create log directory: %s\n", dir);
        return NULL;
    }

    /* localtime_r 是线程安全版本，不共用静态缓冲区 */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char path[512];
    snprintf(path, sizeof(path),
             "%s/%s_%04d%02d%02d_%02d%02d%02d.csv",
             dir, prefix,
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("fopen log");
        return NULL;
    }

    /*
     * 四行表头：UWB / CAR / IMU / AP 四种记录列数分别为 5 / 10 / 8 / 4，
     * 靠列数即可区分（沿用原有约定，不额外加类型列）。
     */
    fprintf(fp, "timestamp,tag_num,x,y,z\n");
    fprintf(fp, "timestamp,vx,vy,vz,weld_state,d_angle,d_dis,yaw,roll,pitch\n");
    fprintf(fp, "timestamp,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,temp\n");
    fprintf(fp, "timestamp,als,ps,ir\n");
    fflush(fp);

    printf("Log file: %s\n", path);
    return fp;
}

/* 追加一行车体状态记录（10 列）。fp 为空时直接返回，调用方无需判空 */
void logger_write_car(FILE *fp, double timestamp,
                      float vx, float vy, float vz,
                      int weld_state, float d_angle, float d_dis,
                      float yaw, float roll, float pitch)
{
    if (!fp) return;
    fprintf(fp, "%.6f,%.2f,%.2f,%.2f,%d,%.2f,%.4f,%.2f,%.2f,%.2f\n",
            timestamp, vx, vy, vz, weld_state, d_angle, d_dis,
            yaw, roll, pitch);
    fflush(fp);   /* 立刻落盘，进程被 Ctrl+C 强杀时已写数据不丢 */
}

/* 追加一行 UWB 定位记录（5 列） */
void logger_write_uwb(FILE *fp, double timestamp,
                      int32_t tag_num,
                      float x, float y, float z)
{
    if (!fp) return;
    fprintf(fp, "%.6f,%d,%.3f,%.3f,%.3f\n",
            timestamp, tag_num, x, y, z);
    fflush(fp);
}

/* 追加一行 IMU 记录（8 列）。加速度 g、角速度 °/s、温度 ℃ */
void logger_write_imu(FILE *fp, double timestamp, const ImuState *imu)
{
    if (!fp || !imu) return;
    fprintf(fp, "%.6f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.2f\n",
            timestamp,
            imu->accel_g[0], imu->accel_g[1], imu->accel_g[2],
            imu->gyro_dps[0], imu->gyro_dps[1], imu->gyro_dps[2],
            imu->temp_c);
    fflush(fp);
}

/* 追加一行 AP3216C 记录（4 列，原始计数） */
void logger_write_ap(FILE *fp, double timestamp, const ApState *ap)
{
    if (!fp || !ap) return;
    fprintf(fp, "%.6f,%d,%d,%d\n", timestamp,
            ap->als, ap->ps, ap->ir);
    fflush(fp);
}

void logger_close(FILE *fp)
{
    if (fp) fclose(fp);
}

/*
 * 当前时间戳（秒，带小数）。用 CLOCK_REALTIME 而非 MONOTONIC，
 * 因为日志要能和外部记录按绝对时间对齐。
 */
double logger_timestamp_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
