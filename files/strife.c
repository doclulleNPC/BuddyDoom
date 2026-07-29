// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.  (fork additions, DOOM Source License)
// Strife content ported from strife-ve / crispy-doom (GPL) as a reference.
//
// DESCRIPTION:
//	Additive Strife game support (BuddyDoom fork).  See strife.h.
//
//	This is the CORE of the Strife port: the doomednum->mobjtype map
//	(P_StrifeThingType), the native-sprite remap, and the availability
//	probe.  The actual actor tables are filled by the per-category
//	installers -- strife_deco.c (scenery), strife_items.c (pickups),
//	strife_mon.c (monsters + abilities) -- exactly like the Heretic split.
//
//	Phase 1 (this file): scaffolding only -- strife_mode wiring compiles and
//	DOOM/Heretic are untouched.  The tables are populated in later phases.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "info.h"
#include "r_state.h"		// sprites[] -- availability probe
#include "strife.h"

extern char**		sprnames;
extern state_t*		states;
extern mobjinfo_t*	mobjinfo;

//
// Strife_Init
// Fill the reserved MT_S*/S_S* slots for the Strife core.  Phase 1: no-op; the
// per-category installers do the work once the enum ranges + reference tables land.
//
void Strife_Init (void)
{
    if (!strife_mode)
	return;
}

//
// Strife_RemapNativeSprites
// Point the placeholder S* sprite codes at strife1.wad's native 4-char codes so the
// software renderer builds sprites[] from the real Strife art.  Must run before R_Init.
// Phase 1: no-op (no SPR_S* range yet).
//
void Strife_RemapNativeSprites (void)
{
    if (!strife_mode)
	return;
}

//
// Strife_Available
// True once strife1.wad's sprites are loaded (the player sprite has frames).
//
int Strife_Available (void)
{
    return sprites[SPR_S_PLAY].numframes > 0;
}

//
// P_StrifeThingType
// Map a Strife map-thing doomednum to a BuddyDoom mobjtype, or -1 to skip it.
// The per-category installers (strife_deco/items/mon/...) set mobjinfo[MT_S_*].doomednum
// to the real Strife editor numbers, so we just scan the reserved Strife mobjtype range
// for a match -- no central switch to keep in sync (mirrors the DOOM lookup, scoped to
// MT_S_FIELDGUARD..NUMMOBJTYPES).  doomednum 0 never matches an (unfilled) slot.
//
int P_StrifeThingType (int doomednum)
{
    int	i;

    if (doomednum <= 0)
	return -1;
    for (i = MT_S_FIELDGUARD; i < NUMMOBJTYPES; i++)
	if (mobjinfo[i].doomednum == doomednum)
	    return i;
    return -1;
}
