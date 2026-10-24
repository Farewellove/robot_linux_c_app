#!/usr/bin/env bash
#
# make_source_bundle.sh —— 生成 source_bundle.md（单文件看懂整个项目）
#
# 用法:
#   bash scripts/make_source_bundle.sh                # 输出到 ./source_bundle.md
#   bash scripts/make_source_bundle.sh /tmp/out.md    # 指定输出路径
#
# 组成（顺序即阅读顺序）：
#   1. 导读、文件清单、构建/运行/测试速查
#   2. 开发问题记录：docs/开发问题记录.md（手写，改那份文件即可）
#   3. 关键文档：README、真实传感器接入与实测记录、源码审计、实测结论汇总
#   4. 构建：Makefile、.gitignore
#   5. 源码：include/*.h、src/*.c
#   6. 测试与对端程序：tests/*.c
#   7. 脚本：performance_evaluation/scripts/*
#   8. 附录：面试审计报告、面试准备文档（面试向，工程结论以第 3 段为准）
#
# 说明：被打包的 Markdown 自身含有 ``` 代码块，因此外层统一用 4 个反引号包裹，
# 避免嵌套围栏截断内容。
#
set -u

cd "$(dirname "$0")/.." || exit 1

OUT="${1:-source_bundle.md}"
NARRATIVE="docs/开发问题记录.md"

