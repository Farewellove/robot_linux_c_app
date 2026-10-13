# modification_report.md —— 具体修改内容及原因

> 原则：**保留现有协议与业务功能**，改动只做"加测量"与"修一个已复现的缺陷"。
> 所有新增能力默认关闭；`src/robot_protocol.c`、`src/robot_tcp.c`、`src/logger.c`
> **一行未改**；协议格式、帧长、发送频率、接收缓冲大小、信号处理结构**全部保持原样**。

---

## 1. 改动清单

| # | 文件 | 类型 | 内容 | 默认行为 |
|---|---|---|---|---|
| M1 | `include/metrics.h` | 新增 | 周期采样结构体与采集接口声明 | 不参与 |
| M2 | `src/metrics.c` | 新增 | 预分配采样数组、CSV 导出、mean/P95/P99/max 统计 | 不参与 |
| M3 | `include/app_config.h` | 修改 | 新增 4 个测量开关字段 + 默认 CSV 路径宏 | 默认全关 |
| M4 | `src/app_config.c` | 修改 | 默认值填充、Banner 打印测量开关 | 多打印 1 行 `Benchmark: off` |
| M5 | `src/main.c` | 修改 | 发送线程逐周期打点 + 可选绝对定时 + `--cycles` 上限 + 退出时导出 CSV | `--benchmark` 关闭时零额外开销 |
| M6 | `src/main.c` | 修改 | 新增 4 个长选项的解析与参数校验 | 不影响原选项 |
| M7 | `src/main.c` | **缺陷修复** | `join(receiver)` 之前 `shutdown(g_sockfd, SHUT_RDWR)` | 仅影响退出路径 |
| M8 | `tests/test_protocol.c` | 新增 | 16 类协议状态机测试（直接编译生产源码） | 不参与主程序 |
| M9 | `tests/mock_robot.c` | 新增 | loopback 模拟机器人服务端 + 下行帧校验 | 不参与主程序 |
| M10 | `Makefile` | 修改 | 新增 `mock/test/test-asan/cross-check/help` 目标 | `make` 默认目标不变 |
| M11 | `performance_evaluation/**` | 新增 | 文档、脚本、原始数据 | 不参与构建 |

---

## 2. 逐项说明

### M1/M2：新增 `metrics` 模块（为什么单独建模块）

- **原因**：周期测量需要"预分配 + 运行期零 IO + 退出时统一导出"三段式逻辑，
  塞进 `sender_thread` 会让它既管业务又管统计。单独成模块也更便于复用与审查。
- **关键设计**：
  - 采样数组在 `metrics_init()` 里 `calloc` 一次（默认 300000 × 48B ≈ 14MB），
    运行期**不再分配/释放**，避免堆抖动污染测量；
  - 运行期**不 printf、不写文件**，只在进程退出时 `metrics_write_csv()` + `metrics_print_summary()`；
  - 单写者（只有发送线程写），因此**无锁、无数据竞争**；导出发生在 `pthread_join` 之前，
    由发送线程自己完成，主线程不读，规避了跨线程读取。
- **回退方式**：删除这两个文件并撤销 M3~M6 即可；不影响任何既有功能。

### M3/M4：配置项扩展（为什么用配置开关而不是编译期宏）

- **原因**：任务要求"提供 `--benchmark` / `--metrics-csv` 等可选标志，默认行为不变"。
  运行期开关便于同一份二进制在不同工况下复用，也避免"测试版/生产版"二进制分叉。
- 新增字段：`benchmark`、`abs_timer`、`max_cycles`、`metrics_csv[256]`。
- **回退**：把 `cfg->benchmark` 恒置 0 即完全回到原行为。

### M5：发送线程埋点（为什么插在这里）

- 测量点精确套住**原有业务代码**：
  - `t0` = `build_control_frame()` 之前 → 周期起点的实际时刻；
  - `t1` = `tcp_send_all()` 返回之后 → 周期工作结束时刻；
  - `work = t1 - t0` 即"组帧 + 发送"耗时，**不包含** sleep。
- **不做的事**：不在循环内打印；不改变组帧内容；不改变发送频率；
  不改变 `seq` 递增与溢出回绕语义。
- **可选绝对定时 `--abs-timer`**：以线程启动时刻为基准算 `next_sched_ns`，
  用 `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)`。这是**新增的可选模式**，
  默认仍走原来的相对睡眠路径。基线数据先用相对模式采集，符合"测完基线才增加可选模式"的要求。
- **`--cycles N`**：跑满 N 个周期后置 `g_running=0` 主动收尾，用于自动化定长采集。
  默认 0（不限制），与改动前完全一致。
