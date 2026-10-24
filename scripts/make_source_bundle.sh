#!/usr/bin/env bash
#
# make_source_bundle.sh —— 生成 source_bundle.md（单文件看懂整个项目）
#
# 用法:
#   bash scripts/make_source_bundle.sh                # 输出到 ./source_bundle.md
#   bash scripts/make_source_bundle.sh /tmp/out.md    # 指定输出路径
#
# 设计原则：**这个文件要能自洽** —— 关键结论（协议格式、模块职责、实测数字）全部内联在导读里，
# 尽量不出现"详见 xxx 文档"这种需要跳出去读的引用；被引用的文档虽然也在本文件后半段收录，
# 但能在导读里说清的就不让读者翻。
#
# 组成（顺序即阅读顺序）：
#   1. 导读：项目一句话、边界、文件清单、协议速查、模块速查、实测结论、速查命令
#   2. 开发问题记录：docs/开发问题记录.md（手写，改那份文件即可）
#   3. 关键文档 / 4. 量化交付文档 / 5. 构建与忽略规则
#   6. 源码：头文件 / 7. 源码：实现 / 8. 测试与对端程序 / 9. 测量脚本
#   10. 附录：面试向文档
#
# 说明：被打包的 Markdown 自身含有 ``` 代码块，因此外层统一用 4 个反引号包裹，
# 避免嵌套围栏截断内容。静态文本一律用 heredoc（<<'MD'）输出，不做转义。
#
set -u

cd "$(dirname "$0")/.." || exit 1

OUT="${1:-source_bundle.md}"
NARRATIVE="docs/开发问题记录.md"

