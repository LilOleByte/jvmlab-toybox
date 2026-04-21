/*
 * ls — list directory contents (POSIX.1-2024-oriented subset for jvmlab-toybox).
 *
 * Implemented: -a -A -1 -d -l -n -g -o -F -p -R -r -t -S -f -c -u -i -s -k -q
 *              -H -L
 * Not implemented: multi-column -C/-m/-x, locale collation beyond strcmp,
 *                  COLUMNS / tty column layout, exact column padding for -l.
 */
#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

struct ls_opts {
	unsigned d : 1;
	unsigned long_fmt : 1;
	unsigned numeric : 1;
	unsigned no_owner : 1; /* -g */
	unsigned no_group : 1; /* -o */
	unsigned F : 1;
	unsigned p : 1;
	unsigned R : 1;
	unsigned r : 1;
	unsigned t : 1;
	unsigned S : 1;
	unsigned f : 1;
	unsigned c : 1;
	unsigned u : 1;
	unsigned i : 1;
	unsigned s : 1;
	unsigned k : 1;
	unsigned q : 1;
	unsigned H : 1;
	unsigned L : 1;
	/* dotfile visibility: 0 default, 1 -a, 2 -A (last wins between a/A) */
	int dot_mode; /* 0=hide dot, 1=-a, 2=-A */
};

typedef struct {
	char *name;
	char path[4096];
	struct stat st;
} ls_ent;

typedef struct {
	dev_t dev;
	ino_t ino;
} ls_visit;

static const struct ls_opts *g_sort_opts;

static int ls_get_info(const char *path, int follow, struct stat *st)
{
	if (follow) return stat(path, st);
	return lstat(path, st);
}

static void ls_err_path(const char *path)
{
	jtb_perror(path);
}

static void printable_q(const char *s, int q, char *out, size_t cap)
{
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p && o + 1 < cap; p++) {
		unsigned char c = *p;
		if (q && (c < 0x20 || c > 0x7e))
			out[o++] = '?';
		else
			out[o++] = (char)c;
	}
	out[o] = '\0';
}

static void mode_str(mode_t m, char out[11])
{
	static const char rwx[] = "rwx";
	out[0] = S_ISDIR(m) ? 'd' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b'
		: S_ISFIFO(m) ? 'p' : S_ISLNK(m) ? 'l' : S_ISSOCK(m) ? 's' : '-';
	/* Owner / group / other: shifts 6, 3, 0 (see <sys/stat.h> permission bits). */
	for (int i = 0; i < 3; i++) {
		unsigned shift = (unsigned)(6 - 3 * i);
		unsigned bits = (unsigned)(m >> shift) & 7u;
		out[1 + 3 * i + 0] = (bits & 4u) ? rwx[0] : '-';
		out[1 + 3 * i + 1] = (bits & 2u) ? rwx[1] : '-';
		out[1 + 3 * i + 2] = (bits & 1u) ? rwx[2] : '-';
	}
	if (m & S_ISUID) {
		if (out[3] == 'x') out[3] = 's';
		else out[3] = 'S';
	}
	if (m & S_ISGID) {
		if (out[6] == 'x') out[6] = 's';
		else out[6] = 'S';
	}
	if (m & S_ISVTX) {
		if (S_ISDIR(m)) {
			if (out[9] == 'x') out[9] = 't';
			else out[9] = 'T';
		}
	}
	out[10] = '\0';
}

static unsigned long long blk_units(const struct stat *st, int k)
{
	unsigned long long b = (unsigned long long)st->st_blocks;
	if (k) return (b * 512ull + 1023ull) / 1024ull;
	return b;
}

static void fmt_time(const struct stat *st, int use_c, int use_u, char *buf, size_t cap)
{
	time_t tm;
	if (use_c)
		tm = st->st_ctime;
	else if (use_u)
		tm = st->st_atime;
	else
		tm = st->st_mtime;

	struct tm local;
	if (!localtime_r(&tm, &local)) {
		(void)snprintf(buf, cap, "?");
		return;
	}
	time_t now = time(NULL);
	double diff = difftime(now, tm);
	int recent = diff >= 0.0 && diff < (182.0 * 86400.0);
	if (recent)
		(void)strftime(buf, cap, "%b %e %H:%M", &local);
	else
		(void)strftime(buf, cap, "%b %e  %Y", &local);
}

