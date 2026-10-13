# Robot Linux C App

纯 C（C11）+ POSIX 的机器人 TCP 通信上位机，从 Qt/C++ 版本重构而来，面向嵌入式 Linux / ARM Linux。
零第三方依赖，只链接 libc + pthread。

> **诚实边界（先看这里）**
> - 本项目是**纯用户态程序**：无内核模块、无设备树、无驱动。
> - **未实现**：断线自动重连、CRC/校验字段、应用层心跳、实时调度（`SCHED_FIFO`/PREEMPT_RT）。
> - `SO_KEEPALIVE` 使用内核默认参数（约 7200 s），**不等于及时断线检测**。
> - 当前所有性能数据来自 **PC loopback 模拟对端**，板端实测尚未进行（见 `performance_evaluation/`）。

---

## 项目结构

```
robot_linux_c_app/
├── README.md                  ← 本文件：唯一入口（结构 / 构建 / 运行 / 协议）
├── Makefile                   ← 全部构建与测试入口
│
├── src/                       ← 生产代码（6 个 .c）
│   ├── main.c                   入口、双线程、信号、周期调度与打点
│   ├── robot_protocol.c         20B 定长帧：组帧 / 解析 / 帧尾同步粘包状态机
│   ├── robot_tcp.c              socket 封装：connect / 循环 send / recv / NODELAY / KEEPALIVE
│   ├── logger.c                 CSV 日志（时间戳命名、逐帧落盘）
│   ├── app_config.c             命令行参数默认值与打印
│   └── metrics.c                周期指标采集（--benchmark 才启用，默认零开销）
├── include/                   ← 对应头文件（5 个 .h）
│
├── tests/                     ← 测试（不参与主程序链接）
│   ├── test_protocol.c          协议状态机测试（16 类用例，直接编译生产源码）
│   └── mock_robot.c             loopback 模拟机器人服务端（含下行帧校验）
│
├── logs/                      ← 程序运行时的 CSV 日志输出目录（自动创建）
│
├── output/                    ← 构建产物（PC 与 ARM 两套，见下方"构建与部署"）
│   ├── pc/                       PC  侧：mock_robot / robot_linux_app
│   └── arm/                      ARM 侧：robot_linux_app / mock_robot
│
├── performance_evaluation/    ← 性能量化交付物（文档 + 脚本 + 数据 + 日志）
│   ├── README.md                索引
│   ├── USAGE.md                 使用说明与标准流程 ★
│   ├── source_audit.md          源码审计（线程模型 / 协议约束 / 缺陷 / 能力边界）
│   ├── modification_report.md   改了什么、为什么、怎么回退
│   ├── test_plan.md             测试环境、命令、指标定义、验收标准
│   ├── performance_report.md    实测数据与结论 ★
│   ├── resume_improvements.md   简历描述（每条挂钩证据）
│   ├── scripts/                 可复现脚本
│   ├── results/                 原始 CSV（数据的唯一来源）
│   └── logs/                    原始运行日志
│
└── docs/                      ← 历史与个人参考文档（不参与构建）
    ├── README.md                归档说明 + 旧报告哪些结论已过时
    ├── 项目源码面试审计报告.md   （旧版审计，部分结论已过时）
    └── 面试准备文档.md
```

---

## 快速开始

```bash
make                     # 同时编译 PC 与 ARM 两套
make test                # 协议测试：16 类用例，应输出 16 passed, 0 failed

# 终端 A：模拟设备端（对端）
./output/pc/mock_robot -b 127.0.0.1 -p 19090 --period 10

# 终端 B：接收端，跑 1000 个周期（10 秒）后自动退出
./output/pc/robot_linux_app -i 127.0.0.1 -p 19090 -e 1 -m 1 --period 10 \
    --benchmark --cycles 1000 --metrics-csv /tmp/timing.csv
```

完整流程（构建 / 日常验证 / 性能量化 / 板端 / 排查）见 **`performance_evaluation/USAGE.md`**。

---

## 构建与部署

产物统一放在 `output/`，按平台分目录，两个程序的角色固定：

| 产物 | 角色 | 运行位置 |
|---|---|---|
| `output/pc/mock_robot` | **模拟设备端**（对端，主动推送状态帧） | PC |
| `output/arm/robot_linux_app` | **接收端**（上位机，主用） | **开发板** |
| `output/pc/robot_linux_app` | 接收端 | PC（本机自测） |
| `output/arm/mock_robot` | 模拟设备端 | 开发板（反向拓扑时用） |

```bash
make                      # 默认：编译 PC + ARM 两套
make pc                   # 只编译 PC 侧（gcc）
make arm                  # 只编译 ARM 侧（交叉编译）
make deploy               # 把 ARM 程序复制到板端工作目录（默认 rootfs/lib/modules/4.1.15）
make deploy NFS_DIR=/path/to/rootfs    # 自定义 NFS 根文件系统路径
make test                 # 协议解析状态机测试（PC 侧，16 类用例）
make test-asan            # AddressSanitizer + UBSan + 泄漏检测
make cross-check          # ARM 交叉编译可用性检查（完整编译 + 链接）
make help                 # 列出所有目标与产物角色
make clean                # 清理 obj/ 与 output/
```

