#!/usr/bin/env bash
# 长期运行资源稳定性观测（RSS / 线程数 / fd 数 / 上下文切换）。
#
# 用法: run_soak.sh <duration_s> <interval_s>
#   例：run_soak.sh 1800 5   -> 连续 30 分钟、每 5 秒采一次
#
# 说明：本脚本给出的是"观察曲线"。要在报告里下"无泄漏"的结论，
#       必须同时提供 Valgrind / ASan 的证据，或重复多次运行的趋势一致性。
set -u

cd "$(dirname "$0")/../.." || exit 1

# PC 侧产物目录（make pc 生成）
PC_BIN="output/pc"
if [ ! -x "$PC_BIN/mock_robot" ] || [ ! -x "$PC_BIN/robot_linux_app" ]; then
    echo "错误: 缺少 $PC_BIN/ 下的程序，请先执行: make pc"
    exit 1
fi

duration=${1:-300}
interval=${2:-5}
out=performance_evaluation/results

mkdir -p "$out" performance_evaluation/logs

port=$((19000 + RANDOM % 2000))
res_csv="$out/03_resource_usage_soak.csv"
mock_csv="$out/03_mock_server_soak.csv"
app_log="performance_evaluation/logs/app_soak.log"
mock_log="performance_evaluation/logs/mock_soak.log"

echo "[SOAK] duration=${duration}s interval=${interval}s port=$port"

cleanup() {
    [ -n "${mock_pid:-}" ] && kill "$mock_pid" 2>/dev/null
    [ -n "${mon_pid:-}" ] && kill "$mon_pid" 2>/dev/null
    [ -n "${app_pid:-}" ] && kill "$app_pid" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

"$PC_BIN/mock_robot" -b 127.0.0.1 -p "$port" --period 10 --frames 100000000 --csv "$mock_csv" \
    --accept-timeout 15 > "$mock_log" 2>&1 &
mock_pid=$!
sleep 0.4

# 不使用 --cycles：按运行时长而不是周期数结束，更贴近"长期运行"语义
"$PC_BIN/robot_linux_app" -i 127.0.0.1 -p "$port" -e 1 -m 1 --period 10 \
    > "$app_log" 2>&1 &
app_pid=$!

samples=$(( duration / interval ))
./performance_evaluation/scripts/monitor_resource.sh \
    "$app_pid" "$interval" "$samples" "$res_csv" &
mon_pid=$!

wait "$mon_pid"
sleep 0.2

kill -TERM "$app_pid" 2>/dev/null
wait "$app_pid" 2>/dev/null
app_rc=$?

kill "$mock_pid" 2>/dev/null

echo "[SOAK] app exit code=$app_rc"
echo "[SOAK] resource CSV: $res_csv"
echo "[SOAK] mock summary: $(grep -E '^\[MOCK\] summary' "$mock_log" || echo 'n/a')"
echo "[SOAK] tail of app log:"
tail -3 "$app_log"
