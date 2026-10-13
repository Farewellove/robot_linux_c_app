#!/usr/bin/env bash
# 依次运行全部周期调度场景，产出可写入报告的原始 CSV 与日志。
#
# 用法: run_all_bench.sh [cycles]
#   默认 cycles=10000（10ms × 10000 ≈ 100 秒/场景）
#
# 场景矩阵：
#   idle/relative      空载（服务端只接不发）
#   traffic/relative   真实业务收发（每 10ms 一帧上行）
#   load/relative      业务收发 + 占满 CPU 的忙循环
#   traffic/absolute   业务收发 + 绝对时间(TIMER_ABSTIME)定时，用于对比累积漂移
set -u

cd "$(dirname "$0")/../.." || exit 1

cycles=${1:-10000}
out=performance_evaluation/results
mkdir -p "$out" performance_evaluation/logs

summary="$out/03_bench_summary.txt"
: > "$summary"
echo "platform : $(uname -srm)"          | tee -a "$summary"
echo "cpu      : $(nproc) cores"          | tee -a "$summary"
echo "kernel   : $(uname -r)"             | tee -a "$summary"
echo "compiler : $(gcc --version | head -1)" | tee -a "$summary"
echo "cycles   : $cycles per scenario"    | tee -a "$summary"
echo "date     : $(date -Is)"             | tee -a "$summary"
echo "======================================" | tee -a "$summary"

for combo in "idle relative" "traffic relative" "load relative" "traffic absolute"; do
    set -- $combo
    bash performance_evaluation/scripts/run_periodic_bench.sh "$1" "$cycles" "$2" "$out" \
        2>&1 | tee -a "$summary"
done

echo "[ALL] summary -> $summary"
