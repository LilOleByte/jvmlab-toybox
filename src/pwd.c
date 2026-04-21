/* SPDX-License-Identifier: 0BSD */
/*
 * pwd - print current working directory.
 *
 * Uses a bounded stack buffer; does not call getcwd(NULL, 0) to avoid
 * depending on malloc in the common path.
 */
#include "common.h"

#include <string.h>
#include <unistd.h>

int jtb_main_pwd(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	char buf[4096];
	if (!getcwd(buf, sizeof(buf))) {
		jtb_perror("pwd");
		return 1;
	}
	if (jtb_write_all(1, buf, strlen(buf)) < 0) return 1;
	if (jtb_write_all(1, "\n", 1) < 0) return 1;
	return 0;
}
