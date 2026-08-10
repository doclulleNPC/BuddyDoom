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
//	Refresh of things, i.e. objects represented by sprites.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: r_things.c,v 1.5 1997/02/03 16:47:56 b1 Exp $";


#include <stdio.h>
#include <stdlib.h>


#include "doomdef.h"
#include "info.h"		/* NUMSPRITES */
#include "m_swap.h"

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "r_local.h"

#include "doomstat.h"
extern byte*	main_tranmap;	// r_data.c -- Boom 260 translucency map
#ifndef ST_HEXEN_HEIGHT
#define ST_HEXEN_HEIGHT 66	// Hexen bar: BASE_HEIGHT - H2BAR y (200-134)
#endif



#define MINZ				(FRACUNIT*4)
#define BASEYCENTER			100

//void R_DrawColumn (void);
//void R_DrawFuzzColumn (void);



typedef struct
{
    int		x1;
    int		x2;
	
    int		column;
    int		topclip;
    int		bottomclip;

} maskdraw_t;



//
// Sprite rotation 0 is facing the viewer,
//  rotation 1 is one angle turn CLOCKWISE around the axis.
// This is not the same as the angle,
//  which increases counter clockwise (protractor).
// There was a lot of stuff grabbed wrong, so I changed it...
//
fixed_t		pspritescale;
fixed_t		pspriteiscale;

lighttable_t**	spritelights;

// constant arrays
//  used for psprite clipping and initializing clipping
int		negonearray[MAXWIDTH];
int		screenheightarray[MAXWIDTH];


//
// INITIALIZATION FUNCTIONS
//

// variables used to look up
//  and range check thing_t sprites patches
spritedef_t*	sprites;
int		numsprites;

spriteframe_t	sprtemp[29];
int		maxframe;
char*		spritename;




//
// R_InstallSpriteLump
// Local function for R_InitSprites.
//
void
R_InstallSpriteLump
( int		lump,
  unsigned	frame,
  unsigned	rotation,
  boolean	flipped )
{
    int		r;
	
    if (frame >= 29 || rotation > 8)
	I_Error("R_InstallSpriteLump: "
		"Bad frame characters in lump %i", lump);
	
    if ((int)frame > maxframe)
	maxframe = frame;
		
    if (rotation == 0)
    {
	// the lump should be used for all rotations
	if (sprtemp[frame].rotate == false)
	    fprintf (stderr, "R_InitSprites: Sprite %s frame %c has "
		     "multip rot=0 lump (later wins)\n", spritename, 'A'+frame);

	if (sprtemp[frame].rotate == true)
	    fprintf (stderr, "R_InitSprites: Sprite %s frame %c has rotations "
		     "and a rot=0 lump (later wins)\n", spritename, 'A'+frame);

	sprtemp[frame].rotate = false;
	for (r=0 ; r<8 ; r++)
	{
	    sprtemp[frame].lump[r] = lump;	// `lump` is the merged sprite INDEX now
	    sprtemp[frame].flip[r] = (byte)flipped;
	}
	return;
    }

    // the lump is only used for one rotation
    if (sprtemp[frame].rotate == false)
	fprintf (stderr, "R_InitSprites: Sprite %s frame %c has rotations "
		 "and a rot=0 lump (later wins)\n", spritename, 'A'+frame);

    sprtemp[frame].rotate = true;
    rotation--;				// make 0 based
    if (sprtemp[frame].lump[rotation] != -1)
	fprintf (stderr, "R_InitSprites: Sprite %s : %c : %c "
		 "has two lumps mapped to it (later wins)\n", spritename, 'A'+frame, '1'+rotation);

    sprtemp[frame].lump[rotation] = lump;	// `lump` is the merged sprite INDEX now
    sprtemp[frame].flip[rotation] = (byte)flipped;
}




