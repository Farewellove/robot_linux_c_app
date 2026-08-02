CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2 -pthread
CFLAGS  += -D_POSIX_C_SOURCE=200112L

INCDIR  := include
SRCDIR  := src
OBJDIR  := obj
TARGET  := robot_linux_app

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET)
