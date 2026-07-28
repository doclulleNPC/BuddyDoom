// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Shared AI for the co-op buddy's "special" companions -- a friendly floating
//	pet that escorts the humans (player + buddy) and attacks nearby monsters.
//	Each companion ACTOR shares this targeting / roaming / deploy behaviour and
//	supplies only its own attack + state machine:
//	  * Security Drone (DOOM)   -- files/p_secdrone.c
//	  * Lichling       (Heretic)-- files/heretic_lichling.c
//	C has no inheritance, so the common behaviour lives here and each actor file
//	calls in (state code-pointers A_Companion*, plus the helpers below).
//
//-----------------------------------------------------------------------------
#ifndef __P_COMPANION__
#define __P_COMPANION__

#include "doomtype.h"
#include "p_mobj.h"
#include "d_player.h"

// Shared tunables (were the DRONE_* set; a companion can still pass its own range).
#define COMPANION_AGGRO_RANGE	(1600*FRACUNIT)	// acquire ANY enemy within this (all around)
#define COMPANION_FIRE_RANGE	(1400*FRACUNIT)	// enter missilestate when a target is in sight within this
#define COMPANION_AVOID		(200*FRACUNIT)	// idle: back off if a human is closer than this
#define COMPANION_ENEMY_RANGE	(1024*FRACUNIT)	// "surrounded" detection radius

// A monster is "attacking a human" when its target is a player mobj (player OR buddy).
boolean	Companion_AttackingHuman (mobj_t* m);

// True for a live, shootable, non-friendly MONSTER worth engaging: skips players
// (human + buddy), our allies (MF_FRIEND), corpses, dead things, and inert decor /
// barrels (no MF_COUNTKILL and no see-state).  The one shared enemy filter used by
// every companion/buddy target scan (callers still add self/range/sight/blacklist).
boolean	Companion_IsEnemy (mobj_t* m);

// Nearest live, shootable, visible non-friendly monster within `range` (all around).
// Priority: whoever is shooting a human; else the nearest enemy.
mobj_t*	Companion_BestTarget (mobj_t* self, fixed_t range);

// Nearest live human (player or buddy) to escort toward.
mobj_t*	Companion_NearestHuman (mobj_t* self);

// Idle behaviour: patrol toward the nearest human (where the fight is), but keep
// `avoid` distance so it never crowds them; wander if there is no human.
void	Companion_Roam (mobj_t* self, fixed_t avoid);

// Count monsters within `range` of `origin` actually ENGAGING a human (target is a
// player + line of sight) -- i.e. real threats, not sleeping/infighting furniture.
int	Companion_CountThreats (mobj_t* origin, fixed_t range);

// Count live friendly mobjs of a given type (deploy cap).
int	Companion_CountActive (int type);

// Generic state code-pointers for a companion that doesn't need a bespoke chase
// (the drone keeps its own charge-augmented chase; the Lichling uses these).
void	A_CompanionLook (mobj_t* self);
void	A_CompanionChase (mobj_t* self);

// Spawn a friendly companion of `type` just in front of and above the buddy, flag it
// MF_FRIEND, and return it (NULL on failure).  The DEPLOY DECISION (when/cost/cap)
// stays in each special's own MaybeSpawn*.
mobj_t*	P_AICoop_SpawnCompanion (player_t* bot, int type);

#endif	// __P_COMPANION__
