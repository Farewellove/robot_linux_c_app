#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(dir, 0755);
}

FILE *logger_open(const char *dir, const char *prefix)
{
    if (ensure_dir(dir) != 0) {
        fprintf(stderr, "Cannot create log directory: %s\n", dir);
        return NULL;
    }

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

    fprintf(fp, "timestamp,tag_num,x,y,z\n");
    fprintf(fp, "timestamp,vx,vy,vz,weld_state,d_angle,d_dis,yaw,roll,pitch\n");
    fflush(fp);

    printf("Log file: %s\n", path);
    return fp;
}

void logger_write_car(FILE *fp, double timestamp,
                      float vx, float vy, float vz,
                      int weld_state, float d_angle, float d_dis,
                      float yaw, float roll, float pitch)
{
    if (!fp) return;
    fprintf(fp, "%.6f,%.2f,%.2f,%.2f,%d,%.2f,%.4f,%.2f,%.2f,%.2f\n",
            timestamp, vx, vy, vz, weld_state, d_angle, d_dis,
            yaw, roll, pitch);
    fflush(fp);
}

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

double logger_timestamp_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