DOC_FILES=(
  README.md
  docs/真实传感器接入与实测记录.md
  performance_evaluation/source_audit.md
  performance_evaluation/results/measurement_summary.md
)
QE_FILES=(
  performance_evaluation/README.md
  performance_evaluation/USAGE.md
  performance_evaluation/test_plan.md
  performance_evaluation/performance_report.md
  performance_evaluation/modification_report.md
  performance_evaluation/resume_improvements.md
  performance_evaluation/results/README.md
  performance_evaluation/logs/README.md
)
BUILD_FILES=(Makefile .gitignore scripts/make_source_bundle.sh)
HEADER_FILES=(include/*.h)
SRC_FILES=(src/*.c)
TEST_FILES=(tests/*.c)
SCRIPT_FILES=(performance_evaluation/scripts/*)
APPENDIX_FILES=(docs/项目源码面试审计报告.md docs/面试准备文档.md)

# ---------- 工具函数 ----------

lang_of() {
  case "$1" in
    *.c)      echo c ;;
    *.h)      echo c ;;
    *.sh)     echo bash ;;
    *.py)     echo python ;;
    *.md)     echo markdown ;;
    *)        echo text ;;
  esac
}

# 输出一个文件的完整内容（4 反引号围栏，避免内部 ``` 截断）
emit_file() {
  local f="$1"
  if [ ! -f "$f" ]; then
    printf '\n## 文件: %s\n\n_（文件不存在，已跳过）_\n' "$f"
    return
  fi
  printf '\n## 文件: %s\n\n' "$f"
  printf '````%s\n' "$(lang_of "$f")"
  cat "$f"
  printf '\n````\n'
}

emit_group() {
  local title="$1"; shift
  printf '\n---\n\n# %s\n' "$title"
  for f in "$@"; do emit_file "$f"; done
}

# ---------- 生成 ----------

{
  printf '# robot_linux_c_app 单文件项目包\n\n'
  printf '> 生成时间：%s\n' "$(date '+%Y-%m-%d %H:%M:%S')"
  printf '> 生成脚本：`scripts/make_source_bundle.sh`（源码或文档改动后重跑即可刷新本文件）\n\n'

  cat <<'MD'
**阅读顺序**：① 导读 → ② 开发问题记录（真实踩坑）→ ③ 关键文档 → ④ 量化交付文档 →
⑤ 构建与忽略规则 → ⑥ 源码：头文件 → ⑦ 源码：实现 → ⑧ 测试与对端程序 →
⑨ 测量脚本 → ⑩ 面试向附录。

**本文件自洽**：协议格式、模块职责、实测数字都在下面"1. 导读"里写全了，
不需要再翻别的文件；后面各段是完整原文，供需要逐行核对时使用。

---

# 1. 导读

## 项目一句话

纯 C11 + POSIX 的机器人 TCP 通信上位机（从 Qt/C++ 版重构而来，零第三方依赖），
并接入了开发板上的真实传感器：用户态读 ICM20608 / AP3216C 的 IIO sysfs，
组包成 20 字节定长帧经 TCP 上传，已在 i.MX6ULL（ARMv7 / Linux 4.1.15）上实测。

## 三条必须守住的边界

1. **没有写内核驱动** —— 传感器数据走的是板端既有 IIO 驱动导出的 sysfs，本项目只用不改；
2. **不是硬实时** —— 周期偏差板端 p99 263 μs、传感器采样 p99 10.375 ms，都是普通 CFS 调度结果；
3. **没有真实机器人闭环** —— 有真实传感器，但下发的控制帧没有真实执行机构响应。

## 两条链路的方向（别搞反）

- **模拟/回归**：PC 跑 `mock_robot` 监听 → 板子跑 `robot_linux_app` 去连（板端命令填 PC 的 IP）；
- **真实传感器**：板子跑 `sensor_robot` 监听 → PC 跑 `robot_linux_app` 去连（PC 命令填板子的 IP）。

MD

  printf '## 文件清单\n\n```\n'
  find . -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.md' -o -name '*.sh' -o -name '*.py' -o -name 'Makefile' -o -name '.gitignore' \) \
    -not -path './.git/*' -not -path './obj/*' -not -path './output/*' -not -path './log/*' \
    -not -name 'source_bundle.md' \
    | sed 's|^\./||' | sort
  printf '```\n\n'

  cat <<'MD'
（`obj/`、`output/`、`log/` 是构建与运行产物，不入库。）

## 协议速查（上下行均 20 字节定长，小端，无 CRC）

- 下行控制帧：`[0]=0x77`、`[2]=enable`、`[3]=mode`、`[5..7]=vx/vy/vz(int8)`、`[18]=seq`、`[19]=0x0A`
- 上行状态帧：帧尾统一 `[18]=0x55` `[19]=0x56`，按 `[0]` 分派四种：

| `[0]` | 含义 | 载荷（字节位） | 换算 |
|---|---|---|---|
| `'u'` | UWB 定位 | `[2..5]` tagNum(int32)、`[6..17]` x/y/z(float) | 原始值 |
| `'c'` | 车体状态 | `[1..3]` vx/vy/vz(int8)、`[4..5]` weld、`[6..7]` dAngle、`[8..11]` dDis(float)、`[12..17]` yaw/roll/pitch(int16) | 速度 /100，角度 /100，姿态 /32768×180 |
| `'i'` | IMU（ICM20608） | `[2..7]` accel xyz(int16)、`[8..13]` gyro xyz(int16)、`[14..15]` temp(int16)、`[16..17]` seq(uint16) | /2048→g，/16.4→°/s，/326.8+25→℃ |
| `'a'` | AP3216C | `[2..3]` als、`[4..5]` ps、`[6..7]` ir(int16)、`[16..17]` seq | 原始计数（驱动 scale=1） |

粘包处理：逐字节填 20 字节窗口 → 判 `[18][19]` → 匹配则按 `[0]` 分派并清零，
不匹配则 `memmove` 左移 1 字节重对齐（最坏丢 19 字节）。
已知边界：帧尾只是同步标志不是校验；帧尾合法但帧头不属于上述四种时，该 20 字节被静默丢弃。

## 模块速查（每个文件干什么）

| 文件 | 作用 |
|---|---|
| `src/main.c` | 入口：参数解析、双线程（sender/receiver）、信号、周期调度、join 前 `shutdown` |
| `src/robot_tcp.c` | socket 封装：connect / 循环 send / recv / NODELAY / KEEPALIVE |
| `src/robot_protocol.c` | 粘包状态机 + 四类帧的组帧与解析 + 原始值→物理量换算 |
| `src/logger.c` | CSV 日志：递归建目录、4 行表头、UWB/CAR/IMU/AP 四类记录、微秒时间戳 |
| `src/metrics.c` | `--benchmark` 才启用的逐周期指标采集与 P50/P95/P99 统计 |
| `src/app_config.c` | 配置默认值（含日志目录与 `ROBOT_LOG_ROOT`）与启动横幅 |
| `src/iio_util.c` | IIO 通用访问：按 `name` 定位设备、常驻 fd、`pread` 读整数 |
| `src/icm20608.c` | 六轴+温度读取（7 个 int16） |
| `src/ap3216c.c` | ALS/PS/IR 读取（3 个 int16） |
| `tests/test_protocol.c` | 19 类协议用例，直接编译生产源码 |
| `tests/mock_robot.c` | 虚拟数据对端（TCP 服务端，含下行帧校验） |
| `tests/sensor_robot.c` | 真实传感器对端（单线程 `select()`：采样推帧 + 校验下行帧） |

**日志与产物**：一律落在 `<LOG_ROOT>/<项目名>/`，默认 `log/robot_linux_c_app/`；
覆盖顺序 默认值 < 环境变量 `ROBOT_LOG_ROOT` < `--log-dir`；`make clean-logs` 只清这一层。

## 实测结论（2026-10-13；原始 CSV 不入库，结论如下）

环境：正点原子 i.MX6ULL，ARMv7 rev5 单核（BogoMIPS 12.00），Linux 4.1.15 #31 SMP PREEMPT，
PC `192.168.1.66` ↔ 板 `192.168.1.55`，普通 CFS 调度。

**周期调度**（`--benchmark`，10 ms，4741 样本，相对睡眠）：

| 指标 | 值 |
|---|---|
| 工作耗时（组帧+发送） | mean 88.2 μs，p95 97.7 μs，p99 143.0 μs，max 357.7 μs |
| 周期偏差 \|d\| | mean 194.6 μs，p95 207.3 μs，p99 263.0 μs，max 489.3 μs |
| 实际周期 start→start | mean 10.1946 ms，min 10.1640 ms，max 10.4893 ms |
| 发送错误 | 0 |

偏差恒为正 → 印证"相对睡眠不补偿工作耗时"，这也是 `--abs-timer` 存在的理由。

**真实传感器链路**（27.4 s，板端 `sensor_robot` → PC）：

| 指标 | 值 |
|---|---|
| 帧数 | 板端发 IMU 2741 / AP 274，PC 收到 2741 / 274 —— **零丢帧** |
| 下行控制帧 | 收 2699 帧，坏帧 0，seq 跳变 0 |
| 读取/发送错误 | IMU 0、AP 0、send 0 |
| 采样周期 | 平均 10.000 ms；p50 9.999 / p90 10.215 / p99 10.375 ms；最坏 46.838 ms（仅 1 次 >12 ms） |
| 采样率 | IMU 100.03 Hz，AP 10.04 Hz |
| 温度 | 均值 34.61 ℃，全程漂移 −0.02 ℃ |
| 姿态 | 静置段 az ≈ +1 g（量纲换算正确）；gyro 静止零偏约 −1.0 / +1.6 / −0.5 °/s，峰值 ±192 °/s |
| AP3216C | als 均值 260（14–455），ps 0–7，ir 多为 0 |
| IIO 节点 | `iio:device1/`=icm20608，`iio:device0/`=ap3216c（编号会变，必须按 name 匹配） |

判定：IIO sysfs + 20 字节帧这条真实数据链路在板上跑通；抖动来自用户态轮询
（7 次 sysfs 读 + `clock_nanosleep`），不是中断触发，因此不能称硬实时采样。

## 速查命令

```bash
make                # PC + ARM 全量编译（0 warning 0 error）
make test           # 协议状态机 19 类用例
make test-asan      # ASan + UBSan + 泄漏检测
make deploy         # 交叉编译并部署到 NFS 板端目录
make clean-logs     # 只清 log/robot_linux_c_app/ 下的运行期日志

# 本机自测（虚拟数据）
./output/pc/mock_robot -b 127.0.0.1 -p 19090 --period 10
./output/pc/robot_linux_app -i 127.0.0.1 -p 19090 --period 10 --cycles 500

# 真实传感器（板端监听，PC 去连）
#  板端: ./load_drivers.sh && ./sensor_robot -p 19090 --period 10 --imu-every 1 --ap-every 10
#  PC  : ./output/pc/robot_linux_app -i 192.168.1.55 -p 19090 -e 0 -m 0 --period 10

# 无硬件也能自测真实链路（造假 sysfs 树）
mkdir -p /tmp/fake_iio/iio:device0 && echo icm20608 > /tmp/fake_iio/iio:device0/name
./output/pc/sensor_robot --iio-root /tmp/fake_iio -p 19090 --period 10 --frames 60
```
MD

  # 第 2 段：开发问题记录（手写）
  printf '\n---\n\n# 2. 开发问题记录（真实踩坑 / 原因 / 解决）\n'
  if [ -f "$NARRATIVE" ]; then
    printf '\n'
    tail -n +2 "$NARRATIVE"   # 去掉自带的一级标题，避免与章节标题重复
  else
    printf '\n_（缺少 %s）_\n' "$NARRATIVE"
  fi

  emit_group "3. 关键文档" "${DOC_FILES[@]}"
  emit_group "4. 量化交付文档" "${QE_FILES[@]}"
  emit_group "5. 构建与忽略规则" "${BUILD_FILES[@]}"
  emit_group "6. 源码：头文件" "${HEADER_FILES[@]}"
  emit_group "7. 源码：实现" "${SRC_FILES[@]}"
  emit_group "8. 测试与对端程序" "${TEST_FILES[@]}"
  emit_group "9. 测量脚本" "${SCRIPT_FILES[@]}"
  emit_group "10. 附录：面试向文档（工程结论以第 1、3 段为准）" "${APPENDIX_FILES[@]}"

  printf '\n---\n\n'
  printf '_本文件由 `scripts/make_source_bundle.sh` 生成，请勿手工编辑；_\n'
  printf '_叙述性内容请改 `docs/开发问题记录.md` 后重跑脚本。_\n'
} > "$OUT"

echo "已生成: $OUT"
echo "  行数: $(wc -l < "$OUT")"
echo "  大小: $(du -h "$OUT" | cut -f1)"
