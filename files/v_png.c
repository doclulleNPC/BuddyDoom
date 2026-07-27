// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Light PNG support for UI elements.  Decodes a PNG lump (stb_image) and
//	converts it into a palette-quantised Doom `patch_t` so it can be drawn by
//	the ordinary V_DrawPatch path (HUD / menu graphics) with no renderer
//	changes.  Colours are nearest-matched into the IWAD PLAYPAL; pixels with
//	alpha < 128 become transparent (gaps in the column posts).
//
//	This is deliberately "light": UI-only, paletted output (so it banded like
//	any Doom graphic), small images (height <= 254, the 1-byte topdelta limit).
//	For true-colour in-world art see HD_TEXTURES.md.
//
//	Usage:  patch_t* p = V_CachePNG("RARRA0");  if (p) V_DrawPatch(x,y,0,p);
//	Results are cached by lump name; returns NULL if the lump is missing or
//	isn't a PNG.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "doomtype.h"
#include "z_zone.h"
#include "w_wad.h"
#include "r_defs.h"		// patch_t
#include "v_video.h"

// ---- palette (built once from PLAYPAL) -------------------------------------

static byte	vp_pal[256][3];
static boolean	vp_pal_ready;

static void VP_LoadPalette (void)
{
    const byte*	p = (const byte*) W_CacheLumpName ("PLAYPAL", PU_CACHE);
    int		i;
    for (i = 0; i < 256; i++)
	{ vp_pal[i][0] = p[i*3]; vp_pal[i][1] = p[i*3+1]; vp_pal[i][2] = p[i*3+2]; }
    vp_pal_ready = true;
}

// Nearest palette index for an RGB triple (squared-distance match).
static byte VP_Nearest (int r, int g, int b)
{
    int		i, best = 0;
    long	bestd = 0x7fffffff;
    for (i = 0; i < 256; i++)
    {
	int  dr = r - vp_pal[i][0], dg = g - vp_pal[i][1], db = b - vp_pal[i][2];
	long d  = (long)dr*dr + (long)dg*dg + (long)db*db;
	if (d < bestd) { bestd = d; best = i; if (!d) break; }
    }
    return (byte) best;
}

// ---- health-colour translation tables (green/yellow/red) -------------------
// A "translation" is a 256-entry palette remap applied per pixel by
// V_DrawPatchTranslated.  We build a luminance-preserving colourise for each hue,
// so any source colour (the gray HUD font OR the red status-bar numbers) becomes
// the target hue at the same brightness.

static byte	vp_xlat_grn[256], vp_xlat_yel[256], vp_xlat_red[256], vp_xlat_blu[256];
static boolean	vp_xlat_ready;

static void VP_BuildHealthXlats (void)
{
    int i;
    if (!vp_pal_ready) VP_LoadPalette ();
    for (i = 0; i < 256; i++)
    {
	int r = vp_pal[i][0], g = vp_pal[i][1], b = vp_pal[i][2];
	int L = (r*77 + g*150 + b*29) >> 8;		// luminance 0..255
	// A lighter green (mix some white in) instead of dark fully-saturated pure
	// green, so the healthy-HP readout (buddy + player) reads as a normal green.
	vp_xlat_grn[i] = VP_Nearest (L/2, L, L/2);
	vp_xlat_yel[i] = VP_Nearest (L, L*13/16, 0);	// gold (MBF cr_gold), not pure yellow
	vp_xlat_red[i] = VP_Nearest (L, 0, 0);
	vp_xlat_blu[i] = VP_Nearest (L/2, L/2, L);	// >max readout (mega health/armor)
    }
    vp_xlat_ready = true;
}

// MBF/Boom status-bar colour for a health/armor value: <25 red, <50 gold, <=100 green, else blue.
const byte* V_HealthTrans (int hp)
{
    if (!vp_xlat_ready) VP_BuildHealthXlats ();
    if (hp <  25) return vp_xlat_red;
    if (hp <  50) return vp_xlat_yel;
    if (hp <= 100) return vp_xlat_grn;
    return vp_xlat_blu;
}

// ---- buddy player-colour remaps --------------------------------------------
// Recolour ONLY the green player-uniform ramp (palette indices 0x70-0x7F) to a
// named target hue, scaled by each source pixel's luminance so the shading is
// preserved (dark folds stay dark).  Everything outside 0x70-0x7F is identity,
// so the marine's face/hands/weapon are untouched -- exactly like Doom's own
// green->indigo/brown/red player translations, just an arbitrary named set.
// Index 0 ("Green") is the marine's own colour: identity (V_BuddyColorTable
// returns NULL so callers fall through to a plain, untranslated draw).

#define VP_NBUDDYCOL	8
static const char* vp_buddycol_name[VP_NBUDDYCOL] =
    { "Green", "Gray", "Brown", "Red", "Blue", "Orange", "Purple", "White" };
