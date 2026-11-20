# Lua 5.4.8 (vendored)

Source from https://www.lua.org/ftp/lua-5.4.8.tar.gz, MIT licence (see the end of `src/lua.h`).

Left out on purpose: the standalone interpreter and compiler (`lua.c`, `luac.c`) and the
libraries plugins must not reach (`liolib.c`, `loslib.c`, `loadlib.c`, `ldblib.c`, `linit.c`).

Local changes, all in `src/luaconf.h`:

- `LUA_32BITS 1`: integers are 32-bit and floats are single precision.
- `LUAI_MAXCCALLS 40`: caps C-level recursion so a plugin cannot overflow the UI task stack
  (32 KB, set in `src/main.cpp`; raise the two together).
- `MAXCCALLS 80`: the matching limit for string patterns, for the same reason.
