// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.  (fork additions, DOOM Source License)
//
// DESCRIPTION:
//	Security Drone (BuddyDoom fork) -- a DOOM-side buddy special.  The generic
//	companion AI (targeting / roaming / deploy-placement) now lives in shared
//	p_companion.c; this file keeps only the DRONE-SPECIFIC pieces: the laser
//	volley (A_SecDroneShot), the lost-soul ram (A_SecDroneCharge/Tick), the
//	charge-augmented chase (A_SecDroneChase), and the deploy DECISION
//	(P_AICoop_MaybeSpawnDrone).  Idle scanning uses shared A_CompanionLook.
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
#include "p_secdrone.h"
#include "p_companion.h"	// shared companion AI + deploy helper

void A_FaceTarget (mobj_t* actor);
void A_Chase (mobj_t* actor);

// --- drone-specific tunables (shared ranges live in p_companion.h) ----------
#define DRONE_CHARGE_RANGE	(640*FRACUNIT)	// ram when the target is within this...
#define DRONE_CHARGE_MIN	(128*FRACUNIT)	// ...but not point-blank (just laser instead)
#define DRONE_CHARGE_SPEED	(24*FRACUNIT)	// ram velocity (lost soul is 20) -- very aggressive
#define DRONE_CHARGE_CD		(35*4)		// ~4 s cooldown between charges
#define DRONE_CHARGE_FLYTICS	30		// give up a missed charge after ~0.85 s
#define DRONE_ENEMY_COUNT	5		// how many ENGAGED enemies counts as "many"
#define DRONE_PAIN_THRESH	25		// damagecount at/above this = "under heavy fire"
#define DRONE_COOLDOWN		(35*12)		// ~12 s between buddy deployments
#define DRONE_MAX_ACTIVE	1		// cap on simultaneous friendly drones
#define DRONE_LOW_HP		30		// buddy HP at/below this = last-resort panic
#define DRONE_CLIP_COST		50		// bullets, else...
#define DRONE_SHELL_COST	25		// ...shells

//
// A_SecDroneShot -- fire one laser at the current target (the volley of 3 comes from
// the three fire states looping through this codepointer).
//
void A_SecDroneShot (mobj_t* self)
{
    mobj_t*	mo;
    int		dist;

    if (!self->target)
	return;
    A_FaceTarget (self);

    // A_SecDroneChase only checks ClearShot once, before the whole 3-shot volley
    // (S_SECDR_ATK1..ATK6, ~21 tics).  If the target sidesteps near a corner
    // partway through, later shots would otherwise fire straight into the wall
    // that first check avoided -- re-verify right before each individual shot.
    if (!Companion_ClearShot (self, self->target))
	return;

    mo = P_SpawnMissile (self, self->target, MT_SECDRONESHOT);
    if (!mo)
	return;

    // Drop the origin to the drone's centre and re-aim vertically so the shot
    // visibly leaves this small floating body (P_SpawnMissile fires from +32).
    mo->z = self->z + (self->height >> 1);
    dist = P_AproxDistance (self->target->x - self->x, self->target->y - self->y) / mo->info->speed;
    if (dist < 1)
	dist = 1;
    mo->momz = ((self->target->z + (self->target->height >> 1)) - mo->z) / dist;
}

//
// A_SecDroneCharge -- lost-soul-style ram: fling the drone at its target as an
// MF_SKULLFLY missile (impact damage via PIT_CheckThing; a FRIEND charger passes
// harmlessly through the humans/allies).  movecount doubles as the flight timeout.
//
void A_SecDroneCharge (mobj_t* self)
{
    mobj_t*	dest = self->target;
    angle_t	an;
    int		dist;

    if (!dest || dest->health <= 0)
	return;

    self->flags |= MF_SKULLFLY;
    if (self->info->attacksound)
	S_StartSound (self, self->info->attacksound);

    A_FaceTarget (self);
    an = self->angle >> ANGLETOFINESHIFT;
    self->momx = FixedMul (DRONE_CHARGE_SPEED, finecosine[an]);
    self->momy = FixedMul (DRONE_CHARGE_SPEED, finesine[an]);
    dist = P_AproxDistance (dest->x - self->x, dest->y - self->y) / DRONE_CHARGE_SPEED;
    if (dist < 1)
	dist = 1;
    self->momz = (dest->z + (dest->height>>1) - self->z) / dist;
    self->movecount = DRONE_CHARGE_FLYTICS;
}

