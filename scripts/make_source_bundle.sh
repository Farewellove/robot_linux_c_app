#!/usr/bin/env bash
#
# make_source_bundle.sh —— 生成 source_bundle.md（单文件看懂整个项目）
#
# 用法:
#   bash scripts/make_source_bundle.sh                # 输出到 ./source_bundle.md
#   bash scripts/make_source_bundle.sh /tmp/out.md    # 指定输出路径
#
# 设计原则：**这个文件要能自洽** —— 背景、拓扑、运行流程、协议、模块职责、设计取舍、
# 能力边界、实测数字全部内联在"1. 导读"里，不出现"详见 xxx 文档"这种需要跳出去读的引用。
# 后面各段是完整原文，供需要逐行核对时使用。
#
# 组成（顺序即阅读顺序）：
#   1. 导读（自洽速览）  2. 开发问题记录  3. 关键文档  4. 量化交付文档
#   5. 构建与忽略规则    6. 源码：头文件  7. 源码：实现  8. 测试与对端程序
#   9. 测量脚本          10. 附录：面试向文档
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
# 打包脚本自身不内联：它的 heredoc 里含有整段导读原文，内嵌会造成大段重复
BUILD_FILES=(Makefile .gitignore)
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
**阅读顺序**：① 导读（自洽速览）→ ② 开发问题记录（真实踩坑）→ ③ 关键文档 → ④ 量化交付文档 →
⑤ 构建与忽略规则 → ⑥ 源码：头文件 → ⑦ 源码：实现 → ⑧ 测试与对端程序 →
⑨ 测量脚本 → ⑩ 面试向附录。

**本文件自洽**：背景、拓扑、运行流程、协议、模块职责、设计取舍、能力边界、实测数字
全部写在"1. 导读"里，读完第 1 段即可完整理解本项目，不需要再翻任何别的文件。
后面各段是完整原文，供需要逐行核对时使用。

---

# 1. 导读

## 1.1 项目一句话

纯 C11 + POSIX 的机器人 TCP 通信上位机（从原 Qt/C++ 版重写而来，零第三方依赖），
并接入了开发板上的真实传感器：用户态读 ICM20608（六轴+温度）与 AP3216C（光/接近/红外）的
IIO sysfs，组包成 20 字节定长帧经 TCP 上传，已在 i.MX6ULL（ARMv7 / Linux 4.1.15）上实测。

## 1.2 背景与目标

- **原来的问题**：爬壁机器人焊接检测控制系统的上位机是 Qt5/C++ 写的，依赖
  Qt Widgets / Qt Network / Qt SerialPort / OpenGL / OpenCV 等大型库，
  要搬到嵌入式 ARM Linux 上就得交叉编译整套依赖链，体积大、资源占用高、部署困难。
- **做法**：只保留"通信 + 协议解析 + 数据记录"这个核心，用纯 C11 + POSIX 重写，
  去掉 GUI 与所有第三方依赖，做成命令行程序。
- **后来补上的**：原来对端只有虚拟数据（公式生成的 UWB/车体帧），
  现在把开发板上真实存在的 ICM20608 / AP3216C 接了进来，端到端跑的是真实物理量。

## 1.3 系统全貌

```
        开发板 i.MX6ULL（192.168.1.55）              PC 上位机（192.168.1.66）
  ┌──────────────────────────────────────┐        ┌──────────────────────────┐
  │ ICM20608 (SPI) ─┐                    │        │                          │
  │                 ├─ IIO 驱动（板端既有）│        │   robot_linux_app         │
  │ AP3216C  (I2C) ─┘   icm20608.ko     │        │   ├ sender_thread         │
  │                     ap3216c.ko      │        │   │   周期组帧 + send      │
  │        ↓ /sys/bus/iio/devices/...   │        │   └ receiver_thread       │
  │   sensor_robot（监听 19090）         │  TCP   │       阻塞 recv → 状态机   │
  │   单线程 select()：采样推帧 + 校验 ──┼═══════►│        → 打印 + CSV 日志   │
  │                                     │  20B帧 │                          │
  │   robot_linux_app（反向拓扑时用）    │◄═══════┤   mock_robot（监听，虚拟） │
  └──────────────────────────────────────┘        └──────────────────────────┘
```

三个可执行文件（都由本仓库编译出来）：

| 程序 | 角色 | 谁监听 / 谁连接 |
|---|---|---|
| `robot_linux_app` | **上位机**：周期下发 20 字节控制帧，接收并解析状态帧，打印 + 写 CSV | 主动连接对端 |
| `mock_robot` | **虚拟数据对端**（回归用）：按公式生成 UWB / 车体帧推送，并校验下行的帧头/帧尾/seq | 监听 |
| `sensor_robot` | **真实传感器对端**：读 IIO sysfs → 组 `'i'`/`'a'` 帧推送，同时校验下行帧 | 监听 |

