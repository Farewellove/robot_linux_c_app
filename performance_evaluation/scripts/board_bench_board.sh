#!/bin/sh
# board_bench_board.sh —— 板端一键采集（在开发板上运行）
#
# 用法（板端串口）:
#   cd /lib/modules/4.1.15
#   ./board_bench_board.sh [PC_IP] [周期数]
#
#   PC_IP    PC 侧模拟设备端的 IP，默认 192.168.1.66
#   周期数    默认 6000（10ms × 6000 = 60 秒）
#
# 也可用环境变量覆盖: PORT / PERIOD_MS / ROBOT_LOG_ROOT / BOARD_LOG_DIR
#
# 做了什么:
#   1) 记录板端运行环境（uname / nproc / cpuinfo / 频率 / 网络）到 board_env_<时间>.txt
#   2) 运行接收端 robot_linux_app，用 --benchmark 采集逐周期调度指标
#   3) 指标 CSV 写在日志目录（NFS 共享目录），PC 侧无需 scp 即可读取
#
# 日志目录约定（不许把 CSV/txt 散落在板端工作目录里）:
#   <ROBOT_LOG_ROOT>/robot_linux_c_app/
#   ROBOT_LOG_ROOT 默认 "log"（相对板端工作目录 → /lib/modules/4.1.15/log/robot_linux_c_app/）
#
# 注意: 请在 PC 侧先执行 board_bench_pc.sh 把模拟设备端起起来，再运行本脚本。

set -u

PC_IP="${1:-192.168.1.66}"
CYCLES="${2:-6000}"
PORT="${PORT:-19090}"
PERIOD_MS="${PERIOD_MS:-10}"

PROJECT_NAME="robot_linux_c_app"
LOG_ROOT="${ROBOT_LOG_ROOT:-log}"

# 切到脚本所在目录（= 板端工作目录 /lib/modules/4.1.15）
cd "$(dirname "$0")" || exit 1

if [ ! -x ./robot_linux_app ]; then
    echo "错误: 当前目录 ($(pwd)) 没有可执行的 robot_linux_app"
    echo "      请在 PC 侧执行: make deploy"
    exit 1
fi

# 所有产物进 log/<项目名>/，不散落在工作目录
LOG_DIR="${BOARD_LOG_DIR:-$LOG_ROOT/$PROJECT_NAME}"
mkdir -p "$LOG_DIR" || {
    echo "错误: 无法创建日志目录 $LOG_DIR"
    exit 1
}

ts=$(date +%Y%m%d_%H%M%S 2>/dev/null || echo "now")
envfile="$LOG_DIR/board_env_${ts}.txt"
csv="$LOG_DIR/board_timing.csv"

echo "=================================================="
echo " 板端调度指标采集"
echo "   PC_IP   = $PC_IP"
echo "   PORT    = $PORT"
echo "   PERIOD  = ${PERIOD_MS} ms"
echo "   CYCLES  = $CYCLES  (约 $((CYCLES * PERIOD_MS / 1000)) 秒)"
echo "   工作目录 = $(pwd)"
echo "   日志目录 = $(pwd)/$LOG_DIR"
echo "=================================================="
echo

echo "--- 记录板端环境 -> $envfile ---"
{
    echo "collect_time: $(date 2>/dev/null)"
    echo
    echo "[uname -a]"
    uname -a 2>/dev/null || echo "uname 不可用"
    echo
    echo "[nproc]"
    nproc 2>/dev/null || echo "nproc 不可用"
    echo
    echo "[/proc/version]"
    cat /proc/version 2>/dev/null || echo "不可读"
    echo
    echo "[/proc/cpuinfo  前 30 行]"
    head -30 /proc/cpuinfo 2>/dev/null || echo "不可读"
    echo
    echo "[CPU 频率 / governor]"
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo "无 cpufreq 节点"
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true
    echo
    echo "[网络]"
    ip addr 2>/dev/null | grep 'inet ' || ifconfig 2>/dev/null | grep 'inet ' || echo "无法获取"
} > "$envfile" 2>&1

cat "$envfile"
echo

echo "--- 开始采集（$CYCLES 周期）---"
echo "    如果长时间停在 Connecting，请在 PC 侧确认模拟设备端已启动且网络可达。"
echo

./robot_linux_app -i "$PC_IP" -p "$PORT" -e 1 -m 1 --period "$PERIOD_MS" \
    --benchmark --cycles "$CYCLES" --metrics-csv "$csv" --log-dir "$LOG_DIR"
rc=$?

echo
echo "=================================================="
if [ "$rc" -eq 0 ] && [ -s "$csv" ]; then
    echo " 采集完成，退出码=$rc"
    ls -l "$csv" "$envfile" 2>/dev/null
    echo
    echo " PC 侧直接复算（无需 scp）:"
    echo "   python3 performance_evaluation/scripts/analyze_timing.py \\"
    echo "       <NFS_DIR>/lib/modules/4.1.15/$csv --label imx6ull_board"
    echo ""
    echo " 板端清理本次产物: rm -rf \"$LOG_DIR\""
    echo
    echo " 或者直接跑 PC 侧脚本 board_bench_pc.sh，它会自动复算并归档。"
else
    echo " 采集未成功，退出码=$rc"
    echo " 排查: 1) PC 侧模拟设备端是否已启动"
    echo "       2) 板端 ping $PC_IP 是否通"
    echo "       3) 端口 $PORT 是否被防火墙拦截"
fi
echo "=================================================="
exit "$rc"
