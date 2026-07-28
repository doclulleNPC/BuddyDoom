#!/usr/bin/env python3
# bake_corvusface.py -- build run/ID0/corvusface.wad, the CORVUS mugshot set for the
# co-op buddy HUD in HERETIC.  The DOOM buddy shows the marine face (BUF* lumps from
# buddydoom.wad); in Heretic the buddy is Corvus, so the HUD loads the COR* set baked
# here instead (see files/hu_buddy.c HU_Buddy_LoadFaces).
#
# Source: the Corvus face PNGs in run/ID0/FACE.FD/ -- named STF* (the standard Doom
# face-lump scheme) and carrying a grAb offset (~ -5,-2, matching the BUF* faces).  We
# convert each to a Doom patch_t indexed against the HERETIC palette (so the paletted
# HUD blit shows correct colours) and rename the leading STF -> COR:
#     STFST00 -> CORST00 , STFOUCH0 -> COROUCH0 , STFGOD0 -> CORGOD0 , ...
#
# The faces are drawn via V_DrawPatch (paletted, native colour) exactly like the marine
# faces -- no PNG/hi-res path -- so they must be real patch_t lumps, not PNG lumps.
# Idempotent: re-running overwrites run/ID0/corvusface.wad.

import os, sys, io, struct, glob
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FACES = os.path.join(ROOT, "run", "ID0", "FACE.FD")
WAD   = os.path.join(ROOT, "run", "ID0", "corvusface.wad")

def find_heretic_iwad():
    for n in ("heretic.wad", "HERETIC.WAD", "blasphemer.wad", "BLASPHEMER.WAD"):
        p = os.path.join(ROOT, "run", "ID0", n)
        if os.path.exists(p): return p
    sys.exit("bake_corvusface: no heretic.wad/blasphemer.wad under run/ID0/ (needed for PLAYPAL)")

def read_playpal(iwad):
    with open(iwad, "rb") as f: data = f.read()
    magic, n, diroff = struct.unpack("<4sii", data[:12])
    for i in range(n):
        off, sz = struct.unpack("<ii", data[diroff+i*16:diroff+i*16+8])
        name = data[diroff+i*16+8:diroff+i*16+16].rstrip(b"\x00").decode("ascii","ignore")
        if name.upper() == "PLAYPAL": return data[off:off+768]
    sys.exit("bake_corvusface: PLAYPAL not found in " + iwad)

def build_nearest(playpal):
    pal = [(playpal[i*3], playpal[i*3+1], playpal[i*3+2]) for i in range(256)]
    cache = {}
    def nearest(rgb):
        v = cache.get(rgb)
        if v is None:
            r, g, b = rgb; best = 0; bd = 1 << 30
            for i, (pr, pg, pb) in enumerate(pal):
                d = (r-pr)**2 + (g-pg)**2 + (b-pb)**2
                if d < bd: bd = d; best = i
                if d == 0: break
            v = cache[rgb] = best
        return v
    return nearest

def grab_from_png(b):
    i = 8
    while i < len(b):
        ln = struct.unpack(">I", b[i:i+4])[0]; typ = b[i+4:i+8]
        if typ == b"grAb": return struct.unpack(">ii", b[i+8:i+16])
        i += 12 + ln
    return None

# PNG -> Doom patch_t against the Heretic palette.  Per-pixel alpha < 128 = transparent
# (a gap in the column), so the face background drops out cleanly.  Offsets come from grAb.
def png_to_patch(raw, nearest):
    img = Image.open(io.BytesIO(raw)).convert("RGBA")
    w, h = img.size
    px = img.load()
    lo, to = grab_from_png(raw) or (0, 0)
    header = struct.pack("<hhhh", w, h, lo, to)
    cols = bytearray(); colofs = []; base = len(header) + 4*w
    for x in range(w):
        colofs.append(base + len(cols)); y = 0
        while y < h:
            while y < h and px[x, y][3] < 128: y += 1          # skip transparent
            if y >= h: break
            top = y; run = bytearray()
            while y < h and px[x, y][3] >= 128:
                r, g, b, _ = px[x, y]
                run.append(nearest((r, g, b))); y += 1
                if len(run) == 254: break
            cols.append(top & 0xff); cols.append(len(run)); cols.append(0)
            cols += run; cols.append(0)
        cols.append(0xff)
    return header + b"".join(struct.pack("<i", o) for o in colofs) + bytes(cols)

def write_wad(path, lumps):
    with open(path, "wb") as f:
        f.write(b"PWAD"); f.write(struct.pack("<ii", len(lumps), 0))
        ent = []
        for name, data in lumps:
            off = f.tell(); f.write(data); ent.append((off, len(data), name))
        diroff = f.tell()
        for off, sz, name in ent:
            f.write(struct.pack("<ii", off, sz))
            f.write(name.encode("ascii")[:8].ljust(8, b"\x00"))
        f.seek(8); f.write(struct.pack("<i", diroff))

def main():
    if not os.path.isdir(FACES):
        sys.exit(f"bake_corvusface: {FACES} not found (the Corvus STF* PNGs)")
    nearest = build_nearest(read_playpal(find_heretic_iwad()))
    lumps = []
    for f in sorted(glob.glob(os.path.join(FACES, "STF*.png"))):
        stem = os.path.splitext(os.path.basename(f))[0].upper()   # STFST00
        name = "COR" + stem[3:]                                   # -> CORST00
        with open(f, "rb") as fh: raw = fh.read()
        lumps.append((name, png_to_patch(raw, nearest)))
    if not lumps:
        sys.exit(f"bake_corvusface: no STF*.png in {FACES}")
    write_wad(WAD, lumps)
    print(f"bake_corvusface: {len(lumps)} Corvus face patches (COR*) -> {WAD}")

if __name__ == "__main__":
    main()