两条链路方向相反，别连错：
- 模拟/回归：PC 跑 `mock_robot` 监听 → 板子跑 `robot_linux_app` 去连（板端填 PC 的 IP）；
- 真实传感器：板子跑 `sensor_robot` 监听 → PC 跑 `robot_linux_app` 去连（PC 填板子的 IP）。

## 1.4 程序怎么跑起来

启动链路（`src/main.c` 的 `main()`）：

```
解析命令行(getopt_long) → 参数校验 → 注册信号(SIGINT/SIGTERM，忽略 SIGPIPE)
  → tcp_connect_to_server()（含 TCP_NODELAY、SO_KEEPALIVE）
  → logger_open()（递归建 log/<项目名>/，写 4 行表头）
  → pthread_create(sender) + pthread_create(receiver)
  → join(sender) → shutdown() 唤醒阻塞的 recv → join(receiver) → cleanup
```

| 线程 | 职责 | 退出条件 |
|---|---|---|
| `sender_thread` | 每周期 `build_control_frame()` + `tcp_send_all()`，`clock_nanosleep` 定时（可选 `--abs-timer` 绝对定时、可选 `--benchmark` 打点） | `g_running=0` / 发送失败 / 达到 `--cycles` |
| `receiver_thread` | 阻塞 `recv` → `recv_buffer_feed()` 滑动窗口 → 解析出帧回调 `on_parsed_frame()` → 打印 + 写 CSV | `g_running=0` / `recv` 出错 |

进程内共享状态只有几个全局量：`g_running`（`volatile sig_atomic_t`，信号只置标志位）、
`g_sockfd`（一读一写，内核保证安全）、`g_log_fp`（只有 receiver 写，用 `g_print_mutex` 串行化
"打印 + 写文件"）。运行期默认**不做堆分配**（`--benchmark` 时才预分配指标数组）。

一条真实数据从传感器到 CSV 的完整路径：

```
sysfs 属性 *_raw → pread()（常驻 fd）→ icm20608_read() 得到 7 个 int16
  → build_imu_frame()（20 字节，帧尾 0x55 0x56）→ send() 经 TCP
  → PC 侧 recv() → recv_buffer_feed() 判帧尾 → 按 buf[0]=='i' 分派
  → parse_imu_frame() → imu_raw_to_physical()（/2048、/16.4、/326.8+25）
  → 控制台打印 + log/robot_linux_c_app/robot_log_<时间>.csv
```

## 1.5 三条必须守住的边界

1. **没有写内核驱动** —— 传感器数据走的是板端既有 IIO 驱动导出的 sysfs，本项目只用不改；
2. **不是硬实时** —— 周期偏差板端 p99 263 μs、传感器采样 p99 10.375 ms，都是普通 CFS 调度结果；
3. **没有真实机器人闭环** —— 有真实传感器，但下发的控制帧没有真实执行机构响应。

