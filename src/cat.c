/* SPDX-License-Identifier: 0BSD */
/*
 * cat - concatenate files (or stdin) to stdout.
 *
 * Uses fixed-size I/O buffer, no line buffering, no seek.
 */
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int copy_fd(int fd, const char *name)
{
	unsigned char buf[4096];

	for (;;) {
		ssize_t r = read(fd, buf, sizeof(buf));
		if (r < 0) {
			if (errno == EINTR) continue;
			jtb_perror(name);
			return 1;
		}
		if (r == 0) return 0;
		if (jtb_write_all(1, buf, (size_t)r) < 0) {
			jtb_perror("stdout");
			return 1;
		}
	}
}

int jtb_main_cat(int argc, char **argv)
{
	int rc = 0;

	if (argc < 2) return copy_fd(0, "stdin");

	for (int i = 1; i < argc && argv[i]; i++) {
		int fd;

		if (strcmp(argv[i], "-") == 0) {
			if (copy_fd(0, "stdin")) rc = 1;
			continue;
		}
		fd = open(argv[i], O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			jtb_perror(argv[i]);
			rc = 1;
			continue;
		}
		if (copy_fd(fd, argv[i])) rc = 1;
		(void)close(fd);
	}
	return rc;
}
