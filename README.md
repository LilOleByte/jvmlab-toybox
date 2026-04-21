# jvmlab-toybox

A tiny static multicall binary. One file per tool, one entry in a table.
Built for JVMLAB minimal Linux; not a Toybox or BusyBox replacement.

## Applets

| Applet  | What it does                                |
|---------|---------------------------------------------|
| `sh`    | Small interactive shell.                    |
| `ls`    | List a directory (`-a` for dotfiles).       |
| `clear` | Clear the terminal.                         |
| `cat`   | Print files (or stdin) to stdout.           |
| `echo`  | Print arguments (`-n` to skip the newline). |
| `pwd`   | Print the current directory.                |
| `mount` | Call `mount(2)`, or dump `/proc/mounts`.    |

## Build

    make

Hardened by default: `-Os`, stack protector, `_FORTIFY_SOURCE=2`, `--static`,
no-exec stack, dead-code stripping.

## Install

    make install DESTDIR=/tmp/rootfs BINDIR=/bin

Installs the binary and a symlink per applet.

## Shell (v1)

- Quoting: `'...'`, `"..."`, `\x`.
- Comments: `#` to end of line.
- Builtins: `cd`, `exit`, and every applet above.
- External commands via `fork` + `execvp`.
- Only variable: `$?` (last exit status).

Not here yet: pipes, redirection, globbing, `$VAR`, job control. Add them one
at a time.

## Add an applet

1. Write `src/<name>.c` with `int jtb_main_<name>(int, char **)`.
2. Declare it in `src/common.h` and add a row to `jtb_cmds[]` in `src/main.c`.
3. Add `<name>` to `APPLETS` in the `Makefile`.

Keep it short. Bounded buffers. No recursion in parsers.

## License

[BSD Zero Clause License (0BSD)](LICENSE). SPDX: `0BSD`.
