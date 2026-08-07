// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(X) Polyobjects -- see files/po_man.c for the staged plan.
//
//-----------------------------------------------------------------------------

#ifndef __PO_MAN__
#define __PO_MAN__

#include "doomtype.h"
#include "d_think.h"
#include "r_defs.h"

#define POLY_MAXOBJS	64	// polyobjects per map
#define POLY_MAXLINES	64	// linedefs per polyobject
#define POLY_MAXSEGS	(POLY_MAXLINES * 4)	// the node builder may SPLIT a line
#define POLY_MAXVERTS	POLY_MAXSEGS		// ...and each split adds a vertex

// One polyobj map thing, as seen by the THINGS pass.  Hexen puts the polyobj id in
// the thing's ANGLE field rather than a facing, so it is carried separately.
typedef struct
{
    short	type;		// 3000 anchor / 3001 start spot / 3002 crushing start spot
    short	id;		// polyobj id (the thing's angle field)
    short	x, y;		// map coords
} po_spot_t;

typedef struct polyobj_s
{
    int		id;
    int		numlines;
    line_t**	lines;

    // The renderer draws SEGS, not linedefs, so the polyobj has to carry its own.
    // Not simply one per line: the node builder is free to split a polyobj line,
    // and each fragment is a seg of its own that must move with the object.
    int		numsegs;
    seg_t**	segs;

    // Which BSP leaf draws this polyobj (r_bsp.c).  Polyobjs are not in the node
    // tree -- they are hung off the subsector their centre lands in at load time.
    subsector_t* subsector;

    // Deduplicated vertex list.  Vertices are SHARED between a polyobj's linedefs,
    // so every transform must visit each one exactly once -- doing it per-line
    // would move shared corners twice and shear the object apart.
    int		numverts;
    vertex_t**	verts;
    fixed_t*	origX;		// vertex positions relative to the start spot, for rotation
    fixed_t*	origY;
    fixed_t*	prevX;		// last position, to roll back a blocked move
    fixed_t*	prevY;

    fixed_t	startX, startY;	// current centre
    angle_t	angle;		// accumulated rotation
    boolean	crush;
    int		bbox[4];	// blockmap CELL range currently linked into
    boolean	linked;
    void*	specialdata;	// the thinker driving it, if any
} polyobj_t;

// Blockmap linkage: a per-cell list of the polyobjs overlapping that cell.
typedef struct polyblock_s
{
    polyobj_t*		polyobj;
    struct polyblock_s*	next;
} polyblock_t;

extern polyobj_t*	polyobjs;
extern int		po_NumPolyobjs;
extern polyblock_t**	PolyBlockMap;

// Place every polyobj: match each start spot to its anchor and translate the
// tagged linedefs into position.  Call from P_SetupLevel after THINGS.
void PO_Init (const po_spot_t* spots, int nspots);

// Look one up by its id, or NULL.
polyobj_t* PO_GetPolyobj (int id);

// Move / rotate.  Both keep the blockmap linkage and the linedefs' cached geometry
// in step.  Return false only if the polyobj id is unknown.
boolean PO_MovePolyobj   (int id, fixed_t dx, fixed_t dy);
boolean PO_RotatePolyobj (int id, angle_t delta);

// Thinkers.
void T_RotatePoly (thinker_t* thinker);
void T_MovePoly   (thinker_t* thinker);
void T_PolyDoor   (thinker_t* thinker);

// Hexen's polyobj line specials, driven from ACS (wired in step 5).
typedef enum { PODOOR_NONE, PODOOR_SLIDE, PODOOR_SWING } podoortype_t;

boolean EV_RotatePoly   (byte* args, int direction, boolean override);
boolean EV_MovePoly     (byte* args, boolean timesEight, boolean override);
boolean EV_OpenPolyDoor (byte* args, podoortype_t type);

#endif
