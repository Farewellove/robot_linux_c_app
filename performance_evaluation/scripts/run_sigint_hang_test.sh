#!/usr/bin/env bash
# 退出时序回归测试：对端静默时收到 SIGINT，进程必须在有界时间内退出。
#
# 背景（源码审计发现）：receiver 线程阻塞在阻塞式 recv 上，Ctrl+C 的信号
# 由内核挑一个线程处理，不一定落在 receiver 上；若落在主线程/sender 上，
# receiver 会一直阻塞，pthread_join 永久挂住，只能 kill -9。
# 修复：main 在 join(receiver) 之前先 shutdown(g_sockfd, SHUT_RDWR)。
#
# 用法: run_sigint_hang_test.sh <binary> <label> [timeout_s]
#   输出：performance_evaluation/results/03_sigint_hang_<label>.txt
set -u

cd "$(dirname "$0")/../.." || exit 1

# PC 侧产物目录（make pc 生成）
PC_BIN="output/pc"
if [ ! -x "$PC_BIN/mock_robot" ]; then
    echo "错误: 缺少 $PC_BIN/mock_robot，请先执行: make pc"
    exit 1
fi

bin=${1:?usage: run_sigint_hang_test.sh <binary> <label> [timeout_s]}
label=${2:?label required}
timeout_s=${3:-8}

mkdir -p performance_evaluation/results performance_evaluation/logs
out="performance_evaluation/results/03_sigint_hang_${label}.txt"
app_log="performance_evaluation/logs/sigint_${label}_app.log"

port=$((19000 + RANDOM % 2000))
"$PC_BIN/mock_robot" -b 127.0.0.1 -p "$port" --silent --accept-timeout 15 \
    --csv "performance_evaluation/results/03_mock_server_sigint_${label}.csv" \
    > "performance_evaluation/logs/sigint_${label}_mock.log" 2>&1 &
mock_pid=$!
sleep 0.4

"$bin" -i 127.0.0.1 -p "$port" -e 1 -m 1 --period 10 > "$app_log" 2>&1 &
app_pid=$!
sleep 2

t0=$(date +%s%N)
kill -INT "$app_pid" 2>/dev/null

alive=yes
ticks=0
while [ "$ticks" -lt "$(( timeout_s * 10 ))" ]; do
    if ! kill -0 "$app_pid" 2>/dev/null; then alive=no; break; fi
    sleep 0.1
    ticks=$((ticks + 1))
done
t1=$(date +%s%N)

latency=$(python3 -c "print(f'{($t1-$t0)/1e9:.3f}')")

if [ "$alive" = "yes" ]; then
    kill -9 "$app_pid" 2>/dev/null
fi
wait "$app_pid" 2>/dev/null
kill "$mock_pid" 2>/dev/null
wait "$mock_pid" 2>/dev/null

{
    echo "label=$label"
    echo "binary=$bin"
    echo "peer_mode=silent (accepts control frames, sends nothing)"
    echo "signal=SIGINT after 2s of running"
    echo "timeout_s=$timeout_s"
    echo "exited_within_timeout=$([ "$alive" = "no" ] && echo yes || echo no)"
    echo "sigint_to_exit_latency_s=$latency"
    echo "required_kill9=$([ "$alive" = "yes" ] && echo yes || echo no)"
} | tee "$out"

echo "[HANGTEST] result -> $out"
