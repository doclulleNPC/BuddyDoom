// (S) Map-placeable Strife item / pickup actors -- see files/strife_items.c.
//
// The Strife ammo boxes, ground weapons, armor, health, backpack/satchel, keys,
// money and inventory-item world pickups, ported from strife-ve's info.c so they
// APPEAR on Strife maps at the correct doomednum, and are picked up with the
// effect mapping documented in strife_items.c (ammo/weapons/armor grant the DOOM
// pools/slots strife_weapons.c set up; med/usable/degnin items pocket into the
// s_arti_* inventory; money accumulates s_arti_coin; keys/tokens are message-only
// for now).  Additive: enum slots reserved in strife_mt.inc/strife_states.inc,
// tables filled at runtime.
#ifndef __STRIFE_ITEMS_H__
#define __STRIFE_ITEMS_H__

#include "d_player.h"		// player_t
#include "p_mobj.h"		// mobj_t

// Fill the appended Strife pickup states/mobjinfo (call once at startup, next to
// Strife_Weapons_Init in d_main.c).  Unconditional -- safe with or without the
// Strife IWAD loaded (the actors only spawn on Strife maps).
void		Strife_Items_Init (void);

// A placed/spawned MT_S_* pickup was touched: apply the effect and (if consumed)
// return true so P_TouchSpecialThing removes it + plays the pickup sound; false
// leaves it on the ground (armor full, inventory slot capped, unknown sprite).
boolean		P_TouchStrifeItem (player_t* player, mobj_t* special);

#endif