//
// R_InitSpriteDefs
// Pass a null terminated list of sprite names
//  (4 chars exactly) to be used.
// Builds the sprite rotation matrixes to account
//  for horizontally flipped sprites.
// Will report an error if the lumps are inconsistant. 
// Only called at startup.
//
// Sprite lump names are 4 characters for the actor,
//  a letter for the frame, and a number for the rotation.
// A sprite that is flippable will have an additional
//  letter/number appended.
// The rotation character can be 0 to signify no rotations.
//
void R_InitSpriteDefs (char** namelist) 
{ 
  char**	check;
  int		i;
  int		l;
  int		intname;
  int		frame;
  int		rotation;
  int		start;
  int		end;
  int		patched;
		
  // count the number of sprite names.
  // NOTE: sprnames[NUMSPRITES] has no NULL terminator, so the old
  //  while(*check) scan walked off the end of the array -- it only
  //  "worked" on Linux by landing on a zero pointer a few slots past
  //  the end (giving 138), and crashed under MSVC (read 141 then
  //  dereferenced garbage pointers). Use the known array length.
  (void)namelist;
  numsprites = num_sprites;

  printf("numsprites = %d\n", numsprites);

  if (!numsprites)
	  return;
		
  sprites = Z_Malloc(numsprites *sizeof(*sprites), PU_STATIC, NULL);
  // Z_Malloc does NOT zero -- sprites with no matching lumps take the
  // numframes==0 path below without setting .spriteframes, leaving it garbage.
  // doom1.wad leaves many sprnames slots (Heretic/Hexen names) lumpless, so any
  // later deref of an uninitialised .spriteframes crashes nondeterministically.
  memset(sprites, 0, numsprites * sizeof(*sprites));

  (void)start; (void)end; (void)patched;

  // Membership map: which lumps are actually in the sprite namespace (spritelumps[]).
  // The override check below must defer to a later same-named SPRITE, but NOT to a
  // later same-named lump living outside the sprite namespace -- e.g. a GZDoom hi-res
  // PNG replacement stored under HI_START/HI_END with the exact same name (FRANK.wad
  // ships FRANA1.. both as SS_ paletted sprites and as HI_ PNGs).  Using the global
  // W_GetNumForName there made every real sprite defer to its PNG twin (not a sprite
  // lump), so the sprite ended up with zero frames.
  {
    char* insprite = calloc (numlumps, 1);
    int   z;
    for (z = 0; z < numspritelumps; z++)
      if ((unsigned)spritelumps[z] < (unsigned)numlumps) insprite[spritelumps[z]] = 1;

  // scan the MERGED sprite list (spritelumps[idx] -> lump) for each sprite name,
  // noting the highest frame letter.  R_InstallSpriteLump now takes the sprite INDEX
  // (idx), not a lump number, since sprite lumps are no longer one contiguous range.
  // Just compare 4 characters as ints
  for (i=0 ; i<numsprites ; i++)
  {
    int		idx;
    spritename = namelist[i];
    if (*(int *)namelist[i] == 0) { sprites[i].numframes = 0; continue; }  // empty gap slot
    memset (sprtemp,-1, sizeof(sprtemp));

    maxframe = - 1;
    intname = *(int *)namelist[i];

    // scan the lumps, filling in the frames for whatever is found
    for (idx=0 ; idx<numspritelumps ; idx++)
    {
      l = spritelumps[idx];
      if (*(int *)lumpinfo[l].name == intname)
      {
        // Override support: a later WAD's sprite of the same name wins -- skip any
        // lump that a later one (in another merged sprite region) replaces, so we don't
        // get "two lumps mapped to it".  Only defer to a shadowing lump that is ITSELF a
        // sprite (in the namespace) -- a non-sprite twin (hi-res PNG) must not shadow it.
        {
          int w = W_GetNumForName (lumpinfo[l].name);
          if (w != l && (unsigned)w < (unsigned)numlumps && insprite[w])
            continue;
        }

        // A renamed-asset PWAD (extract_hexen.py et al.) can leave junk lumps in
        // the sprite namespace that share a real sprite's 4-char prefix but aren't
        // valid frames (e.g. "XCENHIT2"/"XCEN2" for SPR_XCEN -- frame/rot chars out
        // of the A-Z / 0-8 range).  Skip them instead of I_Error'ing the whole game.
        frame = lumpinfo[l].name[4] - 'A';
        rotation = lumpinfo[l].name[5] - '0';
        if ((unsigned)frame >= 29 || (unsigned)rotation > 8)
          continue;
        R_InstallSpriteLump (idx, frame, rotation, false);

        if (lumpinfo[l].name[6])
        {
          frame = lumpinfo[l].name[6] - 'A';
          rotation = lumpinfo[l].name[7] - '0';
          if ((unsigned)frame >= 29 || (unsigned)rotation > 8)
            continue;
          R_InstallSpriteLump (idx, frame, rotation, true);
        }
      }
	  }
	
	  // check the frames that were found for completeness
	  if (maxframe == -1)
	  {
	      sprites[i].numframes = 0;
	      continue;
	  }
		
	  maxframe++;
	
	  for (frame = 0 ; frame < maxframe ; frame++)
	  {
	    switch ((int)sprtemp[frame].rotate)
	    {
	      case -1:
		      // no rotations were found for that frame at all
		      fprintf (stderr, "R_InitSprites: No patches found "
			             "for %s frame %c\n", namelist[i], frame+'A');
		      break;

	      case 0:
		      // only the first rotation is needed
		      break;

	      case 1:
		      // Rotated frame: vanilla demands all 8 and errors out otherwise.
		      // This fork only warned -- which left lump[] holding -1 for the
		      // gaps, and drawing a -1 lump is garbage or a crash.  Fill each
		      // gap from the nearest rotation that IS present (searching
		      // outward, so a half-drawn frame reads as the closest view rather
		      // than as nothing), and say once per frame which ones were filled.
		      //
		      // Art that only supplies the cardinal views is a normal thing to
		      // want -- the turret's new muzzle-flash frames ship 1/3/5/7 only.
		      {
			int missing = 0;
			for (rotation=0 ; rotation<8 ; rotation++)
			  if (sprtemp[frame].lump[rotation] == -1)
			    missing++;

			if (missing && missing < 8)
			{
			  for (rotation=0 ; rotation<8 ; rotation++)
			  {
			    int d;
			    if (sprtemp[frame].lump[rotation] != -1)
			      continue;
			    for (d = 1 ; d <= 4 ; d++)
			    {
			      int a = (rotation + d) & 7;
			      int b = (rotation - d) & 7;
			      int src = (sprtemp[frame].lump[a] != -1) ? a
				      : (sprtemp[frame].lump[b] != -1) ? b : -1;
			      if (src >= 0)
			      {
				sprtemp[frame].lump[rotation] = sprtemp[frame].lump[src];
				sprtemp[frame].flip[rotation] = sprtemp[frame].flip[src];
				break;
			      }
			    }
			  }
			  fprintf (stderr, "R_InitSprites: Sprite %s frame %c had %d of 8 "
				   "rotations missing -- filled from the nearest present one\n",
				   namelist[i], frame+'A', missing);
			}
			else if (missing)
			  fprintf (stderr, "R_InitSprites: Sprite %s frame %c is "
				   "missing rotations\n", namelist[i], frame+'A');
		      }
      		break;
	    }
	  }
	
  	// allocate space for the frames present and copy sprtemp to it
	  sprites[i].numframes = maxframe;
  	sprites[i].spriteframes = 
	    Z_Malloc (maxframe * sizeof(spriteframe_t), PU_STATIC, NULL);
	  memcpy (sprites[i].spriteframes, sprtemp, maxframe*sizeof(spriteframe_t));
  }
    free (insprite);
  }
}




