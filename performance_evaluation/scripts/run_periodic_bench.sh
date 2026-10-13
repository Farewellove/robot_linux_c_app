#!/usr/bin/env bash
# 单个 10ms 周期调度性能场景测试。
#
# 用法: run_periodic_bench.sh <scenario> <cycles> <timer_mode> <out_dir>
#   scenario   : idle | traffic | load
#                idle    = loopback 服务端只接不发（空载，无上行流量）
#                traffic = 服务端每 10ms 推一帧 UWB/CAR（真实业务收发）
#                load    = traffic + 占满全部 CPU 核的忙循环（可控 CPU 负载）
#   cycles     : 采集的周期数（10000 周期 @10ms ≈ 100 秒）
#   timer_mode : relative | absolute
#   out_dir    : 输出目录（CSV + 日志）
#
# 退出后会打印关键统计，并把 app/mock 日志留在 out_dir 下。
set -u

cd "$(dirname "$0")/../.." || exit 1

# PC 侧产物目录（make pc 生成）
PC_BIN="output/pc"
if [ ! -x "$PC_BIN/mock_robot" ] || [ ! -x "$PC_BIN/robot_linux_app" ]; then
    echo "错误: 缺少 $PC_BIN/ 下的程序，请先执行: make pc"
    exit 1
fi

scenario=${1:?usage: run_periodic_bench.sh <scenario> <cycles> <timer_mode> <out_dir>}
cycles=${2:-10000}
timer_mode=${3:-relative}
out_dir=${4:-performance_evaluation/results}

mkdir -p "$out_dir" performance_evaluation/logs

port=$((19000 + RANDOM % 2000))
tag="${scenario}_${timer_mode}"
timing_csv="$out_dir/03_periodic_timing_${tag}.csv"
res_csv="$out_dir/03_resource_usage_${tag}.csv"
mock_csv="$out_dir/03_mock_server_${tag}.csv"
app_log="performance_evaluation/logs/app_${tag}.log"
mock_log="performance_evaluation/logs/mock_${tag}.log"

echo "=================================================================="
echo "[BENCH] scenario=$scenario cycles=$cycles timer=$timer_mode port=$port"
echo "=================================================================="

load_pids=()

cleanup() {
    for p in "${load_pids[@]:-}"; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done
    [ -n "${mock_pid:-}" ] && kill "$mock_pid" 2>/dev/null
    [ -n "${mon_pid:-}" ] && kill "$mon_pid" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

# ---- 启动 loopback 模拟服务端 ----
# 本机自测统一绑定 127.0.0.1，避免长时间基准运行期间端口暴露在局域网
mock_args=(-b 127.0.0.1 -p "$port" --csv "$mock_csv" --accept-timeout 15)
if [ "$scenario" = "idle" ]; then
    mock_args+=(--silent)
else
    mock_args+=(--period 10 --frames 1000000)
fi

"$PC_BIN/mock_robot" "${mock_args[@]}" > "$mock_log" 2>&1 &
mock_pid=$!
sleep 0.4

if ! kill -0 "$mock_pid" 2>/dev/null; then
    echo "[BENCH] mock_robot failed to start, see $mock_log"
    exit 1
fi

# ---- 可控 CPU 负载 ----
if [ "$scenario" = "load" ]; then
    ncores=$(nproc)
    echo "[BENCH] spawning $ncores busy-loop workers"
    for _ in $(seq 1 "$ncores"); do
        ( while :; do :; done ) &
        load_pids+=($!)
    done
    sleep 0.5
fi

# ---- 启动被测程序 ----
app_args=(-i 127.0.0.1 -p "$port" -e 1 -m 1 --vx 0 --vy 0 --vz 0
          --period 10 --benchmark --cycles "$cycles" --metrics-csv "$timing_csv")
if [ "$timer_mode" = "absolute" ]; then
    app_args+=(--abs-timer)
fi

"$PC_BIN/robot_linux_app" "${app_args[@]}" > "$app_log" 2>&1 &
app_pid=$!

# ---- 同步采样资源占用 ----
sample_interval=1
sample_count=$(( cycles / 100 + 5 ))     # 10ms 周期 -> 约每秒 100 周期
./performance_evaluation/scripts/monitor_resource.sh \
    "$app_pid" "$sample_interval" "$sample_count" "$res_csv" > /dev/null 2>&1 &
mon_pid=$!

start_ns=$(date +%s%N)
wait "$app_pid"
app_rc=$?
end_ns=$(date +%s%N)
wall=$(awk -v a="$start_ns" -v b="$end_ns" 'BEGIN{printf "%.1f", (b-a)/1e9}')

kill "$mon_pid" 2>/dev/null
kill "$mock_pid" 2>/dev/null
sleep 0.2

echo "[BENCH] app exit code=$app_rc wall=${wall}s"
echo "------------------------------------------------------------------"
grep -E '^\[METRICS\]' "$app_log" || echo "(no metrics lines found)"
echo "------------------------------------------------------------------"
grep -E '^\[MOCK\] summary' "$mock_log" || true
echo "[BENCH] timing CSV : $timing_csv"
echo "[BENCH] res CSV    : $res_csv"
echo
