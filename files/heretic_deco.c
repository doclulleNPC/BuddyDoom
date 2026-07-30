// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Additive Heretic DECORATION / SCENERY actors in the DOOM engine.
//	Ported from crispy-doom's heretic/info.c (frame tables + mobjinfo) and
//	heretic/p_enemy.c (action funcs), adapted to this engine's 1-arg action
//	signature and MF_/MF2_ flag set.  Same additive approach as heretic.c:
//	Heretic_Deco_Init() appends states + mobjinfo at runtime; the enum slots
//	live at the tail of statenum_t/mobjtype_t/spritenum_t (info.h).  The
//	invented H*-prefixed sprite codes are remapped to native heretic.wad
//	4-char codes by Heretic_RemapNativeSprites() (heretic.c) -- add the pairs
//	listed in deco_snippets.txt to that table.
//
//	Coverage (map-placeable + their sub-actors):
//	  * statics: skull-hangs x4, chandelier, serpent torch, small pillar,
//	    stalagmites/stalactites (large+small), fire brazier, barrel, brown
//	    pillar, moss x2, wall torch, hanging corpse.
//	  * Volcano pod (MT_HPOD) -- shootable/breakable, spits goo, explodes;
//	    its generator (MT_HPODGEN) and goo (MT_HPODGOO).
//	  * Volcano (MT_HVOLCANO) -- erupts blasts (MT_HVOLCANOBLAST) that on
//	    impact spread small tblasts (MT_HVOLCANOTBLAST) -- full ballistic port.
//	  * Teleport-glitter generators + glitter (MT_HTELEGLIT*).
//	  * Key gizmos (blue/green/yellow) + floating orb (MT_HKEYGIZMO*).
//	  * Teleport fog (MT_HTFOG, sub-actor).
//	  * Ambient sound sources (MT_HSOUNDWIND / MT_HSOUNDWATERFALL).
//
//	SIMPLIFICATIONS (no matching engine asset):
//	  - Ambient sound sources are SILENT markers: A_HESound is a no-op because
//	    this engine has no wind/waterfall SFX.  The actors still spawn so maps
//	    referencing DoomEd 41/42 don't error.
//	  - Volcano ball trail puff (crispy A_BeastPuff) dropped -- cosmetic only.
//	  - Substitute SFX: pod explosion -> sfx_barexp, volcano balls -> sfx_firxpl
//	    (native podexp/volhit/volsht/newpod/wind/waterfl lumps are not present).
//	  - MF_TRANSLUCENT and Heretic MF2_* (WINDTHRUST/PUSHABLE/SLIDE/PASSMOBJ/
//	    TELESTOMP/NOTELEPORT/CANNOTPUSH/FIREDAMAGE/FLOATBOB) are dropped -- this
//	    engine doesn't define them.  MF2_LOGRAV IS kept (pod goo, volcano balls).
//	  - Pod generator's per-pod bookkeeping (crispy special1/special2) is mapped
//	    onto the generator's `threshold` (live-pod count) and the pod's `tracer`
//	    (back-link to its generator) -- no struct/savegame change.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "info.h"
#include "m_random.h"
#include "m_fixed.h"
#include "tables.h"		// finecosine/finesine, ANGLETOFINESHIFT, ANG90
#include "sounds.h"
#include "w_wad.h"
#include "p_mobj.h"
#include "r_defs.h"		// subsector_t/sector_t -- teleglitter reads floorheight
#include "heretic.h"

#define ONFLOORZ	MININT		// from p_local.h (avoided to skip its p_spec.h enums)

extern state_t *states;
extern mobjinfo_t *mobjinfo;