- **回退**：去掉 `metrics_on` 分支与 `--abs-timer`/`--cycles` 分支即可。

### M6：命令行扩展

新增 `--benchmark`、`--metrics-csv <path>`、`--cycles <n>`、`--abs-timer`，
沿用既有的 `getopt_long` + `long_opts[long_idx].name` 反查写法，**风格与原代码一致**。

### M7：缺陷修复 —— `join(receiver)` 之前的 `shutdown()`

- **问题**（审计 + 实测复现）：`receiver_thread` 阻塞在阻塞式 `recv()` 上且无超时；
  SIGINT 由内核投递给任意未屏蔽它的线程，若被主线程/sender 收走而对端静默，
  receiver 永不返回，`pthread_join` 永久挂起。
- **证据**：
  - `results/03_sigint_hang_baseline_before_fix.txt`：基线二进制 6 秒内未退出，
    `required_kill9=yes`；
  - `results/03_sigint_hang_after_shutdown_fix.txt`：修复后 `sigint_to_exit_latency_s=0.102`。
- **修复方式**：`pthread_join(sender)` 之后、`pthread_join(receiver)` 之前调用
  `shutdown(g_sockfd, SHUT_RDWR)`。内核会让阻塞中的 `recv` 立刻返回 0，receiver 随即退出。
- **为什么这是最小改动**：不引入 `SO_RCVTIMEO`（会改变 recv 返回语义与调用方分支），
  不引入非阻塞 IO，不改信号处理结构，不新增线程；只在退出路径加一次 `shutdown`。
  `shutdown` 只影响本端读写，**不改变协议交互与正常收发行为**。
- **回退**：删除该行即可恢复原行为（同时恢复"静默对端 + Ctrl+C 挂死"的缺陷）。

### M8/M9：测试代码（为什么不改生产代码来做测试）

- `Makefile` 中 `PROTO_SRC := src/robot_protocol.c`，测试二进制**直接编译生产源码**，
  避免"测试用解析器与生产解析器不一致"的假通过。
- `mock_robot` 只作为测试对端，**不链接进主程序**，不引入任何运行时依赖。
- **回退**：删除 `tests/` 与 Makefile 中对应目标即可。

### M10：Makefile

- `make` 默认目标仍是构建 `robot_linux_app`，**行为不变**。
- 新增目标：`mock`、`test`、`test-asan`、`cross-check`、`help`。
- `cross-check` 只做 ARM 交叉编译的**编译**验证（`-c`，不链接、不部署），
  用于证明改动后仍保持 ARM 交叉编译可用。

---

## 3. 明确"没有做"的事（避免夸大）

| 没有做的事 | 原因 |
|---|---|
| 修改协议帧格式 / 增加 CRC / 长度字段 | 违反"保留现有协议"的硬约束；且会造成与真实机器人不兼容 |
| 增加断线自动重连 | 任务明确要求"若没有断线重连，不为本次测试擅自新增复杂重连模块" |
| 设置 `TCP_KEEPIDLE/KEEPINTVL/KEEPCNT` | 会改变连接生命周期行为，超出本次测量目标；仅在报告中记录为能力边界 |
| 引入实时调度（`SCHED_FIFO`）/ `mlockall` | 会改变原程序运行语义，且需要 root；应作为独立课题评估 |
| 修改日志模块（如改为批量刷盘） | 会改变原落盘语义；本次仅把"每帧 fflush"记录为审计发现 |
| 修改设备树 / 内核源码 / 驱动 | 本项目是纯用户态工程，无相关文件；确认无必要性 |
| 引入第三方测试框架（cmocka/Unity） | 违反"不引入新依赖"；自建 40 行测试框架已足够覆盖 |

---

## 4. 编译与静态检查结果

| 命令 | 结果 |
|---|---|
| `make clean && make` | 成功，**0 warning 0 error**（`-Wall -Wextra`） |
| `make pc` / `make arm` | 两侧均成功，0 warning |
| `make test` | 编译成功；运行 **16 PASS / 0 FAIL**，退出码 0 |
| `make test-asan` | ASan+UBSan 运行完毕，**无任何 sanitizer 报告，无泄漏报告** |
| `make cross-check` | `cross-check OK (ARM objects built)`（arm-linux-gnueabihf-gcc 4.9.4） |

> 注：`make test-asan` 的 `detect_leaks=1` 只覆盖**协议解析+测试代码**这条路径；
> 完整程序的 ASan 覆盖需要另外插桩，本次未做，报告中不宣称全程序已通过 ASan。
