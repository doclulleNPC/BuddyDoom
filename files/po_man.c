// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1993-2008 Raven Software (Hexen polyobjects -- reference for this port)
//
// DESCRIPTION:
//	(X) Polyobjects -- STEPS 1-4 of 5: placement, movement, rendering, COLLISION.
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
//	  1. DONE -- find each polyobj's linedefs, find its anchor and start spot,
//	     and translate its vertices into place.  Touches nothing but p_setup.
//	  2. DONE -- rotate/translate + the thinkers that drive them, and blockmap
//	     link/unlink on every move.
//	  3. DONE -- per-subsector polyobj seg lists, so the lines DRAW.
//	  4. DONE (this step) -- blockmap and sight iteration, so they BLOCK.
//	  5. p_acs.c: wire the Polyobj_* specials into P_ExecuteLineSpecial
//
//	After step 4 a polyobj is a real obstacle: you cannot walk or shoot through a
//	closed door, monsters cannot see through one, and a polyobj that is moved into
//	something solid stops rather than passing through it.  What is still missing is
//	step 5 -- nothing in a map has yet asked one to move -- so in normal play they
//	stand closed.  Each step stays inert until the one that consumes it lands,
//	which is what keeps the four working games out of the blast radius.
//
//	Reference: ../crispy-doom/src/hexen/po_man.c (Raven's original) and the Hexen
//	source at github.com/OpenSourcedGames/Hexen.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>		// abs
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_argv.h"		// M_CheckParm, for the -potest self-check
#include "m_bbox.h"
#include "p_local.h"
#include "r_main.h"		// R_PointInSubsector
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
polyblock_t**	PolyBlockMap;

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

// Gather a polyobj's linedefs.
//
// Hexen marks them two different ways, and this is the part that is easy to get
// wrong: PO_LINE_EXPLICIT tags EVERY line of the object with the id (and an order
// in args[1]), but PO_LINE_START tags only the FIRST line -- the rest of the loop
// is found by walking the geometry, following each line's v2 to the line that
// starts there, until it closes back on the seed.
//
// Collecting purely by tag therefore yields a ONE-LINE "polyobj" for every
// start-line door, which is what the self-test caught: a door reduced to a single
// segment.  Walk the loop like Raven's IterFindPolySegs does.
static int PO_CollectLines (int id, line_t** out, int max)
{
    int		i, n = 0;
    line_t*	seed = NULL;

    // Explicitly-tagged form first: every line carries the id.
    for (i = 0; i < numlines && n < max; i++)
	if (lines[i].special == PO_LINE_EXPLICIT && lines[i].args[0] == id)
	    out[n++] = &lines[i];
    if (n)
    {
	// args[1] is the order within the object; sort so the loop is contiguous.
	int a, b;
	for (a = 0; a < n - 1; a++)
	    for (b = a + 1; b < n; b++)
		if (out[b]->args[1] < out[a]->args[1])
		    { line_t* t = out[a]; out[a] = out[b]; out[b] = t; }
	return n;
    }

    // Start-line form: one seed, then follow the loop.
    for (i = 0; i < numlines; i++)
	if (lines[i].special == PO_LINE_START && lines[i].args[0] == id)
	    { seed = &lines[i]; break; }
    if (!seed)
	return 0;

    out[n++] = seed;
    {
	vertex_t* want = seed->v2;
	while (want != seed->v1 && n < max)
	{
	    line_t* next = NULL;
	    for (i = 0; i < numlines; i++)
	    {
		int k, dup = 0;
		if (lines[i].v1 != want) continue;
		for (k = 0; k < n; k++)
		    if (out[k] == &lines[i]) { dup = 1; break; }
		if (dup) continue;
		next = &lines[i];
		break;
	    }
	    if (!next)
	    {
		printf ("PO_CollectLines: polyobj %d loop is open after %d line(s)\n",
			id, n);
		break;
	    }
	    out[n++] = next;
	    want = next->v2;
	}
    }
    return n;
}

// Gather the segs belonging to this polyobj's linedefs.
//
// Needed because the renderer's unit of work is the seg, not the linedef, and
// because a polyobj line may have been SPLIT by the node builder into several
// segs -- each with its own vertices, all of which have to travel with the
// object or it tears apart as it moves.
static void PO_BuildSegList (polyobj_t* po)
{
    seg_t*	found[POLY_MAXSEGS];
    int		n = 0, i, j;

    for (i = 0; i < numsegs && n < POLY_MAXSEGS; i++)
	for (j = 0; j < po->numlines; j++)
	    if (segs[i].linedef == po->lines[j])
		{ found[n++] = &segs[i]; break; }

    po->numsegs = n;
    po->segs = Z_Malloc (n * sizeof(*po->segs), PU_LEVEL, 0);
    memcpy (po->segs, found, n * sizeof(*po->segs));
}

