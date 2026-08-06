// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1993-2008 Raven Software (Hexen polyobjects -- reference for this port)
//
// DESCRIPTION:
//	(X) Polyobjects -- STEP 1 of 5: discovery and placement.
//
//	A Hexen polyobject is a cluster of linedefs drawn OFF to one side of the map,
//	in void space, which the engine translates into position at level load and can
//	then rotate or slide.  Hexen's swinging and sliding doors are polyobjects, so
//	without them those doorways are simply holes and the door itself sits in the
//	void where the mapper drew it.
//
//	This file is being built up in the order laid out at the end of the previous
//	session, because polyobjects reach into the renderer, the blockmap and the
//	sight code, and doing that in one go with no way to test is how you break the
//	four games that currently work:
//
//	  1. THIS STEP -- find each polyobj's linedefs, find its anchor and start spot,
//	     and translate its vertices into place.  Touches nothing but p_setup.
//	  2. the rotate/translate thinkers + blockmap link/unlink on every move
//	  3. r_bsp.c: per-subsector polyobj seg lists, so the moved lines DRAW
//	  4. p_maputl.c / p_sight.c: include them in blockmap iteration, so they BLOCK
//	  5. p_acs.c: wire the Polyobj_* specials into P_ExecuteLineSpecial
//
//	After this step a polyobj is correctly identified and positioned in the line
//	and vertex arrays, and nothing else has changed: the lines still do not render
//	(step 3) and do not collide (step 4), exactly as before.  That is deliberate --
//	it is the checkpoint that proves the discovery logic before anything
//	load-bearing depends on it.
//
//	Reference: ../crispy-doom/src/hexen/po_man.c (Raven's original) and the Hexen
//	source at github.com/OpenSourcedGames/Hexen.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_bbox.h"
#include "p_local.h"
#include "r_state.h"
#include "z_zone.h"
#include "po_man.h"

// Hexen's polyobj map things and linedef specials.
#define PO_ANCHOR_TYPE		3000	// where the mapper DREW it (the void copy)
#define PO_SPAWN_TYPE		3001	// where it belongs in the map
#define PO_SPAWNCRUSH_TYPE	3002	// ditto, and it crushes what it hits
#define PO_LINE_START		1	// Polyobj_StartLine   -- args[0] = polyobj id
#define PO_LINE_EXPLICIT	5	// Polyobj_ExplicitLine -- args[0] = id, args[1] = order

polyobj_t*	polyobjs;
int		po_NumPolyobjs;

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

// Gather every linedef tagged with this polyobj id.  Hexen supports two markups:
// PO_LINE_START walks the loop from one starting line, and PO_LINE_EXPLICIT lists
// the lines with an explicit ordering.  Both put the id in args[0], so for placement
// -- which does not care about winding order -- collecting by id covers both.
static int PO_CollectLines (int id, line_t** out, int max)
{
    int i, n = 0;
    for (i = 0; i < numlines && n < max; i++)
    {
	line_t* ld = &lines[i];
	if ((ld->special == PO_LINE_START || ld->special == PO_LINE_EXPLICIT)
	    && ld->args[0] == id)
	    out[n++] = ld;
    }
    return n;
}

// Translate a polyobj so its anchor lands on its start spot.  Vertices are shared
// between linedefs, so move each one ONCE -- shifting a vertex twice would shear
// the polyobj apart.
static void PO_Translate (polyobj_t* po, fixed_t dx, fixed_t dy)
{
    int		i, j, k;
    vertex_t*	moved[POLY_MAXLINES * 2];
    int		nmoved = 0;

    for (i = 0; i < po->numlines; i++)
    {
	vertex_t* v[2];
	v[0] = po->lines[i]->v1;
	v[1] = po->lines[i]->v2;
	for (j = 0; j < 2; j++)
	{
	    for (k = 0; k < nmoved; k++)
		if (moved[k] == v[j]) break;
	    if (k < nmoved) continue;			// already shifted
	    if (nmoved < (int)(sizeof moved / sizeof moved[0]))
		moved[nmoved++] = v[j];
	    v[j]->x += dx;
	    v[j]->y += dy;
	}
    }

    // Keep each line's cached geometry consistent with its (now moved) vertices --
    // dx/dy and the bbox are precomputed in P_LoadLineDefs and everything from
    // collision to rendering reads them.
    for (i = 0; i < po->numlines; i++)
    {
	line_t*   ld = po->lines[i];
	vertex_t* v1 = ld->v1;
	vertex_t* v2 = ld->v2;
	ld->dx = v2->x - v1->x;
	ld->dy = v2->y - v1->y;
	ld->bbox[BOXLEFT]   = (v1->x < v2->x) ? v1->x : v2->x;
	ld->bbox[BOXRIGHT]  = (v1->x < v2->x) ? v2->x : v1->x;
	ld->bbox[BOXBOTTOM] = (v1->y < v2->y) ? v1->y : v2->y;
	ld->bbox[BOXTOP]    = (v1->y < v2->y) ? v2->y : v1->y;
    }
}

// ---------------------------------------------------------------------------
// PO_Init -- called from P_SetupLevel once the geometry and THINGS are loaded.
//
// `spots` is every polyobj map thing the THINGS pass saw, in map order; a thing's
// ANGLE field carries the polyobj id, not a facing.
// ---------------------------------------------------------------------------
void PO_Init (const po_spot_t* spots, int nspots)
{
    int	i, j;
    int	npo = 0;

    polyobjs = NULL;
    po_NumPolyobjs = 0;
    if (!nspots || gametype != GT_HEXEN)
	return;

    polyobjs = Z_Malloc (POLY_MAXOBJS * sizeof(*polyobjs), PU_LEVEL, 0);
    memset (polyobjs, 0, POLY_MAXOBJS * sizeof(*polyobjs));

    for (i = 0; i < nspots && npo < POLY_MAXOBJS; i++)
    {
	polyobj_t*	po;
	const po_spot_t* anchor = NULL;
	line_t*		ln[POLY_MAXLINES];
	int		nl;

	if (spots[i].type != PO_SPAWN_TYPE && spots[i].type != PO_SPAWNCRUSH_TYPE)
	    continue;

	nl = PO_CollectLines (spots[i].id, ln, POLY_MAXLINES);
	if (!nl)
	{
	    printf ("PO_Init: polyobj %d has a start spot but no tagged lines\n",
		    spots[i].id);
	    continue;
	}

	// The matching anchor says where those lines were DRAWN.
	for (j = 0; j < nspots; j++)
	    if (spots[j].type == PO_ANCHOR_TYPE && spots[j].id == spots[i].id)
		{ anchor = &spots[j]; break; }
	if (!anchor)
	{
	    printf ("PO_Init: polyobj %d has no anchor (3000) -- not placed\n",
		    spots[i].id);
	    continue;
	}

	po = &polyobjs[npo++];
	po->id       = spots[i].id;
	po->crush    = (spots[i].type == PO_SPAWNCRUSH_TYPE);
	po->numlines = nl;
	po->lines    = Z_Malloc (nl * sizeof(*po->lines), PU_LEVEL, 0);
	memcpy (po->lines, ln, nl * sizeof(*po->lines));
	po->startX   = spots[i].x << FRACBITS;
	po->startY   = spots[i].y << FRACBITS;

	PO_Translate (po, po->startX - (anchor->x << FRACBITS),
			  po->startY - (anchor->y << FRACBITS));
    }

    po_NumPolyobjs = npo;
    if (npo)
	printf ("PO_Init: %d polyobject(s) placed "
		"(step 1 of 5: positioned, not yet drawn or solid)\n", npo);
}
