// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1993-2008 Raven Software (Hexen -- the reference for this port)
//
// DESCRIPTION:
//	(X) Hexen's Thing_* line specials, and the stained-glass shards that are
//	the most visible thing they do.
//
//	BREAKING GLASS.  Hexen has no "breakable glass" line special -- it is done
//	in script.  A window is a linedef whose ACS_Execute runs a script that
//	lowers the glass sector and fires Thing_Projectile a few times with the
//	T_STAINEDGLASS1..0 types, throwing coloured shards out of the frame.  So
//	"implement breaking glass" means implementing the shards AND the specials
//	that launch them; neither is any use alone.
//
//	Those specials are not a niche feature: across hexen.wad's BEHAVIOR lumps
//	Thing_Spawn is called 290 times, Thing_ProjectileGravity 81, Thing_Projectile
//	60 and Thing_Activate 71 -- every one of them a no-op until now.  They are
//	how Hexen spawns ambushes, drops items, throws rocks and starts effects.
//
//	They address things by TID (see mobj_t.tid, read in p_setup.c), never by
//	position, which is why TID support had to come first.
//
//	SIMPLIFICATIONS:
//	  - TranslateThingType maps only the types this engine actually has an
//	    actor for; the rest resolve to -1 and the call is a no-op with
//	    a one-line report, rather than spawning some unrelated DOOM actor.
//	  - Hexen's MF2_FLOORBOUNCE / MF_TRANSLUCENT are not in this engine, so the
//	    shards fall and settle instead of bouncing, and are drawn opaque.
//	  - Thing_Destroy kills rather than gibbing; Thing_Activate/Deactivate cover
//	    the common cases (wake a monster / stop it) rather than Hexen's full
//	    per-type behaviour.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "info.h"
#include "m_fixed.h"
#include "m_random.h"
#include "p_local.h"
#include "p_mobj.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"
#include "hexen_things.h"

extern state_t*		states;
extern mobjinfo_t*	mobjinfo;

extern mobj_t*	P_FindMobjFromTID (int tid, thinker_t** searcher);
extern boolean	P_CheckPosition (mobj_t* thing, fixed_t x, fixed_t y);

// ---------------------------------------------------------------------------
// The stained-glass shards
// ---------------------------------------------------------------------------

static void ST (statenum_t s, spritenum_t spr, int frame, int tics,
		void (*act)(mobj_t*), statenum_t next)
{
    state_t* st = &states[s];
    st->sprite = spr;  st->frame = frame;  st->tics = tics;
    st->action.acp1 = (actionf_p1) act;  st->nextstate = next;
    st->misc1 = st->misc2 = 0;
}

// Every shard is the same actor with different art: a small, weightless,
// non-blocking mote that flies out of the window and settles.
static void SHARD (mobjtype_t mt, statenum_t spawn, statenum_t death)
{
    mobjinfo_t* m = &mobjinfo[mt];

    m->doomednum = -1;			// script-spawned only, never map-placed
    m->spawnstate = spawn;  m->spawnhealth = 1000;
    m->seestate = S_NULL;   m->seesound = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;
    m->painstate = S_NULL;  m->painchance = 0;  m->painsound = sfx_None;
    m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = death;  m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 0;  m->radius = 5*FRACUNIT;  m->height = 16*FRACUNIT;
    m->mass = 5;   m->damage = 0;  m->activesound = sfx_None;
    // MF_MISSILE so it flies and dies on contact rather than sliding along walls;
    // MF_NOBLOCKMAP so a cloud of shards costs nothing and blocks nothing.
    m->flags  = MF_NOBLOCKMAP | MF_DROPOFF | MF_MISSILE | MF_NOGRAVITY;
    m->flags2 = 0;
    m->raisestate = S_NULL;
}

