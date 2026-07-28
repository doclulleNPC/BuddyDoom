#!/usr/bin/env bash
# Build the SDL3 buddy editor (MyBuddy).  C++17 -- see tools/mybuddy.cpp.
set -eu
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$here/font_atlas.h" ] || python3 "$here/bake_font.py"
# stb_vorbis.c (OGG decode for the sound preview) is compiled as C via -x c ... -x none;
# the rest is C++17.  Its stb_vorbis_decode_memory is declared extern "C" in mybuddy.cpp.
g++ -std=c++17 -O2 -DSDL_MAIN_HANDLED -o "$here/mybuddy" \
    "$here/mybuddy.cpp" \
    "$here/buddydef_wad.cpp" \
    "$here/buddydef_parse.cpp" \
    -x c "$here/../files/stb_vorbis.c" -x none \
    -I"$here" -I"$here/.." $(pkg-config --cflags --libs sdl3)
# place the binary next to the game in run/ (matches the other tools)
mkdir -p "$here/../run"
cp -f "$here/mybuddy" "$here/../run/mybuddy"
echo "built $here/mybuddy  (copied to run/mybuddy)"
