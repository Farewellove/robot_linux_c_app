# source_audit.md —— 项目源码审计与实际修改位置

> **审计日期**：2026-10-11
> **审计对象**：`/home/why/C/robot_linux_c_app/`
> **方法**：逐文件完整阅读全部源码/头文件/Makefile/README 后如实记录；所有结论均标注文件与行号。
> **基线版本**：Git commit `197bb98`（`git log --oneline` 可查，工作区干净）
> **重要前提**：本项目是**纯用户态 Linux C 工程**。审计确认源码中不存在内核模块、设备树、
> platform_driver、I2C/SPI/V4L2 驱动代码。因此 `/home/why/zdyz/alientek_linux/.../imx6ull-alientek-emmc.dts`
> 与本项目**无依赖关系**，本次**未修改任何设备树或内核源码**（路径存在性已确认，但无需改动）。

---

## 1. 构建与运行环境（实测）

| 项目 | 值 | 证据 |
|---|---|---|
| 语言标准 | C11 | `Makefile`：`-std=c11` |
| 编译器（本地） | gcc 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04.3) | `gcc --version` |
| 交叉编译器 | arm-linux-gnueabihf-gcc (Linaro GCC 4.9-2017.01) 4.9.4 | `arm-linux-gnueabihf-gcc --version` |
| 构建系统 | GNU Make，`src/*.c` 由 `wildcard` 自动收集 | `Makefile:19` |
| 编译选项 | `-std=c11 -Wall -Wextra -O2 -pthread -D_POSIX_C_SOURCE=200112L` | `Makefile:9-12` |
| 运行平台 | x86_64 Linux，kernel `6.8.0-138-generic`，#138~22.04.1-Ubuntu，12 核 | `uname -a` / `nproc` |
| 动态依赖 | 仅 libc（`ldd robot_linux_app` 只列出 `libc.so.6`、`ld-linux-x86-64.so.2`） | `ldd` |
| 第三方库 | 无（Makefile 无任何 `-l` 外部库） | `Makefile` |
| 基线产物 | `robot_linux_app`，22880 字节，md5 `e9893eaf534794ddca6be9c543c75d76` | `ls -l` / `md5sum` |
| 基线编译结果 | **成功，零警告**（`-Wall -Wextra` 无输出） | `make` |
| 主程序启动命令 | `./output/pc/robot_linux_app -i <ip> -p <port> -e <0\|1> -m <0\|1\|2> --vx/--vy/--vz <int8> --period <ms>`（板端为 `/lib/modules/4.1.15/robot_linux_app`） | `src/main.c` getopt_long |
| 运行期依赖 | 无需 root；未使用 `SCHED_FIFO`/`mlockall`，即**普通 CFS 分时调度** | 源码中无相关调用 |

---

## 2. 线程与共享数据模型（标注文件:行号）

### 2.1 线程

| 线程 | 入口 | 文件:行号 | 职责 | 退出条件 |
|---|---|---|---|---|
| 主线程 | `main()` | `src/main.c` main() | 解析参数、连接、开日志、创建/回收线程、清理 | join 返回后 cleanup |
| 发送线程 | `sender_thread()` | `src/main.c:113` | 周期组帧 + `tcp_send_all` | `g_running==0` 或发送失败或达到 `--cycles` |
| 接收线程 | `receiver_thread()` | `src/main.c:244` | 阻塞 `recv` → 粘包状态机 → 回调 | `g_running==0` 或 `recv` 返回 ≤0 |

- 线程创建：`src/main.c:402`（sender）、`src/main.c:407`（receiver）
- 线程回收：`src/main.c:429`（join sender）、`src/main.c:434`（join receiver）
- 本轮新增：`src/main.c:432` 在 `join(receiver)` 前执行 `shutdown(g_sockfd, SHUT_RDWR)`

### 2.2 共享数据与保护

