/*
 * 周期任务性能指标采集实现（详见 include/metrics.h 的设计约束）。
 *
 * 统计口径（写入报告，保证可复现）：
 *   唤醒延迟 wake_delay = start_ns - scheduled_ns            （仅绝对定时模式有效）
 *   工作耗时 work       = end_ns - start_ns
 *   相邻周期偏差 dev    = (start_ns[i] - start_ns[i-1]) - period_ns
 *   周期超期 overrun    = end_ns > scheduled_ns + period_ns   （仅绝对定时模式可判定）
 *
 * 百分位采用"最近秩"（nearest-rank）法：对升序样本取
 *   idx = round(p * (n-1))，idx 越界时钳到 n-1。
 * 该方法不需要插值，便于用 GNU sort/awk 之类的脚本独立复算校验。
 */
#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

/* 递归创建目录（等价 mkdir -p），已存在且是目录视为成功 */
static int mkdir_p(const char *dir)
{
    char tmp[512];
    size_t len = strlen(dir);

    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, dir, len + 1);

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

/* 只对 path 里最后一个 '/' 之前的部分调用 mkdir_p */
static int ensure_parent_dir(const char *path)
{
    char tmp[512];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);

    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return 0;   /* 无目录部分 / 根目录 -> 无需创建 */

    *slash = '\0';
    return mkdir_p(tmp);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* 最近秩百分位；sorted 必须已升序排列 */
static uint64_t percentile_nearest(const uint64_t *sorted, size_t n, double p)
{
    if (n == 0) return 0;

    double pos = p * (double)(n - 1);
    size_t idx = (size_t)(pos + 0.5);
    if (idx >= n) idx = n - 1;
    return sorted[idx];
}

