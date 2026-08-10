// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	The actual span/column drawing functions.
//	Here find the main potential for optimization,
//	 e.g. inline assembly, different algorithms.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: r_draw.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";


#include <stdint.h>

#include "doomdef.h"

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "r_local.h"
#include "tables.h"		// finesine/finecosine + ANG45/ANG90 (damage indicator arc)
#include "m_fixed.h"		// FixedMul

// Needs access to LFB (guess what).
#include "v_video.h"

// State.
#include "doomstat.h"


// MAXWIDTH / MAXHEIGHT now live in doomdef.h (shared with the runtime
// SCREENWIDTH / SCREENHEIGHT scaling).

// status bar height at bottom of screen (in base 320x200 coords; the real
// status bar is SBARHEIGHT*hires pixels tall at the current resolution)
#define SBARHEIGHT		32

//
// All drawing to the view buffer is accomplished in this file.
// The other refresh files only know about ccordinates,
//  not the architecture of the frame buffer.
// Conveniently, the frame buffer is a linear one,
//  and we need only the base address,
//  and the total size == width*height*depth/8.,
//


byte*		viewimage;
int		viewwidth;
int		scaledviewwidth;
int		viewheight;
int		viewwindowx;
int		viewwindowy;
byte*		ylookup[MAXHEIGHT];
int		columnofs[MAXWIDTH];

// Truecolor ("Fullcolor"): the column/span drawers dual-write a parallel 32-bit
// framebuffer (screen32) via colormap32, a per-light-level RGB shade of the true
// palette (i_video.c).  cm32 = colormap32 + (dc_colormap - colormaps) picks the
// row matching the 8-bit light level; dst32 = screen32 + (dest - screens[0]) is the
// same pixel in the parallel buffer.  i_video.c composites it at present time.
extern unsigned int	colormap32[];
extern unsigned int*	screen32;
extern int		truecolor;
#define TC_CMAPS	34		// colormap32[] row count (must match NUMCMAPS in i_video.c)
// True iff `cm` is a row inside the main COLORMAP that colormap32 mirrors.  A Boom
// sector/fog colormap (a different array, or a row past 34) fails this -> that pixel
// falls back to the 8-bit palette expansion instead of reading out of colormap32.
#define TC_INRANGE(cm)	(truecolor && (unsigned long)((cm) - colormaps) < (unsigned long)(TC_CMAPS*256))

// Gameplay crosshair: 0 off, 1 cross, 2 dot, 3 big cross.  Drawn into the 8-bit
// frame at the 3D-view centre (see R_DrawCrosshair, called from D_Display).
int		crosshair = 0;
// Crosshair colour: an index into a small named set (Green/White/Red/Yellow/Blue),
// resolved to the nearest PLAYPAL entry so it looks right in DOOM/Heretic/Hexen
// (their palettes differ).  Set from Options -> Crosshair.
int		crosshair_color = 0;
// Colour the crosshair by the player's health instead of the fixed colour above
// (Woof's hud_crosshair_health).  Same bands as the status bar: <25 red, <50 gold,
// <=100 green, above max blue, and grey while invulnerable.
int		crosshair_health = 0;

static void XHairPix (int x, int y, int col)
{
    if (x >= 0 && y >= 0 && x < SCREENWIDTH && y < SCREENHEIGHT)
	screens[0][y*SCREENWIDTH + x] = (byte)col;
}

// Nearest palette index to the wanted crosshair colour.  Cached on the colour
// index (the loaded palette is stable for the run), so this scans PLAYPAL only
// when the user changes the colour.
static int XHairColorIndex (void)
{
    static const byte want[5][3] =
    {
	{   0, 255,   0 },	// Green (default; nearest to the old hard-coded 0x70)
	{ 255, 255, 255 },	// White
	{ 255,   0,   0 },	// Red
	{ 255, 255,   0 },	// Yellow
	{  64, 160, 255 }	// Blue
    };
    static int cached_c = -1, cached_i = 0x70;
    const byte* pal;
    const byte* t;
    int i, c;

    c = (crosshair_color >= 0 && crosshair_color <= 4) ? crosshair_color : 0;
    if (c == cached_c)
	return cached_i;

    pal = (const byte*) W_CacheLumpName ("PLAYPAL", PU_CACHE);
    if (!pal)
	return 0x70;
    t = want[c];
    {
	int best = 0, bestd = 0x7fffffff;
	for (i = 0; i < 256; i++)
	{
	    int dr = pal[i*3+0] - t[0];
	    int dg = pal[i*3+1] - t[1];
	    int db = pal[i*3+2] - t[2];
	    int d  = dr*dr + dg*dg + db*db;
	    if (d < bestd) { bestd = d; best = i; }
	}
	cached_c = c; cached_i = best;
    }
    return cached_i;
}

