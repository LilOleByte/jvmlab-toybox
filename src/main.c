/*
 * main.c - jvmlab-toybox multicall dispatcher.
 *
 * Resolves basename(argv[0]); if called as "jvmlab-toybox" directly, expects
 * the applet name as argv[1]. Unknown/empty invocations print a short usage
 * and exit non-zero. No global setjmp state, no recursion across applets.
 */
#include "common.h"

#include <string.h>

const struct jtb_cmd jtb_cmds[] = {
	{ "sh",    jtb_main_sh,    "sh [-c CMD]"         },
	{ "ls",    jtb_main_ls,    "ls [-a] [PATH...]"   },
	{ "clear", jtb_main_clear, "clear"               },
	{ "cat",   jtb_main_cat,   "cat [FILE...]"       },
	{ "echo",  jtb_main_echo,  "echo [-n] [STR...]"  },
	{ "pwd",   jtb_main_pwd,   "pwd"                 },
	{ "mount", jtb_main_mount, "mount [SRC TGT TYPE]"},
};
const size_t jtb_cmds_len = sizeof(jtb_cmds) / sizeof(jtb_cmds[0]);

static const char *base_name(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

static int print_list(void)
{
	(void)jtb_puts("jvmlab-toybox " JTB_VERSION "\napplets:");
	for (size_t i = 0; i < jtb_cmds_len; i++) {
		(void)jtb_write_all(1, " ", 1);
		(void)jtb_puts(jtb_cmds[i].name);
	}
	(void)jtb_write_all(1, "\n", 1);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 1 || !argv[0]) return 127;

	const char *name = base_name(argv[0]);

	if (strcmp(name, "jvmlab-toybox") == 0) {
		if (argc < 2) return print_list();
		if (strcmp(argv[1], "--help") == 0)    return print_list();
		if (strcmp(argv[1], "--version") == 0) {
			(void)jtb_putln("jvmlab-toybox " JTB_VERSION);
			return 0;
		}
		name = argv[1];
		argv += 1;
		argc -= 1;
	}

	const struct jtb_cmd *c = jtb_find(name);
	if (!c) {
		jtb_errx("unknown applet: %s", name);
		return 127;
	}
	return c->fn(argc, argv);
}