void Hexen_Things_Init (void)
{
    int	i;

    // Shards 1-5: five tumbling SGSA frames each, then a 30-tic settle.
    // Frames run 0..24 across the five shards (SGSA A0..Y0 in hexen.wad).
    for (i = 0; i < 5; i++)
    {
	statenum_t  s0 = (statenum_t) (S_XSGS1_1 + i*6);
	mobjtype_t  mt = (mobjtype_t) (MT_XSGSHARD1 + i);
	int	    f0 = i * 5;

	ST (s0+0, SPR_SGSA, f0+0, 4, NULL, s0+1);
	ST (s0+1, SPR_SGSA, f0+1, 4, NULL, s0+2);
	ST (s0+2, SPR_SGSA, f0+2, 4, NULL, s0+3);
	ST (s0+3, SPR_SGSA, f0+3, 4, NULL, s0+4);
	ST (s0+4, SPR_SGSA, f0+4, 4, NULL, s0+0);	// loop while airborne
	ST (s0+5, SPR_SGSA, f0+4, 30, NULL, S_NULL);	// settled, then gone
	SHARD (mt, s0+0, s0+5);
    }

    // Shards 6-0: single-frame SGSB chips (A0..E0).
    for (i = 0; i < 5; i++)
    {
	statenum_t  s0 = (statenum_t) (S_XSGS6_1 + i*2);
	mobjtype_t  mt = (mobjtype_t) (MT_XSGSHARD6 + i);

	ST (s0+0, SPR_SGSB, i, 4,  NULL, s0+0);		// hold while airborne
	ST (s0+1, SPR_SGSB, i, 30, NULL, S_NULL);	// settled, then gone
	SHARD (mt, s0+0, s0+1);
    }
}

// ---------------------------------------------------------------------------
// Hexen's spawnable-thing table
//
// ACS names actors by a small "spawn id", not by mobjtype -- args[1] of every
// Thing_* special is an index into this table.  The order is Hexen's and must not
// be rearranged.  Entries this engine has no actor for are -1.
// ---------------------------------------------------------------------------

#define X	(-1)			// not ported -- call becomes a no-op

static const short TranslateThingType[] =
{
    X,				//  0 T_NONE
    X, X, X, X,			//  1 centaur, centaurleader, demon, ettin
    X, X, X, X, X,		//  5 firegargoyle, waterlurker(+leader), wraith(+buried)
    X,				// 10 fireball1
    X, X,			// 11 mana1, mana2
    X, X, X, X, X, X,		// 13 boots, egg, flight, summon, tportother, teleport
    X, X,			// 19 bishop, icegolem
    X,				// 21 bridge
    X, X, X, X, X,		// 22 bracers, healthpotion, flask, full, boostmana
    X, X, X, X, X,		// 27 fighter axe/hammer/sword1-3
    X, X, X, X,			// 32 cleric staff, holy1-3
    X, X, X, X,			// 36 mage shards, staff1-3
    X,				// 40 morphblast
    X, X, X,			// 41 rock1-3
    X, X, X, X, X, X,		// 44 dirt1-6
    X, X, X,			// 50 arrow, dart, poisondart
    X,				// 53 ripperball
    // 54 T_STAINEDGLASS1 .. 63 T_STAINEDGLASS0 -- what a breaking window throws.
    MT_XSGSHARD1, MT_XSGSHARD2, MT_XSGSHARD3, MT_XSGSHARD4, MT_XSGSHARD5,
    MT_XSGSHARD6, MT_XSGSHARD7, MT_XSGSHARD8, MT_XSGSHARD9, MT_XSGSHARD0,
};

#undef X

#define NUMTRANSLATED	((int)(sizeof(TranslateThingType)/sizeof(TranslateThingType[0])))

// Resolve args[1] to an actor, or -1.  Reports each unported id once so a
// script that quietly does nothing is still traceable.
static mobjtype_t ThingType (int id)
{
    static byte	reported[256];

    if (id < 0 || id >= NUMTRANSLATED || TranslateThingType[id] < 0)
    {
	if (id >= 0 && id < 256 && !reported[id])
	{
	    reported[id] = 1;
	    printf ("Hexen: spawnable thing id %d is not ported -- Thing_* call ignored\n", id);
	}
	return (mobjtype_t) -1;
    }
    return (mobjtype_t) TranslateThingType[id];
}

// ---------------------------------------------------------------------------
// The specials
// ---------------------------------------------------------------------------

