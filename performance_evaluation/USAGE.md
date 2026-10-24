# USAGE.md —— 项目使用说明与标准流程

> 本文回答"这个项目怎么用、按什么流程走"。
> 所有命令均在 `/home/why/C/robot_linux_c_app` 根目录下执行，且已在
> Ubuntu 22.04 / x86_64 / gcc 11.4.0 环境实际验证通过。
>
> 相关文档：`README.md`（交付物索引）、`test_plan.md`（测试口径）、
> `performance_report.md`（实测数据）、`source_audit.md`（能力边界与缺陷）。

---

## 0. 先认识文件（30 秒）

```
robot_linux_c_app/
├── README.md               ★ 唯一入口：结构 / 构建 / 运行 / 协议说明
├── Makefile                所有操作入口
├── src/                    生产代码
│   ├── main.c              入口 / 双线程 / 信号 / 周期打点  ← 改动集中在这里
│   ├── robot_protocol.c    20B 定长帧 + 帧尾同步粘包状态机 + 解析
│   ├── robot_tcp.c         socket 封装（循环 send / recv / NODELAY / KEEPALIVE）
│   ├── logger.c            CSV 日志
│   ├── app_config.c        命令行参数默认值与打印
│   └── metrics.c           周期指标采集（新增，默认关闭）
├── include/                对应头文件
├── tests/
│   ├── test_protocol.c     协议状态机测试（直接编译生产源码）
│   └── mock_robot.c        loopback 模拟机器人服务端
├── logs/                   程序运行时的 CSV 日志输出目录
├── docs/                   历史与个人参考文档（不参与构建，含归档说明）
└── performance_evaluation/ 性能量化交付物
    ├── README.md           ★ 交付物索引，先看这个
    ├── USAGE.md            ★ 本文档
    ├── source_audit.md     源码审计（线程模型 / 协议约束 / 缺陷 / 能力边界）
    ├── test_plan.md        测试环境、命令、指标定义、验收标准
    ├── performance_report.md  ★ 实测数据与结论
    ├── modification_report.md 改了什么、为什么、怎么回退
    ├── resume_improvements.md 简历描述（每条挂钩证据）
    ├── scripts/            可复现脚本
    ├── results/            原始 CSV（所有数字的唯一来源）
    └── logs/               原始日志（含 git diff、socket 观测）
```

**核心原则**
1. 报告里每个数字都能在 `results/` 找到对应的原始 CSV。
2. 执行 `scripts/` 里的脚本可以完整重放整轮实验。
3. 没有实测支撑的能力一律标注 `NOT_RUN`，不写进结论。

---

## 1. 一次性准备：构建

```bash
cd /home/why/C/robot_linux_c_app

make clean && make      # 同时构建 PC 与 ARM 两套 → output/{pc,arm}/
make pc                 # 只想在 PC 上跑，可只编译 PC 侧
```

**通过标准**：0 warning 0 error（编译选项含 `-Wall -Wextra`）。

可用目标一览：

| 目标 | 作用 |
|---|---|
| `make`（默认） | 编译 **PC + ARM 两套**到 `output/` |
| `make pc` | 只编译 PC 侧（gcc） |
| `make arm` | 只编译 ARM 侧（交叉编译） |
| `make deploy` | 把 ARM 程序复制到板端工作目录（默认 `rootfs/lib/modules/4.1.15`，与其它外设测试程序同目录） |
| `make deploy NFS_DIR=<path>` | 指定其它 NFS 根文件系统 |
| `make test` | 构建并运行协议状态机测试 |
| `make test-asan` | 用 ASan+UBSan 重编译并运行协议测试 |
| `make cross-check` | ARM 交叉编译可用性检查（完整编译 + 链接） |
| `make clean` | 清理 `obj/` 与 `output/` |
| `make help` | 列出目标与产物角色 |

### 产物与角色（`output/`）

