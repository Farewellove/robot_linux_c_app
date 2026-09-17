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
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* 目录不存在则创建（0755）；存在但不是目录则报错 */
static int ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(dir, 0755);
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

    /* 两行表头：UWB 记录与 CAR 记录的列布局不同，靠列数区分 */
    fprintf(fp, "timestamp,tag_num,x,y,z\n");
    fprintf(fp, "timestamp,vx,vy,vz,weld_state,d_angle,d_dis,yaw,roll,pitch\n");
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
