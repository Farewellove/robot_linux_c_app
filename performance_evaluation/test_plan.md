# test_plan.md —— 测试步骤、环境、命令与验收标准

> 所有测试均可在**本机（PC Linux x86_64）离线复现**，不依赖真实机器人。
> 唯一需要真实硬件才能完成的项目在文末明确标注为 **NOT_RUN**。

---

## 0. 测试环境（实测记录）

| 项目 | 值 |
|---|---|
| 主机 | `why-Nitro-AN515-55`，x86_64 |
| 内核 | `6.8.0-138-generic #138~22.04.1-Ubuntu SMP PREEMPT_DYNAMIC` |
| CPU | 12 逻辑核（`nproc`=12） |
| 编译器 | gcc 11.4.0；交叉工具链 arm-linux-gnueabihf-gcc 4.9.4 (Linaro) |
| 编译选项 | `-std=c11 -Wall -Wextra -O2 -pthread -D_POSIX_C_SOURCE=200112L` |
| 调度策略 | 默认 CFS，**未使用 root / SCHED_FIFO / CPU 隔离 / PREEMPT_RT** |
| 对端 | 本机 `127.0.0.1` loopback TCP，由 `tests/mock_robot.c` 模拟 |
| 测试日期 | 2026-10-11 |

**必须写进任何结论的前提**：普通 Linux（非实时内核）+ 非实时调度下的 10ms 周期任务
**不等于硬实时**；测得的"唤醒延迟"是**线程唤醒延迟**，**不是端到端执行器控制延迟**。

---

## 1. 构建与静态验证

```bash
make clean && make            # 默认构建 PC + ARM 两套到 output/，应 0 warning 0 error
make pc                       # 只构建 PC 侧
make arm                      # 只构建 ARM 侧
make deploy                   # 部署 ARM 程序到 NFS 根文件系统
make test                     # 构建并运行协议解析测试
make test-asan                # ASan + UBSan + 泄漏检测
make cross-check              # ARM 交叉编译可用性检查（完整编译 + 链接）
```

**验收标准**
- `make` 成功且 `-Wall -Wextra` 无任何 warning。
- `make test` 退出码 0，全部用例 PASS。
- `make test-asan` 运行结束无任何 ASan/UBSan 报告，无泄漏报告。
- `make cross-check` 输出 `cross-check OK`。

---

## 2. P0-A：10ms 控制任务的周期调度测量

### 2.1 指标定义（写进报告的口径）

```
scheduled_ns      第 i 个周期的目标开始时刻（仅 --abs-timer 模式有效；相对睡眠模式记 NA）
start_ns          第 i 个周期实际开始执行（组帧之前）的 CLOCK_MONOTONIC 时刻
end_ns            第 i 个周期 send 返回之后的 CLOCK_MONOTONIC 时刻
唤醒延迟          start_ns - scheduled_ns
工作耗时          end_ns - start_ns
相邻周期偏差      (start_ns[i] - start_ns[i-1]) - period_ns
周期超期          end_ns > scheduled_ns + period_ns   （仅绝对定时模式可判定）
```

- 时间戳统一 `clock_gettime(CLOCK_MONOTONIC)`，64 位纳秒。
- 相对睡眠模式下**不生成 `scheduled_ns`**，字段写 `NA`，只用"相邻周期偏差"口径。
- 百分位采用最近秩法（nearest-rank），可用 `scripts/analyze_timing.py` 独立复算。

### 2.2 命令

```bash
# 单场景
bash performance_evaluation/scripts/run_periodic_bench.sh <scenario> 10000 <timer> performance_evaluation/results
#   scenario   = idle | traffic | load
#   timer      = relative | absolute

# 全部场景（4 个，各 10000 周期 ≈ 100 秒）
bash performance_evaluation/scripts/run_all_bench.sh 10000
```

### 2.3 场景定义

| 场景 | 对端行为 | CPU 负载 | 目的 |
|---|---|---|---|
| `idle` | 只接受控制帧，**不发送任何上行数据** | 无 | 空载基线；同时验证"静默对端"下退出是否正常 |
| `traffic` | 每 10ms 推 1 帧（UWB/CAR 交替） | 无 | **真实业务模式**：收发同时进行 |
| `load` | 同上 | 占满全部 12 核的忙循环 | 观察 CPU 竞争下的抖动劣化 |
| `traffic`+`absolute` | 同上 | 无 | 对比绝对时间定时的累积漂移 |

### 2.4 采样与验收标准

