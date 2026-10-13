/*
 * IIO sysfs 通用访问实现。
 *
 * 关键点：sysfs 属性文件用 pread(fd, buf, n, 0) 重读是有效的 ——
 * 内核的 seq_file 在 pos == 0 时会重新调用 show()，因此每次 pread
 * 都能拿到一次新的硬件采样值。这样只要 open 一次，后续采样就是
 * 纯粹的 read 系统调用，省掉 7 次（ICM20608）/ 3 次（AP3216C）open+close。
 */
#include "iio_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int iio_find_device(const char *iio_root, const char *name,
                    char *dir_out, size_t dir_size)
{
    if (!iio_root || !name || !dir_out || dir_size == 0) return -1;

    for (int i = 0; i < 32; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/iio:device%d/name", iio_root, i);

        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char buf[64] = {0};
        if (!fgets(buf, sizeof(buf), fp)) {
            fclose(fp);
            continue;
        }
        fclose(fp);

        buf[strcspn(buf, "\r\n")] = '\0';
        if (strcmp(buf, name) == 0) {
            snprintf(dir_out, dir_size, "%s/iio:device%d/", iio_root, i);
            return 0;
        }
    }
    return -1;
}

int iio_open_attr(const char *dir, const char *attr)
{
    if (!dir || !attr) return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", dir, attr);
    return open(path, O_RDONLY);
}

int iio_read_int(int fd, int *out)
{
    if (fd < 0 || !out) return -1;

    char buf[32];
    ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return -1;
    buf[n] = '\0';

    char *end = NULL;
    long v = strtol(buf, &end, 10);
    /* 没有任何数字被解析出来（属性文件为空或内容异常）视为失败 */
    if (end == buf) return -1;

    *out = (int)v;
    return 0;
}