static int write_u64(unsigned long long v)
{
	char s[32];
	int n = snprintf(s, sizeof(s), "%llu", v);
	if (n < 0) return -1;
	return jtb_write_all(1, s, (size_t)n) < 0 ? -1 : 0;
}

static int write_sp(void)
{
	return jtb_write_all(1, " ", 1) < 0 ? -1 : 0;
}

static int classify_char(mode_t m)
{
	if (S_ISDIR(m)) return '/';
	if (S_ISLNK(m)) return '@';
	if (S_ISFIFO(m)) return '|';
	if (S_ISSOCK(m)) return '=';
	if (S_ISREG(m) && (m & (S_IXUSR | S_IXGRP | S_IXOTH))) return '*';
	return 0;
}

static int append_classify(mode_t mode, int F, int p)
{
	if (F) {
		int c = classify_char(mode);
		if (c) {
			char b[2] = { (char)c, '\0' };
			if (jtb_puts(b) < 0) return -1;
		}
	} else if (p && S_ISDIR(mode)) {
		if (jtb_puts("/") < 0) return -1;
	}
	return 0;
}

static int read_link_target(const char *path, char *buf, size_t cap)
{
	ssize_t n = readlink(path, buf, cap - 1);
	if (n < 0) return -1;
	buf[(size_t)n] = '\0';
	return 0;
}

static int emit_long(const struct ls_opts *o, const char *path_display,
                     const char *path_stat, const struct stat *st)
{
	char modebuf[12];
	mode_str(st->st_mode, modebuf);

	if (o->i) {
		if (write_u64((unsigned long long)st->st_ino) < 0) return -1;
		if (write_sp() < 0) return -1;
	}
	if (o->s) {
		if (write_u64(blk_units(st, o->k)) < 0) return -1;
		if (write_sp() < 0) return -1;
	}

	if (jtb_puts(modebuf) < 0) return -1;
	if (write_sp() < 0) return -1;
	if (write_u64((unsigned long long)st->st_nlink) < 0) return -1;
	if (write_sp() < 0) return -1;

	if (!o->no_owner) {
		if (o->numeric) {
			if (write_u64((unsigned long long)st->st_uid) < 0) return -1;
		} else {
			struct passwd *pw = getpwuid(st->st_uid);
			if (pw && pw->pw_name) {
				if (jtb_puts(pw->pw_name) < 0) return -1;
			} else {
				if (write_u64((unsigned long long)st->st_uid) < 0) return -1;
			}
		}
		if (write_sp() < 0) return -1;
	}

	if (!o->no_group) {
		if (o->numeric) {
			if (write_u64((unsigned long long)st->st_gid) < 0) return -1;
		} else {
			struct group *gr = getgrgid(st->st_gid);
			if (gr && gr->gr_name) {
				if (jtb_puts(gr->gr_name) < 0) return -1;
			} else {
				if (write_u64((unsigned long long)st->st_gid) < 0) return -1;
			}
		}
		if (write_sp() < 0) return -1;
	}

	if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode)) {
		unsigned maj = major(st->st_rdev);
		unsigned min = minor(st->st_rdev);
		char di[32];
		(void)snprintf(di, sizeof(di), "%u,%u", maj, min);
		if (jtb_puts(di) < 0) return -1;
	} else {
		char sz[32];
		(void)snprintf(sz, sizeof(sz), "%lld", (long long)st->st_size);
		if (jtb_puts(sz) < 0) return -1;
	}
	if (write_sp() < 0) return -1;

	char tbuf[64];
	fmt_time(st, o->c, o->u, tbuf, sizeof(tbuf));
	if (jtb_puts(tbuf) < 0) return -1;
	if (write_sp() < 0) return -1;

	char disp[4096];
	printable_q(path_display, o->q, disp, sizeof(disp));
	if (jtb_puts(disp) < 0) return -1;

	if (S_ISLNK(st->st_mode)) {
		char lnk[4096];
		if (read_link_target(path_stat, lnk, sizeof(lnk)) == 0) {
			char qlnk[4096];
			printable_q(lnk, o->q, qlnk, sizeof(qlnk));
			if (jtb_puts(" -> ") < 0) return -1;
			if (jtb_puts(qlnk) < 0) return -1;
		}
	} else {
		if (append_classify(st->st_mode, o->F, o->p) < 0) return -1;
	}

	return jtb_putln("") < 0 ? -1 : 0;
}

