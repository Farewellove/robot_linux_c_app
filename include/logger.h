#ifndef LOGGER_H
#define LOGGER_H

/*
 * CSV 日志接口。
 * 约定：写函数在 fp == NULL 时静默返回，因此调用方不需要判空，
 * 日志不可用不会中断主流程（最多只打印到控制台）。
 */
#include <stdint.h>
#include <stdio.h>

#include "robot_protocol.h"

/*
 * 打开日志文件，返回文件句柄；失败返回 NULL。
 * dir       日志目录，不存在会自动创建
 * prefix    文件名前缀，最终为 <dir>/<prefix>_YYYYMMDD_HHMMSS.csv
 */
FILE *logger_open(const char *dir, const char *prefix);

/* 追加一行车体状态记录（10 列，见 logger.c 中的表头） */
void  logger_write_car(FILE *fp, double timestamp,
                       float vx, float vy, float vz,
                       int weld_state, float d_angle, float d_dis,
                       float yaw, float roll, float pitch);

/* 追加一行 UWB 定位记录（5 列） */
void  logger_write_uwb(FILE *fp, double timestamp,
                       int32_t tag_num,
                       float x, float y, float z);

/* 追加一行 IMU 记录（8 列：加速度 g / 角速度 °/s / 温度 ℃） */
void  logger_write_imu(FILE *fp, double timestamp, const ImuState *imu);

/* 追加一行 AP3216C 记录（4 列：ALS / PS / IR 原始计数） */
void  logger_write_ap(FILE *fp, double timestamp, const ApState *ap);

/* 关闭日志文件；fp 为 NULL 时安全 */
void  logger_close(FILE *fp);

/* 当前时间戳（秒，带小数，CLOCK_REALTIME 基准） */
double logger_timestamp_now(void);

#endif
