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

#include <math.h>
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

// ---- colour-range translation tables (Boom CR_*) ---------------------------
// A "translation" is a 256-entry palette remap applied per pixel by
// V_DrawPatchTranslated.  Building one by ramping RGB from a pixel's luminance
// (what this used to do) comes out muddy: the status-bar font is a shaded RED
// ramp, and forcing it through e.g. (L/2, L, L/2) throws its saturation away and
// then nearest-matches into whatever DOOM's palette happens to have nearby.
//
// So do it the way Boom/Woof/Crispy do (../woof/src/v_trans.c V_Colorize): go to
// HSV, keep the pixel's VALUE (its shading), force saturation, rotate the HUE to
// the target colour, come back.  The font keeps its highlights and drop shadow and
// simply changes colour.  The per-range tweaks below are Woof's, verbatim --
// green and gold shift hue slightly with brightness, which is what stops them
// reading as flat poster paint.

typedef struct { double x, y, z; } vp_vec;

#define VP_CTOL	0.0001

static void VP_RGBtoHSV (const vp_vec* rgb, vp_vec* hsv)
{
    double r = rgb->x, g = rgb->y, b = rgb->z;
    double cmax = r, cmin = r, h, sat, v;

    if (g > cmax) cmax = g;   if (g < cmin) cmin = g;
    if (b > cmax) cmax = b;   if (b < cmin) cmin = b;
    v = cmax;
    sat = (cmax > VP_CTOL) ? (cmax - cmin) / cmax : 0.0;
    if (sat < VP_CTOL)
	h = 0.0;
    else
    {
	double cd = cmax - cmin;
	double rc = (cmax - r) / cd, gc = (cmax - g) / cd, bc = (cmax - b) / cd;
	if      (r == cmax) h = bc - gc;
	else if (g == cmax) h = 2.0 + rc - bc;
	else                h = 4.0 + gc - rc;
	h *= 60.0;
	if (h < 0.0) h += 360.0;
    }
    hsv->x = h / 360.0; hsv->y = sat; hsv->z = v;
}

static void VP_HSVtoRGB (const vp_vec* hsv, vp_vec* rgb)
{
    double h = hsv->x * 360.0, sat = hsv->y, v = hsv->z;

    if (sat < VP_CTOL) { rgb->x = rgb->y = rgb->z = v; return; }
    if (h >= 360.0) h -= 360.0;
    h /= 60.0;
    {
	int    i = (int) floor (h);
	double f = h - i;
	double pp = v * (1.0 - sat);
	double q  = v * (1.0 - sat * f);
	double t  = v * (1.0 - sat * (1.0 - f));
	switch (i)
	{
	  case 0:  rgb->x = v;  rgb->y = t;  rgb->z = pp; break;
	  case 1:  rgb->x = q;  rgb->y = v;  rgb->z = pp; break;
	  case 2:  rgb->x = pp; rgb->y = v;  rgb->z = t;  break;
	  case 3:  rgb->x = pp; rgb->y = q;  rgb->z = v;  break;
	  case 4:  rgb->x = t;  rgb->y = pp; rgb->z = v;  break;
	  default: rgb->x = v;  rgb->y = pp; rgb->z = q;  break;
	}
    }
}

static byte vp_cr[VP_NCR][256];
static boolean vp_cr_ready;

static void VP_BuildColorRanges (void)
{
    int cr, i;

    if (!vp_pal_ready) VP_LoadPalette ();
    for (cr = 0; cr < VP_NCR; cr++)
	for (i = 0; i < 256; i++)
	{
	    vp_vec rgb, hsv;
	    rgb.x = vp_pal[i][0] / 255.0;
	    rgb.y = vp_pal[i][1] / 255.0;
	    rgb.z = vp_pal[i][2] / 255.0;
	    VP_RGBtoHSV (&rgb, &hsv);

	    if (cr == VP_CR_GRAY)
		hsv.y = 0.0;				// drop all colour, keep shading
	    else
	    {
		hsv.y = 1.0;				// full saturation (crispy)
		switch (cr)
		{
		  case VP_CR_RED:
		    hsv.x = 0.0;
		    break;
		  case VP_CR_GREEN:
		    hsv.x = (144.0 * hsv.z + 120.0 * (1.0 - hsv.z)) / 360.0;
		    break;
		  case VP_CR_GOLD:
		    hsv.x = (7.0 + 53.0 * hsv.z) / 360.0;
		    hsv.y = 1.0 - 0.4 * hsv.z;
		    hsv.z = (hsv.z < 0.2 ? hsv.z : 0.2) + 0.8 * hsv.z;
		    break;
		  case VP_CR_BLUE2:
		  default:
		    hsv.x = 240.0 / 360.0;
		    break;
		}
	    }
	    VP_HSVtoRGB (&hsv, &rgb);
	    vp_cr[cr][i] = VP_Nearest ((int)(rgb.x * 255.0 + 0.5),
				       (int)(rgb.y * 255.0 + 0.5),
				       (int)(rgb.z * 255.0 + 0.5));
	}
    vp_cr_ready = true;
}

