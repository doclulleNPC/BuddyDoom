#!/usr/bin/env python3
# check_gfx_sync.py -- is tools/wad.gfx/ still in sync with the sprites baked into
# run/ID0/buddydoom.wad?
#
# READ-ONLY.  It never writes, renames or deletes anything; it reports and sets an exit
# code, so it can gate a build or just be run by hand before committing.
#
# Why this exists: bake_buddy_voice.py rebuilds buddydoom.wad from scratch out of
# wad.gfx/, so wad.gfx is the source of truth and the WAD is a build product.  Edit a
# sprite inside the WAD (in SLADE, say) and the next bake throws that edit away without a
# word.  That is not hypothetical -- ten MTUR muzzle-flash lumps carried sprite offsets in
# a grAb chunk that their source PNGs did not have, and a re-bake would have stripped every
# one of them.  Same trap in the other direction: a lump with no source file at all simply
# disappears on the next bake.
#
# Reported:
#   DIFFERS   both sides exist and the bytes disagree -- with the grAb offsets of each,
#             because that is usually what actually diverged
#   WAD ONLY  a sprite lump with no source PNG      -> the next bake DROPS it
#   SRC ONLY  a source PNG that is not in the WAD   -> the next bake ADDS it
#   STRAY     a source PNG whose 4-character prefix is not in bake_buddy_voice's
#             GFX_PREFIXES -> the next bake refuses to run at all
#
# One caveat on the obvious fix.  "Just re-bake" is NOT safe in a fresh checkout: the
# ~130 DS* buddy voice clips come from the ElevenLabs pipeline via a cache directory that
# is not in the repo (tools/wad.snd holds only the 16 drone/stalker/dog effects, while the
# WAD carries 228 OGG lumps).  A bake here would drop the rest.  So when this reports a
# placement problem, moving the lump inside S_START..S_END in a WAD editor is usually the
# safer repair than rebuilding.
#
#   python3 tools/check_gfx_sync.py
#   python3 tools/check_gfx_sync.py --wad run/ID0/other.wad
#
# Exit code: 0 in sync, 1 otherwise.
#
import ast
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GFX_DIR = os.path.join(HERE, "wad.gfx")
BAKER = os.path.join(HERE, "bake_buddy_voice.py")
DEFAULT_WAD = os.path.join(ROOT, "run", "ID0", "buddydoom.wad")


def sprite_lumps(path):
    """Name -> bytes for every lump inside S_START..S_END (the bake puts them all there)."""
    with open(path, "rb") as f:
        data = f.read()
    magic, num, off = struct.unpack("<4sii", data[:12])
    out, inside = {}, False
    for i in range(num):
        lo, ls, nm = struct.unpack("<ii8s", data[off + i * 16 : off + i * 16 + 16])
        name = nm.split(b"\x00")[0].decode("latin1").upper()
        if name in ("S_START", "SS_START"):
            inside = True
            continue
        if name in ("S_END", "SS_END"):
            inside = False
            continue
        if inside:
            out[name] = data[lo : lo + ls]
    return out


def source_pngs(d):
    out = {}
    for f in sorted(os.listdir(d)):
        if f.lower().endswith(".png"):
            with open(os.path.join(d, f), "rb") as fh:
                out[os.path.splitext(f)[0].upper()] = fh.read()
    return out


def grab(b):
    """The grAb chunk (leftoffset, topoffset) a sprite needs, or None."""
    i = 8
    while i + 8 <= len(b):
        ln = struct.unpack(">I", b[i : i + 4])[0]
        typ = b[i + 4 : i + 8]
        if typ == b"grAb":
            return struct.unpack(">ii", b[i + 8 : i + 16])
        if typ == b"IDAT":          # grAb must precede the image data
            return None
        i += 12 + ln
    return None


def describe(b):
    g = grab(b)
    return "%d B, grAb=%s" % (len(b), ("%d,%d" % g) if g else "none")


def allowed_prefixes():
    """GFX_PREFIXES out of bake_buddy_voice.py, parsed rather than imported so this stays
    read-only and cannot trip over anything the baker does at import time."""
    try:
        tree = ast.parse(open(BAKER, encoding="utf-8").read())
    except OSError:
        return None
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for t in node.targets:
                if isinstance(t, ast.Name) and t.id == "GFX_PREFIXES":
                    try:
                        return {str(v).upper() for v in ast.literal_eval(node.value)}
                    except ValueError:
                        return None
    return None


def main():
    args = sys.argv[1:]
    wad = DEFAULT_WAD
    if "--wad" in args:
        i = args.index("--wad")
        if i + 1 >= len(args):
            sys.exit("check_gfx_sync: --wad needs a path")
        wad = args[i + 1] if os.path.isabs(args[i + 1]) else os.path.join(ROOT, args[i + 1])
    if not os.path.exists(wad):
        sys.exit("check_gfx_sync: %s not found" % wad)
    if not os.path.isdir(GFX_DIR):
        sys.exit("check_gfx_sync: %s not found" % GFX_DIR)

    src = source_pngs(GFX_DIR)
    lumps = sprite_lumps(wad)
    print("check_gfx_sync: %s  <->  %s (sprite namespace)"
          % (os.path.relpath(GFX_DIR, ROOT), os.path.relpath(wad, ROOT)))
    print("  %d source PNGs, %d sprite lumps" % (len(src), len(lumps)))

    problems = 0

    prefixes = allowed_prefixes()
    if prefixes:
        for name in sorted(src):
            if name[:4] not in prefixes:
                print("  STRAY     %-9s prefix not in GFX_PREFIXES -- the bake will refuse "
                      "to run" % name)
                problems += 1
    else:
        print("  (note: could not read GFX_PREFIXES from bake_buddy_voice.py; "
              "prefix check skipped)")

    for name in sorted(set(src) | set(lumps)):
        if name not in src:
            print("  WAD ONLY  %-9s %s -- the next bake DROPS this"
                  % (name, describe(lumps[name])))
            problems += 1
        elif name not in lumps:
            print("  SRC ONLY  %-9s %s -- the next bake ADDS this"
                  % (name, describe(src[name])))
            problems += 1
        elif src[name] != lumps[name]:
            print("  DIFFERS   %-9s source: %s | lump: %s"
                  % (name, describe(src[name]), describe(lumps[name])))
            problems += 1

    if not problems:
        print("  OK -- every sprite matches its source.")
        return 0
    print("  %d problem(s)." % problems)
    print("  wad.gfx is the source of truth in principle -- but check the header before")
    print("  reaching for bake_buddy_voice.py: the voice clips are not reproducible from")
    print("  this repo, so a full re-bake loses them.  Prefer fixing the PNGs, or moving")
    print("  a misplaced lump inside S_START..S_END in a WAD editor.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