- 每场景 **10000 周期**（10ms × 10000 ≈ 100 秒），样本全量落盘为 CSV。
- 运行期**不允许**在周期循环内打印到终端（代码已保证：发送线程循环内无 printf）。
- 内存预分配，运行期无 `malloc`。
- 每个场景必须产出：原始 CSV、app 日志、mock 日志、资源 CSV。
- 验收：`samples` 列应等于 `--cycles` 设定值（不出现 capacity dropped）。

---

## 3. P0-B：协议解析状态机自动化测试

### 3.1 命令

```bash
make test                                        # 正常运行
make test-asan                                   # 插桩运行
python3 -c "print(open('performance_evaluation/results/03_parser_tests.csv').read())"
```

- 测试**直接编译 `src/robot_protocol.c`**（`Makefile` 中 `PROTO_SRC`），
  **没有重写第二份解析器**，测的就是上线的状态机。
- 随机分块使用**固定种子 20261011**，可完全复现。

### 3.2 覆盖矩阵与验收标准

| 用例 | 场景 | 验收标准 |
|---|---|---|
| T01 | 单帧完整到达（UWB） | 输出 1 帧，字段值逐一正确 |
| T02 | 单帧完整到达（CAR）+ 定点缩放 | 1 帧；`vx/vy/vz=/100`、`dAngle=/100`、`yaw=/32768*180` 全部正确 |
| T03 | 粘包：两帧同次输入 | 顺序输出 2 帧（UWB→CAR） |
| T04 | 半包：单帧切成 7+13 两段 | 第一段 **0** 帧，补齐后 **1** 帧 |
| T05 | **10000 帧**固定种子随机分块（1..17 字节） | 解析出 **10000** 帧，每帧类型与内容逐一比对 |
| T06 | 垃圾前缀（13×0x00 / 9×0xFF） | 能重新对齐，输出 1 帧 |
| T07 | 错误帧尾 + 后续合法帧 | 坏帧被丢弃，后续帧正确解析 |
| T08 | 帧尾合法但帧头未知 | 按实现**静默丢弃 20 字节**（记录真实行为，不臆造规则） |
| T09 | 4096 字节垃圾洪泛后接合法帧 | 不越界、恰好输出 1 帧 |
| T10 | 连接中途关闭（只到 12 字节） | **不输出伪造完整帧**，无崩溃 |
| T11 | 帧尾字节序列出现在 payload（位置 16/17） | 帧仍被正确解析（协议只在 [18][19] 判帧尾） |
| T12 | 长度不足 / 帧头不符 | 4 个解析调用全部返回 -1 |
| T13 | `build_control_frame` 布局与缓冲区下界 | `0x77/0x0A`/保留字节/seq 正确；缓冲区不足返回 -1 |
| T14 | `mode_name` 映射 | 0/1/2/其他 → STOP/MANUAL/AUTO/UNKNOWN |
| T15 | 空输入 `len=0` | 0 帧，不崩溃 |

**总体验收**：16 个用例全部 PASS，`make test` 退出码 0；
ASan/UBSan 运行无任何报告。

---

## 4. P0-C：socket 端到端模拟测试（与纯解析器单测严格区分）

### 4.1 命令

```bash
bash performance_evaluation/scripts/run_periodic_bench.sh traffic 10000 relative performance_evaluation/results
# 结束后看 03_mock_server_traffic_relative.csv 与 mock 日志
```

### 4.2 模拟服务端（`tests/mock_robot.c`）做了什么

1. 监听 `127.0.0.1:<port>`，接受上位机连接；
2. 按指定周期推送 20 字节状态帧（偶数 UWB、奇数 CAR，帧尾 `0x55 0x56`）；
3. **逐帧校验上行控制帧**：`[0]==0x77`、`[19]==0x0A`，并检查 `seq` 是否连续
   （跨 recv 的半包用累积缓冲重同步，避免把半包误判为坏帧）；
4. 输出 `tx_status_frames / rx_ctrl_frames / rx_bad_ctrl_frames / rx_seq_gaps`。

**验收标准**：`rx_bad_ctrl_frames == 0`，`rx_seq_gaps == 0`，
`rx_ctrl_frames` 与上位机发送周期数一致（±1 次收尾差异）。

> **能力边界声明**：这是**离线 TCP 模拟**，验证的是"字节流时序 + 帧格式 + 解析正确性"，
> **不是真实机器人闭环控制测试**。

---

## 5. P1：断线、异常与长期运行

### 5.1 断线检测（命令）

```bash
bash performance_evaluation/scripts/run_disconnect_test.sh
```