// engine helpers (no public header -- declare by hand, same as heretic.c)
extern void	A_Scream (mobj_t*);
extern void	A_Explode (mobj_t*);		// radius blast (P_RadiusAttack 128) -- pod pop
extern void	S_StartSound (void* origin, int sfx_id);
extern mobj_t*	P_SpawnMobj (fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
extern boolean	P_SetMobjState (mobj_t* mobj, statenum_t state);
extern void	P_RemoveMobj (mobj_t* mobj);
extern boolean	P_CheckPosition (mobj_t* thing, fixed_t x, fixed_t y);
extern void	P_CheckMissileSpawn (mobj_t* th);
extern void	P_RadiusAttack (mobj_t* spot, mobj_t* source, int damage);

// ---------------------------------------------------------------------------
// Action functions (crispy heretic/p_enemy.c, adapted to the 1-arg signature).
// ---------------------------------------------------------------------------

// Pod (MT_HPOD): when hit, has a chance to spew 1-2 goo gibs upward.
void A_HPodPain (mobj_t* pod)
{
    int		i, count, chance;
    mobj_t*	goo;

    chance = P_Random ();
    if (chance < 128)
	return;
    count = chance > 240 ? 2 : 1;
    for (i = 0; i < count; i++)
    {
	goo = P_SpawnMobj (pod->x, pod->y, pod->z + 48*FRACUNIT, MT_HPODGOO);
	goo->target = pod;
	goo->momx = (P_Random () - P_Random ()) << 9;		// P_SubRandom()<<9
	goo->momy = (P_Random () - P_Random ()) << 9;
	goo->momz = FRACUNIT/2 + (P_Random () << 9);
    }
}

// Pod death: decrement its generator's live-pod count (tracer = generator).
void A_HRemovePod (mobj_t* pod)
{
    if (pod->tracer && pod->tracer->threshold > 0)
	pod->tracer->threshold--;
}

#define MAX_GEN_PODS	16

// Pod generator: periodically grow a fresh pod, capped at MAX_GEN_PODS live.
void A_HMakePod (mobj_t* gen)
{
    mobj_t*	mo;
    fixed_t	x, y;
    angle_t	an;

    if (gen->threshold >= MAX_GEN_PODS)		// too many generated pods
	return;
    x = gen->x;
    y = gen->y;
    mo = P_SpawnMobj (x, y, ONFLOORZ, MT_HPOD);
    if (!P_CheckPosition (mo, x, y))		// didn't fit
    {
	P_RemoveMobj (mo);
	return;
    }
    P_SetMobjState (mo, S_HPOD_GROW1);
    an = ((angle_t)(P_Random () << 24)) >> ANGLETOFINESHIFT;	// P_ThrustMobj, 4.5 speed
    mo->momx += FixedMul ((fixed_t)(4.5*FRACUNIT), finecosine[an]);
    mo->momy += FixedMul ((fixed_t)(4.5*FRACUNIT), finesine[an]);
    gen->threshold++;				// bump live-pod count
    mo->tracer = gen;				// link pod back to its generator
}

// Ambient sound source: SILENT here (no wind/waterfall SFX in this engine).
void A_HESound (mobj_t* mo)
{
    (void)mo;
}

// Teleport-glitter generators: drop a rising glitter sprite near the source.
void A_HSpawnTeleGlitter (mobj_t* actor)
{
    mobj_t*	mo;
    int		r1, r2;

    r1 = P_Random ();
    r2 = P_Random ();
    mo = P_SpawnMobj (actor->x + ((r2 & 31) - 16) * FRACUNIT,
		      actor->y + ((r1 & 31) - 16) * FRACUNIT,
		      actor->subsector->sector->floorheight, MT_HTELEGLITTER);
    mo->momz = FRACUNIT / 4;
}

void A_HSpawnTeleGlitter2 (mobj_t* actor)
{
    mobj_t*	mo;
    int		r1, r2;

    r1 = P_Random ();
    r2 = P_Random ();
    mo = P_SpawnMobj (actor->x + ((r2 & 31) - 16) * FRACUNIT,
		      actor->y + ((r1 & 31) - 16) * FRACUNIT,
		      actor->subsector->sector->floorheight, MT_HTELEGLITTER2);
    mo->momz = FRACUNIT / 4;
}

// Glitter accelerates upward over its short life (health is the age counter).
void A_HAccTeleGlitter (mobj_t* actor)
{
    if (++actor->health > 35)
	actor->momz += actor->momz / 2;
}

// Key gizmo base: spawn its floating coloured orb 60 units up.
void A_HInitKeyGizmo (mobj_t* gizmo)
{
    mobj_t*	mo;
    statenum_t	state = S_NULL;

    switch (gizmo->type)
    {
      case MT_HKEYGIZMOBLUE:	state = S_HKGZ_BLUE;	break;
      case MT_HKEYGIZMOGREEN:	state = S_HKGZ_GREEN;	break;
      case MT_HKEYGIZMOYELLOW:	state = S_HKGZ_YELLOW;	break;
      default:			break;
    }
    mo = P_SpawnMobj (gizmo->x, gizmo->y, gizmo->z + 60*FRACUNIT, MT_HKEYGIZMOFLOAT);
    P_SetMobjState (mo, state);
}

// Volcano: arm a random delay before the next eruption.
void A_HVolcanoSet (mobj_t* volcano)
{
    volcano->tics = 105 + (P_Random () & 127);
}

// Volcano eruption: hurl 1-3 ballistic blasts in random directions.
void A_HVolcanoBlast (mobj_t* volcano)
{
    int		i, count;
    mobj_t*	blast;
    angle_t	angle;

    count = 1 + (P_Random () % 3);
    for (i = 0; i < count; i++)
    {
	blast = P_SpawnMobj (volcano->x, volcano->y,
			     volcano->z + 44*FRACUNIT, MT_HVOLCANOBLAST);
	blast->target = volcano;
	angle = (angle_t)(P_Random () << 24);
	blast->angle = angle;
	angle >>= ANGLETOFINESHIFT;
	blast->momx = FixedMul (1*FRACUNIT, finecosine[angle]);
	blast->momy = FixedMul (1*FRACUNIT, finesine[angle]);
	blast->momz = (fixed_t)(2.5*FRACUNIT) + (P_Random () << 10);
	P_CheckMissileSpawn (blast);
    }
}

// Volcano blast impact: a small radius burn, then fling 4 tiny tblasts outward.
void A_HVolcBallImpact (mobj_t* ball)
{
    int		i;
    mobj_t*	tiny;
    angle_t	angle;

    if (ball->z <= ball->floorz)
    {
	ball->flags  |= MF_NOGRAVITY;
	ball->flags2 &= ~MF2_LOGRAV;
	ball->z += 28*FRACUNIT;
    }
    P_RadiusAttack (ball, ball->target, 25);
    for (i = 0; i < 4; i++)
    {
	tiny = P_SpawnMobj (ball->x, ball->y, ball->z, MT_HVOLCANOTBLAST);
	tiny->target = ball;
	angle = i * ANG90;
	tiny->angle = angle;
	angle >>= ANGLETOFINESHIFT;
	tiny->momx = FixedMul ((fixed_t)(FRACUNIT*.7), finecosine[angle]);
	tiny->momy = FixedMul ((fixed_t)(FRACUNIT*.7), finesine[angle]);
	tiny->momz = FRACUNIT + (P_Random () << 9);
	P_CheckMissileSpawn (tiny);
    }
}

// ---------------------------------------------------------------------------
// Table fill helper (same as heretic.c)
// ---------------------------------------------------------------------------
static void ST (statenum_t s, spritenum_t spr, int frame, int tics,
		actionf_p1 act, statenum_t next)
{
    states[s].sprite      = spr;
    states[s].frame       = frame;
    states[s].tics        = tics;
    states[s].action.acp1 = act;
    states[s].nextstate   = next;
    states[s].misc1 = states[s].misc2 = 0;
}

void Heretic_Deco_Init (void)
{
    mobjinfo_t*	m;

    // ====================================================================
    // Volcano pod: shootable, spits goo when hurt, explodes on death.
    // ====================================================================
    ST (S_HPOD_WAIT1, SPR_HPOD,     0, 10, NULL,                     S_HPOD_WAIT1);
    ST (S_HPOD_PAIN1, SPR_HPOD,     1, 14, (actionf_p1)A_HPodPain,   S_HPOD_WAIT1);
    ST (S_HPOD_DIE1,  SPR_HPOD, 32770,  5, (actionf_p1)A_HRemovePod, S_HPOD_DIE2);
    ST (S_HPOD_DIE2,  SPR_HPOD, 32771,  5, (actionf_p1)A_Scream,     S_HPOD_DIE3);
    ST (S_HPOD_DIE3,  SPR_HPOD, 32772,  5, (actionf_p1)A_Explode,    S_HPOD_DIE4);
    ST (S_HPOD_DIE4,  SPR_HPOD, 32773, 10, NULL,                     S_NULL);
    ST (S_HPOD_GROW1, SPR_HPOD,     8,  3, NULL, S_HPOD_GROW2);
    ST (S_HPOD_GROW2, SPR_HPOD,     9,  3, NULL, S_HPOD_GROW3);
    ST (S_HPOD_GROW3, SPR_HPOD,    10,  3, NULL, S_HPOD_GROW4);
    ST (S_HPOD_GROW4, SPR_HPOD,    11,  3, NULL, S_HPOD_GROW5);
    ST (S_HPOD_GROW5, SPR_HPOD,    12,  3, NULL, S_HPOD_GROW6);
    ST (S_HPOD_GROW6, SPR_HPOD,    13,  3, NULL, S_HPOD_GROW7);
    ST (S_HPOD_GROW7, SPR_HPOD,    14,  3, NULL, S_HPOD_GROW8);
    ST (S_HPOD_GROW8, SPR_HPOD,    15,  3, NULL, S_HPOD_WAIT1);
    ST (S_HPODGOO1,   SPR_HPOD,     6,  8, NULL, S_HPODGOO2);
    ST (S_HPODGOO2,   SPR_HPOD,     7,  8, NULL, S_HPODGOO1);
    ST (S_HPODGOOX,   SPR_HPOD,     6, 10, NULL, S_NULL);
    ST (S_HPODGEN,    SPR_HAMG,     0, 35, (actionf_p1)A_HMakePod, S_HPODGEN);

    m = &mobjinfo[MT_HPOD];
    m->doomednum = 2035;      m->spawnstate  = S_HPOD_WAIT1; m->spawnhealth = 45;
    m->seestate  = S_NULL;    m->seesound  = sfx_None;       m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_HPOD_PAIN1;  m->painchance = 255;
    m->painsound = sfx_None;  m->meleestate = S_NULL;        m->missilestate = S_NULL;
    m->deathstate = S_HPOD_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_barexp;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 54*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_NOBLOOD|MF_SHOOTABLE|MF_DROPOFF; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HPODGOO];
    m->doomednum = -1;        m->spawnstate  = S_HPODGOO1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;    m->seesound  = sfx_None;       m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;        m->painchance = 0;
    m->painsound = sfx_None;  m->meleestate = S_NULL;        m->missilestate = S_NULL;
    m->deathstate = S_HPODGOOX; m->xdeathstate = S_NULL;     m->deathsound = sfx_None;
    m->speed = 0; m->radius = 2*FRACUNIT; m->height = 4*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF; m->flags2 = MF2_LOGRAV; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HPODGEN];
    m->doomednum = 43;        m->spawnstate  = S_HPODGEN;    m->spawnhealth = 1000;
    m->seestate  = S_NULL;    m->seesound  = sfx_None;       m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;        m->painchance = 0;
    m->painsound = sfx_None;  m->meleestate = S_NULL;        m->missilestate = S_NULL;
    m->deathstate = S_NULL;   m->xdeathstate = S_NULL;       m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOSECTOR; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // Ambient sound sources (silent markers -- no engine wind/waterfall SFX)
    // ====================================================================
    ST (S_HSND_WIND,      SPR_HAMG, 0, 100, (actionf_p1)A_HESound, S_HSND_WIND);
    ST (S_HSND_WATERFALL, SPR_HAMG, 0,  85, (actionf_p1)A_HESound, S_HSND_WATERFALL);

    m = &mobjinfo[MT_HSOUNDWIND];
    m->doomednum = 42;        m->spawnstate  = S_HSND_WIND;  m->spawnhealth = 1000;
    m->seestate  = S_NULL;    m->seesound  = sfx_None;       m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;        m->painchance = 0;
    m->painsound = sfx_None;  m->meleestate = S_NULL;        m->missilestate = S_NULL;
    m->deathstate = S_NULL;   m->xdeathstate = S_NULL;       m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOSECTOR; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSOUNDWATERFALL];
    m->doomednum = 41;        m->spawnstate  = S_HSND_WATERFALL; m->spawnhealth = 1000;
    m->seestate  = S_NULL;    m->seesound  = sfx_None;       m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;        m->painchance = 0;
    m->painsound = sfx_None;  m->meleestate = S_NULL;        m->missilestate = S_NULL;
    m->deathstate = S_NULL;   m->xdeathstate = S_NULL;       m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOSECTOR; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // Hanging skulls (ceiling-mounted, four heights)
    // ====================================================================
    ST (S_HSKH70, SPR_HSK1, 0, -1, NULL, S_NULL);
    ST (S_HSKH60, SPR_HSK2, 0, -1, NULL, S_NULL);
    ST (S_HSKH45, SPR_HSK3, 0, -1, NULL, S_NULL);
    ST (S_HSKH35, SPR_HSK4, 0, -1, NULL, S_NULL);

    m = &mobjinfo[MT_HSKULLHANG70];
    m->doomednum = 17; m->spawnstate = S_HSKH70; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 70*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSKULLHANG60];
    m->doomednum = 24; m->spawnstate = S_HSKH60; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 60*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSKULLHANG45];
    m->doomednum = 25; m->spawnstate = S_HSKH45; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 45*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSKULLHANG35];
    m->doomednum = 26; m->spawnstate = S_HSKH35; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 35*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    // (H) Dead Corvus -- a placeable dead-player corpse, Heretic's answer to DOOM's
    // thing 15 (there is none in vanilla Heretic).  Uses the Heretic player sprite
    // PLAY frame 15 -- the resting corpse the death sequence ends on (crispy
    // S_PLAY_DIE9).  Editor number 56 in heretic_mode only: DOOM's PLAY sprite has no
    // frame 15, so its doomednum stays -1 there (it would I_Error on render).
    ST (S_HDEADCORVUS, SPR_PLAY, 15, -1, NULL, S_NULL);
    {
	extern int heretic_mode;
	m = &mobjinfo[MT_HDEADCORVUS];
	m->doomednum = heretic_mode ? 56 : -1; m->spawnstate = S_HDEADCORVUS; m->spawnhealth = 1000;
	m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
	m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
	m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
	m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
	m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
	m->damage = 0; m->activesound = sfx_None;
	m->flags = 0; m->flags2 = 0; m->raisestate = S_NULL;	// inert floor corpse (like MT_MISC62)
    }

    // ====================================================================
    // Chandelier (ceiling, animated) + serpent torch (floor, animated)
    // ====================================================================
    ST (S_HCHDL1, SPR_HCHD, 0, 4, NULL, S_HCHDL2);
    ST (S_HCHDL2, SPR_HCHD, 1, 4, NULL, S_HCHDL3);
    ST (S_HCHDL3, SPR_HCHD, 2, 4, NULL, S_HCHDL1);

    m = &mobjinfo[MT_HCHANDELIER];
    m->doomednum = 28; m->spawnstate = S_HCHDL1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 60*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    ST (S_HSRTC1, SPR_HSRT, 0, 4, NULL, S_HSRTC2);
    ST (S_HSRTC2, SPR_HSRT, 1, 4, NULL, S_HSRTC3);
    ST (S_HSRTC3, SPR_HSRT, 2, 4, NULL, S_HSRTC1);

    m = &mobjinfo[MT_HSERPTORCH];
    m->doomednum = 27; m->spawnstate = S_HSRTC1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 12*FRACUNIT; m->height = 54*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // Small pillar + stalagmites/stalactites (static solids)
    // ====================================================================
    ST (S_HSMALLPILLAR,     SPR_HSMP, 0, -1, NULL, S_NULL);
    ST (S_HSTGS,            SPR_HSGS, 0, -1, NULL, S_NULL);
    ST (S_HSTGL,            SPR_HSGL, 0, -1, NULL, S_NULL);
    ST (S_HSTCS,            SPR_HSCS, 0, -1, NULL, S_NULL);
    ST (S_HSTCL,            SPR_HSCL, 0, -1, NULL, S_NULL);

    m = &mobjinfo[MT_HSMALLPILLAR];
    m->doomednum = 29; m->spawnstate = S_HSMALLPILLAR; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 34*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSTALAGMITESMALL];
    m->doomednum = 37; m->spawnstate = S_HSTGS; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 8*FRACUNIT; m->height = 32*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSTALAGMITELARGE];
    m->doomednum = 38; m->spawnstate = S_HSTGL; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 12*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSTALACTITESMALL];
    m->doomednum = 39; m->spawnstate = S_HSTCS; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 8*FRACUNIT; m->height = 36*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSTALACTITELARGE];
    m->doomednum = 40; m->spawnstate = S_HSTCL; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 12*FRACUNIT; m->height = 68*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // Fire brazier (animated), barrel, brown pillar, moss x2, wall torch,
    // hanging corpse.
    // ====================================================================
    ST (S_HFBR1, SPR_HKFR, 0, 3, NULL, S_HFBR2);
    ST (S_HFBR2, SPR_HKFR, 1, 3, NULL, S_HFBR3);
    ST (S_HFBR3, SPR_HKFR, 2, 3, NULL, S_HFBR4);
    ST (S_HFBR4, SPR_HKFR, 3, 3, NULL, S_HFBR5);
    ST (S_HFBR5, SPR_HKFR, 4, 3, NULL, S_HFBR6);
    ST (S_HFBR6, SPR_HKFR, 5, 3, NULL, S_HFBR7);
    ST (S_HFBR7, SPR_HKFR, 6, 3, NULL, S_HFBR8);
    ST (S_HFBR8, SPR_HKFR, 7, 3, NULL, S_HFBR1);

    m = &mobjinfo[MT_HFIREBRAZIER];
    m->doomednum = 76; m->spawnstate = S_HFBR1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 44*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    ST (S_HBARREL, SPR_HBAR, 0, -1, NULL, S_NULL);
    m = &mobjinfo[MT_HBARREL];
    m->doomednum = 44; m->spawnstate = S_HBARREL; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 12*FRACUNIT; m->height = 32*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    ST (S_HBRPILLAR, SPR_HBRP, 0, -1, NULL, S_NULL);
    m = &mobjinfo[MT_HBRPILLAR];
    m->doomednum = 47; m->spawnstate = S_HBRPILLAR; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 14*FRACUNIT; m->height = 128*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    ST (S_HMOSS1, SPR_HMS1, 0, -1, NULL, S_NULL);
    ST (S_HMOSS2, SPR_HMS2, 0, -1, NULL, S_NULL);

    m = &mobjinfo[MT_HMOSS1];
    m->doomednum = 48; m->spawnstate = S_HMOSS1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 23*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HMOSS2];
    m->doomednum = 49; m->spawnstate = S_HMOSS2; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 27*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    ST (S_HWTRCH1, SPR_HWTR, 32768, 6, NULL, S_HWTRCH2);
    ST (S_HWTRCH2, SPR_HWTR, 32769, 6, NULL, S_HWTRCH3);
    ST (S_HWTRCH3, SPR_HWTR, 32770, 6, NULL, S_HWTRCH1);

    m = &mobjinfo[MT_HWALLTORCH];
    m->doomednum = 50; m->spawnstate = S_HWTRCH1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    ST (S_HHANGCORPSE, SPR_HHCO, 0, -1, NULL, S_NULL);
    m = &mobjinfo[MT_HHANGINGCORPSE];
    m->doomednum = 51; m->spawnstate = S_HHANGCORPSE; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 8*FRACUNIT; m->height = 104*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // Key gizmos (blue/green/yellow) + the floating coloured orb on top
    // ====================================================================
    ST (S_HKGZ1, SPR_HKG1, 0,  1, NULL,                       S_HKGZ2);
    ST (S_HKGZ2, SPR_HKG1, 0,  1, (actionf_p1)A_HInitKeyGizmo,S_HKGZ3);
    ST (S_HKGZ3, SPR_HKG1, 0, -1, NULL,                       S_NULL);
    ST (S_HKGZ_START,  SPR_HKGB,     0,  1, NULL, S_HKGZ_START);
    ST (S_HKGZ_BLUE,   SPR_HKGB, 32768, -1, NULL, S_NULL);
    ST (S_HKGZ_GREEN,  SPR_HKGG, 32768, -1, NULL, S_NULL);
    ST (S_HKGZ_YELLOW, SPR_HKGY, 32768, -1, NULL, S_NULL);

    m = &mobjinfo[MT_HKEYGIZMOBLUE];
    m->doomednum = 94; m->spawnstate = S_HKGZ1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 50*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HKEYGIZMOGREEN];
    m->doomednum = 95; m->spawnstate = S_HKGZ1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 50*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HKEYGIZMOYELLOW];
    m->doomednum = 96; m->spawnstate = S_HKGZ1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 50*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HKEYGIZMOFLOAT];
    m->doomednum = -1; m->spawnstate = S_HKGZ_START; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // Volcano (erupts ballistic blasts) + its blast + secondary tblast.
    // ====================================================================
    ST (S_HVOLC1, SPR_HVLC, 0, 350, NULL,                        S_HVOLC2);
    ST (S_HVOLC2, SPR_HVLC, 0,  35, (actionf_p1)A_HVolcanoSet,   S_HVOLC3);
    ST (S_HVOLC3, SPR_HVLC, 1,   3, NULL,                        S_HVOLC4);
    ST (S_HVOLC4, SPR_HVLC, 2,   3, NULL,                        S_HVOLC5);
    ST (S_HVOLC5, SPR_HVLC, 3,   3, NULL,                        S_HVOLC6);
    ST (S_HVOLC6, SPR_HVLC, 1,   3, NULL,                        S_HVOLC7);
    ST (S_HVOLC7, SPR_HVLC, 2,   3, NULL,                        S_HVOLC8);
    ST (S_HVOLC8, SPR_HVLC, 3,   3, NULL,                        S_HVOLC9);
    ST (S_HVOLC9, SPR_HVLC, 4,  10, (actionf_p1)A_HVolcanoBlast, S_HVOLC2);

    m = &mobjinfo[MT_HVOLCANO];
    m->doomednum = 87; m->spawnstate = S_HVOLC1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 12*FRACUNIT; m->height = 20*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID; m->flags2 = 0; m->raisestate = S_NULL;

    // volcano blast (main ballistic ball): trail-puff dropped (was A_BeastPuff)
    ST (S_HVOLCBALL1,  SPR_HVFB, 0, 4, NULL, S_HVOLCBALL2);
    ST (S_HVOLCBALL2,  SPR_HVFB, 1, 4, NULL, S_HVOLCBALL1);
    ST (S_HVOLCBALLX1, SPR_HXPL, 0, 4, (actionf_p1)A_HVolcBallImpact, S_HVOLCBALLX2);
    ST (S_HVOLCBALLX2, SPR_HXPL, 1, 4, NULL, S_HVOLCBALLX3);
    ST (S_HVOLCBALLX3, SPR_HXPL, 2, 4, NULL, S_HVOLCBALLX4);
    ST (S_HVOLCBALLX4, SPR_HXPL, 3, 4, NULL, S_HVOLCBALLX5);
    ST (S_HVOLCBALLX5, SPR_HXPL, 4, 4, NULL, S_HVOLCBALLX6);
    ST (S_HVOLCBALLX6, SPR_HXPL, 5, 4, NULL, S_NULL);

    m = &mobjinfo[MT_HVOLCANOBLAST];
    m->doomednum = -1; m->spawnstate = S_HVOLCBALL1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_HVOLCBALLX1; m->xdeathstate = S_NULL; m->deathsound = sfx_firxpl;
    m->speed = 2*FRACUNIT; m->radius = 8*FRACUNIT; m->height = 8*FRACUNIT; m->mass = 100;
    m->damage = 2; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF; m->flags2 = MF2_LOGRAV; m->raisestate = S_NULL;

    // secondary tblast (spread on impact)
    ST (S_HVOLCTBALL1,  SPR_HVTF, 0, 4, NULL, S_HVOLCTBALL2);
    ST (S_HVOLCTBALL2,  SPR_HVTF, 1, 4, NULL, S_HVOLCTBALL1);
    ST (S_HVOLCTBALLX1, SPR_HSFF, 2, 4, NULL, S_HVOLCTBALLX2);
    ST (S_HVOLCTBALLX2, SPR_HSFF, 1, 4, NULL, S_HVOLCTBALLX3);
    ST (S_HVOLCTBALLX3, SPR_HSFF, 0, 4, NULL, S_HVOLCTBALLX4);
    ST (S_HVOLCTBALLX4, SPR_HSFF, 1, 4, NULL, S_HVOLCTBALLX5);
    ST (S_HVOLCTBALLX5, SPR_HSFF, 2, 4, NULL, S_HVOLCTBALLX6);
    ST (S_HVOLCTBALLX6, SPR_HSFF, 3, 4, NULL, S_HVOLCTBALLX7);
    ST (S_HVOLCTBALLX7, SPR_HSFF, 4, 4, NULL, S_NULL);

    m = &mobjinfo[MT_HVOLCANOTBLAST];
    m->doomednum = -1; m->spawnstate = S_HVOLCTBALL1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_HVOLCTBALLX1; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 2*FRACUNIT; m->radius = 8*FRACUNIT; m->height = 6*FRACUNIT; m->mass = 100;
    m->damage = 1; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF; m->flags2 = MF2_LOGRAV; m->raisestate = S_NULL;

    // ====================================================================
    // Teleport-glitter generators + the rising glitter sprites they emit.
    // ====================================================================
    ST (S_HTGLITGEN1, SPR_HTGL, 0, 8, (actionf_p1)A_HSpawnTeleGlitter,  S_HTGLITGEN1);
    ST (S_HTGLITGEN2, SPR_HTGL, 5, 8, (actionf_p1)A_HSpawnTeleGlitter2, S_HTGLITGEN2);
    ST (S_HTGLITTER1_1, SPR_HTGL, 32768, 2, NULL,                         S_HTGLITTER1_2);
    ST (S_HTGLITTER1_2, SPR_HTGL, 32769, 2, (actionf_p1)A_HAccTeleGlitter,S_HTGLITTER1_3);
    ST (S_HTGLITTER1_3, SPR_HTGL, 32770, 2, NULL,                         S_HTGLITTER1_4);
    ST (S_HTGLITTER1_4, SPR_HTGL, 32771, 2, (actionf_p1)A_HAccTeleGlitter,S_HTGLITTER1_5);
    ST (S_HTGLITTER1_5, SPR_HTGL, 32772, 2, NULL,                         S_HTGLITTER1_1);
    ST (S_HTGLITTER2_1, SPR_HTGL, 32773, 2, NULL,                         S_HTGLITTER2_2);
    ST (S_HTGLITTER2_2, SPR_HTGL, 32774, 2, (actionf_p1)A_HAccTeleGlitter,S_HTGLITTER2_3);
    ST (S_HTGLITTER2_3, SPR_HTGL, 32775, 2, NULL,                         S_HTGLITTER2_4);
    ST (S_HTGLITTER2_4, SPR_HTGL, 32776, 2, (actionf_p1)A_HAccTeleGlitter,S_HTGLITTER2_5);
    ST (S_HTGLITTER2_5, SPR_HTGL, 32777, 2, NULL,                         S_HTGLITTER2_1);

    m = &mobjinfo[MT_HTELEGLITGEN];
    m->doomednum = 74; m->spawnstate = S_HTGLITGEN1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY|MF_NOSECTOR; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HTELEGLITGEN2];
    m->doomednum = 52; m->spawnstate = S_HTGLITGEN2; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY|MF_NOSECTOR; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HTELEGLITTER];
    m->doomednum = -1; m->spawnstate = S_HTGLITTER1_1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY|MF_MISSILE; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HTELEGLITTER2];
    m->doomednum = -1; m->spawnstate = S_HTGLITTER2_1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY|MF_MISSILE; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // Teleport fog (Heretic TELE sprite; sub-actor, not map-placed)
    // ====================================================================
    ST (S_HTFOG1,  SPR_HTLE, 32768, 6, NULL, S_HTFOG2);
    ST (S_HTFOG2,  SPR_HTLE, 32769, 6, NULL, S_HTFOG3);
    ST (S_HTFOG3,  SPR_HTLE, 32770, 6, NULL, S_HTFOG4);
    ST (S_HTFOG4,  SPR_HTLE, 32771, 6, NULL, S_HTFOG5);
    ST (S_HTFOG5,  SPR_HTLE, 32772, 6, NULL, S_HTFOG6);
    ST (S_HTFOG6,  SPR_HTLE, 32773, 6, NULL, S_HTFOG7);
    ST (S_HTFOG7,  SPR_HTLE, 32774, 6, NULL, S_HTFOG8);
    ST (S_HTFOG8,  SPR_HTLE, 32775, 6, NULL, S_HTFOG9);
    ST (S_HTFOG9,  SPR_HTLE, 32774, 6, NULL, S_HTFOG10);
    ST (S_HTFOG10, SPR_HTLE, 32773, 6, NULL, S_HTFOG11);
    ST (S_HTFOG11, SPR_HTLE, 32772, 6, NULL, S_HTFOG12);
    ST (S_HTFOG12, SPR_HTLE, 32771, 6, NULL, S_HTFOG13);
    ST (S_HTFOG13, SPR_HTLE, 32770, 6, NULL, S_NULL);

    m = &mobjinfo[MT_HTFOG];
    m->doomednum = -1; m->spawnstate = S_HTFOG1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;
}
