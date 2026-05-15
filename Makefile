# Makefile for Skytraq Command API
#
# Works on:
#   - Linux (gcc / clang)
#   - Windows MSYS2 UCRT64 (gcc from the ucrt64 toolchain)
#   - Windows MinGW
#
# Usage:
#   make                   - build ./gnss_tool (or gnss_tool.exe on Windows)
#   make clean             - remove build artefacts

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -std=c99
LDFLAGS ?=

SRCS = gnss_tool.c protocol.c serial.c messages.c
OBJS = $(SRCS:.c=.o)

# Detect Windows (MSYS2 UCRT64 / MinGW) so we get the .exe suffix automatically.
ifeq ($(OS),Windows_NT)
  TARGET = gnss_tool.exe
  # No extra libs needed; serial uses kernel32 which is linked by default.
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Linux)
    TARGET = gnss_tool
    # No extra libs needed; termios is in libc.
  else
    TARGET = gnss_tool
  endif
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJS) $(TARGET)

# Header dependencies (small project, so explicit is fine)
gnss_tool.o: gnss_tool.c serial.h protocol.h messages.h
protocol.o:  protocol.c protocol.h serial.h
serial.o:    serial.c serial.h
messages.o:  messages.c messages.h

.PHONY: all clean
