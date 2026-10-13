#ifndef METRICS_H
#define METRICS_H

/*
 * 周期任务性能指标采集模块（可选，默认关闭）。
 *
 * 设计约束：
 *   1. 只由发送线程单线程写入，主线程在 join 之后才读取，因此内部无锁；
 *   2. 采样数组一次性预分配，运行期不做任何 malloc/free，避免堆抖动干扰测量；
 *   3. 运行期不打印、不写文件，只在退出时统一导出 CSV + 打印汇总，
 *      避免 printf / 磁盘 IO 污染 10ms 周期；
 *   4. 采集逻辑完全不影响默认通信行为 —— 只有 --benchmark 打开时才会
 *      多出 2 次 clock_gettime(CLOCK_MONOTONIC)，默认路径零开销。
 *
 * 时间戳统一使用 CLOCK_MONOTONIC 纳秒（64 位），不使用 CLOCK_REALTIME，
 * 因为周期性调度关心的是"间隔"而不是"墙上时间"。
 */

#include <stddef.h>
#include <stdint.h>

/* 单个周期的原始采样点 */
typedef struct {
    uint64_t cycle_index;   /* 周期序号，从 0 开始 */
    uint64_t scheduled_ns;  /* 该周期的目标开始时刻；相对睡眠模式下为 0（N/A） */
    uint64_t start_ns;      /* 本周期实际开始执行（组帧之前）的单调时刻 */
    uint64_t end_ns;        /* 本周期 send 返回之后的单调时刻 */
    int32_t  send_ret;      /* tcp_send_all 返回值：0 成功，-1 失败 */
    int32_t  send_errno;    /* 失败时的 errno，成功为 0 */
} CycleSample;

typedef struct {
    CycleSample *samples;
    size_t       capacity;   /* 预分配容量 */
    size_t       count;      /* 已记录样本数 */
    size_t       dropped;    /* 超出容量后被丢弃的样本数 */
    uint64_t     period_ns;  /* 目标周期（纳秒） */
    int          abs_mode;   /* 1 = 使用绝对时间定时，scheduled_ns 有效 */
} Metrics;

/* 默认预分配容量：约 300000 周期 × 48B ≈ 14MB，覆盖 10ms×50 分钟 */
#define METRICS_DEFAULT_CAPACITY 300000UL

/*
 * 初始化采集器：按 capacity 预分配采样数组。
 * 返回 0 成功，-1 失败（内存不足）。
 */
int  metrics_init(Metrics *m, size_t capacity, uint64_t period_ns, int abs_mode);

/* 记录一个周期样本；超出容量后只累加 dropped，不再写入 */
void metrics_record(Metrics *m, const CycleSample *s);

/*
 * 导出原始样本为 CSV（自动创建父目录）。
 * 返回 0 成功，-1 失败。周期序号、原始纳秒时间戳、派生量与发送状态全部落盘，
 * 便于事后用脚本独立复算任何统计口径。
 */
int  metrics_write_csv(const Metrics *m, const char *path);

/* 在控制台打印汇总统计（mean / P95 / P99 / max / 超限次数） */
void metrics_print_summary(const Metrics *m);

/* 释放采集器内存（必须在导出、打印之后调用） */
void metrics_free(Metrics *m);

#endif /* METRICS_H */
