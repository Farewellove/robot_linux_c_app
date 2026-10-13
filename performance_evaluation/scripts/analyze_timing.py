#!/usr/bin/env python3
"""独立复算周期调度 CSV 的统计量，用于交叉校验程序内置的 metrics 汇总。

用法:
    python3 analyze_timing.py <03_periodic_timing_*.csv> [--period-ns 10000000] [--label NAME]

设计意图：报告里的每个数字都应该能被第三方用原始 CSV 独立算出来，
所以这个脚本只读原始列（start_ns / end_ns / scheduled_ns），不信任程序打印的汇总。
"""
import argparse
import statistics
import sys


def percentile_nearest(sorted_vals, p):
    if not sorted_vals:
        return None
    pos = p * (len(sorted_vals) - 1)
    idx = int(pos + 0.5)
    idx = max(0, min(idx, len(sorted_vals) - 1))
    return sorted_vals[idx]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--period-ns", type=int, default=10_000_000)
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    starts, works, devs, wakes = [], [], [], []
    abs_mode = False
    send_errors = 0
    rows = 0

    with open(args.csv, newline="") as f:
        header = f.readline().strip().split(",")
        idx = {name: i for i, name in enumerate(header)}
        prev_start = None

        for line in f:
            parts = line.rstrip("\n").split(",")
            if len(parts) < len(header):
                continue
            rows += 1

            start = int(parts[idx["start_ns"]])
            end = int(parts[idx["end_ns"]])
            starts.append(start)
            works.append(end - start)

            if parts[idx["scheduled_ns"]] != "NA":
                abs_mode = True
                sched = int(parts[idx["scheduled_ns"]])
                wakes.append(max(0, start - sched))

            if prev_start is not None:
                devs.append((start - prev_start) - args.period_ns)
            prev_start = start

            if int(parts[idx["send_ret"]]) != 0:
                send_errors += 1

    if not starts:
        print(f"[{args.label}] no data in {args.csv}")
        return 1

    abs_devs = sorted(abs(d) for d in devs)
    works_sorted = sorted(works)

    print(f"=== {args.label or args.csv} ===")
    print(f"rows                : {rows}")
    print(f"timer mode          : {'absolute' if abs_mode else 'relative'}")
    print(f"work_ns             : mean={statistics.mean(works):.0f} "
          f"p95={percentile_nearest(works_sorted, 0.95)} "
          f"p99={percentile_nearest(works_sorted, 0.99)} max={max(works)}")
    if devs:
        print(f"period_dev_ns (|d|) : mean={statistics.mean(abs_devs):.0f} "
              f"p95={percentile_nearest(abs_devs, 0.95)} "
              f"p99={percentile_nearest(abs_devs, 0.99)} max={max(abs_devs)}")
        print(f"period_dev_ns signed: min={min(devs)} max={max(devs)}")
        s2s = [s - p for s, p in zip(starts[1:], starts[:-1])]
        print(f"start_to_start_ns   : mean={statistics.mean(s2s):.0f} "
              f"min={min(s2s)} max={max(s2s)}")
    if wakes:
        ws = sorted(wakes)
        print(f"wake_delay_ns       : mean={statistics.mean(wakes):.0f} "
              f"p95={percentile_nearest(ws, 0.95)} p99={percentile_nearest(ws, 0.99)} "
              f"max={max(wakes)}")
    print(f"send_errors         : {send_errors}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
