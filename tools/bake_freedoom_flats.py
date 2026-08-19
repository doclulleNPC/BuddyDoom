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
# The default target is the MAP PACK, not buddydoom.wad.  Load order puts buddydoom.wad
# after the IWAD and flat lookup takes the last match, so shipping these there would
# replace id's nukage in every map of every game -- not the intent.  In e1-arenas.wad they
# reach exactly the maps that ask for them.
#
#   python3 tools/bake_freedoom_flats.py                             # -> e1-arenas.wad
#   python3 tools/bake_freedoom_flats.py --wad run/ID0/other.wad     # somewhere else
#   python3 tools/bake_freedoom_flats.py --remove --wad run/ID0/x.wad
#
# --remove strips the lumps again, and drops F_START/F_END and P_START/P_END with them if
# that empties the section -- so a WAD this ran on can be put back exactly as it was.
#
# If the target is buddydoom.wad, re-run it AFTER bake_buddy_voice.py: that one rewrites
# the WAD from scratch, so every other bake has to be re-applied on top (the same rule
# bake_secdrone.py already documents).
#
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_WAD = os.path.join(ROOT, "run", "ID0", "e1-arenas.wad")

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


def namespaces(lumps):
    """Namespace letter per lump index: 'F' inside F_*..F_END, 'P' inside P_*, 'S' inside
    S_*, '' otherwise.  Sub-markers (F1_START, PP_START, ...) keep the outer letter, which
    is exactly how R_InitFlats / R_InitSpriteLumps read them."""
    out = []
    ns = ''
    for n, _ in lumps:
        u = n.upper()
        if u.endswith('_START') and u[0] in 'FPS':
            ns = u[0]; out.append(ns); continue
        if u.endswith('_END') and u[0] in 'FPS':
            out.append(ns); ns = ''; continue
        out.append(ns)
    return out


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


def strip(wad):
    magic, lumps = read_lumps(wad)
    drop = {n.upper() for n, _ in WANT}
    kept = [l for l in lumps if l[0].upper() not in drop]
    removed = len(lumps) - len(kept)

    # Drop a section marker pair that we just emptied, so the WAD goes back to the shape
    # it had before this script ever touched it.
    for kind in ("F", "P"):
        s, e = section_bounds(kept, kind)
        while s is not None and e is not None and all(
                not d and kept[i][0].upper().endswith(("_START", "_END"))
                for i, (n, d) in enumerate(kept[s:e+1], s)):
            del kept[s:e+1]
            removed += (e - s + 1)
            s, e = section_bounds(kept, kind)

    write_wad(wad, magic, kept)
    print("bake_freedoom_flats: removed %d lumps from %s"
          % (removed, os.path.relpath(wad, ROOT)))


def main():
    args = sys.argv[1:]
    remove = "--remove" in args
    if remove:
        args.remove("--remove")
    wad = DEFAULT_WAD
    if "--wad" in args:
        i = args.index("--wad")
        if i + 1 >= len(args):
            sys.exit("bake_freedoom_flats: --wad needs a path")
        wad = args[i+1] if os.path.isabs(args[i+1]) else os.path.join(ROOT, args[i+1])

    if not os.path.exists(wad):
        sys.exit("bake_freedoom_flats: %s not found" % wad)
    if remove:
        strip (wad)
        return

    WAD = wad
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
    ns = namespaces(lumps)

    updated = 0
    strays = []
    pending = {"F": [], "P": []}
    for name, kind, d in grabbed:
        # Refresh in place ONLY when the existing lump is in the namespace this one belongs
        # in.  Matching on the bare name overwrote e1-arenas.wad's own PNG art, which
        # happens to carry these very names in the SPRITE namespace -- same name, different
        # thing entirely, and a flat dropped in among the sprites would not be found by
        # R_InitFlats anyway.
        hit = None
        for i, (n, _) in enumerate(lumps):
            if n.upper() == name.upper():
                if ns[i] == kind:
                    hit = i
                    break
                strays.append((name, ns[i] or "(root)"))
        if hit is not None:
            lumps[hit][1] = d
            updated += 1
        else:
            pending[kind].append([name, d])

    if strays:
        print("bake_freedoom_flats: NOTE -- %s already carries these names in another "
              "namespace; left untouched, but a global by-name lookup (PNAMES) takes the "
              "LAST match, so the duplicates are worth clearing out:"
              % os.path.relpath(WAD, ROOT))
        for name, where in strays:
            print("    %-9s in ns=%s" % (name, where))

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
