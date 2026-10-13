#!/usr/bin/env bash
# board_bench_pc.sh —— PC 侧一键编排：起模拟设备端 → 等开发板连接 → 等采集完成
#                      → 自动复算统计 → 归档 board_*.csv
#
# 用法:
#   bash performance_evaluation/scripts/board_bench_pc.sh                 # 默认 6000 周期
#   bash performance_evaluation/scripts/board_bench_pc.sh 12000           # 自定义周期数
#   CYCLES=3000 PORT=19090 bash .../board_bench_pc.sh
#
# 可覆盖的环境变量:
#   CYCLES          采集周期数，默认 6000（10ms × 6000 = 60 秒）
#   PORT            模拟设备端监听端口，默认 19090
#   PERIOD_MS       发送周期，默认 10
#   ACCEPT_TIMEOUT  等待开发板连接的秒数，默认 300
#   NFS_DIR         NFS 根文件系统路径，默认 /home/why/zdyz/nfs/rootfs
#   CSV_NAME        板端写入的指标 CSV 文件名，默认 board_timing.csv
#   BOARD_LOG_DIR   板端日志目录（相对板端工作目录），默认 log/robot_linux_c_app
#
# 执行前请确认: 已在另一终端（或稍后）于板端运行 board_bench_board.sh。
# 本脚本会先打印板端需要执行的命令，然后开始等待连接。

set -u

cd "$(dirname "$0")/../.." || exit 1

CYCLES="${1:-${CYCLES:-6000}}"
PORT="${PORT:-19090}"
PERIOD_MS="${PERIOD_MS:-10}"
ACCEPT_TIMEOUT="${ACCEPT_TIMEOUT:-300}"
NFS_DIR="${NFS_DIR:-/home/why/zdyz/nfs/rootfs}"
BOARD_DIR_REL="lib/modules/4.1.15"
BOARD_DIR="$NFS_DIR/$BOARD_DIR_REL"

# 板端所有产物都写在 log/<项目名>/ 下（见 board_bench_board.sh），这里按同一约定取
BOARD_LOG_REL="${BOARD_LOG_DIR:-log/robot_linux_c_app}"
BOARD_LOG_DIR_ABS="$BOARD_DIR/$BOARD_LOG_REL"
CSV_NAME="${CSV_NAME:-board_timing.csv}"
BOARD_CSV="$BOARD_LOG_DIR_ABS/$CSV_NAME"

PC_BIN="output/pc"
MOCK_BIN="$PC_BIN/mock_robot"

LOG_DIR="performance_evaluation/logs"
RES_DIR="performance_evaluation/results"
mkdir -p "$LOG_DIR" "$RES_DIR"

TS="$(date +%Y%m%d_%H%M%S)"
MOCK_LOG="$LOG_DIR/board_mock_${TS}.log"
MOCK_CSV="$RES_DIR/board_mock_server_${TS}.csv"
TIMING_ARCHIVE="$RES_DIR/board_periodic_timing_${TS}.csv"

# ---------------- 基本检查 ----------------
if [ ! -x "$MOCK_BIN" ]; then
    echo "错误: 缺少 $MOCK_BIN，请先执行: make pc"
    exit 1
fi
if [ ! -d "$BOARD_DIR" ]; then
    echo "错误: 板端工作目录不存在: $BOARD_DIR"
    echo "      确认 NFS 根文件系统路径（可用 NFS_DIR=... 覆盖）"
    exit 1
fi
if [ ! -f "$BOARD_DIR/board_bench_board.sh" ]; then
    echo "提示: $BOARD_DIR 下没有 board_bench_board.sh"
    echo "      请先在项目根目录执行: make deploy"
    echo
fi

# 探测 PC 用于板端连接的 IP（优先 192.168.1.x 有线网段）
PC_IP="$(ip -4 addr show 2>/dev/null | awk '/inet 192\.168\.1\./ {sub("/.*","",$2); print $2; exit}')"
if [ -z "$PC_IP" ]; then
    PC_IP="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src") print $(i+1)}')"
fi
[ -n "$PC_IP" ] || PC_IP="<PC的IP>"

# 旧 CSV 先挪走，便于判断本次是否真的产出了数据
if [ -f "$BOARD_CSV" ]; then
    mv -f "$BOARD_CSV" "${BOARD_CSV}.prev"
    echo "注意: 已把上一次的 $CSV_NAME 挪到 ${CSV_NAME}.prev"
fi

echo "=================================================================="
echo " PC 侧编排: 开发板调度指标采集"
echo "   PC_IP         = $PC_IP"
echo "   PORT          = $PORT"
echo "   PERIOD        = ${PERIOD_MS} ms"
echo "   CYCLES        = $CYCLES  (约 $((CYCLES * PERIOD_MS / 1000)) 秒)"
echo "   等待连接超时   = ${ACCEPT_TIMEOUT} 秒"
echo "   板端工作目录   = $BOARD_DIR"
echo "   板端日志目录   = $BOARD_LOG_DIR_ABS"
echo "=================================================================="
echo
echo ">>> 现在请在【开发板串口】执行以下命令:"
echo
echo "    cd /lib/modules/4.1.15"
echo "    ./board_bench_board.sh $PC_IP $CYCLES"
echo
echo "（也可以在 PC 上另开一个终端用 output/pc/robot_linux_app 模拟板端做联调）"
echo

