# 板端实测结果汇总（2026-10-13）

> **说明**：本文件是**结论**，不是原始数据。
> 采集产生的原始 CSV / txt / log（累计约 7.8 千行、数百 KB）**不入库**，
> 需要复核时用下面的复现命令重新采集即可。所有数字均来自 `analyze_timing.py` 与
> `sensor_robot` 自身的统计输出，未做二次加工或填充。

---

## 1. 测试环境（开发板）

| 项 | 值 |
|---|---|
| 开发板 | 正点原子 i.MX6ULL，主机名 `imx6ull` |
| SoC / 架构 | ARMv7 Processor rev 5 (v7l)，CPU part `0xc07`，BogoMIPS 12.00 |
| 核数 | 1（`nproc` = 1） |
| 内核 | Linux **4.1.15** `#31 SMP PREEMPT`（Linaro GCC 4.9.4 编译） |
| 网络 | PC `192.168.1.66` ↔ 开发板 `192.168.1.55` |
| 调度 | 普通 CFS，未使用 `SCHED_FIFO` / CPU 隔离 / PREEMPT_RT |

## 2. 周期调度实测（`--benchmark`，10 ms 周期）

| 指标 | 值 |
|---|---|
| 样本数 | 4741 个周期 |
| 定时模式 | 相对睡眠（relative，`clock_nanosleep` flag=0） |
| 工作耗时（组帧+发送） | mean 88.2 μs，p95 97.7 μs，p99 143.0 μs，max 357.7 μs |
| 周期偏差 \|d\| | mean 194.6 μs，p95 207.3 μs，p99 263.0 μs，max 489.3 μs |
| 周期偏差（有符号） | min +164.0 μs，max +489.3 μs（**恒为正** → 相对睡眠只会导致周期变长） |
| 实际周期（start→start） | mean 10.1946 ms，min 10.1640 ms，max 10.4893 ms |
| 发送错误 | 0 |

**结论**：普通 CFS 下 10 ms 周期的偏差稳定在 0.2 ms 量级，p99 263 μs；
偏差恒为正印证了"相对睡眠不补偿工作耗时"的分析（这也是 `--abs-timer` 存在的原因）。
**不是硬实时**，不能声称微秒级确定性。

## 3. 真实传感器链路实测（ICM20608 / AP3216C）

采集 27.4 秒，板端 `sensor_robot -p 19090 --period 10 --imu-every 1 --ap-every 10`
→ PC `robot_linux_app -i 192.168.1.55`：

| 指标 | 值 |
|---|---|
| 帧数 | 板端发 IMU 2741 / AP 274，PC 收到 **2741 / 274** —— **零丢帧** |
| 下行控制帧 | 收 2699 帧，坏帧 0，seq 跳变 0 |
| 读取/发送错误 | IMU 读 0、AP 读 0、send 0 |
| 采样周期（IMU） | 平均 10.000 ms；p50 9.999 / p90 10.215 / p99 10.375 ms；最坏 46.838 ms（2740 个间隔中仅 1 次 >12 ms） |
| 实际采样率 | IMU 100.03 Hz，AP 10.04 Hz |
| 温度 | 均值 34.61 ℃，全程漂移 −0.02 ℃ |
| 加速度 | x/y/z 见 ±0.9 g；静置段 az ≈ +1 g（量纲换算正确） |
| 角速度 | 静止零偏约 −1.0 / +1.6 / −0.5 °/s；峰值 ±192 °/s（采集中板子被转动） |
| AP3216C | als 均值 260（14–455），ps 0–7，ir 多为 0（少数 256/257） |
| IIO 节点 | `iio:device1/` = icm20608，`iio:device0/` = ap3216c（编号随加载顺序变，必须按 name 匹配） |

**结论**：IIO sysfs + 20 字节帧这条真实数据链路在板上跑通，零丢帧、零坏帧；
周期抖动是用户态轮询（7 次 sysfs 读 + `clock_nanosleep`）的固有代价，p99 10.375 ms。

## 4. 模拟端对照（同一次板端采集）

| 项 | 值 |
|---|---|
| `mock_robot` 推送状态帧 | 4845 帧 |
| 收到的上位机控制帧 | 4741 帧 |
| 坏帧 / seq 跳变 | 0 / 0 |

## 5. 复现命令

```bash
# ① 周期调度（PC 监听，板子跑接收端）
bash performance_evaluation/scripts/board_bench_pc.sh 6000
# 板端：cd /lib/modules/4.1.15 && ./board_bench_board.sh 192.168.1.66 6000
# 复算：python3 performance_evaluation/scripts/analyze_timing.py \
#         <NFS_DIR>/lib/modules/4.1.15/log/robot_linux_c_app/board_timing.csv \
#         --label board_imx6ull --period-ns 10000000

# ② 真实传感器（板子监听，PC 跑接收端）
# 板端：./load_drivers.sh && ./sensor_robot -p 19090 --period 10 --imu-every 1 --ap-every 10
# PC  ：./output/pc/robot_linux_app -i 192.168.1.55 -p 19090 -e 0 -m 0 --period 10
```

产物默认落在 `log/robot_linux_c_app/`（板端为 `/lib/modules/4.1.15/log/robot_linux_c_app/`），
该目录已被 `.gitignore` 忽略，不会误提交。
