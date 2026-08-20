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
// Revision 1.3  1997/01/29 20:10
// DESCRIPTION:
//	Preparation of data for rendering,
//	generation of lookups, caching, retrieval by name.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: r_data.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";

#ifdef __BEOS__
#ifdef __GNUC__
extern void *alloca(int);
#else
#include <alloca.h>
#endif
#endif /* __BEOS__ */

#ifdef _WIN32
#include <malloc.h>		/* MSVC: alloca is the _alloca intrinsic */
#define alloca _alloca
#endif

#include <stdint.h>

#include "m_swap.h"
#include "v_video.h"		// V_PNGLumpInfo / V_PNGLumpDecodeCached (PNG sprites)

#include "i_system.h"
#include "z_zone.h"

#include "w_wad.h"

#include "doomdef.h"
#include "r_local.h"
#include "p_local.h"

#include "doomstat.h"
#include "r_sky.h"

#include "r_data.h"

//
// Graphics.
// DOOM graphics for walls and sprites
// is stored in vertical runs of opaque pixels (posts).
// A column is composed of zero or more posts,
// a patch or sprite is composed of zero or more columns.
// 



//
// Texture definition.
// Each texture is composed of one or more patches,
// with patches being lumps stored in the WAD.
// The lumps are referenced by number, and patched
// into the rectangular texture space using origin
// and possibly other attributes.
//
typedef struct
{
    short	originx;
    short	originy;
    short	patch;
    short	stepdir;
    short	colormap;
} mappatch_t;


//
// Texture definition.
// A DOOM wall texture is a list of patches
// which are to be combined in a predefined order.
//
typedef struct
{
    char		name[8];
    boolean		masked;	
    short		width;
    short		height;
    int			columndirectory;	// OBSOLETE (4-byte on-disk placeholder)
    short		patchcount;
    mappatch_t	patches[1];
} maptexture_t;


// Strife stores the same two records SHORTER on disk: maptexture_t drops the obsolete
// columndirectory (18-byte header instead of 22) and mappatch_t drops stepdir/colormap
// (6 bytes instead of 10).  Read a Strife TEXTURE1 through the Doom structs and every
// patchcount comes out as the old columndirectory field -- 0 for the first texture --
// after which the walk runs off the lump and R_InitTextures segfaults, which is exactly
// what strife1.wad did before this.  Verified against strife1.wad: all 220 entries
// parse contiguously under this layout, none under Doom's.
typedef struct
{
    short	originx;
    short	originy;
    short	patch;
} mappatch_strife_t;

typedef struct
{
    char			name[8];
    boolean			masked;
    short			width;
    short			height;
    short			patchcount;
    mappatch_strife_t		patches[1];
} maptexture_strife_t;


// A single patch from a texture definition,
//  basically a rectangular area within
//  the texture rectangle.
typedef struct
{
    // Block origin (allways UL),
    // which has allready accounted
    // for the internal origin of the patch.
    int		originx;	
    int		originy;
    int		patch;
} texpatch_t;


// A maptexturedef_t describes a rectangular texture,
//  which is composed of one or more mappatch_t structures
//  that arrange graphic patches.
typedef struct
{
    // Keep name for switch changing, etc.
    char	name[8];		
    short	width;
    short	height;
    
    // All the patches[patchcount]
    //  are drawn back to front into the cached texture.
    short	patchcount;
    texpatch_t	patches[1];		
    
} texture_t;



int		firstflat;
int		lastflat;
int		numflats;
int*		flatlumps;	// flat index -> lump number (merged across all F_*..F_END, like spritelumps[])
static byte*	flatpng;	// per flat: the lump is a PNG and needs decoding
static void**	flatcache;	// per flat: decoded 64x64 buffer (PU_CACHE; NULL = not resident)

int		firstpatch;
int		lastpatch;
int		numpatches;

int		firstspritelump;
int		lastspritelump;
int		numspritelumps;
int*		spritelumps;	// sprite index -> lump number (merged across all S_*..S_END)
void**		spritepatch;	// sprite index -> converted PNG patch_t (NULL = use the raw lump)
hdimage_t*	hdsprite;	// sprite index -> full-colour ARGB image (truecolor), or {0}

extern patch_t*	V_PNGLumpToPatch (int lump);	// v_png.c -- GZDoom PNG sprite -> paletted patch
extern patch_t*	V_PNGLumpDecode (int lump, unsigned int** rgba_out, int* w_out, int* h_out);

int		numtextures;
texture_t**	textures;


int*			texturewidthmask;
// needed for texture pegging
fixed_t*		textureheight;		
int*			texturecompositesize;
short**			texturecolumnlump;
// 32-bit offsets: tall/wide patch lumps exceed 64 KB, so a 16-bit column
// offset truncated (ZZZGATE 512x512 -> right half garbled).
unsigned**		texturecolumnofs;
byte**			texturecomposite;

// for global animation
int*		flattranslation;
int*		texturetranslation;

// needed for pre rendering
fixed_t*	spritewidth;	
fixed_t*	spriteoffset;
fixed_t*	spritetopoffset;

lighttable_t	*colormaps;


//
// MAPTEXTURE_T CACHING
// When a texture is first needed,
//  it counts the number of composite columns
//  required in the texture and allocates space
//  for a column directory and any new columns.
// The directory will simply point inside other patches
//  if there is only one patch in a given column,
//  but any columns with multiple patches
//  will have new column_ts generated.
//