| 产物 | 角色 | 运行位置 |
|---|---|---|
| `output/pc/mock_robot` | **模拟设备端**（对端，主动推送 20B 状态帧） | PC |
| `output/arm/robot_linux_app` | **接收端**（上位机，主用） | **开发板** |
| `output/pc/robot_linux_app` | 接收端 | PC（本机自测） |
| `output/arm/mock_robot` | 模拟设备端 | 开发板（反向拓扑时用） |

---

## 2. 日常开发流程

### 2.1 只改了协议解析（`src/robot_protocol.c`）→ 秒级回归

```bash
make test          # 16 类用例，含 10000 帧固定种子随机分块
make test-asan     # 越界 / 未定义行为 / 泄漏检查
```

**通过标准**
- 输出 `=== summary: 16 passed, 0 failed ===`，退出码 0
- `make test-asan` 无任何 sanitizer 报告

### 2.2 改了通信 / 线程 / 定时（`main.c`、`robot_tcp.c`）→ 起模拟对端跑端到端

```bash
# 终端 A：模拟设备端（--silent = 只接不发，模拟空载）
./output/pc/mock_robot -b 127.0.0.1 -p 19090 --silent --accept-timeout 60

# 终端 B：接收端
./output/pc/robot_linux_app -i 127.0.0.1 -p 19090 -e 1 -m 1 \
    --period 10 --cycles 500 --benchmark
```

**检查三处**
1. `[METRICS]` 汇总里的 `send_errors` 必须为 0；
2. `[MOCK] summary` 里的 `rx_bad_ctrl_frames` 与 `rx_seq_gaps` 必须为 0；
3. 进程能自行退出（`--cycles 500` 跑完即退），无残留进程。

模拟端常用参数：

| 参数 | 作用 |
|---|---|
| `-b <ip>` | **监听地址，默认 `0.0.0.0`（跨机测试必需）**；本机自测可用 `-b 127.0.0.1` |
| `-p <port>` | 监听端口（默认 19090） |
| `--period <ms>` | 状态帧推送周期（默认 10） |
| `--frames <n>` | 最多推送 n 帧（0 = 不限） |
| `--silent` | 只接收不发送（测空载 / 静默对端行为） |
| `--close-after <n>` | 推 n 帧后主动断开（测断线检测） |
| `--rst` | 断开时发 RST 而非 FIN |
| `--csv <path>` | 校验结果 CSV 输出路径 |
| `--help` | 用法说明 |

### 2.3 提交前自检清单

| 检查项 | 命令 | 通过标准 |
|---|---|---|
| 编译干净 | `make clean && make` | 0 warning 0 error |
| 协议回归 | `make test` | 16/16 PASS |
| 内存安全 | `make test-asan` | 无报告 |
| ARM 仍可编译 | `make cross-check` | `cross-check OK` |
| 默认行为未变 | 不带任何新增 flag 跑一次 | 通信正常，无额外输出 |

---

## 3. 性能量化流程（需要出数据时跑，约 8 分钟）

```bash
# ① 周期调度基准：4 场景 × 10000 周期 ≈ 7 分钟
bash performance_evaluation/scripts/run_all_bench.sh 10000

# ② 端到端 + 断线 + 退出时序 ≈ 30 秒
bash performance_evaluation/scripts/run_disconnect_test.sh
bash performance_evaluation/scripts/run_sigint_hang_test.sh output/pc/robot_linux_app after_shutdown_fix 6

# ③ 长期资源观测：30 分钟，挂后台跑，不要在前台等
nohup bash performance_evaluation/scripts/run_soak.sh 1800 5 \
      > performance_evaluation/logs/soak_run.log 2>&1 &

# ④ 独立复算（不信任程序自己打印的汇总）
python3 performance_evaluation/scripts/analyze_timing.py \
    performance_evaluation/results/03_periodic_timing_traffic_relative.csv \
    --label traffic_relative

python3 performance_evaluation/scripts/analyze_resource.py \
    performance_evaluation/results/03_resource_usage_soak.csv --label soak_30min
```

### 场景矩阵