| 共享对象 | 定义 | 访问者 | 保护机制 | 审计结论 |
|---|---|---|---|---|
| `g_running` | `src/main.c:40` | 信号处理函数 / 主线程 / 两工作线程 | `volatile sig_atomic_t` | 仅作布尔退出标志，读写原子，安全 |
| `g_sockfd` | `src/main.c`（static int） | sender(send)/receiver(recv)/main(shutdown,close) | 无锁 | 内核 socket 层对同一 fd 的 send/recv 是安全的；close 发生在两个线程 join 之后，无并发 |
| `g_log_fp` | `src/main.c`（static FILE*） | 仅 receiver 线程回调写 | 实际由 `g_print_mutex` 串行化 | 只有 receiver 写，不存在双写；mutex 是冗余但无害的保险 |
| `stdout`（日志打印） | 系统 | receiver 回调 / **sender 线程的 `printf` / 主线程 `printf`** | 只有 `on_parsed_frame` 加 `g_print_mutex` | ⚠️ **sender/main 的 printf 未加锁**，实测出现输出交错（见 `logs/disconnect_fin_app.log` 中 `[CAR] ... vyrecv: connection closed by peer`）。仅影响可读性，不影响协议与数据 |
| `cfg (AppConfig)` | `main` 栈 | sender 线程只读 | 无（只读） | join 前 main 栈有效，安全 |
| `RecvBuffer rb` | receiver 线程栈 | receiver 独占 | 无 | 线程局部，安全 |
| `Metrics`（本轮新增） | sender 线程栈 + 堆数组 | 仅 sender 写，join 后 main 不读 | 单写者 | 无数据竞争；导出发生在发送线程自身，join 之前 |

### 2.3 退出时序（本次修复的真实缺陷）

审计发现并在实测中复现：`receiver_thread` 阻塞在**阻塞式 `recv()`**（`src/robot_tcp.c:98`）上，
且从未设置 `SO_RCVTIMEO`。`SIGINT/SIGTERM` 由内核投递给"任意一个未屏蔽该信号的线程"，
若被主线程或 sender 收走，而对端此刻静默（不发数据也不断开），receiver 会一直阻塞，
`src/main.c:434` 的 `pthread_join` 永久挂起，进程只能 `kill -9`。

- 复现证据：`performance_evaluation/results/03_sigint_hang_baseline_before_fix.txt`
  （基线二进制 6 秒内未退出，`required_kill9=yes`）
- 修复证据：`performance_evaluation/results/03_sigint_hang_after_shutdown_fix.txt`
  （`sigint_to_exit_latency_s=0.102`，`required_kill9=no`）

---

## 3. 10ms 周期任务到底是怎么实现的

**结论：相对睡眠（relative sleep），不是绝对时间定时，也不是忙等待。**

原始实现（基线 `src/main.c` 发送线程内）：

```c
struct timespec req;
req.tv_sec  = period_ns / 1000000000L;
req.tv_nsec = period_ns % 1000000000L;
clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);   /* flag=0 => 相对睡眠 */
```

关键事实：

1. 使用 `CLOCK_MONOTONIC`，**不受 NTP/校时影响**，这一点是正确设计。
2. `flag=0` 表示 `req` 是**相对时长**，睡眠从"本次调用时刻"起算。
3. 因此**实际周期 = 目标周期 + 本周期工作耗时（组帧+send）**，误差会逐周期累积。
4. 源码中**没有独立的目标时刻基准**（`scheduled_ns`），所以基线阶段
   **不能凭空生成 `scheduled_ns` 与唤醒延迟**——本次实现如实把该列记为 `NA`。
5. 未使用 `TIMER_ABSTIME`、未使用 `timerfd`、未使用忙等待、未设置实时调度策略。

**统计打点插入位置**：`src/main.c` 发送线程循环体内，`build_control_frame()` 之前
（`t0` → 周期开始）与 `tcp_send_all()` 返回之后（`t1` → 周期结束），即
`work = t1 - t0` 精确覆盖"组帧 + 发送"这段原有业务代码。

本轮在**不改动默认行为**的前提下新增了可选的绝对时间模式（`--abs-timer`，
`src/main.c:214`），使 `scheduled_ns` / 唤醒延迟 / 超期判定成为可测指标。

---

## 4. TCP 应用层帧边界（协议真实约束）

| 问题 | 源码事实 | 位置 |
|---|---|---|
| 帧长 | 上下行均固定 20 字节 | `include/robot_protocol.h:8-9` |
| 下行帧头尾 | `[0]=0x77`，`[19]=0x0A` | `src/robot_protocol.c` build_control_frame |
| 上行帧边界 | **帧尾标志** `[18]==0x55 && [19]==0x56` | `src/robot_protocol.c:24-28` |
| 上行帧类型 | `[0]=='u'`（UWB）/ `[0]=='c'`（车体状态） | `src/robot_protocol.c` recv_buffer_feed |
| 是否有长度字段 | **没有** | 协议为定长 20B |
| 是否有转义机制 | **没有** | 源码中无任何转义处理 |
| 是否有校验字段 | **没有**（既无 CRC 也无 checksum）。`0x55 0x56` 只是**同步标志**，不是校验 | 源码通读确认 |
| 载荷是否可能含 `0x55 0x56` | **可能**。只有 `[18][19]` 位置被当作帧尾，其他位置的同值字节不会误判（已用测试 T11 验证） | `tests/test_protocol.c` T11 |
| 解析器输入接口 | `int recv_buffer_feed(RecvBuffer *rb, const uint8_t *data, size_t len, void (*on_frame)(const ParsedFrame *))`，返回 0，帧数通过回调隐式表达 | `include/robot_protocol.h:65-66` |