static const byte vp_buddycol_rgb[VP_NBUDDYCOL][3] =
{
    {   0,   0,   0 },		// Green  -- identity, table unused
    { 184, 184, 184 },		// Gray
    { 152, 100,  52 },		// Brown
    { 208,  16,  16 },		// Red
    {  64,  72, 216 },		// Blue
    { 232, 120,  24 },		// Orange
    { 152,  48, 192 },		// Purple
    { 240, 240, 240 },		// White
};
static byte	vp_buddycol[VP_NBUDDYCOL][256];
static boolean	vp_buddycol_ready;

static void VP_BuildBuddyCols (void)
{
    int c, i;
    if (!vp_pal_ready) VP_LoadPalette ();
    for (c = 0; c < VP_NBUDDYCOL; c++)
    {
	for (i = 0; i < 256; i++)
	{
	    if (c != 0 && i >= 0x70 && i <= 0x7f)
	    {
		int r = vp_pal[i][0], g = vp_pal[i][1], b = vp_pal[i][2];
		int L = (r*77 + g*150 + b*29) >> 8;			// luminance 0..255
		vp_buddycol[c][i] = VP_Nearest (vp_buddycol_rgb[c][0]*L/255,
						vp_buddycol_rgb[c][1]*L/255,
						vp_buddycol_rgb[c][2]*L/255);
	    }
	    else
		vp_buddycol[c][i] = (byte) i;				// identity
	}
    }
    vp_buddycol_ready = true;
}

int         V_BuddyColorCount (void)      { return VP_NBUDDYCOL; }
const char* V_BuddyColorName  (int i)     { return (i >= 0 && i < VP_NBUDDYCOL) ? vp_buddycol_name[i] : ""; }

const byte* V_BuddyColorTable (int i)
{
    if (i <= 0 || i >= VP_NBUDDYCOL) return NULL;	// 0 = Green = identity (plain draw)
    if (!vp_buddycol_ready) VP_BuildBuddyCols ();
    return vp_buddycol[i];
}

// ---- RGBA -> patch_t --------------------------------------------------------

#define VP_ALPHA_CUT	128		// alpha below this -> transparent
#define VP_MAXH		254		// 1-byte topdelta cap (UI images are small)

// Build a column-format patch_t (Z_Malloc'd, PU_STATIC) from RGBA pixels.
// loff/toff are the sprite offsets (from a PNG grAb chunk, else 0).
static patch_t* VP_BuildPatch (const unsigned char* rgba, int w, int h, int loff, int toff)
{
    int		x, y, total, off;
    int*	colsize;
    byte*	base;
    patch_t*	patch;

    if (h > VP_MAXH) h = VP_MAXH;		// clamp (UI only)
    colsize = (int*) malloc (w * sizeof(int));
    if (!colsize) return NULL;

    // pass 1: size each column's post data (topdelta+len+pad + data + pad, then 0xff).
    total = 8 + w*4;				// header + columnofs[w]
    for (x = 0; x < w; x++)
    {
	int sz = 0;
	for (y = 0; y < h; )
	{
	    int top, run;
	    while (y < h && rgba[(y*w+x)*4+3] < VP_ALPHA_CUT) y++;	// skip clear
	    if (y >= h) break;
	    top = y; run = 0;
	    while (y < h && rgba[(y*w+x)*4+3] >= VP_ALPHA_CUT && (y-top) < 254) { run++; y++; }
	    sz += 4 + run;
	}
	sz += 1;				// end-of-column marker
	colsize[x] = sz;
	total += sz;
    }

    patch = (patch_t*) Z_Malloc (total, PU_STATIC, 0);
    base  = (byte*) patch;
    // header (host-endian == LE; the engine reads via SHORT/LONG which are no-ops on LE)
    patch->width = (short)w; patch->height = (short)h;
    patch->leftoffset = (short)loff; patch->topoffset = (short)toff;

    off = 8 + w*4;
    for (x = 0; x < w; x++)
    {
	byte* d = base + off;
	*(int*)(base + 8 + x*4) = off;		// columnofs[x]
	for (y = 0; y < h; )
	{
	    int top, run, k;
	    while (y < h && rgba[(y*w+x)*4+3] < VP_ALPHA_CUT) y++;
	    if (y >= h) break;
	    top = y; run = 0;
	    while (y < h && rgba[(y*w+x)*4+3] >= VP_ALPHA_CUT && (y-top) < 254) { run++; y++; }
	    *d++ = (byte)top;			// topdelta
	    *d++ = (byte)run;			// length
	    *d++ = 0;				// pad (V_DrawPatch skips it)
	    for (k = 0; k < run; k++)
	    {
		const unsigned char* px = &rgba[((top+k)*w+x)*4];
		*d++ = VP_Nearest (px[0], px[1], px[2]);
	    }
	    *d++ = 0;				// trailing pad
	}
	*d++ = 0xff;				// end of column
	off += colsize[x];
    }

    free (colsize);
    return patch;
}

// ---- public: cache a PNG lump as a patch -----------------------------------

#define VP_CACHE_MAX	64
static struct { char name[9]; patch_t* patch; } vp_cache[VP_CACHE_MAX];
static int vp_cache_n;

