# results/ —— 实测数据目录

## 当前状态

**本目录已于 2026-10-13 清空（不含子目录外的任何文件）。**

原因：项目转入**开发板实测阶段**，按需求清除此前基于 PC loopback 模拟对端的离线测试数据，
避免 PC 模拟数据与板端真实数据混在同一目录下。

- 被清除的内容：`03_periodic_timing_*.csv`、`03_resource_usage_*.csv`、
  `03_mock_server_*.csv`、`03_disconnect_tests.csv`、`03_parser_tests.csv`、
  `03_sigint_hang_*.txt`、`03_bench_summary.txt`
- 清除前的完整副本：`/tmp/robot_offline_sim_backup_20261013.tar.gz`（临时位置，重启后可能丢失）
- **历史数值未被删除**：所有汇总结论仍保留在 `../performance_report.md` 与
  `../resume_improvements.md` 的表格中，仅是原始 CSV 不再随仓库保存。

## 如何重新生成

离线（PC loopback）数据全部可由脚本一键重放：

```bash
# 协议状态机测试
make test
# 周期调度 4 场景 × 10000 周期
bash ../scripts/run_all_bench.sh 10000
# 断线 + 退出时序
bash ../scripts/run_disconnect_test.sh
bash ../scripts/run_sigint_hang_test.sh output/pc/robot_linux_app after_shutdown_fix 6
# 30 分钟资源观测
bash ../scripts/run_soak.sh 1800 5
```

板端实测数据请统一以 `board_` 前缀命名，例如：

```
board_periodic_timing_<soc>_<scenario>.csv
board_resource_usage_<soc>_<minutes>min.csv
board_mock_server_<soc>.csv
```

命名区分是强制的：**报告里必须能一眼区分"PC 模拟数据"与"板端实测数据"**。
