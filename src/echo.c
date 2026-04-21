/*
 * echo - print arguments separated by single spaces, newline unless -n.
 *
 * No escape processing (no -e). Keeps the surface minimal and predictable.
 */
#include "common.h"

#include <string.h>

int jtb_main_echo(int argc, char **argv)
{
	int i = 1;
	int newline = 1;

	if (i < argc && argv[i] && strcmp(argv[i], "-n") == 0) {
		newline = 0;
		i++;
	}

	for (int first = 1; i < argc && argv[i]; i++, first = 0) {
		if (!first && jtb_write_all(1, " ", 1) < 0) return 1;
		if (jtb_write_all(1, argv[i], strlen(argv[i])) < 0) return 1;
	}
	if (newline && jtb_write_all(1, "\n", 1) < 0) return 1;
	return 0;
}
