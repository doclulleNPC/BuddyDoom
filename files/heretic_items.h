// (H) Map-placeable Heretic item / pickup actors -- see files/heretic_items.c.
//
// The real Heretic keys, ammo, ground weapons, shields and health vial, ported
// from crispy-doom's heretic/info.c so they APPEAR on Heretic maps with the
// correct sprite at the correct doomednum.  PICKUP EFFECTS ARE OUT OF SCOPE
// (this engine has no Heretic weapon/ammo/key subsystem) -- touch is handled
// minimally (health/armor map to the DOOM equivalent; the rest is a cosmetic
// pocket).  Additive: enum slots at the end of info.h, tables filled at runtime.
#ifndef __HERETIC_ITEMS_H__
#define __HERETIC_ITEMS_H__

#include "d_player.h"		// player_t
#include "p_mobj.h"		// mobj_t

// Fill the appended Heretic item states/mobjinfo (call once at startup, next to
// HereticInv_Init in d_main.c).
void		Heretic_Items_Init (void);

// A placed/spawned MT_H* item was touched: apply the minimal effect and pocket
// it.  Returns true if handled (so P_TouchSpecialThing removes it + plays the
// pickup sound and stops), false if `special` is not one of ours.
boolean		P_TouchHereticItem (player_t* player, mobj_t* special);

#endif