patch_t* V_CachePNG (const char* name)
{
    int			i, lump, len, w, h, comp;
    const byte*		raw;
    unsigned char*	rgba;
    patch_t*		patch;
    char		nm[9];

    if (!name) return NULL;
    strncpy (nm, name, 8); nm[8] = 0;

    for (i = 0; i < vp_cache_n; i++)
	if (!strncmp (vp_cache[i].name, nm, 8)) return vp_cache[i].patch;

    lump = W_CheckNumForName (nm);
    if (lump < 0) return NULL;
    len = W_LumpLength (lump);
    raw = (const byte*) W_CacheLumpNum (lump, PU_CACHE);
    if (len < 8 || raw[0] != 0x89 || raw[1] != 'P' || raw[2] != 'N' || raw[3] != 'G')
	return NULL;				// not a PNG -> caller handles as a normal lump

    rgba = stbi_load_from_memory (raw, len, &w, &h, &comp, 4);
    if (!rgba) return NULL;
    if (!vp_pal_ready) VP_LoadPalette ();
    patch = VP_BuildPatch (rgba, w, h, 0, 0);		// UI patches are drawn at explicit x,y
    stbi_image_free (rgba);
    if (!patch) return NULL;

    if (vp_cache_n < VP_CACHE_MAX)
	{ strncpy (vp_cache[vp_cache_n].name, nm, 8); vp_cache[vp_cache_n].name[8] = 0;
	  vp_cache[vp_cache_n].patch = patch; vp_cache_n++; }
    return patch;
}

// ---- public: convert a PNG *sprite* lump (by number) into a patch ----------
// Unlike V_CachePNG, this keeps the sprite's offsets: GZDoom PNG sprites store
// them in a `grAb` chunk (2 signed big-endian int32 = left/top).  Used by the
// sprite system (r_data.c R_InitSpriteLumps) so GZDoom PNG sprite WADs render
// instead of being skipped.  Returns a PU_STATIC patch, or NULL on failure.

// Find the grAb chunk offsets (walk PNG chunks; grAb precedes IDAT).
static void VP_ParseGrab (const byte* raw, int len, int* loff, int* toff)
{
    int p = 8;						// skip the 8-byte PNG signature
    *loff = *toff = 0;
    while (p + 12 <= len)
    {
	unsigned clen = ((unsigned)raw[p]<<24)|(raw[p+1]<<16)|(raw[p+2]<<8)|raw[p+3];
	const byte* ty = raw + p + 4;
	if (ty[0]=='g' && ty[1]=='r' && ty[2]=='A' && ty[3]=='b' && clen >= 8 && p+8+8 <= len)
	{
	    const byte* d = raw + p + 8;
	    *loff = (int)(((unsigned)d[0]<<24)|(d[1]<<16)|(d[2]<<8)|d[3]);
	    *toff = (int)(((unsigned)d[4]<<24)|(d[5]<<16)|(d[6]<<8)|d[7]);
	    return;
	}
	if (ty[0]=='I' && ty[1]=='D' && ty[2]=='A' && ty[3]=='T')
	    return;					// grAb always precedes IDAT
	p += 12 + (int)clen;				// 4 len + 4 type + data + 4 crc
    }
}

// Decode a PNG sprite lump to a patch.  If rgba_out != NULL, also keep an
// ARGB8888 copy of the full-colour image (Z_Malloc'd PU_STATIC, caller owns it)
// and report its w/h -- used for the truecolor HD sprite path.
patch_t* V_PNGLumpDecode (int lump, unsigned int** rgba_out, int* w_out, int* h_out)
{
    int			len, w, h, comp, loff, toff;
    const byte*		raw;
    unsigned char*	rgba;
    patch_t*		patch;

    if (rgba_out) *rgba_out = NULL;
    if (lump < 0) return NULL;
    len = W_LumpLength (lump);
    raw = (const byte*) W_CacheLumpNum (lump, PU_CACHE);
    if (len < 8 || raw[0]!=0x89 || raw[1]!='P' || raw[2]!='N' || raw[3]!='G')
	return NULL;

    VP_ParseGrab (raw, len, &loff, &toff);
    rgba = stbi_load_from_memory (raw, len, &w, &h, &comp, 4);
    if (!rgba) return NULL;
    if (!vp_pal_ready) VP_LoadPalette ();
    patch = VP_BuildPatch (rgba, w, h, loff, toff);

    if (rgba_out && patch)
    {
	// stb gives R,G,B,A bytes; pack to ARGB8888 (0xAARRGGBB) to match screen32/palette.
	unsigned int* keep = (unsigned int*) Z_Malloc (w*h*sizeof(unsigned int), PU_STATIC, 0);
	int i;
	for (i = 0; i < w*h; i++)
	{
	    const unsigned char* p = rgba + i*4;
	    keep[i] = ((unsigned)p[3]<<24) | (p[0]<<16) | (p[1]<<8) | p[2];
	}
	*rgba_out = keep;
	if (w_out) *w_out = w;
	if (h_out) *h_out = h;
    }
    stbi_image_free (rgba);
    return patch;
}

patch_t* V_PNGLumpToPatch (int lump)
{
    return V_PNGLumpDecode (lump, NULL, NULL, NULL);
}