// Thing_Projectile / Thing_ProjectileGravity (134 / 136):
//	args = tid, type, angle, speed, vertical speed
// Launch `type` from every thing tagged `tid`.  This is the one that throws glass.
boolean EV_ThingProjectile (byte* args, boolean gravity)
{
    mobjtype_t	mt = ThingType (args[1]);
    angle_t	angle;
    unsigned	fine;
    fixed_t	speed, vspeed;
    thinker_t*	searcher = NULL;
    mobj_t*	spot;
    boolean	success = false;

    if ((int) mt < 0)
	return false;
    if (nomonsters && (mobjinfo[mt].flags & MF_COUNTKILL))
	return false;

    angle  = (angle_t)((int) args[2] << 24);
    fine   = angle >> ANGLETOFINESHIFT;
    speed  = (int) args[3] << 13;
    vspeed = (int) args[4] << 13;

    while ((spot = P_FindMobjFromTID (args[0], &searcher)) != NULL)
    {
	mobj_t* mo = P_SpawnMobj (spot->x, spot->y, spot->z, mt);
	if (!mo)
	    continue;
	if (mo->info->seesound)
	    S_StartSound (mo, mo->info->seesound);
	mo->target = spot;			// originator
	mo->angle  = angle;
	mo->momx   = FixedMul (speed, finecosine[fine]);
	mo->momy   = FixedMul (speed, finesine[fine]);
	mo->momz   = vspeed;
	if (gravity)
	{
	    mo->flags  &= ~MF_NOGRAVITY;
	    mo->flags2 |= MF2_LOGRAV;
	}
	success = true;
    }
    return success;
}

// Thing_Spawn / Thing_SpawnNoFog (135 / 137): args = tid, type, angle
boolean EV_ThingSpawn (byte* args, boolean fog)
{
    mobjtype_t	mt = ThingType (args[1]);
    angle_t	angle;
    thinker_t*	searcher = NULL;
    mobj_t*	spot;
    boolean	success = false;

    if ((int) mt < 0)
	return false;
    if (nomonsters && (mobjinfo[mt].flags & MF_COUNTKILL))
	return false;

    angle = (angle_t)((int) args[2] << 24);

    while ((spot = P_FindMobjFromTID (args[0], &searcher)) != NULL)
    {
	mobj_t* mo = P_SpawnMobj (spot->x, spot->y, spot->z, mt);
	if (!mo)
	    continue;

	// Refuse to spawn something into a wall or into another actor -- Hexen
	// uses P_TestMobjLocation for this; P_CheckPosition is the equivalent here.
	if (!P_CheckPosition (mo, mo->x, mo->y))
	{
	    P_RemoveMobj (mo);
	    continue;
	}

	mo->angle = angle;
	if (fog)
	{
	    mobj_t* f = P_SpawnMobj (spot->x, spot->y, spot->z + 32*FRACUNIT, MT_TFOG);
	    if (f) S_StartSound (f, sfx_telept);
	}
	success = true;
    }
    return success;
}

// Thing_Activate / Thing_Deactivate (130 / 131): args = tid
//
// Hexen has per-type activation (bridges start spinning, statues animate, ...).
// The general case here is "wake it up" / "put it back to sleep", which covers the
// dormant-ambush use that most maps want it for.
boolean EV_ThingActivate (byte* args, boolean activate)
{
    thinker_t*	searcher = NULL;
    mobj_t*	mo;
    boolean	success = false;

    while ((mo = P_FindMobjFromTID (args[0], &searcher)) != NULL)
    {
	if (activate)
	{
	    if (mo->info->seestate != S_NULL && mo->health > 0)
	    {
		P_SetMobjState (mo, mo->info->seestate);
		success = true;
	    }
	}
	else
	{
	    if (mo->info->spawnstate != S_NULL && mo->health > 0)
	    {
		mo->target = NULL;
		P_SetMobjState (mo, mo->info->spawnstate);
		success = true;
	    }
	}
    }
    return success;
}

// Thing_Remove / Thing_Destroy (132 / 133): args = tid
boolean EV_ThingRemove (byte* args, boolean destroy)
{
    thinker_t*	searcher = NULL;
    mobj_t*	mo;
    boolean	success = false;

    while ((mo = P_FindMobjFromTID (args[0], &searcher)) != NULL)
    {
	if (mo->player)			// never delete a player out from under someone
	    continue;
	if (destroy)
	    P_DamageMobj (mo, NULL, NULL, mo->health + 1000);	// kill it properly
	else
	    P_RemoveMobj (mo);
	success = true;
    }
    return success;
}
