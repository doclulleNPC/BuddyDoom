#!/usr/bin/env bash
# Build the SDL3 BUDDYDEF editor (mybuddy).
set -eu
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$here/font_atlas.h" ] || python3 "$here/bake_font.py"
gcc -O2 -DSDL_MAIN_HANDLED -o "$here/mybuddy" \
    "$here/mybuddy.c" \
    "$here/buddydef_wad.c" \
    "$here/buddydef_parse.c" \
    -I"$here" -I"$here/.." $(pkg-config --cflags --libs sdl3)
# place the binary next to the game in run/ (matches the other tools)
mkdir -p "$here/../run"
cp -f "$here/mybuddy" "$here/../run/mybuddy"
echo "built $here/mybuddy  (copied to run/mybuddy)"
