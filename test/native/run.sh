#!/usr/bin/env bash
# Build and run the plugin runtime tests on this computer. No device needed.
#
#   test/native/run.sh            run everything
#   test/native/run.sh weather    run the tests whose name contains "weather"
#
# Needs a C and C++ compiler (cc / c++).

set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/.pio/native-test"
mkdir -p "$build/lua"

CC="${CC:-cc}"
CXX="${CXX:-c++}"
sanitize="-fsanitize=address,undefined -fno-omit-frame-pointer"

# Lua is C and rarely changes, so its objects are cached.
lua_objects=()
for source in "$root"/lib/lua/src/*.c; do
    object="$build/lua/$(basename "${source%.c}").o"
    if [ ! -f "$object" ] || [ "$source" -nt "$object" ] || [ "$root/lib/lua/src/luaconf.h" -nt "$object" ]; then
        "$CC" -std=c99 -O1 -g -w $sanitize -c "$source" -o "$object"
    fi
    lua_objects+=("$object")
done

"$CXX" -std=c++17 -O1 -g -Wall -Wextra $sanitize \
    -I "$root/lib/lua/src" -I "$root/lib/plugin_core/src" -I "$root/test/native" \
    "$root"/lib/plugin_core/src/*.cpp "$root/test/native/test_plugin_core.cpp" \
    "${lua_objects[@]}" -o "$build/test_plugin_core"

"$build/test_plugin_core" "$root/plugins" ${1:+"$1"}
