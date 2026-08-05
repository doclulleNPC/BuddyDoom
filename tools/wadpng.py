#!/usr/bin/env python3
"""
wadpng.py -- shared helper for the *stuff.wad extractors: turn a Doom-format
`patch_t` graphic into a PNG lump that BuddyDoom can load directly.

Why PNG at all?  The old extractors palette-QUANTISED every borrowed sprite into
the DOOM PLAYPAL (tools/extract_hexen.py's build_xlat/remap_patch).  That threw
away colour permanently -- Hexen's and Strife's palettes are quite different from
DOOM's, so ~every pixel moved to a near-miss shade and could never be recovered.

BuddyDoom reads PNG sprite lumps natively (files/r_data.c R_InitSpriteLumps ->
files/v_png.c V_PNGLumpDecode).  It builds BOTH:
  * a paletted patch_t for the 8-bit software path (nearest-matched into the
    CURRENT IWAD's PLAYPAL, at load time -- so the same pack looks right whether
    it's loaded over DOOM, DOOM2 or Freedoom), and
  * a full-colour ARGB copy in hdsprite[] for the truecolor sprite blit
    (files/r_things.c R_BlitHDSprite).
So writing PNG in the SOURCE game's own palette is strictly better than baking a
conversion in: nothing is lost, and the truecolor path gets real Hexen/Strife art.

Sprite offsets survive via a `grAb` chunk (GZDoom's convention, 2 signed
big-endian int32 = leftoffset/topoffset) -- exactly what VP_ParseGrab reads.

No third-party deps: PNGs are written by hand with zlib.
"""

import struct
import zlib

__all__ = ["read_patch", "patch_to_png", "is_patch"]


def is_patch(raw):
    """Cheap sanity check that `raw` looks like a Doom patch_t."""
    if len(raw) < 8:
        return False
    w, h, _lo, _to = struct.unpack("<hhhh", raw[:8])
    if not (0 < w <= 4096 and 0 < h <= 4096):
        return False
    return 8 + 4 * w <= len(raw)


def read_patch(raw):
    """Decode a Doom patch_t.

    Returns (w, h, leftoffset, topoffset, rows) where rows[y][x] is a palette
    index or None for transparent.  Tolerates truncated / malformed columns the
    way the engine does (skip, don't raise) -- borrowed IWAD art is sometimes
    sloppy and a hard failure would lose the whole lump.
    """
    w, h, loff, toff = struct.unpack("<hhhh", raw[:8])
    cols = struct.unpack(f"<{w}I", raw[8:8 + 4 * w])
    rows = [[None] * w for _ in range(h)]
    for x, o in enumerate(cols):
        if not (0 < o < len(raw)):
            continue
        # Doom column posts: topdelta, length, pad, pixels..., pad; 0xFF ends.
        while o < len(raw) and raw[o] != 0xFF:
            if o + 2 >= len(raw):
                break
            top, n = raw[o], raw[o + 1]
            o += 3                                  # + the leading pad byte
            for i in range(n):
                p = o + i
                if p >= len(raw):
                    break
                y = top + i
                if 0 <= y < h:
                    rows[y][x] = raw[p]
            o += n + 1                              # + the trailing pad byte
    return w, h, loff, toff, rows


def _chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data)))


def patch_to_png(raw, palette):
    """Doom patch_t + its OWN game's 256-entry palette -> PNG bytes (RGBA + grAb).

    `palette` is a list of 256 (r, g, b) tuples -- the source WAD's PLAYPAL, NOT
    the target's.  Returns None if `raw` isn't a usable patch.
    """
    if not is_patch(raw):
        return None
    w, h, loff, toff, rows = read_patch(raw)

    # RGBA scanlines, filter byte 0 (None) per row.  Transparent pixels are
    # written fully transparent black; v_png.c treats alpha < 128 as a gap in
    # the column posts, which is exactly the patch's own transparency.
    raw_rgba = bytearray()
    for y in range(h):
        raw_rgba.append(0)
        row = rows[y]
        for x in range(w):
            c = row[x]
            if c is None:
                raw_rgba += b"\0\0\0\0"
            else:
                r, g, b = palette[c]
                raw_rgba += bytes((r, g, b, 255))

    out = b"\x89PNG\r\n\x1a\n"
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    out += _chunk(b"grAb", struct.pack(">ii", loff, toff))   # BEFORE IDAT
    out += _chunk(b"IDAT", zlib.compress(bytes(raw_rgba), 9))
    out += _chunk(b"IEND", b"")
    return out