// Build the deduplicated vertex list.  Vertices are SHARED between a polyobj's
// linedefs and segs, so every transform has to visit each exactly once --
// iterating lines and moving v1/v2 would shift shared corners twice and shear the
// object apart.
static void PO_BuildVertexList (polyobj_t* po)
{
    vertex_t*	seen[POLY_MAXVERTS];
    int		n = 0, i, j, k;

    // Take vertices from the SEGS as well as the linedefs.  The linedef endpoints
    // alone are not the whole object: a split line contributes a vertex that
    // belongs to no linedef, and leaving it behind shears the polyobj open.
    for (i = 0; i < po->numlines + po->numsegs; i++)
    {
	vertex_t* v[2];
	if (i < po->numlines)
	    { v[0] = po->lines[i]->v1;              v[1] = po->lines[i]->v2; }
	else
	    { v[0] = po->segs[i - po->numlines]->v1; v[1] = po->segs[i - po->numlines]->v2; }
	for (j = 0; j < 2; j++)
	{
	    for (k = 0; k < n; k++)
		if (seen[k] == v[j]) break;
	    if (k == n && n < POLY_MAXVERTS)
		seen[n++] = v[j];
	}
    }

    po->numverts = n;
    po->verts = Z_Malloc (n * sizeof(*po->verts), PU_LEVEL, 0);
    po->origX = Z_Malloc (n * sizeof(*po->origX), PU_LEVEL, 0);
    po->origY = Z_Malloc (n * sizeof(*po->origY), PU_LEVEL, 0);
    po->prevX = Z_Malloc (n * sizeof(*po->prevX), PU_LEVEL, 0);
    po->prevY = Z_Malloc (n * sizeof(*po->prevY), PU_LEVEL, 0);
    memcpy (po->verts, seen, n * sizeof(*po->verts));
}

// Recompute each linedef's cached geometry from its (moved) vertices.  dx/dy and
// the bbox are precomputed in P_LoadLineDefs and everything from collision to
// rendering reads those rather than the vertices themselves.
static void PO_UpdateLines (polyobj_t* po)
{
    int i;
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
	ld->slopetype = !ld->dx ? ST_VERTICAL
		      : !ld->dy ? ST_HORIZONTAL
		      : (FixedDiv (ld->dy, ld->dx) > 0) ? ST_POSITIVE : ST_NEGATIVE;
    }
}

static void PO_Translate (polyobj_t* po, fixed_t dx, fixed_t dy)
{
    int i;
    for (i = 0; i < po->numverts; i++)
    {
	po->verts[i]->x += dx;
	po->verts[i]->y += dy;
    }
    PO_UpdateLines (po);
}

// ---------------------------------------------------------------------------
// Blockmap linkage
//
// A polyobj occupies a RANGE of blockmap cells and moves, so it cannot live in
// the ordinary blocklinks (those are for point-like mobjs).  Hexen keeps a
// parallel PolyBlockMap: one list of overlapping polyobjs per cell, rebuilt on
// every move.  Nothing reads it until step 4 -- this just keeps it correct.
// ---------------------------------------------------------------------------

static void PO_UnLinkPolyobj (polyobj_t* po)
{
    int x, y;
    if (!po->linked || !PolyBlockMap) return;
    for (y = po->bbox[BOXBOTTOM]; y <= po->bbox[BOXTOP]; y++)
	for (x = po->bbox[BOXLEFT]; x <= po->bbox[BOXRIGHT]; x++)
	{
	    polyblock_t** link;
	    if (x < 0 || x >= bmapwidth || y < 0 || y >= bmapheight) continue;
	    for (link = &PolyBlockMap[y*bmapwidth + x]; *link; link = &(*link)->next)
		if ((*link)->polyobj == po)
		{
		    polyblock_t* dead = *link;
		    *link = dead->next;		// Z_Free'd with the level (PU_LEVEL)
		    break;
		}
	}
    po->linked = false;
}

