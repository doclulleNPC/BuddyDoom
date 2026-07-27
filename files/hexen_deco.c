// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Additive Hexen DECORATION / SCENERY actors in the DOOM engine.
//	Ported from crispy-doom's hexen/info.c (frame tables + mobjinfo) and
//	hexen/a_action.c (action funcs), adapted to this engine's 1-arg action
//	signature and MF_/MF2_ flag set.  Same additive approach as hexen.c and
//	heretic_deco.c: Hexen_Deco_Init() appends states + mobjinfo at runtime;
//	the enum slots live at the tail of statenum_t/mobjtype_t/spritenum_t
//	(info.h).  Sprites use the NATIVE Hexen 4-char codes registered directly
//	into sprnames_builtin[] (like the poison PSBG/SHRM precedent), except for
//	five codes that collide with existing DOOM sprites -- those are aliased
//	(see [NOTES] in hxdeco_snippets.txt / the SPRITE table below).
//
//	Coverage (map-placeable scenery + their destructible sub-actors):
//	  * ~75 plain statics: winged statue, rocks, chandelier(+unlit), trees,
//	    stumps, mushrooms, stalagmites/stalactites (stone + ice), moss, swamp
//	    vine, corpses (kabob/sleeping/hanging/lynched), tombstones x7,
//	    gargoyle statues x13, banner, log, brown/black rocks, rubble, vase.
//	  * Animated light props: wall torch(+unlit), twined torch(+unlit),
//	    fire bull(+unlit), cauldron(+unlit), brass torch, fire thing, blue
//	    candle, candle, tele-smoke, small/large flame, chandelier.
//	  * Solid furniture: barrel, bucket, iron maiden, chains x7, table clutter x10.
//	  * DESTRUCTIBLE (shootable, shatter into MT_*BIT chunks or burn):
//	    pottery x3 (+POTTERYBIT), suit of armor (+ARMORCHUNK), xmas tree,
//	    shrub x2, destructible tree, corpse-sitting (+CORPSEBIT), bell.
//	  * Cosmetic sub-actors: corpse blood drip, leaf1/leaf2 (+ leaf spawner),
//	    rock debris x3, blood pool.
//
//	SIMPLIFICATIONS (documented in [NOTES]):
//	  - MT_TFOG / MT_TELEPORTMAN already exist in the base DOOM engine -- NOT
//	    re-added here (the Hexen ednum 14 teleport-dest + fog reuse them).
//	  - A_TreeDeath: this engine has no fire-damage tracking (MF2_FIREDAMAGE),
//	    so a destroyed tree/shrub goes straight to its burn/explode sequence
//	    on death instead of the two-stage "reset then burn-only-on-fire".
//	  - A_PotteryCheck (per-bit line-of-sight persistence) dropped; pottery
//	    bits just settle as debris.  Pottery/suit args[0] item-spawn dropped.
//	  - MT_BRIDGE ported as a static invisible solid platform; the three
//	    orbiting MT_BRIDGEBALL cosmetics (need special1/args + orbit table)
//	    are dropped.  MT_BRIDGEBALL itself is not ported.
//	  - Flames spawn visible + looping (the ACS hide/unhide + args-timed
//	    self-extinguish of the *_TEMP variants is dropped; TEMP variants not
//	    ported).
//	  - Dropped Hexen MF2_* not defined here (SLIDE/PUSHABLE/TELESTOMP/
//	    PASSMOBJ/NOTELEPORT/FLOORCLIP...).  MF2_LOGRAV IS kept (blood drip,
//	    leaves).  MF_TRANSLUCENT dropped (not defined) on tele-smoke/fog.
//	  - No decoration-specific SFX in this build: shatter/break -> sfx_barexp,
//	    bell -> sfx_metal, corpse-gib skull -> sfx_slop, blood drip -> silent.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "info.h"
#include "m_random.h"
#include "m_fixed.h"
#include "tables.h"		// finecosine/finesine, ANGLETOFINESHIFT
#include "sounds.h"
#include "w_wad.h"
#include "p_mobj.h"
#include "hexen.h"

extern state_t *states;
extern mobjinfo_t *mobjinfo;

