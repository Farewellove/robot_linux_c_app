# Robot Linux C App

纯 C（C11）+ POSIX 的机器人 TCP 通信上位机，从 Qt/C++ 版本重构而来，面向嵌入式 Linux / ARM Linux。
零第三方依赖，只链接 libc + pthread。

> **诚实边界（先看这里）**
> - 本项目是**纯用户态程序**：无内核模块、无设备树、无驱动。
> - **未实现**：断线自动重连、CRC/校验字段、应用层心跳、实时调度（`SCHED_FIFO`/PREEMPT_RT）。
> - `SO_KEEPALIVE` 使用内核默认参数（约 7200 s），**不等于及时断线检测**。
> - 当前所有性能数据来自 **PC loopback 模拟对端**，板端实测尚未进行（见 `performance_evaluation/`）。
> - **真实传感器通路已在开发板真机联调通过**（2026-10-13，见下方"真实传感器实测记录"）；
>   已知局限：采样由用户态 `clock_nanosleep` + 7 次 sysfs 读驱动，不是传感器数据就绪中断，
>   周期抖动 p99 = 10.38 ms、最坏 46.8 ms（单峰），不能当作硬实时采样看待。

---

## 项目结构

```
robot_linux_c_app/
├── README.md                  ← 本文件：唯一入口（结构 / 构建 / 运行 / 协议）
├── Makefile                   ← 全部构建与测试入口
│
├── src/                       ← 生产代码（9 个 .c）
│   ├── main.c                   入口、双线程、信号、周期调度与打点
│   ├── robot_protocol.c         20B 定长帧：组帧 / 解析 / 帧尾同步粘包状态机
│   ├── robot_tcp.c              socket 封装：connect / 循环 send / recv / NODELAY / KEEPALIVE
│   ├── logger.c                 CSV 日志（时间戳命名、逐帧落盘）
│   ├── app_config.c             命令行参数默认值与打印
│   ├── metrics.c                周期指标采集（--benchmark 才启用，默认零开销）
│   ├── iio_util.c               IIO sysfs 通用访问（按 name 定位设备 / 常驻 fd / pread 重读）
│   ├── icm20608.c               ICM20608 六轴+温度读取（只取 int16 原始值）
│   └── ap3216c.c                AP3216C ALS/PS/IR 读取
├── include/                   ← 对应头文件（8 个 .h）
│
├── tests/                     ← 测试与对端程序（不参与主程序链接）
│   ├── test_protocol.c          协议状态机测试（19 类用例，直接编译生产源码）
│   ├── mock_robot.c             loopback 模拟机器人服务端（虚拟数据，含下行帧校验）
│   └── sensor_robot.c           真实传感器服务端（读板载 ICM20608/AP3216C 推真实数据）
│
├── log/                       ← 运行期日志根目录（自动创建，git 忽略）
│   └── robot_linux_c_app/       ← 按项目分目录：robot_log_*.csv / 03_periodic_timing.csv /
│                                  mock_server.csv / sensor_server.csv
│
├── output/                    ← 构建产物（PC 与 ARM 两套，见下方"构建与部署"）
│   ├── pc/                       PC  侧：mock_robot / sensor_robot / robot_linux_app
│   └── arm/                      ARM 侧：robot_linux_app / mock_robot / sensor_robot
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
make test                # 协议测试：19 类用例，应输出 19 passed, 0 failed

# 终端 A：模拟设备端（对端，虚拟数据）
./output/pc/mock_robot -b 127.0.0.1 -p 19090 --period 10

# 终端 B：接收端，跑 1000 个周期（10 秒）后自动退出
./output/pc/robot_linux_app -i 127.0.0.1 -p 19090 -e 1 -m 1 --period 10 \
    --benchmark --cycles 1000 --metrics-csv /tmp/timing.csv
```

### 真实传感器数据（开发板）

板端用 `sensor_robot` 替代 `mock_robot`，推的是 ICM20608 / AP3216C 的真实采样值：