MD

  printf '## 1.6 文件清单\n\n```\n'
  find . -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.md' -o -name '*.sh' -o -name '*.py' -o -name 'Makefile' -o -name '.gitignore' \) \
    -not -path './.git/*' -not -path './obj/*' -not -path './output/*' -not -path './log/*' \
    -not -name 'source_bundle.md' \
    | sed 's|^\./||' | sort
  printf '```\n\n'

  cat <<'MD'
（`obj/`、`output/`、`log/` 是构建与运行产物，不入库；
`scripts/make_source_bundle.sh` 是打包脚本本身，内联会造成大段重复，故不收录，
需要时按文件头部的用法重跑即可。）

## 1.7 协议速查（上下行均 20 字节定长，小端，无 CRC）

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

## 1.8 模块速查（每个文件干什么）

| 文件 | 作用 |
|---|---|
| `src/main.c` | 入口：参数解析、双线程、信号、周期调度、join 前 `shutdown` |
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

## 1.9 设计取舍速览（为什么这么做）

| 取舍 | 选择 | 原因 |
|---|---|---|
| 帧边界怎么定 | 用**帧尾** `0x55 0x56` 而不是帧头 | 帧头有 `'u'/'c'/'i'/'a'` 四种不固定；后来加两种帧时状态机一行没改 |
| IMU 数据传什么 | 线上只传 **int16 原始值** | float 装不进 20 字节；换算系数以宏定义收发共用一份，不会两处漂移 |
| sysfs 怎么读 | 属性 fd **常驻** + `pread` | 每帧系统调用从 21 次（7×open/read/close）降到 7 次 |
| IIO 设备怎么找 | 遍历读 `name` 匹配 | 编号随加载顺序变（实测 device1=icm20608、device0=ap3216c），写死会取错 |
| 传感器代码放哪 | 用 `filter-out` **不链进上位机** | 保持 `robot_linux_app` 体积与既有行为不变，旧的性能数据仍可比 |
| 日志往哪写 | `<根>/<项目名>/`，默认 `log/robot_linux_c_app/` | 板端目录多项目共用，散落会互相污染 |
| 多字节字段怎么读 | 一律 `memcpy` | 避免 ARM 未对齐访问触发 SIGBUS |
| 退出怎么保证 | join(receiver) 前先 `shutdown` | 否则对端静默时 recv 永久阻塞、join 挂死（真实踩过，见第 2 段） |

## 1.10 实测结论（2026-10-13；原始 CSV 不入库，结论如下）

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

## 1.11 能力边界（这些**没有**做，别误以为有）

| 项 | 状态 |
|---|---|
| 协议校验（CRC / 长度字段 / 转义） | ❌ 无，帧尾只是同步标志 |
| 断线自动重连 | ❌ 断线即整体退出 |
| 应用层心跳 / 保活确认 | ❌ 只有 `SO_KEEPALIVE`（内核默认空闲 7200 s 才探测） |
| 串口通信（UART 直连机器人） | ❌ 未实现 |
| V4L2 摄像头采集 / 传输 | ❌ 未实现 |
| MQTT 遥测 | ❌ 未实现 |
| systemd 自启动 / 日志轮转 | ❌ 未实现 |
| 运行时改参数 / JSON 配置 | ❌ 未实现 |
| 传感器零偏与温漂校准 | ❌ 未做（只有原始物理量，不做姿态解算） |
| 实时调度（`SCHED_FIFO` / PREEMPT_RT） | ❌ 未做 |
| 内核驱动 / 设备树 | ❌ 本项目不含任何驱动与设备树代码 |
| 真实机器人（执行器）闭环 | ❌ 没有真实执行机构响应控制帧 |

## 1.12 测试覆盖速览

`tests/test_protocol.c` 共 **19 类用例**（直接编译生产源码 `src/robot_protocol.c`，不复制一份测试专用解析器）：

| 组 | 覆盖内容 |
|---|---|
| T01–T02 | 单帧完整到达、定点缩放正确性（`'u'` / `'c'`） |
| T03–T05 | 粘包（多帧同次输入）、半包（7+13 拆分）、10000 帧随机分块（固定种子） |
| T06–T08 | 垃圾前缀（`0x00` / `0xFF`）、坏帧尾恢复、帧尾合法但帧头未知 → 静默丢弃 |
| T09–T11 | 垃圾洪水后恢复、连接中断时残留半帧、帧尾字节出现在载荷中不误判 |
| T12–T15 | 长度不足/帧头不符的拒绝、控制帧布局与缓冲区下界、模式名映射、空输入 |
| T16–T18 | IMU 帧（含负值 int16 与换算）、AP3216C 帧、四种帧混合流的分派 |

另有 `make test-asan`（ASan + UBSan + 泄漏检测，仅覆盖"协议解析 + 测试代码"这条路径，
不代表全程序已通过 ASan）与 `make cross-check`（ARM 交叉编译可用性）。

## 1.13 板端部署与采集步骤

1. PC 侧：`make`（同时编 PC 与 ARM 两套）→ `make deploy`
   （把 `robot_linux_app` / `mock_robot` / `sensor_robot` / `board_bench_board.sh` 复制到
   NFS 根文件系统，板端对应路径 `/lib/modules/4.1.15/`，并预建 `log/robot_linux_c_app/`）
2. 板端：`cd /lib/modules/4.1.15 && ./load_drivers.sh`（加载 `icm20608.ko` / `ap3216c.ko`，
   需要内核开启 `CONFIG_IIO`，否则会报 `Unknown symbol iio_device_alloc`）
3. 真实数据：板端 `./sensor_robot -p 19090 --period 10 --imu-every 1 --ap-every 10`，
   PC `./output/pc/robot_linux_app -i 192.168.1.55 -p 19090 -e 0 -m 0 --period 10`
4. 周期调度采集：PC `bash performance_evaluation/scripts/board_bench_pc.sh 6000`，
   板端 `./board_bench_board.sh 192.168.1.66 6000`；产物落 `log/robot_linux_c_app/`

## 1.14 速查命令

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