//
// A_SecDroneChase -- charge-augmented engage: keep a live target (shared best-target
// priority), then in order: CHARGE (ram, off cooldown, mid-range) > FIRE (in sight,
// in range) > close in.  The per-drone charge cooldown lives in self->lastlook (a
// free, savegame-persisted int the drone AI never uses otherwise).
//
void A_SecDroneChase (mobj_t* self)
{
    mobj_t*	t = self->target;
    fixed_t	dist;

    if (self->flags & MF_SKULLFLY)		// a hit knocked us out of a charge -> cancel
    {
	self->flags &= ~MF_SKULLFLY;
	self->momx = self->momy = self->momz = 0;
    }

    if (self->lastlook > 0)			// tick the charge cooldown
	self->lastlook--;

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

    if (self->lastlook == 0
	&& dist <= DRONE_CHARGE_RANGE && dist >= DRONE_CHARGE_MIN
	&& P_CheckSight (self, t))
    {
	A_SecDroneCharge (self);
	self->lastlook = DRONE_CHARGE_CD;
	P_SetMobjState (self, S_SECDR_CHG1);
	return;
    }

    if (dist <= COMPANION_FIRE_RANGE && P_CheckSight (self, t)
	&& Companion_ClearShot (self, t))	// wall (L-corner) in the line of fire -> close in first
    {
	P_SetMobjState (self, self->info->missilestate);	// attack now
	return;
    }
    A_Chase (self);						// else close the distance
}

//
// A_SecDroneChargeTick -- one frame of the ram flight.  A real hit is handled by the
// engine (clears MF_SKULLFLY, resets to spawnstate); if we're still flagged, we're
// mid-flight.  Bail out (resume chasing) when the flight times out or the target dies.
//
void A_SecDroneChargeTick (mobj_t* self)
{
    if (!(self->flags & MF_SKULLFLY))
    {
	P_SetMobjState (self, self->info->seestate);
	return;
    }
    if (--self->movecount <= 0 || !self->target || self->target->health <= 0)
    {
	self->flags &= ~MF_SKULLFLY;
	self->momx = self->momy = self->momz = 0;
	P_SetMobjState (self, self->info->seestate);		// resume chasing
    }
}

//
// P_AICoop_MaybeSpawnDrone
// Deploy DECISION (the drone is an emergency asset): only when the buddy is under
// heavy fire, genuinely surrounded, an ammo pool is overflowing, or -- last resort --
// critically hurt AND under attack (which may bypass the cooldown/cap).  Placement +
// the actual spawn come from the shared P_AICoop_SpawnCompanion.
//
void P_AICoop_MaybeSpawnDrone (player_t* bot)
{
    static int	cooldown = 0;
    mobj_t*	d;
    boolean	useClip, useShell;
    boolean	heavyFire, surrounded, lowHP;
    boolean	clipCapped, shellCapped, atCap;
    int		threats;

    if (!bot || !bot->mo || bot->playerstate != PST_LIVE)
	return;

    // Cheap gates before the per-tic threat scan; only a critically hurt buddy may
    // redeploy mid-cooldown and needs the scan.
    if (bot->health > DRONE_LOW_HP)
    {
	if (cooldown > 0)  { cooldown--; return; }
	if (gametic & 7)   return;			// ~4-5 Hz deploy evaluation
    }

    threats = Companion_CountThreats (bot->mo, COMPANION_ENEMY_RANGE);
    lowHP   = bot->health > 0 && bot->health <= DRONE_LOW_HP && threats >= 1;

    if (cooldown > 0 && !lowHP) { cooldown--; return; }

    useClip  = bot->ammo[am_clip]  >= DRONE_CLIP_COST;
    useShell = bot->ammo[am_shell] >= DRONE_SHELL_COST;
    if (!heretic_mode && !useClip && !useShell)
	return;					// the DOOM drone burns ammo; the Heretic lichling is free

    heavyFire   = bot->damagecount >= DRONE_PAIN_THRESH;
    surrounded  = threats >= DRONE_ENEMY_COUNT;
    clipCapped  = useClip  && bot->maxammo[am_clip]  > 0 && bot->ammo[am_clip]  >= bot->maxammo[am_clip];
    shellCapped = useShell && bot->maxammo[am_shell] > 0 && bot->ammo[am_shell] >= bot->maxammo[am_shell];
    atCap       = clipCapped || shellCapped;

    if (!heavyFire && !surrounded && !atCap && !lowHP)
	return;					// safe and not overflowing -> hold

    if (atCap && !heavyFire && !surrounded && !lowHP)
	useClip = clipCapped;			// burning overflow -> spend the capped pool

    // One companion special per game family -- same deploy decision, different actor and
    // message.  Heretic gets the Lichling, Strife the Stalker; both are free, only the
    // DOOM tech drone costs ammo.
    {
	int ctype = heretic_mode ? MT_LICHLING
		  : (gametype == GT_STRIFE) ? MT_STALKERBUDDY : MT_SECDRONE;

	if (Companion_CountActive (ctype) >= DRONE_MAX_ACTIVE)
	    return;				// at most one companion out

	d = P_AICoop_SpawnCompanion (bot, ctype);
	if (!d)
	    return;

	if (!heretic_mode)			// only the DOOM drone spends ammo
	{
	    if (useClip) bot->ammo[am_clip]  -= DRONE_CLIP_COST;
	    else         bot->ammo[am_shell] -= DRONE_SHELL_COST;
	}
	cooldown = DRONE_COOLDOWN;
	players[consoleplayer].message = heretic_mode ? "[Buddy] Summoning a Lichling!"
	    : (gametype == GT_STRIFE) ? "[Buddy] Releasing a Stalker!"
	    : "[Buddy] Deploying security drone!";
    }
}
