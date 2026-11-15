#!/usr/bin/env bash
# Run a plugin on this computer and save a screenshot of its card.
#
#   tools/sim/run.sh plugins/weather.lua
#   tools/sim/run.sh plugins/weather.lua --set city=Lisbon --set unit=F --out lisbon.png
#   tools/sim/run.sh plugins/flappy.lua --seconds 4 --press center@0.5,center@1.1,center@1.7
#
# Needs a C/C++ compiler, curl (for plugins that fetch data) and the LVGL
# source that PlatformIO downloads on the first firmware build
# (.pio/libdeps/...). The first run compiles LVGL, which takes about a minute.

set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/.pio/native-sim"
lvgl="$root/.pio/libdeps/adafruit_feather_esp32s3_reversetft/lvgl"

if [ ! -d "$lvgl/src" ]; then
    echo "LVGL is not downloaded yet. Build the firmware once (pio run), or run: pio pkg install" >&2
    exit 1
fi

CC="${CC:-cc}"
CXX="${CXX:-c++}"
includes=(-I "$root/tools/sim" -I "$lvgl" -I "$lvgl/src" -I "$root/include" -I "$root/include/fonts"
          -I "$root/src" -I "$root/lib/lua/src" -I "$root/lib/plugin_core/src")
cflags=(-O1 -g -w -DLV_CONF_INCLUDE_SIMPLE "${includes[@]}")

# The C sources (LVGL, fonts, Lua) are compiled once; later runs reuse the objects.
export CC
export SIM_CFLAGS="${cflags[*]}"

objects=()
jobs=()
while IFS= read -r source; do
    object="$build/lvgl/${source#"$lvgl/src/"}.o"
    objects+=("$object")
    jobs+=("$source" "$object")
done < <(find "$lvgl/src" -name '*.c' | sort)
for source in "$root"/include/fonts/*.c; do
    object="$build/fonts/$(basename "$source").o"
    objects+=("$object")
    jobs+=("$source" "$object")
done
for source in "$root"/lib/lua/src/*.c; do
    object="$build/lua/$(basename "$source").o"
    objects+=("$object")
    jobs+=("$source" "$object")
done

cpus="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
printf '%s\0' "${jobs[@]}" | xargs -0 -n 2 -P "$cpus" bash -c '
    source="$0"; object="$1"
    if [ ! -f "$object" ] || [ "$source" -nt "$object" ]; then
        mkdir -p "$(dirname "$object")"
        "$CC" -std=c99 $SIM_CFLAGS -c "$source" -o "$object"
    fi'

"$CXX" -std=c++17 -O1 -g -Wall -DLV_CONF_INCLUDE_SIMPLE "${includes[@]}" \
    "$root/tools/sim/sim.cpp" "$root/src/plugins/LvglGfx.cpp" "$root/tools/sim/SimStyle.cpp" \
    "$root"/lib/plugin_core/src/*.cpp "${objects[@]}" -o "$build/sim"

"$build/sim" "$@"
