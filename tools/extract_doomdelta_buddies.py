#!/usr/bin/env python3
"""Extract the four Doom Delta player classes into BuddyDoom buddy assets.

Doom Delta (https://www.doomworld.com/ -- DoomDelta-v3.1.0.pk3) rebuilds the
1992 DOOM Bible cast as four GZDoom player classes.  This script lifts their
sprites and voice clips out of the .pk3 and stages them where
`tools/bake_buddy_voice.py` expects them:

    sprites/players/<class>/PLA<n>*.png  ->  tools/wad.gfx/PLA<n>*.png
    sounds/chars/DS???{DIE,PAI,OOF}.wav  ->  tools/wad.snd/DS???*.ogg

Idempotent: it rewrites exactly the files it owns and touches nothing else in
those directories, so re-running after a Doom Delta update is safe.

THE FRAME REMAP (the only non-obvious part)
-------------------------------------------
Doom Delta authors its player sheets for GZDoom's own state table, which is not
the 1993 layout the BuddyDoom renderer walks.  Buddies are player 2, so their
art is driven by `S_PLAY*` in files/info.c, which needs frames A..W:

    A-D walk   E attack   F attack2   G pain   H-N death (7)   O-W gibs (9)

Doom Delta ships A..U instead:

    A-D walk   E missile  F melee     G pain   H-L death (5)   M-U gibs (9)

So death is two frames short and the gib run sits two letters early.  Feeding
the sheet in as-is puts gib art in the middle of the death animation and leaves
V/W missing, where `R_ProjectSprite`'s bounds check (r_things.c) silently falls
back to the stock marine mid-corpse.  We therefore re-letter on the way out:

    A..G -> A..G     (unchanged, all 8 rotations)
    H..L -> H..L     death, verbatim
    L    -> M, N     death held two extra frames to fill the 7-frame run
    M..U -> O..W     gibs, shifted +2

Result is a complete 23-frame vanilla player sheet per class.
"""

import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

PK3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    "/home/dulli/Source/DoomDelta-v3.1.0.pk3")

TOOLS = Path(__file__).resolve().parent
GFX_DIR = TOOLS / "wad.gfx"
SND_DIR = TOOLS / "wad.snd"

# (pk3 sprite folder, 4-char sprite base, sound-lump stem)
CLASSES = [
    ("lorelei", "PLA1", "LOR"),
    ("john",    "PLA2", "JON"),
    ("dimitri", "PLA3", "DIM"),
    ("thi",     "PLA4", "THI"),
]

# Doom Delta frame letter -> the vanilla S_PLAY frame letter(s) it becomes.
FRAME_MAP = {
    "A": ["A"], "B": ["B"], "C": ["C"], "D": ["D"],   # walk
    "E": ["E"], "F": ["F"], "G": ["G"],               # attack, attack2, pain
    "H": ["H"], "I": ["I"], "J": ["J"], "K": ["K"],   # death 1-4
    "L": ["L", "M", "N"],                             # death 5, held for 6 and 7
    "M": ["O"], "N": ["P"], "O": ["Q"], "P": ["R"],   # gibs 1-4
    "Q": ["S"], "R": ["T"], "S": ["U"], "T": ["V"],   # gibs 5-8
    "U": ["W"],                                       # gibs 9
}


def png_has_grab(data):
    """True if the PNG carries a grAb chunk (sprite offsets)."""
    i = 8
    while i + 8 <= len(data):
        ln = struct.unpack(">I", data[i:i + 4])[0]
        typ = data[i + 4:i + 8]
        if typ == b"grAb":
            return True
        if typ == b"IDAT":
            return False
        i += 12 + ln
    return False