# ---------------- 启动模拟设备端 ----------------
"$MOCK_BIN" -b 0.0.0.0 -p "$PORT" --period "$PERIOD_MS" \
    --frames 100000000 --accept-timeout "$ACCEPT_TIMEOUT" \
    --csv "$MOCK_CSV" > "$MOCK_LOG" 2>&1 &
MOCK_PID=$!

cleanup() {
    kill "$MOCK_PID" 2>/dev/null
    wait "$MOCK_PID" 2>/dev/null
}
trap cleanup EXIT INT TERM

echo "[1/3] 模拟设备端已启动 (pid=$MOCK_PID)，等待开发板连接 ..."

connected=no
i=0
while [ "$i" -lt "$ACCEPT_TIMEOUT" ]; do
    if ! kill -0 "$MOCK_PID" 2>/dev/null; then
        break
    fi
    if grep -q 'client connected' "$MOCK_LOG" 2>/dev/null; then
        connected=yes
        break
    fi
    if [ $((i % 15)) -eq 0 ] && [ "$i" -gt 0 ]; then
        echo "      ... 等待中 (${i}s / ${ACCEPT_TIMEOUT}s)"
    fi
    sleep 1
    i=$((i + 1))
done

if [ "$connected" != "yes" ]; then
    echo
    echo "[!] 未等到开发板连接。请检查:"
    echo "    1) 板端是否已执行 ./board_bench_board.sh $PC_IP $CYCLES"
    echo "    2) 板端能否 ping 通 $PC_IP"
    echo "    3) PC 防火墙是否拦截 $PORT 端口 (sudo ufw status)"
    echo
    echo "    模拟设备端日志: $MOCK_LOG"
    exit 1
fi

echo
echo "[2/3] 开发板已连接，采集进行中（约 $((CYCLES * PERIOD_MS / 1000)) 秒）..."
echo "      $(grep 'client connected' "$MOCK_LOG" | head -1)"

# 等模拟端退出（板端程序跑完 --cycles 后会断开，模拟端随之退出）
WAIT_MAX=$(( CYCLES * PERIOD_MS / 1000 + 120 ))
j=0
while [ "$j" -lt "$WAIT_MAX" ]; do
    kill -0 "$MOCK_PID" 2>/dev/null || break
    if [ -f "$BOARD_CSV" ] && [ $((j % 20)) -eq 0 ] && [ "$j" -gt 0 ]; then
        echo "      ... 采集中 (${j}s)"
    fi
    sleep 1
    j=$((j + 1))
done
wait "$MOCK_PID" 2>/dev/null
trap - EXIT INT TERM

echo
echo "[3/3] 采集结束，处理结果 ..."
echo

# ---------------- 结果校验与复算 ----------------
if [ ! -s "$BOARD_CSV" ]; then
    echo "[!] 未找到板端指标 CSV: $BOARD_CSV"
    echo "    模拟设备端日志尾部:"
    tail -5 "$MOCK_LOG" | sed 's/^/      /'
    exit 1
fi

ROWS=$(wc -l < "$BOARD_CSV")
echo "板端指标 CSV : $BOARD_CSV  (${ROWS} 行)"
cp -f "$BOARD_CSV" "$TIMING_ARCHIVE"
echo "已归档       : $TIMING_ARCHIVE"

# 板端环境记录
ENV_LATEST="$(ls -t "$BOARD_LOG_DIR_ABS"/board_env_*.txt 2>/dev/null | head -1)"
if [ -n "$ENV_LATEST" ]; then
    cp -f "$ENV_LATEST" "$RES_DIR/board_env_${TS}.txt"
    echo "板端环境记录 : $RES_DIR/board_env_${TS}.txt"
fi

if [ -f "$MOCK_CSV" ]; then
    echo "模拟端统计   : $MOCK_CSV"
fi

echo
echo "---------------- 独立复算（analyze_timing.py）----------------"
if command -v python3 >/dev/null 2>&1; then
    python3 performance_evaluation/scripts/analyze_timing.py \
        "$TIMING_ARCHIVE" --label "board_imx6ull" --period-ns $((PERIOD_MS * 1000000))
else
    echo "python3 不可用，跳过复算"
fi
echo "-------------------------------------------------------------"
echo
echo "模拟设备端下行校验（坏帧 / seq 跳变 必须为 0）:"
if [ -f "$MOCK_CSV" ]; then
    cat "$MOCK_CSV" | sed 's/^/  /'
else
    grep -E '^\[MOCK\] summary' "$MOCK_LOG" | sed 's/^/  /'
fi
echo
echo "板端环境摘要（供报告引用）:"
if [ -n "$ENV_LATEST" ]; then
    grep -E 'Linux|processor|BogoMIPS|model name|cpu MHz|^[0-9]+$' "$ENV_LATEST" | head -12 | sed 's/^/  /'
fi
echo
echo "=================================================================="
echo " 完成。产物:"
echo "   板端原始 CSV : $TIMING_ARCHIVE"
echo "   模拟端统计   : $MOCK_CSV"
echo "   模拟端日志   : $MOCK_LOG"
echo " 下一步: 把上面的复算输出与板端环境摘要提供给分析方，"
echo "         即可把 performance_report.md 的汇总表更新为板端实测值。"
echo "=================================================================="
