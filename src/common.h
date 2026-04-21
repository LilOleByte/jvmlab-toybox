/*
 * common.h - shared helpers for jvmlab-toybox.
 *
 * Keep this header dependency-free: only libc. No global state.
 */
#ifndef JVMLAB_TOYBOX_COMMON_H
#define JVMLAB_TOYBOX_COMMON_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include <stddef.h>
#include <sys/types.h>

#define JTB_VERSION "0.1.0"

typedef int (*jtb_main_fn)(int argc, char **argv);

struct jtb_cmd {
	const char *name;
	jtb_main_fn fn;
	const char *usage;
};

extern const struct jtb_cmd jtb_cmds[];
extern const size_t jtb_cmds_len;

const struct jtb_cmd *jtb_find(const char *name);

void jtb_perror(const char *ctx);
void jtb_errx(const char *fmt, ...);

ssize_t jtb_write_all(int fd, const void *buf, size_t n);
int jtb_puts(const char *s);
int jtb_putln(const char *s);

int jtb_main_sh(int argc, char **argv);
int jtb_main_ls(int argc, char **argv);
int jtb_main_clear(int argc, char **argv);
int jtb_main_cat(int argc, char **argv);
int jtb_main_echo(int argc, char **argv);
int jtb_main_pwd(int argc, char **argv);
int jtb_main_mount(int argc, char **argv);

#endif
