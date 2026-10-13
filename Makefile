# robot_linux_c_app 构建脚本
#
# 产物统一放在 output/ 下，按平台分目录：
#
#   output/pc/mock_robot        模拟设备端（对端，主动推送数据）      ← PC 上运行
#   output/pc/robot_linux_app   接收端（上位机）                      ← PC 上本机自测
#   output/arm/robot_linux_app  接收端（上位机）                      ← 开发板上运行（主）
#   output/arm/mock_robot       模拟设备端                            ← 开发板上运行（反向拓扑用）
#
# 典型用法：
#   make                同时编译 PC 与 ARM 两套
#   make pc             只编译 PC 侧（gcc）
#   make arm            只编译 ARM 侧（交叉编译）
#   make deploy         把 ARM 程序复制到板端工作目录（默认 rootfs/lib/modules/4.1.15）
#   make test           协议解析状态机测试（PC 侧，16 类用例）
#   make test-asan      ASan + UBSan 版本
#   make clean          清理 obj/ 与 output/
#
# 源文件由 wildcard 自动收集 src/*.c，新增 .c 无需改本文件。

CC       ?= gcc
CROSS    ?= arm-linux-gnueabihf-gcc

CFLAGS   := -std=c11 -Wall -Wextra -O2 -pthread
# 声明使用 POSIX.1-2001 接口（clock_nanosleep、localtime_r 等），
# 不加这一项在 -std=c11 严格模式下会看不到这些函数的声明
CFLAGS   += -D_POSIX_C_SOURCE=200112L

INCDIR   := include
SRCDIR   := src
TESTDIR  := tests
SCRIPTS_DIR := performance_evaluation/scripts
OBJDIR   := obj
OUTDIR   := output
PC_DIR   := $(OUTDIR)/pc
ARM_DIR  := $(OUTDIR)/arm

APP      := robot_linux_app    # 接收端（上位机）：src/*.c
SIM      := mock_robot         # 模拟设备端：tests/mock_robot.c

# NFS 根文件系统 + 板端统一工作目录。
# 板端把所有外设测试程序与驱动（ap3216cAPP / gt911_recovery_test / *.ko ...）都放在
# /lib/modules/4.1.15（见其 load_drivers.sh 里的 MOD_DIR），本项目沿用同一约定。
# 可用 make deploy NFS_DIR=/path/to/rootfs 覆盖根文件系统路径。
NFS_DIR  ?= /home/why/zdyz/nfs/rootfs
BOARD_SUBDIR ?= lib/modules/4.1.15
NFS_BIN  := $(NFS_DIR)/$(BOARD_SUBDIR)

