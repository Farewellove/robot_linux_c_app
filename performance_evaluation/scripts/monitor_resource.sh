#!/usr/bin/env bash
# 进程级资源观测：固定间隔采样 VmRSS / 线程数 / 打开的文件描述符数 / 上下文切换。
#
# 用法: monitor_resource.sh <pid> <interval_s> <samples> <csv_path>
#
# 说明：VmRSS 与 fd 数是"观察指标"，用有限时长的曲线只能说明趋势，
#       不能据此断言"无内存泄漏/无 fd 泄漏"。要下结论需要 Valgrind/ASan 等工具配合。
set -u

pid=${1:?usage: monitor_resource.sh <pid> <interval_s> <samples> <csv_path>}
interval=${2:-1}
samples=${3:-60}
csv=${4:-/tmp/resource_usage.csv}

mkdir -p "$(dirname "$csv")"
echo "elapsed_s,rss_kb,threads,fd_count,voluntary_ctxt_switches,nonvoluntary_ctxt_switches" > "$csv"

start_ns=$(date +%s%N)
i=0
while [ "$i" -lt "$samples" ]; do
    if [ ! -d "/proc/$pid" ]; then
        break
    fi

    now_ns=$(date +%s%N)
    elapsed=$(awk -v a="$start_ns" -v b="$now_ns" 'BEGIN{printf "%.3f", (b-a)/1e9}')

    status=$(cat "/proc/$pid/status" 2>/dev/null)
    rss=$(printf '%s\n' "$status" | awk '/^VmRSS:/{print $2}')
    thr=$(printf '%s\n' "$status" | awk '/^Threads:/{print $2}')
    vc=$(printf '%s\n' "$status"  | awk '/^voluntary_ctxt_switches:/{print $2}')
    nvc=$(printf '%s\n' "$status" | awk '/^nonvoluntary_ctxt_switches:/{print $2}')
    fdc=$(ls "/proc/$pid/fd" 2>/dev/null | wc -l | tr -d ' ')

    echo "${elapsed:-0},${rss:-0},${thr:-0},${fdc:-0},${vc:-0},${nvc:-0}" >> "$csv"

    i=$((i + 1))
    sleep "$interval"
done

echo "[MONITOR] wrote $i samples to $csv"
