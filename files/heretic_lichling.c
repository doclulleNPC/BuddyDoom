// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.  (fork additions, DOOM Source License)
//
// DESCRIPTION:
//	Lichling -- the Heretic co-op-buddy special: a little floating Lich the buddy
//	summons (the Heretic counterpart of the DOOM Security Drone).  Its targeting /
//	roaming / chase come from the shared companion AI (A_CompanionLook /
//	A_CompanionChase, files/p_companion.c); this file supplies only its attack.
//
//	Attack (A_LichlingAttack): melee about as strong as a Pinky demon; else a
//	ranged bolt -- ICE for a single target, FIRE (small AOE) when several enemies
//	are clustered around the target, with a small random flip.  A toned-down take
//	on the Iron Lich, NOT its full ice-ball + homing-whirlwind set.  Sprites are
//	the scaled true-colour PNGs in lichling.wad (LICS body, LICF fire, LICE ice).
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "s_sound.h"
#include "sounds.h"
#include "info.h"

void A_FaceTarget (mobj_t* actor);		// p_enemy.c

// A fire bolt does AOE, so prefer it when this many+ enemies are packed near the
// target; ICE otherwise (single-target).
#define LICHLING_CLUSTER	2
#define LICHLING_AOE_RANGE	(160*FRACUNIT)	// "packed together" radius around the target
#define LICHLING_FIRE_BLAST	40		// P_RadiusAttack damage/radius (small AOE)

// Count live, shootable, non-friendly monsters within `range` of `origin`.
static int Lichling_EnemiesNear (mobj_t* origin, fixed_t range)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next ; th != &thinkercap ; th = th->next)
    {
	mobj_t*	m;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)	continue;
	m = (mobj_t*)th;
	if (m->player)			continue;
	if (m->flags & (MF_FRIEND | MF_CORPSE))	continue;
	if (!(m->flags & MF_SHOOTABLE))	continue;
	if (m->health <= 0)		continue;
	if (P_AproxDistance (m->x - origin->x, m->y - origin->y) <= range)
	    n++;
    }
    return n;
}

//
// A_LichlingAttack -- melee (Pinky-strength) in range, else ice/fire bolt.
//
void A_LichlingAttack (mobj_t* self)
{
    mobj_t*	t = self->target;
    boolean	fire;

    if (!t)
	return;
    A_FaceTarget (self);

    if (P_CheckMeleeRange (self))		// ~ Pinky bite: (rnd%10+1)*4 = 4..40
    {
	int damage = ((P_Random () % 10) + 1) * 4;
	if (self->info->attacksound)
	    S_StartSound (self, self->info->attacksound);
	P_DamageMobj (t, self, self, damage);
	return;
    }

    // Ranged: several enemies clustered near the target -> AOE fire; else ice on the
    // single target.  A small random flip keeps it from being fully deterministic.
    fire = Lichling_EnemiesNear (t, LICHLING_AOE_RANGE) >= LICHLING_CLUSTER;
    if (P_Random () < 64)
	fire = !fire;
    P_SpawnMissile (self, t, fire ? MT_LICHFIRE : MT_LICHICE);
}

//
// A_LichFireImpact -- the fire bolt's small area blast on impact (its deathstate).
// A friendly source, so P_DamageMobj's MF_FRIEND guard spares the humans/allies.
//
void A_LichFireImpact (mobj_t* self)
{
    P_RadiusAttack (self, self->target, LICHLING_FIRE_BLAST);
}