void R_DrawCrosshair (void)
{
    int	cx, cy, i, len, gap, col;

    if (!crosshair)
	return;
    cx  = viewwindowx + scaledviewwidth/2;
    cy  = viewwindowy + viewheight/2;
    col = XHairColorIndex ();			// user-selected colour (nearest palette)

    // Health colouring (Woof CRByHealth): run the chosen colour through the matching
    // Boom colour range rather than substituting a fixed palette index, so the
    // crosshair keeps its brightness and only its HUE moves -- a bright green cross
    // turns bright red, a dim one turns dim red.  Same tables the status-bar numbers
    // use (v_png.c), so the two always agree on what "low health" looks like.
    if (crosshair_health)
    {
	player_t*	pl = &players[displayplayer];
	int		hp = pl->health;
	const byte*	tr;

	tr = V_ColorRange ((pl->powers[pw_invulnerability] || (pl->cheats & CF_GODMODE))
				       ? VP_CR_GRAY
			 : hp <  25   ? VP_CR_RED
			 : hp <  50   ? VP_CR_GOLD
			 : hp <= 100  ? VP_CR_GREEN
			 : VP_CR_BLUE2);
	if (tr)
	    col = tr[col];
    }

    if (crosshair == 2)				// filled dot
    {
	int r = hires, dx, dy;
	for (dy = -r ; dy <= r ; dy++)
	    for (dx = -r ; dx <= r ; dx++)
		XHairPix (cx+dx, cy+dy, col);
	return;
    }

    gap = 2*hires;				// centre gap so the target stays visible
    len = (crosshair == 3 ? 7 : 4) * hires;
    for (i = gap ; i <= len ; i++)
    {
	XHairPix (cx+i, cy, col);  XHairPix (cx-i, cy, col);
	XHairPix (cx, cy+i, col);  XHairPix (cx, cy-i, col);
    }
}


// ===========================================================================
//  Directional damage indicator (modern-FPS "hit indicator" / damage ring)
//
//  A red arc around the crosshair pointing at the direction incoming damage came
//  from.  P_DamageMobj calls R_DamageIndicator with the world-space BAM angle from
//  the viewed player to the attacker; D_Display calls R_DrawDamageIndicators after
//  the crosshair.  The arc's screen bearing is (attacker angle - viewangle), so it
//  rotates as you turn -- attacker straight ahead shows at 12 o'clock, behind at 6.
//  Purely a HUD overlay: it reads gametic/viewangle but never feeds the playsim, so
//  demos and netplay stay in sync.  Toggle: `damage_indicator` (Options -> Crosshair).
// ===========================================================================
int		damage_indicator = 1;		// config (m_misc.c): on/off

#define DMGIND_MAX	8			// concurrent arcs (distinct directions)
#define DMGIND_TICS	24			// ~0.7 s lifetime per hit
#define DMGIND_RAMP	8			// red fade steps (bright -> dark)

static struct { angle_t ang; int until; } dmgind[DMGIND_MAX];

// Nearest-palette red for fade `level` (0 = brightest .. RAMP-1 = darkest).  Resolved
// against PLAYPAL so it's a real red in DOOM/Heretic/Hexen/Strife; cached (the loaded
// palette is stable for the run).
static int R_DamageRed (int level)
{
    static int	ramp[DMGIND_RAMP];
    static int	ready = 0;
    if (!ready)
    {
	const byte* pal = (const byte*) W_CacheLumpName ("PLAYPAL", PU_CACHE);
	int k, i;
	for (k = 0; k < DMGIND_RAMP; k++)
	{
	    int want = 224 - k*22;		// red channel bright (224) -> dark (~70)
	    int best = 0x70, bestd = 0x7fffffff;
	    if (!pal) { ramp[k] = 0x70; continue; }
	    for (i = 0; i < 256; i++)
	    {
		int dr = pal[i*3+0] - want, dg = pal[i*3+1], db = pal[i*3+2];
		int d  = dr*dr + dg*dg + db*db;
		if (d < bestd) { bestd = d; best = i; }
	    }
	    ramp[k] = best;
	}
	ready = 1;
    }
    if (level < 0)            level = 0;
    if (level >= DMGIND_RAMP) level = DMGIND_RAMP - 1;
    return ramp[level];
}

// Record a hit from `worldangle` (BAM, viewed player -> attacker).  Hits from ~the same
// direction refresh one arc instead of spawning a swarm; otherwise the oldest slot is
// reused.
void R_DamageIndicator (angle_t worldangle)
{
    int	i, slot = 0, oldest = 0x7fffffff;
    if (!damage_indicator)
	return;
    for (i = 0; i < DMGIND_MAX; i++)
    {
	angle_t d = worldangle - dmgind[i].ang;
	if (d > ANG180) d = (angle_t)0 - d;			// |delta|
	if (dmgind[i].until > gametic && d < ANG45/2) { slot = i; break; }
	if (dmgind[i].until < oldest) { oldest = dmgind[i].until; slot = i; }
    }
    dmgind[slot].ang   = worldangle;
    dmgind[slot].until = gametic + DMGIND_TICS;
}