//
// R_DrawColumnInCache
// Clip and draw a column
//  from a patch into a cached post.
//
void
R_DrawColumnInCache
( column_t*	patch,
  byte*		cache,
  int		originy,
  int		cacheheight )
{
    int		count;
    int		position;
    byte*	source;
    int		topdelta = -1;		// DeePsea tall-patch: track last absolute delta

    while (patch->topdelta != 0xff)
    {
	// Tall patches (>254 rows) encode topdelta cumulatively: if this post's
	// topdelta is <= the previous, it is a relative continuation.
	if (patch->topdelta <= topdelta)
	    topdelta += patch->topdelta;
	else
	    topdelta = patch->topdelta;

	source = (byte *)patch + 3;
	count = patch->length;
	position = originy + topdelta;

	if (position < 0)
	{
	    count += position;
	    position = 0;
	}

	if (position + count > cacheheight)
	    count = cacheheight - position;

	if (count > 0)
	    memcpy (cache + position, source, count);
		
	patch = (column_t *)(  (byte *)patch + patch->length + 4); 
    }
}



//
// R_CachePatchForComposite
// A texture patch, ready to read columns from.
//
// PNAMES resolves with a plain W_CheckNumForName (no namespace restriction), so a PWAD
// that stores its wall patches as PNG lands raw file bytes here -- and the composite code
// would then take columnofs[] out of the PNG signature and index far outside the lump.
// Decode those into a real patch_t first.
//
// *tmp is set when the result is a fresh block the caller must Z_Free; raw Doom patches
// stay zone-cached exactly as before.  NULL = unusable patch, skip it.
//
static patch_t* R_CachePatchForComposite (int lump, boolean* tmp)
{
    *tmp = false;
    if (V_IsPNGLump (lump))
    {
	patch_t* p = V_PNGLumpToPatch (lump);		// PU_STATIC, ours to free
	if (!p) return NULL;
	*tmp = true;
	return p;
    }
    return W_CacheLumpNum (lump, PU_CACHE);
}


//
// R_GenerateComposite
// Using the texture definition,
//  the composite texture is created from the patches,
//  and each column is cached.
//
void R_GenerateComposite (int texnum)
{
    byte*		block;
    texture_t*		texture;
    texpatch_t*		patch;	
    patch_t*		realpatch;
    int			x;
    int			x1;
    int			x2;
    int			i;
    column_t*		patchcol;
    short*		collump;
    unsigned*		colofs;
	
    texture = textures[texnum];

    block = Z_Malloc (texturecompositesize[texnum],
		      PU_STATIC,
		      &texturecomposite[texnum]);

    // VANILLA BUG (tutti-frutti): zero-fill so rows not covered by any patch are
    // deterministic (palette 0) instead of whatever heap garbage Z_Malloc left.
    memset (block, 0, texturecompositesize[texnum]);

    collump = texturecolumnlump[texnum];
    colofs = texturecolumnofs[texnum];
    
    // Composite the columns together.
    patch = texture->patches;
		
    for (i=0 , patch = texture->patches;
	 i<texture->patchcount;
	 i++, patch++)
    {
	boolean	rp_tmp;

	realpatch = R_CachePatchForComposite (patch->patch, &rp_tmp);
	if (!realpatch)
	    continue;
	x1 = patch->originx;
	x2 = x1 + SHORT(realpatch->width);

	if (x1<0)
	    x = 0;
	else
	    x = x1;
	
	if (x2 > texture->width)
	    x2 = texture->width;

	for ( ; x<x2 ; x++)
	{
	    // Column does not have multiple patches?
	    if (collump[x] >= 0)
		continue;
	    
	    patchcol = (column_t *)((byte *)realpatch
				    + LONG(realpatch->columnofs[x-x1]));
	    R_DrawColumnInCache (patchcol,
				 block + colofs[x],
				 patch->originy,
				 texture->height);
	}
	if (rp_tmp)
	    Z_Free (realpatch);
    }

    // Now that the texture has been built in column cache,
    //  it is purgable from zone memory.
    Z_ChangeTag (block, PU_CACHE);
}



