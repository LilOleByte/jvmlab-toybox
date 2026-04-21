/* SPDX-License-Identifier: 0BSD */
/*
 * clear - reset cursor, erase screen, clear scrollback (ANSI).
 */
#include "common.h"

int jtb_main_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	static const char seq[] = "\033[H\033[2J\033[3J";
	return jtb_write_all(1, seq, sizeof(seq) - 1) < 0 ? 1 : 0;
}