void R_DrawDamageIndicators (void)
{
    int	cx, cy, R, thick, i;
    if (!damage_indicator)
	return;
    cx    = viewwindowx + scaledviewwidth/2;
    cy    = viewwindowy + viewheight/2;
    R     = 18 * hires;					// ring radius around the crosshair
    thick = 2  * hires;
    for (i = 0; i < DMGIND_MAX; i++)
    {
	angle_t	s;
	int	life, col, k;
	const int steps = 40;
	if (dmgind[i].until <= gametic)
	    continue;
	life = dmgind[i].until - gametic;			// DMGIND_TICS (fresh) .. 1 (fading)
	col  = R_DamageRed ((DMGIND_TICS - life) * DMGIND_RAMP / DMGIND_TICS);
	// screen bearing: attacker straight ahead (relative 0) -> 12 o'clock (top)
	s = ANG90 + (dmgind[i].ang - viewangle);
	for (k = 0; k <= steps; k++)
	{
	    angle_t  a    = s - ANG45/2 + (angle_t)((unsigned long long)ANG45 * k / steps);
	    unsigned fine = a >> ANGLETOFINESHIFT;
	    fixed_t  cosv = finecosine[fine], sinv = finesine[fine];
	    int	     rr;
	    for (rr = R; rr < R + thick; rr++)
	    {
		int px = cx + (FixedMul (rr<<FRACBITS, cosv) >> FRACBITS);
		int py = cy - (FixedMul (rr<<FRACBITS, sinv) >> FRACBITS);
		XHairPix (px, py, col);
	    }
	}
    }
}

// Color tables for different players,
//  translate a limited part to another
//  (color ramps used for  suit colors).
//
byte		translations[3][256];	
 
 


//
// R_DrawColumn
// Source is the top of the column to scale.
//
lighttable_t*		dc_colormap; 
int			dc_x; 
int			dc_yl; 
int			dc_yh; 
fixed_t			dc_iscale; 
fixed_t			dc_texturemid;
int			dc_skyheight = 128;

// Full height (rows) of the texture the current column belongs to.  Vanilla
// hardcoded a 128-row wrap (`&127`), which tiled any texture taller than 128
// (e.g. Legacy of Rust's 512x512 ZZZGATE portal -> scrambled).  Callers set this
// to the real texture height before drawing (r_segs.c walls, r_things.c sprites);
// it defaults to 128 so anything that forgets behaves exactly as vanilla.
int			dc_texheight = 128;

// first pixel in a column (possibly virtual) 
byte*			dc_source;		

// just for profiling 
int			dccount;

//
// A column is a vertical slice/span from a wall texture that,
//  given the DOOM style restrictions on the view orientation,
//  will always have constant z depth.
// Thus a special case loop for very fast rendering can
//  be used. It has also been used with Wolfenstein 3D.
// 
void R_DrawColumn (void) 
{ 
    int			count; 
    byte*		dest; 
    fixed_t		frac;
    fixed_t		fracstep;	 
 
    count = dc_yh - dc_yl; 

    // Zero length, column does not exceed a pixel.
    if (count < 0) 
	return; 
				 
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT) 
	I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x); 
#endif 

    // Framebuffer destination address.
    // Use ylookup LUT to avoid multiply with ScreenWidth.
    // Use columnofs LUT for subwindows? 
    dest = ylookup[dc_yl] + columnofs[dc_x];  

    // Determine scaling,
    //  which is the only mapping to be done.
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 

    // Inner loop that does the actual texture mapping,
    //  e.g. a DDA-lile scaling.
    // This is as fast as it gets.
    {
	int heightmask = dc_texheight - 1;

	if (TC_INRANGE(dc_colormap))		// dual-write the smooth 32-bit view
	{
	    unsigned int*	cm32  = colormap32 + (dc_colormap - colormaps);
	    unsigned int*	dst32 = screen32   + (dest - screens[0]);
	    if (dc_texheight & heightmask)	// non-power-of-two: modulo wrap
	    {
		heightmask = dc_texheight << FRACBITS;
		if (frac < 0) while ((frac += heightmask) < 0) ;
		else          while (frac >= heightmask) frac -= heightmask;
		do {
		    byte px = dc_source[frac>>FRACBITS];
		    *dest = dc_colormap[px];  *dst32 = cm32[px];
		    dest += SCREENWIDTH;      dst32 += SCREENWIDTH;
		    if ((frac += fracstep) >= heightmask) frac -= heightmask;
		} while (count--);
	    }
	    else				// power-of-two
	    {
		do {
		    byte px = dc_source[(frac>>FRACBITS) & heightmask];
		    *dest = dc_colormap[px];  *dst32 = cm32[px];
		    dest += SCREENWIDTH;      dst32 += SCREENWIDTH;
		    frac += fracstep;
		} while (count--);
	    }
	    return;
	}

	if (dc_texheight & heightmask)		// non-power-of-two: modulo wrap
	{
	    heightmask = dc_texheight << FRACBITS;
	    if (frac < 0) while ((frac += heightmask) < 0) ;
	    else          while (frac >= heightmask) frac -= heightmask;
	    do {
		*dest = dc_colormap[dc_source[frac>>FRACBITS]];
		dest += SCREENWIDTH;
		if ((frac += fracstep) >= heightmask) frac -= heightmask;
	    } while (count--);
	}
	else					// power-of-two (128 -> vanilla &127)
	{
	    do {
		*dest = dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]];
		dest += SCREENWIDTH;
		frac += fracstep;
	    } while (count--);
	}
    }
}