```bash
# ① 开发板：加载驱动（IIO 依赖内核已开启 CONFIG_IIO）
cd /lib/modules/4.1.15 && ./load_drivers.sh

# ② 开发板：真实数据源，10ms 一帧 IMU、每 10 个周期一帧 AP3216C
./sensor_robot -p 19090 --period 10 --imu-every 1 --ap-every 10 --print-every 100

# ③ PC：接收端照原样跑，命令一个字都不用改
./output/pc/robot_linux_app -i 192.168.1.55 -p 19090 -e 0 -m 0 --period 10
```

> 注意方向：这一节是**板子监听、PC 主动连板子**；而 `board_bench_*` 那套是**PC 监听、板子主动连 PC**
> （板端命令填的是 PC 的 `192.168.1.66`）。两者方向相反，别混用。

### 真实传感器实测记录（2026-10-13，i.MX6ULL + ICM20608 / AP3216C）

板端 `./sensor_robot -p 19090 --period 10 --imu-every 1 --ap-every 10`，PC 侧 `robot_linux_app -i 192.168.1.55`
接收，持续 27.4 秒。原始数据在 `performance_evaluation/results/`：

- `board_sensor_imu_20261013_223355.csv` —— PC 收到的逐帧日志（2741 条 IMU + 274 条 AP）
- `board_sensor_server_20261013_223355.csv` —— 板端 `sensor_robot` 的运行统计

| 项 | 结果 |
|---|---|
| 帧数 | 板端发 IMU 2741 / AP 274；PC 收到 2741 / 274 —— **零丢帧** |
| 下行控制帧 | 收 2699 帧，坏帧 0，seq 跳变 0 |
| 传感器读取错误 | IMU 0、AP 0；发送错误 0 |
| 采样周期（IMU） | 平均 10.000 ms，p50 9.999 / p90 10.215 / p99 10.375 ms，最坏 46.838 ms（仅 1 次） |
| 实际采样率 | IMU 100.03 Hz、AP 10.04 Hz |
| 温度 | 均值 34.61 ℃，全程漂移 -0.02 ℃（片内温度，高于室温属正常） |
| 姿态数据范围 | accel x/y/z 见 ±0.9 g，gyro 峰值 ±192 °/s（采集过程中板子被转动过） |

结论：IIO sysfs + 20B 帧这条真实数据链路在板上是通的，周期稳定性落在用户态轮询的正常量级。
若要更高确定性的采样周期，需要驱动侧用中断/定时器触发，而不是现在的用户态定时读。

完整的设计选型、帧格式、代码结构与实测数据判读见
**`docs/真实传感器接入与实测记录.md`**。

无硬件时也能自测这条链路 —— 造一棵假 IIO 目录树喂给 `--iio-root`：

```bash
mkdir -p /tmp/fake_iio/iio:device0 && echo icm20608 > /tmp/fake_iio/iio:device0/name
# 再建 in_accel{0,1,2}_raw / in_anglvel{0,1,2}_raw / in_temp0_raw 等文件（各写一个整数）
./output/pc/sensor_robot --iio-root /tmp/fake_iio -p 19090 --period 10 --frames 60
```

完整流程（构建 / 日常验证 / 性能量化 / 板端 / 排查）见 **`performance_evaluation/USAGE.md`**。

---

## 构建与部署

产物统一放在 `output/`，按平台分目录，两个程序的角色固定：

| 产物 | 角色 | 运行位置 |
|---|---|---|
| `output/pc/mock_robot` | **模拟设备端**（对端，推虚拟状态帧） | PC |
| `output/arm/sensor_robot` | **真实传感器设备端**（读 ICM20608/AP3216C 推真实帧） | **开发板** |
| `output/arm/robot_linux_app` | **接收端**（上位机，主用） | **开发板** |
| `output/pc/robot_linux_app` | 接收端 | PC（本机自测） |
| `output/arm/mock_robot` | 模拟设备端 | 开发板（反向拓扑时用） |
| `output/pc/sensor_robot` | 真实传感器端 | PC（仅编译，配合 `--iio-root` 离线自测） |

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
# ③ 开发板串口：跑板端脚本（记录环境 → 采集 60 秒 → CSV 落在日志目录）
cd /lib/modules/4.1.15
./board_bench_board.sh 192.168.1.66 6000
# 产物：/lib/modules/4.1.15/log/robot_linux_c_app/{board_timing.csv,board_env_<时间>.txt}
```

PC 侧脚本会自动完成：等连接 → 等采集结束 → 复算统计 → 归档 `results/board_periodic_timing_<时间>.csv`
与 `results/board_env_<时间>.txt` → 打印模拟端坏帧/seq 跳变校验。

**不用脚本时的手工等价命令**

```bash
# PC
./output/pc/mock_robot -p 19090 --period 10 --frames 1000000 --accept-timeout 300 \
    --csv performance_evaluation/results/board_mock_server.csv
