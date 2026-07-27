#!/usr/bin/env bash
#
# build_mybuddy_win.sh -- cross-build MyBuddy (BUDDYDEF editor) for Windows
# with MinGW-w64.  Point SDL3 at a MinGW SDL3 dev package (same as
# build_config_win.sh):
#
#     SDL3=/path/to/SDL3-devel-3.x.y-mingw/x86_64-w64-mingw32 ./tools/build_mybuddy_win.sh
#
set -eu

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${CXX:=x86_64-w64-mingw32-g++}"
: "${SDL3:?Set SDL3 to your MinGW SDL3 dir (contains include/ lib/ bin/)}"

command -v "$CXX" >/dev/null 2>&1 || { echo "[build] $CXX not found" >&2; exit 1; }
[ -f "$SDL3/include/SDL3/SDL.h" ] || { echo "[build] SDL3 headers not at $SDL3/include" >&2; exit 1; }
[ -f "$here/font_atlas.h" ] || python3 "$here/bake_font.py"

# -mwindows: GUI subsystem (no console window). SDL_MAIN_HANDLED: we own main().
# File dialogs are SDL3's native SDL_ShowOpen/SaveFileDialog -- no comdlg32 needed.
"$CXX" -O2 -DSDL_MAIN_HANDLED -I"$here" -I"$here/.." -I"$SDL3/include" \
    "$here/mybuddy.cpp" \
    "$here/buddydef_wad.cpp" \
    "$here/buddydef_parse.cpp" \
    -L"$SDL3/lib" -lSDL3 -mwindows -static-libgcc \
    -o "$here/mybuddy.exe"

mkdir -p "$here/../run"
cp -f "$here/mybuddy.exe" "$here/../run/mybuddy.exe"
[ -f "$SDL3/bin/SDL3.dll" ] && cp -f "$SDL3/bin/SDL3.dll" "$here/../run/SDL3.dll" || true
echo "built $here/mybuddy.exe (copied to run/mybuddy.exe)"