//
// R_DrawTLColumn  (Boom 260) -- like R_DrawColumn but blends the texel over the existing screen
// pixel through dc_tranmap, giving a translucent 2S middle texture.
//
byte* dc_tranmap;

void R_DrawTLColumn (void)
{
    int		count;
    byte*	dest;
    fixed_t	frac;
    fixed_t	fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
	return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
	I_Error ("R_DrawTLColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[dc_x];
    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl-centery)*fracstep;

    {
	int heightmask = dc_texheight - 1;
	// Truecolor: the tranmap gives an 8-bit blended index; mirror its true colour
	// into screen32 (colormap32[idx] with idx<256 is row 0 == the plain palette).
	unsigned int*	dst32 = truecolor ? screen32 + (dest - screens[0]) : NULL;
	if (dc_texheight & heightmask)		// non-power-of-two: modulo wrap
	{
	    heightmask = dc_texheight << FRACBITS;
	    if (frac < 0) while ((frac += heightmask) < 0) ;
	    else          while (frac >= heightmask) frac -= heightmask;
	    do {
		*dest = dc_tranmap[(*dest<<8) + dc_colormap[dc_source[frac>>FRACBITS]]];
		if (dst32) { *dst32 = colormap32[*dest]; dst32 += SCREENWIDTH; }
		dest += SCREENWIDTH;
		if ((frac += fracstep) >= heightmask) frac -= heightmask;
	    } while (count--);
	}
	else
	{
	    do {
		// foreground texel (already colormapped) over the current screen pixel via the tranmap
		*dest = dc_tranmap[(*dest<<8) + dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]]];
		if (dst32) { *dst32 = colormap32[*dest]; dst32 += SCREENWIDTH; }
		dest += SCREENWIDTH;
		frac += fracstep;
	    } while (count--);
	}
    }
}


//
// R_DrawSkyColumn
// Like R_DrawColumn but CLAMPS the texture row to [0,127] instead of wrapping
// (&127).  The sky texture is 128 rows; at hi-res a tall sky span (a large open
// area where the sky fills much of the screen) maps past row 127 and the wrap
// would tile the sky vertically.  Clamping extends the bottom row (horizon)
// downward and the top row upward instead -- no visible repeat.  Identical to
// R_DrawColumn for normal (<=128-row) spans, so no regression for small skies.
//
void R_DrawSkyColumn (void)
{
    int		count;
    byte*	dest;
    fixed_t	frac;
    fixed_t	fracstep;
    int		row;

    count = dc_yh - dc_yl;
    if (count < 0)
	return;

    dest = ylookup[dc_yl] + columnofs[dc_x];
    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl-centery)*fracstep;

    if (TC_INRANGE(dc_colormap))			// dual-write the smooth 32-bit view
    {
	unsigned int*	cm32  = colormap32 + (dc_colormap - colormaps);
	unsigned int*	dst32 = screen32   + (dest - screens[0]);
	do {
	    row = frac>>FRACBITS;
	    if (row < 0)                  row = 0;
	    else if (row >= dc_skyheight) row = dc_skyheight - 1;
	    { byte px = dc_source[row]; *dest = dc_colormap[px]; *dst32 = cm32[px]; }
	    dest += SCREENWIDTH;  dst32 += SCREENWIDTH;
	    frac += fracstep;
	} while (count--);
	return;
    }
    do
    {
	row = frac>>FRACBITS;
	if (row < 0)                  row = 0;
	else if (row >= dc_skyheight) row = dc_skyheight - 1;
	*dest = dc_colormap[dc_source[row]];
	dest += SCREENWIDTH;
	frac += fracstep;
    } while (count--);
}

// Like R_DrawSkyColumn but palette index 0 is transparent -- used to overlay an
// ID24 SKYDEFS foreground sky layer (type 2) on top of the background sky.
void R_DrawSkyColumnMasked (void)
{
    int		count;
    byte*	dest;
    fixed_t	frac, fracstep;
    int		row;
    byte	s;

    count = dc_yh - dc_yl;
    if (count < 0) return;
    dest = ylookup[dc_yl] + columnofs[dc_x];
    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl-centery)*fracstep;
    if (TC_INRANGE(dc_colormap))			// dual-write the smooth 32-bit view
    {
	unsigned int*	cm32  = colormap32 + (dc_colormap - colormaps);
	unsigned int*	dst32 = screen32   + (dest - screens[0]);
	do {
	    row = frac>>FRACBITS;
	    if (row < 0)                  row = 0;
	    else if (row >= dc_skyheight) row = dc_skyheight - 1;
	    s = dc_source[row];
	    if (s) { *dest = dc_colormap[s]; *dst32 = cm32[s]; }	// index 0 = transparent
	    dest += SCREENWIDTH;  dst32 += SCREENWIDTH;
	    frac += fracstep;
	} while (count--);
	return;
    }
    do
    {
	row = frac>>FRACBITS;
	if (row < 0)                  row = 0;
	else if (row >= dc_skyheight) row = dc_skyheight - 1;
	s = dc_source[row];
	if (s) *dest = dc_colormap[s];		// index 0 = transparent
	dest += SCREENWIDTH;
	frac += fracstep;
    } while (count--);
}