| 场景 | 对端 | CPU 负载 | 用途 |
|---|---|---|---|
| `idle` | 只接不发 | 无 | 空载基线 |
| `traffic` | 每 10ms 推送一帧 | 无 | 真实业务收发 |
| `load` | 每 10ms 推送一帧 | 占满全部核心 | 观察 CPU 竞争下抖动劣化 |
| `traffic` + `--abs-timer` | 同 traffic | 无 | 对比绝对时间定时的漂移改善 |

### 注意事项

1. **跑基准时不要同时做别的事**（编译、下载、开视频都会污染结果）。
2. `idle` 是空载基线，跑它的时候更不能有额外负载。
3. 运行期程序不做任何 printf / 文件写入，测量本身不干扰被测周期。
4. 百分位采用最近秩法，脚本与程序内置算法一致 —— 两者结果应逐项相同，不同即说明有 bug。

---

## 4. 连接真实机器人

```bash
./output/pc/robot_linux_app -i <机器人IP> -p <端口> -e 1 -m 1 \
    --vx 0 --vy 0 --vz 0 --period 10
```

（若在开发板上直连机器人，把路径换成 `/lib/modules/4.1.15/robot_linux_app`。）

| 参数 | 说明 |
|---|---|
| `-i` / `-p` | 机器人 IP / TCP 端口 |
| `-e 0\|1` | 使能位（默认 0 = 不使能，避免连上就动） |
| `-m 0\|1\|2` | 模式：STOP / MANUAL / AUTO |
| `--vx/--vy/--vz` | int8 速度分量 |
| `--period` | 发送周期（ms，默认 10） |

只想采集数据不打断流程：

```bash
./output/pc/robot_linux_app -i <IP> -p <端口> -e 1 -m 1 \
    --benchmark --cycles 6000 --metrics-csv /tmp/real_robot_timing.csv
# 约 60 秒后自动退出，并写出 /tmp/real_robot_timing.csv
```

**退出**：`Ctrl+C`（退出时序缺陷已修复，实测退出时延 0.102 s）。

**必须知道的边界**
- 程序**没有断线自动重连**，断线即整体退出；
- `SO_KEEPALIVE` 使用内核默认参数（约 7200 s），**拔网线不会在秒级内被发现**；
- 协议**无 CRC / 校验字段**，无法检测载荷内容损坏。

---

## 5. 板端（ARM Linux）实测流程

### 5.1 拓扑与角色

```
┌──────────────────────────────┐              ┌──────────────────────────────┐
│  PC（192.168.1.66）           │   以太网      │  开发板（i.MX6ULL）           │
│  output/pc/mock_robot         │ ◄──────────► │  /lib/modules/4.1.15/         │
│                               │              │      robot_linux_app          │
│  = 模拟设备端（对端）          │              │  = 接收端（上位机）            │
│  · 监听 0.0.0.0:19090         │   20B 控制帧  │  · 每 10ms 发控制帧            │
│  · 每 10ms 推送 20B 状态帧     │ ───────────► │  · 粘包解析 UWB/CAR            │
│  · 逐帧校验下行帧与 seq        │ ◄─────────── │  · 写 CSV 日志 + --benchmark   │
└──────────────────────────────┘   20B 状态帧  └──────────────────────────────┘
```

NFS 根文件系统已配置：`/etc/exports` 中 `/home/why/zdyz/nfs/rootfs 192.168.1.0/24(...)`，
本机有线网卡 `enp8s0` = **192.168.1.66**。

**部署目录沿用板端既有约定**：板端把所有外设测试程序与驱动都放在
`/lib/modules/4.1.15`（其 `load_drivers.sh` 内 `MOD_DIR=/lib/modules/4.1.15`，
同目录还有 `ap3216cAPP`、`gt911_recovery_test`、`iio_bench`、各 `*.ko`）。
本项目部署到同一目录，便于板端统一管理、并让测量 CSV 直接落在 NFS 共享目录里。

| 位置 | 路径 |
|---|---|
| 主机侧 | `/home/why/zdyz/nfs/rootfs/lib/modules/4.1.15/` |
| 板端 | `/lib/modules/4.1.15/` |

