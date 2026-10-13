#ifndef ICM20608_H
#define ICM20608_H

/*
 * ICM20608（六轴 + 温度）用户态读取封装。
 *
 * 板端接口是 IIO sysfs（驱动 icm20608.ko，设备名 "icm20608"），
 * 本模块只取 int16 原始值，不做物理量换算 —— 换算系数属于协议的一部分，
 * 由 robot_protocol.h 的 imu_raw_to_physical() 统一负责（收发两侧同一套）。
 *
 * 典型用法：
 *   Icm20608 dev;
 *   icm20608_open(&dev, NULL);          // NULL = /sys/bus/iio/devices
 *   int16_t raw[ICM_RAW_LEN];
 *   icm20608_read(&dev, raw);           // 每次调用触发一次采样
 *   icm20608_close(&dev);
 */
#include <stdint.h>

#include "robot_protocol.h"   /* ICM_RAW_LEN */

/* 属性名顺序与 raw 下标一一对应：ax, ay, az, gx, gy, gz, temp */
typedef struct {
    char dir[256];              /* 定位到的 IIO 设备目录（调试/日志用） */
    int  fd[ICM_RAW_LEN];       /* 7 个属性文件的常驻 fd */
    int  ok;                    /* 1 = 全部打开成功，可用 */
} Icm20608;

/*
 * 打开传感器：定位 IIO 设备并打开 7 个属性文件。
 * iio_root 传 NULL 表示用默认路径 /sys/bus/iio/devices。
 * 成功返回 0（dev->ok = 1），失败返回 -1 并把 dev 全部置为不可用状态。
 */
int  icm20608_open(Icm20608 *dev, const char *iio_root);

/*
 * 采样一次，结果写入 raw[ICM_RAW_LEN]（下标见上方说明）。
 * 成功返回 0；任一通道读失败返回 -1（此时 raw 中不保证全部有效）。
 */
int  icm20608_read(Icm20608 *dev, int16_t raw[ICM_RAW_LEN]);

/* 关闭所有 fd；可重复调用 */
void icm20608_close(Icm20608 *dev);

#endif
