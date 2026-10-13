/*
 * AP3216C IIO sysfs 读取实现。
 *
 * raw 与 scale 分开读：scale 是静态量程系数，只需在 open 时取一次；
 * raw 每次采样都会触发一次 I2C 读取。
 */
#include "ap3216c.h"
#include "iio_util.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

static const char *const k_ap_raw_attrs[AP_RAW_LEN] = {
    "in_illuminance_raw",
    "in_proximity_raw",
    "in_intensity_ir_raw",
};

static const char *const k_ap_scale_attrs[AP_RAW_LEN] = {
    "in_illuminance_scale",
    "in_proximity_scale",
    "in_intensity_ir_scale",
};

#define AP_IIO_NAME "ap3216c"

static int16_t clamp_i16(int v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

int ap3216c_open(Ap3216c *dev, const char *iio_root)
{
    if (!dev) return -1;

    memset(dev, 0, sizeof(*dev));
    for (int i = 0; i < AP_RAW_LEN; i++) {
        dev->fd[i]    = -1;
        dev->scale[i] = 1;      /* 读不到 scale 时按 1，等价于 raw 直接可用 */
    }

    const char *root = iio_root ? iio_root : IIO_DEFAULT_ROOT;

    if (iio_find_device(root, AP_IIO_NAME, dev->dir, sizeof(dev->dir)) != 0) {
        fprintf(stderr, "[AP3216C] IIO device \"%s\" not found under %s\n",
                AP_IIO_NAME, root);
        return -1;
    }

    for (int i = 0; i < AP_RAW_LEN; i++) {
        dev->fd[i] = iio_open_attr(dev->dir, k_ap_raw_attrs[i]);
        if (dev->fd[i] < 0) {
            fprintf(stderr, "[AP3216C] cannot open %s%s: %s\n",
                    dev->dir, k_ap_raw_attrs[i], strerror(errno));
            ap3216c_close(dev);
            return -1;
        }

        /* scale 只是辅助信息，读不到不影响主流程，保持 1 */
        int sfd = iio_open_attr(dev->dir, k_ap_scale_attrs[i]);
        if (sfd >= 0) {
            int v = 0;
            if (iio_read_int(sfd, &v) == 0 && v > 0) dev->scale[i] = v;
            close(sfd);
        }
    }

    dev->ok = 1;
    return 0;
}

int ap3216c_read(Ap3216c *dev, int16_t raw[AP_RAW_LEN])
{
    if (!dev || !dev->ok || !raw) return -1;

    int rc = 0;
    for (int i = 0; i < AP_RAW_LEN; i++) {
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

void ap3216c_close(Ap3216c *dev)
{
    if (!dev) return;
    for (int i = 0; i < AP_RAW_LEN; i++) {
        if (dev->fd[i] >= 0) {
            close(dev->fd[i]);
            dev->fd[i] = -1;
        }
    }
    dev->ok = 0;
}
