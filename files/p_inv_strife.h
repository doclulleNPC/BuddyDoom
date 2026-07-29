// (S) Strife inventory-item use-effects -- see files/p_inv_strife.c.
//
// The nine Strife inventory items (s_arti_medpatch .. s_arti_coin, defined in
// d_player.h's artitype_t) share the generic player_t.inventory[] list.  Their
// on-floor pickup actors live in files/strife_items.c; their USE effects live in
// p_inv_strife.c and are dispatched from p_invent.c's P_UseArtifact.
#ifndef __P_INV_STRIFE_H__
#define __P_INV_STRIFE_H__

#include "d_player.h"		// player_t, artitype_t

// Apply the effect of Strife inventory item `a` (a in s_arti_medpatch ..
// s_arti_coin).  Returns true if the item was consumed (and sets player->message);
// false if it refused (e.g. full health, or coin which is not usable).  Called
// from p_invent.c's dispatcher, next to ApplyHereticArtifact.
boolean		ApplyStrifeArtifact (player_t* player, artitype_t a);

#endif