// UNUSED.
// Loop unrolled.
#if 0
void R_DrawColumn (void) 
{ 
    int			count; 
    byte*		source;
    byte*		dest;
    byte*		colormap;
    
    unsigned		frac;
    unsigned		fracstep;
    unsigned		fracstep2;
    unsigned		fracstep3;
    unsigned		fracstep4;	 
 
    count = dc_yh - dc_yl + 1; 

    source = dc_source;
    colormap = dc_colormap;		 
    dest = ylookup[dc_yl] + columnofs[dc_x];  
	 
    fracstep = dc_iscale<<9; 
    frac = (dc_texturemid + (dc_yl-centery)*dc_iscale)<<9; 
 
    fracstep2 = fracstep+fracstep;
    fracstep3 = fracstep2+fracstep;
    fracstep4 = fracstep3+fracstep;
	
    while (count >= 8) 
    { 
	dest[0] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*2] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*3] = colormap[source[(frac+fracstep3)>>25]];
	
	frac += fracstep4; 

	dest[SCREENWIDTH*4] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH*5] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*6] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*7] = colormap[source[(frac+fracstep3)>>25]]; 

	frac += fracstep4; 
	dest += SCREENWIDTH*8; 
	count -= 8;
    } 
	
    while (count > 0)
    { 
	*dest = colormap[source[frac>>25]]; 
	dest += SCREENWIDTH; 
	frac += fracstep; 
	count--;
    } 
}
#endif


//
// Spectre/Invisibility.
//
#define FUZZTABLE		50 
// One screen row.  SCREENWIDTH is now a runtime value, so the table holds
// unit offsets and the row stride is applied at the use site.
#define FUZZOFF	(1)


int	fuzzoffset[FUZZTABLE] =
{
    FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,
    FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
    FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,
    FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,
    FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF 
}; 

int	fuzzpos = 0; 


void R_DrawShadowColumn (void)
{
    int		count;
    byte*	dest;
    byte*   colormap;

    count = dc_yh - dc_yl + 1;
    if (count <= 0)
	return;

    dest = ylookup[dc_yl] + columnofs[dc_x];
    colormap = colormaps + 16 * 256;

    if (truecolor)				// dual-write: smooth-darken via colormap32 row 16
    {
	unsigned int*	dst32 = screen32 + (dest - screens[0]);
	do {
	    byte bg = *dest;
	    *dest  = colormap[bg];
	    *dst32 = colormap32[16*256 + bg];
	    dest += SCREENWIDTH;  dst32 += SCREENWIDTH;
	} while (--count);
	return;
    }
    do
    {
	*dest = colormap[*dest];
	dest += SCREENWIDTH;
    } while (--count);
}


//
// Framebuffer postprocessing.
// Creates a fuzzy image by copying pixels
//  from adjacent ones to left and right.
// Used with an all black colormap, this
//  could create the SHADOW effect,
//  i.e. spectres and invisible players.
//
void R_DrawFuzzColumn (void) 
{ 
    int			count; 
    byte*		dest; 
    fixed_t		frac;
    fixed_t		fracstep;	 

    // Adjust borders. Low... 
    if (!dc_yl) 
	dc_yl = 1;

    // .. and high.
    if (dc_yh == viewheight-1) 
	dc_yh = viewheight - 2; 
		 
    count = dc_yh - dc_yl; 

    // Zero length.
    if (count < 0) 
	return; 

    
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0 || dc_yh >= SCREENHEIGHT)
    {
	I_Error ("R_DrawFuzzColumn: %i to %i at %i",
		 dc_yl, dc_yh, dc_x);
    }
#endif


    dest = ylookup[dc_yl] + columnofs[dc_x];

    // Looks familiar.
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 

    // Looks like an attempt at dithering,
    //  using the colormap #6 (of 0-31, a bit
    //  brighter than average).
    {
	unsigned int* dst32 = truecolor ? screen32 + (dest - screens[0]) : NULL;
	do
	{
	    // Lookup framebuffer, and retrieve a pixel one column left/right of the
	    // current one; darken it through colormap #6.  Truecolor mirrors that with
	    // the smooth colormap32 row 6 of the same neighbour pixel.
	    byte nb = dest[fuzzoffset[fuzzpos]*SCREENWIDTH];
	    *dest = colormaps[6*256+nb];
	    if (dst32) { *dst32 = colormap32[6*256+nb]; dst32 += SCREENWIDTH; }

	    // Clamp table lookup index.
	    if (++fuzzpos == FUZZTABLE)
		fuzzpos = 0;

	    dest += SCREENWIDTH;
	    frac += fracstep;
	} while (count--);
    }
}
 
  
 

//
// R_DrawTranslatedColumn
// Used to draw player sprites
//  with the green colorramp mapped to others.
// Could be used with different translation
//  tables, e.g. the lighter colored version
//  of the BaronOfHell, the HellKnight, uses
//  identical sprites, kinda brightened up.
//
byte*	dc_translation;
byte*	translationtables;