//
// GAME FUNCTIONS
//
vissprite_t	vissprites[MAXVISSPRITES];
vissprite_t*	vissprite_p;
int		newvissprite;

// Buddy colour: one companion actor may carry an arbitrary palette translation
// (chosen in the Buddy menu / declared in BUDDYDEF), applied to its sprite each
// frame without touching mobj_t (so savegames are unaffected).  The companion
// driver sets this each tic via R_SetBuddyColor; R_ProjectSprite copies the
// table onto the matching vissprite.  xlat==NULL means "no override".
static mobj_t*		r_buddycolor_mo;
static const byte*	r_buddycolor_xlat;

void R_SetBuddyColor (mobj_t* mo, const byte* xlat)
{
    r_buddycolor_mo   = mo;
    r_buddycolor_xlat = xlat;
}

// Buddy skin: the same one companion actor may also render with an ARBITRARY sprite
// (the BUDDYDEF-selected body, e.g. FRAN/HARG) in place of its own mobj->sprite (SPR_PLAY),
// so a modder buddy actually looks like itself in-world without becoming a monster.  Set
// each tic by the companion driver (R_SetBuddySkin); R_ProjectSprite swaps the spritedef
// when the actor matches AND the skin has the current player frame (else it falls back).
// spr < 0 means "no override".  Like the colour, this never touches mobj_t (savegame-safe).
static mobj_t*		r_buddyskin_mo;
static int		r_buddyskin_spr = -1;

void R_SetBuddySkin (mobj_t* mo, int spritenum)
{
    r_buddyskin_mo  = mo;
    r_buddyskin_spr = spritenum;
}



//
// R_InitSprites
// Called at program start.
//
void R_InitSprites (char** namelist)
{
    int		i;

    // Fill the WHOLE array (MAXWIDTH), not just the startup SCREENWIDTH: negonearray is
    // a constant -1 sprite ceiling-clip, and the internal resolution can be raised at
    // runtime (Options -> Video / V_SetRes).  Bounding this by the startup width left
    // negonearray[startwidth..viewwidth) as 0, which over-clipped (hid) sprite columns
    // after a res bump.  It never changes afterwards, so no per-res refill is needed.
    for (i=0 ; i<MAXWIDTH ; i++)
    {
	negonearray[i] = -1;
    }

    R_InitSpriteDefs (namelist);
}



//
// R_ClearSprites
// Called at frame start.
//
void R_ClearSprites (void)
{
    vissprite_p = vissprites;
}


//
// R_NewVisSprite
//
vissprite_t	overflowsprite;

vissprite_t* R_NewVisSprite (void)
{
    if (vissprite_p == &vissprites[MAXVISSPRITES])
	return &overflowsprite;
    
    vissprite_p++;
    return vissprite_p-1;
}



//
// R_DrawMaskedColumn
// Used for sprites and masked mid textures.
// Masked means: partly transparent, i.e. stored
//  in posts/runs of opaque pixels.
//
int*		mfloorclip;
int*		mceilingclip;

fixed_t		spryscale;
int64_t		sprtopscreen;
int		r_shadows = 1;
// (H) sprite foot clipping: `footclip` config toggle (Options -> Features), and the
// per-sprite bottom clip row R_DrawMaskedColumn honours (-1 = no clip).
int		footclip = 1;
int		dc_baseclip = -1;

void R_DrawMaskedColumn (column_t* column, int texheight)
{
    int64_t	topscreen;
    int64_t 	bottomscreen;
    fixed_t	basetexturemid;
    int		topdelta = -1;		// DeePsea tall-patch absolute-topdelta accumulator

    basetexturemid = dc_texturemid;

    // Posted columns clip per-post to their on-screen extent.  `texheight` is the
    // vertical wrap height: 128 for sprites/psprites (vanilla -- and it keeps a tall
    // wall's dc_texheight from leaking in from r_segs), but a 2S masked mid-texture
    // passes its REAL height so tall (>128) posts don't wrap on themselves (Legacy of
    // Rust's 512-tall ZZZGATE portal has 246-row posts).
    dc_texheight = texheight;

    for ( ; column->topdelta != 0xff ; )
    {
	// Tall patches (>254 rows, e.g. the 512-tall ZZZGATE gate) store lower rows with
	// the DeePsea convention: a topdelta <= the previous one is RELATIVE to it, not
	// absolute.  Accumulate so the gate's lower half lands below its upper half rather
	// than overlapping it.  Normal (strictly-increasing) patches take the else branch
	// every post, so their placement is byte-for-byte unchanged.
	if (column->topdelta <= topdelta)
	    topdelta += column->topdelta;
	else
	    topdelta = column->topdelta;

	// calculate unclipped screen coordinates
	//  for post
	topscreen = sprtopscreen + (int64_t)spryscale*topdelta;
	bottomscreen = topscreen + (int64_t)spryscale*column->length;

	dc_yl = (int)((topscreen+FRACUNIT-1)>>FRACBITS);
	dc_yh = (int)((bottomscreen-1)>>FRACBITS);

	if (dc_yh >= mfloorclip[dc_x])
	    dc_yh = mfloorclip[dc_x]-1;
	if (dc_yl <= mceilingclip[dc_x])
	    dc_yl = mceilingclip[dc_x]+1;
	// (H) foot clip: hide the submerged bottom of a liquid-standing sprite.
	if (dc_baseclip >= 0 && dc_yh > dc_baseclip)
	    dc_yh = dc_baseclip;

	if (dc_yl <= dc_yh)
	{
	    dc_source = (byte *)column + 3;
	    dc_texturemid = basetexturemid - (topdelta<<FRACBITS);
	    // dc_source = (byte *)column + 3 - topdelta;

	    // Drawn by either R_DrawColumn
	    //  or (SHADOW) R_DrawFuzzColumn.
	    colfunc ();
	}
	column = (column_t *)(  (byte *)column + column->length + 4);
    }

    dc_texturemid = basetexturemid;
}



