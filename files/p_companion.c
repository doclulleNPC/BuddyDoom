// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.  (fork additions, DOOM Source License)
//
// DESCRIPTION:
//	Shared co-op-buddy "special" companion AI -- see p_companion.h.  Factored out
//	of p_secdrone.c so the Security Drone (DOOM) and the Lichling (Heretic) share
//	one targeting/roaming/deploy implementation.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "r_main.h"		// R_PointToAngle2
#include "tables.h"		// finesine / finecosine
#include "s_sound.h"
#include "sounds.h"
#include "info.h"
#include "p_companion.h"

// Stock monster AI reused for movement/aim (p_enemy.c).
void A_FaceTarget (mobj_t* actor);
void A_Chase (mobj_t* actor);
boolean P_Move (mobj_t* actor);
void P_NewChaseDir (mobj_t* actor);

boolean Companion_AttackingHuman (mobj_t* m)
{
    return m->target != NULL && m->target->player != NULL;
}

boolean Companion_IsEnemy (mobj_t* m)
{
    if (m->player)			return false;	// human OR buddy
    if (!(m->flags & MF_COUNTKILL) && m->info->seestate == S_NULL)
					return false;	// inert decor / barrel
    if (m->flags & MF_FRIEND)		return false;	// our allies
    if (!(m->flags & MF_SHOOTABLE))	return false;
    if (m->flags & MF_CORPSE)		return false;
    if (m->health <= 0)			return false;
    return true;
}

mobj_t* Companion_BestTarget (mobj_t* self, fixed_t range)
{
    thinker_t*	th;
    mobj_t*	bestPrim = NULL;  fixed_t bestPrimD = 0;	// attacking a human
    mobj_t*	bestSec  = NULL;  fixed_t bestSecD  = 0;	// any enemy

    for (th = thinkercap.next ; th != &thinkercap ; th = th->next)
    {
	mobj_t*	m;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)	continue;
	m = (mobj_t*)th;
	if (m == self)			continue;
	if (!Companion_IsEnemy (m))	continue;
	d = P_AproxDistance (m->x - self->x, m->y - self->y);
	if (d > range)			continue;
	if (!P_CheckSight (self, m))	continue;
	if (Companion_AttackingHuman (m)) {
	    if (!bestPrim || d < bestPrimD) { bestPrim = m; bestPrimD = d; }
	} else {
	    if (!bestSec  || d < bestSecD)  { bestSec  = m; bestSecD  = d; }
	}
    }
    return bestPrim ? bestPrim : bestSec;
}

boolean Companion_ClearShot (mobj_t* self, mobj_t* t)
{
    // P_AimLineAttack traces the real firing line at the target's bearing: it yields a
    // linetarget ONLY when a shootable thing is reached with no wall in the way.  If the
    // inside corner of an L-bend sits between us, the trace stops at the wall and
    // linetarget stays NULL -> hold fire, keep advancing until the shot can connect.
    angle_t	an = R_PointToAngle2 (self->x, self->y, t->x, t->y);
    P_AimLineAttack (self, an, COMPANION_FIRE_RANGE);
    return linetarget != NULL;
}

mobj_t* Companion_NearestHuman (mobj_t* self)
{
    int		i;
    mobj_t*	best = NULL;
    fixed_t	bestd = 0;

    for (i = 0; i < MAXPLAYERS; i++)
    {
	mobj_t*	pm;
	fixed_t	d;
	if (!playeringame[i])		continue;
	pm = players[i].mo;
	if (!pm || pm->health <= 0)	continue;
	d = P_AproxDistance (pm->x - self->x, pm->y - self->y);
	if (!best || d < bestd) { best = pm; bestd = d; }
    }
    return best;
}