void R_DrawTranslatedColumn (void) 
{ 
    int			count; 
    byte*		dest; 
    fixed_t		frac;
    fixed_t		fracstep;	 
 
    count = dc_yh - dc_yl; 
    if (count < 0) 
	return; 
				 
#ifdef RANGECHECK 
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT)
    {
	I_Error ( "R_DrawColumn: %i to %i at %i",
		  dc_yl, dc_yh, dc_x);
    }
    
#endif 


    dest = ylookup[dc_yl] + columnofs[dc_x];

    // Looks familiar.
    fracstep = dc_iscale; 
    frac = dc_texturemid + (dc_yl-centery)*fracstep; 

    // Here we do an additional index re-mapping.
    if (TC_INRANGE(dc_colormap))		// dual-write the smooth 32-bit view
    {
	unsigned int*	cm32  = colormap32 + (dc_colormap - colormaps);
	unsigned int*	dst32 = screen32   + (dest - screens[0]);
	do {
	    byte tp = dc_translation[dc_source[frac>>FRACBITS]];
	    *dest = dc_colormap[tp];  *dst32 = cm32[tp];
	    dest += SCREENWIDTH;      dst32 += SCREENWIDTH;
	    frac += fracstep;
	} while (count--);
	return;
    }
    do
    {
	// Translation tables are used
	//  to map certain colorramps to other ones,
	//  used with PLAY sprites.
	// Thus the "green" ramp of the player 0 sprite
	//  is mapped to gray, red, black/indigo.
	*dest = dc_colormap[dc_translation[dc_source[frac>>FRACBITS]]];
	dest += SCREENWIDTH;

	frac += fracstep;
    } while (count--);
}




//
// R_InitTranslationTables
// Creates the translation tables to map
//  the green color ramp to gray, brown, red.
// Assumes a given structure of the PLAYPAL.
// Could be read from a lump instead.
//
void R_InitTranslationTables (void)
{
    int		i;
	
    translationtables = Z_Malloc (256*3+255, PU_STATIC, 0);
    translationtables = (byte *)(( (intptr_t)translationtables + 255 )& ~255);
    
    // translate just the 16 green colors
    for (i=0 ; i<256 ; i++)
    {
	if (i >= 0x70 && i<= 0x7f)
	{
	    // map green ramp to gray, brown, red
	    translationtables[i] = 0x60 + (i&0xf);
	    translationtables [i+256] = 0x40 + (i&0xf);
	    translationtables [i+512] = 0x20 + (i&0xf);
	}
	else
	{
	    // Keep all other colors as is.
	    translationtables[i] = translationtables[i+256] 
		= translationtables[i+512] = i;
	}
    }
}




//
// R_DrawSpan 
// With DOOM style restrictions on view orientation,
//  the floors and ceilings consist of horizontal slices
//  or spans with constant z depth.
// However, rotation around the world z axis is possible,
//  thus this mapping, while simpler and faster than
//  perspective correct texture mapping, has to traverse
//  the texture at an angle in all but a few cases.
// In consequence, flats are not stored by column (like walls),
//  and the inner loop has to step in texture space u and v.
//
int			ds_y; 
int			ds_x1; 
int			ds_x2;

lighttable_t*		ds_colormap; 

// --- 2D ordered light dithering (Options -> Video -> Light Dither) --------------------------
// Softens the hard 8-bit colormap banding: each pixel picks between the two nearest colormap
// levels (dc/ds_colormap = brighter, *_colormap2 = next darker) using a 4x4 Bayer threshold vs
// the fractional light (*_litfrac, 0-15).  Only walls + flats are dithered (r_segs / r_plane).
lighttable_t*		dc_colormap2;
int			dc_litfrac;
lighttable_t*		ds_colormap2;
int			ds_litfrac;
int			dither_lighting = 0;	// config toggle
int			r_dither_on = 0;		// dither_lighting && normal detail (per frame)

static const unsigned char r_dith[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

void R_DrawColumnDither (void)
{
    int		count = dc_yh - dc_yl;
    byte*	dest;
    fixed_t	frac, fracstep;
    const unsigned char* drow;
    int		y;
    if (count < 0) return;
    dest = ylookup[dc_yl] + columnofs[dc_x];
    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl-centery)*fracstep;
    drow = r_dith[dc_x & 3];
    y = dc_yl;
    {
	int heightmask = dc_texheight - 1;
	unsigned int* dst32 = truecolor ? screen32 + (dest - screens[0]) : NULL;
	if (dc_texheight & heightmask)		// non-power-of-two: modulo wrap
	{
	    heightmask = dc_texheight << FRACBITS;
	    if (frac < 0) while ((frac += heightmask) < 0) ;
	    else          while (frac >= heightmask) frac -= heightmask;
	    do {
		lighttable_t* cm = (dc_litfrac > drow[y & 3]) ? dc_colormap2 : dc_colormap;
		*dest = cm[dc_source[frac>>FRACBITS]];
		if (dst32) { *dst32 = colormap32[*dest]; dst32 += SCREENWIDTH; }
		dest += SCREENWIDTH; y++;
		if ((frac += fracstep) >= heightmask) frac -= heightmask;
	    } while (count--);
	}
	else
	{
	    do {
		lighttable_t* cm = (dc_litfrac > drow[y & 3]) ? dc_colormap2 : dc_colormap;
		*dest = cm[dc_source[(frac>>FRACBITS) & heightmask]];
		if (dst32) { *dst32 = colormap32[*dest]; dst32 += SCREENWIDTH; }
		dest += SCREENWIDTH;
		frac += fracstep;
		y++;
	    } while (count--);
	}
    }
}

