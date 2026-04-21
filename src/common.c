/*
 * common.c - shared helpers for jvmlab-toybox.
 */
#include "common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

ssize_t jtb_write_all(int fd, const void *buf, size_t n)
{
	const unsigned char *p = buf;
	size_t left = n;

	while (left) {
		ssize_t w = write(fd, p, left);
		if (w < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (w == 0) return -1;
		p += (size_t)w;
		left -= (size_t)w;
	}
	return (ssize_t)n;
}

int jtb_puts(const char *s)
{
	size_t n = strlen(s);
	return jtb_write_all(1, s, n) < 0 ? -1 : 0;
}

int jtb_putln(const char *s)
{
	if (jtb_puts(s) < 0) return -1;
	return jtb_write_all(1, "\n", 1) < 0 ? -1 : 0;
}

void jtb_perror(const char *ctx)
{
	int e = errno;
	const char *msg = strerror(e);

	(void)jtb_write_all(2, "jvmlab-toybox: ", 15);
	if (ctx) {
		(void)jtb_write_all(2, ctx, strlen(ctx));
		(void)jtb_write_all(2, ": ", 2);
	}
	(void)jtb_write_all(2, msg, strlen(msg));
	(void)jtb_write_all(2, "\n", 1);
}

void jtb_errx(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;

	(void)jtb_write_all(2, "jvmlab-toybox: ", 15);
	(void)jtb_write_all(2, buf, (size_t)n);
	(void)jtb_write_all(2, "\n", 1);
}

const struct jtb_cmd *jtb_find(const char *name)
{
	if (!name) return NULL;
	for (size_t i = 0; i < jtb_cmds_len; i++) {
		if (strcmp(jtb_cmds[i].name, name) == 0)
			return &jtb_cmds[i];
	}
	return NULL;
}