APP_SRCS   := $(wildcard $(SRCDIR)/*.c)
SIM_SRCS   := $(TESTDIR)/mock_robot.c
PROTO_SRC  := $(SRCDIR)/robot_protocol.c

# 两个架构各自独立的 .o 目录 —— 绝不能共用，否则会把上一架构的目标文件链进新二进制
PC_OBJS  := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/pc/%.o,$(APP_SRCS))
ARM_OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/arm/%.o,$(APP_SRCS))

.PHONY: all pc arm deploy test test-asan cross-check clean help

all: pc arm

pc: $(PC_DIR)/$(APP) $(PC_DIR)/$(SIM)

arm: $(ARM_DIR)/$(APP) $(ARM_DIR)/$(SIM)

# ---------------- 编译：PC（gcc） ----------------
$(OBJDIR)/pc/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

$(PC_DIR)/$(APP): $(PC_OBJS)
	@mkdir -p $(PC_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(PC_DIR)/$(SIM): $(SIM_SRCS)
	@mkdir -p $(PC_DIR)
	$(CC) $(CFLAGS) -o $@ $^

# ---------------- 编译：ARM（交叉） ----------------
$(OBJDIR)/arm/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CROSS) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

$(ARM_DIR)/$(APP): $(ARM_OBJS)
	@mkdir -p $(ARM_DIR)
	$(CROSS) $(CFLAGS) -o $@ $^

$(ARM_DIR)/$(SIM): $(SIM_SRCS)
	@mkdir -p $(ARM_DIR)
	$(CROSS) $(CFLAGS) -o $@ $^

# ---------------- NFS 部署 ----------------
# 把 ARM 侧程序复制到 NFS 根文件系统的 /lib/modules/4.1.15（板端统一工作目录）
deploy: arm
	@if [ ! -d "$(NFS_DIR)" ]; then \
		echo "错误: NFS 根文件系统不存在: $(NFS_DIR)"; \
		echo "      用 make deploy NFS_DIR=/path/to/rootfs 指定正确路径"; \
		exit 1; \
	fi
	@mkdir -p $(NFS_BIN)
	cp -v $(ARM_DIR)/$(APP) $(NFS_BIN)/
	cp -v $(ARM_DIR)/$(SIM) $(NFS_BIN)/
	cp -v $(SCRIPTS_DIR)/board_bench_board.sh $(NFS_BIN)/
	@chmod +x $(NFS_BIN)/board_bench_board.sh
	@echo
	@echo "已部署到板端工作目录: $(NFS_BIN)/"
	@echo "对应板端路径:        /$(BOARD_SUBDIR)/"
	@ls -l $(NFS_BIN)/$(APP) $(NFS_BIN)/$(SIM) $(NFS_BIN)/board_bench_board.sh
	@echo
	@echo "板端（开发板串口）执行："
	@echo "  cd /$(BOARD_SUBDIR) && ./board_bench_board.sh <PC的IP> 6000"
	@echo
	@echo "PC 侧先跑编排脚本（会自动等连接、复算、归档）："
	@echo "  bash $(SCRIPTS_DIR)/board_bench_pc.sh 6000"

# ---------------- 测试 ----------------
$(PC_DIR)/test_protocol: $(TESTDIR)/test_protocol.c $(PROTO_SRC)
	@mkdir -p $(PC_DIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ $^

# 协议解析状态机测试：直接编译生产解析器源码，测的就是上线代码
test: $(PC_DIR)/test_protocol
	./$(PC_DIR)/test_protocol performance_evaluation/results/03_parser_tests.csv

# ASan + UBSan：用于排查越界/未定义行为。
# 注意：ASan 只能"报出问题"，未报警不等于形式化证明无缺陷。
test-asan:
	@mkdir -p $(PC_DIR)
	$(CC) -std=c11 -Wall -Wextra -O1 -g -pthread -D_POSIX_C_SOURCE=200112L \
	      -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -I$(INCDIR) -o $(PC_DIR)/test_protocol_asan $(TESTDIR)/test_protocol.c $(PROTO_SRC)
	ASAN_OPTIONS=detect_leaks=1 ./$(PC_DIR)/test_protocol_asan /tmp/03_parser_tests_asan.csv

# ---------------- 交叉编译可用性检查 ----------------
cross-check: arm
	@echo "cross-check OK: ARM 侧已完整交叉编译并链接通过"
	@ls -l $(ARM_DIR)/$(APP) $(ARM_DIR)/$(SIM)

# ---------------- 其它 ----------------
help:
	@echo "目标："
	@echo "  all (默认) / pc / arm / deploy / test / test-asan / cross-check / clean"
	@echo
	@echo "产物与角色："
	@echo "  output/pc/mock_robot        模拟设备端（PC 运行，主动向主板推送数据）"
	@echo "  output/pc/robot_linux_app   接收端（PC 本机自测）"
	@echo "  output/arm/robot_linux_app  接收端（开发板运行，主用）"
	@echo "  output/arm/mock_robot       模拟设备端（开发板运行，反向拓扑用）"
	@echo
	@echo "NFS 根文件系统  : $(NFS_DIR)"
	@echo "板端部署目录    : /$(BOARD_SUBDIR)  （板端统一工作目录）"

clean:
	rm -rf $(OBJDIR) $(OUTDIR)