**状态机行为（逐字节滑动窗口）**：`wr` 累加到 20 后判断帧尾；匹配则按 `[0]` 分派解析并
回调、`wr=0`；不匹配则 `memmove` 左移 1 字节、`wr=19` 继续。

**审计发现的边界行为（必须如实记录，不得美化为"完美协议"）**：

1. **帧尾合法但帧头既非 `'u'` 也非 `'c'` 时，这 20 字节被静默丢弃**
   （`src/robot_protocol.c` recv_buffer_feed 内的注释已写明）。这是**静默丢数据**，
   且丢弃的是"帧尾恰好等于 0x55 0x56 的 20 字节窗口"，存在把真实帧一并吞掉的可能。
   测试用例 T08 固化了这一行为。
2. **无校验字段**，因此**无法检测载荷内容损坏**（位翻转、截断但恰好对齐）。
   报告与简历中**不得声称具备数据完整性校验能力**。
3. 最坏情况下需要丢 19 字节才能重新对齐（帧尾同步的固有代价）。

---

## 5. send / recv 错误处理核查

| 检查项 | 现状 | 位置 | 结论 |
|---|---|---|---|
| 短收发（部分写） | `tcp_send_all` 循环直到 `remaining==0` | `src/robot_tcp.c:66-86` | ✅ 已处理 |
| `EINTR`（send） | `continue` 重试 | `src/robot_tcp.c:74` | ✅ 已处理 |
| `EINTR`（recv） | 返回 0 表示"本次无数据"，调用方继续 | `src/robot_tcp.c:100` | ✅ 已处理（语义自定义，调用方匹配） |
| `EAGAIN/EWOULDBLOCK` | **未处理**（socket 未设非阻塞，且未设 `SO_RCVTIMEO`） | — | ⚠️ 当前不会出现；一旦引入超时就需要补 |
| `send()` 返回 0 | 打印"connection closed by peer"并返回 -1 | `src/robot_tcp.c:78-81` | ✅ 已处理 |
| `recv()` 返回 0 | 判定对端关闭，返回 -1 让上层退出 | `src/robot_tcp.c:104-108` | ✅ 已处理（FIN 场景实测生效） |
| 接收缓冲区边界 | 单次 recv 到 4096 暂存区（`RECV_BUF_SIZE`），帧边界由状态机负责；状态机内部数组严格 20 字节 | `src/main.c:249`/`src/robot_protocol.c` | ✅ 无越界（ASan 验证） |
| 坏帧恢复 | `memmove` 左移 1 字节重同步 | `src/robot_protocol.c` | ✅ 有恢复行为 |
| `SIGPIPE` | `MSG_NOSIGNAL` + `signal(SIGPIPE, SIG_IGN)` 双重防护 | `src/robot_tcp.c:72` / `src/main.c:384` | ✅ 已处理 |
| 连接断开检测 | 依赖 `recv` 返回 0/错误（对端 FIN/RST） | `src/robot_tcp.c` | ✅ 实测有效（`03_disconnect_tests.csv`） |
| 空闲断线检测 | 设置了 `SO_KEEPALIVE`，但**未设置 `TCP_KEEPIDLE/KEEPINTVL/KEEPCNT`**，使用内核默认（空闲 7200s 才开始探测） | `src/robot_tcp.c:127-131` | ⚠️ **`SO_KEEPALIVE` 不等于及时断线检测**：拔网线场景下默认要 2 小时以上才会报错 |
| 发送/接收超时 | 完全没有（无 `SO_SNDTIMEO`/`SO_RCVTIMEO`） | — | ⚠️ 阻塞式 IO，是第 2.3 节挂死问题的根因 |
| 断线重连 | **未实现** | — | 明确能力边界：断线后程序整体退出，不自动重连 |
| 心跳/应用层保活 | **未实现** | — | — |

