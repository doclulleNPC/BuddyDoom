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
// DESCRIPTION:
//	Gamma correction LUT.
//	Functions to draw patches (by post) directly to screen.
//	Functions to blit a block to the screen.
//
//-----------------------------------------------------------------------------


#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "doomtype.h"

#include "doomdef.h"

// Needed because we are refering to patches.
#include "r_data.h"

//
// VIDEO
//

#define CENTERY			(SCREENHEIGHT/2)


// Screen 0 is the screen updated by I_Update screen.
// Screen 1 is an extra buffer.



extern	byte*		screens[5];

extern  int	dirtybox[4];

extern	byte	gammatable[5][256];
extern	int	usegamma;



// Allocates buffer screens, call before R_Init.
void V_Init (void);


void
V_CopyRect
( int		srcx,
  int		srcy,
  int		srcscrn,
  int		width,
  int		height,
  int		destx,
  int		desty,
  int		destscrn );

void
V_DrawPatch
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch);

void
V_DrawPatchDirect
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch );

// Draw a patch magnified by an extra integer factor (for larger menu text).
void
V_DrawPatchScaled
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch,
  int		sc );


// Draw a linear block of pixels into the view buffer.
void
V_DrawBlock
( int		x,
  int		y,
  int		scrn,
  int		width,
  int		height,
  byte*		src );

// Reads a linear block of pixels into the view buffer.
void
V_GetBlock
( int		x,
  int		y,
  int		scrn,
  int		width,
  int		height,
  byte*		dest );

// Light PNG support for UI graphics: decode a PNG lump and return it as a
// palette-quantised patch_t (drawable by V_DrawPatch), cached by lump name.
// Returns NULL if the lump is missing or isn't a PNG.  See v_png.c.
patch_t* V_CachePNG (const char* name);

// Convert a PNG *sprite* lump (by number) into a paletted patch_t, preserving the
// sprite offsets from its grAb chunk.  Lets GZDoom PNG sprite WADs render in the
// software renderer.  Returns a PU_STATIC patch, or NULL.  See v_png.c.
patch_t* V_PNGLumpToPatch (int lump);

// Like V_PNGLumpToPatch, but also keeps an ARGB8888 full-colour copy (PU_STATIC) in
// *rgba_out with its w/h -- for the truecolor HD sprite renderer.  See v_png.c.
patch_t* V_PNGLumpDecode (int lump, unsigned int** rgba_out, int* w_out, int* h_out);

// PNG header only: dimensions + grAb offsets, WITHOUT decoding any pixels.  Lets
// R_InitSpriteLumps size thousands of PNG sprites for free.  false = not a PNG.
boolean V_PNGLumpInfo (int lump, int* w_out, int* h_out, int* loff, int* toff);

// True if `lump` is a PNG file (magic check only, no decode).
boolean V_IsPNGLump (int lump);

// Decode a PNG *flat* lump into the 64x64 linear paletted buffer the span drawer wants.
// Must be exactly 64x64 -- the span drawer masks with &63, so no other size is
// addressable; anything else is refused (NULL) rather than stretched.  Allocated
// PU_CACHE against `user`, like V_PNGLumpDecodeCached.  See r_data.c R_GetFlat.
byte* V_PNGLumpToFlat (int lump, void** user);

// Decode a PNG sprite into PURGEABLE zone blocks owned by the caller's cache slots
// (PU_CACHE + user pointer, so Z_Free NULLs them and the caller re-decodes on the
// next draw).  rgba_user == NULL skips the full-colour copy.  See v_png.c.
patch_t* V_PNGLumpDecodeCached (int lump, void** patch_user, void** rgba_user,
				int* w_out, int* h_out);

// Draw a patch with a 256-entry palette translation applied per pixel (NULL = none).
void V_DrawPatchTranslated (int x, int y, int scrn, patch_t* patch, const byte* trans);

// Fullscreen page (title/help/credits) helpers: raw 320x200 (Heretic-style 64000-byte
// lumps) vs patch, auto-detected, centred in widescreen.  See v_video.c.
void V_DrawRawScreen (int lumpnum);
void V_DrawFullscreenLumpName (const char* name);

// V_DrawPatchScaled + palette translation, magnified around the patch's origin
// (trans == NULL -> no remap).  Used for the enlarged, recoloured buddy preview.
void V_DrawPatchScaledTranslated (int x, int y, int scrn, patch_t* patch, int sc, const byte* trans);

// Boom CR_* colour ranges, built by an HSV hue-rotate that keeps each pixel's
// shading (v_png.c; the algorithm is ../woof/src/v_trans.c V_Colorize).
enum { VP_CR_NONE = -1, VP_CR_RED = 0, VP_CR_GOLD, VP_CR_GREEN, VP_CR_BLUE2,
       VP_CR_GRAY, VP_NCR };
const byte* V_ColorRange (int cr);

const byte* V_HealthTrans (int hp);

// ---- buddy player-colour remaps (v_png.c) ----------------------------------
// A named set of translations that recolour ONLY the green player-uniform ramp
// (palette 0x70-0x7F), luminance-preserving, leaving skin/gun/etc. untouched --
// like Doom's built-in player colours but arbitrary and named.  Used by the
// Buddy menu preview, the in-world buddy sprite (R_SetBuddyColor) and BUDDYDEF.
int         V_BuddyColorCount (void);		// number of named colours (index 0 = Green)
const char* V_BuddyColorName  (int i);		// display name, or "" if out of range
const byte* V_BuddyColorTable (int i);		// 256-entry remap, or NULL for Green(0)=identity


void
V_MarkRect
( int		x,
  int		y,
  int		width,
  int		height );

#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