编译参数：`-std=c11 -Wall -Wextra -O2 -pthread -D_POSIX_C_SOURCE=200112L`
（PC 与 ARM 两侧都应保持 **0 warning 0 error**）

### 板级实测流程（PC ↔ 开发板）

**用两个脚本，一个 PC 端、一个板端：**

```bash
# ① PC：交叉编译 + 部署（连板端脚本一起复制到 /lib/modules/4.1.15）
make deploy
```

```bash
# ② PC：开一个终端跑编排脚本 —— 它会打印板端该敲的命令，然后等板子连进来
bash performance_evaluation/scripts/board_bench_pc.sh 6000
```

```bash
# ③ 开发板串口：跑板端脚本（记录环境 → 采集 60 秒 → CSV 落在 NFS 目录）
cd /lib/modules/4.1.15
./board_bench_board.sh 192.168.1.66 6000
```

PC 侧脚本会自动完成：等连接 → 等采集结束 → 复算统计 → 归档 `results/board_periodic_timing_<时间>.csv`
与 `results/board_env_<时间>.txt` → 打印模拟端坏帧/seq 跳变校验。

**不用脚本时的手工等价命令**

```bash
# PC
./output/pc/mock_robot -p 19090 --period 10 --frames 1000000 --accept-timeout 300 \
    --csv performance_evaluation/results/board_mock_server.csv
# 板端
cd /lib/modules/4.1.15 && uname -a && nproc
./robot_linux_app -i 192.168.1.66 -p 19090 -e 1 -m 1 --period 10 \
    --benchmark --cycles 6000 --metrics-csv board_timing.csv
# PC 复算（CSV 在 NFS 目录里，无需 scp）
python3 performance_evaluation/scripts/analyze_timing.py \
    /home/why/zdyz/nfs/rootfs/lib/modules/4.1.15/board_timing.csv --label imx6ull
```

> `mock_robot` 默认绑定 `0.0.0.0`（跨机必需）；仅本机自测时用 `-b 127.0.0.1` 收紧监听范围。
> 已实测：交叉工具链 sysroot 的 glibc 与 NFS rootfs **同为 `libc-2.19-2014.08-1-git`**，
> ARM 产物只要求最高 `GLIBC_2.17`，可直接在板上运行。

---

## 运行

```bash
# 默认参数（IP 192.168.1.103，端口 9090，周期 10ms）
./output/pc/robot_linux_app

# 完整参数
./output/pc/robot_linux_app -i 192.168.1.103 -p 9090 -e 1 -m 0 --vx 0 --vy 0 --vz 0 --period 10
```

> 开发板上运行的是 `output/arm/robot_linux_app`（经 `make deploy` 放到板端 `/lib/modules/4.1.15/`，
> 与 `ap3216cAPP`、`gt911_recovery_test` 等外设测试程序同一目录）。

### 命令行参数

| 参数 | 说明 | 默认值 |
|---|---|---|
| `-i <ip>` | 机器人 IP 地址 | 192.168.1.103 |
| `-p <port>` | 机器人 TCP 端口 | 9090 |
| `-e <0\|1>` | 使能位（默认不使能，避免连上就动） | 0 |
| `-m <0\|1\|2>` | 模式 (0=STOP, 1=MANUAL, 2=AUTO) | 0 |
| `--vx / --vy / --vz <val>` | 速度分量（int8） | 0 |
| `--period <ms>` | 控制帧发送周期 | 10 |
| `--benchmark` | 采集每周期调度指标（唤醒延迟/工作耗时/周期偏差） | 关闭 |
| `--metrics-csv <path>` | 指标 CSV 输出路径 | `performance_evaluation/results/03_periodic_timing.csv` |
| `--cycles <n>` | 跑满 n 个周期后退出（0 = 不限制） | 0 |
| `--abs-timer` | 改用绝对时间定时（`TIMER_ABSTIME`） | 关闭 |
| `-h` | 显示帮助 | - |

> `--benchmark` 关闭时（默认）**不产生任何额外系统调用**，不影响原有通信行为。

---

## 协议说明

上下行均为 **20 字节定长帧**，二进制字段小端存放，未做字节序转换（假定设备端同为小端）。

### 控制下发帧（上位机 → 机器人）

```
buf[0]  = 0x77        帧头
buf[1]  = 0x00
buf[2]  = enable      0/1
buf[3]  = mode        0=STOP, 1=MANUAL, 2=AUTO
buf[4]  = 0x00
buf[5]  = vx (int8)
buf[6]  = vy (int8)
buf[7]  = vz (int8)
buf[8..17]  = 0x00
buf[18] = seq         自增序列号（uint8，溢出回绕）
buf[19] = 0x0A        帧尾
```

### 状态回传帧（机器人 → 上位机）