//
// Truecolor HD sprite: blit a full-colour PNG sprite (kept RGBA, r_data.c) straight
// into screen32 with alpha blending, scaled to the vissprite and dimmed by the sector
// light -- instead of the palette-quantised patch.  Reuses the paletted path's
// per-column clip (mfloorclip/mceilingclip) so wall/floor occlusion still works.
extern unsigned int*	screen32;
extern double		fc_lightdim[];
extern int		truecolor;
int			hd_sprites = 1;		// config: full-colour HD sprites in truecolor

static void R_BlitHDSprite (vissprite_t* vis, hdimage_t* hd)
{
    int		pw = hd->w, ph = hd->h;
    fixed_t	scale = vis->scale;
    int64_t	sprtop = (int64_t)centeryfrac - (int64_t)FixedMul(vis->texturemid, scale);
    int		ytop = (int)((sprtop + FRACUNIT-1) >> FRACBITS);
    int		ybot = (int)((sprtop + (int64_t)ph*scale - 1) >> FRACBITS);
    int		span = ybot - ytop;
    fixed_t	frac = vis->startfrac;
    double	dim = 1.0;
    int		x;

    if (pw < 1 || !hd->rgba) return;
    if (span < 1) span = 1;

    // Distance/sector light: dim the true-colour source like the 8-bit colormap would.
    if (vis->colormap)
    {
	int row = (int)((vis->colormap - colormaps) >> 8);
	if (row >= 0 && row < 32) dim = fc_lightdim[row];
    }

    for (x = vis->x1; x <= vis->x2; x++, frac += vis->xiscale)
    {
	int	texcol = frac >> FRACBITS;
	int	yl = ytop, yh = ybot, y, sx = viewwindowx + x;

	if ((unsigned)sx >= (unsigned)SCREENWIDTH) continue;
	if (texcol < 0) texcol = 0; else if (texcol >= pw) texcol = pw-1;

	if (yh >= mfloorclip[x])   yh = mfloorclip[x]-1;
	if (yl <= mceilingclip[x]) yl = mceilingclip[x]+1;

	for (y = yl; y <= yh; y++)
	{
	    int		hv = (int)((int64_t)(y - ytop) * hd->h / span);
	    int		sy = y + viewwindowy;
	    unsigned	px, a, r, g, b, *sp;

	    if (hv < 0) hv = 0; else if (hv >= hd->h) hv = hd->h-1;
	    if ((unsigned)sy >= (unsigned)SCREENHEIGHT) continue;
	    px = hd->rgba[hv*hd->w + texcol];
	    a  = px >> 24;
	    if (!a) continue;					// transparent texel

	    r = (px>>16)&0xff; g = (px>>8)&0xff; b = px&0xff;
	    if (dim < 0.999) { r=(unsigned)(r*dim); g=(unsigned)(g*dim); b=(unsigned)(b*dim); }

	    sp = &screen32[sy*SCREENWIDTH + sx];
	    if (a >= 255)
		*sp = 0xff000000u | (r<<16) | (g<<8) | b;
	    else
	    {
		unsigned d = *sp, na = 255-a;
		unsigned dr=(d>>16)&0xff, dg=(d>>8)&0xff, db=d&0xff;
		*sp = 0xff000000u
		    | (((r*a + dr*na)/255)<<16)
		    | (((g*a + dg*na)/255)<<8)
		    |  ((b*a + db*na)/255);
	    }
	}
    }
}

