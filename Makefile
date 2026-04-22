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

# Warning profile. -Werror on the things that historically correlate
# with security bugs (implicit decls, format-string injection) instead
# of a blanket -Werror that breaks on harmless upstream drift.
WARN    = -Wall -Wextra -Wformat=2 -Wshadow -Wvla -Wconversion \
          -Werror=implicit-function-declaration \
          -Werror=format-security

# Hardening flags -- rationale lives in jvmlab-build/README.md
# ("Userspace hardening"). Summary: CET indirect-call protection, stack
# canaries + guard-page probes, zero-init stack locals, PIE with full
# RELRO and immediate binding, _FORTIFY_SOURCE=2 bounds checks.
# Requires GCC 12+ or clang 16+ (ubuntu-24.04 / CachyOS both satisfy).
HARDEN  = -fstack-protector-strong \
          -fstack-clash-protection \
          -fcf-protection=full \
          -ftrivial-auto-var-init=zero \
          -fPIE \
          -D_FORTIFY_SOURCE=2

SIZE    = -Os -ffunction-sections -fdata-sections \
          -fno-strict-aliasing -fno-omit-frame-pointer
STD     = -std=c11

CFLAGS  ?= $(STD) $(SIZE) $(WARN) $(HARDEN)
# -static-pie: static binary + kernel-applied ASLR on exec. Full RELRO
# (-z relro -z now) makes GOT / .data.rel.ro read-only after load so a
# single arbitrary write cannot be escalated to code execution via
# PLT/GOT overwrite (still relevant inside static-pie musl).
LDFLAGS ?= -static-pie \
           -Wl,-z,noexecstack \
           -Wl,-z,relro -Wl,-z,now \
           -Wl,--gc-sections

.PHONY: all clean install smoke

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

# smoke: run the POSIX-sh smoke tests against the freshly built binary.
smoke: $(BIN)
	BIN=$(abspath $(BIN)) sh tests/smoke.sh
