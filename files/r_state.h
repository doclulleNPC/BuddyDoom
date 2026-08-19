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
//	Refresh/render internal state variables (global).
//
//-----------------------------------------------------------------------------


#ifndef __R_STATE__
#define __R_STATE__

// Need data structure definitions.
#include "d_player.h"
#include "r_data.h"



#ifdef __GNUG__
#pragma interface
#endif



//
// Refresh internal data structures,
//  for rendering.
//

// needed for texture pegging
extern fixed_t*		textureheight;

// needed for pre rendering (fracs)
extern fixed_t*		spritewidth;

extern fixed_t*		spriteoffset;
extern fixed_t*		spritetopoffset;

extern lighttable_t*	colormaps;

extern int		viewwidth;
extern int		scaledviewwidth;
extern int		viewheight;

extern int		firstflat;
extern int		numflats;
extern int*		flatlumps;	// flat index -> lump number (merged flat namespaces)

// for global animation
extern int*		flattranslation;

// 64x64 span source for a flat index -- decodes and caches PNG flats, hands raw Doom
// flats straight back.  NULL = unusable (bad size / decode failure); skip drawing.
byte*			R_GetFlat (int flatnum, int tag);
extern int*		texturetranslation;	


// Sprite....
extern int		firstspritelump;
extern int		lastspritelump;
extern int		numspritelumps;
extern int*		spritelumps;	// sprite index -> lump number (merged namespaces)
extern void**		spritepatch;	// sprite index -> converted PNG patch_t, or NULL

// Full-colour HD sprite (truecolor): the ARGB8888 image kept from a PNG sprite lump,
// blitted straight into screen32 instead of the palette-quantised patch.  rgba==NULL
// (or w==0) means "no HD image".  Parallel to spritepatch[]/spritelumps[].
typedef struct { int w, h; unsigned int* rgba; } hdimage_t;
extern hdimage_t*	hdsprite;



//
// Lookup tables for map data.
//
extern int		numsprites;
extern spritedef_t*	sprites;

extern int		numvertexes;
extern vertex_t*	vertexes;

extern int		numsegs;
extern seg_t*		segs;

extern int		numsectors;
extern sector_t*	sectors;

extern int		numsubsectors;
extern subsector_t*	subsectors;

extern int		numnodes;
extern node_t*		nodes;

extern int		numlines;
extern line_t*		lines;

extern int		numsides;
extern side_t*		sides;


//
// POV data.
//
extern fixed_t		viewx;
extern fixed_t		viewy;
extern fixed_t		viewz;

extern angle_t		viewangle;
extern player_t*	viewplayer;


// ?
extern angle_t		clipangle;

extern int		viewangletox[FINEANGLES/2];
extern angle_t		xtoviewangle[MAXWIDTH+1];
//extern fixed_t		finetangent[FINEANGLES/2];

extern fixed_t		rw_distance;
extern angle_t		rw_normalangle;



// angle to line origin
extern int		rw_angle1;

// Segs count?
extern int		sscount;

extern visplane_t*	floorplane;
extern visplane_t*	ceilingplane;


#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
