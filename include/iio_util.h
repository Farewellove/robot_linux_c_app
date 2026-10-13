#ifndef IIO_UTIL_H
#define IIO_UTIL_H

/*
 * IIO sysfs 通用访问封装（正点原子 i.MX6ULL，内核 4.1.15）。
 *
 * 板上的 ICM20608 / AP3216C 都是 IIO 驱动，用户态只读 sysfs：
 *   /sys/bus/iio/devices/iio:deviceN/name           ← 用来定位设备
 *   /sys/bus/iio/devices/iio:deviceN/<attr>         ← 每次 read 触发一次硬件采样
 *
 * 这里的三个函数只做"定位 + 打开 + 读整数"，不含任何传感器语义，
 * 供 icm20608.c / ap3216c.c 复用，避免两处各写一遍。
 */
#include <stddef.h>

/* IIO 设备默认根目录 */
#define IIO_DEFAULT_ROOT "/sys/bus/iio/devices"

/*
 * 按 name 文件的内容在 iio:device0..iio:device31 中定位设备。
 * 找到就把 "<root>/iio:deviceN/"（带结尾斜杠）写入 dir_out，返回 0；
 * 未找到返回 -1。
 * 为什么要遍历而不是写死编号：设备号取决于驱动加载顺序，写死会偶发取错设备。
 */
int iio_find_device(const char *iio_root, const char *name,
                    char *dir_out, size_t dir_size);

/*
 * 打开设备目录下的某个属性文件，返回 fd（失败返回 -1）。
 * fd 由调用方长期持有，采样时用 iio_read_int() 反复 pread，
 * 避免每次采样都 open/close（sysfs 的 open 也要走内核路径解析）。
 */
int iio_open_attr(const char *dir, const char *attr);

/*
 * 从已打开的 fd 读一个整数（pread 偏移 0，每次都重新触发 show()）。
 * 成功返回 0 并写入 *out（支持负值，即 int16 原始值域 -32768..32767）；
 * 失败返回 -1。
 */
int iio_read_int(int fd, int *out);

#endif
