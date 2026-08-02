# Robot Linux C App

纯 C（C11）机器人上位机通信程序，从 Qt/C++ 版本重构而来，适用于嵌入式 Linux / ARM Linux 平台。

## 与原 Qt 项目的对应关系

| 原 Qt 模块 | C 版本对应 | 说明 |
|---|---|---|
| `mainwindow.cpp` / Widget UI | 无 | 去除 GUI，改为命令行 |
| `robotcontrol.cpp` 控制帧发送 | `src/robot_protocol.c` | 保留 20 字节帧协议 |
| `tcpsocket.cpp` TCP 通信 | `src/robot_tcp.c` | POSIX socket 替代 Qt QTcpSocket |
| OpenGL 3D 显示 | 无 | 去除 |
| 控制下发 10ms 定时器 | `main.c` sender_thread + clock_nanosleep | pthread + POSIX 时钟 |
| 状态解析 | `src/robot_protocol.c` parse_car/uwb_frame | 保留全部解析逻辑 |
| 日志保存 | `src/logger.c` | CSV 格式 |

## 编译

```bash
make
```

编译参数：`gcc -std=c11 -Wall -Wextra -O2 -pthread`

交叉编译（ARM）：

```bash
make CC=arm-linux-gnueabihf-gcc
```

清理：

```bash
make clean
```

## 运行

```bash
# 默认参数（IP 192.168.1.103，端口 9090，周期 10ms）
./robot_linux_app

# 完整参数
./robot_linux_app -i 192.168.1.103 -p 9090 -e 1 -m 0 --vx 0 --vy 0 --vz 0 --period 10
```

### 命令行参数

| 参数 | 说明 | 默认值 |
|---|---|---|
| `-i <ip>` | 机器人 IP 地址 | 192.168.1.103 |
| `-p <port>` | 机器人 TCP 端口 | 9090 |
| `-e <0\|1>` | 使能 | 0 |
| `-m <0\|1\|2>` | 模式 (0=STOP, 1=MANUAL, 2=AUTO) | 0 |
| `--vx <val>` | X 方向速度 (int8) | 0 |
| `--vy <val>` | Y 方向速度 (int8) | 0 |
| `--vz <val>` | Z 方向速度 (int8) | 0 |
| `--period <ms>` | 控制帧发送周期 (ms) | 10 |
| `-h` | 显示帮助 | - |

## 协议说明

### 控制下发帧（上位机 → 机器人）

20 字节固定帧，周期性发送。

```
buf[0]  = 0x77
buf[1]  = 0x00
buf[2]  = enable (0/1)
buf[3]  = mode   (0=STOP, 1=MANUAL, 2=AUTO)
buf[4]  = 0x00
buf[5]  = vx (int8)
buf[6]  = vy (int8)
buf[7]  = vz (int8)
buf[8] ~ buf[17] = 0x00
buf[18] = seq (自增序列号)
buf[19] = 0x0A
```

### 状态回传帧（机器人 → 上位机）

20 字节固定帧。

**帧尾校验**：`buf[18] == 0x55 && buf[19] == 0x56` 为合法帧。

**UWB 定位帧**（`buf[0] == 'u'`）：

```
buf[1]     = 'p'
buf[2..5]  = tagNum (int32, 标签号)
buf[6..9]  = x (float)
buf[10..13] = y (float)
buf[14..17] = z (float)
```

**机器人状态帧**（`buf[0] == 'c'`）：

```
buf[1]     = vx_raw (int8),   vx = vx_raw / 100.0
buf[2]     = vy_raw (int8),   vy = vy_raw / 100.0
buf[3]     = vz_raw (int8),   vz = vz_raw / 100.0
buf[4..5]  = weld_state (int16)
buf[6..7]  = dAngle_raw (int16), dAngle = dAngle_raw / 100.0
buf[8..11] = dDis (float)
buf[12..13] = yaw_raw (int16),   yaw = yaw_raw / 32768 * 180
buf[14..15] = roll_raw (int16),  roll = roll_raw / 32768 * 180
buf[16..17] = pitch_raw (int16), pitch = pitch_raw / 32768 * 180
```

### TCP 粘包/半包处理

使用接收缓存 + 帧尾同步状态机。每次 `recv` 后的数据逐字节输入缓冲区，当缓冲达到 20 字节时检查 `buf[18]==0x55 && buf[19]==0x56`。若匹配则解析帧并清空缓冲；若不匹配则丢弃首字节、滑动窗口继续扫描，无需帧头 magic number。

## 日志格式

日志保存到 `logs/robot_log_YYYYMMDD_HHMMSS.csv`。

CSV 包含两行表头：UWB 数据列和 CAR 状态数据列。每行数据以时间戳开头，类型字段区分 UWB/CAR。

示例：

```csv
timestamp,tag_num,x,y,z
timestamp,vx,vy,vz,weld_state,d_angle,d_dis,yaw,roll,pitch
1703001234.123456,1,1.234,2.345,0.000
1703001234.234567,0.00,0.00,0.00,0,0.00,0.0000,0.00,0.00,0.00
```

## 后续可扩展方向

- **串口通信**：使用 termios 实现 UART 直连机器人，替代 TCP
- **V4L2 摄像头采集**：通过 V4L2 API 采集 USB/MIPI 摄像头
- **UDP 图像传输**：使用 JPEG 压缩 + UDP 传输视频流
- **systemd 自启动**：编写 `.service` 文件实现开机自启
- **交叉编译**：使用 `arm-linux-gnueabihf-gcc` 或 `aarch64-linux-gnu-gcc` 编译到 ARM 平台
- **自动重连**：检测断线后自动尝试重连，配合指数退避
- **运行时参数调整**：通过 Unix domain socket 或 stdin 接收运行时命令
- **JSON 配置**：使用 JSON（如 cJSON）替代命令行参数
- **MQTT 遥测**：通过 MQTT 上报机器人状态到云端