### 5.2 交叉编译与部署（主机侧）

```bash
make arm        # 只编译 ARM 侧 → output/arm/
make deploy     # 复制到板端工作目录 rootfs/lib/modules/4.1.15
# 自定义根文件系统路径：make deploy NFS_DIR=/path/to/rootfs
# 自定义板端子目录：    make deploy BOARD_SUBDIR=root
```

`deploy` 会打印板端可直接执行的命令。

**ABI 兼容性（已实测确认）**

| 检查项 | 结果 |
|---|---|
| 产物架构 | `ELF32 / ARM / EXEC` |
| 动态加载器 | `/lib/ld-linux-armhf.so.3` |
| 动态依赖 | 仅 `libpthread.so.0`、`libc.so.6` |
| 要求的最高 GLIBC 符号版本 | `GLIBC_2.17` |
| NFS rootfs 的 glibc | `2.19-2014.08-1-git` |
| 交叉工具链 sysroot 的 glibc | `2.19-2014.08-1-git`（**与 rootfs 完全一致**） |

结论：**产物可直接在板上运行，无需额外拷贝库**。

### 5.3 一键采集：两个脚本（推荐）

只涉及两个脚本，各管一端：

| 脚本 | 运行位置 | 职责 |
|---|---|---|
| `board_bench_pc.sh` | **PC** | 起模拟设备端 → 等开发板连接 → 等采集结束 → 自动复算 + 归档 + 校验 |
| `board_bench_board.sh` | **开发板** | 记录板端环境 → 运行接收端采集 60 秒 → CSV 落在 NFS 目录 |

`board_bench_board.sh` 由 `make deploy` 一并复制到 `/lib/modules/4.1.15`，无需手工拷贝。

**终端 1（PC）**
```bash
bash performance_evaluation/scripts/board_bench_pc.sh 6000
```
脚本会先打印板端该敲的命令，然后阻塞等待连接（默认最多 300 秒）。

**串口（开发板）**
```bash
cd /lib/modules/4.1.15
./board_bench_board.sh 192.168.1.66 6000
```

跑完 PC 侧会自动输出：
- `analyze_timing.py` 的统计结果（mean / P95 / P99 / max）
- 归档路径 `results/board_periodic_timing_<时间>.csv`
- 板端环境记录 `results/board_env_<时间>.txt`
- 模拟端下行校验（`rx_bad_ctrl_frames` 与 `rx_seq_gaps` 必须为 0）

**联调验证（不需要真板子）**：用 PC 程序冒充板端，一样能跑通整条链路：
```bash
bash performance_evaluation/scripts/board_bench_pc.sh 500 &
./output/pc/robot_linux_app -i 127.0.0.1 -p 19090 -e 1 -m 1 --period 10 \
    --benchmark --cycles 500 \
    --metrics-csv /home/why/zdyz/nfs/rootfs/lib/modules/4.1.15/board_timing.csv
```

### 5.4 手工等价命令（不使用脚本时）

```bash
# PC：起模拟设备端（必须监听 0.0.0.0 才能被板子连上）
./output/pc/mock_robot -p 19090 --period 10 --frames 1000000 \
    --accept-timeout 300 \
    --csv performance_evaluation/results/board_mock_server.csv

# 板端串口：记录环境 + 运行（产物统一进 log/robot_linux_c_app/）
cd /lib/modules/4.1.15
uname -a; nproc; head -25 /proc/cpuinfo
mkdir -p log/robot_linux_c_app
./robot_linux_app -i 192.168.1.66 -p 19090 -e 1 -m 1 --period 10 \
    --benchmark --cycles 6000 \
    --metrics-csv log/robot_linux_c_app/board_timing.csv

# PC：复算（CSV 就在 NFS 目录里，无需 scp）
python3 performance_evaluation/scripts/analyze_timing.py \
    /home/why/zdyz/nfs/rootfs/lib/modules/4.1.15/log/robot_linux_c_app/board_timing.csv \
    --label imx6ull
```

