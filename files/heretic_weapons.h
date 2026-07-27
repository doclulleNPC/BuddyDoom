// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(H) Heretic player WEAPONS in the DOOM engine -- Phase 1: Staff + Gold Wand.
//
//	Same additive mechanism as files/heretic_items.c: the weapon psprite states,
//	the puff mobjinfo and the firing code pointers are appended to the engine
//	tables at runtime (Heretic_Weapons_Init, called from D_DoomMain in
//	heretic_mode), and weaponinfo[wp_fist]/weaponinfo[wp_pistol] are overwritten
//	so the player's slot-1/slot-2 weapons become the Heretic Staff and Gold Wand.
//	Ported 1:1 from crispy-doom src/heretic (p_pspr.c / info.c) and adapted to the
//	DOOM engine's 2-arg psprite action signature + DOOM ammo types.
//
//-----------------------------------------------------------------------------

#ifndef __HERETIC_WEAPONS__
#define __HERETIC_WEAPONS__

// Append states/puff-mobjs and overwrite weaponinfo[] for the Heretic weapon
// set.  Idempotent; call once at startup when heretic_mode is on.
void Heretic_Weapons_Init (void);

#endif
