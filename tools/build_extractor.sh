#!/usr/bin/env bash
# Build the SDL3 asset extractor for BuddyDoom (tools/extractor.c).
# Replaces the extract_heretic_monsters.py / extract_hexen.py /
# extract_freedoom2.py scripts with a GUI: pick a source IWAD, hit Extract.
set -eu
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$here/font_atlas.h" ] || python3 "$here/bake_font.py"
# wadpng.c/wadcodes.c: shared *stuff.wad PNG conversion + sprite-name policy
# (mirrored by tools/wadpng.py / wadcodes.py).  miniz.c supplies the deflate.
gcc -O2 -DSDL_MAIN_HANDLED -o "$here/extractor" \
    "$here/extractor.c" "$here/wadpng.c" "$here/wadcodes.c" "$here/../files/miniz.c" \
    -I"$here" -I"$here/../files" $(pkg-config --cflags --libs sdl3) -lm
# place the binary next to the game in run/
mkdir -p "$here/../run"
cp -f "$here/extractor" "$here/../run/extractor"
echo "built $here/extractor  (copied to run/extractor)"