板端注意：
- **产物统一写进 `log/robot_linux_c_app/`（NFS 共享目录），PC 侧无需 scp 即可读取**；
  不再直接散落在 `/lib/modules/4.1.15/`（该目录是多个项目共用的工作目录）；
  换根目录用 `ROBOT_LOG_ROOT` 或 `--log-dir`；
- 板端一般**没有 python3**，`analyze_*.py` 在主机侧运行；
- `--cycles 6000`（60 秒）占用约 `6000 × 40 B = 240 KB` 内存，i.MX6ULL 完全够用；
- 若要看核心间差异可用 `taskset -c 0` 固定到 CPU0（**注意这会改变调度条件，需在报告中说明**）。

**板端数据归档命名必须带 `board_` 前缀**，与 PC 模拟数据严格区分。用脚本时这个前缀由脚本自动加上。

### 5.5 当前状态

| 项目 | 状态 |
|---|---|
| ARM 交叉编译（`make arm` / `make deploy`） | ✅ 已通过，产物已部署到 NFS |
| ABI / glibc 兼容性核对 | ✅ 已核对（见 5.2） |
| 跨机模式验证（PC 监听 0.0.0.0 ↔ 通过 192.168.1.66 连接） | ✅ 已实测通过 |
| **板端周期调度实测** | ✅ **已实测**（4741 周期，结论见 `results/measurement_summary.md`） |
| **真实传感器数据实测** | ✅ **已实测**（2741 条 IMU + 274 条 AP，结论见 `results/measurement_summary.md`） |

板端数据采集完成后，需在 `performance_report.md` 的汇总表中**新增板端列**，
并把每一项从"PC 模拟值"更新为"板端实测值"，同时保留 PC 值作为对照。

---

## 6. 排查速查表

| 现象 | 先查什么 |
|---|---|
| 连不上机器人 | `ss -tnp \| grep <端口>`；对端是否在监听；IP 是否可达 |
| 连上了但没有任何输出 | 是否传了 `-e 1`；换成 `mock_robot` 验证是解析问题还是对端问题 |
| 退出卡住 | 已修复（`join` 前 `shutdown`）；若仍有，`kill -INT <pid>` 后看日志是否走到 `Shutting down...` |
| 基准数据异常好 | 是否误开了空闲的 CPU；用 `nproc` 忙循环是否真的起来了 |
| 基准数据异常差 | 是否有残留忙循环进程（`pgrep -a 'while'`）；是否有其他重负载 |
| 控制台输出交错 | 已知现象：sender/main 的 printf 未持锁（不影响协议与数据），见 `source_audit.md` 第 2.2 节 |
| 数字与报告对不上 | 用 `analyze_timing.py` 复算，一切以 `results/` 原始 CSV 为准 |
| 日志文件在哪里 | 程序 CSV 日志在 `logs/robot_log_YYYYMMDD_HHMMSS.csv`；测试日志在 `performance_evaluation/logs/` |

---

## 7. 最短验证路径（只想确认"能跑、结论可信"）

```bash
make clean && make && make test                          # ≈1 分钟
bash performance_evaluation/scripts/run_disconnect_test.sh   # ≈10 秒
cat performance_evaluation/README.md                     # 交付物索引
cat performance_evaluation/performance_report.md         # 实测数据与结论
```

预期结果速览（本机实测）

| 项目 | 结果 |
|---|---|
| 协议测试 | 16/16 PASS，10000 帧随机分块正确率 100% |
| 周期偏差 P99 | 相对睡眠 0.452~0.481 ms；绝对定时 **0.122 ms**；占满 12 核 3.295 ms |
| 唤醒延迟 P99 | 0.177 ms（绝对定时） |
| 下行控制帧 | 30 分钟 177108 帧，坏帧 0，seq 跳变 0 |
| 断线检测 | FIN / RST 均在 ~2.0 s 内自行退出，退出码 0 |
| SIGINT 退出时延 | 0.102 s（修复前：挂死需 `kill -9`） |
| 30 分钟资源 | RSS 恒定 2016 kB（变化量 0）、线程 3、fd 19 |
