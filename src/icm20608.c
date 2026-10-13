/*
 * ICM20608 IIO sysfs 读取实现。
 *
 * 为什么只拿原始值：量程配置（±16g / ±2000°/s）是驱动侧的事，换算系数
 * 与帧格式绑定，放在 robot_protocol 里收发共用一份，避免"板端算一次、
 * 上位机再算一次"导致两处系数漂移。
 */
#include "icm20608.h"
#include "iio_util.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

/* 与 raw 下标一一对应：accel x/y/z、gyro x/y/z、temp */
static const char *const k_icm_attrs[ICM_RAW_LEN] = {
    "in_accel0_raw",
    "in_accel1_raw",
    "in_accel2_raw",
    "in_anglvel0_raw",
    "in_anglvel1_raw",
    "in_anglvel2_raw",
    "in_temp0_raw",
};

#define ICM_IIO_NAME "icm20608"

/* int → int16 饱和截断：sysfs 理论上不会超范围，但越界时不能静默回绕 */
static int16_t clamp_i16(int v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

int icm20608_open(Icm20608 *dev, const char *iio_root)
{
    if (!dev) return -1;

    memset(dev, 0, sizeof(*dev));
    for (int i = 0; i < ICM_RAW_LEN; i++) dev->fd[i] = -1;

    const char *root = iio_root ? iio_root : IIO_DEFAULT_ROOT;

    if (iio_find_device(root, ICM_IIO_NAME, dev->dir, sizeof(dev->dir)) != 0) {
        fprintf(stderr, "[ICM20608] IIO device \"%s\" not found under %s\n",
                ICM_IIO_NAME, root);
        return -1;
    }

    for (int i = 0; i < ICM_RAW_LEN; i++) {
        dev->fd[i] = iio_open_attr(dev->dir, k_icm_attrs[i]);
        if (dev->fd[i] < 0) {
            fprintf(stderr, "[ICM20608] cannot open %s%s: %s\n",
                    dev->dir, k_icm_attrs[i], strerror(errno));
            icm20608_close(dev);
            return -1;
        }
    }

    dev->ok = 1;
    return 0;
}

int icm20608_read(Icm20608 *dev, int16_t raw[ICM_RAW_LEN])
{
    if (!dev || !dev->ok || !raw) return -1;

    int rc = 0;
    for (int i = 0; i < ICM_RAW_LEN; i++) {
        int v = 0;
        if (iio_read_int(dev->fd[i], &v) != 0) {
            raw[i] = 0;
            rc = -1;
        } else {
            raw[i] = clamp_i16(v);
        }
    }
    return rc;
}

void icm20608_close(Icm20608 *dev)
{
    if (!dev) return;
    for (int i = 0; i < ICM_RAW_LEN; i++) {
        if (dev->fd[i] >= 0) {
            close(dev->fd[i]);
            dev->fd[i] = -1;
        }
    }
    dev->ok = 0;
}
