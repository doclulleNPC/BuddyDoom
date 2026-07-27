// (X) Map-placeable Hexen ITEM / PICKUP + PUZZLE actors -- see files/hexen_items.c.
//
// The real Hexen mana, keys, armor, artifacts, puzzle pieces and class weapon
// pieces, ported from crispy-doom's hexen/info.c so they APPEAR on Hexen maps
// with the correct native sprite at the correct doomednum.  PICKUP EFFECTS ARE
// OUT OF SCOPE (this engine has no Hexen class / weapon / mana / key / puzzle
// subsystem) -- touch is handled minimally (health -> P_GiveBody, armor ->
// P_GiveArmor, everything else a cosmetic pocket).  Additive: enum slots at the
// end of info.h, tables filled at runtime (mirrors files/heretic_items.c).
#ifndef __HEXEN_ITEMS_H__
#define __HEXEN_ITEMS_H__

#include "d_player.h"		// player_t
#include "p_mobj.h"		// mobj_t

// Fill the appended Hexen item states/mobjinfo (call once at startup, next to
// Hexen_Init / Heretic_Items_Init in d_main.c).
void		Hexen_Items_Init (void);

// A placed/spawned MT_Z* item was touched: apply the minimal effect and pocket
// it.  Returns true if handled (so P_TouchSpecialThing removes it + plays the
// pickup sound and stops), false if `special` is not one of ours.
boolean		P_TouchHexenItem (player_t* player, mobj_t* special);

// Resolve a console-summon name ("bluemana", "steelkey", "puzzskull", ...) to a
// MT_Z* type, or -1.  For wiring into Hexen_TypeByName / C_MobjByName.
int		Hexen_ItemTypeByName (const char* name);

#endif
