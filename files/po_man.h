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
#include "r_defs.h"

#define POLY_MAXOBJS	64	// polyobjects per map
#define POLY_MAXLINES	64	// linedefs per polyobject

// One polyobj map thing, as seen by the THINGS pass.  Hexen puts the polyobj id in
// the thing's ANGLE field rather than a facing, so it is carried separately.
typedef struct
{
    short	type;		// 3000 anchor / 3001 start spot / 3002 crushing start spot
    short	id;		// polyobj id (the thing's angle field)
    short	x, y;		// map coords
} po_spot_t;

typedef struct
{
    int		id;
    int		numlines;
    line_t**	lines;
    fixed_t	startX, startY;	// where it was placed
    boolean	crush;
} polyobj_t;

extern polyobj_t*	polyobjs;
extern int		po_NumPolyobjs;

// Place every polyobj: match each start spot to its anchor and translate the
// tagged linedefs into position.  Call from P_SetupLevel after THINGS.
void PO_Init (const po_spot_t* spots, int nspots);

#endif
