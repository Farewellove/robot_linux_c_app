# robot_linux_c_app 构建脚本
#
#   make        编译，产物为根目录下的 robot_linux_app
#   make clean  删除 obj/ 与可执行文件
#
# 源文件由 wildcard 自动收集 src/*.c，新增 .c 文件无需改本文件。

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2 -pthread
# 声明使用 POSIX.1-2001 接口（clock_nanosleep、localtime_r 等），
# 不加这一项在 -std=c11 严格模式下会看不到这些函数的声明
CFLAGS  += -D_POSIX_C_SOURCE=200112L

INCDIR  := include
SRCDIR  := src
OBJDIR  := obj
TARGET  := robot_linux_app

SRCS := $(wildcard $(SRCDIR)/*.c)
# 把 src/xxx.c 映射成 obj/xxx.o，只重编改动过的文件
OBJS := $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

.PHONY: all clean

all: $(TARGET)

# 链接：把全部目标文件合成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 编译：-I 指定头文件搜索路径，-c 只编译不链接
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET)
