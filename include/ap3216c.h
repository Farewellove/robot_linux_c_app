#ifndef AP3216C_H
#define AP3216C_H

/*
 * AP3216C（环境光 ALS / 接近 PS / 红外 IR）用户态读取封装。
 *
 * 与 ICM20608 一样走 IIO sysfs（驱动 ap3216c.ko，设备名 "ap3216c"）。
 * 帧里只传原始计数；板上该驱动的 scale 当前恒为 1，所以原始值即显示值，
 * 换算不在协议里写死，避免驱动后续改量程时协议跟着变。
 */
#include <stdint.h>

/* 通道数：als, ps, ir */
#define AP_RAW_LEN 3

typedef struct {
    char dir[256];              /* 定位到的 IIO 设备目录 */
    int  fd[AP_RAW_LEN];        /* 3 个 raw 属性的常驻 fd */
    int  scale[AP_RAW_LEN];     /* 打开时读一次；读取失败时按 1 处理 */
    int  ok;                    /* 1 = 可用 */
} Ap3216c;

/*
 * 打开传感器并缓存 scale。iio_root 为 NULL 时用 /sys/bus/iio/devices。
 * 成功返回 0，失败返回 -1。
 */
int  ap3216c_open(Ap3216c *dev, const char *iio_root);

/* 采样一次，写入 raw[3]（顺序 als, ps, ir）。成功返回 0，任一通道失败返回 -1 */
int  ap3216c_read(Ap3216c *dev, int16_t raw[AP_RAW_LEN]);

/* 关闭所有 fd；可重复调用 */
void ap3216c_close(Ap3216c *dev);

#endif
