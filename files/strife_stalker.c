// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.  (fork additions, DOOM Source License)
//
// DESCRIPTION:
//	Stalker -- the Strife co-op-buddy special: the Strife counterpart of the DOOM
//	Security Drone and the Heretic Lichling.  Its targeting / roaming / chase come
//	from the shared companion AI (A_CompanionLook / A_CompanionChase,
//	files/p_companion.c); this file supplies only its attack.
//
//	THE STALKER SHOOTS.  Rogue drew a full 8-rotation firing pose for it -- sprite
//	frames M and N of the SPID sheet -- and then never used them: vanilla Strife's
//	MT_STALKER has meleestate S_S_SPID_09 and missilestate S_NULL, so it can only
//	claw.  Those two frames are the only ones of the 27 the original game leaves
//	unreferenced, which is what makes them unmistakably the unfinished attack.
//	A_StalkerBuddyAttack finally fires them: a chaingunner-strength hitscan burst
//	(3..15 per bullet, the ((rnd%5)+1)*3 of A_CPosAttack in p_enemy.c), with the
//	original claw kept for melee range.
//
//	Art: the frames ship as STLK* PNGs in buddydoom.wad rather than under their
//	native SPID* names, because SPID is Doom's Spider Mastermind -- see
//	docs/BUDDY_SPRITE_COLLISIONS.md.  Sounds likewise (STLK*).  The gunshot is the
//	one asset with no original -- a Stalker that never fired has no firing sound --
//	so it is Freedoom's pistol (BSD-licensed), not a borrowed Strife lump.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "s_sound.h"
#include "sounds.h"
#include "info.h"
#include "p_companion.h"

void A_FaceTarget (mobj_t* actor);		// p_enemy.c

// Strife's own stalker claw: 2*(rnd%8)+2 = 2..16 (A_StalkerScratch, strife_mon.c).
#define STLK_CLAW_DAMAGE	(2 * (P_Random () % 8) + 2)

// Bullet spread.  A_CPosAttack uses <<20 (about +/-2.8 deg); the stalker is a stable
// four-legged mount rather than a jogging trooper, so it shoots a little tighter.
#define STLK_SPREAD_SHIFT	19

//
// A_StalkerBuddyAttack -- claw in melee range, else one chaingunner-strength bullet.
// Called twice per burst from the ATK states (see info.c S_STLKB_ATK*).
//
void A_StalkerBuddyAttack (mobj_t* self)
{
    mobj_t*	t = self->target;
    int		bangle, angle, slope, damage;

    if (!t)
	return;
    A_FaceTarget (self);

    if (P_CheckMeleeRange (self))
    {
	if (self->info->painsound)		// STLKATK doubles as the claw sound
	    S_StartSound (self, self->info->painsound);
	P_DamageMobj (t, self, self, STLK_CLAW_DAMAGE);
	return;
    }

    // Hold fire until the line of FIRE is clear, not just the line of sight: a friendly
    // hitscan that clips the corner we are standing behind would spray the room (and,
    // with friendly fire on, the human).  Companion_ClearShot traces the real shot.
    if (!Companion_ClearShot (self, t))
	return;

    S_StartSound (self, sfx_stlk_fire);

    bangle = self->angle;
    slope  = P_AimLineAttack (self, bangle, MISSILERANGE);
    angle  = bangle + ((P_Random () - P_Random ()) << STLK_SPREAD_SHIFT);
    damage = ((P_Random () % 5) + 1) * 3;	// 3..15, exactly the chaingunner's
    P_LineAttack (self, angle, MISSILERANGE, slope, damage);
}

//
// A_StalkerBuddyRefire -- mid-burst check (A_CPosRefire's shape): break off and go back
// to the chase when the target is gone, dead or out of sight, so the burst can't keep
// hammering a corpse or an empty corridor.
//
void A_StalkerBuddyRefire (mobj_t* self)
{
    A_FaceTarget (self);

    if (P_Random () < 40)			// mostly keep firing
	return;

    if (!self->target || self->target->health <= 0 || !P_CheckSight (self, self->target))
	P_SetMobjState (self, self->info->seestate);
}
