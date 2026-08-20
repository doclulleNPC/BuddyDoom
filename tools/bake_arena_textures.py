#!/usr/bin/env python3
# bake_arena_textures.py -- give a map pack its own TEXTURE1 / PNAMES / ANIMATED so
# renamed graphics become usable and animate.
#
# The problem this solves: a PWAD can drop a patch lump in P_START..P_END all it likes,
# but A PATCH IS NOT A TEXTURE.  A texture exists only if some TEXTURE1 record defines one
# from it, and DOOM.WAD defines no SFALL* at all (that is a DOOM2 texture).  So renamed
# patches like SFALLF1-4 stay invisible to both the editor and the engine until a TEXTURE1
# here names them.  FLATS need none of this -- they resolve by lump name, so NUKAGEF1-3
# already work as soon as they sit inside F_START..F_END.
#
# TWO WHOLESALE-REPLACEMENT TRAPS, both handled by rebuilding the whole table:
#
#   * TEXTURE1/PNAMES.  R_InitTextures calls W_CacheLumpName("TEXTURE1"), which takes the
#     LAST match -- a PWAD's table REPLACES the IWAD's rather than adding to it.  Ship a
#     TEXTURE1 holding only the four new entries and every stock wall texture disappears.
#     So the IWAD's entire table is copied in and the new records appended.  New PNAMES
#     entries are APPENDED too, which keeps every existing patch index valid.
#
#   * ANIMATED.  P_InitPicAnims (files/p_spec.c) uses the lump INSTEAD OF its built-in
#     animdefs[] whenever one is present -- so an ANIMATED holding only the new animations
#     would stop nukage, lava, blood, the firewall textures and everything else from
#     cycling.  The built-in table is therefore parsed straight out of p_spec.c and
#     re-emitted ahead of the new entries, so the two cannot drift apart.
#
# Consequence worth knowing: the baked TEXTURE1 is a snapshot of ONE IWAD's texture set.
# For an E1Mx pack built against DOOM.WAD that is what you want; loading it over DOOM2.WAD
# would narrow the texture list to Doom 1's.
#
#   python3 tools/bake_arena_textures.py
#   python3 tools/bake_arena_textures.py --wad run/ID0/other.wad
#
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_WAD = os.path.join(ROOT, "run", "ID0", "e1-arenas.wad")
PSPEC = os.path.join(ROOT, "files", "p_spec.c")

IWADS = ["DOOM.WAD", "doom.wad", "DOOM2.WAD", "doom2.wad",
         "freedoom1.wad", "freedoom2.wad"]

# Single-patch wall textures to define: (texture name, patch lump, width, height)
NEW_TEXTURES = [("SFALLF1", "SFALLF1", 64, 128), ("SFALLF2", "SFALLF2", 64, 128),
                ("SFALLF3", "SFALLF3", 64, 128), ("SFALLF4", "SFALLF4", 64, 128)]
# Animations to add: (istexture, last, first, speed)
NEW_ANIMS = [(0, "NUKAGEF3", "NUKAGEF1", 8),
             (1, "SFALLF4",  "SFALLF1",  8)]


