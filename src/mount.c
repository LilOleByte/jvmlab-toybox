/*
 * mount - minimal mount(2) wrapper.
 *
 * Forms:
 *   mount                       → dump /proc/mounts
 *   mount SRC TGT TYPE          → mount(src, tgt, type, 0, NULL)
 *
 * EBUSY is treated as success so repeated calls from /init are idempotent
 * (e.g. kernel-mounted devtmpfs).
 */
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <unistd.h>

static int dump_mounts(void)
{
	int fd = open("/proc/mounts", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		jtb_perror("/proc/mounts");
		return 1;
	}
	unsigned char buf[4096];
	int rc = 0;
	for (;;) {
		ssize_t r = read(fd, buf, sizeof(buf));
		if (r < 0) {
			if (errno == EINTR) continue;
			jtb_perror("/proc/mounts");
			rc = 1;
			break;
		}
		if (r == 0) break;
		if (jtb_write_all(1, buf, (size_t)r) < 0) { rc = 1; break; }
	}
	(void)close(fd);
	return rc;
}

int jtb_main_mount(int argc, char **argv)
{
	if (argc == 1) return dump_mounts();

	if (argc != 4) {
		jtb_errx("usage: mount [SRC TGT TYPE]");
		return 2;
	}

	if (mount(argv[1], argv[2], argv[3], 0, NULL) == 0) return 0;
	if (errno == EBUSY) return 0;

	jtb_perror(argv[2]);
	return 1;
}