void R_DrawSpanDither (void)
{
    fixed_t	xfrac = ds_xfrac, yfrac = ds_yfrac;
    byte*	dest;
    int		count, spot, x, sy = ds_y & 3;
    dest = ylookup[ds_y] + columnofs[ds_x1];
    count = ds_x2 - ds_x1;
    x = ds_x1;
    {
	unsigned int* dst32 = truecolor ? screen32 + (dest - screens[0]) : NULL;
	do {
	    lighttable_t* cm = (ds_litfrac > r_dith[x & 3][sy]) ? ds_colormap2 : ds_colormap;
	    spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);
	    *dest++ = cm[ds_source[spot]];
	    if (dst32) *dst32++ = colormap32[dest[-1]];
	    xfrac += ds_xstep;
	    yfrac += ds_ystep;
	    x++;
	} while (count--);
    }
}


fixed_t			ds_xfrac; 
fixed_t			ds_yfrac; 
fixed_t			ds_xstep; 
fixed_t			ds_ystep;

// start of a 64*64 tile image 
byte*			ds_source;	

// just for profiling
int			dscount;


//
// Draws the actual span.
void R_DrawSpan (void) 
{ 
    fixed_t		xfrac;
    fixed_t		yfrac; 
    byte*		dest; 
    int			count;
    int			spot; 
	 
#ifdef RANGECHECK 
    if (ds_x2 < ds_x1
	|| ds_x1<0
	|| ds_x2>=SCREENWIDTH  
	|| (unsigned)ds_y>SCREENHEIGHT)
    {
	I_Error( "R_DrawSpan: %i to %i at %i",
		 ds_x1,ds_x2,ds_y);
    }
//	dscount++; 
#endif 

    
    xfrac = ds_xfrac; 
    yfrac = ds_yfrac; 
	 
    dest = ylookup[ds_y] + columnofs[ds_x1];

    // We do not check for zero spans here?
    count = ds_x2 - ds_x1;

    if (TC_INRANGE(ds_colormap))		// dual-write the smooth 32-bit view
    {
	unsigned int*	cm32  = colormap32 + (ds_colormap - colormaps);
	unsigned int*	dst32 = screen32   + (dest - screens[0]);
	do
	{
	    byte px;
	    spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);
	    px = ds_source[spot];
	    *dest++  = ds_colormap[px];
	    *dst32++ = cm32[px];
	    xfrac += ds_xstep;
	    yfrac += ds_ystep;
	} while (count--);
	return;
    }

    do
    {
	// Current texture index in u,v.
	spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);

	// Lookup pixel from flat texture tile,
	//  re-index using light/colormap.
	*dest++ = ds_colormap[ds_source[spot]];

	// Next step in u,v.
	xfrac += ds_xstep;
	yfrac += ds_ystep;

    } while (count--);
}



// UNUSED.
// Loop unrolled by 4.
#if 0
void R_DrawSpan (void) 
{ 
    unsigned	position, step;

    byte*	source;
    byte*	colormap;
    byte*	dest;
    
    unsigned	count;
    usingned	spot; 
    unsigned	value;
    unsigned	temp;
    unsigned	xtemp;
    unsigned	ytemp;
		
    position = ((ds_xfrac<<10)&0xffff0000) | ((ds_yfrac>>6)&0xffff);
    step = ((ds_xstep<<10)&0xffff0000) | ((ds_ystep>>6)&0xffff);
		
    source = ds_source;
    colormap = ds_colormap;
    dest = ylookup[ds_y] + columnofs[ds_x1];	 
    count = ds_x2 - ds_x1 + 1; 
	
    while (count >= 4) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[0] = colormap[source[spot]]; 

	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[1] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[2] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[3] = colormap[source[spot]]; 
		
	count -= 4;
	dest += 4;
    } 
    while (count > 0) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	*dest++ = colormap[source[spot]]; 
	count--;
    } 
} 
#endif

//
// R_InitBuffer 
// Creats lookup tables that avoid
//  multiplies and other hazzles
//  for getting the framebuffer address
//  of a pixel to draw.
//
void
R_InitBuffer
( int		width,
  int		height ) 
{ 
    int		i; 

    // Handle resize,
    //  e.g. smaller view windows
    //  with border and/or status bar.
    viewwindowx = (SCREENWIDTH-width) >> 1; 

    // Column offset. For windows.
    for (i=0 ; i<width ; i++) 
	columnofs[i] = viewwindowx + i;

    // Samw with base row offset.
    if (width == SCREENWIDTH)
	viewwindowy = 0;
    else
	viewwindowy = (SCREENHEIGHT-SBARHEIGHT*hires-height) >> 1;

    // Preclaculate all row offsets.
    for (i=0 ; i<height ; i++) 
	ylookup[i] = screens[0] + (i+viewwindowy)*SCREENWIDTH; 
} 
 
 