// R_DrawVisSprite
//  mfloorclip and mceilingclip should also be set.
//
void
R_DrawVisSprite
( vissprite_t*		vis,
  int			x1,
  int			x2 )
{
    column_t*		column;
    int			texturecolumn;
    fixed_t		frac;
    patch_t*		patch;
	
	
    // PNG sprites are decoded (and re-decoded after a purge) on demand; ordinary
    // Doom sprites come straight from the lump cache.  r_data.c R_SpritePatch.
    patch = R_SpritePatch (vis->patch);

    if (r_shadows && !fixedcolormap)
    {
	if (!(vis->mobjflags & (MF_NOSECTOR | MF_MISSILE | MF_NOCLIP | MF_SHADOW)))
	{
	    if (vis->mobjflags & (MF_SHOOTABLE | MF_SPECIAL | MF_SOLID))
	    {
		fixed_t saved_spryscale = spryscale;
		int64_t saved_sprtopscreen = sprtopscreen;
		fixed_t saved_dc_iscale = dc_iscale;
		fixed_t saved_dc_texturemid = dc_texturemid;
		void (*saved_colfunc)(void) = colfunc;

		spryscale = vis->scale / 10;
		if (spryscale < 1) spryscale = 1;
		sprtopscreen = (int64_t)centeryfrac - (int64_t)FixedMul(vis->floorz - viewz, vis->scale);
		colfunc = R_DrawShadowColumn;

		frac = vis->startfrac;
		for (dc_x=vis->x1 ; dc_x<=vis->x2 ; dc_x++, frac += vis->xiscale)
		{
		    texturecolumn = frac>>FRACBITS;
		    column = (column_t *) ((byte *)patch +
					   LONG(patch->columnofs[texturecolumn]));
		    R_DrawMaskedColumn (column, 128);	// sprite shadow -- vanilla wrap
		}

		spryscale = saved_spryscale;
		sprtopscreen = saved_sprtopscreen;
		dc_iscale = saved_dc_iscale;
		dc_texturemid = saved_dc_texturemid;
		colfunc = saved_colfunc;
	    }
	}
    }

    // Full-colour HD sprite (truecolor): draw the palette-quantised patch normally
    // (so screens[0] holds the sprite and the composite's src==snapshot test passes),
    // then overlay the full-colour RGBA on screen32 AFTER the columns (below).  Skip
    // for spectres (no colormap) and invulnerability (fixedcolormap) -- those want the
    // paletted fuzz / inverse look.
    boolean hd = (truecolor && hd_sprites && vis->colormap && !fixedcolormap
		  && hdsprite[vis->patch].rgba);

    dc_colormap = vis->colormap;

    if (!dc_colormap)
    {
	// NULL colormap = shadow draw
	colfunc = fuzzcolfunc;
    }
    // (X) Hexen MF_ALTSHADOW: partly transparent, but still lit and coloured
    // normally -- Hexen's second translucency level, which DOOM does not have.
    // Blend through Boom's tranmap rather than using the spectre fuzz: fuzz is a
    // much harsher effect and reads as a rendering fault on an ordinary monster.
    // Falls back to a solid draw if the WAD gave us no tranmap.
    else if ((vis->mobjflags2 & MF2_ALTSHADOW) && main_tranmap)
    {
	colfunc = R_DrawTLColumn;
	dc_tranmap = main_tranmap;
    }
    else if (vis->translation)
    {
	// Explicit per-actor buddy-colour remap (arbitrary named colour) -- takes
	// precedence over the MF_TRANSLATION player-number bits.
	colfunc = R_DrawTranslatedColumn;
	dc_translation = (byte*) vis->translation;
    }
    else if (vis->mobjflags & MF_TRANSLATION)
    {
	colfunc = R_DrawTranslatedColumn;
	dc_translation = translationtables - 256 +
	    ( (vis->mobjflags & MF_TRANSLATION) >> (MF_TRANSSHIFT-8) );
    }
	
    dc_iscale = abs(vis->xiscale)>>detailshift;
    dc_texturemid = vis->texturemid;
    frac = vis->startfrac;
    spryscale = vis->scale;
    sprtopscreen = (int64_t)centeryfrac - (int64_t)FixedMul(dc_texturemid,spryscale);

    // (H) foot clip: clamp the sprite's columns at the liquid surface, `footclip`
    // world units up from its bottom (crispy baseclip), so the submerged part is hidden.
    if (vis->footclip)
    {
	int64_t sprbot = sprtopscreen + (int64_t)FixedMul (SHORT(patch->height)<<FRACBITS, spryscale);
	dc_baseclip = (int)((sprbot - (int64_t)FixedMul (vis->footclip<<FRACBITS, spryscale)) >> FRACBITS);
    }
    else
	dc_baseclip = -1;

    for (dc_x=vis->x1 ; dc_x<=vis->x2 ; dc_x++, frac += vis->xiscale)
    {
	texturecolumn = frac>>FRACBITS;
#ifdef RANGECHECK
	if (texturecolumn < 0 || texturecolumn >= SHORT(patch->width))
	    I_Error ("R_DrawSpriteRange: bad texturecolumn");
#endif
	column = (column_t *) ((byte *)patch +
			       LONG(patch->columnofs[texturecolumn]));
	R_DrawMaskedColumn (column, 128);	// sprite -- vanilla 128-row wrap
    }
    dc_baseclip = -1;		// (H) don't leak the foot clip into other sprites/psprites

    colfunc = basecolfunc;

    // Overlay the full-colour HD image on screen32, over the quantised sprite that
    // was just drawn to screens[0]/screen32.  The composite then shows this instead
    // of the quantised version wherever the sprite is visible.
    if (hd)
	R_BlitHDSprite (vis, &hdsprite[vis->patch]);
}