| 子场景 | 对端行为 | 验收标准 |
|---|---|---|
| `fin` | 推 200 帧后优雅关闭（FIN） | 上位机在 ~2.0s 内自行退出，日志含 `connection closed by peer`，退出码 0 |
| `rst` | 推 200 帧后 RST 强断 | 上位机在 ~2.0s 内自行退出，日志含 `Connection reset by peer`，退出码 0 |
| `silent+sigint` | 只接不发，2 秒后发 SIGINT | **5 秒内**退出（回归 `shutdown` 修复） |

### 5.2 退出时序回归（A/B）

```bash
bash performance_evaluation/scripts/run_sigint_hang_test.sh /tmp/orig_app/robot_linux_app baseline_before_fix 6
bash performance_evaluation/scripts/run_sigint_hang_test.sh output/pc/robot_linux_app after_shutdown_fix 6
```

基线二进制由 `git worktree add --detach /tmp/orig_app HEAD` 从修复前 commit 构建。

### 5.3 长期运行资源观测

```bash
bash performance_evaluation/scripts/run_soak.sh 300 1      # 5 分钟
bash performance_evaluation/scripts/run_soak.sh 1800 5     # 30 分钟（报告口径要求）
```

采样项：`VmRSS`、`Threads`、`/proc/<pid>/fd` 计数、自愿/非自愿上下文切换。

**验收标准**：fd 数与线程数保持恒定；RSS 无单调增长趋势。
**明确的表述边界**：`VmRSS` + fd 曲线是**观察指标**，
有限时长内"未观察到增长"**不能断言"无内存泄漏"**；
要下结论必须配合 Valgrind/ASan 等工具（本项目的 ASan 已覆盖协议解析模块）。

---

## 6. 明确标注 NOT_RUN 的项目

| 项目 | 状态 | 原因 | 执行步骤 |
|---|---|---|---|
| ARM 交叉编译 + 部署到 NFS | ✅ **已完成** | 工具链与 rootfs glibc 一致，产物可直接运行 | `make arm && make deploy`（详见 `USAGE.md` 第 5 节） |
| 跨机模式连通性（PC 监听 0.0.0.0 ↔ 经 192.168.1.66 连接） | ✅ **已完成** | — | 见 `README.md` 板级实测流程 |
| **板端周期调度实测数据采集** | ✅ **已完成**（2026-10-13） | i.MX6ULL 4.1.15 单核；`board_periodic_timing_20261013_211746.csv` + `board_env_20261013_211746.txt` | 复算：`python3 performance_evaluation/scripts/analyze_timing.py <csv> --label board_imx6ull --period-ns 10000000` |
| **真实传感器数据链路实测** | ✅ **已完成**（2026-10-13） | 板端 `sensor_robot` 读 ICM20608/AP3216C → TCP → PC `robot_linux_app`；2741 条 IMU + 274 条 AP 零丢帧 | `docs/真实传感器接入与实测记录.md`；数据 `results/board_sensor_*_20261013_223355.csv` |
| 真实机器人/执行器闭环测试 | **NOT_RUN** | 无真实机器人，默认 IP `192.168.1.103` 无法确认可达 | `output/pc/robot_linux_app -i <真实IP> -p <真实端口> -e 1 -m 1 --benchmark --cycles 6000 --metrics-csv results/real_robot_timing.csv`，同时用示波器/机器人侧日志测端到端执行延迟 |
| 真实网线拔插 / 断电断线检测 | **NOT_RUN** | 尚未执行 | 板端运行中直接拔网线，观察是否在 `TCP_KEEPIDLE` 默认 7200s 内报错（预期**不会**及时报错，这是已知能力边界） |
| Valgrind 全量 memcheck | **NOT_RUN** | 未安装 Valgrind；已用 ASan 覆盖纯解析器路径 | `sudo apt install valgrind && valgrind --leak-check=full output/pc/robot_linux_app -i 127.0.0.1 -p 19090 --cycles 500` |

---

## 7. 可复现性清单

| 产物 | 生成命令 | 文件 |
|---|---|---|
| 协议测试结果 | `make test` | `results/03_parser_tests.csv` |
| 周期调度原始样本 | `run_all_bench.sh 10000` | `results/03_periodic_timing_*.csv` |
| 资源观测 | 同上 / `run_soak.sh` | `results/03_resource_usage_*.csv` |
| 端到端下行校验 | 同上 | `results/03_mock_server_*.csv` |
| 断线测试 | `run_disconnect_test.sh` | `results/03_disconnect_tests.csv` |
| 退出时序 A/B | `run_sigint_hang_test.sh` | `results/03_sigint_hang_*.txt` |
| 原始日志 | 全部脚本自动写入 | `performance_evaluation/logs/` |
