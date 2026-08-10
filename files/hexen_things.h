// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(X) Hexen's Thing_* line specials + the stained-glass shards they throw.
//	See files/hexen_things.c.
//
//-----------------------------------------------------------------------------

#ifndef __HEXEN_THINGS__
#define __HEXEN_THINGS__

#include "doomtype.h"

// Build the shard states + mobjinfo.  Call once at startup, alongside the other
// Hexen_*_Init (d_main.c).
void	Hexen_Things_Init (void);

// The specials, as called from P_ExecuteLineSpecial (p_acs.c).  Each takes the
// linedef's 5 ACS args; args[0] is the TID they act on.
boolean	EV_ThingProjectile (byte* args, boolean gravity);	// 134 / 136
boolean	EV_ThingSpawn      (byte* args, boolean fog);		// 135 / 137
boolean	EV_ThingActivate   (byte* args, boolean activate);	// 130 / 131
boolean	EV_ThingRemove     (byte* args, boolean destroy);	// 132 / 133

#endif