# 板端（产物同样进 log/robot_linux_c_app/）
cd /lib/modules/4.1.15 && uname -a && nproc
mkdir -p log/robot_linux_c_app
./robot_linux_app -i 192.168.1.66 -p 19090 -e 1 -m 1 --period 10 \
    --benchmark --cycles 6000 \
    --metrics-csv log/robot_linux_c_app/board_timing.csv
# PC 复算（CSV 在 NFS 目录里，无需 scp）
python3 performance_evaluation/scripts/analyze_timing.py \
    /home/why/zdyz/nfs/rootfs/lib/modules/4.1.15/log/robot_linux_c_app/board_timing.csv \
    --label imx6ull
```

> `mock_robot` 默认绑定 `0.0.0.0`（跨机必需）；仅本机自测时用 `-b 127.0.0.1` 收紧监听范围。
> 已实测：交叉工具链 sysroot 的 glibc 与 NFS rootfs **同为 `libc-2.19-2014.08-1-git`**，
> ARM 产物只要求最高 `GLIBC_2.17`，可直接在板上运行。

---

## 运行

### 实测环境地址

| 角色 | IP | 说明 |
|---|---|---|
| PC（上位机 / 模拟设备端） | `192.168.1.66` | `board_bench_pc.sh` 会自动探测，脚本里不用手填 |
| 开发板（i.MX6ULL） | `192.168.1.55` | 作为真实数据源时用 `-i 192.168.1.55` 连它 |

```bash
# 连开发板（真实传感器数据源跑在板子上时）
./output/pc/robot_linux_app -i 192.168.1.55 -p 19090 -e 0 -m 0 --period 10

