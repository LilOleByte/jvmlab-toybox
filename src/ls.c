/* SPDX-License-Identifier: 0BSD */
/*
 * ls - one name per line, optional -a. No sorting, no columns, no -l.
 *
 * Writes a blank line plus a "PATH:" header only when multiple paths are given.
 */
#include "common.h"

#include <dirent.h>
#include <string.h>

static int list_one(const char *path, int show_all, int show_header)
{
	DIR *d = opendir(path);
	if (!d) {
		jtb_perror(path);
		return 1;
	}

	if (show_header) {
		(void)jtb_puts(path);
		(void)jtb_write_all(1, ":\n", 2);
	}

	int rc = 0;
	for (;;) {
		struct dirent *e = readdir(d);
		if (!e) break;
		if (!show_all && e->d_name[0] == '.') continue;
		if (jtb_putln(e->d_name) < 0) { rc = 1; break; }
	}
	(void)closedir(d);
	return rc;
}

int jtb_main_ls(int argc, char **argv)
{
	int show_all = 0;
	int i = 1;

	while (i < argc && argv[i] && argv[i][0] == '-' && argv[i][1] != '\0') {
		if (strcmp(argv[i], "--") == 0) { i++; break; }
		for (const char *p = argv[i] + 1; *p; p++) {
			if (*p == 'a') show_all = 1;
			else {
				jtb_errx("ls: unknown option -%c", *p);
				return 2;
			}
		}
		i++;
	}

	if (i >= argc) return list_one(".", show_all, 0);

	int multi = (argc - i) > 1;
	int rc = 0;
	for (int first = 1; i < argc && argv[i]; i++, first = 0) {
		if (multi && !first) (void)jtb_write_all(1, "\n", 1);
		if (list_one(argv[i], show_all, multi)) rc = 1;
	}
	return rc;
}