//
// R_ProjectSprite
// Generates a vissprite for a thing
//  if it might be visible.
//
void R_ProjectSprite (mobj_t* thing)
{
    fixed_t		tr_x;
    fixed_t		tr_y;

    // Fully invisible (ZDoom RF_INVISIBLE) -- e.g. the submerged Hexen Serpent.
    if (thing->flags2 & MF2_DONTDRAW)
	return;
    
    fixed_t		gxt;
    fixed_t		gyt;
    
    fixed_t		tx;
    fixed_t		tz;

    fixed_t		xscale;
    
    int			x1;
    int			x2;

    spritedef_t*	sprdef;
    spriteframe_t*	sprframe;
    int			lump;
    
    unsigned		rot;
    boolean		flip;
    
    int			index;

    vissprite_t*	vis;
    
    angle_t		ang;
    fixed_t		iscale;
    
    // transform the origin point
    tr_x = thing->x - viewx;
    tr_y = thing->y - viewy;
	
    gxt = FixedMul(tr_x,viewcos); 
    gyt = -FixedMul(tr_y,viewsin);
    
    tz = gxt-gyt; 

    // thing is behind view plane?
    if (tz < MINZ)
	return;
    
    xscale = FixedDiv(projection, tz);
	
    gxt = -FixedMul(tr_x,viewsin); 
    gyt = FixedMul(tr_y,viewcos); 
    tx = -(gyt+gxt); 

    // too far off the side?
    if (abs(tx)>(tz<<2))
	return;
    
    // Heretic (phase 1): the H* monster/artifact sprites aren't present in heretic.wad
    // (they live in hereticstuff.wad / a later phase), so a spawned Heretic thing has no
    // sprite frames -- skip drawing it instead of I_Erroring.  DOOM is unaffected.
    // (M3c) Skip a thing whose sprite/frame is out of range instead of aborting -- covers Heretic
    // phase-1 sprites AND DSDHacked sprites/frames beyond the built-in tables (DOOM I_Errored).
    if ((unsigned)thing->sprite >= numsprites)
	return;
    if ( (thing->frame&FF_FRAMEMASK) >= sprites[thing->sprite].numframes )
	return;

    // decide which patch to use for sprite relative to player
#ifdef RANGECHECK
    if ((unsigned)thing->sprite >= numsprites)
	I_Error ("R_ProjectSprite: invalid sprite number %i ",
		 thing->sprite);
#endif
    // (buddy) render the co-op companion with its selected skin sprite, but only if that
    // sprite actually carries the current player frame -- otherwise keep the PLAY body.
    if (thing == r_buddyskin_mo && r_buddyskin_spr >= 0
	&& (unsigned)r_buddyskin_spr < numsprites
	&& (thing->frame&FF_FRAMEMASK) < sprites[r_buddyskin_spr].numframes)
	sprdef = &sprites[r_buddyskin_spr];
    else
	sprdef = &sprites[thing->sprite];
#ifdef RANGECHECK
    if ( (thing->frame&FF_FRAMEMASK) >= sprdef->numframes )
	I_Error ("R_ProjectSprite: invalid sprite frame %i : %i ",
		 thing->sprite, thing->frame);
#endif
    sprframe = &sprdef->spriteframes[ thing->frame & FF_FRAMEMASK];

    if (sprframe->rotate)
    {
	// choose a different rotation based on player view
	ang = R_PointToAngle (thing->x, thing->y);
	rot = (ang-thing->angle+(unsigned)(ANG45/2)*9)>>29;
	lump = sprframe->lump[rot];
	flip = (boolean)sprframe->flip[rot];
    }
    else
    {
	// use single rotation for all views
	lump = sprframe->lump[0];
	flip = (boolean)sprframe->flip[0];
    }
    
    // calculate edges of the shape.  Do it in 64-bit: at hi-res centerxfrac and xscale
    // are large, so the 32-bit (centerxfrac + tx*xscale) sum can overflow before the
    // >>FRACBITS (crispy/woof use FixedMul64 here for the same reason).
    tx -= spriteoffset[lump];
    x1 = (int)(((long long)centerxfrac + (((long long)tx * xscale) >> FRACBITS)) >> FRACBITS);

    // off the right side?
    if (x1 > viewwidth)
	return;

    tx +=  spritewidth[lump];
    x2 = (int)(((long long)centerxfrac + (((long long)tx * xscale) >> FRACBITS)) >> FRACBITS) - 1;

    // off the left side
    if (x2 < 0)
	return;
    
    // store information in a vissprite
    vis = R_NewVisSprite ();
    vis->mobjflags = thing->flags;
    vis->mobjflags2 = thing->flags2;
    vis->translation = (thing == r_buddycolor_mo) ? r_buddycolor_xlat : NULL;
    vis->floorz = thing->floorz;
    vis->scale = xscale<<detailshift;
    vis->gx = thing->x;
    vis->gy = thing->y;
    vis->gz = thing->z;
    vis->gzt = thing->z + spritetopoffset[lump];
    vis->texturemid = vis->gzt - viewz;
    // (H) foot clipping: sink an actor standing on a liquid flat by FOOTCLIPSIZE (10)
    // world units and clip its submerged bottom (crispy heretic).  Gated on the
    // `footclip` option; floating things (MF_NOGRAVITY) never clip.
    {
	extern int heretic_mode, footclip;
	extern int P_ThingOnLiquid (mobj_t*);
	if (heretic_mode && footclip
	    && !(thing->flags & MF_NOGRAVITY)
	    && thing->z <= thing->subsector->sector->floorheight
	    && P_ThingOnLiquid (thing))
	    vis->footclip = 10;
	else
	    vis->footclip = 0;
	vis->texturemid -= vis->footclip << FRACBITS;
    }
    vis->x1 = x1 < 0 ? 0 : x1;
    vis->x2 = x2 >= viewwidth ? viewwidth-1 : x2;	
    iscale = FixedDiv (FRACUNIT, xscale);

    if (flip)
    {
	vis->startfrac = spritewidth[lump]-1;
	vis->xiscale = -iscale;
    }
    else
    {
	vis->startfrac = 0;
	vis->xiscale = iscale;
    }

    if (vis->x1 > x1)
	vis->startfrac += vis->xiscale*(vis->x1-x1);
    vis->patch = lump;
    
    // get light level
    if (thing->flags & MF_SHADOW)
    {
	// shadow draw
	vis->colormap = NULL;
    }
    else if (fixedcolormap)
    {
	// fixed map
	vis->colormap = fixedcolormap;
    }
    else if (thing->frame & FF_FULLBRIGHT)
    {
	// full bright
	vis->colormap = colormaps;
    }
    
    else
    {
	// diminished light
	index = xscale>>(LIGHTSCALESHIFT-detailshift);

	if (index >= MAXLIGHTSCALE) 
	    index = MAXLIGHTSCALE-1;

	vis->colormap = spritelights[index];
    }	
}




//
// R_AddSprites
// During BSP traversal, this adds sprites by sector.
//
void R_AddSprites (sector_t* sec)
{
    mobj_t*		thing;
    int			lightnum;

    // BSP is traversed by subsector.
    // A sector might have been split into several
    //  subsectors during BSP building.
    // Thus we check whether its already added.
    if (sec->validcount == validcount)
	return;		

    // Well, now it will be done.
    sec->validcount = validcount;
	
    lightnum = (sec->lightlevel >> LIGHTSEGSHIFT)+extralight;

    if (lightnum < 0)		
	spritelights = scalelight[0];
    else if (lightnum >= LIGHTLEVELS)
	spritelights = scalelight[LIGHTLEVELS-1];
    else
	spritelights = scalelight[lightnum];

    // Handle all things in sector.
    for (thing = sec->thinglist ; thing ; thing = thing->snext)
	R_ProjectSprite (thing);
}


