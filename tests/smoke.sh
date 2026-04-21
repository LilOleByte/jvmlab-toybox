#!/bin/sh
# tests/smoke.sh - jvmlab-toybox smoke test harness.
#
# Usage:
#   BIN=/path/to/jvmlab-toybox sh tests/smoke.sh
#
# Contract:
#   - One "PASS <name>", "FAIL <name>: <reason>", or "SKIP <name>: <reason>"
#     line per check, followed by a summary and an exit code equal to the
#     number of failures.
#   - POSIX sh only; no bashisms. Hermetic: fresh mktemp workdir, LC_ALL=C,
#     TZ=UTC, scrubbed PATH, HOME inside the workdir.

set -u

: "${BIN:?BIN must point at the built jvmlab-toybox binary}"
if [ ! -x "$BIN" ]; then
	echo "smoke: BIN not executable: $BIN" >&2
	exit 2
fi

# Resolve BIN to an absolute path so we can cd freely inside tests.
case "$BIN" in
	/*) ;;
	*)  BIN="$PWD/$BIN" ;;
esac

export LC_ALL=C
export TZ=UTC

WORK=$(mktemp -d -t jtb-smoke.XXXXXX) || { echo "mktemp failed" >&2; exit 2; }
trap 'rm -rf "$WORK"' EXIT INT HUP TERM

mkdir -p "$WORK/bin" "$WORK/home"
HOME="$WORK/home"; export HOME
PATH="$WORK/bin:/usr/bin:/bin"; export PATH

APPLETS="sh ls clear cat echo pwd mount"
for a in $APPLETS; do
	ln -s "$BIN" "$WORK/bin/$a"
done

pass=0
fail=0
skipped=0
REASON=

run() {
	"$BIN" "$@" >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
}

rc()  { cat "$WORK/rc"; }
outc() { cat "$WORK/out"; }

check() {
	_name=$1; shift
	REASON=
	if "$@"; then
		echo "PASS $_name"
		pass=$((pass + 1))
	else
		[ -n "$REASON" ] || REASON="(no reason)"
		echo "FAIL $_name: $REASON"
		fail=$((fail + 1))
	fi
}

skip() {
	echo "SKIP $1: $2"
	skipped=$((skipped + 1))
}

# ---------- assertion helpers ----------

assert_rc() {
	if [ "$(rc)" != "$1" ]; then
		REASON="rc=$(rc) want=$1"
		return 1
	fi
}

assert_out_eq() {
	printf '%s' "$1" >"$WORK/exp"
	if ! cmp -s "$WORK/exp" "$WORK/out"; then
		REASON="stdout mismatch; got=$(od -An -c "$WORK/out" | tr -s ' ' | head -c 120)"
		return 1
	fi
}

assert_out_file() {
	if ! cmp -s "$1" "$WORK/out"; then
		REASON="stdout differs from $1"
		return 1
	fi
}

assert_err_has() {
	if ! grep -qF -- "$1" "$WORK/err"; then
		REASON="stderr missing literal: $1"
		return 1
	fi
}

assert_out_has() {
	if ! grep -qF -- "$1" "$WORK/out"; then
		REASON="stdout missing literal: $1"
		return 1
	fi
}

# ============================================================
# Dispatcher - src/main.c
# ============================================================

t_dispatch_list() {
	run
	assert_rc 0 || return 1
	grep -q '^jvmlab-toybox 0\.1\.0$' "$WORK/out" \
		|| { REASON="no version header in list output"; return 1; }
	for _a in $APPLETS; do
		grep -qw -- "$_a" "$WORK/out" \
			|| { REASON="applet missing from list: $_a"; return 1; }
	done
}

t_dispatch_version() {
	run --version
	assert_rc 0 || return 1
	assert_out_eq 'jvmlab-toybox 0.1.0
'
}

t_dispatch_help() {
	run --help
	assert_rc 0 || return 1
	grep -q '^jvmlab-toybox 0\.1\.0$' "$WORK/out" \
		|| { REASON="--help did not print list"; return 1; }
	grep -qw 'sh' "$WORK/out" \
		|| { REASON="--help did not list applets"; return 1; }
}

t_dispatch_unknown() {
	run nope-applet
	assert_rc 127 || return 1
	assert_err_has 'unknown applet: nope-applet'
}

t_dispatch_symlink() {
	"$WORK/bin/echo" hi >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	assert_out_eq 'hi
'
}

check dispatch/list     t_dispatch_list
check dispatch/version  t_dispatch_version
check dispatch/help     t_dispatch_help
check dispatch/unknown  t_dispatch_unknown
check dispatch/symlink  t_dispatch_symlink

# ============================================================
# echo - src/echo.c
# ============================================================

t_echo_basic() {
	run echo a b
	assert_rc 0 || return 1
	assert_out_eq 'a b
'
}

t_echo_n() {
	run echo -n hi
	assert_rc 0 || return 1
	assert_out_eq 'hi'
}

t_echo_empty() {
	run echo
	assert_rc 0 || return 1
	assert_out_eq '
' || return 1
	run echo -n
	assert_rc 0 || return 1
	assert_out_eq ''
}

t_echo_no_escapes() {
	run echo 'a\tb'
	assert_rc 0 || return 1
	# Expect the 4-char token plus newline, backslash-t left literal.
	printf 'a\\tb\n' >"$WORK/exp"
	if ! cmp -s "$WORK/exp" "$WORK/out"; then
		REASON="echo interpreted escapes"
		return 1
	fi
}

check echo/basic      t_echo_basic
check echo/n-flag     t_echo_n
check echo/empty      t_echo_empty
check echo/no-escapes t_echo_no_escapes

# ============================================================
# pwd - src/pwd.c
# ============================================================

t_pwd_matches() {
	mkdir -p "$WORK/sub"
	_want=$(cd "$WORK/sub" && pwd -P)
	(cd "$WORK/sub" && "$BIN" pwd) >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	printf '%s\n' "$_want" >"$WORK/exp"
	if ! cmp -s "$WORK/exp" "$WORK/out"; then
		REASON="pwd=$(outc) want=$_want"
		return 1
	fi
}

check pwd/matches t_pwd_matches

# ============================================================
# clear - src/clear.c
# ============================================================

t_clear_bytes() {
	run clear
	assert_rc 0 || return 1
	# Exact 11 bytes: ESC [ H ESC [ 2 J ESC [ 3 J
	printf '\033[H\033[2J\033[3J' >"$WORK/exp"
	if ! cmp -s "$WORK/exp" "$WORK/out"; then
		REASON="clear bytes mismatch (size=$(wc -c <"$WORK/out"))"
		return 1
	fi
}

check clear/bytes t_clear_bytes

# ============================================================
# cat - src/cat.c
# ============================================================

# Fixture files with NULs and high bytes.
printf 'hello\0world\377\n' >"$WORK/f1"
printf 'second\0file\376\n' >"$WORK/f2"

t_cat_stdin() {
	printf foo | "$BIN" cat >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	assert_out_eq 'foo'
}

t_cat_file() {
	"$BIN" cat "$WORK/f1" >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	assert_out_file "$WORK/f1"
}

t_cat_multi() {
	"$BIN" cat "$WORK/f1" "$WORK/f2" >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	cat "$WORK/f1" "$WORK/f2" >"$WORK/exp"
	if ! cmp -s "$WORK/exp" "$WORK/out"; then
		REASON="cat multi output differs"
		return 1
	fi
}

t_cat_missing() {
	run cat /nope-path-xyz
	# The applet returns 1 on any error (see cat.c:53).
	if [ "$(rc)" = 0 ]; then
		REASON="cat /nope exited 0"
		return 1
	fi
	assert_err_has '/nope-path-xyz'
}

check cat/stdin   t_cat_stdin
check cat/file    t_cat_file
check cat/multi   t_cat_multi
check cat/missing t_cat_missing

# ============================================================
# mount - src/mount.c
# ============================================================

t_mount_dump() {
	if [ ! -r /proc/mounts ]; then
		REASON="/proc/mounts not readable"
		return 1
	fi
	cat /proc/mounts >"$WORK/pm1"
	"$BIN" mount >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	if cmp -s "$WORK/out" "$WORK/pm1"; then
		return 0
	fi
	# One retry, accepting either snapshot around the call.
	cat /proc/mounts >"$WORK/pm2"
	"$BIN" mount >"$WORK/out" 2>"$WORK/err"
	cat /proc/mounts >"$WORK/pm3"
	if cmp -s "$WORK/out" "$WORK/pm2" || cmp -s "$WORK/out" "$WORK/pm3"; then
		return 0
	fi
	REASON="mount output does not match /proc/mounts snapshots"
	return 1
}

t_mount_usage() {
	run mount a b
	assert_rc 2 || return 1
	assert_err_has 'usage: mount'
}

t_mount_enoent() {
	run mount /nope-src /nope-tgt-xyz ext4
	assert_rc 1 || return 1
	assert_err_has '/nope-tgt-xyz'
}

t_mount_ebusy_idempotent() {
	mkdir -p "$WORK/m"
	"$BIN" mount tmpfs "$WORK/m" tmpfs >"$WORK/out1" 2>"$WORK/err1"
	_r1=$?
	"$BIN" mount tmpfs "$WORK/m" tmpfs >"$WORK/out2" 2>"$WORK/err2"
	_r2=$?
	# Cleanup before asserting.
	if command -v umount >/dev/null 2>&1; then
		umount "$WORK/m" 2>/dev/null || true
	fi
	[ "$_r1" = 0 ] || { REASON="first mount rc=$_r1"; return 1; }
	[ "$_r2" = 0 ] || { REASON="second mount rc=$_r2 (EBUSY not treated as success)"; return 1; }
}

check mount/dump   t_mount_dump
check mount/usage  t_mount_usage
check mount/enoent t_mount_enoent

# Probe whether the mount(2) syscall is actually available to us; bare uid=0
# is not enough inside unprivileged user namespaces / sandboxes that lack
# CAP_SYS_ADMIN in the init namespace.
mkdir -p "$WORK/_probe"
if "$BIN" mount tmpfs "$WORK/_probe" tmpfs >/dev/null 2>&1; then
	umount "$WORK/_probe" 2>/dev/null || true
	check mount/ebusy-idempotent t_mount_ebusy_idempotent
else
	skip mount/ebusy-idempotent 'mount(2) not permitted here'
fi
rmdir "$WORK/_probe" 2>/dev/null || true

# ============================================================
# sh - src/sh.c
# ============================================================
#
# The applet sh is single-command-per-line (no `;`, no `&&`, no redirection).
# For the few cases in the plan that need a second command (e.g. $? after a
# failed cd, cd-then-pwd), we feed two lines on stdin rather than using `;`.

t_sh_c_echo() {
	run sh -c 'echo hi'
	assert_rc 0 || return 1
	assert_out_eq 'hi
'
}

t_sh_single_quote() {
	run sh -c "echo 'a  b'"
	assert_rc 0 || return 1
	assert_out_eq 'a  b
'
}

t_sh_double_escape() {
	run sh -c 'echo "a\"b"'
	assert_rc 0 || return 1
	assert_out_eq 'a"b
'
}

t_sh_comment() {
	run sh -c 'echo a # rest'
	assert_rc 0 || return 1
	assert_out_eq 'a
'
}

t_sh_dollar_q() {
	# Base case: fresh shell, $? must be 0.
	run sh -c 'echo $?'
	assert_rc 0 || return 1
	assert_out_eq '0
' || return 1
	# Status propagation: failed cd on line 1, echo $? on line 2.
	printf 'cd /nope-xyz\necho $?\n' | "$BIN" sh >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	# The outer sh returns the last command's status (echo=0).
	assert_rc 0 || return 1
	assert_out_eq '1
'
}

t_sh_unterminated() {
	run sh -c "echo 'x"
	assert_rc 2 || return 1
	assert_err_has 'unterminated quote'
}

t_sh_exit_code() {
	run sh -c 'exit 42'
	assert_rc 42
}

t_sh_cd_builtin() {
	printf 'cd /\npwd\n' | "$BIN" sh >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	assert_out_eq '/
'
}

t_sh_enoent() {
	run sh -c '/does/not/exist/xyz'
	assert_rc 127
}

check sh/c-echo        t_sh_c_echo
check sh/single-quote  t_sh_single_quote
check sh/double-escape t_sh_double_escape
check sh/comment       t_sh_comment
check sh/dollar-q      t_sh_dollar_q
check sh/unterminated  t_sh_unterminated
check sh/exit-code     t_sh_exit_code
check sh/cd-builtin    t_sh_cd_builtin
check sh/enoent        t_sh_enoent

# ============================================================
# ls - src/ls.c
# ============================================================

# Fixture: tree/a/file, tree/b/c (dir), tree/.hidden, tree/sym -> a/file
mkdir -p "$WORK/tree/a" "$WORK/tree/b/c"
printf 'x\n' >"$WORK/tree/a/file"
printf 'y\n' >"$WORK/tree/.hidden"
ln -s a/file "$WORK/tree/sym"

# Run ls from inside tree/ so displayed names are clean.
ls_in_tree() {
	(cd "$WORK/tree" && "$BIN" ls "$@") >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
}

t_ls_default() {
	ls_in_tree
	assert_rc 0 || return 1
	# Default hides dotfiles; ls is one-per-line, strcmp-sorted.
	assert_out_eq 'a
b
sym
'
}

t_ls_a() {
	ls_in_tree -a
	assert_rc 0 || return 1
	assert_out_eq '.
..
.hidden
a
b
sym
'
}

t_ls_A() {
	ls_in_tree -A
	assert_rc 0 || return 1
	assert_out_eq '.hidden
a
b
sym
'
}

t_ls_d() {
	(cd "$WORK" && "$BIN" ls -d tree) >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	assert_out_eq 'tree
'
}

t_ls_l_mode() {
	_want=$(stat -c %A "$WORK/tree/a/file")
	ls_in_tree -l a/file
	assert_rc 0 || return 1
	_got=$(awk 'NR==1{print $1; exit}' "$WORK/out")
	if [ "$_got" != "$_want" ]; then
		REASON="mode=$_got want=$_want"
		return 1
	fi
}

t_ls_l_symlink() {
	ls_in_tree -l sym
	assert_rc 0 || return 1
	if ! grep -q 'sym -> a/file$' "$WORK/out"; then
		REASON="no 'sym -> a/file' suffix in -l output"
		return 1
	fi
}

t_ls_F() {
	ls_in_tree -F
	assert_rc 0 || return 1
	assert_out_eq 'a/
b/
sym@
'
}

t_ls_R() {
	# Run from the parent of tree so we can inspect -R descent cleanly.
	(cd "$WORK" && "$BIN" ls -R tree) >"$WORK/out" 2>"$WORK/err"
	echo $? >"$WORK/rc"
	assert_rc 0 || return 1
	# This applet's -R uses basename-only subdir headers (a:, b:, c:) and
	# does not emit a header for the root arg. Assert that every subdir
	# was descended into and its contents printed.
	for _h in 'a:' 'b:' 'c:'; do
		grep -qxF -- "$_h" "$WORK/out" \
			|| { REASON="missing -R header: $_h"; return 1; }
	done
	# And the leaf file inside a/ is listed under its header.
	grep -qxF 'file' "$WORK/out" \
		|| { REASON="-R did not list tree/a/file"; return 1; }
}

t_ls_r_sort() {
	ls_in_tree -r
	assert_rc 0 || return 1
	assert_out_eq 'sym
b
a
'
}

t_ls_i() {
	# The applet emits the inode column only in long format (see emit_long()
	# in src/ls.c), so smoke-tests -i in combination with -l.
	_want=$(stat -c %i "$WORK/tree/a/file")
	ls_in_tree -il a/file
	assert_rc 0 || return 1
	_got=$(awk 'NR==1{print $1; exit}' "$WORK/out")
	case "$_got" in
		''|*[!0-9]*) REASON="not integer: '$_got'"; return 1 ;;
	esac
	if [ "$_got" != "$_want" ]; then
		REASON="ino=$_got want=$_want"
		return 1
	fi
}

t_ls_loop_L() {
	ln -s . "$WORK/tree/self"
	if command -v timeout >/dev/null 2>&1; then
		timeout 5 "$BIN" ls -RL "$WORK/tree" >"$WORK/out" 2>"$WORK/err"
		_r=$?
	else
		("$BIN" ls -RL "$WORK/tree" >"$WORK/out" 2>"$WORK/err") &
		_pid=$!
		_i=0
		while [ "$_i" -lt 5 ] && kill -0 "$_pid" 2>/dev/null; do
			sleep 1
			_i=$((_i + 1))
		done
		if kill -0 "$_pid" 2>/dev/null; then
			kill -9 "$_pid" 2>/dev/null
			wait "$_pid" 2>/dev/null
			rm -f "$WORK/tree/self"
			REASON="ls -RL hung (loop not detected)"
			return 1
		fi
		wait "$_pid"; _r=$?
	fi
	rm -f "$WORK/tree/self"
	# 124 is timeout-killed (hung); anything else means it terminated.
	if [ "$_r" = 124 ]; then
		REASON="ls -RL timed out (loop not detected)"
		return 1
	fi
	# ls can legitimately exit non-zero on per-entry errors; loop test only
	# cares that it terminated.
	return 0
}

t_ls_missing() {
	run ls /nope-ls-xyz
	if [ "$(rc)" = 0 ]; then
		REASON="ls /nope exited 0"
		return 1
	fi
	assert_err_has '/nope-ls-xyz'
}

check ls/default    t_ls_default
check ls/-a         t_ls_a
check ls/-A         t_ls_A
check ls/-d         t_ls_d
check ls/-l-mode    t_ls_l_mode
check ls/-l-symlink t_ls_l_symlink
check ls/-F         t_ls_F
check ls/-R         t_ls_R
check ls/-r-sort    t_ls_r_sort
check ls/-i         t_ls_i
check ls/loop-L     t_ls_loop_L
check ls/missing    t_ls_missing

# ============================================================
# Summary
# ============================================================

echo '---'
echo "summary: $pass passed, $fail failed, $skipped skipped"
exit "$fail"