//
// R_GenerateLookup
//
void R_GenerateLookup (int texnum)
{
    texture_t*		texture;
    byte*		patchcount;	// patchcount[texture->width]
    texpatch_t*		patch;	
    patch_t*		realpatch;
    int			x;
    int			x1;
    int			x2;
    int			i;
    short*		collump;
    unsigned*		colofs;
    // VANILLA BUG (tutti-frutti): vanilla read single-patch columns directly
    // from the WAD patch; if the patch didn't span the full texture height the
    // column drawer ran past it -> garbage stripe.  Track each column's
    // covering-patch vertical span so partially-covered columns can be forced
    // through the (zero-filled) composite path.  Normal full-height columns are
    // unaffected (still direct); worst case is just more compositing.
    short*		coltop;
    short*		colbot;
    byte*		colpng;		// column is fed by a PNG patch -> must be composited

    texture = textures[texnum];

    // Composited texture not created yet.
    texturecomposite[texnum] = 0;

    texturecompositesize[texnum] = 0;
    collump = texturecolumnlump[texnum];
    colofs = texturecolumnofs[texnum];

    // Now count the number of columns
    //  that are covered by more than one patch.
    // Fill in the lump / offset, so columns
    //  with only a single patch are all done.
    patchcount = (byte *)alloca (texture->width);
    memset (patchcount, 0, texture->width);
    coltop = (short *)alloca (texture->width * sizeof(short));
    colbot = (short *)alloca (texture->width * sizeof(short));
    // Columns fed by a PNG patch must go through the composite path -- see below.
    colpng = (byte *)alloca (texture->width);
    memset (colpng, 0, texture->width);
    patch = texture->patches;
		
    for (i=0 , patch = texture->patches;
	 i<texture->patchcount;
	 i++, patch++)
    {
	boolean	rp_tmp;

	realpatch = R_CachePatchForComposite (patch->patch, &rp_tmp);
	if (!realpatch)
	    continue;
	x1 = patch->originx;
	x2 = x1 + SHORT(realpatch->width);
	
	if (x1 < 0)
	    x = 0;
	else
	    x = x1;

	if (x2 > texture->width)
	    x2 = texture->width;
	for ( ; x<x2 ; x++)
	{
	    patchcount[x]++;
	    collump[x] = patch->patch;
	    colofs[x] = LONG(realpatch->columnofs[x-x1])+3;
	    coltop[x] = patch->originy;
	    colbot[x] = patch->originy + SHORT(realpatch->height);
	    if (rp_tmp) colpng[x] = 1;
	}
	if (rp_tmp)
	    Z_Free (realpatch);
    }

    for (x=0 ; x<texture->width ; x++)
    {
	if (!patchcount[x])
	{
	    printf ("R_GenerateLookup: column without a patch (%s)\n",
		    texture->name);
	    return;
	}
	// I_Error ("R_GenerateLookup: column without a patch");

	// Composite the column if it has multiple patches OR a single patch
	// that doesn't span the full texture height (the latter is the
	// tutti-frutti fix -- vanilla only checked patchcount > 1) OR the
	// texture is taller than one post can hold (>254 rows): such a column
	// is stored as multiple posts (DeePsea tall patch) and cannot be read
	// directly as flat pixels, so it must go through the composite path.
	// ...OR the column comes from a PNG patch.  The single-patch shortcut records the
	// LUMP plus an offset into it and lets the renderer read the column straight out of
	// the file -- but a PNG's columns only exist in the decoded copy, which is freed the
	// moment this function returns.  Compositing is the only way those can be drawn.
	if (patchcount[x] > 1
	    || coltop[x] > 0
	    || colbot[x] < texture->height
	    || texture->height > 254
	    || colpng[x])
	{
	    // Use the cached block.
	    collump[x] = -1;
	    colofs[x] = texturecompositesize[texnum];

	    // 32-bit texturecompositesize + colofs -> no 64k texture limit.
	    texturecompositesize[texnum] += texture->height;
	}
    }	
}




//
// R_GetColumn
//
byte*
R_GetColumn
( int		tex,
  int		col )
{
    int		lump;
    int		ofs;
	
    col &= texturewidthmask[tex];
    lump = texturecolumnlump[tex][col];
    ofs = texturecolumnofs[tex][col];
    
    if (lump > 0)
	return (byte *)W_CacheLumpNum(lump,PU_CACHE)+ofs;

    if (!texturecomposite[tex])
	R_GenerateComposite (tex);

    return texturecomposite[tex] + ofs;
}


//
// R_GetMaskedColumn
//
// For a 2S middle texture, return a POSTED column_t* that R_DrawMaskedColumn can walk
// (topdelta/length posts + 0xff terminator), or NULL if the texture can't be drawn masked.
//
// A single-patch texture whose patch doesn't span the full texture height gets forced through
// the tutti-frutti composite path (R_GenerateLookup) -- but the composite is raw pixels with no
// posts, so it can't be masked (that's why BuddyDoom skipped it, making transparent single-patch
// signs like BOOMEDIT's "250TEXT" description shields vanish).  Here we read such a column
// straight from its source patch instead, which is already posted (transparent below the patch).
// (Assumes the patch's originy is 0 -- true for the sign textures; a nonzero originy would shift
// the posts, but multi-patch composites are genuinely un-maskable and return NULL.)
//
column_t*
R_GetMaskedColumn
( int		tex,
  int		col )
{
    texture_t*	t = textures[tex];

    col &= texturewidthmask[tex];

    if (texturecolumnlump[tex][col] > 0)		// ordinary direct single-patch column
	return (column_t *)((byte *)R_GetColumn (tex, col) - 3);

    if (t->patchcount == 1)				// tutti-frutti-composited single patch
    {
	texpatch_t*	tp = &t->patches[0];
	patch_t*	rp = W_CacheLumpNum (tp->patch, PU_CACHE);
	int		pc = col - tp->originx;
	if (pc >= 0 && pc < SHORT(rp->width))
	    return (column_t *)((byte *)rp + LONG(rp->columnofs[pc]));
    }

    return NULL;					// multi-patch composite: not maskable
}