//
// R_DrawPSprite
//
void R_DrawPSprite (pspdef_t* psp)
{
    fixed_t		tx;
    int			x1;
    int			x2;
    spritedef_t*	sprdef;
    spriteframe_t*	sprframe;
    int			lump;
    boolean		flip;
    vissprite_t*	vis;
    vissprite_t		avis;
    
    // Heretic (phase 1): DOOM weapon sprites (PISG/SHTG/...) aren't in heretic.wad,
    // so the player's psprite frame is invalid -- skip drawing the weapon instead of
    // I_Erroring (Heretic weapons are a later phase).  DOOM is unaffected.
    // (M3c) skip an out-of-range psprite (Heretic phase-1 sprites + DSDHacked) instead of aborting
    if ((unsigned)psp->state->sprite >= numsprites)
	return;
    if ((psp->state->frame & FF_FRAMEMASK) >= sprites[psp->state->sprite].numframes)
	return;

    // decide which patch to use
#ifdef RANGECHECK
    if ( (unsigned)psp->state->sprite >= numsprites)
	I_Error ("R_ProjectSprite: invalid sprite number %i ",
		 psp->state->sprite);
#endif
    sprdef = &sprites[psp->state->sprite];
#ifdef RANGECHECK
    if ( (psp->state->frame & FF_FRAMEMASK)  >= sprdef->numframes)
	I_Error ("R_ProjectSprite: invalid sprite frame %i : %i ",
		 psp->state->sprite, psp->state->frame);
#endif
    sprframe = &sprdef->spriteframes[ psp->state->frame & FF_FRAMEMASK ];

    lump = sprframe->lump[0];
    flip = (boolean)sprframe->flip[0];
    
    // calculate edges of the shape
    tx = psp->sx-160*FRACUNIT;
	
    tx -= spriteoffset[lump];	
    x1 = (centerxfrac + FixedMul (tx,pspritescale) ) >>FRACBITS;

    // off the right side
    if (x1 > viewwidth)
	return;		

    tx +=  spritewidth[lump];
    x2 = ((centerxfrac + FixedMul (tx, pspritescale) ) >>FRACBITS) - 1;

    // off the left side
    if (x2 < 0)
	return;
    
    // store information in a vissprite
    vis = &avis;
    vis->mobjflags = 0;
    vis->footclip = 0;			// psprites (the weapon) are never foot-clipped
    vis->texturemid = (BASEYCENTER<<FRACBITS)+FRACUNIT/2-(psp->sy-spritetopoffset[lump]);

    // Hexen's status bar is 66px tall (H2BAR at y=134) against DOOM's 32, and at the
    // largest view sizes the 3D view renders FULL height with the bar drawn over it.
    // The weapon therefore landed entirely underneath the bar and was painted out --
    // drawn correctly, just never seen.  Lift it by the extra height so it sits above
    // the bar.  A larger texturemid draws higher up the screen.
    if (gametype == GT_HEXEN)
	vis->texturemid += (ST_HEXEN_HEIGHT - 32) << FRACBITS;
    { extern int statusbar_style, setblocks;   // full bar over a full-height view hides the weapon
      if (statusbar_style == 0 && setblocks <= 10 && viewheight == SCREENHEIGHT)
        vis->texturemid += 16 << FRACBITS; }   // lift by half the 32px bar (centery shift)
    vis->x1 = x1 < 0 ? 0 : x1;
    vis->x2 = x2 >= viewwidth ? viewwidth-1 : x2;	
    vis->scale = pspritescale<<detailshift;
    
    if (flip)
    {
	vis->xiscale = -pspriteiscale;
	vis->startfrac = spritewidth[lump]-1;
    }
    else
    {
	vis->xiscale = pspriteiscale;
	vis->startfrac = 0;
    }
    
    if (vis->x1 > x1)
	vis->startfrac += vis->xiscale*(vis->x1-x1);

    vis->patch = lump;
    vis->translation = NULL;		// player weapon is never buddy-recoloured (avis is stack-local)

    if (viewplayer->powers[pw_invisibility] > 4*32
	|| viewplayer->powers[pw_invisibility] & 8)
    {
	// shadow draw
	vis->colormap = NULL;
    }
    else if (fixedcolormap)
    {
	// fixed color
	vis->colormap = fixedcolormap;
    }
    else if (psp->state->frame & FF_FULLBRIGHT)
    {
	// full bright
	vis->colormap = colormaps;
    }
    else
    {
	// local light
	vis->colormap = spritelights[MAXLIGHTSCALE-1];
    }
	
    R_DrawVisSprite (vis, vis->x1, vis->x2);
}



//
// R_DrawPlayerSprites
//
void R_DrawPlayerSprites (void)
{
    int		i;
    int		lightnum;
    pspdef_t*	psp;
    fixed_t	savecenteryfrac;
    int		savecentery;

    // MOD/free-look: the weapon is attached to the screen, not the world, so
    // draw it against the un-pitched view centre.  Otherwise the pitch-shifted
    // centeryfrac floats the weapon up and opens a gap to the status bar when
    // looking down (and slides it off the bottom when looking up).
    savecenteryfrac = centeryfrac;
    savecentery = centery;
    centery = viewheight/2;
    centeryfrac = centery<<FRACBITS;

    // get light level
    lightnum =
	(viewplayer->mo->subsector->sector->lightlevel >> LIGHTSEGSHIFT)
	+extralight;

    if (lightnum < 0)		
	spritelights = scalelight[0];
    else if (lightnum >= LIGHTLEVELS)
	spritelights = scalelight[LIGHTLEVELS-1];
    else
	spritelights = scalelight[lightnum];
    
    // clip to screen bounds
    mfloorclip = screenheightarray;
    mceilingclip = negonearray;
    
    // add all active psprites
    for (i=0, psp=viewplayer->psprites;
	 i<NUMPSPRITES;
	 i++,psp++)
    {
	if (psp->state)
	    R_DrawPSprite (psp);
    }

    centeryfrac = savecenteryfrac;
    centery = savecentery;
}