---

## 6. 日志、信号安全与资源管理核查

| 项目 | 现状 | 结论 |
|---|---|---|
| 信号处理函数 | 仅 `g_running = 0`，无 IO、无 malloc、无锁 | ✅ 异步信号安全 |
| 日志写入 | `fprintf` + **每帧 `fflush`** | 落盘及时，但高频刷盘；且 **`fprintf` 返回值未检查**（磁盘满/写失败无法感知） |
| 日志并发 | `on_parsed_frame` 在 `g_print_mutex` 内完成 printf+写文件 | ✅ 文件内容不交错 |
| 控制台并发 | sender/main 的 printf 未持锁 | ⚠️ 输出可交错（不影响协议） |
| 定时任务是否在循环中打印 | 发送线程循环内**无 printf**（仅启动/退出打印） | ✅ 不污染测量 |
| 资源释放 | `goto cleanup` 统一路径；socket `shutdown+close`、日志 `fclose`、mutex `destroy` | ✅ 静态路径完整 |
| 内存分配 | 运行期无 `malloc`（仅 `fopen` 内部），全部栈上 | ✅ 无双写/双重释放风险 |
| 是否可凭静态阅读断言"无竞态/无泄漏" | **不可以**。本次用 ASan/UBSan 实测辅助，但"未报警"≠形式化证明 | 已在报告中如实标注 |

---

## 7. 基线可编译/可运行状态（修改前记录）

| 项目 | 结果 |
|---|---|
| `make clean && make` | 成功，**0 warning 0 error** |
| 二进制 | `robot_linux_app` 22880B，md5 `e9893eaf534794ddca6be9c543c75d76` |
| 现有测试 | **不存在**（项目内无任何 tests/、无测试框架、无 CI） |
| Git 版本记录 | 存在（3 个 commit，基线 `197bb98`），工作区干净 |
| 自动化测试基础设施 | 无 → 本次从零建立 |
| 性能数据 | **无**（README 与文档中未记录任何实测数字） |
| 与真实机器人联调记录 | **无任何证据**（无日志、无联调文档、默认 IP `192.168.1.103` 无法确认是真实地址） |

> **边界声明**：本次全部功能验证均基于 **PC 本机 loopback（127.0.0.1）TCP 模拟对端**，
> **没有接入真实机器人、没有做真实闭环控制测试**。模拟服务端只复现"字节流时序与断线行为"，
> 不能代表真实机器人的协议实现、时延特性与执行器响应。

---

## 8. 本轮实际修改位置一览

| 文件 | 改动性质 | 位置 | 是否影响默认行为 |
|---|---|---|---|
| `include/metrics.h`（新增） | 新增指标采集接口 | 全文 | 否 |
| `src/metrics.c`（新增） | 预分配采样、CSV 导出、P50/P95/P99/max 统计 | 全文 | 否 |
| `include/app_config.h` | 新增 `benchmark/abs_timer/max_cycles/metrics_csv` 字段与默认路径宏 | 结构体尾部 | 否（默认全关） |
| `src/app_config.c` | 默认值填充 + 启动横幅打印新字段 | `app_config_set_defaults` / `app_config_print` | 仅多打印 1 行 Banner |
| `src/main.c` | 发送线程逐周期打点、绝对定时模式、`--cycles` 上限、退出时导出 CSV | `sender_thread` | 否（`--benchmark` 关闭时零开销） |
| `src/main.c` | 新增 `--benchmark/--metrics-csv/--cycles/--abs-timer` 长选项 | `main()` getopt_long 段 | 否 |
| `src/main.c:432` | **缺陷修复**：join(receiver) 前 `shutdown()` 唤醒阻塞 recv | `main()` | 否（仅影响退出路径） |
| `tests/test_protocol.c`（新增） | 协议状态机测试，直接编译生产源码 | 全文 | 否 |
| `tests/mock_robot.c`（新增） | loopback 模拟服务端，校验下行帧 | 全文 | 否 |
| `Makefile` | 新增 `mock/test/test-asan/cross-check` 目标 | 全文 | 否（`make` 默认目标不变） |
| `performance_evaluation/`（新增） | 交付文档、脚本、原始数据 | — | 否 |

**未改动**：`src/robot_protocol.c`、`src/robot_tcp.c`、`src/logger.c` 的**任何一行**。
协议格式、帧长、发送频率、接收缓冲大小、信号处理结构全部保持原样。
**未改动**：设备树、内核源码、任何驱动。