static double mean_u64(const uint64_t *v, size_t n)
{
    if (n == 0) return 0.0;

    /* 用无符号累加 + double 除法，避免先转 double 导致的整数精度损失 */
    unsigned long long sum = 0;
    for (size_t i = 0; i < n; i++) sum += v[i];
    return (double)sum / (double)n;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

int metrics_init(Metrics *m, size_t capacity, uint64_t period_ns, int abs_mode)
{
    if (!m || capacity == 0) return -1;

    memset(m, 0, sizeof(*m));
    m->samples = (CycleSample *)calloc(capacity, sizeof(CycleSample));
    if (!m->samples) {
        fprintf(stderr, "[METRICS] calloc(%zu x %zu) failed: %s\n",
                capacity, sizeof(CycleSample), strerror(errno));
        return -1;
    }

    m->capacity  = capacity;
    m->period_ns = period_ns;
    m->abs_mode  = abs_mode;
    return 0;
}

void metrics_record(Metrics *m, const CycleSample *s)
{
    if (!m || !s) return;

    if (m->count >= m->capacity) {
        m->dropped++;
        return;
    }
    m->samples[m->count++] = *s;
}

int metrics_write_csv(const Metrics *m, const char *path)
{
    if (!m || !path) return -1;

    if (ensure_parent_dir(path) != 0) {
        fprintf(stderr, "[METRICS] cannot create directory for %s\n", path);
        return -1;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "[METRICS] cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    /* 大缓冲，一次性顺序写，避免 10 万行逐行刷盘 */
    setvbuf(fp, NULL, _IOFBF, 1u << 20);

    fprintf(fp, "cycle_index,scheduled_ns,start_ns,end_ns,"
                "work_ns,wake_delay_ns,period_dev_ns,send_ret,send_errno\n");

    for (size_t i = 0; i < m->count; i++) {
        const CycleSample *s = &m->samples[i];
        uint64_t work = s->end_ns - s->start_ns;

        fprintf(fp, "%llu,", (unsigned long long)s->cycle_index);

        if (m->abs_mode) {
            fprintf(fp, "%llu,", (unsigned long long)s->scheduled_ns);
        } else {
            fprintf(fp, "NA,");
        }

        fprintf(fp, "%llu,%llu,%llu,",
                (unsigned long long)s->start_ns,
                (unsigned long long)s->end_ns,
                (unsigned long long)work);

        if (m->abs_mode) {
            fprintf(fp, "%lld,", (long long)(s->start_ns - s->scheduled_ns));
        } else {
            fprintf(fp, "NA,");
        }

        if (i == 0) {
            fprintf(fp, "NA,");   /* 首周期没有前驱，无法计算相邻偏差 */
        } else {
            long long prev = (long long)(s->start_ns - m->samples[i - 1].start_ns);
            fprintf(fp, "%lld,", prev - (long long)m->period_ns);
        }

        fprintf(fp, "%d,%d\n", s->send_ret, s->send_errno);
    }

    if (fclose(fp) != 0) {
        fprintf(stderr, "[METRICS] fclose(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }

    printf("[METRICS] raw samples written: %s (%zu rows)\n", path, m->count);
    if (m->dropped > 0) {
        printf("[METRICS] WARNING: %zu samples dropped (capacity exhausted)\n", m->dropped);
    }
    return 0;
}

void metrics_print_summary(const Metrics *m)
{
    if (!m || m->count == 0) {
        printf("[METRICS] no sample collected.\n");
        return;
    }

    size_t n = m->count;
    size_t dev_n = (n > 1) ? (n - 1) : 0;

    uint64_t *work   = (uint64_t *)malloc(n * sizeof(uint64_t));
    uint64_t *dev_abs = (uint64_t *)malloc((dev_n ? dev_n : 1) * sizeof(uint64_t));
    uint64_t *wake   = (uint64_t *)malloc(n * sizeof(uint64_t));

    if (!work || !dev_abs || !wake) {
        fprintf(stderr, "[METRICS] summary allocation failed\n");
        free(work); free(dev_abs); free(wake);
        return;
    }

    uint64_t work_max = 0;
    size_t   overrun = 0;          /* 周期超期：end > scheduled + period */
    size_t   work_gt_period = 0;   /* 工作耗时本身超过目标周期 */
    size_t   send_errors = 0;

    for (size_t i = 0; i < n; i++) {
        const CycleSample *s = &m->samples[i];
        work[i] = s->end_ns - s->start_ns;
        if (work[i] > work_max) work_max = work[i];
        if (work[i] > m->period_ns) work_gt_period++;

        if (s->send_ret != 0) send_errors++;

        if (m->abs_mode) {
            wake[i] = (s->start_ns >= s->scheduled_ns)
                        ? (s->start_ns - s->scheduled_ns) : 0;
            if (s->end_ns > s->scheduled_ns + m->period_ns) overrun++;
        } else {
            wake[i] = 0;
        }

        if (i > 0) {
            long long prev = (long long)(s->start_ns - m->samples[i - 1].start_ns);
            long long dev  = prev - (long long)m->period_ns;
            dev_abs[i - 1] = (dev < 0) ? (uint64_t)(-dev) : (uint64_t)dev;
        }
    }

    qsort(work, n, sizeof(uint64_t), cmp_u64);

    printf("[METRICS] ================= periodic timing summary =================\n");
    printf("[METRICS] samples=%zu dropped=%zu target_period=%llu ns (%.3f ms) mode=%s\n",
           n, m->dropped, (unsigned long long)m->period_ns,
           (double)m->period_ns / 1e6, m->abs_mode ? "absolute" : "relative");
    printf("[METRICS] work_ns        mean=%.0f  p95=%llu  p99=%llu  max=%llu  (max=%.3f ms)\n",
           mean_u64(work, n),
           (unsigned long long)percentile_nearest(work, n, 0.95),
           (unsigned long long)percentile_nearest(work, n, 0.99),
           (unsigned long long)work_max,
           (double)work_max / 1e6);

    if (dev_n > 0) {
        qsort(dev_abs, dev_n, sizeof(uint64_t), cmp_u64);
        printf("[METRICS] period_dev_ns  |dev| mean=%.0f  p95=%llu  p99=%llu  max=%llu  (p99=%.3f ms)\n",
               mean_u64(dev_abs, dev_n),
               (unsigned long long)percentile_nearest(dev_abs, dev_n, 0.95),
               (unsigned long long)percentile_nearest(dev_abs, dev_n, 0.99),
               (unsigned long long)dev_abs[dev_n - 1],
               (double)percentile_nearest(dev_abs, dev_n, 0.99) / 1e6);
    }

    if (m->abs_mode) {
        qsort(wake, n, sizeof(uint64_t), cmp_u64);
        printf("[METRICS] wake_delay_ns  mean=%.0f  p95=%llu  p99=%llu  max=%llu\n",
               mean_u64(wake, n),
               (unsigned long long)percentile_nearest(wake, n, 0.95),
               (unsigned long long)percentile_nearest(wake, n, 0.99),
               (unsigned long long)wake[n - 1]);
    } else {
        printf("[METRICS] wake_delay_ns  N/A (relative-sleep mode: no independent deadline)\n");
    }

    printf("[METRICS] overrun(end>scheduled+period)=%zu  work_gt_period=%zu  send_errors=%zu\n",
           overrun, work_gt_period, send_errors);
    printf("[METRICS] ===========================================================\n");

    free(work);
    free(dev_abs);
    free(wake);
}

void metrics_free(Metrics *m)
{
    if (!m) return;
    free(m->samples);
    m->samples = NULL;
    m->capacity = 0;
    m->count = 0;
}