//
// R_InitTextures
// Initializes the texture list
//  with the textures from the world map.
//
void R_InitTextures (void)
{
    maptexture_t*	mtexture;
    texture_t*		texture;
    mappatch_t*		mpatch;
    texpatch_t*		patch;

    int			i;
    int			j;

    int*		maptex;
    int*		maptex2;
    int*		maptex1;
    
    char		name[9];
    char*		names;
    char*		name_p;
    
    int*		patchlookup;
    
    int			totalwidth;
    int			nummappatches;
    int			offset;
    int			maxoff;
    int			maxoff2;
    int			numtextures1;
    int			numtextures2;

    int*		directory;
    
    int			temp1;
    int			temp2;
    int			temp3;

    
    // Load the patch names from pnames.lmp.
    name[8] = 0;	
    names = W_CacheLumpName ("PNAMES", PU_STATIC);
    nummappatches = LONG ( *((int *)names) );
    name_p = names+4;
    patchlookup = alloca (nummappatches*sizeof(*patchlookup));
    
    for (i=0 ; i<nummappatches ; i++)
    {
	strncpy (name,name_p+i*8, 8);
	patchlookup[i] = W_CheckNumForName (name);
    }
    Z_Free (names);
    
    // Load the map texture definitions from textures.lmp.
    // The data is contained in one or two lumps,
    //  TEXTURE1 for shareware, plus TEXTURE2 for commercial.
    maptex = maptex1 = W_CacheLumpName ("TEXTURE1", PU_STATIC);
    numtextures1 = LONG(*maptex);
    maxoff = W_LumpLength (W_GetNumForName ("TEXTURE1"));
    directory = maptex+1;
	
    if (W_CheckNumForName ("TEXTURE2") != -1)
    {
	maptex2 = W_CacheLumpName ("TEXTURE2", PU_STATIC);
	numtextures2 = LONG(*maptex2);
	maxoff2 = W_LumpLength (W_GetNumForName ("TEXTURE2"));
    }
    else
    {
	maptex2 = NULL;
	numtextures2 = 0;
	maxoff2 = 0;
    }
    numtextures = numtextures1 + numtextures2;
	
    textures = Z_Malloc (numtextures*sizeof(*textures), PU_STATIC, 0);
    texturecolumnlump = Z_Malloc (numtextures*sizeof(*texturecolumnlump), PU_STATIC, 0);
    texturecolumnofs = Z_Malloc (numtextures*sizeof(*texturecolumnofs), PU_STATIC, 0);
    texturecomposite = Z_Malloc (numtextures*sizeof(*texturecomposite), PU_STATIC, 0);
    texturecompositesize = Z_Malloc (numtextures*sizeof(*texturecompositesize), PU_STATIC, 0);
    texturewidthmask = Z_Malloc (numtextures*sizeof(*texturewidthmask), PU_STATIC, 0);
    textureheight = Z_Malloc (numtextures*sizeof(*textureheight), PU_STATIC, 0);

    totalwidth = 0;
    
    //	Really complex printing shit...
    temp1 = W_GetNumForName ("S_START");  // P_???????
    temp2 = W_GetNumForName ("S_END") - 1;
    temp3 = ((temp2-temp1+63)/64) + ((numtextures+63)/64);
    printf("[");
    for (i = 0; i < temp3; i++)
	printf(" ");
    printf("         ]");
    for (i = 0; i < temp3; i++)
	printf("\x8");
    printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");	
	
    for (i=0 ; i<numtextures ; i++, directory++)
    {
	if (!(i&63))
	    printf (".");

	if (i == numtextures1)
	{
	    // Start looking in second texture file.
	    maptex = maptex2;
	    maxoff = maxoff2;
	    directory = maptex+1;
	}

	offset = LONG(*directory);

	if (offset > maxoff)
	    I_Error ("R_InitTextures: bad texture directory");
	
	mtexture = (maptexture_t *) ( (byte *)maptex + offset);

	// Pull the header fields out first, because Strife's on-disk layout is shorter
	// (see maptexture_strife_t above).  The patch entries differ only in STRIDE --
	// originx/originy/patch are the first three shorts either way -- so the loop
	// below walks raw bytes and reads them through mappatch_t regardless.
	{
	    byte* mp_raw;
	    int   mp_stride, tex_w, tex_h, tex_pc;
	    char* tex_name;

	    if (strife_mode)
	    {
		maptexture_strife_t* st = (maptexture_strife_t *) ( (byte *)maptex + offset);
		tex_name  = st->name;
		tex_w     = SHORT(st->width);
		tex_h     = SHORT(st->height);
		tex_pc    = SHORT(st->patchcount);
		mp_raw    = (byte *) &st->patches[0];
		mp_stride = (int) sizeof (mappatch_strife_t);
	    }
	    else
	    {
		tex_name  = mtexture->name;
		tex_w     = SHORT(mtexture->width);
		tex_h     = SHORT(mtexture->height);
		tex_pc    = SHORT(mtexture->patchcount);
		mp_raw    = (byte *) &mtexture->patches[0];
		mp_stride = (int) sizeof (mappatch_t);
	    }

	    texture = textures[i] =
		Z_Malloc (sizeof(texture_t)
			  + sizeof(texpatch_t)*(tex_pc-1),
			  PU_STATIC, 0);

	    texture->width = tex_w;
	    texture->height = tex_h;
	    texture->patchcount = tex_pc;

	    /* memcpy() generates a BUS error on Solaris with optimization on */
	    { char *src; char *dst;
	      src = tex_name;
	      dst = (char *)texture->name;
	      for (j=0; j<sizeof(texture->name); ++j )
		*dst++ = *src++;
	    }

	    patch = &texture->patches[0];

	    for (j=0 ; j<texture->patchcount ; j++, mp_raw += mp_stride, patch++)
	    {
		mpatch = (mappatch_t *) mp_raw;
		patch->originx = SHORT(mpatch->originx);
		patch->originy = SHORT(mpatch->originy);
		patch->patch = patchlookup[SHORT(mpatch->patch)];
		if (patch->patch == -1)
		{
		    I_Error ("R_InitTextures: Missing patch in texture %s",
			     texture->name);
		}
	    }
	}
	texturecolumnlump[i] = Z_Malloc (texture->width*sizeof(short), PU_STATIC,0);
	texturecolumnofs[i] = Z_Malloc (texture->width*sizeof(unsigned), PU_STATIC,0);

	j = 1;
	while (j*2 <= texture->width)
	    j<<=1;

	texturewidthmask[i] = j-1;
	textureheight[i] = texture->height<<FRACBITS;
		
	totalwidth += texture->width;
    }

    Z_Free (maptex1);
    if (maptex2)
	Z_Free (maptex2);
    
    // Precalculate whatever possible.	
    for (i=0 ; i<numtextures ; i++)
	R_GenerateLookup (i);
    
    // Create translation table for global animation.
    texturetranslation = Z_Malloc ((numtextures+1)*4, PU_STATIC, 0);
    
    for (i=0 ; i<numtextures ; i++)
	texturetranslation[i] = i;
}