def extract_sprites(zf):
    """Copy every class sheet into wad.gfx under vanilla player frame letters."""
    GFX_DIR.mkdir(parents=True, exist_ok=True)
    names = zf.namelist()
    total, nograb = 0, []

    for folder, base, _snd in CLASSES:
        # wipe anything we previously wrote for this base, so a re-run after a
        # Doom Delta update cannot leave orphaned frames behind
        for old in GFX_DIR.glob(f"{base}*.png"):
            old.unlink()

        src = [n for n in names
               if n.startswith(f"sprites/players/{folder}/") and n.lower().endswith(".png")]
        if not src:
            sys.exit(f"extract_doomdelta_buddies: no sprites for '{folder}' in {PK3}")

        written = 0
        for name in sorted(src):
            stem = Path(name).stem.upper()          # e.g. PLA1A2A8 or PLA1H0
            if not stem.startswith(base):
                continue
            rest = stem[len(base):]                 # A2A8 / H0
            frame, rot = rest[0], rest[1:]
            if frame not in FRAME_MAP:
                sys.exit(f"extract_doomdelta_buddies: {name} has unmapped frame '{frame}'")

            data = zf.read(name)
            if not png_has_grab(data):
                nograb.append(stem)

            for dst_frame in FRAME_MAP[frame]:
                # `rot` keeps the mirrored-pair form (2A8 -> becomes <newframe>2<newframe>8)
                if len(rot) == 3 and rot[1] == frame:
                    dst_rot = rot[0] + dst_frame + rot[2]
                else:
                    dst_rot = rot
                out = GFX_DIR / f"{base}{dst_frame}{dst_rot}.png"
                if len(out.stem) > 8:
                    sys.exit(f"extract_doomdelta_buddies: lump name '{out.stem}' >8 chars")
                out.write_bytes(data)
                written += 1

        print(f"  {base} ({folder:8s}) -> {written:3d} sprite lumps")
        total += written

    if nograb:
        print(f"  ! {len(nograb)} source PNG(s) carry no grAb chunk; "
              f"those frames will draw at offset 0,0: {', '.join(sorted(set(nograb))[:6])}")
    return total


def extract_sounds(zf):
    """Transcode the per-class die/pain/oof clips to OGG in wad.snd."""
    SND_DIR.mkdir(parents=True, exist_ok=True)
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        sys.exit("extract_doomdelta_buddies: ffmpeg not found (needed to transcode WAV -> OGG)")

    written = 0
    for _folder, _base, snd in CLASSES:
        for kind in ("DIE", "PAI", "OOF"):
            lump = f"DS{snd}{kind}"                 # exactly 8 chars
            src = f"sounds/chars/{lump}.wav"
            if src not in zf.namelist():
                sys.exit(f"extract_doomdelta_buddies: {src} missing from {PK3}")
            out = SND_DIR / f"{lump}.ogg"
            proc = subprocess.run(
                [ffmpeg, "-y", "-loglevel", "error", "-i", "pipe:0",
                 "-ac", "1", "-ar", "11025", "-c:a", "libvorbis", "-q:a", "3",
                 "-f", "ogg", str(out)],
                input=zf.read(src), capture_output=True)
            if proc.returncode != 0:
                sys.exit(f"extract_doomdelta_buddies: ffmpeg failed on {src}:\n"
                         f"{proc.stderr.decode('utf-8', 'replace')}")
            written += 1
    print(f"  {written} sound lumps -> wad.snd (11025 Hz mono OGG)")
    return written


def main():
    if not PK3.is_file():
        sys.exit(f"extract_doomdelta_buddies: {PK3} not found\n"
                 f"usage: {Path(__file__).name} [path/to/DoomDelta-vX.Y.Z.pk3]")
    print(f"extract_doomdelta_buddies: reading {PK3}")
    with zipfile.ZipFile(PK3) as zf:
        nspr = extract_sprites(zf)
        nsnd = extract_sounds(zf)
    print(f"\nStaged {nspr} sprites + {nsnd} sounds. "
          f"Now run tools/bake_buddy_voice.py to rebuild run/ID0/buddydoom.wad.")


if __name__ == "__main__":
    main()
