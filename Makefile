# jvmlab-toybox: hardened static multicall binary for JVMLAB minimal Linux.
#
# Default toolchain: musl (musl-gcc). Override CC=cc for glibc, or set
# CFLAGS / LDFLAGS from the environment if needed (minimal.sh does).

# := so a shell-exported CC=cc does not silently override this default.
CC      := musl-gcc
BIN      = jvmlab-toybox

SRCS = src/main.c src/common.c \
       src/sh.c src/ls.c src/clear.c src/cat.c \
       src/echo.c src/pwd.c src/mount.c
OBJS = $(SRCS:.c=.o)

WARN    = -Wall -Wextra -Wformat=2 -Wshadow -Wvla -Wconversion \
          -Werror=implicit-function-declaration
HARDEN  = -fstack-protector-strong -D_FORTIFY_SOURCE=2
SIZE    = -Os -ffunction-sections -fdata-sections \
          -fno-strict-aliasing -fno-omit-frame-pointer
STD     = -std=c11

CFLAGS  ?= $(STD) $(SIZE) $(WARN) $(HARDEN)
LDFLAGS ?= --static -Wl,-z,noexecstack -Wl,--gc-sections

.PHONY: all clean install

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c src/common.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJS)

# install BINDIR=/path: drop binary and symlinks into BINDIR.
BINDIR ?= /usr/bin
APPLETS = sh ls clear cat echo pwd mount

install: $(BIN)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	for n in $(APPLETS); do \
	  ln -sf $(BIN) $(DESTDIR)$(BINDIR)/$$n; \
	done