//
// R_InitFlats
//
// A flat lives inside an F_START..F_END (or Boom FF_START..FF_END) region.  The IWAD
// nests sub-markers (F1_START/F1_END, F2_START/F2_END, ...) inside that region purely
// for organisation; those, like any *_START/*_END marker, are NOT flats.
#define IS_F_START(n) (!strncasecmp((n),"F_START",8) || !strncasecmp((n),"FF_START",8))
#define IS_F_END(n)   (!strncasecmp((n),"F_END",8)   || !strncasecmp((n),"FF_END",8))
static boolean R_IsFlatMarker (const char* name)
{
    // 8-char, NUL-padded copy so strstr can't run off the fixed-size lump name.
    char nm[9]; nm[8] = 0; memcpy (nm, name, 8);
    return strstr (nm, "_START") != NULL || strstr (nm, "_END") != NULL;
}

void R_InitFlats (void)
{
    extern lumpinfo_t*	lumpinfo;
    extern int		numlumps;
    int			l, i, in_ns;

    // MERGE flat namespaces: collect the real flats inside EVERY F_START..F_END region,
    // skipping the sub-markers (F1_START, ...) and any empty region -- exactly as
    // R_InitSpriteLumps does for sprites.  The old "span first F_START to last F_END" trick
    // broke whenever a PWAD appended its own (often EMPTY) F_START/F_END at the end of the
    // file (e.g. e1-arenas.wad): the span then swallowed every intervening non-flat lump
    // (that PWAD's maps + another WAD's sprite/patch namespaces), inflating numflats from
    // ~100 to ~1700 and putting garbage lumps in the flat index space.
    numflats = 0; in_ns = 0;
    for (l = 0; l < numlumps; l++)
    {
	char* n = lumpinfo[l].name;
	if (IS_F_START(n)) { in_ns = 1; continue; }
	if (IS_F_END(n))   { in_ns = 0; continue; }
	if (in_ns && !R_IsFlatMarker(n)) numflats++;
    }

    flatlumps = Z_Malloc ((numflats ? numflats : 1) * sizeof(*flatlumps), PU_STATIC, 0);

    i = 0; in_ns = 0;
    for (l = 0; l < numlumps; l++)
    {
	char* n = lumpinfo[l].name;
	if (IS_F_START(n)) { in_ns = 1; continue; }
	if (IS_F_END(n))   { in_ns = 0; continue; }
	if (in_ns && !R_IsFlatMarker(n)) flatlumps[i++] = l;
    }

    // Legacy contiguous-range fields (kept for any code that still references them); flat
    // index -> lump now goes through flatlumps[] so non-contiguous PWAD flats work too.
    firstflat = numflats ? flatlumps[0] : 0;
    lastflat  = numflats ? flatlumps[numflats-1] : 0;

    // Create translation table for global animation.
    flattranslation = Z_Malloc ((numflats+1)*4, PU_STATIC, 0);
    for (i=0 ; i<numflats ; i++)
	flattranslation[i] = i;

    // PNG flats: do the magic check ONCE here, so the per-frame R_GetFlat is a table
    // lookup and not a lump peek.  flatpng[] marks which flats need decoding, flatcache[]
    // holds the decoded 64x64 buffer (PU_CACHE -- the zone may reclaim any of them).
    // Guarded: Z_Free is NOT free(3).  It derives the block header from the pointer and
    // reads it straight away, so a NULL argument faults -- and both of these are NULL on
    // the very first call, which is every startup.
    if (flatpng)   { Z_Free (flatpng);   flatpng   = NULL; }
    if (flatcache) { Z_Free (flatcache); flatcache = NULL; }
    if (numflats)
    {
	flatpng   = Z_Malloc (numflats, PU_STATIC, 0);
	flatcache = Z_Malloc (numflats * sizeof(*flatcache), PU_STATIC, 0);
	for (i = 0; i < numflats; i++)
	{
	    flatpng[i]   = V_IsPNGLump (flatlumps[i]) ? 1 : 0;
	    flatcache[i] = NULL;
	}
    }
}

