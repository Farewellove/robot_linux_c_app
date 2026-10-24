# performance_evaluation —— 交付物索引

本目录是"机器人通信与状态监测系统"性能量化与工程质量提升任务的完整交付材料。
所有数字均来自本机实际运行产生的原始 CSV/日志，**未运行的项目一律标注 NOT_RUN**。

## 文档

| 文件 | 内容 |
|---|---|
| `USAGE.md` | **项目使用说明与标准流程**：构建、日常开发验证、性能量化流程、真机与板端流程、排查速查表 |
| `source_audit.md` | 源码只读审计：构建环境、线程与共享数据模型（带行号）、10ms 周期实现方式、协议帧边界真实约束、send/recv 错误处理核查、基线状态、本轮修改位置一览 |
| `modification_report.md` | 逐项修改内容、原因、是否改变默认行为、回退方式，以及"明确没有做的事" |
| `test_plan.md` | 测试环境、命令、指标定义、场景矩阵、验收标准、可复现性清单、NOT_RUN 及板端执行步骤 |
| `performance_report.md` | 真实性能数据、统计口径、结论与能力边界、汇总量化表、与改动前的对比 |
| `resume_improvements.md` | 可直接使用的简历描述（每条挂钩证据）、面试追问准备、禁止使用的表述 |

## 脚本（`scripts/`）

| 脚本 | 用途 |
|---|---|
| `board_bench_pc.sh` | **板端实测 - PC 侧编排**：起模拟设备端 → 等开发板连接 → 等采集结束 → 自动复算 + 归档 + 校验 |
| `board_bench_board.sh` | **板端实测 - 板端脚本**（由 `make deploy` 复制到 `/lib/modules/4.1.15`）：记录环境 → 采集 → CSV 落在 NFS 目录 |
| `run_periodic_bench.sh` | 单场景 10ms 周期调度测量（idle / traffic / load × relative / absolute） |
| `run_all_bench.sh` | 依次跑完 4 个场景并汇总（默认 10000 周期/场景） |
| `mock_robot`（`../tests/mock_robot.c`） | loopback 模拟机器人服务端，逐帧校验下行控制帧 |
| `run_disconnect_test.sh` | 断线检测（FIN / RST）与 SIGINT 退出时序测试 |
| `run_sigint_hang_test.sh` | 对端静默 + SIGINT 的退出时延回归（可用于修复前后 A/B） |
| `run_soak.sh` | 长期运行资源观测（RSS / 线程数 / fd / 上下文切换） |
| `monitor_resource.sh` | 进程级资源采样 |
| `analyze_timing.py` | **独立复算**周期调度 CSV（交叉校验程序内置统计，不信任程序汇总） |
| `analyze_resource.py` | 汇总资源观测 CSV |

## 原始数据（`results/`）

| 文件 | 说明 |
|---|---|
> **数据状态（2026-10-13）**：下表所列离线（PC loopback 模拟）原始数据已按需求清除，
> 项目转入**开发板实测阶段**。重新生成方式见 `results/README.md`；
> 汇总结论仍保留在 `performance_report.md` 中，作为板端实测的对照基线。
> 板端采集的数据请统一以 `board_` 前缀命名。

| 文件 | 说明 |
|---|---|
| `03_parser_tests.csv` | 协议状态机 16 类用例结果（含 10000 帧固定种子用例） |
| `03_periodic_timing_{idle,traffic,load}_relative.csv` | 逐周期原始时间戳（各 10000 行） |
| `03_periodic_timing_traffic_absolute.csv` | 绝对时间定时模式逐周期原始时间戳（含 scheduled_ns） |
| `03_resource_usage_*.csv` | 各场景的 RSS / 线程数 / fd / 上下文切换采样 |
| ~~`03_resource_usage_soak.csv`~~ | 已清理：30 分钟 soak 结论（360 点，RSS/线程数/fd 变化量均为 0）已并入 `performance_report.md` |
| `03_mock_server_*.csv` | 模拟服务端统计（收到控制帧数 / 坏帧数 / seq 跳变数） |
| `03_disconnect_tests.csv` | 断线检测结果 |
| `03_sigint_hang_*.txt` | 退出时序修复前后的 A/B 结果 |
| `03_bench_summary.txt` | 全部场景的汇总输出 |

## 原始日志（`logs/`）

| 文件 | 说明 |
|---|---|
| `01_make_test_stdout.log` | `make test` 完整输出（16 PASS / 0 FAIL） |
| `02_make_test_asan_stdout.log` | ASan + UBSan + 泄漏检测运行输出 |
| `03_cross_check.log` | ARM 交叉编译可用性检查输出 |
| `04_analyze_resource.log` | 资源 CSV 的独立复算输出 |
| `05_git_diff.patch` | 基线 commit 与本次工作区完整 diff（证据留存） |
| `analyze_timing.log` | 周期 CSV 的独立复算输出（与程序内置汇总逐项一致） |
| ~~`bench_all.log` / `soak_run.log`~~ | 已清理：运行日志不入库，需要时用 `scripts/` 里的脚本重跑 |
| `app_*.log` / `mock_*.log` | 各场景的被测程序与模拟服务端原始日志 |

## 一键复现

```bash
make clean && make                       # 构建 PC + ARM 两套到 output/
make test && make test-asan              # 协议测试 + 插桩测试
make cross-check                         # ARM 交叉编译检查（完整编译 + 链接）
make deploy                              # 部署 ARM 程序到 NFS 根文件系统
bash performance_evaluation/scripts/run_all_bench.sh 10000   # ≈7 分钟
bash performance_evaluation/scripts/run_disconnect_test.sh   # ≈10 秒
bash performance_evaluation/scripts/run_sigint_hang_test.sh output/pc/robot_linux_app after_shutdown_fix 6
bash performance_evaluation/scripts/run_soak.sh 1800 5       # 30 分钟
```

### 与任务书中目录名的对应关系

任务书使用 `reports/03_*.md` 与 `results/03_*.csv`。本次统一收敛到 `performance_evaluation/`：

| 任务书路径 | 实际路径 |
|---|---|
| `reports/03_source_audit.md` | `performance_evaluation/source_audit.md` |
| `reports/03_test_plan.md` | `performance_evaluation/test_plan.md` |
| `reports/03_results.md` | `performance_evaluation/performance_report.md` |
| `reports/03_resume_evidence.md` | `performance_evaluation/resume_improvements.md` |
| `results/03_periodic_timing.csv` | `performance_evaluation/results/03_periodic_timing_*.csv`（按场景拆分） |
| `results/03_parser_tests.csv` | `performance_evaluation/results/03_parser_tests.csv` |
| `results/03_resource_usage.csv` | `performance_evaluation/results/03_resource_usage_*.csv` |
| （修改记录） | `performance_evaluation/modification_report.md` |