static void PO_LinkPolyobj (polyobj_t* po)
{
    fixed_t	l, r, t, b;
    int		i, x, y;

    if (!PolyBlockMap || !po->numverts) return;

    l = r = po->verts[0]->x;
    t = b = po->verts[0]->y;
    for (i = 1; i < po->numverts; i++)
    {
	if (po->verts[i]->x < l) l = po->verts[i]->x;
	if (po->verts[i]->x > r) r = po->verts[i]->x;
	if (po->verts[i]->y < b) b = po->verts[i]->y;
	if (po->verts[i]->y > t) t = po->verts[i]->y;
    }
    po->bbox[BOXLEFT]   = (l - bmaporgx) >> MAPBLOCKSHIFT;
    po->bbox[BOXRIGHT]  = (r - bmaporgx) >> MAPBLOCKSHIFT;
    po->bbox[BOXBOTTOM] = (b - bmaporgy) >> MAPBLOCKSHIFT;
    po->bbox[BOXTOP]    = (t - bmaporgy) >> MAPBLOCKSHIFT;

    for (y = po->bbox[BOXBOTTOM]; y <= po->bbox[BOXTOP]; y++)
	for (x = po->bbox[BOXLEFT]; x <= po->bbox[BOXRIGHT]; x++)
	{
	    polyblock_t* pb;
	    if (x < 0 || x >= bmapwidth || y < 0 || y >= bmapheight) continue;
	    pb = Z_Malloc (sizeof(*pb), PU_LEVEL, 0);
	    pb->polyobj = po;
	    pb->next = PolyBlockMap[y*bmapwidth + x];
	    PolyBlockMap[y*bmapwidth + x] = pb;
	}
    po->linked = true;
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

void PO_ClearLevel (void)
{
    polyobjs = NULL;
    po_NumPolyobjs = 0;
    PolyBlockMap = NULL;
}

// Is anything solid standing where the polyobj has just been put?
//
// Called AFTER the move, on the new geometry: the cheap test is to ask whether a
// mobj's bounding box now straddles one of the polyobj's lines.  A polyobj that is
// merely passing a thing does not count -- only one that would end up overlapping
// it -- which is why this runs on the result rather than sweeping the path.
static boolean PO_CheckMobjBlocking (line_t* ld, polyobj_t* po)
{
    int		left, right, top, bottom, i, j;
    boolean	blocked = false;

    // Widen by MAXRADIUS: blocklinks holds a thing in the cell of its CENTRE, so a
    // thing whose centre is a cell away can still overlap this line.
    left   = (ld->bbox[BOXLEFT]   - bmaporgx - MAXRADIUS) >> MAPBLOCKSHIFT;
    right  = (ld->bbox[BOXRIGHT]  - bmaporgx + MAXRADIUS) >> MAPBLOCKSHIFT;
    bottom = (ld->bbox[BOXBOTTOM] - bmaporgy - MAXRADIUS) >> MAPBLOCKSHIFT;
    top    = (ld->bbox[BOXTOP]    - bmaporgy + MAXRADIUS) >> MAPBLOCKSHIFT;

    if (left   < 0) left   = 0;
    if (bottom < 0) bottom = 0;
    if (right  >= bmapwidth)  right = bmapwidth  - 1;
    if (top    >= bmapheight) top   = bmapheight - 1;

    for (j = bottom; j <= top; j++)
	for (i = left; i <= right; i++)
	{
	    mobj_t* mo;
	    for (mo = blocklinks[j*bmapwidth + i]; mo; mo = mo->bnext)
	    {
		fixed_t box[4];
		if (!(mo->flags & MF_SOLID) && !mo->player)
		    continue;
		box[BOXTOP]    = mo->y + mo->radius;
		box[BOXBOTTOM] = mo->y - mo->radius;
		box[BOXLEFT]   = mo->x - mo->radius;
		box[BOXRIGHT]  = mo->x + mo->radius;

		if (box[BOXRIGHT]  <= ld->bbox[BOXLEFT]
		 || box[BOXLEFT]   >= ld->bbox[BOXRIGHT]
		 || box[BOXTOP]    <= ld->bbox[BOXBOTTOM]
		 || box[BOXBOTTOM] >= ld->bbox[BOXTOP])
		    continue;
		if (P_BoxOnLineSide (box, ld) != -1)
		    continue;		// wholly on one side: not straddling the line

		if (po->crush)
		    P_DamageMobj (mo, NULL, NULL, 3);
		blocked = true;
	    }
	}
    return blocked;
}

// Put every vertex back where PO_SavePrev recorded it.
static void PO_Restore (polyobj_t* po)
{
    int i;
    for (i = 0; i < po->numverts; i++)
    {
	po->verts[i]->x = po->prevX[i];
	po->verts[i]->y = po->prevY[i];
    }
    PO_UpdateLines (po);
}

// Did that move squash anything?  If so undo it and say so, so the thinker can
// reverse a door instead of grinding through the player.
static boolean PO_Blocked (polyobj_t* po)
{
    int i;
    for (i = 0; i < po->numlines; i++)
	if (PO_CheckMobjBlocking (po->lines[i], po))
	    return true;
    return false;
}

polyobj_t* PO_GetPolyobj (int id)
{
    int i;
    for (i = 0; i < po_NumPolyobjs; i++)
	if (polyobjs[i].id == id)
	    return &polyobjs[i];
    return NULL;
}

static void PO_SavePrev (polyobj_t* po)
{
    int i;
    for (i = 0; i < po->numverts; i++)
    {
	po->prevX[i] = po->verts[i]->x;
	po->prevY[i] = po->verts[i]->y;
    }
}

boolean PO_MovePolyobj (int id, fixed_t dx, fixed_t dy)
{
    polyobj_t* po = PO_GetPolyobj (id);
    if (!po) return false;

    PO_UnLinkPolyobj (po);
    PO_SavePrev (po);
    PO_Translate (po, dx, dy);

    if (PO_Blocked (po))
    {
	PO_Restore (po);
	PO_LinkPolyobj (po);
	return false;		// caller decides: reverse, wait, or give up
    }

    po->startX += dx;
    po->startY += dy;
    PO_LinkPolyobj (po);
    return true;
}

boolean PO_RotatePolyobj (int id, angle_t delta)
{
    polyobj_t*	po = PO_GetPolyobj (id);
    int		i;
    angle_t	na;
    unsigned	fine;

    if (!po) return false;

    PO_UnLinkPolyobj (po);
    PO_SavePrev (po);

    // Rotate the ORIGINAL offsets by the accumulated angle rather than nudging the
    // live vertices each tic: repeated incremental rotation in 16.16 fixed point
    // drifts, and a door that has swung back and forth for a while would no longer
    // meet its frame.
    //
    // Even so this is not bit-exact, and that is the tables, not the method: DOOM
    // samples finesine half a fine-unit off centre, so finesine[0] == 25 and
    // finecosine[0] == 65531.  A full 360-degree turn therefore lands ~0.006 map
    // units from where it started rather than exactly on it.  Well below anything
    // visible, and Raven's original has the same property -- but worth knowing
    // before chasing it as a bug (it was, once).
    na = po->angle + delta;
    fine = na >> ANGLETOFINESHIFT;
    for (i = 0; i < po->numverts; i++)
    {
	fixed_t ox = po->origX[i], oy = po->origY[i];
	po->verts[i]->x = po->startX + FixedMul (ox, finecosine[fine])
				     - FixedMul (oy, finesine[fine]);
	po->verts[i]->y = po->startY + FixedMul (ox, finesine[fine])
				     + FixedMul (oy, finecosine[fine]);
    }
    PO_UpdateLines (po);
    if (PO_Blocked (po))
    {
	PO_Restore (po);
	PO_LinkPolyobj (po);
	return false;		// angle and seg angles left untouched
    }

    po->angle = na;

    // seg->angle is a CACHED value, computed once at load from the seg's direction,
    // and r_segs.c derives the wall's texture mapping from it (rw_normalangle).
    // Move a seg without turning it here and the wall renders with the texture
    // skewed as though it were still facing its original way.
    for (i = 0; i < po->numsegs; i++)
	po->segs[i]->angle += delta;

    PO_UpdateLines (po);
    PO_LinkPolyobj (po);
    return true;
}

// ---------------------------------------------------------------------------
// Thinkers
// ---------------------------------------------------------------------------

// Hexen's polyevent_t, trimmed: no sound sequences here.
typedef struct
{
    thinker_t	thinker;
    int		polyobj;
    int		speed;
    unsigned	dist;
    int		angle;		// rotation: fine angle step; translation: direction
    fixed_t	xSpeed, ySpeed;
} polyevent_t;

static void PO_Finished (polyobj_t* po)
{
    if (po) po->specialdata = NULL;
}

void T_RotatePoly (thinker_t* thinker)
{
    polyevent_t*	pe = (polyevent_t*) thinker;
    polyobj_t*		po = PO_GetPolyobj (pe->polyobj);
    unsigned		absSpeed;

    if (!po)				// nothing left to drive
	{ P_RemoveThinker (&pe->thinker); return; }

    // Blocked by something solid.  Do NOT charge the tic against the remaining
    // distance and do NOT give up -- just retry next tic, so the object carries on
    // by itself once whatever is in the way moves.  Dropping the thinker here would
    // leave the object frozen part-way for the rest of the level.
    if (!PO_RotatePolyobj (pe->polyobj, pe->speed))
	return;

    if (pe->dist == (unsigned)-1)	// perpetual
	return;

    absSpeed = (unsigned) abs (pe->speed);
    if (pe->dist <= absSpeed)
    {
	PO_Finished (po);
	P_RemoveThinker (&pe->thinker);
	return;
    }
    pe->dist -= absSpeed;
}

void T_MovePoly (thinker_t* thinker)
{
    polyevent_t*	pe = (polyevent_t*) thinker;
    polyobj_t*		po = PO_GetPolyobj (pe->polyobj);
    unsigned		absSpeed;

    if (!po)
	{ P_RemoveThinker (&pe->thinker); return; }

    if (!PO_MovePolyobj (pe->polyobj, pe->xSpeed, pe->ySpeed))
	return;				// blocked -- retry next tic, see T_RotatePoly

    absSpeed = (unsigned) abs (pe->speed);
    if (pe->dist <= absSpeed)
    {
	PO_Finished (po);
	P_RemoveThinker (&pe->thinker);
	return;
    }
    pe->dist -= absSpeed;
}

// A polyobj door: run out, wait, come back.  Hexen models sliding and swinging
// doors with the same thinker, differing only in which transform it drives.
typedef struct
{
    thinker_t		thinker;
    int			polyobj;
    int			speed;
    int			dist;
    int			totalDist;
    int			direction;
    fixed_t		xSpeed, ySpeed;
    int			tics;
    int			waitTics;
    podoortype_t	type;
    boolean		close;
} polydoor_t;

void T_PolyDoor (thinker_t* thinker)
{
    polydoor_t*	pd = (polydoor_t*) thinker;
    polyobj_t*	po = PO_GetPolyobj (pd->polyobj);

    boolean	moved;

    if (!po)
	{ P_RemoveThinker (&pd->thinker); return; }

    if (pd->tics)			// holding open
	{ --pd->tics; return; }

    moved = (pd->type == PODOOR_SLIDE)
	  ? PO_MovePolyobj (pd->polyobj, pd->xSpeed, pd->ySpeed)
	  : PO_RotatePolyobj (pd->polyobj, pd->speed);

    if (!moved)
    {
	// Something solid is in the way.  A crusher keeps pushing, and a door that
	// is still OPENING simply waits for the obstruction to clear.  A door caught
	// CLOSING on someone opens back up -- the same courtesy an ordinary DOOM door
	// extends, and without it a polyobj door would pin the player against its
	// frame with nothing to do about it.
	if (po->crush || !pd->close)
	    return;
	pd->dist   = pd->totalDist - pd->dist;	// however far it had already closed
	pd->xSpeed = -pd->xSpeed;
	pd->ySpeed = -pd->ySpeed;
	pd->speed  = -pd->speed;
	pd->close  = false;
	return;
    }

    pd->dist -= abs (pd->speed ? pd->speed : 1);
    if (pd->dist > 0)
	return;

    if (pd->close)			// back home: done
    {
	PO_Finished (po);
	P_RemoveThinker (&pd->thinker);
	return;
    }

    // Fully open: reverse now, then hold for waitTics before actually moving.
    pd->close  = true;
    pd->dist   = pd->totalDist;
    pd->xSpeed = -pd->xSpeed;
    pd->ySpeed = -pd->ySpeed;
    pd->speed  = -pd->speed;
    pd->tics   = pd->waitTics;
}

// ---------------------------------------------------------------------------
// The line specials ACS calls (wired up in step 5)
// ---------------------------------------------------------------------------

boolean EV_RotatePoly (byte* args, int direction, boolean override)
{
    polyobj_t*		po = PO_GetPolyobj (args[0]);
    polyevent_t*	pe;

    if (!po) return false;
    if (po->specialdata && !override) return false;

    pe = Z_Malloc (sizeof(*pe), PU_LEVEL, 0);
    memset (pe, 0, sizeof(*pe));
    pe->thinker.function.acp1 = (actionf_p1) T_RotatePoly;
    pe->polyobj = args[0];
    pe->speed   = (args[1] * direction * ANG90) / 64;		// Hexen's units
    pe->dist    = args[2] ? (unsigned)((args[2] * ANG90) / 64) : (unsigned)-1;
    po->specialdata = pe;
    P_AddThinker (&pe->thinker);
    return true;
}

boolean EV_MovePoly (byte* args, boolean timesEight, boolean override)
{
    polyobj_t*		po = PO_GetPolyobj (args[0]);
    polyevent_t*	pe;
    angle_t		an;

    if (!po) return false;
    if (po->specialdata && !override) return false;

    pe = Z_Malloc (sizeof(*pe), PU_LEVEL, 0);
    memset (pe, 0, sizeof(*pe));
    pe->thinker.function.acp1 = (actionf_p1) T_MovePoly;
    pe->polyobj = args[0];
    pe->dist    = timesEight ? args[3] * 8 * FRACUNIT : args[3] * FRACUNIT;
    pe->speed   = (args[1] * FRACUNIT) / 8;
    an = (unsigned)(args[2] * (ANG90 / 64)) >> ANGLETOFINESHIFT;
    pe->xSpeed  = FixedMul (pe->speed, finecosine[an]);
    pe->ySpeed  = FixedMul (pe->speed, finesine[an]);
    po->specialdata = pe;
    P_AddThinker (&pe->thinker);
    return true;
}

boolean EV_OpenPolyDoor (byte* args, podoortype_t type)
{
    polyobj_t*	po = PO_GetPolyobj (args[0]);
    polydoor_t*	pd;
    angle_t	an;

    if (!po) return false;
    if (po->specialdata) return false;

    pd = Z_Malloc (sizeof(*pd), PU_LEVEL, 0);
    memset (pd, 0, sizeof(*pd));
    pd->thinker.function.acp1 = (actionf_p1) T_PolyDoor;
    pd->polyobj  = args[0];
    pd->type     = type;
    pd->close    = false;
    if (type == PODOOR_SLIDE)
    {
	pd->waitTics = args[4];
	pd->speed    = (args[1] * FRACUNIT) / 8;
	pd->totalDist = pd->dist = args[3] * FRACUNIT;
	an = (unsigned)(args[2] * (ANG90 / 64)) >> ANGLETOFINESHIFT;
	pd->xSpeed = FixedMul (pd->speed, finecosine[an]);
	pd->ySpeed = FixedMul (pd->speed, finesine[an]);
    }
    else					// swing
    {
	pd->waitTics = args[3];
	pd->speed    = (args[1] * ANG90) / 64;
	pd->totalDist = pd->dist = (args[2] * ANG90) / 64;
    }
    po->specialdata = pd;
    P_AddThinker (&pd->thinker);
    return true;
}

// ---------------------------------------------------------------------------
// PO_Init -- called from P_SetupLevel once the geometry and THINGS are loaded.
//
// `spots` is every polyobj map thing the THINGS pass saw, in map order; a thing's
// ANGLE field carries the polyobj id, not a facing.
// ---------------------------------------------------------------------------
// -potest: p_sight.c's working state, so the self-test can cast a sight line by
// hand the way P_CheckSight does, without needing two mobjs to sight between.
extern divline_t	strace;
extern fixed_t		t2x, t2y, sightzstart, topslope, bottomslope;
extern boolean		P_CrossBSPNode (int bspnum);

// Is the straight line (x1,y1)->(x2,y2) unobstructed?
static boolean PO_TestSight (fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2)
{
    validcount++;
    sightzstart = 0;
    topslope    =  FRACUNIT;		// a wide vertical window, so only the
    bottomslope = -FRACUNIT;		// geometry decides, not the heights
    strace.x  = x1;  strace.y  = y1;
    strace.dx = x2 - x1;
    strace.dy = y2 - y1;
    t2x = x2;  t2y = y2;
    return P_CrossBSPNode (numnodes - 1);
}

// -potest scratch: collects the lines P_BlockLinesIterator hands back.
static line_t*	potest_lines[POLY_MAXLINES * 4];
static int	potest_found;

static boolean PO_CountTestLine (line_t* ld)
{
    if (potest_found < (int)(sizeof(potest_lines)/sizeof(potest_lines[0])))
	potest_lines[potest_found++] = ld;
    return true;			// keep going, we want them all
}

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
    PolyBlockMap = Z_Malloc (bmapwidth * bmapheight * sizeof(*PolyBlockMap),
			     PU_LEVEL, 0);
    memset (PolyBlockMap, 0, bmapwidth * bmapheight * sizeof(*PolyBlockMap));

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

	PO_BuildSegList (po);		// before the vertex list -- it feeds it
	PO_BuildVertexList (po);
	PO_Translate (po, po->startX - (anchor->x << FRACBITS),
			  po->startY - (anchor->y << FRACBITS));
    }

    // Cache each vertex relative to its start spot in a SECOND pass, once every
    // polyobj has been translated.  Doing it inline above looked equivalent and was
    // not: polyobjs can share a vertex, so translating a later one moved a vertex an
    // earlier one had already cached, leaving that cache stale.  A 360-degree
    // rotation then snapped the shared vertex to the stale offset instead of back
    // where it started -- sub-unit drift that the self-test caught and that would
    // have shown up in game as a door slowly not meeting its frame.
    for (i = 0; i < npo; i++)
    {
	polyobj_t* po = &polyobjs[i];
	fixed_t ax = 0, ay = 0;

	for (j = 0; j < po->numverts; j++)
	{
	    po->origX[j] = po->verts[j]->x - po->startX;
	    po->origY[j] = po->verts[j]->y - po->startY;
	    ax += po->verts[j]->x >> FRACBITS;	// sum in map units: 128 vertices
	    ay += po->verts[j]->y >> FRACBITS;	// * 32767 still fits an int
	}
	PO_LinkPolyobj (po);

	// Attach to the BSP leaf containing the polyobj's CENTRE, not its start
	// spot.  The two are usually the same, but the start spot is only a map
	// thing the author dropped somewhere inside the object -- on a long
	// polyobj it can sit in a different leaf than the bulk of the geometry,
	// and then the object draws only from a spot the player may never stand.
	if (po->numverts)
	{
	    subsector_t* sub = R_PointInSubsector ((ax / po->numverts) << FRACBITS,
						   (ay / po->numverts) << FRACBITS);
	    if (sub->poly)
	    {
		// Raven calls I_Error here.  A missing polyobj is a lesser evil
		// than refusing to load the map, so warn and leave the first one
		// attached -- the second simply will not draw.
		printf ("PO_Init: polyobjs %d and %d share a subsector; "
			"%d will not be drawn\n", sub->poly->id, po->id, po->id);
	    }
	    // -nopolydraw loads the polyobj but leaves it unattached, so it does not
	    // draw.  Kept because it is the only way to A/B this: run the same scene
	    // with and without it under -shotat and diff the two frames.  That is how
	    // step 3 was verified -- the diff was the doorway, and nothing else.
	    else if (!M_CheckParm ("-nopolydraw"))
	    {
		sub->poly = po;
		po->subsector = sub;
	    }
	}
    }

    po_NumPolyobjs = npo;

    // Integrity: no linedef may belong to two polyobjs.  They would then drag each
    // other's geometry about -- each transform moving vertices the other also owns
    // -- and neither would end up where either thinks it is.  The start-line form
    // is walked by following v2 to the next line that begins there, so two objects
    // meeting at a shared vertex (the leaves of a double door) can send that walk
    // into the neighbour's geometry.
    {
	int a, b, i, j;
	for (a = 0; a < npo; a++)
	    for (b = a + 1; b < npo; b++)
	{
	    for (i = 0; i < polyobjs[a].numlines; i++)
		for (j = 0; j < polyobjs[b].numlines; j++)
		    if (polyobjs[a].lines[i] == polyobjs[b].lines[j])
			printf ("PO_Init: polyobjs %d and %d both claim linedef %d\n",
				polyobjs[a].id, polyobjs[b].id,
				(int)(polyobjs[a].lines[i] - lines));
	    // Sharing a VERTEX is just as bad and much easier to do by accident:
	    // WAD vertices are a shared pool, so two objects whose corners land on
	    // the same coordinate get the same vertex_t, and then moving one drags
	    // a corner of the other with it.
	    for (i = 0; i < polyobjs[a].numverts; i++)
		for (j = 0; j < polyobjs[b].numverts; j++)
		    if (polyobjs[a].verts[i] == polyobjs[b].verts[j])
			printf ("PO_Init: polyobjs %d and %d share vertex %d (%d,%d)\n",
				polyobjs[a].id, polyobjs[b].id,
				(int)(polyobjs[a].verts[i] - vertexes),
				polyobjs[a].verts[i]->x >> FRACBITS,
				polyobjs[a].verts[i]->y >> FRACBITS);
	}
    }

    // Self-test, opt-in with -potest.  Nothing calls the transforms until step 5,
    // so without this there is no way to know they are right before steps 3 and 4
    // start depending on them -- and it has already earned its keep twice: it caught
    // PO_CollectLines returning a ONE-LINE door (start-line polyobjs need the loop
    // walked, not just the tag read), and it forced the rotation accuracy question
    // below to be answered rather than assumed.
    //
    // TOLERANCE, not equality: DOOM's trig tables are sampled half a fine-unit off
    // centre, so finesine[0] is 25 and finecosine[0] is 65531 rather than 0 and
    // 65536.  A "net zero" rotation through them is therefore not the identity, and
    // a round trip lands slightly off by construction.  Raven's original has the
    // same property.
    //
    // The error is a fixed FRACTION of the radius (~25/65536, about 1/2600), not a
    // fixed distance, so the tolerance has to scale with the object: a 400-unit
    // bridge on MAP05 legitimately drifts 0.15 units where a 16-unit door drifts
    // 0.006.  Allow radius/1024 -- roughly 2.5x the predicted error, still far
    // below a pixel -- with a floor for small objects.
    if (npo && M_CheckParm ("-potest"))
    {
	int t, v, bad = 0, blocked = 0, sightok = 0, sightskip = 0;
	for (t = 0; t < npo; t++)
	{
	    polyobj_t* po = &polyobjs[t];
	    fixed_t sx[POLY_MAXVERTS], sy[POLY_MAXVERTS];
	    angle_t sang[POLY_MAXSEGS], spoang = po->angle;
	    fixed_t radius = 0, TOL;
	    boolean stopped = false;
	    for (v = 0; v < po->numverts; v++)
	    {
		if (abs (po->origX[v]) > radius) radius = abs (po->origX[v]);
		if (abs (po->origY[v]) > radius) radius = abs (po->origY[v]);
	    }
	    TOL = radius / 1024;
	    if (TOL < FRACUNIT / 16) TOL = FRACUNIT / 16;

	    for (v = 0; v < po->numverts; v++)
		{ sx[v] = po->verts[v]->x; sy[v] = po->verts[v]->y; }
	    for (v = 0; v < po->numsegs; v++)
		sang[v] = po->segs[v]->angle;

	    // A transform can now legitimately FAIL: since step 4 a polyobj refuses to
	    // move into something solid, and by the time PO_Init runs the map's things
	    // are already spawned -- several of Hexen's doors have an ettin standing in
	    // the arc they would sweep.  A blocked object is left where it was, so the
	    // round-trip identity below does not hold and asserting it would report a
	    // working feature as a fault.  Count those separately: they are evidence the
	    // blocking path RUNS, not evidence of a bug.
	    for (v = 0; v < 8; v++)
		if (!PO_RotatePolyobj (po->id, ANG45)) { stopped = true; break; }

	    if (!stopped)
		for (v = 0; v < po->numverts; v++)
		    if (abs (po->verts[v]->x - sx[v]) > TOL
		     || abs (po->verts[v]->y - sy[v]) > TOL)
			{ printf ("  PO_SELFTEST: po%d vertex %d drifted %d,%d after a full "
				  "rotation\n", po->id, v,
				  (int)(po->verts[v]->x - sx[v]),
				  (int)(po->verts[v]->y - sy[v]));
			  bad++; break; }

	    // Translation must be EXACT -- it is plain addition, no trig involved.
	    if (!stopped)
	    {
		if (!PO_MovePolyobj (po->id,  64*FRACUNIT, -32*FRACUNIT)
		 || !PO_MovePolyobj (po->id, -64*FRACUNIT,  32*FRACUNIT))
		    stopped = true;
		else
		    for (v = 0; v < po->numverts; v++)
			if (abs (po->verts[v]->x - sx[v]) > TOL
			 || abs (po->verts[v]->y - sy[v]) > TOL)
			    { printf ("  PO_SELFTEST: po%d vertex %d drifted after a "
				      "there-and-back move\n", po->id, v); bad++; break; }
	    }

	    if (stopped)
		blocked++;

	    // Put it back exactly as it was found.  A transform that is blocked
	    // part-way leaves the object part-turned -- 135 degrees round, say --
	    // and everything after this would then be judging a position the map
	    // never puts it in.  It also means -potest no longer perturbs the level
	    // it is inspecting, so a screenshot from the same run is still the real
	    // level.  (This is why two polyobjs looked like they did not block
	    // sight: they did, just not from where the test had left them.)
	    PO_UnLinkPolyobj (po);
	    for (v = 0; v < po->numverts; v++)
		{ po->verts[v]->x = sx[v]; po->verts[v]->y = sy[v]; }
	    for (v = 0; v < po->numsegs; v++)
		po->segs[v]->angle = sang[v];
	    po->angle = spoang;
	    PO_UpdateLines (po);
	    PO_LinkPolyobj (po);

	    if (!po->linked)
		{ printf ("  PO_SELFTEST: po%d lost its blockmap linkage\n", po->id); bad++; }

	    // Every one of the polyobj's lines must come back out of
	    // P_BlockLinesIterator, or it is not solid: that iterator is what
	    // movement, shooting and traversal all collide against.  Checking the
	    // linkage alone is not enough -- the list can be correct while the
	    // iterator that reads it is not.
	    {
		int cx, cy, seen = 0;
		potest_found = 0;
		validcount++;
		for (cy = po->bbox[BOXBOTTOM]; cy <= po->bbox[BOXTOP]; cy++)
		    for (cx = po->bbox[BOXLEFT]; cx <= po->bbox[BOXRIGHT]; cx++)
			P_BlockLinesIterator (cx, cy, PO_CountTestLine);
		for (v = 0; v < po->numlines; v++)
		{
		    int f;
		    for (f = 0; f < potest_found; f++)
			if (potest_lines[f] == po->lines[v]) { seen++; break; }
		}
		if (seen != po->numlines)
		{
		    printf ("  PO_SELFTEST: po%d -- the blockmap iterator returned "
			    "%d of its %d line(s); it is not solid\n",
			    po->id, seen, po->numlines);
		    bad++;
		}
	    }

	    // Sight: a monster must not be able to see through a closed polyobj door.
	    // Cast a ray from the polyobj's centre outward through one of its own
	    // walls -- and cast the SAME ray with the polyobj detached, so the result
	    // is unambiguous.  Blocked either way just means some other wall is in the
	    // line: that tells us nothing, so it is counted as inconclusive rather
	    // than passed.
	    if (po->subsector && po->numlines && po->numverts)
	    {
		fixed_t	cx = 0, cy = 0;
		int	verdict = 0;		// -1 fail, +1 pass, 0 no verdict

		// From the CENTROID, not startX/startY.  The start spot is the hinge
		// a swinging door turns about, which sits on the object's EDGE -- a
		// ray from there runs along the door rather than through it, and
		// reports "does not block sight" for a door that plainly does.
		for (v = 0; v < po->numverts; v++)
		    { cx += po->verts[v]->x >> FRACBITS; cy += po->verts[v]->y >> FRACBITS; }
		cx = (cx / po->numverts) << FRACBITS;
		cy = (cy / po->numverts) << FRACBITS;

		// Try each wall in turn.  A door's narrow ends face its jamb, so a ray
		// out through one of those is blocked with or without the polyobj and
		// settles nothing; its broad faces look into open rooms.  Which line is
		// which varies, so just take the first that gives a usable answer.
		for (v = 0; v < po->numlines && !verdict; v++)
		{
		    line_t*	ln = po->lines[v];
		    fixed_t	mx = ln->v1->x + ((ln->v2->x - ln->v1->x) >> 1);
		    fixed_t	my = ln->v1->y + ((ln->v2->y - ln->v1->y) >> 1);
		    fixed_t	ex = mx + (mx - cx);	// out past the wall
		    fixed_t	ey = my + (my - cy);
		    polyobj_t*	saved;

		    if (mx == cx && my == cy)
			continue;

		    saved = po->subsector->poly;
		    po->subsector->poly = NULL;
		    if (!PO_TestSight (cx, cy, ex, ey))
			{ po->subsector->poly = saved; continue; }	// no verdict
		    po->subsector->poly = saved;

		    verdict = PO_TestSight (cx, cy, ex, ey) ? -1 : 1;
		}

		if (verdict > 0)
		    sightok++;
		else if (verdict < 0)
		    { printf ("  PO_SELFTEST: po%d does not block sight\n", po->id); bad++; }
		else
		    sightskip++;
	    }
	}
	printf ("PO_SELFTEST: %d polyobj(s), %d failure(s); %d blocked by something "
		"solid standing in them; sight blocked by %d, inconclusive for %d\n",
		npo, bad, blocked, sightok, sightskip);
    }

    if (npo)
    {
	int t, nseg = 0, ndrawn = 0;
	for (t = 0; t < npo; t++)
	    { nseg += polyobjs[t].numsegs; if (polyobjs[t].subsector) ndrawn++; }
	printf ("PO_Init: %d polyobject(s), %d seg(s), %d attached to a subsector "
		"(steps 1-4 of 5: they draw, move and block; nothing drives them yet)\n",
		npo, nseg, ndrawn);
    }
}
