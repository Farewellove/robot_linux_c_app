#!/usr/bin/env bash
# 断线检测与退出时序测试（P1）。
#
# 三个子场景：
#   fin        服务端推 N 帧后优雅关闭（FIN） -> 预期上位机 recv 返回 0 后主动退出
#   rst        服务端推 N 帧后 RST 强断       -> 预期上位机 recv 报错后主动退出
#   silent     服务端只接不发，上位机收 SIGINT -> 预期在 1 秒量级内干净退出
#              （若没有 join 前的 shutdown 唤醒，receiver 会永久阻塞在 recv）
#
# 用法: run_disconnect_test.sh [out_dir]
set -u

cd "$(dirname "$0")/../.." || exit 1

# PC 侧产物目录（make pc 生成）
PC_BIN="output/pc"
if [ ! -x "$PC_BIN/mock_robot" ] || [ ! -x "$PC_BIN/robot_linux_app" ]; then
    echo "错误: 缺少 $PC_BIN/ 下的程序，请先执行: make pc"
    exit 1
fi

out=${1:-performance_evaluation/results}
mkdir -p "$out" performance_evaluation/logs

csv="$out/03_disconnect_tests.csv"
echo "case,server_close_mode,tx_frames_before_close,app_exit_code,app_wall_s,app_detected_close,sigint_exit_ok" > "$csv"

run_case() {
    local name=$1 mode=$2 frames=$3
    local port=$((19000 + RANDOM % 2000))
    local mock_log="performance_evaluation/logs/disconnect_${name}_mock.log"
    local app_log="performance_evaluation/logs/disconnect_${name}_app.log"

    local margs=(-b 127.0.0.1 -p "$port" --period 10 --frames 1000000 --csv "$out/03_mock_server_disconnect_${name}.csv" --accept-timeout 15)
    if [ "$mode" = "rst" ]; then
        margs+=(--close-after "$frames" --rst)
    else
        margs+=(--close-after "$frames")
    fi

    "$PC_BIN/mock_robot" "${margs[@]}" > "$mock_log" 2>&1 &
    local mock_pid=$!
    sleep 0.4

    # --cycles 给一个足够大的值，确保不是因为周期上限退出
    local start_ns=$(date +%s%N)
    timeout 30 "$PC_BIN/robot_linux_app" -i 127.0.0.1 -p "$port" -e 1 -m 1 --period 10 \
        --cycles 100000 > "$app_log" 2>&1
    local rc=$?
    local end_ns=$(date +%s%N)
    local wall=$(awk -v a="$start_ns" -v b="$end_ns" 'BEGIN{printf "%.3f", (b-a)/1e9}')

    kill "$mock_pid" 2>/dev/null
    wait "$mock_pid" 2>/dev/null

    local detected="no"
    # FIN 路径打印 "connection closed by peer"，RST 路径打印 "Connection reset by peer"
    if grep -qE "connection closed by peer|Connection reset by peer" "$app_log"; then
        detected="yes"
    fi

    echo "[DISCONNECT] $name: exit=$rc wall=${wall}s detected_close=$detected"
    echo "$name,$mode,$frames,$rc,$wall,$detected," >> "$csv"
}

run_case fin    fin    200
run_case rst    rst    200

# ---- silent + SIGINT ----
port=$((19000 + RANDOM % 2000))
"$PC_BIN/mock_robot" -b 127.0.0.1 -p "$port" --silent --csv "$out/03_mock_server_disconnect_silent.csv" \
    --accept-timeout 15 > performance_evaluation/logs/disconnect_silent_mock.log 2>&1 &
mock_pid=$!
sleep 0.4

start_ns=$(date +%s%N)
"$PC_BIN/robot_linux_app" -i 127.0.0.1 -p "$port" -e 1 -m 1 --period 10 \
    > performance_evaluation/logs/disconnect_silent_app.log 2>&1 &
app_pid=$!
sleep 2

# 只给主进程发 SIGINT（模拟 Ctrl+C），然后测量退出耗时；5 秒超时视为"挂死"
kill -INT "$app_pid" 2>/dev/null
sigint_ok=no
for _ in $(seq 1 50); do
    if ! kill -0 "$app_pid" 2>/dev/null; then sigint_ok=yes; break; fi
    sleep 0.1
done
wait "$app_pid" 2>/dev/null
rc=$?
end_ns=$(date +%s%N)
wall=$(awk -v a="$start_ns" -v b="$end_ns" 'BEGIN{printf "%.3f", (b-a)/1e9}')
kill "$mock_pid" 2>/dev/null
wait "$mock_pid" 2>/dev/null

echo "[DISCONNECT] silent+sigint: exited_within_5s=$sigint_ok exit=$rc total_wall=${wall}s"
echo "silent+sigint,silent,0,$rc,$wall,no,$sigint_ok" >> "$csv"

echo "[DISCONNECT] results -> $csv"