//
// R_GetFlat
// The 64x64 span source for a flat index.  Raw Doom flats come straight from the lump as
// they always did; a PNG flat is decoded once and kept in flatcache[] until the zone
// reclaims it (the slot is the Z_Malloc user, so Z_Free NULLs it and the next call simply
// decodes again).
//
// `tag` is the zone tag for the RAW path only, so each caller keeps the lifetime it had:
// the renderer pins PU_STATIC for the duration of a visplane and tags it back afterwards,
// the automap just borrows PU_CACHE.  A decoded PNG is always PU_CACHE against its own
// cache slot -- pinning one would defeat the point of decoding it lazily.
//
// Returns NULL when a PNG flat cannot be used -- wrong size, or a decode failure.  Callers
// must handle that and skip drawing rather than pass a garbage pointer on.
//
byte* R_GetFlat (int flatnum, int tag)
{
    if (flatnum < 0 || flatnum >= numflats)
	return NULL;
    if (!flatpng || !flatpng[flatnum])
	return W_CacheLumpNum (flatlumps[flatnum], tag);
    if (!flatcache[flatnum])
	V_PNGLumpToFlat (flatlumps[flatnum], &flatcache[flatnum]);
    return (byte*) flatcache[flatnum];
}


//
// R_InitSpriteLumps
// Finds the width and hoffset of all sprites in the wad,
//  so the sprite does not need to be cached completely
//  just for having the header info ready during rendering.
//
void R_InitSpriteLumps (void)
{
    int		i, l, in_ns;
    patch_t	*patch;

    // MERGE sprite namespaces: collect lumps inside EVERY S_START..S_END (and Boom's
    // SS_*) region, not just the last one.  Without this, a sprite PWAD's own marker
    // pair becomes the active range and SHADOWS all the IWAD sprites -- which is why
    // add-on sprite WADs had to embed a full copy of the IWAD sprites (the doom2stuff /
    // hereticstuff "superset" hack).  With the merge they can carry only their own
    // sprites.  spritelumps[i] maps a sprite index -> its lump number (no longer a
    // contiguous range), so all sprite indexing goes through it.
    #define IS_S_START(n) (!strncasecmp((n),"S_START",8) || !strncasecmp((n),"SS_START",8))
    #define IS_S_END(n)   (!strncasecmp((n),"S_END",8)   || !strncasecmp((n),"SS_END",8))

    // A GZDoom mod WAD stores its sprites as PNG (e.g. FRANK.wad's FRANA1..), which this
    // software renderer can't read directly.  We convert each PNG sprite lump to a paletted
    // patch_t at load (V_PNGLumpToPatch) and keep it in a parallel spritepatch[] side-table;
    // the sprite draw path (R_DrawVisSprite) uses that patch instead of the raw lump.  A PNG
    // that fails to decode gets a NULL slot and simply renders nothing (never a crash).
    #define IS_PNG_LUMP(l)  (W_LumpLength(l) >= 4 && \
        ((byte*)W_CacheLumpNum((l),PU_CACHE))[0]==0x89 && \
        ((byte*)W_CacheLumpNum((l),PU_CACHE))[1]=='P' && \
        ((byte*)W_CacheLumpNum((l),PU_CACHE))[2]=='N' && \
        ((byte*)W_CacheLumpNum((l),PU_CACHE))[3]=='G')

    numspritelumps = 0; in_ns = 0;
    for (l = 0; l < numlumps; l++)
    {
	if (IS_S_START(lumpinfo[l].name)) { in_ns = 1; continue; }
	if (IS_S_END  (lumpinfo[l].name)) { in_ns = 0; continue; }
	if (in_ns) numspritelumps++;
    }

    spritelumps     = Z_Malloc (numspritelumps*sizeof(int), PU_STATIC, 0);
    spritewidth     = Z_Malloc (numspritelumps*4, PU_STATIC, 0);
    spriteoffset    = Z_Malloc (numspritelumps*4, PU_STATIC, 0);
    spritetopoffset = Z_Malloc (numspritelumps*4, PU_STATIC, 0);
    spritepatch     = Z_Malloc (numspritelumps*sizeof(void*), PU_STATIC, 0);
    memset (spritepatch, 0, numspritelumps*sizeof(void*));
    hdsprite        = Z_Malloc (numspritelumps*sizeof(hdimage_t), PU_STATIC, 0);
    memset (hdsprite, 0, numspritelumps*sizeof(hdimage_t));

    i = 0; in_ns = 0;
    for (l = 0; l < numlumps; l++)
    {
	if (IS_S_START(lumpinfo[l].name)) { in_ns = 1; continue; }
	if (IS_S_END  (lumpinfo[l].name)) { in_ns = 0; continue; }
	if (!in_ns) continue;
	if (!(i&63)) printf (".");
	spritelumps[i] = l;
	if (IS_PNG_LUMP(l))
	{
	    // Header only -- do NOT decode here.  A whole-game PNG pack has thousands
	    // of sprites (hexenstuff.wad 1941 + strifestuff.wad 2002); decoding them
	    // all at load pinned ~11 MB of patches and ~210 MB of full-colour copies
	    // in a 48 MB zone, which is exactly the "Z_Malloc: failed on allocation"
	    // crash.  R_SpritePatch() decodes each one on first draw into a PURGEABLE
	    // block instead, so only what is on screen costs anything.
	    int pw = 0, ph = 0, lo = 0, to = 0;
	    if (V_PNGLumpInfo (l, &pw, &ph, &lo, &to))
	    {
		spritewidth[i]     = pw<<FRACBITS;
		spriteoffset[i]    = lo<<FRACBITS;
		spritetopoffset[i] = to<<FRACBITS;
	    }
	    else spritewidth[i] = spriteoffset[i] = spritetopoffset[i] = 0;
	    spritepatch[i] = NULL;			// decoded lazily
	    i++;
	    continue;
	}
	patch = W_CacheLumpNum (l, PU_CACHE);
	spritewidth[i]     = SHORT(patch->width)<<FRACBITS;
	spriteoffset[i]    = SHORT(patch->leftoffset)<<FRACBITS;
	spritetopoffset[i] = SHORT(patch->topoffset)<<FRACBITS;
	i++;
    }
    firstspritelump = numspritelumps ? spritelumps[0] : 0;	// legacy; indexing uses spritelumps[]
    lastspritelump  = numspritelumps ? spritelumps[numspritelumps-1] : 0;
    #undef IS_S_START
    #undef IS_S_END
    #undef IS_PNG_LUMP
}