**帧尾标志**：`buf[18] == 0x55 && buf[19] == 0x56` 判定为合法帧。
`buf[0]` 用于分派帧类型：`'u'` = UWB 定位帧，`'c'` = 车体状态帧。

**UWB 定位帧**（`buf[0] == 'u'`，`buf[1] == 'p'`）：

```
buf[2..5]   tagNum (int32)
buf[6..9]   x (float)
buf[10..13] y (float)
buf[14..17] z (float)
```

**车体状态帧**（`buf[0] == 'c'`）：

```
buf[1]      vx_raw (int8)     vx = raw / 100.0
buf[2]      vy_raw (int8)     vy = raw / 100.0
buf[3]      vz_raw (int8)     vz = raw / 100.0
buf[4..5]   weld_state (int16)  原始值，不缩放
buf[6..7]   dAngle_raw (int16)  dAngle = raw / 100.0
buf[8..11]  dDis (float)        原始值
buf[12..13] yaw_raw (int16)     yaw   = raw / 32768 * 180
buf[14..15] roll_raw (int16)    roll  = raw / 32768 * 180
buf[16..17] pitch_raw (int16)   pitch = raw / 32768 * 180
```

### TCP 粘包 / 半包处理

接收缓存 + **帧尾同步**滑动窗口状态机（`recv_buffer_feed`）：

1. 逐字节填入 20 字节缓冲；
2. 缓冲满 20 字节时检查 `buf[18]==0x55 && buf[19]==0x56`；
3. **匹配** → 按 `buf[0]` 分派解析、回调上报，缓冲清零；
4. **不匹配** → `memmove` 左移 1 字节丢弃最早字节，继续等待新字节补齐。

因为 UWB 帧头是 `'u'`、车体帧头是 `'c'`，帧头不固定，所以用**统一的帧尾**做边界判定，
无需帧头 magic number；最坏情况丢 19 字节即可重新对齐。

**已知约束（如实记录，未擅自改协议）**
- 协议**没有长度字段、没有转义、没有 CRC/校验字段**，因此**无法检测载荷内容损坏**。
  `0x55 0x56` 只是同步标志，不是校验。
- 载荷其他位置出现 `0x55 0x56` 不会误判（只有 `[18][19]` 参与判定，已用测试 T11 验证）。
- 当 20 字节窗口帧尾恰为 `0x55 0x56` 但帧头既非 `'u'` 也非 `'c'` 时，
  该 20 字节被**静默丢弃**（用例 T08 固化了此行为）。

---

## 日志格式

程序日志保存到 `logs/robot_log_YYYYMMDD_HHMMSS.csv`，含两行表头：

```csv
timestamp,tag_num,x,y,z
timestamp,vx,vy,vz,weld_state,d_angle,d_dis,yaw,roll,pitch
1703001234.123456,1,1.234,2.345,0.000
1703001234.234567,0.00,0.00,0.00,0,0.00,0.0000,0.00,0.00,0.00
```

- 时间戳来自 `clock_gettime(CLOCK_REALTIME)`，微秒级有效精度；
- 每帧写入后 `fflush`，被强杀时已写数据不丢（代价是刷盘频繁）；
- 按列数区分 UWB（5 列）与 CAR（10 列）记录。

---

## 测试与测量

```bash
make test                                  # 协议状态机 16 类用例
make test-asan                             # ASan + UBSan + 泄漏检测
make cross-check                           # ARM 交叉编译可用性检查

# 板端实测（两个脚本，PC 端 + 板端）
bash performance_evaluation/scripts/board_bench_pc.sh 6000    # PC 侧编排（另在板端跑 board_bench_board.sh）

# PC 本机量化
bash performance_evaluation/scripts/run_all_bench.sh 10000    # 4 场景 × 10000 周期 ≈ 7 分钟
bash performance_evaluation/scripts/run_disconnect_test.sh    # 断线 / 退出时序
bash performance_evaluation/scripts/run_soak.sh 1800 5        # 30 分钟资源观测
```

指标口径、验收标准、实测数据与能力边界：
**`performance_evaluation/test_plan.md`** 与 **`performance_evaluation/performance_report.md`**。

---

## 后续可扩展方向

- **自动重连**：断线后按指数退避重连（当前断线即退出）
- **应用层心跳 / 校验**：补 CRC 或序列号确认机制，以检测数据损坏与丢帧
- **串口通信**：用 termios 实现 UART 直连机器人，替代 TCP（`recv_buffer_feed` 可复用）
- **实时性增强**：`SCHED_FIFO` + 优先级 + CPU 隔离 + PREEMPT_RT 内核（需评估对原语义的影响）
- **日志轮转**：避免长期运行写满磁盘（当前无轮转）
- **systemd 自启动**：编写 `.service`，`Restart=on-failure`
- **运行时参数调整**：通过 Unix domain socket 或 stdin 接收运行时命令
- **JSON 配置**：替代命令行参数
- **MQTT 遥测**：上报机器人状态到云端