# 完整参数
./output/pc/robot_linux_app -i 192.168.1.55 -p 19090 -e 1 -m 0 --vx 0 --vy 0 --vz 0 --period 10
```

> 代码里的 `DEFAULT_ROBOT_IP` 仍是 `192.168.1.103`（原机器人地址，见 `include/app_config.h`），
> 连开发板时显式传 `-i 192.168.1.55`，或改那个宏把它变成默认值。

> 开发板上运行的是 `output/arm/robot_linux_app`（经 `make deploy` 放到板端 `/lib/modules/4.1.15/`，
> 与 `ap3216cAPP`、`gt911_recovery_test` 等外设测试程序同一目录）。

### 命令行参数

| 参数 | 说明 | 默认值 |
|---|---|---|
| `-i <ip>` | 对端 IP 地址（连开发板填 `192.168.1.55`） | 192.168.1.103 |
| `-p <port>` | 对端 TCP 端口（实测用 19090） | 9090 |
| `-e <0\|1>` | 使能位（默认不使能，避免连上就动） | 0 |
| `-m <0\|1\|2>` | 模式 (0=STOP, 1=MANUAL, 2=AUTO) | 0 |
| `--vx / --vy / --vz <val>` | 速度分量（int8） | 0 |
| `--period <ms>` | 控制帧发送周期 | 10 |
| `--benchmark` | 采集每周期调度指标（唤醒延迟/工作耗时/周期偏差） | 关闭 |
| `--metrics-csv <path>` | 指标 CSV 输出路径 | `log/robot_linux_c_app/03_periodic_timing.csv` |
| `--cycles <n>` | 跑满 n 个周期后退出（0 = 不限制） | 0 |
| `--abs-timer` | 改用绝对时间定时（`TIMER_ABSTIME`） | 关闭 |
| `--log-dir <path>` | CSV 日志输出目录 | `log/robot_linux_c_app` |
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
`buf[0]` 用于分派帧类型：`'u'` = UWB 定位帧，`'c'` = 车体状态帧，
`'i'` = IMU 帧（ICM20608），`'a'` = AP3216C 帧。

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

**IMU 帧**（`buf[0] == 'i'`，ICM20608 真实采样，板端 `sensor_robot` 推送）：

```
buf[1]      0x00        保留
buf[2..3]   accel_x (int16)   accel = raw / 2048.0   → g    （±16g 量程）
buf[4..5]   accel_y (int16)
buf[6..7]   accel_z (int16)
buf[8..9]   gyro_x  (int16)   gyro  = raw / 16.4     → °/s  （±2000°/s 量程）
buf[10..11] gyro_y  (int16)
buf[12..13] gyro_z  (int16)
buf[14..15] temp    (int16)   temp  = raw / 326.8 + 25 → ℃
buf[16..17] seq (uint16)      帧序号，溢出回绕
```

线上只传 int16 原始值，物理量在解析侧换算（`imu_raw_to_physical()`），
换算系数即协议的一部分，收发两侧共用同一份宏定义，避免两处漂移。

**AP3216C 帧**（`buf[0] == 'a'`，环境光 / 接近 / 红外）：

```
buf[1]      0x00        保留
buf[2..3]   als (int16)  环境光原始计数
buf[4..5]   ps  (int16)  接近检测原始计数
buf[6..7]   ir  (int16)  红外原始计数
buf[8..15]  0x00        保留
buf[16..17] seq (uint16) 帧序号
```

板端该驱动的 `scale` 当前恒为 1，故原始计数即显示值；协议不写死换算系数。

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

## 日志目录规范

运行期产生的所有 CSV / txt **一律落在 `log/<项目名>/` 下**，不许散落在工作目录里
（板端 `/lib/modules/4.1.15/` 是多个项目共用的工作目录，散落会互相污染）：

```
log/                              ← 日志根（相对当前工作目录）
└── robot_linux_c_app/            ← 本项目一层；其它项目各自一个目录
    ├── robot_log_20261013_220849.csv    上位机逐帧日志（UWB/CAR/IMU/AP）
    ├── 03_periodic_timing.csv           --benchmark 的逐周期调度指标
    ├── mock_server.csv                  mock_robot 的校验统计
    └── sensor_server.csv                sensor_robot 的采样/发送统计

# 板端（cwd = /lib/modules/4.1.15）即为：
/lib/modules/4.1.15/log/robot_linux_c_app/
```

覆盖方式（后者优先）：默认值 < 环境变量 `ROBOT_LOG_ROOT` < `--log-dir`。
板端脚本同样遵守：环境变量 `ROBOT_LOG_ROOT` / `BOARD_LOG_DIR` 可改。

> 区分两类产物：
> **运行期日志** → `log/<项目名>/`（随手可删，`make clean-logs` 清掉）；
> **量化交付物**（脚本归档的 CSV / 报告）→ PC 侧留在 `performance_evaluation/{results,logs}/`，
> 属于仓库内的证据，不随运行清理。

程序日志保存到 `log/robot_linux_c_app/robot_log_YYYYMMDD_HHMMSS.csv`，含四行表头：

```csv
timestamp,tag_num,x,y,z
timestamp,vx,vy,vz,weld_state,d_angle,d_dis,yaw,roll,pitch
timestamp,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,temp
timestamp,als,ps,ir
1703001234.123456,1,1.234,2.345,0.000
1703001234.234567,0.00,0.00,0.00,0,0.00,0.0000,0.00,0.00,0.00
1703001234.245678,0.0000,0.5000,1.0000,10.000,-10.000,0.000,26.00
1703001234.255678,1234,56,789
```

- 时间戳来自 `clock_gettime(CLOCK_REALTIME)`，微秒级有效精度；
- 每帧写入后 `fflush`，被强杀时已写数据不丢（代价是刷盘频繁）；
- 按列数区分 UWB（5 列）、CAR（10 列）、IMU（8 列）、AP（4 列）记录。

---

## 测试与测量

```bash
make test                                  # 协议状态机 19 类用例（含 IMU/AP 新帧）
make clean-logs                            # 只清 log/<项目名>/ 下的运行期日志
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
