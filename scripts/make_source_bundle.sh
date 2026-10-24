#!/usr/bin/env bash
#
# make_source_bundle.sh —— 生成 source_bundle.md（只看这一个文件就能理解整个项目）
#
# 用法:
#   bash scripts/make_source_bundle.sh                # 输出到 ./source_bundle.md
#   bash scripts/make_source_bundle.sh /tmp/out.md    # 指定输出路径
#
# 设计原则：
#   1. **自洽**：背景、拓扑、运行流程、协议、模块职责、设计取舍、能力边界、实测数字
#      全部内联在"1. 导读"里（内容源 docs/bundle_intro.md），不出现需要跳出阅读的引用；
#   2. **全贴**：仓库里所有能贴的文本文件（源码、头文件、文档、脚本、构建文件、证据日志）
#      整篇贴进本文件，只有"原始测试数据 CSV"不贴（结论已在导读里给出）；
#   3. 静态文本放在 docs/bundle_intro.md / docs/开发问题记录.md，脚本只负责拼接，
#      因此脚本自身也能被收录而不产生重复。
#
# 组成（顺序即阅读顺序）：
#   1. 导读（docs/bundle_intro.md）  2. 开发问题记录  3. 关键文档  4. 量化交付文档与证据
#   5. 构建与忽略规则    6. 源码：头文件  7. 源码：实现  8. 测试与对端程序
#   9. 测量脚本          10. 附录：面试向文档
#
# 说明：被打包的 Markdown 自身含有 ``` 代码块，因此外层统一用 4 个反引号包裹，
# 避免嵌套围栏截断内容。
#
set -u

cd "$(dirname "$0")/.." || exit 1

OUT="${1:-source_bundle.md}"
INTRO="docs/bundle_intro.md"
NARRATIVE="docs/开发问题记录.md"

DOC_FILES=(
  README.md
  docs/真实传感器接入与实测记录.md
  performance_evaluation/source_audit.md
  performance_evaluation/results/measurement_summary.md
)
QE_FILES=(
  performance_evaluation/README.md
  performance_evaluation/USAGE.md
  performance_evaluation/test_plan.md
  performance_evaluation/performance_report.md
  performance_evaluation/modification_report.md
  performance_evaluation/resume_improvements.md
  performance_evaluation/results/README.md
  performance_evaluation/logs/README.md
  performance_evaluation/logs/03_cross_check.log
  performance_evaluation/logs/05_git_diff.patch
)
# 说明：docs/bundle_intro.md 是第 1 段导读的内容源，已在开头整段输出，
# 不再作为"文件"重复贴一次（docs/开发问题记录.md 同理，是第 2 段正文）
BUILD_FILES=(Makefile .gitignore scripts/make_source_bundle.sh)
HEADER_FILES=(include/*.h)
SRC_FILES=(src/*.c)
TEST_FILES=(tests/*.c)
SCRIPT_FILES=(performance_evaluation/scripts/*)
APPENDIX_FILES=(docs/项目源码面试审计报告.md docs/面试准备文档.md)

# ---------- 工具函数 ----------

lang_of() {
  case "$1" in
    *.c)      echo c ;;
    *.h)      echo c ;;
    *.sh)     echo bash ;;
    *.py)     echo python ;;
    *.md)     echo markdown ;;
    *.log)    echo text ;;
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

# ---------- 生成 ----------

FILELIST="$(mktemp)"
{
  printf '```\n'
  find . -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.md' -o -name '*.sh' -o -name '*.py' \
       -o -name '*.log' -o -name '*.patch' -o -name 'Makefile' -o -name '.gitignore' \) \
    -not -path './.git/*' -not -path './obj/*' -not -path './output/*' -not -path './log/*' \
    -not -name 'source_bundle.md' \
    | sed 's|^\./||' | sort
  printf '```\n'
} > "$FILELIST"

{
  printf '# robot_linux_c_app 单文件项目包\n\n'
  printf '> 生成时间：%s\n' "$(date '+%Y-%m-%d %H:%M:%S')"
  printf '> 生成脚本：`scripts/make_source_bundle.sh`（源码或文档改动后重跑即可刷新本文件）\n\n'

  cat <<'MD'
**阅读顺序**：① 导读（自洽速览）→ ② 开发问题记录（真实踩坑）→ ③ 关键文档 →
④ 量化交付文档与证据 → ⑤ 构建与忽略规则 → ⑥ 源码：头文件 → ⑦ 源码：实现 →
⑧ 测试与对端程序 → ⑨ 测量脚本 → ⑩ 面试向附录。

**这个文件怎么用**：

- **只想了解项目**：读完第 1 段"导读"就够了 —— 背景、系统拓扑、运行流程、协议格式、
  模块职责、设计取舍、能力边界、实测数字全部在里面，不需要再看别的东西。
- **想逐行核对**：第 3 段起是各文件的完整原文，仓库里所有源码、头文件、文档、
  脚本、构建文件、证据日志都整篇贴在这里。
- **不贴的东西**：只有原始测试/采集数据（CSV）没贴，因为体积大且可重新采集，
  结论已经在第 1.10 节给出。
MD

  # 第 1 段：导读（docs/bundle_intro.md，其中 <!--FILE_LIST--> 用实际清单替换）
  printf '\n---\n\n'
  sed -e "/<!--FILE_LIST-->/r $FILELIST" -e "/<!--FILE_LIST-->/d" "$INTRO"

  # 第 2 段：开发问题记录
  printf '\n---\n\n# 2. 开发问题记录（真实踩坑 / 原因 / 解决）\n'
  if [ -f "$NARRATIVE" ]; then
    printf '\n'
    tail -n +2 "$NARRATIVE"   # 去掉自带的一级标题，避免与章节标题重复
  else
    printf '\n_（缺少 %s）_\n' "$NARRATIVE"
  fi

  emit_group "3. 关键文档" "${DOC_FILES[@]}"
  emit_group "4. 量化交付文档与证据" "${QE_FILES[@]}"
  emit_group "5. 构建与忽略规则（含本文件的生成脚本与导读源文件）" "${BUILD_FILES[@]}"
  emit_group "6. 源码：头文件" "${HEADER_FILES[@]}"
  emit_group "7. 源码：实现" "${SRC_FILES[@]}"
  emit_group "8. 测试与对端程序" "${TEST_FILES[@]}"
  emit_group "9. 测量脚本" "${SCRIPT_FILES[@]}"
  emit_group "10. 附录：面试向文档（工程结论以第 1、3 段为准）" "${APPENDIX_FILES[@]}"

  printf '\n---\n\n'
  printf '_本文件由 `scripts/make_source_bundle.sh` 生成，请勿手工编辑；_\n'
  printf '_叙述性内容请改 `docs/bundle_intro.md`（导读）或 `docs/开发问题记录.md`（踩坑）后重跑脚本。_\n'
} > "$OUT"

rm -f "$FILELIST"

echo "已生成: $OUT"
echo "  行数: $(wc -l < "$OUT")"
echo "  大小: $(du -h "$OUT" | cut -f1)"
