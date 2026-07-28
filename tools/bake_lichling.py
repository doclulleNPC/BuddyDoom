#!/usr/bin/env python3
# bake_lichling.py -- build run/ID0/lichling.wad, the sprite WAD for the Lichling
# (the little-Lich buddy special).  Two sprite sets, both kept as true-colour PNG
# lumps (BuddyDoom renders PNG sprites in full colour via V_PNGLumpDecode/hdsprite,
# like FRANK.wad) so the Lichling shows correct colours in BOTH Heretic and DOOM:
#
#   * BODY   -- the extracted Iron-Lich PNG frames in run/ID0/lich/ (LICH*),
#               scaled down and renamed to sprite LICS (LICH/ironlich stay the
#               Iron Lich's; LICS = "lich small").
#   * ATTACK -- the Iron-Lich projectiles pulled straight from heretic.wad and
#               palette-decoded to PNG: FX05 (fire) -> LICF, FX06 (ice) -> LICE.
#
# Every frame's grAb offset is scaled with the image so positioning stays right.
#
#   python tools/bake_lichling.py           # 50% scale
#   python tools/bake_lichling.py 0.6
#
import os, struct, sys, io, zlib
from PIL import Image

HERE  = os.path.dirname(os.path.abspath(__file__))
ROOT  = os.path.dirname(HERE)
BODY  = os.path.join(ROOT, "run", "ID0", "lich")       # extracted Iron-Lich PNG frames
WAD   = os.path.join(ROOT, "run", "ID0", "lichling.wad")
IWAD  = os.path.join(ROOT, "run", "ID0", "heretic.wad")
SCALE = float(sys.argv[1]) if len(sys.argv) > 1 else 0.5

BODY_SRC_PREFIX, BODY_DST_PREFIX = "LICH", "LICS"       # body: LICH* -> LICS*
# heretic.wad projectile sprite -> Lichling sprite code (fire / ice).
PROJECTILES = [("FX05", "LICF"), ("FX06", "LICE")]

# ---- PNG helpers ----------------------------------------------------------
def read_grab(b):
    i = 8
    while i + 12 <= len(b):
        ln = struct.unpack(">I", b[i:i+4])[0]; typ = b[i+4:i+8]
        if typ == b"grAb": return struct.unpack(">ii", b[i+8:i+16])
        i += 12 + ln
    return None

def png_chunk(typ, data):
    return struct.pack(">I", len(data)) + typ + data + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff)

def img_to_png_lump(img, lo, to, scale):
    w0, h0 = img.size
    w = max(1, round(w0 * scale)); h = max(1, round(h0 * scale))
    img = img.resize((w, h), Image.LANCZOS)
    buf = io.BytesIO(); img.save(buf, "PNG"); png = buf.getvalue()
    grab = png_chunk(b"grAb", struct.pack(">ii", round(lo * scale), round(to * scale)))
    return png[:33] + grab + png[33:]                    # 8-byte sig + IHDR(25) = 33

def png_file_to_lump(raw, scale):
    img = Image.open(io.BytesIO(raw)).convert("RGBA")
    lo, to = read_grab(raw) or (img.size[0] // 2, img.size[1])
    return img_to_png_lump(img, lo, to, scale)

# ---- WAD / Doom-patch helpers ---------------------------------------------
def read_lumps(path):
    with open(path, "rb") as f: data = f.read()
    magic, num, off = struct.unpack("<4sii", data[:12])
    out = []
    for i in range(num):
        lo, ls, nm = struct.unpack("<ii8s", data[off+i*16:off+i*16+16])
        out.append((nm.split(b"\x00")[0].decode("latin1"), data[lo:lo+ls]))
    return out

def read_playpal(iwad):
    for name, d in read_lumps(iwad):
        if name.upper() == "PLAYPAL": return d[:768]
    sys.exit("bake_lichling: PLAYPAL not found in " + iwad)

# Decode a Doom patch_t (indexed columns) to an RGBA PIL image using `pal` (768 bytes).
def patch_to_img(raw, pal):
    w, h, lo, to = struct.unpack("<hhhh", raw[:8])
    colofs = struct.unpack("<%di" % w, raw[8:8 + 4*w])
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0)); px = img.load()
    for x in range(w):
        p = colofs[x]
        while raw[p] != 0xff:
            top = raw[p]; ln = raw[p+1]; p += 3           # topdelta, length, unused pad
            for i in range(ln):
                idx = raw[p + i]
                if 0 <= top + i < h:
                    px[x, top + i] = (pal[idx*3], pal[idx*3+1], pal[idx*3+2], 255)
            p += ln + 1                                    # pixels + trailing pad
    return img, lo, to

def write_wad(path, magic, lumps):
    with open(path, "wb") as f:
        f.write(magic); f.write(struct.pack("<i", len(lumps))); f.write(struct.pack("<i", 0))
        ent = []
        for name, data in lumps:
            off = f.tell(); f.write(data); ent.append((off, len(data), name))
        diroff = f.tell()
        for off, sz, name in ent:
            f.write(struct.pack("<ii", off, sz)); f.write(name.encode("ascii")[:8].ljust(8, b"\x00"))
        f.seek(8); f.write(struct.pack("<i", diroff))

def main():
    if not os.path.isdir(BODY): sys.exit(f"bake_lichling: {BODY} not found (extract the body PNGs there)")
    if not os.path.exists(IWAD): sys.exit(f"bake_lichling: {IWAD} not found (for projectiles + palette)")
    pal   = read_playpal(IWAD)
    hlump = read_lumps(IWAD)

    lumps = [("S_START", b"")]

    # 1) body frames: LICH*.png -> LICS*
    body = sorted(f for f in os.listdir(BODY) if f.lower().endswith(".png"))
    for fn in body:
        with open(os.path.join(BODY, fn), "rb") as f: raw = f.read()
        name = os.path.splitext(fn)[0].upper()
        if name.startswith(BODY_SRC_PREFIX): name = BODY_DST_PREFIX + name[len(BODY_SRC_PREFIX):]
        lumps.append((name[:8], png_file_to_lump(raw, SCALE)))
    nbody = len(lumps) - 1

    # 2) projectiles: heretic.wad FX05/FX06 patches -> PNG under LICF/LICE.  Only the
    #    plain single-rotation frames (e.g. FX05A0) -- skip the multi-rotation impact.
    nproj = 0
    for src, dst in PROJECTILES:
        for name, data in hlump:
            if len(name) == 6 and name[:4] == src and name[5] == "0" and len(data) >= 8:
                img, lo, to = patch_to_img(data, pal)
                lumps.append((dst + name[4:], img_to_png_lump(img, lo, to, SCALE)))
                nproj += 1

    lumps.append(("S_END", b""))
    write_wad(WAD, b"PWAD", lumps)
    print(f"bake_lichling: {nbody} body (LICS) + {nproj} projectile (LICF/LICE) PNG frames @ {SCALE:g}x -> {WAD}")

if __name__ == "__main__":
    main()