//
// R_FillBackScreen
// Fills the back screen with a pattern
//  for variable screen sizes
// Also draws a beveled edge.
//
void R_FillBackScreen (void) 
{ 
    byte*	src;
    byte*	dest; 
    int		x;
    int		y; 
    patch_t*	patch;

    // DOOM border patch.
    char	name1[] = "FLOOR7_2";

    // DOOM II border patch.
    char	name2[] = "GRNROCK";	

    char*	name;
	
    // 3D view fills the whole width -> no border needed.
    if (scaledviewwidth == SCREENWIDTH)
	return;

    if ( gamemode == commercial)
	name = name2;
    else
	name = name1;
    
    src = W_CacheLumpName (name, PU_CACHE); 
    dest = screens[1]; 
	 
    for (y=0 ; y<SCREENHEIGHT-SBARHEIGHT*hires ; y++)
    {
	for (x=0 ; x<SCREENWIDTH/64 ; x++)
	{ 
	    memcpy (dest, src+((y&63)<<6), 64); 
	    dest += 64; 
	} 

	if (SCREENWIDTH&63) 
	{ 
	    memcpy (dest, src+((y&63)<<6), SCREENWIDTH&63); 
	    dest += (SCREENWIDTH&63); 
	} 
    } 
	
    // The border edge patches are drawn through the scaling V_DrawPatch, so
    // use the view rectangle expressed in base 320x200 coordinates.
    {
	int	vx = viewwindowx / hires;
	int	vy = viewwindowy / hires;
	int	vw = scaledviewwidth / hires;
	int	vh = viewheight / hires;

	patch = W_CacheLumpName ("brdr_t",PU_CACHE);
	for (x=0 ; x<vw ; x+=8)
	    V_DrawPatch (vx+x,vy-8,1,patch);
	patch = W_CacheLumpName ("brdr_b",PU_CACHE);
	for (x=0 ; x<vw ; x+=8)
	    V_DrawPatch (vx+x,vy+vh,1,patch);
	patch = W_CacheLumpName ("brdr_l",PU_CACHE);
	for (y=0 ; y<vh ; y+=8)
	    V_DrawPatch (vx-8,vy+y,1,patch);
	patch = W_CacheLumpName ("brdr_r",PU_CACHE);
	for (y=0 ; y<vh ; y+=8)
	    V_DrawPatch (vx+vw,vy+y,1,patch);

	// Draw beveled edge.
	V_DrawPatch (vx-8, vy-8, 1, W_CacheLumpName ("brdr_tl",PU_CACHE));
	V_DrawPatch (vx+vw, vy-8, 1, W_CacheLumpName ("brdr_tr",PU_CACHE));
	V_DrawPatch (vx-8, vy+vh, 1, W_CacheLumpName ("brdr_bl",PU_CACHE));
	V_DrawPatch (vx+vw, vy+vh, 1, W_CacheLumpName ("brdr_br",PU_CACHE));
    }
}
 

//
// Copy a screen buffer.
//
void
R_VideoErase
( unsigned	ofs,
  int		count ) 
{ 
  // LFB copy.
  // This might not be a good idea if memcpy
  //  is not optiomal, e.g. byte by byte on
  //  a 32bit CPU, as GNU GCC/Linux libc did
  //  at one point.
    memcpy (screens[0]+ofs, screens[1]+ofs, count); 
} 


//
// R_DrawViewBorder
// Draws the border around the view
//  for different size windows?
//
void
V_MarkRect
( int		x,
  int		y,
  int		width,
  int		height ); 
 
void R_DrawViewBorder (void) 
{ 
    int		top;
    int		side;
    int		ofs;
    int		i; 
 
    if (scaledviewwidth == SCREENWIDTH) 
	return; 
  
    top = ((SCREENHEIGHT-SBARHEIGHT*hires)-viewheight)/2;
    side = (SCREENWIDTH-scaledviewwidth)/2;
 
    // copy top and one line of left side 
    R_VideoErase (0, top*SCREENWIDTH+side); 
 
    // copy one line of right side and bottom 
    ofs = (viewheight+top)*SCREENWIDTH-side; 
    R_VideoErase (ofs, top*SCREENWIDTH+side); 
 
    // copy sides using wraparound 
    ofs = top*SCREENWIDTH + SCREENWIDTH-side; 
    side <<= 1;
    
    for (i=1 ; i<viewheight ; i++) 
    { 
	R_VideoErase (ofs, side); 
	ofs += SCREENWIDTH; 
    } 

    // ? 
    V_MarkRect (0,0,SCREENWIDTH, SCREENHEIGHT-SBARHEIGHT); 
} 
 
 