//
// R_SpritePatch
// The patch for sprite index `idx`, whatever format it is stored in.  Ordinary
// Doom sprites come straight from the lump cache; a PNG sprite is decoded on first
// use into PU_CACHE blocks owned by spritepatch[idx] / hdsprite[idx].rgba, so the
// zone can reclaim any sprite that is not currently on screen and we decode it
// again next time it is.  Never returns NULL for a valid index (a PNG that fails
// to decode falls back to the raw lump, which simply renders nothing).
//
patch_t* R_SpritePatch (int idx)
{
    extern int	truecolor;		// i_video.c
    extern int	hd_sprites;		// r_things.c

    if ((unsigned) idx >= (unsigned) numspritelumps)
	return NULL;
    if (spritepatch[idx])
	return (patch_t*) spritepatch[idx];

    if (W_LumpLength (spritelumps[idx]) >= 4)
    {
	const byte* h = (const byte*) W_CacheLumpNum (spritelumps[idx], PU_CACHE);
	if (h[0]==0x89 && h[1]=='P' && h[2]=='N' && h[3]=='G')
	{
	    // Only keep the full-colour copy when the truecolor HD path can use it --
	    // otherwise it is ~50x the size of the patch, pinned for nothing.
	    int	 hw = 0, hh = 0;
	    void** ru = (truecolor && hd_sprites) ? (void**) &hdsprite[idx].rgba : NULL;
	    patch_t* p = V_PNGLumpDecodeCached (spritelumps[idx], &spritepatch[idx],
						ru, &hw, &hh);
	    if (p && ru)
		{ hdsprite[idx].w = hw; hdsprite[idx].h = hh; }
	    if (p)
		return p;
	}
    }
    return (patch_t*) W_CacheLumpNum (spritelumps[idx], PU_CACHE);
}



//
// R_InitColormaps
//
void R_InitColormaps (void)
{
    int	lump, length;
    
    // Load in the light tables, 
    //  256 byte align tables.
    lump = W_GetNumForName("COLORMAP"); 
    length = W_LumpLength (lump) + 255; 
    colormaps = Z_Malloc (length, PU_STATIC, 0); 
    colormaps = (byte *)( ((intptr_t)colormaps + 255)&~0xff);
    W_ReadLump (lump,colormaps); 
}



//
// R_InitData
// Locates all the lumps
//  that will be used by all views
// Must be called after W_Init.
//
void R_InitData (void)
{
    R_InitTextures ();
    printf ("\nInitTextures");
    R_InitFlats ();
    printf ("\nInitFlats");
    R_InitSpriteLumps ();
    printf ("\nInitSprites");
    R_InitColormaps ();
    printf ("\nInitColormaps");
    R_InitTranMap ();
    printf ("\nInitTranMap");
}


// Boom 260 translucency map: main_tranmap[bg*256 + fg] = the palette index closest to blending
// foreground colour fg (~66%) over background colour bg (~34%).  Loaded from a TRANMAP lump if the
// WAD provides one, else generated once from PLAYPAL at startup.
byte* main_tranmap = NULL;

void R_InitTranMap (void)
{
    int lump = W_CheckNumForName ("TRANMAP");
    byte* pal;
    int bg, fg, c;
    const int w1 = 168, w2 = 256 - 168;		// ~66% foreground

    if (lump != -1)					// WAD-supplied filter map
    {
	main_tranmap = W_CacheLumpNum (lump, PU_STATIC);
	return;
    }

    pal = W_CacheLumpName ("PLAYPAL", PU_STATIC);
    main_tranmap = Z_Malloc (256*256, PU_STATIC, 0);

    for (bg = 0; bg < 256; bg++)
    {
	int br = pal[bg*3+0], bgg = pal[bg*3+1], bb = pal[bg*3+2];
	for (fg = 0; fg < 256; fg++)
	{
	    int tr = (pal[fg*3+0]*w1 + br *w2) >> 8;
	    int tg = (pal[fg*3+1]*w1 + bgg*w2) >> 8;
	    int tb = (pal[fg*3+2]*w1 + bb *w2) >> 8;
	    int best = 0;
	    long bestd = 1L<<30;
	    for (c = 0; c < 256; c++)
	    {
		int dr = tr - pal[c*3+0], dg = tg - pal[c*3+1], db = tb - pal[c*3+2];
		long d = (long)dr*dr + (long)dg*dg + (long)db*db;
		if (d < bestd) { bestd = d; best = c; }
	    }
	    main_tranmap[bg*256 + fg] = (byte)best;
	}
    }
    Z_ChangeTag (pal, PU_CACHE);
}



