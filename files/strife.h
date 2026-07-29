// Additive Strife content for BuddyDoom -- ported from strife-ve / crispy-doom's
// strife/info.c + p_enemy.c (and gzdoom zscript for behaviour), appended to the
// engine's states[]/mobjinfo[]/sprnames[] tables in the reserved S_S*/MT_S*/SPR_S*
// ranges (info.h).  Sprites come from strife1.wad's native codes (remapped below);
// sounds are filled by sounds_strife.c.  Activated only in strife_mode.
#ifndef __STRIFE__
#define __STRIFE__

#include "m_fixed.h"
struct mobj_s;

// Fill the Strife monster/core slots appended to states[]/mobjinfo[].  Call once at
// startup (after the info tables exist, before any Strife thing spawns).
void Strife_Init (void);

// (strife_mode) Remap the S* Strife sprite codes to strife1.wad's native 4-char codes
// so the actors render from the real Strife art.  Call BEFORE R_Init builds sprites[].
void Strife_RemapNativeSprites (void);

// True if strife1.wad's sprites are loaded -- spawn Strife things only then.
int  Strife_Available (void);

// Map a real Strife map-thing doomednum (strife/info.c) to the corresponding BuddyDoom
// mobjtype (MT_S*), or -1 for an unported Strife thing (skip it).
int  P_StrifeThingType (int doomednum);

#endif
