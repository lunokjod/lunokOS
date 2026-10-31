# Lua 5.4.7 (vendored)

The sources are in `src/`, taken from `lua-5.4.7.tar.gz` on lua.org.

## Why they are in the repository

PlatformIO can fetch a dependency, but Lua needs one edit before it will do what this project
needs, and a dependency that is patched at build time is a dependency that breaks quietly. The
sources are here, they are compiled with the same flags on both bands, and the patched line is
written down below so it can be found.

## What is compiled

Core: `lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate
lstring ltable ltm lundump lvm lzio`.

Libraries: `lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib`.

Not compiled, and replaced by our own code:

| File | Why |
|---|---|
| `lua.c`, `luac.c` | They are the stand-alone interpreter and compiler |
| `liolib.c` | `io` has to talk to the VFS, not to a process |
| `loslib.c` | `os` has to talk to the kernel clock, not to the host |
| `loadlib.c` | no C modules on a board |
| `ldblib.c` | only `traceback` is wanted, and that comes from `lauxlib` |
| `linit.c` | it opens the libraries we just removed |

## The one edit to upstream

`src/luaconf.h`:

```c
#define LUA_INT_DEFAULT     LUA_INT_LONGLONG
#define LUA_FLOAT_DEFAULT   LUA_FLOAT_FLOAT /* lunokOS: 32-bit floats, see docs/20 */
```

Integers stay 64-bit (Unix seconds do not fit in 32 bits) and floats become 32-bit (half the
memory, enough for the numbers this system handles). These two cannot be set from the command
line: in 5.4.7 `luaconf.h` defines `LUA_INT_TYPE` and `LUA_FLOAT_TYPE` unconditionally, so a `-D`
is simply overwritten. That is the whole reason the sources are vendored.

## What to check when the version changes

Nothing in the plan needs a patch beyond that line, but a new release has to be checked for
`LUAI_MAXSTACK` (the stack ceiling, which we may want to lower) and for the exact list of files
above.