static int emit_short(const struct ls_opts *o, const char *path_display,
                      const struct stat *st)
{
	char disp[4096];
	printable_q(path_display, o->q, disp, sizeof(disp));
	if (jtb_puts(disp) < 0) return -1;
	if (append_classify(st->st_mode, o->F, o->p) < 0) return -1;
	return jtb_putln("") < 0 ? -1 : 0;
}

static int path_join(const char *dir, const char *name, char *out, size_t cap)
{
	int n = snprintf(out, cap, "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= cap) return -1;
	return 0;
}

static int ent_visible(const char *name, const struct ls_opts *o)
{
	if (o->f) return 1;
	if (o->dot_mode == 1) return 1;
	if (o->dot_mode == 2)
		return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
	return name[0] != '.';
}

static time_t sort_time(const struct stat *st, const struct ls_opts *o)
{
	if (o->c) return st->st_ctime;
	if (o->u) return st->st_atime;
	return st->st_mtime;
}

static int cmp_ent(const void *a, const void *b)
{
	const ls_ent *x = a;
	const ls_ent *y = b;
	const struct ls_opts *o = g_sort_opts;
	int c = 0;

	if (o->S) {
		off_t sx = x->st.st_size;
		off_t sy = y->st.st_size;
		if (sx != sy) c = (sx > sy) ? -1 : 1;
	} else if (o->t) {
		time_t tx = sort_time(&x->st, o);
		time_t ty = sort_time(&y->st, o);
		if (tx != ty) c = (tx > ty) ? -1 : 1;
	} else {
		c = strcmp(x->name, y->name);
	}
	if (c != 0) return o->r ? -c : c;
	return strcmp(x->name, y->name);
}

static unsigned long long total_blocks(ls_ent *ents, size_t n, int k)
{
	unsigned long long t = 0;
	for (size_t i = 0; i < n; i++)
		t += blk_units(&ents[i].st, k);
	return t;
}

static int list_dir(const char *dirpath, const struct ls_opts *o, ls_visit *stk,
                    size_t stk_len, int print_label, const char *label);

static int follow_info_for(const struct ls_opts *o, int cmdline_operand)
{
	return o->L || (o->H && cmdline_operand);
}

static int print_entry(const struct ls_opts *o, const char *path, const char *display,
                       int follow_info)
{
	struct stat st;
	if (ls_get_info(path, follow_info, &st) < 0) {
		ls_err_path(path);
		return 1;
	}
		if (o->long_fmt) return emit_long(o, display, path, &st) ? 1 : 0;
	return emit_short(o, display, &st) ? 1 : 0;
}

/*
 * Returns: 1 = list directory contents, 0 = single entry, -1 = error (missing).
 * Sets *st_out when return is 0 or 1 (lstat or stat per follow_operand).
 */
static int operand_kind(const char *path, const struct ls_opts *o, int cmdline,
                        struct stat *st_out, int *follow_operand)
{
	struct stat lst;
	if (lstat(path, &lst) < 0) return -1;
	*follow_operand = follow_info_for(o, cmdline);
	if (o->d) {
		if (ls_get_info(path, *follow_operand, st_out) < 0) return -1;
		return 0;
	}
	if (S_ISDIR(lst.st_mode)) {
		if (ls_get_info(path, o->L, st_out) < 0) return -1;
		return 1;
	}
	if (S_ISLNK(lst.st_mode)) {
		struct stat stf;
		if (stat(path, &stf) < 0) {
			*st_out = lst;
			return 0;
		}
		if (S_ISDIR(stf.st_mode)) {
			if (!o->long_fmt && !o->F) {
				if (ls_get_info(path, o->L, st_out) < 0) return -1;
				return 1;
			}
			if (ls_get_info(path, *follow_operand, st_out) < 0) return -1;
			return 0;
		}
	}
	if (ls_get_info(path, *follow_operand, st_out) < 0) return -1;
	return 0;
}

static int list_dir(const char *dirpath, const struct ls_opts *o, ls_visit *stk,
                    size_t stk_len, int print_label, const char *label)
{
	struct stat dirst;
	if (ls_get_info(dirpath, o->L, &dirst) < 0) {
		ls_err_path(dirpath);
		return 1;
	}

	if (stk) {
		for (size_t i = 0; i < stk_len; i++) {
			if (stk[i].dev == dirst.st_dev && stk[i].ino == dirst.st_ino) {
				jtb_errx("ls: directory loop: %s", dirpath);
				return 1;
			}
		}
	}

	ls_visit *new_stk = stk;
	size_t new_len = stk_len;
	ls_visit local[256];

	if (o->R) {
		if (!stk) {
			new_stk = local;
			new_len = 0;
		}
		if (new_len >= sizeof(local) / sizeof(local[0])) {
			jtb_errx("ls: recursion depth limit");
			return 1;
		}
		new_stk[new_len].dev = dirst.st_dev;
		new_stk[new_len].ino = dirst.st_ino;
		new_len++;
	}

	DIR *d = opendir(dirpath);
	if (!d) {
		ls_err_path(dirpath);
		return 1;
	}

	size_t cap = 32;
	size_t nent = 0;
	ls_ent *ents = malloc(cap * sizeof(*ents));
	if (!ents) {
		(void)closedir(d);
		return 1;
	}

	int rc = 0;
	for (;;) {
		errno = 0;
		struct dirent *de = readdir(d);
		if (!de) {
			if (errno) {
				ls_err_path(dirpath);
				rc = 1;
			}
			break;
		}
		if (!ent_visible(de->d_name, o)) continue;

		if (nent + 1 > cap) {
			size_t ncap = cap * 2;
			ls_ent *ne = realloc(ents, ncap * sizeof(*ne));
			if (!ne) {
				rc = 1;
				break;
			}
			ents = ne;
			cap = ncap;
		}
		ls_ent *e = &ents[nent];
		e->name = strdup(de->d_name);
		if (!e->name) {
			rc = 1;
			break;
		}
		if (path_join(dirpath, de->d_name, e->path, sizeof(e->path)) < 0) {
			free(e->name);
			rc = 1;
			break;
		}
		if (ls_get_info(e->path, o->L ? 1 : 0, &e->st) < 0) {
			ls_err_path(e->path);
			free(e->name);
			rc = 1;
			continue;
		}
		nent++;
	}
	(void)closedir(d);

	if (rc) {
		for (size_t i = 0; i < nent; i++) free(ents[i].name);
		free(ents);
		return rc;
	}

	if (!o->f) {
		g_sort_opts = o;
		qsort(ents, nent, sizeof(*ents), cmp_ent);
	}

	if (print_label && label) {
		char hd[4096];
		printable_q(label, o->q, hd, sizeof(hd));
		if (jtb_puts(hd) < 0) rc = 1;
		else if (jtb_write_all(1, ":\n", 2) < 0) rc = 1;
	}

	if (o->long_fmt && nent) {
		if (jtb_puts("total ") < 0) rc = 1;
		else if (write_u64(total_blocks(ents, nent, o->k)) < 0) rc = 1;
		else if (jtb_putln("") < 0) rc = 1;
	}

	for (size_t i = 0; i < nent; i++) {
		ls_ent *e = &ents[i];
		if (o->long_fmt) {
			if (emit_long(o, e->name, e->path, &e->st)) rc = 1;
		} else {
			if (emit_short(o, e->name, &e->st)) rc = 1;
		}
	}

	if (o->R && !rc) {
		for (size_t i = 0; i < nent; i++) {
			ls_ent *e = &ents[i];
			if (!S_ISDIR(e->st.st_mode)) continue;
			if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) continue;
			struct stat tst;
			if (lstat(e->path, &tst) == 0 && S_ISLNK(tst.st_mode) && !o->L) continue;
			if (jtb_putln("") < 0) rc = 1;
			char sublbl[4096];
			printable_q(e->name, o->q, sublbl, sizeof(sublbl));
			if (list_dir(e->path, o, new_stk, new_len, 1, sublbl)) rc = 1;
		}
	}

	for (size_t i = 0; i < nent; i++) free(ents[i].name);
	free(ents);
	return rc;
}