def read_lumps(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, num, off = struct.unpack("<4sii", data[:12])
    out = []
    for i in range(num):
        lo, ls, nm = struct.unpack("<ii8s", data[off + i * 16 : off + i * 16 + 16])
        out.append([nm.split(b"\x00")[0].decode("latin1"), data[lo : lo + ls]])
    return magic, out


def write_wad(path, magic, lumps):
    body, dirent, off = bytearray(), bytearray(), 12
    for name, data in lumps:
        dirent += struct.pack("<ii8s", off + len(body), len(data),
                              name.encode("latin1")[:8].ljust(8, b"\x00"))
        body += data
    with open(path, "wb") as f:
        f.write(struct.pack("<4sii", magic, len(lumps), 12 + len(body)))
        f.write(body)
        f.write(dirent)


def lump_of(lumps, name):
    for n, d in lumps:
        if n.upper() == name.upper():
            return d
    return None


def find_iwad():
    for p in IWADS:
        fp = os.path.join(ROOT, "run", "ID0", p)
        if os.path.exists(fp):
            return fp
    sys.exit("bake_arena_textures: no IWAD under run/ID0/ to take TEXTURE1/PNAMES from")


def parse_pnames(raw):
    n = struct.unpack("<i", raw[:4])[0]
    return [raw[4 + i * 8 : 12 + i * 8].split(b"\x00")[0].decode("latin1").upper()
            for i in range(n)]


def build_pnames(names):
    out = bytearray(struct.pack("<i", len(names)))
    for n in names:
        out += n.encode("latin1")[:8].ljust(8, b"\x00")
    return bytes(out)


def parse_texture1(raw):
    """-> the raw per-texture records in order (offsets get rebuilt on write)."""
    cnt = struct.unpack("<i", raw[:4])[0]
    offs = list(struct.unpack("<%di" % cnt, raw[4 : 4 + 4 * cnt]))
    recs = []
    for o in offs:
        pc = struct.unpack("<h", raw[o + 20 : o + 22])[0]
        recs.append(raw[o : o + 22 + 10 * pc])
    return recs


def texture_record(name, w, h, patchidx):
    """A single-patch texture: 22-byte header plus one 10-byte mappatch_t at (0,0)."""
    r = bytearray()
    r += name.encode("latin1")[:8].ljust(8, b"\x00")
    r += struct.pack("<i", 0)              # masked -- ignored by the renderer
    r += struct.pack("<hh", w, h)
    r += struct.pack("<i", 0)              # obsolete columndirectory: 4 bytes, and it has
                                           # to stay 4 so the on-disk layout matches the
                                           # 32-bit original (see docs/LEGACY_FIXES.md)
    r += struct.pack("<h", 1)              # patchcount
    r += struct.pack("<hhhhh", 0, 0, patchidx, 1, 0)
    return bytes(r)


def build_texture1(recs):
    cnt = len(recs)
    off = 4 + 4 * cnt
    offs, body = [], bytearray()
    for r in recs:
        offs.append(off + len(body))
        body += r
    return struct.pack("<i", cnt) + struct.pack("<%di" % cnt, *offs) + bytes(body)


def vanilla_anims():
    """The engine's compile-time animdefs[], read out of p_spec.c so it cannot drift."""
    src = open(PSPEC, encoding="latin1").read()
    m = re.search(r"animdef_t\s+animdefs\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        sys.exit("bake_arena_textures: could not find animdefs[] in files/p_spec.c")
    out = []
    pat = r'\{\s*(true|false)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*\}'
    for istex, last, first, speed in re.findall(pat, m.group(1)):
        out.append((1 if istex == "true" else 0, last, first, int(speed)))
    return out


def build_animated(entries):
    out = bytearray()
    for istex, last, first, speed in entries:
        out += struct.pack("<B", istex)
        out += last.encode("latin1")[:8].ljust(9, b"\x00")
        out += first.encode("latin1")[:8].ljust(9, b"\x00")
        out += struct.pack("<i", speed)
    out += b"\xff" + b"\x00" * 22          # terminator record
    return bytes(out)


def put(lumps, name, data):
    """Replace the lump if it is already there, else append it (root namespace)."""
    for l in lumps:
        if l[0].upper() == name.upper():
            l[1] = data
            return "refreshed"
    lumps.append([name, data])
    return "new"


def main():
    args = sys.argv[1:]
    wad = DEFAULT_WAD
    if "--wad" in args:
        i = args.index("--wad")
        if i + 1 >= len(args):
            sys.exit("bake_arena_textures: --wad needs a path")
        wad = args[i + 1] if os.path.isabs(args[i + 1]) else os.path.join(ROOT, args[i + 1])
    if not os.path.exists(wad):
        sys.exit("bake_arena_textures: %s not found" % wad)

    iwad = find_iwad()
    _, iw = read_lumps(iwad)
    magic, lumps = read_lumps(wad)

    # Every patch the new textures reference has to actually be in the target WAD.
    for _, patch, _, _ in NEW_TEXTURES:
        if lump_of(lumps, patch) is None:
            sys.exit("bake_arena_textures: patch %s missing from %s"
                     % (patch, os.path.basename(wad)))

    pn = parse_pnames(lump_of(iw, "PNAMES"))
    base = len(pn)
    for _, patch, _, _ in NEW_TEXTURES:
        if patch.upper() not in pn:
            pn.append(patch.upper())

    # TEXTURE1 only.  R_InitTextures reads the PWAD's TEXTURE1 (last match) AND still
    # picks up the IWAD's TEXTURE2 separately, so folding TEXTURE2 in here would list every
    # one of those textures twice.  The IWAD's TEXTURE2 records keep working untouched:
    # PNAMES was extended by APPENDING, so all their patch indices still resolve.
    recs = parse_texture1(lump_of(iw, "TEXTURE1"))
    have = {r[:8].split(b"\x00")[0].decode("latin1").upper() for r in recs}
    added = 0
    for name, patch, w, h in NEW_TEXTURES:
        if name.upper() in have:
            continue
        recs.append(texture_record(name, w, h, pn.index(patch.upper())))
        added += 1

    anims = vanilla_anims()
    nv = len(anims)
    for e in NEW_ANIMS:
        if e not in anims:
            anims.append(e)

    r1 = put(lumps, "PNAMES", build_pnames(pn))
    r2 = put(lumps, "TEXTURE1", build_texture1(recs))
    r3 = put(lumps, "ANIMATED", build_animated(anims))
    write_wad(wad, magic, lumps)

    print("bake_arena_textures: base = %s" % os.path.basename(iwad))
    print("  PNAMES   %-9s %d entries  (%d from the IWAD + %d new)"
          % (r1, len(pn), base, len(pn) - base))
    print("  TEXTURE1 %-9s %d textures (%d from the IWAD + %d new)"
          % (r2, len(recs), len(recs) - added, added))
    print("  ANIMATED %-9s %d animations (%d vanilla + %d new)"
          % (r3, len(anims), nv, len(anims) - nv))
    print("  -> %s" % os.path.relpath(wad, ROOT))


if __name__ == "__main__":
    main()
