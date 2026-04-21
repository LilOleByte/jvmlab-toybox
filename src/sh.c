/* SPDX-License-Identifier: 0BSD */
/*
 * sh - very small interactive shell for JVMLAB minimal live.
 *
 * Scope (v1):
 *   - Single command per line; no pipes, no redirection, no globbing.
 *   - Quoting: '...' (literal), "..." (allows \\ \" \$ escapes), \x outside
 *     quotes, # starts a comment to end of line.
 *   - Builtins: cd, exit; applet table (ls, clear, cat, echo, pwd, mount).
 *   - External commands via fork + execvp + waitpid.
 *   - Variable expansion: only $? (last exit status).
 *   - Signals: parent ignores SIGINT/SIGQUIT; children get default handlers.
 *
 * No setjmp/longjmp, no globals beyond the read-only command table in main.c,
 * so nested applet invocations are safe (unlike Toybox sh_main recursion).
 */
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define LINE_MAX_SZ 4096
#define TOK_MAX_SZ  4096
#define ARG_MAX_CNT 128

static int read_line(int fd, char *buf, size_t cap, size_t *outlen)
{
	size_t n = 0;
	int got_any = 0;

	for (;;) {
		char c;
		ssize_t r = read(fd, &c, 1);
		if (r < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (r == 0) {
			if (!got_any) return 0;
			buf[n] = '\0';
			*outlen = n;
			return 1;
		}
		got_any = 1;
		if (c == '\n') { buf[n] = '\0'; *outlen = n; return 1; }
		if (n + 1 < cap) buf[n++] = c;
	}
}

static int append_status(char *tokbuf, size_t cap, size_t *ti, int last_status)
{
	char sbuf[16];
	int n = snprintf(sbuf, sizeof(sbuf), "%d", last_status);
	if (n < 0) return -1;
	for (int k = 0; k < n; k++) {
		if (*ti >= cap) return -1;
		tokbuf[(*ti)++] = sbuf[k];
	}
	return 0;
}

static int tokenize(const char *line, char *tokbuf, size_t tokcap,
                    char **argv, int maxargs, int *argcout, int last_status)
{
	size_t ti = 0;
	size_t i = 0;
	int ac = 0;
	int in_token = 0;
	enum { N, S, D } st = N;

#define START_TOKEN() do { \
		if (!in_token) { \
			if (ac >= maxargs - 1) return -1; \
			argv[ac++] = &tokbuf[ti]; \
			in_token = 1; \
		} \
	} while (0)

#define PUSH_CHAR(ch) do { \
		if (ti >= tokcap) return -1; \
		tokbuf[ti++] = (ch); \
	} while (0)

	while (line[i]) {
		char c = line[i];

		if (st == N) {
			if (c == '#') break;
			if (c == ' ' || c == '\t') {
				if (in_token) { PUSH_CHAR('\0'); in_token = 0; }
				i++;
				continue;
			}
			if (c == '\'') { START_TOKEN(); st = S; i++; continue; }
			if (c == '"')  { START_TOKEN(); st = D; i++; continue; }
			if (c == '\\' && line[i + 1]) {
				START_TOKEN();
				PUSH_CHAR(line[i + 1]);
				i += 2;
				continue;
			}
			if (c == '$' && line[i + 1] == '?') {
				START_TOKEN();
				if (append_status(tokbuf, tokcap, &ti, last_status) < 0) return -1;
				i += 2;
				continue;
			}
			START_TOKEN();
			PUSH_CHAR(c);
			i++;
			continue;
		}
		if (st == S) {
			if (c == '\'') { st = N; i++; continue; }
			PUSH_CHAR(c);
			i++;
			continue;
		}
		if (c == '"') { st = N; i++; continue; }
		if (c == '\\' && (line[i + 1] == '"' || line[i + 1] == '\\' || line[i + 1] == '$')) {
			PUSH_CHAR(line[i + 1]);
			i += 2;
			continue;
		}
		if (c == '$' && line[i + 1] == '?') {
			if (append_status(tokbuf, tokcap, &ti, last_status) < 0) return -1;
			i += 2;
			continue;
		}
		PUSH_CHAR(c);
		i++;
	}

#undef START_TOKEN
#undef PUSH_CHAR

	if (st != N) return -2;
	if (in_token) {
		if (ti >= tokcap) return -1;
		tokbuf[ti++] = '\0';
	}
	if (ac >= maxargs) return -1;
	argv[ac] = NULL;
	*argcout = ac;
	return 0;
}

static int run_external(int argc, char **argv)
{
	pid_t pid = fork();
	if (pid < 0) {
		jtb_perror("fork");
		return 1;
	}
	if (pid == 0) {
		(void)signal(SIGINT, SIG_DFL);
		(void)signal(SIGQUIT, SIG_DFL);
		(void)execvp(argv[0], argv);
		jtb_perror(argv[0]);
		_exit(errno == ENOENT ? 127 : 126);
	}
	int st = 0;
	for (;;) {
		pid_t w = waitpid(pid, &st, 0);
		if (w < 0) {
			if (errno == EINTR) continue;
			jtb_perror("wait");
			return 1;
		}
		break;
	}
	(void)argc;
	if (WIFEXITED(st))   return WEXITSTATUS(st);
	if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
	return 1;
}

static int builtin_cd(int argc, char **argv)
{
	const char *dir;

	if (argc >= 2 && argv[1]) dir = argv[1];
	else {
		dir = getenv("HOME");
		if (!dir || !*dir) dir = "/";
	}
	if (chdir(dir) < 0) {
		jtb_perror(dir);
		return 1;
	}
	return 0;
}

static int run_line(const char *line, int *last_status)
{
	char tokbuf[TOK_MAX_SZ];
	char *argv[ARG_MAX_CNT];
	int argc = 0;

	int t = tokenize(line, tokbuf, sizeof(tokbuf), argv,
	                 (int)(sizeof(argv) / sizeof(argv[0])), &argc, *last_status);
	if (t == -2) {
		jtb_errx("sh: unterminated quote");
		*last_status = 2;
		return 2;
	}
	if (t < 0) {
		jtb_errx("sh: line too long or too many tokens");
		*last_status = 2;
		return 2;
	}
	if (argc == 0) return *last_status;

	if (strcmp(argv[0], "exit") == 0) {
		int code = (argc >= 2 && argv[1]) ? atoi(argv[1]) : *last_status;
		exit(code);
	}
	if (strcmp(argv[0], "cd") == 0) {
		*last_status = builtin_cd(argc, argv);
		return *last_status;
	}

	const struct jtb_cmd *c = jtb_find(argv[0]);
	if (c && c->fn != jtb_main_sh) {
		*last_status = c->fn(argc, argv);
		return *last_status;
	}

	*last_status = run_external(argc, argv);
	return *last_status;
}

int jtb_main_sh(int argc, char **argv)
{
	int last = 0;

	if (argc >= 3 && argv[1] && strcmp(argv[1], "-c") == 0 && argv[2]) {
		(void)run_line(argv[2], &last);
		return last;
	}

	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);

	int tty = isatty(0);
	char line[LINE_MAX_SZ];

	for (;;) {
		if (tty) (void)jtb_write_all(1, "$ ", 2);
		size_t n = 0;
		int rc = read_line(0, line, sizeof(line), &n);
		if (rc < 0) { jtb_perror("stdin"); break; }
		if (rc == 0) break;
		(void)run_line(line, &last);
	}
	if (tty) (void)jtb_write_all(1, "\n", 1);
	return last;
}