// engine helpers (no public header -- declare by hand, same as hexen.c/heretic_deco.c)
extern void	A_Scream (mobj_t*);
extern void	A_Fall (mobj_t*);		// == Hexen A_NoBlocking (clears MF_SOLID)
extern void	A_Explode (mobj_t*);		// radius blast (P_RadiusAttack 128)
extern void	S_StartSound (void* origin, int sfx_id);
extern mobj_t*	P_SpawnMobj (fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
extern boolean	P_SetMobjState (mobj_t* mobj, statenum_t state);
extern void	P_RemoveMobj (mobj_t* mobj);

#define P_SubRandom()	(P_Random () - P_Random ())

// Hexen's P_ThrustMobj (not in this engine) -- add momentum along an angle.
static void ThrustMobj (mobj_t* mo, angle_t angle, fixed_t move)
{
    angle >>= ANGLETOFINESHIFT;
    mo->momx += FixedMul (move, finecosine[angle]);
    mo->momy += FixedMul (move, finesine[angle]);
}

// ---------------------------------------------------------------------------
// Action functions (crispy hexen/a_action.c, adapted to the 1-arg signature).
// ---------------------------------------------------------------------------

// Pottery shatters into 6..9 flying bits, then vanishes (item-spawn arg dropped).
void A_PotteryExplode (mobj_t* actor)
{
    mobj_t*	mo = NULL;
    int		i;

    for (i = (P_Random () & 3) + 3; i; i--)
    {
	mo = P_SpawnMobj (actor->x, actor->y, actor->z, MT_XZPOTTERYBIT);
	P_SetMobjState (mo, mo->info->spawnstate + (P_Random () % 5));
	mo->momz = ((P_Random () & 7) + 5) * (3 * FRACUNIT / 4);
	mo->momx = P_SubRandom () << (FRACBITS - 6);
	mo->momy = P_SubRandom () << (FRACBITS - 6);
    }
    if (mo)
	S_StartSound (mo, sfx_barexp);
    P_RemoveMobj (actor);
}

// A pottery bit picks one of the five broken-shard frames and settles as debris.
void A_PotteryChooseBit (mobj_t* actor)
{
    P_SetMobjState (actor, actor->info->deathstate + (P_Random () % 5) + 1);
}

// Suit of armor bursts into 10 numbered chunks, then vanishes (item-spawn dropped).
void A_SoAExplode (mobj_t* actor)
{
    mobj_t*	mo = NULL;
    int		i, r1, r2, r3;

    for (i = 0; i < 10; i++)
    {
	r1 = P_Random ();
	r2 = P_Random ();
	r3 = P_Random ();
	mo = P_SpawnMobj (actor->x + ((r3 - 128) << 12),
			  actor->y + ((r2 - 128) << 12),
			  actor->z + (r1 * actor->height / 256),
			  MT_XZARMORCHUNK);
	P_SetMobjState (mo, mo->info->spawnstate + i);
	mo->momz = ((P_Random () & 7) + 5) * FRACUNIT;
	mo->momx = P_SubRandom () << (FRACBITS - 6);
	mo->momy = P_SubRandom () << (FRACBITS - 6);
    }
    if (mo)
	S_StartSound (mo, sfx_barexp);
    P_RemoveMobj (actor);
}

// The lynched corpse occasionally drips a blood gout from its midriff.
void A_CorpseBloodDrip (mobj_t* actor)
{
    if (P_Random () > 128)
	return;
    P_SpawnMobj (actor->x, actor->y, actor->z + actor->height / 2,
		 MT_XZCORPSEBLOODDRIP);
}

// The sitting corpse (shootable) bursts into flying gib bits + a tumbling skull.
void A_CorpseExplode (mobj_t* actor)
{
    mobj_t*	mo;
    int		i;

    for (i = (P_Random () & 3) + 3; i; i--)
    {
	mo = P_SpawnMobj (actor->x, actor->y, actor->z, MT_XZCORPSEBIT);
	P_SetMobjState (mo, mo->info->spawnstate + (P_Random () % 3));
	mo->momz = ((P_Random () & 7) + 5) * (3 * FRACUNIT / 4);
	mo->momx = P_SubRandom () << (FRACBITS - 6);
	mo->momy = P_SubRandom () << (FRACBITS - 6);
    }
    mo = P_SpawnMobj (actor->x, actor->y, actor->z, MT_XZCORPSEBIT);
    P_SetMobjState (mo, S_ZD_CORPSEBIT4);		// the skull
    if (mo)
    {
	mo->momz = ((P_Random () & 7) + 5) * (3 * FRACUNIT / 4);
	mo->momx = P_SubRandom () << (FRACBITS - 6);
	mo->momy = P_SubRandom () << (FRACBITS - 6);
	S_StartSound (mo, sfx_slop);
    }
    P_RemoveMobj (actor);
}

// Tree / shrub destroyed: this engine has no fire-damage tracking, so go
// straight to the burn/explode meleestate sequence (crispy's non-fire "reset,
// stay standing until burned" stage is dropped).
void A_TreeDeath (mobj_t* actor)
{
    P_SetMobjState (actor, actor->info->meleestate);
}

// Bell: on the first swing frame, become weightless and stretch the swing arc.
void A_BellReset1 (mobj_t* actor)
{
    actor->flags  |= MF_NOGRAVITY;
    actor->height <<= 2;
}

// Bell: after the swing settles, re-arm it (shootable again, health reset).
void A_BellReset2 (mobj_t* actor)
{
    actor->flags |= MF_SHOOTABLE;
    actor->flags &= ~MF_CORPSE;
    actor->health = 5;
}

// Leaf spawner: puff out 1..4 fluttering leaves, thrust away from the spawner.
void A_LeafSpawn (mobj_t* actor)
{
    mobj_t*	mo;
    int		i;

    for (i = (P_Random () & 3) + 1; i; i--)
    {
	mobjtype_t	type = MT_XZLEAF1 + (P_Random () & 1);
	fixed_t		z = actor->z + (P_Random () << 14);
	fixed_t		y = actor->y + (P_SubRandom () << 14);
	fixed_t		x = actor->x + (P_SubRandom () << 14);

	mo = P_SpawnMobj (x, y, z, type);
	if (mo)
	{
	    ThrustMobj (mo, actor->angle, (P_Random () << 9) + 3 * FRACUNIT);
	    mo->target = actor;
	    mo->movecount = 0;		// leaf age (crispy special1)
	}
    }
}

// Occasionally give a fluttering leaf a little upward lift.
void A_LeafThrust (mobj_t* actor)
{
    if (P_Random () > 96)
	return;
    actor->momz += (P_Random () << 9) + FRACUNIT;
}

// Age the settled leaf; re-gust it now and then, remove it after ~20 checks.
void A_LeafCheck (mobj_t* actor)
{
    actor->movecount++;
    if (actor->movecount >= 20)
    {
	P_SetMobjState (actor, S_NULL);
	return;
    }
    if (P_Random () > 64)
    {
	if (!actor->momx && !actor->momy && actor->target)
	    ThrustMobj (actor, actor->target->angle, (P_Random () << 9) + FRACUNIT);
	return;
    }
    P_SetMobjState (actor, S_ZD_LEAF1_8);
    actor->momz = (P_Random () << 9) + FRACUNIT;
    if (actor->target)
	ThrustMobj (actor, actor->target->angle, (P_Random () << 9) + 2 * FRACUNIT);
    actor->flags |= MF_MISSILE;
}

// ---------------------------------------------------------------------------
// Table-fill helpers
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

// Fill a plain (non-interactive) mobjinfo record.
static void PROP (mobjtype_t mt, int ednum, statenum_t spawn, int hp,
		  fixed_t radius, fixed_t height, int flags)
{
    mobjinfo_t*	m = &mobjinfo[mt];
    m->doomednum = ednum; m->spawnstate = spawn; m->spawnhealth = hp;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = radius; m->height = height; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None; m->flags = flags;
    m->flags2 = 0; m->raisestate = S_NULL;
}

// One-frame static prop: makes its S_NULL-terminated spawnstate + the mobjinfo.
static void STATICPROP (mobjtype_t mt, statenum_t s, spritenum_t spr, int frame,
			int ednum, fixed_t radius, fixed_t height, int flags)
{
    ST (s, spr, frame, -1, NULL, S_NULL);
    PROP (mt, ednum, s, 1000, radius, height, flags);
}

#define R(n)	((n) * FRACUNIT)
#define SOLID	MF_SOLID
#define CEIL	(MF_SPAWNCEILING | MF_NOGRAVITY)
#define NB	MF_NOBLOCKMAP

void Hexen_Deco_Init (void)
{
    mobjinfo_t*	m;

    // ====================================================================
    // Plain single-frame statics (the bulk of Hexen scenery).
    // ====================================================================
    STATICPROP (MT_XZWINGEDSTATUE, S_ZD_WINGEDSTATUE, SPR_ZSTWN, 0, 9011, R(10), R(62), SOLID);
    STATICPROP (MT_XZROCK1, S_ZD_ROCK1, SPR_ZRCK1, 0, 6,  R(20), R(16), 0);
    STATICPROP (MT_XZROCK2, S_ZD_ROCK2, SPR_ZRCK2, 0, 7,  R(20), R(16), 0);
    STATICPROP (MT_XZROCK3, S_ZD_ROCK3, SPR_ZRCK3, 0, 9,  R(20), R(16), 0);
    STATICPROP (MT_XZROCK4, S_ZD_ROCK4, SPR_ZRCK4, 0, 15, R(20), R(16), SOLID);

    STATICPROP (MT_XZTREEDEAD,      S_ZD_TREEDEAD,      SPR_ZTRE1, 0, 24, R(10), R(96),  SOLID);
    STATICPROP (MT_XZTREE,          S_ZD_TREE,          SPR_ZTRE1, 0, 25, R(15), R(128), SOLID);
    STATICPROP (MT_XZTREESWAMP182,  S_ZD_TREESWAMP182,  SPR_ZTRE2, 0, 26, R(10), R(150), SOLID);
    STATICPROP (MT_XZTREESWAMP172,  S_ZD_TREESWAMP172,  SPR_ZTRE3, 0, 27, R(10), R(120), SOLID);
    STATICPROP (MT_XZSTUMPBURNED,   S_ZD_STUMPBURNED,   SPR_ZSTM1, 0, 28, R(12), R(20),  SOLID);
    STATICPROP (MT_XZSTUMPBARE,     S_ZD_STUMPBARE,     SPR_ZSTM2, 0, 29, R(12), R(20),  SOLID);
    STATICPROP (MT_XZSTUMPSWAMP1,   S_ZD_STUMPSWAMP1,   SPR_ZSTM3, 0, 37, R(20), R(16),  0);
    STATICPROP (MT_XZSTUMPSWAMP2,   S_ZD_STUMPSWAMP2,   SPR_ZSTM4, 0, 38, R(20), R(16),  0);

    STATICPROP (MT_XZSHROOMLARGE1, S_ZD_SHROOMLARGE1, SPR_ZMSH1, 0, 39, R(20), R(16), 0);
    STATICPROP (MT_XZSHROOMLARGE2, S_ZD_SHROOMLARGE2, SPR_ZMSH2, 0, 40, R(20), R(16), 0);
    STATICPROP (MT_XZSHROOMLARGE3, S_ZD_SHROOMLARGE3, SPR_ZMSH3, 0, 41, R(20), R(16), 0);
    STATICPROP (MT_XZSHROOMSMALL1, S_ZD_SHROOMSMALL1, SPR_ZMSH4, 0, 42, R(20), R(16), 0);
    STATICPROP (MT_XZSHROOMSMALL2, S_ZD_SHROOMSMALL2, SPR_ZMSH5, 0, 44, R(20), R(16), 0);
    STATICPROP (MT_XZSHROOMSMALL3, S_ZD_SHROOMSMALL3, SPR_ZMSH6, 0, 45, R(20), R(16), 0);
    STATICPROP (MT_XZSHROOMSMALL4, S_ZD_SHROOMSMALL4, SPR_ZMSH7, 0, 46, R(20), R(16), 0);
    STATICPROP (MT_XZSHROOMSMALL5, S_ZD_SHROOMSMALL5, SPR_ZMSH8, 0, 47, R(20), R(16), 0);

    STATICPROP (MT_XZSTALAGMITEPILLAR, S_ZD_STALAGMITEPILLAR, SPR_ZSGMP, 0, 48, R(8), R(138), SOLID);
    STATICPROP (MT_XZSTALAGMITELARGE,  S_ZD_STALAGMITELARGE,  SPR_ZSGM1, 0, 49, R(8), R(48),  SOLID);
    STATICPROP (MT_XZSTALAGMITEMEDIUM, S_ZD_STALAGMITEMEDIUM, SPR_ZSGM2, 0, 50, R(6), R(40),  SOLID);
    STATICPROP (MT_XZSTALAGMITESMALL,  S_ZD_STALAGMITESMALL,  SPR_ZSGM3, 0, 51, R(8), R(36),  SOLID);
    STATICPROP (MT_XZSTALACTITELARGE,  S_ZD_STALACTITELARGE,  SPR_ZSLC1, 0, 52, R(8), R(66),  SOLID | CEIL);
    STATICPROP (MT_XZSTALACTITEMEDIUM, S_ZD_STALACTITEMEDIUM, SPR_ZSLC2, 0, 56, R(6), R(50),  SOLID | CEIL);
    STATICPROP (MT_XZSTALACTITESMALL,  S_ZD_STALACTITESMALL,  SPR_ZSLC3, 0, 57, R(8), R(40),  SOLID | CEIL);

    STATICPROP (MT_XZMOSSCEILING1, S_ZD_MOSSCEILING1, SPR_ZMSS1, 0, 58, R(20), R(20), CEIL);
    STATICPROP (MT_XZMOSSCEILING2, S_ZD_MOSSCEILING2, SPR_ZMSS2, 0, 59, R(20), R(24), CEIL);
    STATICPROP (MT_XZSWAMPVINE,    S_ZD_SWAMPVINE,    SPR_ZSWMV, 0, 60, R(8),  R(52), SOLID);
    STATICPROP (MT_XZCORPSEKABOB,  S_ZD_CORPSEKABOB,  SPR_ZCPS1, 0, 61, R(10), R(92), SOLID);
    STATICPROP (MT_XZCORPSESLEEPING,S_ZD_CORPSESLEEPING,SPR_ZCPS2,0, 62, R(20), R(16), 0);

    STATICPROP (MT_XZTOMBSTONERIP,         S_ZD_TOMBSTONERIP,         SPR_ZTMS1, 0, 63, R(10), R(46), SOLID);
    STATICPROP (MT_XZTOMBSTONESHANE,       S_ZD_TOMBSTONESHANE,       SPR_ZTMS2, 0, 64, R(10), R(46), SOLID);
    STATICPROP (MT_XZTOMBSTONEBIGCROSS,    S_ZD_TOMBSTONEBIGCROSS,    SPR_ZTMS3, 0, 65, R(10), R(46), SOLID);
    STATICPROP (MT_XZTOMBSTONEBRIANR,      S_ZD_TOMBSTONEBRIANR,      SPR_ZTMS4, 0, 66, R(10), R(52), SOLID);
    STATICPROP (MT_XZTOMBSTONECROSSCIRCLE, S_ZD_TOMBSTONECROSSCIRCLE, SPR_ZTMS5, 0, 67, R(10), R(52), SOLID);
    STATICPROP (MT_XZTOMBSTONESMALLCROSS,  S_ZD_TOMBSTONESMALLCROSS,  SPR_ZTMS6, 0, 68, R(8),  R(46), SOLID);
    STATICPROP (MT_XZTOMBSTONEBRIANP,      S_ZD_TOMBSTONEBRIANP,      SPR_ZTMS7, 0, 69, R(8),  R(46), SOLID);
    STATICPROP (MT_XZCORPSEHANGING,        S_ZD_CORPSEHANGING,        SPR_ZCPS3, 0, 71, R(6),  R(75), SOLID | CEIL);

    STATICPROP (MT_XZGARGGREENTALL,  S_ZD_GARGGREENTALL,  SPR_ZSTT2, 0, 72,   R(14), R(108), SOLID);
    STATICPROP (MT_XZGARGBLUETALL,   S_ZD_GARGBLUETALL,   SPR_ZSTT3, 0, 73,   R(14), R(108), SOLID);
    STATICPROP (MT_XZGARGGREENSHORT, S_ZD_GARGGREENSHORT, SPR_ZSTT4, 0, 74,   R(14), R(62),  SOLID);
    STATICPROP (MT_XZGARGBLUESHORT,  S_ZD_GARGBLUESHORT,  SPR_ZSTT5, 0, 76,   R(14), R(62),  SOLID);
    STATICPROP (MT_XZGARGSTRIPETALL, S_ZD_GARGSTRIPETALL, SPR_ZGAR1, 0, 8044, R(14), R(108), SOLID);
    STATICPROP (MT_XZGARGDKREDTALL,  S_ZD_GARGDKREDTALL,  SPR_ZGAR2, 0, 8045, R(14), R(108), SOLID);
    STATICPROP (MT_XZGARGREDTALL,    S_ZD_GARGREDTALL,    SPR_ZGAR3, 0, 8046, R(14), R(108), SOLID);
    STATICPROP (MT_XZGARGTANTALL,    S_ZD_GARGTANTALL,    SPR_ZGAR4, 0, 8047, R(14), R(108), SOLID);
    STATICPROP (MT_XZGARGRUSTTALL,   S_ZD_GARGRUSTTALL,   SPR_ZGAR5, 0, 8048, R(14), R(108), SOLID);
    STATICPROP (MT_XZGARGDKREDSHORT, S_ZD_GARGDKREDSHORT, SPR_ZGAR6, 0, 8049, R(14), R(62),  SOLID);
    STATICPROP (MT_XZGARGREDSHORT,   S_ZD_GARGREDSHORT,   SPR_ZGAR7, 0, 8050, R(14), R(62),  SOLID);
    STATICPROP (MT_XZGARGTANSHORT,   S_ZD_GARGTANSHORT,   SPR_ZGAR8, 0, 8051, R(14), R(62),  SOLID);
    STATICPROP (MT_XZGARGRUSTSHORT,  S_ZD_GARGRUSTSHORT,  SPR_ZGAR9, 0, 8052, R(14), R(62),  SOLID);

    STATICPROP (MT_XZBANNERTATTERED, S_ZD_BANNERTATTERED, SPR_ZBNR1, 0, 77, R(8),  R(120), SOLID);
    STATICPROP (MT_XZTREELARGE1,     S_ZD_TREELARGE1,     SPR_ZTRE4, 0, 78, R(15), R(180), SOLID);
    STATICPROP (MT_XZTREELARGE2,     S_ZD_TREELARGE2,     SPR_ZTRE5, 0, 79, R(15), R(180), SOLID);
    STATICPROP (MT_XZTREEGNARLED1,   S_ZD_TREEGNARLED1,   SPR_ZTRE6, 0, 80, R(22), R(100), SOLID);
    STATICPROP (MT_XZTREEGNARLED2,   S_ZD_TREEGNARLED2,   SPR_ZTRE7, 0, 87, R(22), R(100), SOLID);
    STATICPROP (MT_XZLOG,            S_ZD_LOG,            SPR_ZLOGG, 0, 88, R(20), R(25),  SOLID);

    STATICPROP (MT_XZSTALACTITEICELARGE,  S_ZD_STALACTITEICELARGE,  SPR_ZICT1, 0, 89, R(8), R(66), SOLID | CEIL);
    STATICPROP (MT_XZSTALACTITEICEMEDIUM, S_ZD_STALACTITEICEMEDIUM, SPR_ZICT2, 0, 90, R(5), R(50), SOLID | CEIL);
    STATICPROP (MT_XZSTALACTITEICESMALL,  S_ZD_STALACTITEICESMALL,  SPR_ZICT3, 0, 91, R(4), R(32), SOLID | CEIL);
    STATICPROP (MT_XZSTALACTITEICETINY,   S_ZD_STALACTITEICETINY,   SPR_ZICT4, 0, 92, R(4), R(8),  SOLID | CEIL);
    STATICPROP (MT_XZSTALAGMITEICELARGE,  S_ZD_STALAGMITEICELARGE,  SPR_ZICM1, 0, 93, R(8), R(66), SOLID);
    STATICPROP (MT_XZSTALAGMITEICEMEDIUM, S_ZD_STALAGMITEICEMEDIUM, SPR_ZICM2, 0, 94, R(5), R(50), SOLID);
    STATICPROP (MT_XZSTALAGMITEICESMALL,  S_ZD_STALAGMITEICESMALL,  SPR_ZICM3, 0, 95, R(4), R(32), SOLID);
    STATICPROP (MT_XZSTALAGMITEICETINY,   S_ZD_STALAGMITEICETINY,   SPR_ZICM4, 0, 96, R(4), R(8),  SOLID);

    STATICPROP (MT_XZROCKBROWN1, S_ZD_ROCKBROWN1, SPR_ZRKBL, 0, 97,  R(17), R(72), SOLID);
    STATICPROP (MT_XZROCKBROWN2, S_ZD_ROCKBROWN2, SPR_ZRKBS, 0, 98,  R(15), R(50), SOLID);
    STATICPROP (MT_XZROCKBLACK,  S_ZD_ROCKBLACK,  SPR_ZRKBK, 0, 99,  R(20), R(40), SOLID);
    STATICPROP (MT_XZRUBBLE1,    S_ZD_RUBBLE1,    SPR_ZRBL1, 0, 100, R(20), R(16), 0);
    STATICPROP (MT_XZRUBBLE2,    S_ZD_RUBBLE2,    SPR_ZRBL2, 0, 101, R(20), R(16), 0);
    STATICPROP (MT_XZRUBBLE3,    S_ZD_RUBBLE3,    SPR_ZRBL3, 0, 102, R(20), R(16), 0);
    STATICPROP (MT_XZVASEPILLAR, S_ZD_VASEPILLAR, SPR_ZVASE, 0, 103, R(12), R(54), SOLID);
    STATICPROP (MT_XZCORPSELYNCHED, S_ZD_CORPSELYNCHED, SPR_ZCPS4, 0, 108, R(11), R(95), SOLID | CEIL);

    STATICPROP (MT_XZIRONMAIDEN, S_ZD_IRONMAIDEN, SPR_ZIRON, 0, 8067, R(12), R(60), SOLID);
    STATICPROP (MT_XZBUCKET,     S_ZD_BUCKET,     SPR_ZBCKT, 0, 8103, R(8),  R(72), SOLID | CEIL);
    STATICPROP (MT_XZBARREL,     S_ZD_BARREL,     SPR_ZBARL, 0, 8100, R(15), R(32), SOLID);
    STATICPROP (MT_XZBLOODPOOL,  S_ZD_BLOODPOOL,  SPR_ZBDPL, 0, 111,  R(20), R(16), NB);

    // Unlit variants (single static frame off the animated sheets).
    STATICPROP (MT_XZWALLTORCH_U,    S_ZD_WALLTORCH_U,    SPR_ZWLTR, 8, 55,   R(20), R(16), NB | MF_NOGRAVITY);
    STATICPROP (MT_XZTWINEDTORCH_U,  S_ZD_TWINEDTORCH_U,  SPR_ZTWTR, 8, 117,  R(10), R(64), SOLID);
    STATICPROP (MT_XZFIREBULL_U,     S_ZD_FIREBULL_U,     SPR_ZFBUL, 7, 8043, R(20), R(80), SOLID);
    STATICPROP (MT_XZCAULDRON_U,     S_ZD_CAULDRON_U,     SPR_ZCDRN, 0, 8070, R(12), R(26), SOLID);
    STATICPROP (MT_XZCHANDELIER_U,   S_ZD_CHANDELIER_U,   SPR_ZCDLR, 3, 8063, R(20), R(60), CEIL);

    // Hanging chains (ceiling-mounted, one CHNS frame each).
    STATICPROP (MT_XZCHAINBIT32,      S_ZD_CHAINBIT32,      SPR_ZCHNS, 0, 8071, R(4), R(32), NB | MF_NOGRAVITY | MF_SPAWNCEILING);
    STATICPROP (MT_XZCHAINBIT64,      S_ZD_CHAINBIT64,      SPR_ZCHNS, 1, 8072, R(4), R(64), NB | MF_NOGRAVITY | MF_SPAWNCEILING);
    STATICPROP (MT_XZCHAINEND_HEART,  S_ZD_CHAINEND_HEART,  SPR_ZCHNS, 2, 8073, R(4), R(32), NB | MF_NOGRAVITY | MF_SPAWNCEILING);
    STATICPROP (MT_XZCHAINEND_HOOK1,  S_ZD_CHAINEND_HOOK1,  SPR_ZCHNS, 3, 8074, R(4), R(32), NB | MF_NOGRAVITY | MF_SPAWNCEILING);
    STATICPROP (MT_XZCHAINEND_HOOK2,  S_ZD_CHAINEND_HOOK2,  SPR_ZCHNS, 4, 8075, R(4), R(32), NB | MF_NOGRAVITY | MF_SPAWNCEILING);
    STATICPROP (MT_XZCHAINEND_SPIKE,  S_ZD_CHAINEND_SPIKE,  SPR_ZCHNS, 5, 8076, R(4), R(32), NB | MF_NOGRAVITY | MF_SPAWNCEILING);
    STATICPROP (MT_XZCHAINEND_SKULL,  S_ZD_CHAINEND_SKULL,  SPR_ZCHNS, 6, 8077, R(4), R(32), NB | MF_NOGRAVITY | MF_SPAWNCEILING);

    // Table clutter (10 non-blocking bits).
    STATICPROP (MT_XZTABLESHIT1,  S_ZD_TABLESHIT1,  SPR_ZTST1, 0, 8500, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT2,  S_ZD_TABLESHIT2,  SPR_ZTST2, 0, 8501, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT3,  S_ZD_TABLESHIT3,  SPR_ZTST3, 0, 8502, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT4,  S_ZD_TABLESHIT4,  SPR_ZTST4, 0, 8503, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT5,  S_ZD_TABLESHIT5,  SPR_ZTST5, 0, 8504, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT6,  S_ZD_TABLESHIT6,  SPR_ZTST6, 0, 8505, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT7,  S_ZD_TABLESHIT7,  SPR_ZTST7, 0, 8506, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT8,  S_ZD_TABLESHIT8,  SPR_ZTST8, 0, 8507, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT9,  S_ZD_TABLESHIT9,  SPR_ZTST9, 0, 8508, R(20), R(16), NB);
    STATICPROP (MT_XZTABLESHIT10, S_ZD_TABLESHIT10, SPR_ZTST0, 0, 8509, R(20), R(16), NB);

    // Bridge (simplified: static invisible solid platform; orbiting balls dropped).
    STATICPROP (MT_XZBRIDGE, S_ZD_BRIDGE, SPR_TNT1, 0, 118, R(32), R(2), SOLID | MF_NOGRAVITY);

    // ====================================================================
    // Animated light props (looping frame cycles).
    // ====================================================================
    ST (S_ZD_WALLTORCH1, SPR_ZWLTR, 32768|0, 5, NULL, S_ZD_WALLTORCH2);
    ST (S_ZD_WALLTORCH2, SPR_ZWLTR, 32768|1, 5, NULL, S_ZD_WALLTORCH3);
    ST (S_ZD_WALLTORCH3, SPR_ZWLTR, 32768|2, 5, NULL, S_ZD_WALLTORCH4);
    ST (S_ZD_WALLTORCH4, SPR_ZWLTR, 32768|3, 5, NULL, S_ZD_WALLTORCH5);
    ST (S_ZD_WALLTORCH5, SPR_ZWLTR, 32768|4, 5, NULL, S_ZD_WALLTORCH6);
    ST (S_ZD_WALLTORCH6, SPR_ZWLTR, 32768|5, 5, NULL, S_ZD_WALLTORCH7);
    ST (S_ZD_WALLTORCH7, SPR_ZWLTR, 32768|6, 5, NULL, S_ZD_WALLTORCH8);
    ST (S_ZD_WALLTORCH8, SPR_ZWLTR, 32768|7, 5, NULL, S_ZD_WALLTORCH1);
    PROP (MT_XZWALLTORCH, 54, S_ZD_WALLTORCH1, 1000, R(20), R(16), NB | MF_NOGRAVITY);

    ST (S_ZD_TWINEDTORCH1, SPR_ZTWTR, 0, 4, NULL, S_ZD_TWINEDTORCH2);
    ST (S_ZD_TWINEDTORCH2, SPR_ZTWTR, 1, 4, NULL, S_ZD_TWINEDTORCH3);
    ST (S_ZD_TWINEDTORCH3, SPR_ZTWTR, 2, 4, NULL, S_ZD_TWINEDTORCH4);
    ST (S_ZD_TWINEDTORCH4, SPR_ZTWTR, 3, 4, NULL, S_ZD_TWINEDTORCH5);
    ST (S_ZD_TWINEDTORCH5, SPR_ZTWTR, 4, 4, NULL, S_ZD_TWINEDTORCH6);
    ST (S_ZD_TWINEDTORCH6, SPR_ZTWTR, 5, 4, NULL, S_ZD_TWINEDTORCH7);
    ST (S_ZD_TWINEDTORCH7, SPR_ZTWTR, 6, 4, NULL, S_ZD_TWINEDTORCH8);
    ST (S_ZD_TWINEDTORCH8, SPR_ZTWTR, 7, 4, NULL, S_ZD_TWINEDTORCH1);
    PROP (MT_XZTWINEDTORCH, 116, S_ZD_TWINEDTORCH1, 1000, R(10), R(64), SOLID);

    ST (S_ZD_FIREBULL1, SPR_ZFBUL, 0, 4, NULL, S_ZD_FIREBULL2);
    ST (S_ZD_FIREBULL2, SPR_ZFBUL, 1, 4, NULL, S_ZD_FIREBULL3);
    ST (S_ZD_FIREBULL3, SPR_ZFBUL, 2, 4, NULL, S_ZD_FIREBULL4);
    ST (S_ZD_FIREBULL4, SPR_ZFBUL, 3, 4, NULL, S_ZD_FIREBULL5);
    ST (S_ZD_FIREBULL5, SPR_ZFBUL, 4, 4, NULL, S_ZD_FIREBULL6);
    ST (S_ZD_FIREBULL6, SPR_ZFBUL, 5, 4, NULL, S_ZD_FIREBULL7);
    ST (S_ZD_FIREBULL7, SPR_ZFBUL, 6, 4, NULL, S_ZD_FIREBULL1);
    PROP (MT_XZFIREBULL, 8042, S_ZD_FIREBULL1, 1000, R(20), R(80), SOLID);

    ST (S_ZD_CAULDRON1, SPR_ZCDRN, 32768|1, 4, NULL, S_ZD_CAULDRON2);
    ST (S_ZD_CAULDRON2, SPR_ZCDRN, 32768|2, 4, NULL, S_ZD_CAULDRON3);
    ST (S_ZD_CAULDRON3, SPR_ZCDRN, 32768|3, 4, NULL, S_ZD_CAULDRON4);
    ST (S_ZD_CAULDRON4, SPR_ZCDRN, 32768|4, 4, NULL, S_ZD_CAULDRON5);
    ST (S_ZD_CAULDRON5, SPR_ZCDRN, 32768|5, 4, NULL, S_ZD_CAULDRON6);
    ST (S_ZD_CAULDRON6, SPR_ZCDRN, 32768|6, 4, NULL, S_ZD_CAULDRON7);
    ST (S_ZD_CAULDRON7, SPR_ZCDRN, 32768|7, 4, NULL, S_ZD_CAULDRON1);
    PROP (MT_XZCAULDRON, 8069, S_ZD_CAULDRON1, 1000, R(12), R(26), SOLID);

    ST (S_ZD_BLUECANDLE1, SPR_ZCAND, 32768|0, 5, NULL, S_ZD_BLUECANDLE2);
    ST (S_ZD_BLUECANDLE2, SPR_ZCAND, 32768|1, 5, NULL, S_ZD_BLUECANDLE3);
    ST (S_ZD_BLUECANDLE3, SPR_ZCAND, 32768|2, 5, NULL, S_ZD_BLUECANDLE4);
    ST (S_ZD_BLUECANDLE4, SPR_ZCAND, 32768|3, 5, NULL, S_ZD_BLUECANDLE5);
    ST (S_ZD_BLUECANDLE5, SPR_ZCAND, 32768|4, 5, NULL, S_ZD_BLUECANDLE1);
    PROP (MT_XZBLUECANDLE, 8066, S_ZD_BLUECANDLE1, 1000, R(20), R(16), NB);

    ST (S_ZD_BRASSTORCH1,  SPR_ZBRTR, 0,  4, NULL, S_ZD_BRASSTORCH2);
    ST (S_ZD_BRASSTORCH2,  SPR_ZBRTR, 1,  4, NULL, S_ZD_BRASSTORCH3);
    ST (S_ZD_BRASSTORCH3,  SPR_ZBRTR, 2,  4, NULL, S_ZD_BRASSTORCH4);
    ST (S_ZD_BRASSTORCH4,  SPR_ZBRTR, 3,  4, NULL, S_ZD_BRASSTORCH5);
    ST (S_ZD_BRASSTORCH5,  SPR_ZBRTR, 4,  4, NULL, S_ZD_BRASSTORCH6);
    ST (S_ZD_BRASSTORCH6,  SPR_ZBRTR, 5,  4, NULL, S_ZD_BRASSTORCH7);
    ST (S_ZD_BRASSTORCH7,  SPR_ZBRTR, 6,  4, NULL, S_ZD_BRASSTORCH8);
    ST (S_ZD_BRASSTORCH8,  SPR_ZBRTR, 7,  4, NULL, S_ZD_BRASSTORCH9);
    ST (S_ZD_BRASSTORCH9,  SPR_ZBRTR, 8,  4, NULL, S_ZD_BRASSTORCH10);
    ST (S_ZD_BRASSTORCH10, SPR_ZBRTR, 9,  4, NULL, S_ZD_BRASSTORCH11);
    ST (S_ZD_BRASSTORCH11, SPR_ZBRTR, 10, 4, NULL, S_ZD_BRASSTORCH12);
    ST (S_ZD_BRASSTORCH12, SPR_ZBRTR, 11, 4, NULL, S_ZD_BRASSTORCH13);
    ST (S_ZD_BRASSTORCH13, SPR_ZBRTR, 12, 4, NULL, S_ZD_BRASSTORCH1);
    PROP (MT_XZBRASSTORCH, 8061, S_ZD_BRASSTORCH1, 1000, R(6), R(35), SOLID);

    ST (S_ZD_FIRETHING1, SPR_ZFSKL, 0, 4, NULL, S_ZD_FIRETHING2);
    ST (S_ZD_FIRETHING2, SPR_ZFSKL, 1, 3, NULL, S_ZD_FIRETHING3);
    ST (S_ZD_FIRETHING3, SPR_ZFSKL, 2, 4, NULL, S_ZD_FIRETHING4);
    ST (S_ZD_FIRETHING4, SPR_ZFSKL, 3, 3, NULL, S_ZD_FIRETHING5);
    ST (S_ZD_FIRETHING5, SPR_ZFSKL, 4, 4, NULL, S_ZD_FIRETHING6);
    ST (S_ZD_FIRETHING6, SPR_ZFSKL, 5, 3, NULL, S_ZD_FIRETHING7);
    ST (S_ZD_FIRETHING7, SPR_ZFSKL, 6, 4, NULL, S_ZD_FIRETHING8);
    ST (S_ZD_FIRETHING8, SPR_ZFSKL, 7, 3, NULL, S_ZD_FIRETHING9);
    ST (S_ZD_FIRETHING9, SPR_ZFSKL, 8, 4, NULL, S_ZD_FIRETHING1);
    PROP (MT_XZFIRETHING, 8060, S_ZD_FIRETHING1, 1000, R(5), R(10), SOLID);

    ST (S_ZD_CHANDELIER1, SPR_ZCDLR, 0, 4, NULL, S_ZD_CHANDELIER2);
    ST (S_ZD_CHANDELIER2, SPR_ZCDLR, 1, 4, NULL, S_ZD_CHANDELIER3);
    ST (S_ZD_CHANDELIER3, SPR_ZCDLR, 2, 4, NULL, S_ZD_CHANDELIER1);
    PROP (MT_XZCHANDELIER, 17, S_ZD_CHANDELIER1, 1000, R(20), R(60), CEIL);

    ST (S_ZD_CANDLE1, SPR_ZCNDL, 32768|0, 4, NULL, S_ZD_CANDLE2);
    ST (S_ZD_CANDLE2, SPR_ZCNDL, 32768|1, 4, NULL, S_ZD_CANDLE3);
    ST (S_ZD_CANDLE3, SPR_ZCNDL, 32768|2, 4, NULL, S_ZD_CANDLE1);
    PROP (MT_XZCANDLE, 119, S_ZD_CANDLE1, 1000, R(20), R(16), NB | MF_NOGRAVITY);

    // Tele-smoke (26-frame puff, looping).  MF_TRANSLUCENT dropped (undefined).
    {
	int i;
	for (i = 0; i < 26; i++)
	    ST (S_ZD_TELESMOKE1 + i, SPR_ZTSMK, i, (i & 1) ? 3 : 4,
		NULL, (i == 25) ? S_ZD_TELESMOKE1 : (S_ZD_TELESMOKE1 + i + 1));
    }
    PROP (MT_XZTELESMOKE, 140, S_ZD_TELESMOKE1, 1000, R(20), R(16), NB | MF_NOGRAVITY);

    // Simplified flames (spawn visible + loop; ACS hide/self-extinguish dropped).
    ST (S_ZD_FLAMESMALL1, SPR_ZFFSM, 32768|0, 3, NULL, S_ZD_FLAMESMALL2);
    ST (S_ZD_FLAMESMALL2, SPR_ZFFSM, 32768|1, 3, NULL, S_ZD_FLAMESMALL3);
    ST (S_ZD_FLAMESMALL3, SPR_ZFFSM, 32768|2, 3, NULL, S_ZD_FLAMESMALL4);
    ST (S_ZD_FLAMESMALL4, SPR_ZFFSM, 32768|3, 3, NULL, S_ZD_FLAMESMALL5);
    ST (S_ZD_FLAMESMALL5, SPR_ZFFSM, 32768|4, 3, NULL, S_ZD_FLAMESMALL1);
    PROP (MT_XZFLAMESMALL, 10501, S_ZD_FLAMESMALL1, 1000, R(20), R(16), 0);

    {
	int i;
	for (i = 0; i < 16; i++)
	    ST (S_ZD_FLAMELARGE1 + i, SPR_ZFFLG, 32768 | i, 4,
		NULL, (i == 15) ? S_ZD_FLAMELARGE1 : (S_ZD_FLAMELARGE1 + i + 1));
    }
    PROP (MT_XZFLAMELARGE, 10503, S_ZD_FLAMELARGE1, 1000, R(20), R(16), 0);

    // ====================================================================
    // Lynched corpse that drips blood + its sub-actors.
    // ====================================================================
    ST (S_ZD_LYNCHEDNOHEART, SPR_ZCPS5, 0, 140, (actionf_p1)A_CorpseBloodDrip, S_ZD_LYNCHEDNOHEART);
    PROP (MT_XZLYNCHEDNOHEART, 109, S_ZD_LYNCHEDNOHEART, 1000, R(10), R(100), SOLID | CEIL);

    // Blood drip: falls (low gravity), splashes on landing.
    ST (S_ZD_BLOODDRIP,    SPR_ZBDRP, 0, -1, NULL, S_NULL);
    ST (S_ZD_BLOODDRIP_X1, SPR_ZBDSH, 0, 3, NULL, S_ZD_BLOODDRIP_X2);
    ST (S_ZD_BLOODDRIP_X2, SPR_ZBDSH, 1, 3, NULL, S_ZD_BLOODDRIP_X3);
    ST (S_ZD_BLOODDRIP_X3, SPR_ZBDSH, 2, 2, NULL, S_ZD_BLOODDRIP_X4);
    ST (S_ZD_BLOODDRIP_X4, SPR_ZBDSH, 3, 2, NULL, S_NULL);
    m = &mobjinfo[MT_XZCORPSEBLOODDRIP];
    m->doomednum = -1; m->spawnstate = S_ZD_BLOODDRIP; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_BLOODDRIP_X1; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = FRACUNIT; m->height = R(4); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB | MF_MISSILE | MF_DROPOFF; m->flags2 = MF2_LOGRAV; m->raisestate = S_NULL;

    // ====================================================================
    // Leaf spawner (invisible) + the two fluttering leaves it emits.
    // ====================================================================
    ST (S_ZD_LEAFSPAWNER, SPR_TNT1, 0, 20, (actionf_p1)A_LeafSpawn, S_ZD_LEAFSPAWNER);
    m = &mobjinfo[MT_XZLEAFSPAWNER];
    m->doomednum = 113; m->spawnstate = S_ZD_LEAFSPAWNER; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(20); m->height = R(16); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB | MF_NOSECTOR | MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    // Leaf 1 -- flutter loop, A_LeafThrust lifts, death A_LeafCheck re-gusts/fades.
    ST (S_ZD_LEAF1_1,  SPR_ZLEF1, 0, 4, NULL,                     S_ZD_LEAF1_2);
    ST (S_ZD_LEAF1_2,  SPR_ZLEF1, 1, 4, NULL,                     S_ZD_LEAF1_3);
    ST (S_ZD_LEAF1_3,  SPR_ZLEF1, 2, 4, NULL,                     S_ZD_LEAF1_4);
    ST (S_ZD_LEAF1_4,  SPR_ZLEF1, 3, 4, (actionf_p1)A_LeafThrust, S_ZD_LEAF1_5);
    ST (S_ZD_LEAF1_5,  SPR_ZLEF1, 4, 4, NULL,                     S_ZD_LEAF1_6);
    ST (S_ZD_LEAF1_6,  SPR_ZLEF1, 5, 4, NULL,                     S_ZD_LEAF1_7);
    ST (S_ZD_LEAF1_7,  SPR_ZLEF1, 6, 4, NULL,                     S_ZD_LEAF1_8);
    ST (S_ZD_LEAF1_8,  SPR_ZLEF1, 7, 4, (actionf_p1)A_LeafThrust, S_ZD_LEAF1_9);
    ST (S_ZD_LEAF1_9,  SPR_ZLEF1, 8, 4, NULL,                     S_ZD_LEAF1_10);
    ST (S_ZD_LEAF1_10, SPR_ZLEF1, 0, 4, NULL,                     S_ZD_LEAF1_11);
    ST (S_ZD_LEAF1_11, SPR_ZLEF1, 1, 4, NULL,                     S_ZD_LEAF1_12);
    ST (S_ZD_LEAF1_12, SPR_ZLEF1, 2, 4, (actionf_p1)A_LeafThrust, S_ZD_LEAF1_13);
    ST (S_ZD_LEAF1_13, SPR_ZLEF1, 3, 4, NULL,                     S_ZD_LEAF1_14);
    ST (S_ZD_LEAF1_14, SPR_ZLEF1, 4, 4, NULL,                     S_ZD_LEAF1_15);
    ST (S_ZD_LEAF1_15, SPR_ZLEF1, 5, 4, NULL,                     S_ZD_LEAF1_16);
    ST (S_ZD_LEAF1_16, SPR_ZLEF1, 6, 4, (actionf_p1)A_LeafThrust, S_ZD_LEAF1_17);
    ST (S_ZD_LEAF1_17, SPR_ZLEF1, 7, 4, NULL,                     S_ZD_LEAF1_18);
    ST (S_ZD_LEAF1_18, SPR_ZLEF1, 8, 4, NULL,                     S_NULL);
    ST (S_ZD_LEAF_X1,  SPR_ZLEF3, 3, 10, (actionf_p1)A_LeafCheck, S_ZD_LEAF_X1);

    ST (S_ZD_LEAF2_1,  SPR_ZLEF2, 0, 4, NULL,                     S_ZD_LEAF2_2);
    ST (S_ZD_LEAF2_2,  SPR_ZLEF2, 1, 4, NULL,                     S_ZD_LEAF2_3);
    ST (S_ZD_LEAF2_3,  SPR_ZLEF2, 2, 4, NULL,                     S_ZD_LEAF2_4);
    ST (S_ZD_LEAF2_4,  SPR_ZLEF2, 3, 4, (actionf_p1)A_LeafThrust, S_ZD_LEAF2_5);
    ST (S_ZD_LEAF2_5,  SPR_ZLEF2, 4, 4, NULL,                     S_ZD_LEAF2_6);
    ST (S_ZD_LEAF2_6,  SPR_ZLEF2, 5, 4, NULL,                     S_ZD_LEAF2_7);
    ST (S_ZD_LEAF2_7,  SPR_ZLEF2, 6, 4, NULL,                     S_ZD_LEAF2_8);
    ST (S_ZD_LEAF2_8,  SPR_ZLEF2, 7, 4, (actionf_p1)A_LeafThrust, S_ZD_LEAF2_9);
    ST (S_ZD_LEAF2_9,  SPR_ZLEF2, 8, 4, NULL,                     S_ZD_LEAF1_10);	// merge into leaf1 tail
    m = &mobjinfo[MT_XZLEAF1];
    m->doomednum = -1; m->spawnstate = S_ZD_LEAF1_1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_LEAF_X1; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(2); m->height = R(4); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB | MF_MISSILE; m->flags2 = MF2_LOGRAV; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XZLEAF2];		// must sit immediately after MT_XZLEAF1
    m->doomednum = -1; m->spawnstate = S_ZD_LEAF2_1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_LEAF_X1; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(2); m->height = R(4); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB | MF_MISSILE; m->flags2 = MF2_LOGRAV; m->raisestate = S_NULL;

    // ====================================================================
    // Thrown-rock debris x3 (Hexen MT_ROCK1..3; ROCK sprite aliased "ZRCK").
    // ====================================================================
    ST (S_ZD_ROCKDEBRIS1,   SPR_ZROCK, 0, 20, NULL, S_ZD_ROCKDEBRIS1);
    ST (S_ZD_ROCKDEBRIS1_D, SPR_ZROCK, 0, 10, NULL, S_NULL);
    ST (S_ZD_ROCKDEBRIS2,   SPR_ZROCK, 1, 20, NULL, S_ZD_ROCKDEBRIS2);
    ST (S_ZD_ROCKDEBRIS2_D, SPR_ZROCK, 1, 10, NULL, S_NULL);
    ST (S_ZD_ROCKDEBRIS3,   SPR_ZROCK, 2, 20, NULL, S_ZD_ROCKDEBRIS3);
    ST (S_ZD_ROCKDEBRIS3_D, SPR_ZROCK, 2, 10, NULL, S_NULL);
    m = &mobjinfo[MT_XZROCKDEBRIS1];
    m->doomednum = -1; m->spawnstate = S_ZD_ROCKDEBRIS1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_ROCKDEBRIS1_D; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(20); m->height = R(16); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB | MF_DROPOFF | MF_MISSILE; m->flags2 = 0; m->raisestate = S_NULL;
    m = &mobjinfo[MT_XZROCKDEBRIS2];
    m->doomednum = -1; m->spawnstate = S_ZD_ROCKDEBRIS2; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_ROCKDEBRIS2_D; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(20); m->height = R(16); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB | MF_DROPOFF | MF_MISSILE; m->flags2 = 0; m->raisestate = S_NULL;
    m = &mobjinfo[MT_XZROCKDEBRIS3];
    m->doomednum = -1; m->spawnstate = S_ZD_ROCKDEBRIS3; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_ROCKDEBRIS3_D; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(20); m->height = R(16); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB | MF_DROPOFF | MF_MISSILE; m->flags2 = 0; m->raisestate = S_NULL;

    // ====================================================================
    // DESTRUCTIBLES
    // ====================================================================

    // ---- Corpse (sitting): shootable, bursts into gib bits + skull ----
    ST (S_ZD_CORPSESITTING,   SPR_ZCPS6, 0, -1, NULL,                      S_NULL);
    ST (S_ZD_CORPSESITTING_X, SPR_ZCPS6, 0,  1, (actionf_p1)A_CorpseExplode, S_NULL);
    m = &mobjinfo[MT_XZCORPSESITTING];
    m->doomednum = 110; m->spawnstate = S_ZD_CORPSESITTING; m->spawnhealth = 30;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_CORPSESITTING_X; m->xdeathstate = S_NULL; m->deathsound = sfx_slop;
    m->speed = 0; m->radius = R(15); m->height = R(35); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD; m->flags2 = 0; m->raisestate = S_NULL;

    // Corpse bits (4 gib sprites, spawn-only).
    ST (S_ZD_CORPSEBIT1, SPR_ZCPB1, 0, -1, NULL, S_NULL);
    ST (S_ZD_CORPSEBIT2, SPR_ZCPB2, 0, -1, NULL, S_NULL);
    ST (S_ZD_CORPSEBIT3, SPR_ZCPB3, 0, -1, NULL, S_NULL);
    ST (S_ZD_CORPSEBIT4, SPR_ZCPB4, 0, -1, NULL, S_NULL);
    m = &mobjinfo[MT_XZCORPSEBIT];
    m->doomednum = -1; m->spawnstate = S_ZD_CORPSEBIT1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(5); m->height = R(5); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = NB; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Pottery x3: shootable, shatter into pottery bits ----
    ST (S_ZD_POTTERY_EXPLODE, SPR_ZPOT1, 0, 0, (actionf_p1)A_PotteryExplode, S_NULL);
    ST (S_ZD_POTTERY1, SPR_ZPOT1, 0, -1, NULL, S_NULL);
    ST (S_ZD_POTTERY2, SPR_ZPOT2, 0, -1, NULL, S_NULL);
    ST (S_ZD_POTTERY3, SPR_ZPOT3, 0, -1, NULL, S_NULL);
    m = &mobjinfo[MT_XZPOTTERY1];
    m->doomednum = 104; m->spawnstate = S_ZD_POTTERY1; m->spawnhealth = 15;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_POTTERY_EXPLODE; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(10); m->height = R(32); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD | MF_DROPOFF; m->flags2 = 0; m->raisestate = S_NULL;
    m = &mobjinfo[MT_XZPOTTERY2];
    m->doomednum = 105; m->spawnstate = S_ZD_POTTERY2; m->spawnhealth = 15;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_POTTERY_EXPLODE; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(10); m->height = R(25); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD | MF_DROPOFF; m->flags2 = 0; m->raisestate = S_NULL;
    m = &mobjinfo[MT_XZPOTTERY3];
    m->doomednum = 106; m->spawnstate = S_ZD_POTTERY3; m->spawnhealth = 15;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_POTTERY_EXPLODE; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(15); m->height = R(25); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD | MF_DROPOFF; m->flags2 = 0; m->raisestate = S_NULL;

    // Pottery bits: spawnstates 1..5 (whole shards), deathstate EX0 -> chooses
    // one of five broken-shard frames (EX1..EX5) that persist as debris.
    ST (S_ZD_POTTERYBIT1, SPR_ZPBIT, 0, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT2, SPR_ZPBIT, 1, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT3, SPR_ZPBIT, 2, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT4, SPR_ZPBIT, 3, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT5, SPR_ZPBIT, 4, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT_EX0, SPR_ZPBIT, 5, 0, (actionf_p1)A_PotteryChooseBit, S_NULL);
    ST (S_ZD_POTTERYBIT_EX1, SPR_ZPBIT, 5, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT_EX2, SPR_ZPBIT, 6, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT_EX3, SPR_ZPBIT, 7, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT_EX4, SPR_ZPBIT, 8, -1, NULL, S_NULL);
    ST (S_ZD_POTTERYBIT_EX5, SPR_ZPBIT, 9, -1, NULL, S_NULL);
    m = &mobjinfo[MT_XZPOTTERYBIT];
    m->doomednum = -1; m->spawnstate = S_ZD_POTTERYBIT1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_POTTERYBIT_EX0; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(5); m->height = R(5); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_MISSILE; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Suit of armor: shootable, bursts into 10 armor chunks ----
    ST (S_ZD_SUITOFARMOR,   SPR_ZSUIT, 0, -1, NULL, S_NULL);
    ST (S_ZD_SUITOFARMOR_X, SPR_ZSUIT, 0,  1, (actionf_p1)A_SoAExplode, S_NULL);
    m = &mobjinfo[MT_XZSUITOFARMOR];
    m->doomednum = 8064; m->spawnstate = S_ZD_SUITOFARMOR; m->spawnhealth = 60;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_SUITOFARMOR_X; m->xdeathstate = S_NULL; m->deathsound = sfx_barexp;
    m->speed = 0; m->radius = R(16); m->height = R(72); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD; m->flags2 = 0; m->raisestate = S_NULL;

    // Armor chunks (10 numbered frames, spawn-only; A_SoAExplode picks +0..+9).
    ST (S_ZD_ARMORCHUNK1,  SPR_ZSUIT, 1,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK2,  SPR_ZSUIT, 2,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK3,  SPR_ZSUIT, 3,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK4,  SPR_ZSUIT, 4,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK5,  SPR_ZSUIT, 5,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK6,  SPR_ZSUIT, 6,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK7,  SPR_ZSUIT, 7,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK8,  SPR_ZSUIT, 8,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK9,  SPR_ZSUIT, 9,  -1, NULL, S_NULL);
    ST (S_ZD_ARMORCHUNK10, SPR_ZSUIT, 10, -1, NULL, S_NULL);
    m = &mobjinfo[MT_XZARMORCHUNK];
    m->doomednum = -1; m->spawnstate = S_ZD_ARMORCHUNK1; m->spawnhealth = 1000;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_NULL; m->xdeathstate = S_NULL; m->deathsound = sfx_None;
    m->speed = 0; m->radius = R(4); m->height = R(8); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = 0; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Destructible tree: shootable, topples through a 6-frame break ----
    ST (S_ZD_TREEDES1,   SPR_ZTRDT, 0, -1, NULL,                 S_NULL);
    ST (S_ZD_TREEDES_D1, SPR_ZTRDT, 1, 5, NULL,                  S_ZD_TREEDES_D2);
    ST (S_ZD_TREEDES_D2, SPR_ZTRDT, 2, 5, (actionf_p1)A_Scream,  S_ZD_TREEDES_D3);
    ST (S_ZD_TREEDES_D3, SPR_ZTRDT, 3, 5, NULL,                  S_ZD_TREEDES_D4);
    ST (S_ZD_TREEDES_D4, SPR_ZTRDT, 4, 5, NULL,                  S_ZD_TREEDES_D5);
    ST (S_ZD_TREEDES_D5, SPR_ZTRDT, 5, 5, NULL,                  S_ZD_TREEDES_D6);
    ST (S_ZD_TREEDES_D6, SPR_ZTRDT, 6, -1, NULL,                 S_NULL);
    m = &mobjinfo[MT_XZTREEDESTRUCTIBLE];
    m->doomednum = 8062; m->spawnstate = S_ZD_TREEDES1; m->spawnhealth = 70;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_TREEDES_D1; m->xdeathstate = S_NULL; m->deathsound = sfx_barexp;
    m->speed = 0; m->radius = R(15); m->height = R(180); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Xmas tree: shootable; death -> A_TreeDeath -> burn/explode (melee) ----
    ST (S_ZD_XMASTREE,     SPR_ZXMAS, 0, -1, NULL,                    S_NULL);
    ST (S_ZD_XMASTREE_DIE, SPR_ZXMAS, 0,  4, (actionf_p1)A_TreeDeath, S_ZD_XMASTREE);
    ST (S_ZD_XMASTREE_X1,  SPR_ZXMAS, 32768|1, 6, NULL,                 S_ZD_XMASTREE_X2);
    ST (S_ZD_XMASTREE_X2,  SPR_ZXMAS, 32768|2, 6, (actionf_p1)A_Scream, S_ZD_XMASTREE_X3);
    ST (S_ZD_XMASTREE_X3,  SPR_ZXMAS, 32768|3, 5, NULL,                 S_ZD_XMASTREE_X4);
    ST (S_ZD_XMASTREE_X4,  SPR_ZXMAS, 32768|4, 5, (actionf_p1)A_Explode,S_ZD_XMASTREE_X5);
    ST (S_ZD_XMASTREE_X5,  SPR_ZXMAS, 32768|5, 5, NULL,                 S_ZD_XMASTREE_X6);
    ST (S_ZD_XMASTREE_X6,  SPR_ZXMAS, 32768|6, 4, NULL,                 S_ZD_XMASTREE_X7);
    ST (S_ZD_XMASTREE_X7,  SPR_ZXMAS, 7, 5, NULL,                       S_ZD_XMASTREE_X8);
    ST (S_ZD_XMASTREE_X8,  SPR_ZXMAS, 8, 4, (actionf_p1)A_Fall,         S_ZD_XMASTREE_X9);
    ST (S_ZD_XMASTREE_X9,  SPR_ZXMAS, 9, 4, NULL,                       S_ZD_XMASTREE_X10);
    ST (S_ZD_XMASTREE_X10, SPR_ZXMAS, 10, -1, NULL,                     S_NULL);
    m = &mobjinfo[MT_XZXMASTREE];
    m->doomednum = 8068; m->spawnstate = S_ZD_XMASTREE; m->spawnhealth = 20;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_ZD_XMASTREE_X1; m->missilestate = S_NULL;
    m->deathstate = S_ZD_XMASTREE_DIE; m->xdeathstate = S_NULL; m->deathsound = sfx_barexp;
    m->speed = 0; m->radius = R(11); m->height = R(130); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Shrubs x2: shootable; death -> A_TreeDeath -> burn (melee) ----
    ST (S_ZD_SHRUB1,     SPR_ZSHB1, 0, -1, NULL,                    S_NULL);
    ST (S_ZD_SHRUB1_DIE, SPR_ZSHB1, 0,  1, (actionf_p1)A_TreeDeath, S_ZD_SHRUB1);
    ST (S_ZD_SHRUB1_X1,  SPR_ZSHB1, 32768|1, 7, NULL,                 S_ZD_SHRUB1_X2);
    ST (S_ZD_SHRUB1_X2,  SPR_ZSHB1, 32768|2, 6, (actionf_p1)A_Scream, S_ZD_SHRUB1_X3);
    ST (S_ZD_SHRUB1_X3,  SPR_ZSHB1, 32768|3, 5, NULL,                 S_NULL);
    m = &mobjinfo[MT_XZSHRUB1];
    m->doomednum = 8101; m->spawnstate = S_ZD_SHRUB1; m->spawnhealth = 20;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_ZD_SHRUB1_X1; m->missilestate = S_NULL;
    m->deathstate = S_ZD_SHRUB1_DIE; m->xdeathstate = S_NULL; m->deathsound = sfx_barexp;
    m->speed = 0; m->radius = R(8); m->height = R(24); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD; m->flags2 = 0; m->raisestate = S_NULL;

    ST (S_ZD_SHRUB2,     SPR_ZSHB2, 0, -1, NULL,                    S_NULL);
    ST (S_ZD_SHRUB2_DIE, SPR_ZSHB2, 0,  1, (actionf_p1)A_TreeDeath, S_ZD_SHRUB2);
    ST (S_ZD_SHRUB2_X1,  SPR_ZSHB2, 32768|1, 7, NULL,                  S_ZD_SHRUB2_X2);
    ST (S_ZD_SHRUB2_X2,  SPR_ZSHB2, 32768|2, 6, (actionf_p1)A_Scream,  S_ZD_SHRUB2_X3);
    ST (S_ZD_SHRUB2_X3,  SPR_ZSHB2, 32768|3, 5, (actionf_p1)A_Explode, S_ZD_SHRUB2_X4);
    ST (S_ZD_SHRUB2_X4,  SPR_ZSHB2, 32768|4, 5, NULL,                  S_NULL);
    m = &mobjinfo[MT_XZSHRUB2];
    m->doomednum = 8102; m->spawnstate = S_ZD_SHRUB2; m->spawnhealth = 10;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_ZD_SHRUB2_X1; m->missilestate = S_NULL;
    m->deathstate = S_ZD_SHRUB2_DIE; m->xdeathstate = S_NULL; m->deathsound = sfx_barexp;
    m->speed = 0; m->radius = R(16); m->height = R(40); m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Bell: shootable; when rung it swings through a long arc, then re-arms ----
    ST (S_ZD_BELL, SPR_ZBBLL, 5, -1, NULL, S_NULL);
    {
	// The 47-frame swing (crispy S_ZBELL_X1..X47); frame pattern + Scream cues.
	static const signed char bframe[47] = {
	    0,1,2,3,2,1,0,4,5,6,5,4,0,1,2,3,2,1,0,4,5,6,5,4,0,1,2,3,2,1,0,4,5,6,5,4,
	    0,1,2,1,0,4,0,1,0,4,0
	};
	static const unsigned char btics[47] = {
	    4,4,4,5,4,4,3,4,5,6,5,4,4,5,5,6,5,5,4,5,5,7,5,5,5,6,6,7,6,6,5,6,6,7,6,6,
	    6,6,6,7,8,12,10,12,12,14,1
	};
	int i;
	for (i = 0; i < 47; i++)
	{
	    actionf_p1 act = NULL;
	    if (i == 0)  act = (actionf_p1)A_BellReset1;
	    else if (i == 46) act = (actionf_p1)A_BellReset2;
	    else if (i == 3 || i == 9 || i == 15 || i == 21 || i == 27 || i == 33)
		act = (actionf_p1)A_Scream;
	    ST (S_ZD_BELL_X1 + i, SPR_ZBBLL, bframe[i], btics[i], act,
		(i == 46) ? S_ZD_BELL : (S_ZD_BELL_X1 + i + 1));
	}
    }
    m = &mobjinfo[MT_XZBELL];
    m->doomednum = 8065; m->spawnstate = S_ZD_BELL; m->spawnhealth = 5;
    m->seestate = S_NULL; m->seesound = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL; m->painchance = 0;
    m->painsound = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate = S_ZD_BELL_X1; m->xdeathstate = S_NULL; m->deathsound = sfx_metal;
    m->speed = 0; m->radius = R(56); m->height = R(120); m->mass = 0x7fffffff;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD | MF_NOGRAVITY | MF_SPAWNCEILING;
    m->flags2 = 0; m->raisestate = S_NULL;
}

#undef R
#undef SOLID
#undef CEIL
#undef NB
