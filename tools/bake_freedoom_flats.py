#!/usr/bin/env python3
# bake_freedoom_flats.py -- copy the animated NUKAGE flats and the *FALL wall patches
# out of a Freedoom IWAD into run/ID0/buddydoom.wad, IN PLACE and idempotently.
#
# Why: e1-arenas.wad's maps floor 179 sectors with NUKAGE1/NUKAGE3, but those flats only
# exist in the commercial IWADs -- and BuddyDoom must not ship or modify those.  Freedoom
# is BSD-licensed and freely redistributable, its PLAYPAL is byte-identical to DOOM.WAD's
# (verified by this script at bake time), and its flats are already raw 64x64 lumps.  So
# they can be copied verbatim: no palette remap, no format conversion.
#
# What goes where matters more than the bytes:
#   * NUKAGE1-3  are FLATS   -> must land inside F_START..F_END, or R_InitFlats
#     (files/r_data.c) never sees them and the floors fall back to flat 0.
#   * SFALL1-4   are PATCHES -> must land inside P_START..P_END.  Note these only become
#     a usable *texture* if some TEXTURE1 defines one from them; on a DOOM.WAD game none
#     does, so they are carried for map packs that bring their own TEXTURE1.  No map in
#     e1-arenas.wad references them today.
#
# Run AFTER bake_buddy_voice.py -- that one rewrites buddydoom.wad from scratch, so every
# other bake (this, bake_secdrone.py) has to be re-applied on top of it.
#
#   python3 tools/bake_freedoom_flats.py
#
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WAD  = os.path.join(ROOT, "run", "ID0", "buddydoom.wad")

# (lump, namespace).  "F" = flat namespace, "P" = patch namespace.
WANT = [("NUKAGE1", "F"), ("NUKAGE2", "F"), ("NUKAGE3", "F"),
        ("SFALL1",  "P"), ("SFALL2",  "P"), ("SFALL3",  "P"), ("SFALL4",  "P")]

FREEDOOM = ["freedoom1.wad", "freedoom2.wad", "freedm.wad",
            "FREEDOOM1.WAD", "FREEDOOM2.WAD", "FREEDM.WAD"]
# Only used to confirm the palettes match; never written to.
COMMERCIAL = ["DOOM.WAD", "DOOM2.WAD", "doom.wad", "doom2.wad"]


def read_lumps(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, num, off = struct.unpack("<4sii", data[:12])
    lumps = []
    for i in range(num):
        lo, ls, nm = struct.unpack("<ii8s", data[off + i*16 : off + i*16 + 16])
        lumps.append([nm.split(b"\x00")[0].decode("latin1"), data[lo:lo+ls]])
    return magic, lumps


def write_wad(path, magic, lumps):
    off = 12
    body = bytearray()
    dirent = bytearray()
    for name, data in lumps:
        dirent += struct.pack("<ii8s", off + len(body), len(data),
                              name.encode("latin1")[:8].ljust(8, b"\x00"))
        body += data
    with open(path, "wb") as f:
        f.write(struct.pack("<4sii", magic, len(lumps), 12 + len(body)))
        f.write(body)
        f.write(dirent)


def find(paths):
    for p in paths:
        fp = os.path.join(ROOT, "run", "ID0", p)
        if os.path.exists(fp):
            return fp
    return None


def lump_of(lumps, name):
    for n, d in lumps:
        if n.upper() == name.upper():
            return d
    return None


def section_bounds(lumps, kind):
    """Index range (start_marker, end_marker) of the FIRST kind_START..kind_END pair."""
    s = e = None
    for i, (n, _) in enumerate(lumps):
        u = n.upper()
        if s is None and u == kind + "_START":
            s = i
        elif s is not None and u == kind + "_END":
            e = i
            break
    return s, e


def main():
    if not os.path.exists(WAD):
        sys.exit("bake_freedoom_flats: %s not found -- run bake_buddy_voice.py first" % WAD)

    src = find(FREEDOOM)
    if not src:
        sys.exit("bake_freedoom_flats: no Freedoom IWAD under run/ID0/ "
                 "(freedoom1.wad / freedoom2.wad / freedm.wad)")

    _, srclumps = read_lumps(src)

    # Copying raw palette indices is only safe while the palettes agree.  They do today,
    # but check rather than assume -- a silent mismatch would just look like wrong colours.
    com = find(COMMERCIAL)
    if com:
        a = lump_of(read_lumps(com)[1], "PLAYPAL")
        b = lump_of(srclumps, "PLAYPAL")
        if a and b and a[:768] != b[:768]:
            sys.exit("bake_freedoom_flats: %s and %s disagree on PLAYPAL; a raw copy would "
                     "shift every colour. Remap the indices before using this." %
                     (os.path.basename(com), os.path.basename(src)))

    grabbed = []
    for name, kind in WANT:
        d = lump_of(srclumps, name)
        if d is None:
            sys.exit("bake_freedoom_flats: %s missing from %s" % (name, os.path.basename(src)))
        if kind == "F" and len(d) != 4096:
            sys.exit("bake_freedoom_flats: %s is %d bytes; a flat must be 64x64 = 4096"
                     % (name, len(d)))
        grabbed.append((name, kind, d))

    magic, lumps = read_lumps(WAD)
    names = {n.upper(): i for i, (n, _) in enumerate(lumps)}

    updated = 0
    pending = {"F": [], "P": []}
    for name, kind, d in grabbed:
        i = names.get(name.upper())
        if i is not None:
            lumps[i][1] = d          # already present -> refresh in place
            updated += 1
        else:
            pending[kind].append([name, d])

    added = 0
    for kind in ("F", "P"):
        if not pending[kind]:
            continue
        s, e = section_bounds(lumps, kind)
        if s is not None and e is not None:
            lumps[e:e] = pending[kind]          # insert just inside the existing section
        else:
            lumps += [[kind + "_START", b""]] + pending[kind] + [[kind + "_END", b""]]
        added += len(pending[kind])

    write_wad(WAD, magic, lumps)
    print("bake_freedoom_flats: %d lumps from %s -> %s (%d new, %d refreshed)"
          % (len(grabbed), os.path.basename(src), os.path.relpath(WAD, ROOT), added, updated))


if __name__ == "__main__":
    main()
