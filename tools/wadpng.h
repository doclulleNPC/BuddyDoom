// wadpng.h -- shared *stuff.wad graphics conversion (C mirror of tools/wadpng.py).
//
// Turns a Doom-format `patch_t` graphic into a PNG lump that BuddyDoom loads
// natively (files/r_data.c R_InitSpriteLumps -> files/v_png.c).  The engine
// nearest-matches the PNG into whatever IWAD palette is actually running AND keeps
// a full-colour copy for the truecolor sprite path, so writing PNG in the SOURCE
// game's own palette is strictly better than baking a conversion in: nothing is
// lost, and the pack is smaller than the raw column format.
//
// Sprite offsets ride along in a `grAb` chunk (GZDoom's convention, 2 signed
// big-endian int32 = leftoffset/topoffset) -- what v_png.c VP_ParseGrab reads.
//
// tools/extract_hexen.py and tools/extract_strife.py implement the same thing in
// Python; keep the two in step.
#ifndef WADPNG_H
#define WADPNG_H

// True if `raw` looks like a Doom patch_t (so a same-prefixed sound lump that
// sneaked into a sprite scan can be rejected instead of corrupting the pack).
int wadpng_is_patch(const unsigned char* raw, int size);

// Convert a Doom patch to PNG using `pal` (768 bytes, the SOURCE wad's PLAYPAL).
// Returns a malloc'd buffer (caller frees) and stores its length in *out_len, or
// NULL if `raw` is not a usable patch / compression failed.
unsigned char* wadpng_from_patch(const unsigned char* raw, int size,
                                 const unsigned char* pal, int* out_len);

#endif
