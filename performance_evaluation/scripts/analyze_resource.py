#!/usr/bin/env python3
"""汇总资源观测 CSV（RSS / 线程数 / fd 数 / 上下文切换）。

用法:
    python3 analyze_resource.py <03_resource_usage_*.csv> [--label NAME]

注意：本脚本给出的是首末值与极值趋势。持续时间有限时，只能说明"未观察到
单调增长"，不能作为"无内存泄漏"的证明。
"""
import argparse
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    rows = []
    with open(args.csv) as f:
        header = f.readline().strip().split(",")
        idx = {n: i for i, n in enumerate(header)}
        for line in f:
            p = line.strip().split(",")
            if len(p) < len(header):
                continue
            rows.append(p)

    if not rows:
        print(f"[{args.label}] no data")
        return 1

    def col(name):
        return [int(r[idx[name]]) for r in rows if r[idx[name]].isdigit()]

    dur_start = float(rows[0][idx["elapsed_s"]])
    dur_end = float(rows[-1][idx["elapsed_s"]])
    rss = col("rss_kb")
    thr = col("threads")
    fdc = col("fd_count")

    print(f"=== {args.label or args.csv} ===")
    print(f"samples           : {len(rows)}")
    print(f"duration_s        : {dur_end - dur_start:.1f}")
    print(f"VmRSS_kB          : first={rss[0]} last={rss[-1]} "
          f"min={min(rss)} max={max(rss)} delta={rss[-1] - rss[0]}")
    print(f"Threads           : first={thr[0]} last={thr[-1]} min={min(thr)} max={max(thr)}")
    print(f"fd_count          : first={fdc[0]} last={fdc[-1]} min={min(fdc)} max={max(fdc)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