// One of the VP_CR_* ranges, or NULL for "no remap" (VP_CR_NONE).
const byte* V_ColorRange (int cr)
{
    if (cr <= VP_CR_NONE || cr >= VP_NCR) return NULL;
    if (!vp_cr_ready) VP_BuildColorRanges ();
    return vp_cr[cr];
}

// MBF/Boom status-bar colour for a health/armor value: <25 red, <50 gold,
// <=100 green, else blue.
const byte* V_HealthTrans (int hp)
{
    return V_ColorRange (hp <  25 ? VP_CR_RED
		       : hp <  50 ? VP_CR_GOLD
		       : hp <= 100 ? VP_CR_GREEN
		       : VP_CR_BLUE2);
}

// ---- buddy player-colour remaps --------------------------------------------
// Recolour ONLY the green player-uniform ramp (palette indices 0x70-0x7F) to a
// named target hue, scaled by each source pixel's luminance so the shading is
// preserved (dark folds stay dark).  Everything outside 0x70-0x7F is identity,
// so the marine's face/hands/weapon are untouched -- exactly like Doom's own
// green->indigo/brown/red player translations, just an arbitrary named set.
// Index 0 ("Green") is the marine's own colour: identity (V_BuddyColorTable
// returns NULL so callers fall through to a plain, untranslated draw).

// "None" (last index) is like Green: identity -- no recolour, the sprite's own colours.
// It is appended AFTER White so the existing 0..7 config values keep their meaning.
#define VP_NBUDDYCOL	9
#define VP_BUDDYCOL_NONE (VP_NBUDDYCOL - 1)
static const char* vp_buddycol_name[VP_NBUDDYCOL] =
    { "Green", "Gray", "Brown", "Red", "Blue", "Orange", "Purple", "White", "None" };
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
    {   0,   0,   0 },		// None   -- identity, table unused (plain draw)
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
	    if (c != 0 && c != VP_BUDDYCOL_NONE && i >= 0x70 && i <= 0x7f)
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
    if (i == VP_BUDDYCOL_NONE)       return NULL;	// "None" = identity too (plain draw)
    if (!vp_buddycol_ready) VP_BuildBuddyCols ();
    return vp_buddycol[i];
}

// ---- RGBA -> patch_t --------------------------------------------------------

#define VP_ALPHA_CUT	128		// alpha below this -> transparent
#define VP_MAXH		254		// 1-byte topdelta cap (UI images are small)

// Build a column-format patch_t (Z_Malloc'd, PU_STATIC) from RGBA pixels.
// loff/toff are the sprite offsets (from a PNG grAb chunk, else 0).
// `tag`/`user` go straight to Z_Malloc, so the caller decides whether the patch is
// pinned (PU_STATIC) or purgeable (PU_CACHE + the address of its own cache slot).
static patch_t* VP_BuildPatch (const unsigned char* rgba, int w, int h, int loff, int toff,
			       int tag, void** user)
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

    patch = (patch_t*) Z_Malloc (total, tag, user);
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
    patch = VP_BuildPatch (rgba, w, h, 0, 0, PU_STATIC, NULL);	// UI: drawn at explicit x,y
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
// Read a PNG lump's dimensions + grAb offsets WITHOUT decoding any pixels.
// R_InitSpriteLumps needs spritewidth/offset for every sprite up front, but a pack
// like hexenstuff.wad/strifestuff.wad has thousands of them -- decoding all of them
// at load cost hundreds of MB of zone (see V_PNGLumpDecodeCached).  IHDR is always
// the first chunk, so w/h are at a fixed offset; grAb is walked like VP_ParseGrab.
// Returns false if the lump isn't a PNG.
boolean V_PNGLumpInfo (int lump, int* w_out, int* h_out, int* loff, int* toff)
{
    int		len;
    const byte*	raw;

    if (lump < 0) return false;
    len = W_LumpLength (lump);
    if (len < 33) return false;
    raw = (const byte*) W_CacheLumpNum (lump, PU_CACHE);
    if (raw[0]!=0x89 || raw[1]!='P' || raw[2]!='N' || raw[3]!='G')
	return false;
    if (raw[12]!='I' || raw[13]!='H' || raw[14]!='D' || raw[15]!='R')
	return false;
    if (w_out) *w_out = (int)(((unsigned)raw[16]<<24)|(raw[17]<<16)|(raw[18]<<8)|raw[19]);
    if (h_out) *h_out = (int)(((unsigned)raw[20]<<24)|(raw[21]<<16)|(raw[22]<<8)|raw[23]);
    VP_ParseGrab (raw, len, loff, toff);
    return true;
}

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
    patch = VP_BuildPatch (rgba, w, h, loff, toff, PU_STATIC, NULL);

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