//
// R_SortVisSprites
//
vissprite_t	vsprsortedhead;


// Ascending by scale (far -> near = painter's order); ties broken by array address so
// equal-scale sprites keep insertion order -- identical result to the old stable
// selection sort, but O(n log n) instead of O(n^2) (which spiked with director hordes).
static int R_VisSpriteCmp (const void* a, const void* b)
{
    const vissprite_t* sa = *(vissprite_t* const*)a;
    const vissprite_t* sb = *(vissprite_t* const*)b;
    if (sa->scale < sb->scale) return -1;
    if (sa->scale > sb->scale) return  1;
    return (sa < sb) ? -1 : (sa > sb);		// tie -> lower index (contiguous vissprites[]) first
}

void R_SortVisSprites (void)
{
    static vissprite_t*	order[MAXVISSPRITES];	// no per-frame alloc: vissprites[] is capped at this
    int			i, count = vissprite_p - vissprites;
    vissprite_t*	prev;

    vsprsortedhead.next = vsprsortedhead.prev = &vsprsortedhead;
    if (!count)
	return;

    for (i = 0 ; i < count ; i++)
	order[i] = &vissprites[i];
    qsort (order, count, sizeof(order[0]), R_VisSpriteCmp);

    // relink into vsprsortedhead in ascending-scale order (R_DrawMasked walks .next)
    prev = &vsprsortedhead;
    for (i = 0 ; i < count ; i++)
    {
	order[i]->prev = prev;
	prev->next = order[i];
	prev = order[i];
    }
    prev->next = &vsprsortedhead;
    vsprsortedhead.prev = prev;
}



//
// R_DrawSprite
//
void R_DrawSprite (vissprite_t* spr)
{
    drawseg_t*		ds;
    int		clipbot[MAXWIDTH];
    int		cliptop[MAXWIDTH];
    int			x;
    int			r1;
    int			r2;
    fixed_t		scale;
    fixed_t		lowscale;
    int			silhouette;
		
    for (x = spr->x1 ; x<=spr->x2 ; x++)
	clipbot[x] = cliptop[x] = -2;
    
    // Scan drawsegs from end to start for obscuring segs.
    // The first drawseg that has a greater scale
    //  is the clip seg.
    for (ds=ds_p-1 ; ds >= drawsegs ; ds--)
    {
	// determine if the drawseg obscures the sprite
	if (ds->x1 > spr->x2
	    || ds->x2 < spr->x1
	    || (!ds->silhouette
		&& !ds->maskedtexturecol) )
	{
	    // does not cover sprite
	    continue;
	}
			
	r1 = ds->x1 < spr->x1 ? spr->x1 : ds->x1;
	r2 = ds->x2 > spr->x2 ? spr->x2 : ds->x2;

	if (ds->scale1 > ds->scale2)
	{
	    lowscale = ds->scale2;
	    scale = ds->scale1;
	}
	else
	{
	    lowscale = ds->scale1;
	    scale = ds->scale2;
	}
		
	if (scale < spr->scale
	    || ( lowscale < spr->scale
		 && !R_PointOnSegSide (spr->gx, spr->gy, ds->curline) ) )
	{
	    // masked mid texture?
	    if (ds->maskedtexturecol)	
		R_RenderMaskedSegRange (ds, r1, r2);
	    // seg is behind sprite
	    continue;			
	}

	
	// clip this piece of the sprite
	silhouette = ds->silhouette;
	
	if (spr->gz >= ds->bsilheight)
	    silhouette &= ~SIL_BOTTOM;

	if (spr->gzt <= ds->tsilheight)
	    silhouette &= ~SIL_TOP;
			
	if (silhouette == 1)
	{
	    // bottom sil
	    for (x=r1 ; x<=r2 ; x++)
		if (clipbot[x] == -2)
		    clipbot[x] = ds->sprbottomclip[x];
	}
	else if (silhouette == 2)
	{
	    // top sil
	    for (x=r1 ; x<=r2 ; x++)
		if (cliptop[x] == -2)
		    cliptop[x] = ds->sprtopclip[x];
	}
	else if (silhouette == 3)
	{
	    // both
	    for (x=r1 ; x<=r2 ; x++)
	    {
		if (clipbot[x] == -2)
		    clipbot[x] = ds->sprbottomclip[x];
		if (cliptop[x] == -2)
		    cliptop[x] = ds->sprtopclip[x];
	    }
	}
		
    }
    
    // all clipping has been performed, so draw the sprite

    // check for unclipped columns
    for (x = spr->x1 ; x<=spr->x2 ; x++)
    {
	if (clipbot[x] == -2)		
	    clipbot[x] = viewheight;

	if (cliptop[x] == -2)
	    cliptop[x] = -1;
    }
		
    mfloorclip = clipbot;
    mceilingclip = cliptop;
    R_DrawVisSprite (spr, spr->x1, spr->x2);
}




//
// R_DrawMasked
//
void R_DrawMasked (void)
{
    vissprite_t*	spr;
    drawseg_t*		ds;
	
    R_SortVisSprites ();

    if (vissprite_p > vissprites)
    {
	// draw all vissprites back to front
	for (spr = vsprsortedhead.next ;
	     spr != &vsprsortedhead ;
	     spr=spr->next)
	{
	    
	    R_DrawSprite (spr);
	}
    }
    
    // render any remaining masked mid textures
    for (ds=ds_p-1 ; ds >= drawsegs ; ds--)
	if (ds->maskedtexturecol)
	    R_RenderMaskedSegRange (ds, ds->x1, ds->x2);
    
    // draw the psprites on top of everything
    //  but does not draw on side views
    if (!viewangleoffset)		
	R_DrawPlayerSprites ();
}