static int cmp_str(const void *a, const void *b)
{
	char *const *x = a;
	char *const *y = b;
	return strcmp(*x, *y);
}

int jtb_main_ls(int argc, char **argv)
{
	struct ls_opts o = {0};
	int i = 1;

	while (i < argc && argv[i] && argv[i][0] == '-' && argv[i][1] != '\0') {
		if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		}
		for (const char *p = argv[i] + 1; *p; p++) {
			switch (*p) {
			case 'a':
				o.dot_mode = 1;
				break;
			case 'A':
				o.dot_mode = 2;
				break;
			case '1':
				break; /* one column: only format we implement */
			case 'd':
				o.d = 1;
				break;
			case 'l':
				o.long_fmt = 1;
				break;
			case 'n':
				o.long_fmt = 1;
				o.numeric = 1;
				break;
			case 'g':
				o.long_fmt = 1;
				o.no_owner = 1;
				break;
			case 'o':
				o.long_fmt = 1;
				o.no_group = 1;
				break;
			case 'F':
				o.F = 1;
				break;
			case 'p':
				o.p = 1;
				break;
			case 'R':
				o.R = 1;
				break;
			case 'r':
				o.r = 1;
				break;
			case 't':
				o.t = 1;
				o.S = 0;
				break;
			case 'S':
				o.S = 1;
				o.t = 0;
				break;
			case 'f':
				o.f = 1;
				break;
			case 'c':
				o.c = 1;
				o.u = 0;
				break;
			case 'u':
				o.u = 1;
				o.c = 0;
				break;
			case 'i':
				o.i = 1;
				break;
			case 's':
				o.s = 1;
				break;
			case 'k':
				o.k = 1;
				break;
			case 'q':
				o.q = 1;
				break;
			case 'H':
				o.H = 1;
				break;
			case 'L':
				o.L = 1;
				break;
			default:
				jtb_errx("ls: unknown option -%c", *p);
				return 2;
			}
		}
		i++;
	}

	if (o.f) {
		o.dot_mode = 1;
		o.t = o.S = o.r = 0;
	}

	int ac = argc - i;
	if (ac == 0) {
		struct stat st;
		int fo = 0;
		int k = operand_kind(".", &o, 0, &st, &fo);
		if (k < 0) {
			ls_err_path(".");
			return 1;
		}
		if (k == 1)
			return list_dir(".", &o, NULL, 0, 0, NULL) ? 1 : 0;
		return print_entry(&o, ".", ".", fo) ? 1 : 0;
	}

	char **paths = malloc((size_t)ac * sizeof(*paths));
	if (!paths) return 1;
	int np = 0;
	for (int k = i; k < argc && argv[k]; k++) paths[np++] = argv[k];

	char **files = malloc((size_t)np * sizeof(*files));
	char **dirs = malloc((size_t)np * sizeof(*dirs));
	if (!files || !dirs) {
		free(paths);
		free(files);
		free(dirs);
		return 1;
	}
	int nf = 0, nd = 0;

	for (int k = 0; k < np; k++) {
		struct stat st;
		int fo = 0;
		int kind = operand_kind(paths[k], &o, 1, &st, &fo);
		if (kind < 0) {
			ls_err_path(paths[k]);
			free(paths);
			free(files);
			free(dirs);
			return 1;
		}
		if (kind == 1)
			dirs[nd++] = paths[k];
		else
			files[nf++] = paths[k];
	}

	qsort(files, (size_t)nf, sizeof(*files), cmp_str);
	qsort(dirs, (size_t)nd, sizeof(*dirs), cmp_str);

	int rc = 0;
	int first = 1;
	int multi = np > 1;

	for (int k = 0; k < nf; k++) {
		if (!first && jtb_putln("") < 0) rc = 1;
		first = 0;
		struct stat st;
		int fo = 0;
		(void)operand_kind(files[k], &o, 1, &st, &fo);
		if (print_entry(&o, files[k], files[k], follow_info_for(&o, 1))) rc = 1;
	}

	for (int k = 0; k < nd; k++) {
		if (!first && jtb_putln("") < 0) rc = 1;
		first = 0;
		if (list_dir(dirs[k], &o, NULL, 0, multi, dirs[k])) rc = 1;
	}

	free(paths);
	free(files);
	free(dirs);
	return rc ? 1 : 0;
}
