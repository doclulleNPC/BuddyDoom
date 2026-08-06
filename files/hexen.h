// Additive Hexen content for BuddyDoom (approach A in HERETIC_HEXEN.md, same as heretic.c):
// Hexen monsters ported from crispy-doom's hexen/info.c + p_enemy.c, appended to the
// engine's states[]/mobjinfo[] tables at runtime.  Sprites come from hexenstuff.wad (the
// renamed X* sprites built by tools/extract_hexen.py); sounds reuse DOOM SFX for now.
// Activated only when those sprites are present.
#ifndef __HEXEN_H__
#define __HEXEN_H__

#include "m_fixed.h"
struct mobj_s;

// Fill the Hexen slots appended to states[]/mobjinfo[].  Call once at startup
// (after the info tables exist, before any Hexen monster spawns).
void Hexen_Init (void);

// True if hexenstuff.wad's sprites are loaded -- spawn Hexen monsters only then, else
// they'd render as a blank (0-frame) sprite.
int  Hexen_Available (void);

// Map a name ("ettin"/...) to a Hexen mobjtype, or -1 if unknown.
int  Hexen_TypeByName (const char* name);

// Map a poison-feature name ("poisoncloud"/"poisonbag"/"poisonshroom") to its mobjtype,
// gated on the poison art (PSBG/SHRM) rather than the monster sprites.  -1 if unknown/absent.
int  Hexen_PoisonTypeByName (const char* name);

// Additive Hexen wave-2 content: decorations/scenery (files/hexen_deco.c), items/pickups/
// puzzle pieces (files/hexen_items.c), and monster variants + bosses (files/hexen_mon.c).
// Call each once at startup after Hexen_Init (they append to the same tables).  All are
// summon-only (doomednum forced to -1 in D_DoomMain -- no hexen_mode map path yet).
void Hexen_Deco_Init (void);
void Hexen_Mon_Init (void);
int  Hexen_Mon_TypeByName (const char* name);	// "korax"/"heresiarch"/... -> MT_ or -1

// Give every ported Hexen actor its REAL Hexen map-thing number.  Called from
// P_SetupLevel on a Hexen-format map only -- off a Hexen map those numbers would
// shadow DOOM/Heretic things (files/hexen_mon.c).
void Hexen_SetMapEdnums (void);

// Hexen map-thing number -> mobjtype, or -1 if unported.  Searches only the
// additive Hexen block, so DOOM actors can never answer (files/hexen_mon.c).
int  P_HexenThingType (int doomednum);

// Spawn a Hexen monster at (x,y) on the floor; NULL if unavailable or type<0.
struct mobj_s* Hexen_Spawn (int type, fixed_t x, fixed_t y);

#endif
