# logs/ —— 原始日志目录

## 当前状态

**本目录已于 2026-10-13 清除离线测试日志**，仅保留两份与"板端实测阶段"仍然相关的文件：

| 文件 | 保留原因 |
|---|---|
| `03_cross_check.log` | ARM 交叉编译可用性检查记录。进入板端实测阶段**必须**依赖它，不属于离线测试数据 |
| `05_git_diff.patch` | 相对基线 commit `197bb98` 的工作区差异**快照**（第一次提交前保存）。改动已入库后，权威记录以 `git log` / `git diff 197bb98 HEAD` 为准，本文件仅作单文件留档 |

已清除的内容：`01_make_test_stdout.log`、`02_make_test_asan_stdout.log`、
`analyze_timing.log`、`04*_analyze_resource*.log`、`06_socket_observation.log`、
`app_*.log`、`mock_*.log`、`bench_all.log`、`soak_run.log`。

清除前的完整副本：`/tmp/robot_offline_sim_backup_20261013.tar.gz`。

## 如何重新生成

所有日志都由 `../scripts/` 下的脚本自动写入本目录：

```bash
make test                                                     # 协议测试输出
make test-asan                                                # ASan/UBSan 输出
bash ../scripts/run_all_bench.sh 10000                        # 基准全流程
bash ../scripts/run_soak.sh 1800 5                            # 30 分钟观测
```

板端日志建议用 `board_` 前缀，与离线日志区分：

```
board_app_<soc>_<scenario>.log
board_console_<soc>_<scenario>.log
```

## 板端采集注意

板端通常没有 `python3`，`analyze_*.py` 请**在主机侧**对拷回的 CSV 运行。
板端只需保证 `uname -a`、`/proc/cpuinfo`、`nproc` 的输出被记录下来。