//
// R_FlatNumForName
// Retrieval, get a flat number for a flat name.
//
// Like R_FlatNumForName but reports -1 instead of warning + defaulting to flat 0.
// P_InitPicAnims needs this: "is there a flat called BLOOD1?" is a real question,
// and answering it with 0 is what produced the bogus "bad cycle" I_Error.
int R_CheckFlatNumForName (char* name)
{
    extern lumpinfo_t*	lumpinfo;
    int			k;

    // Search from the end so a PWAD flat overriding an IWAD one of the same name
    // wins (mirrors W_CheckNumForName's last-match rule).
    for (k = numflats-1 ; k >= 0 ; k--)
	if (!strncasecmp (lumpinfo[flatlumps[k]].name, name, 8))
	    return k;
    return -1;
}

int R_FlatNumForName (char* name)
{
    extern lumpinfo_t*	lumpinfo;
    int			k;
    char		namet[9];

    // flatlumps[] is the merged, non-contiguous flat list, so a flat number is now a dense
    // index into it -- not lump-minus-firstflat.  Search from the end so a PWAD flat that
    // overrides an IWAD one of the same name wins (mirrors W_CheckNumForName's last-match).
    for (k = numflats-1 ; k >= 0 ; k--)
	if (!strncasecmp (lumpinfo[flatlumps[k]].name, name, 8))
	    return k;

    namet[8] = 0;
    memcpy (namet, name,8);
    fprintf(stderr, "Warning: R_FlatNumForName: %s not found, defaulting to 0\n", namet);
    return 0;
}




//
// R_CheckTextureNumForName
// Check whether texture is available.
// Filter out NoTexture indicator.
//
int	R_CheckTextureNumForName (char *name)
{
    int		i;

    // "NoTexture" marker.
    if (name[0] == '-')		
	return 0;
		
    for (i=0 ; i<numtextures ; i++)
	if (!I_strncasecmp (textures[i]->name, name, 8) )
	    return i;
		
    return -1;
}



//
// R_TextureNumForName
// Calls R_CheckTextureNumForName,
//  aborts with error message.
//
int	R_TextureNumForName (char* name)
{
    int		i;
	
    i = R_CheckTextureNumForName (name);

    if (i==-1)
    {
	fprintf(stderr, "Warning: R_TextureNumForName: %s not found, defaulting to 0\n", name);
	return 0;
    }
    return i;
}




//
// R_PrecacheLevel
// Preloads all relevant graphics for the level.
//
int		flatmemory;
int		texturememory;
int		spritememory;

void R_PrecacheLevel (void)
{
    char*		flatpresent;
    char*		texturepresent;
    char*		spritepresent;

    int			i;
    int			j;
    int			k;
    int			lump;
    
    texture_t*		texture;
    thinker_t*		th;
    spriteframe_t*	sf;

    if (demoplayback)
	return;
    
    // Precache flats.
    flatpresent = alloca(numflats);
    memset (flatpresent,0,numflats);	

    for (i=0 ; i<numsectors ; i++)
    {
	flatpresent[sectors[i].floorpic] = 1;
	flatpresent[sectors[i].ceilingpic] = 1;
    }
	
    flatmemory = 0;

    for (i=0 ; i<numflats ; i++)
    {
	if (flatpresent[i])
	{
	    lump = flatlumps[i];
	    flatmemory += lumpinfo[lump].size;
	    W_CacheLumpNum(lump, PU_CACHE);
	}
    }
    
    // Precache textures.
    texturepresent = alloca(numtextures);
    memset (texturepresent,0, numtextures);
	
    for (i=0 ; i<numsides ; i++)
    {
	texturepresent[sides[i].toptexture] = 1;
	texturepresent[sides[i].midtexture] = 1;
	texturepresent[sides[i].bottomtexture] = 1;
    }

    // Sky texture is always present.
    // Note that F_SKY1 is the name used to
    //  indicate a sky floor/ceiling as a flat,
    //  while the sky texture is stored like
    //  a wall texture, with an episode dependend
    //  name.
    texturepresent[skytexture] = 1;
	
    texturememory = 0;
    for (i=0 ; i<numtextures ; i++)
    {
	if (!texturepresent[i])
	    continue;

	texture = textures[i];
	
	for (j=0 ; j<texture->patchcount ; j++)
	{
	    lump = texture->patches[j].patch;
	    texturememory += lumpinfo[lump].size;
	    W_CacheLumpNum(lump , PU_CACHE);
	}
    }
    
    // Precache sprites.
    spritepresent = alloca(numsprites);
    memset (spritepresent,0, numsprites);
	
    for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
    {
	if (th->function.acp1 == (actionf_p1)P_MobjThinker)
	    spritepresent[((mobj_t *)th)->sprite] = 1;
    }
	
    spritememory = 0;
    for (i=0 ; i<numsprites ; i++)
    {
	if (!spritepresent[i])
	    continue;

	for (j=0 ; j<sprites[i].numframes ; j++)
	{
	    sf = &sprites[i].spriteframes[j];
	    for (k=0 ; k<8 ; k++)
	    {
		lump = spritelumps[sf->lump[k]];
		spritememory += lumpinfo[lump].size;
		W_CacheLumpNum(lump , PU_CACHE);
	    }
	}
    }
}