DOC_FILES=(
  README.md
  docs/真实传感器接入与实测记录.md
  performance_evaluation/source_audit.md
  performance_evaluation/results/measurement_summary.md
)
# 量化交付文档（脚本、测试计划、报告、改动清单等）
QE_FILES=(
  performance_evaluation/README.md
  performance_evaluation/USAGE.md
  performance_evaluation/test_plan.md
  performance_evaluation/performance_report.md
  performance_evaluation/modification_report.md
  performance_evaluation/resume_improvements.md
  performance_evaluation/results/README.md
  performance_evaluation/logs/README.md
)
BUILD_FILES=(Makefile .gitignore scripts/make_source_bundle.sh)
HEADER_FILES=(include/*.h)
SRC_FILES=(src/*.c)
TEST_FILES=(tests/*.c)
SCRIPT_FILES=(performance_evaluation/scripts/*)
APPENDIX_FILES=(docs/项目源码面试审计报告.md docs/面试准备文档.md)

# ---------- 工具函数 ----------

# 按扩展名选代码块语言标记
lang_of() {
  case "$1" in
    *.c)      echo c ;;
    *.h)      echo c ;;
    *.sh)     echo bash ;;
    *.py)     echo python ;;
    *.md)     echo markdown ;;
    *)        echo text ;;
  esac
}

# 输出一个文件的完整内容（4 反引号围栏，避免内部 ``` 截断）
emit_file() {
  local f="$1"
  if [ ! -f "$f" ]; then
    printf '\n## 文件: %s\n\n_（文件不存在，已跳过）_\n' "$f"
    return
  fi
  printf '\n## 文件: %s\n\n' "$f"
  printf '````%s\n' "$(lang_of "$f")"
  cat "$f"
  printf '\n````\n'
}

emit_group() {
  local title="$1"; shift
  printf '\n---\n\n# %s\n' "$title"
  for f in "$@"; do emit_file "$f"; done
}

count_lines() {
  local total=0
  for f in "$@"; do
    [ -f "$f" ] && total=$(( total + $(wc -l < "$f") ))
  done
  echo "$total"
}

# ---------- 生成 ----------

{
  printf '# robot_linux_c_app 单文件项目包\n\n'
  printf '> 生成时间：%s\n' "$(date '+%Y-%m-%d %H:%M:%S')"
  printf '> 生成脚本：`scripts/make_source_bundle.sh`（源码或文档改动后重跑即可刷新本文件）\n\n'
  printf '**阅读顺序**：① 导读 → ② 开发问题记录（真实踩坑）→ ③ 关键文档 → ④ 量化交付文档 →\n'
  printf '⑤ 构建与忽略规则 → ⑥ 源码：头文件 → ⑦ 源码：实现 → ⑧ 测试与对端程序 →\n'
  printf '⑨ 测量脚本 → ⑩ 面试向附录。\n\n'

  printf -- '---\n\n# 1. 导读\n\n'
  printf '## 项目一句话\n\n'
  printf '纯 C11 + POSIX 的机器人 TCP 通信上位机（从 Qt/C++ 版重构而来，零第三方依赖），\n'
  printf '并接入了开发板上的真实传感器：用户态读 ICM20608 / AP3216C 的 IIO sysfs，\n'
  printf '组包成 20 字节定长帧经 TCP 上传，已在 i.MX6ULL（ARMv7 / Linux 4.1.15）上实测。\n\n'

  printf '## 三条必须守住的边界\n\n'
  printf '1. **没有写内核驱动** —— 传感器数据走的是板端既有 IIO 驱动导出的 sysfs；\n'
  printf '2. **不是硬实时** —— 周期偏差板端 p99 263 μs、传感器采样 p99 10.375 ms，都是普通 CFS 调度结果；\n'
  printf '3. **没有真实机器人闭环** —— 有真实传感器，但下发的控制帧没有真实执行机构响应。\n\n'

  printf '## 文件清单\n\n```\n'
  find . -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.md' -o -name '*.sh' -o -name '*.py' -o -name 'Makefile' -o -name '.gitignore' \) \
    -not -path './.git/*' -not -path './obj/*' -not -path './output/*' -not -path './log/*' \
    -not -name 'source_bundle.md' \
    | sed 's|^\./||' | sort
  printf '```\n\n'
  printf '（上面是仓库全部受版本管理的文本类文件；`obj/`、`output/`、`log/` 是构建与运行产物，不入库。）\n\n'

  printf '## 速查命令\n\n```bash\n'
  printf 'make                # PC + ARM 全量编译（0 warning 0 error）\n'
  printf 'make test           # 协议状态机 19 类用例\n'
  printf 'make test-asan      # ASan + UBSan + 泄漏检测\n'
  printf 'make deploy         # 交叉编译并部署到 NFS 板端目录\n'
  printf 'make clean-logs     # 只清 log/robot_linux_c_app/ 下的运行期日志\n'
  printf '\n'
  printf '# 本机自测（虚拟数据）\n'
  printf './output/pc/mock_robot -b 127.0.0.1 -p 19090 --period 10\n'
  printf './output/pc/robot_linux_app -i 127.0.0.1 -p 19090 --period 10 --cycles 500\n'
  printf '\n'
  printf '# 真实传感器（板端监听，PC 去连）\n'
  printf '#  板端: ./load_drivers.sh && ./sensor_robot -p 19090 --period 10 --imu-every 1 --ap-every 10\n'
  printf '#  PC  : ./output/pc/robot_linux_app -i <板子IP> -p 19090 -e 0 -m 0 --period 10\n'
  printf '```\n\n'
  printf '实测环境地址：PC `192.168.1.66`，开发板 `192.168.1.55`。\n\n'
  printf '实测结论（原始 CSV 不入库）：`performance_evaluation/results/measurement_summary.md`。\n'

  # 第 2 段：开发问题记录（手写）
  printf '\n---\n\n# 2. 开发问题记录（真实踩坑 / 原因 / 解决）\n'
  if [ -f "$NARRATIVE" ]; then
    printf '\n'
    # 去掉文件自带的一级标题，避免与上面的章节标题重复
    tail -n +2 "$NARRATIVE"
  else
    printf '\n_（缺少 %s）_\n' "$NARRATIVE"
  fi

  emit_group "3. 关键文档" "${DOC_FILES[@]}"
  emit_group "4. 量化交付文档" "${QE_FILES[@]}"
  emit_group "5. 构建与忽略规则" "${BUILD_FILES[@]}"
  emit_group "6. 源码：头文件" "${HEADER_FILES[@]}"
  emit_group "7. 源码：实现" "${SRC_FILES[@]}"
  emit_group "8. 测试与对端程序" "${TEST_FILES[@]}"
  emit_group "9. 测量脚本" "${SCRIPT_FILES[@]}"
  emit_group "10. 附录：面试向文档（工程结论以第 3 段为准）" "${APPENDIX_FILES[@]}"

  printf '\n---\n\n'
  printf '_本文件由 `scripts/make_source_bundle.sh` 生成，请勿手工编辑；_\n'
  printf '_需要修改叙述性内容请改 `docs/开发问题记录.md` 后重跑脚本。_\n'
} > "$OUT"

echo "已生成: $OUT"
echo "  行数: $(wc -l < "$OUT")"
echo "  大小: $(du -h "$OUT" | cut -f1)"