// Decode a PNG sprite lump into PURGEABLE zone blocks, tied to the caller's own
// pointers.  This is what the sprite system uses: an all-PNG pack has thousands of
// sprites and decoding them PU_STATIC at load pins hundreds of MB (a full-colour
// copy of strifestuff.wad's sprites alone is ~176 MB against a 48 MB heap -- the
// "Z_Malloc: failed on allocation" report).  PU_CACHE + a user pointer lets the zone
// reclaim any sprite that isn't currently on screen; Z_Free NULLs the pointer, and
// the caller simply decodes again on the next draw.
//
// `patch_user` and `rgba_user` are the addresses of the caller's cache slots.
// Pass rgba_user == NULL to skip the full-colour copy entirely (the truecolor HD
// path is off, so it would be pinned for nothing).
patch_t* V_PNGLumpDecodeCached (int lump, void** patch_user,
				void** rgba_user, int* w_out, int* h_out)
{
    int			len, w, h, comp, loff, toff;
    const byte*		raw;
    unsigned char*	rgba;
    patch_t*		patch;

    if (lump < 0 || !patch_user) return NULL;
    len = W_LumpLength (lump);
    raw = (const byte*) W_CacheLumpNum (lump, PU_CACHE);
    if (len < 8 || raw[0]!=0x89 || raw[1]!='P' || raw[2]!='N' || raw[3]!='G')
	return NULL;

    VP_ParseGrab (raw, len, &loff, &toff);
    rgba = stbi_load_from_memory (raw, len, &w, &h, &comp, 4);
    if (!rgba) return NULL;
    if (!vp_pal_ready) VP_LoadPalette ();
    patch = VP_BuildPatch (rgba, w, h, loff, toff, PU_CACHE, patch_user);
    if (!patch)
    {
	stbi_image_free (rgba);
	return NULL;
    }

    if (rgba_user)
    {
	unsigned int*	keep = (unsigned int*) Z_Malloc (w*h*sizeof(unsigned int),
							 PU_CACHE, rgba_user);
	int		i;
	for (i = 0; i < w*h; i++)
	{
	    const unsigned char* p = rgba + i*4;
	    keep[i] = ((unsigned)p[3]<<24) | (p[0]<<16) | (p[1]<<8) | p[2];
	}
	if (w_out) *w_out = w;
	if (h_out) *h_out = patch->height;		// VP_BuildPatch may clamp (host-endian)
    }
    stbi_image_free (rgba);
    return patch;
}

// ---------------------------------------------------------------------------
// V_IsPNGLump / V_PNGLumpToFlat -- PNG in the FLAT and TEXTURE-PATCH paths.
//
// The sprite path has read PNG since R_InitSpriteLumps; flats and wall patches had not,
// so a PWAD that stored those as PNG handed raw file bytes to code expecting Doom
// formats.  For a flat that renders as noise; for a texture patch it is worse -- PNAMES
// resolves by plain W_CheckNumForName, so R_GenerateComposite would read `columnofs[]`
// out of the PNG header and index far outside the lump.
// ---------------------------------------------------------------------------

boolean V_IsPNGLump (int lump)
{
    const byte*	raw;

    if (lump < 0 || W_LumpLength (lump) < 8) return false;
    raw = (const byte*) W_CacheLumpNum (lump, PU_CACHE);
    return raw[0]==0x89 && raw[1]=='P' && raw[2]=='N' && raw[3]=='G';
}

// Decode a PNG flat into the 64x64 linear paletted buffer R_DrawSpan wants.  The span
// drawer masks its coordinates with &63 / &(63*64), so 64x64 is not a convention here --
// it is the only size that can be addressed.  Anything else is refused rather than
// stretched, so a mis-sized flat is a visible "not found" instead of a scrambled floor.
//
// Allocated PU_CACHE against the caller's slot: a big PNG flat set would otherwise pin
// 4 KB per flat forever, and the zone can reclaim any floor that is off screen.
byte* V_PNGLumpToFlat (int lump, void** user)
{
    int			len, w, h, comp, i;
    const byte*		raw;
    unsigned char*	rgba;
    byte*		flat;

    if (lump < 0 || !user) return NULL;
    len = W_LumpLength (lump);
    raw = (const byte*) W_CacheLumpNum (lump, PU_CACHE);
    if (len < 8 || raw[0]!=0x89 || raw[1]!='P' || raw[2]!='N' || raw[3]!='G')
	return NULL;

    rgba = stbi_load_from_memory (raw, len, &w, &h, &comp, 4);
    if (!rgba) return NULL;
    if (w != 64 || h != 64)
    {
	static int	warned;
	if (warned < 8)
	{
	    fprintf (stderr, "V_PNGLumpToFlat: %.8s is %dx%d; a flat must be 64x64\n",
		     lumpinfo[lump].name, w, h);
	    warned++;
	}
	stbi_image_free (rgba);
	return NULL;
    }

    if (!vp_pal_ready) VP_LoadPalette ();
    flat = (byte*) Z_Malloc (64*64, PU_CACHE, user);
    for (i = 0; i < 64*64; i++)
    {
	const unsigned char* p = rgba + i*4;
	// A flat is opaque by definition -- there is no transparent index to fall back on,
	// so a cut-out pixel just takes its nearest colour like every other one.
	flat[i] = VP_Nearest (p[0], p[1], p[2]);
    }
    stbi_image_free (rgba);
    return flat;
}