void Companion_Roam (mobj_t* self, fixed_t avoid)
{
    mobj_t*	h = Companion_NearestHuman (self);
    fixed_t	hd;

    self->target = NULL;			// idle -> no combat target

    if (!h)					// nobody to escort: random wander
    {
	if (--self->movecount < 0 || !P_Move (self))
	{
	    self->movedir   = P_Random () & 7;
	    self->movecount = 8 + (P_Random () & 15);
	}
	return;
    }

    hd = P_AproxDistance (h->x - self->x, h->y - self->y);
    if (hd < avoid)
    {
	// Too close -> flee straight away so we stop blocking the human.
	angle_t	away = R_PointToAngle2 (h->x, h->y, self->x, self->y);
	self->movedir   = ((unsigned)(away + ANG45/2) / ANG45) & 7;
	self->movecount = 4;
	P_Move (self);
	return;
    }

    // Otherwise close toward the human (where the fight is).  Borrow it as a pathing
    // target for P_NewChaseDir only, then drop it so idle never holds a combat target.
    self->target = h;
    if (--self->movecount < 0 || !P_Move (self))
	P_NewChaseDir (self);
    self->target = NULL;
}

int Companion_CountThreats (mobj_t* origin, fixed_t range)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next ; th != &thinkercap ; th = th->next)
    {
	mobj_t*	m;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)	continue;
	m = (mobj_t*)th;
	if (!Companion_IsEnemy (m))	continue;
	if (!Companion_AttackingHuman (m))	continue;
	if (P_AproxDistance (m->x - origin->x, m->y - origin->y) > range)	continue;
	if (!P_CheckSight (origin, m))	continue;
	n++;
    }
    return n;
}

int Companion_CountActive (int type)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next ; th != &thinkercap ; th = th->next)
    {
	mobj_t*	m;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)	continue;
	m = (mobj_t*)th;
	if (m->type == type && m->health > 0)
	    n++;
    }
    return n;
}

// Aggressive idle: sweep all-around for the nearest enemy and engage immediately;
// else roam toward the fight.
void A_CompanionLook (mobj_t* self)
{
    mobj_t*	e = Companion_BestTarget (self, COMPANION_AGGRO_RANGE);
    if (e)
    {
	self->target = e;
	if (self->info->seesound)
	    S_StartSound (self, self->info->seesound);
	P_SetMobjState (self, self->info->seestate);
	return;
    }
    Companion_Roam (self, COMPANION_AVOID);
}

// Generic engage: keep a live target (re-acquire the best when the current one dies),
// fire when it's in sight and range, else close the distance with the stock chase.
void A_CompanionChase (mobj_t* self)
{
    mobj_t*	t = self->target;
    fixed_t	dist;

    if (self->flags & MF_SKULLFLY)			// stray charge state -> cancel
    {
	self->flags &= ~MF_SKULLFLY;
	self->momx = self->momy = self->momz = 0;
    }

    if (!t || t->health <= 0 || !(t->flags & MF_SHOOTABLE) || (t->flags & MF_CORPSE))
    {
	t = Companion_BestTarget (self, COMPANION_AGGRO_RANGE);
	self->target = t;
	if (!t)
	{
	    P_SetMobjState (self, self->info->spawnstate);
	    return;
	}
    }

    A_FaceTarget (self);
    dist = P_AproxDistance (t->x - self->x, t->y - self->y);
    if (dist <= COMPANION_FIRE_RANGE && P_CheckSight (self, t)
	&& Companion_ClearShot (self, t))	// wall (L-corner) in the line of fire -> close in first
    {
	P_SetMobjState (self, self->info->missilestate);
	return;
    }
    A_Chase (self);
}

mobj_t* P_AICoop_SpawnCompanion (player_t* bot, int type)
{
    mobj_t*	b;
    mobj_t*	c;
    angle_t	ang;
    unsigned	fine;
    fixed_t	x, y, z;

    if (!bot || !bot->mo)
	return NULL;
    b = bot->mo;

    ang  = b->angle;
    fine = ang >> ANGLETOFINESHIFT;
    x = b->x + FixedMul (b->radius + 32*FRACUNIT, finecosine[fine]);
    y = b->y + FixedMul (b->radius + 32*FRACUNIT, finesine[fine]);
    z = b->z + 48*FRACUNIT;

    c = P_SpawnMobj (x, y, z, type);
    if (!c)
	return NULL;
    c->angle  = ang;
    c->flags |= MF_FRIEND;			// hunt monsters, spare the player/buddy
    return c;
}
