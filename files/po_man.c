// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1993-2008 Raven Software (Hexen polyobjects -- reference for this port)
//
// DESCRIPTION:
//	(X) Polyobjects -- STEPS 1-2 of 5: placement, movement, blockmap linkage.
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
//	  2. DONE (this step) -- rotate/translate + the thinkers that drive them, and
//	     blockmap link/unlink on every move.
//	  3. r_bsp.c: per-subsector polyobj seg lists, so the moved lines DRAW
//	  4. p_maputl.c / p_sight.c: include them in blockmap iteration, so they BLOCK
//	  5. p_acs.c: wire the Polyobj_* specials into P_ExecuteLineSpecial
//
//	After step 2 a polyobj can be moved and rotated, and its blockmap linkage is
//	maintained as it goes -- but STILL nothing observable changes, because nothing
//	reads that linkage yet (step 4) and the moved lines are not drawn yet (step 3).
//	Blocking actors is deliberately step 4's job too, so a moving polyobj currently
//	passes through things.  Each step stays inert until the one that consumes it
//	lands, which is what keeps the four working games out of the blast radius.
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

// Build the deduplicated vertex list.  Vertices are SHARED between a polyobj's
// linedefs, so every transform has to visit each exactly once -- iterating lines
// and moving v1/v2 would shift shared corners twice and shear the object apart.
static void PO_BuildVertexList (polyobj_t* po)
{
    vertex_t*	seen[POLY_MAXVERTS];
    int		n = 0, i, j, k;

    for (i = 0; i < po->numlines; i++)
    {
	vertex_t* v[2];
	v[0] = po->lines[i]->v1;
	v[1] = po->lines[i]->v2;
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
    po->startX += dx;
    po->startY += dy;
    PO_LinkPolyobj (po);
    // Blocking actors is step 4's job -- it needs blockmap iteration over the
    // polyobj's lines, which nothing does yet.  Until then a polyobj slides
    // through anything standing in it.
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
    po->angle = na;
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

    if (!PO_RotatePolyobj (pe->polyobj, pe->speed))
	{ P_RemoveThinker (&pe->thinker); return; }

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

    if (!PO_MovePolyobj (pe->polyobj, pe->xSpeed, pe->ySpeed))
	{ P_RemoveThinker (&pe->thinker); return; }

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

    if (pd->tics)
    {
	if (!--pd->tics)		// waited long enough -- head back
	{
	    pd->close = true;
	    pd->dist = pd->totalDist;
	    pd->xSpeed = -pd->xSpeed;
	    pd->ySpeed = -pd->ySpeed;
	    pd->speed  = -pd->speed;
	}
	return;
    }

    if (pd->type == PODOOR_SLIDE)
	PO_MovePolyobj (pd->polyobj, pd->xSpeed, pd->ySpeed);
    else
	PO_RotatePolyobj (pd->polyobj, pd->speed);

    pd->dist -= abs (pd->speed ? pd->speed : 1);
    if (pd->dist > 0)
	return;

    if (pd->close)			// back home: done
    {
	PO_Finished (po);
	P_RemoveThinker (&pd->thinker);
	return;
    }
    pd->tics = pd->waitTics;		// fully open: hold
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
	for (j = 0; j < po->numverts; j++)
	{
	    po->origX[j] = po->verts[j]->x - po->startX;
	    po->origY[j] = po->verts[j]->y - po->startY;
	}
	PO_LinkPolyobj (po);
    }

    po_NumPolyobjs = npo;

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
	int t, v, bad = 0;
	for (t = 0; t < npo; t++)
	{
	    polyobj_t* po = &polyobjs[t];
	    fixed_t sx[POLY_MAXVERTS], sy[POLY_MAXVERTS];
	    fixed_t radius = 0, TOL;
	    for (v = 0; v < po->numverts; v++)
	    {
		if (abs (po->origX[v]) > radius) radius = abs (po->origX[v]);
		if (abs (po->origY[v]) > radius) radius = abs (po->origY[v]);
	    }
	    TOL = radius / 1024;
	    if (TOL < FRACUNIT / 16) TOL = FRACUNIT / 16;

	    for (v = 0; v < po->numverts; v++)
		{ sx[v] = po->verts[v]->x; sy[v] = po->verts[v]->y; }

	    for (v = 0; v < 8; v++) PO_RotatePolyobj (po->id, ANG45);
	    for (v = 0; v < po->numverts; v++)
		if (abs (po->verts[v]->x - sx[v]) > TOL
		 || abs (po->verts[v]->y - sy[v]) > TOL)
		    { printf ("  PO_SELFTEST: po%d vertex %d drifted %d,%d after a full "
			      "rotation\n", po->id, v,
			      (int)(po->verts[v]->x - sx[v]),
			      (int)(po->verts[v]->y - sy[v]));
		      bad++; break; }

	    // Translation must be EXACT -- it is plain addition, no trig involved.
	    PO_MovePolyobj (po->id,  64*FRACUNIT, -32*FRACUNIT);
	    PO_MovePolyobj (po->id, -64*FRACUNIT,  32*FRACUNIT);
	    for (v = 0; v < po->numverts; v++)
		if (abs (po->verts[v]->x - sx[v]) > TOL
		 || abs (po->verts[v]->y - sy[v]) > TOL)
		    { printf ("  PO_SELFTEST: po%d vertex %d drifted after a there-and-back "
			      "move\n", po->id, v); bad++; break; }

	    if (!po->linked)
		{ printf ("  PO_SELFTEST: po%d lost its blockmap linkage\n", po->id); bad++; }
	}
	printf ("PO_SELFTEST: %d polyobj(s), %d failure(s)\n", npo, bad);
    }

    if (npo)
	printf ("PO_Init: %d polyobject(s) placed and blockmap-linked "
		"(steps 1-2 of 5: they move, but do not draw or block yet)\n", npo);
}
