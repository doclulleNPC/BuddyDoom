// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Additive STRIFE MONSTERS + BOSSES + their attack/projectile actors in the
//	DOOM engine.  Ported from strife-ve's strife/info.c (states[] + mobjinfo[])
//	and strife/p_enemy.c (the A_* codepointers -- LOGIC ported, re-expressed in
//	BuddyDoom's P_* API).  Same additive mechanism as heretic.c / hexen_mon.c /
//	strife_deco.c: Strife_Mon_Init() fills the runtime states[]/mobjinfo[] slots
//	reserved at the tail of statenum_t/mobjtype_t (info.h via strife_*.inc), and
//	sets mobjinfo[MT_S_*].doomednum so P_StrifeThingType resolves map things.
//
//	Naming transform (STRIFE_PORT_GUIDE.md): MT_XXX->MT_S_XXX, S_XXX->S_S_XXX,
//	SPR_XXX->SPR_S_XXX, sfx_xxx->sfx_s_xxx.  Frame values carry the full-bright
//	bit as the raw value 32768+n exactly as in strife-ve.
//
//	Coverage (this file):
//	  * every Strife MF_COUNTKILL actor: Acolytes (GUARD1-8/SHADOWGUARD/PGUARD
//	    Templar), Reaver, Crusader, Sentinel, Stalker, Inquisitor, Rebels +
//	    leaders (Macil RLEADER/RLEADER2), peasants, beggars, rat, plus the
//	    "shootable townspeople" the decorations installer skipped: merchants
//	    (SHOPKEEPER_*), Zombie/ZombieSpawner/Becoming, KneelingGuy, tanks
//	    (HUGE_TANK_1-3 / TANK_4-6), MT_RAT (id 85), MT_TURRET (id 27).
//	  * bosses: Programmer (+ProgrammerBase), Bishop, Loremaster (MT_PRIEST),
//	    Oracle, Entity + Sub-Entity, Nest/Pod, FieldGuard.
//	  * spectral enemies: Spectres A-E (alien spectres) + SpectreHead/Node gibs.
//	  * every projectile/effect those fire or spawn: Crusader missile+flame
//	    (C_MISSILE/C_FLAME), Sentinel plasma (L_LASER/R_LASER), Bishop seeker
//	    (SEEKMISSILE), Inquisitor grenade+arm (INQGRENADE/INQARM), hookshot +
//	    chainshot (HOOKSHOT/CHAINSHOT), the whole Sigil ground/zap/shot FX chain
//	    (SIGIL_A_GROUND / SIGIL_A_ZAP_LEFT/RIGHT / SIGIL_C_SHOT / SIGIL_SD_SHOT /
//	    SIGIL_SE_SHOT / SIGIL_E_OFFSHOOT / SIGIL_TRAIL), missile smoke, and the
//	    Programmer base.
//
//	The decorations already filled by strife_deco.c (101 actors) are NOT refilled
//	here.  Pure pickups (MF_SPECIAL, items installer) and the *player* weapon
//	projectiles (owned by the weapon installer) are excluded -- EXCEPT projectiles
//	a MONSTER also fires, which are filled here (see report notes).
//
//	Flag translation (STRIFE_PORT_GUIDE.md): MF_ALLY->MF_FRIEND; MF_MVIS /
//	MF_SPECTRAL / MF_COLORSWAP1-3 / MF_NODIALOG dropped (no engine equivalent);
//	all other MF_* kept verbatim.  Strife crashstate/flags2 dropped; raisestate=0.
//
//	Codepointers reused from the engine (declared extern): A_Look, A_Chase,
//	A_FaceTarget, A_Fall, A_Pain, A_Scream, A_XScream.  A_FriendLook is mapped to
//	A_Look (ally idle/wander logic simplified).  All other Strife A_* are ported
//	as static functions below.  Dropped-to-NULL (no engine support; actor still
//	spawns/moves/attacks/dies) with a TODO: A_BodyParts (ludicrous-gibs; would
//	need MEAT/JUNK chunk frames the deco installer left unfilled), A_ActiveSound
//	(ambient loops), A_AcolyteSpecial (quest token), A_ClearForceField (line
//	special 148), A_Listen (alarm), A_HideZombie / A_MerchantPain (shop-door
//	dialog), A_SetTLOptions (MTF translucency), A_ZombieInSpecialSector (nukage
//	insta-kill).
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "r_state.h"
#include "s_sound.h"
#include "sounds.h"
#include "info.h"
#include "tables.h"
#include "m_fixed.h"
#include "p_mobj.h"
#include "r_defs.h"	// (known gotcha: pulls in struct defs info.h leans on)
#include "strife.h"	// Strife_Available -- gate the console name lookup

extern state_t   *states;
extern mobjinfo_t *mobjinfo;

// ---------------------------------------------------------------------------
// Engine action codepointers + helpers reused verbatim (no public header).
// ---------------------------------------------------------------------------
extern void	A_Look (mobj_t*);
extern void	A_Chase (mobj_t*);
extern void	A_FaceTarget (mobj_t*);
extern void	A_Fall (mobj_t*);
extern void	A_Pain (mobj_t*);
extern void	A_Scream (mobj_t*);
extern void	A_XScream (mobj_t*);

extern boolean	P_CheckMeleeRange (mobj_t*);
extern boolean	P_CheckMissileRange (mobj_t*);
extern boolean	P_LookForPlayers (mobj_t*, boolean allaround);
extern void	P_NewChaseDir (mobj_t*);
extern boolean	P_Move (mobj_t*);
extern boolean	P_TryMove (mobj_t*, fixed_t x, fixed_t y);
extern void	P_ExplodeMissile (mobj_t*);
extern void	F_StartFinale (void);
extern void	G_ExitLevel (void);
extern int	EV_DoFloor (line_t* line, floor_e floortype);
extern int	EV_DoDoor (line_t* line, vldoor_e type);

// TRACEANGLE: Strife widened the revenant tracer turn rate (p_enemy.c).
#define TRACEANGLE	0xE000000

// Entity/Sub-Entity spawn anchor (Strife globals; the Sub-Entities must appear
// where the Entity rose, not in the void -- serialized via the actor spawnpoint).
static fixed_t entity_pos_x, entity_pos_y, entity_pos_z;

// ---------------------------------------------------------------------------
// Strife-specific movement / spawn helpers (strife/p_mobj.c + p_enemy.c),
// re-expressed with BuddyDoom's API.  static -> no clash with the engine.
// ---------------------------------------------------------------------------

// P_ThrustMobj: add momentum along a BAM angle.
static void SM_Thrust (mobj_t* actor, angle_t angle, fixed_t force)
{
    angle_t an = angle >> ANGLETOFINESHIFT;
    actor->momx += FixedMul (finecosine[an], force);
    actor->momy += FixedMul (finesine[an],   force);
}

// P_CheckMissileSpawn: nudge a fresh missile forward, explode if it's already
// embedded in something.
static void SM_CheckMissileSpawn (mobj_t* th)
{
    th->x += (th->momx >> 1);
    th->y += (th->momy >> 1);
    th->z += (th->momz >> 1);
    if (!P_TryMove (th, th->x, th->y))
	P_ExplodeMissile (th);
}

// P_SpawnFacingMissile: fire `type` from source along SOURCE'S facing angle
// (not aimed straight at the target), with vertical slope toward the target.
static mobj_t* SM_SpawnFacingMissile (mobj_t* source, mobj_t* target, mobjtype_t type)
{
    mobj_t*	th;
    angle_t	an;
    fixed_t	dist;

    th = P_SpawnMobj (source->x, source->y, source->z + 32*FRACUNIT, type);

    if (th->info->seesound)
	S_StartSound (th, th->info->seesound);

    th->target = source;
    th->angle  = source->angle;
    an = th->angle;

    if (target->flags & MF_SHADOW)		// fuzzy target -> inaccurate
    {
	int t = P_Random ();
	an += (t - P_Random ()) << 21;
    }

    an >>= ANGLETOFINESHIFT;
    th->momx = FixedMul (th->info->speed, finecosine[an]);
    th->momy = FixedMul (th->info->speed, finesine[an]);

    dist = P_AproxDistance (target->x - source->x, target->y - source->y);
    dist = dist / th->info->speed;
    if (dist < 1)
	dist = 1;
    th->momz = (target->z - source->z) / dist;

    SM_CheckMissileSpawn (th);
    return th;
}

// P_SpawnMortar: fire `type` along source's facing, with slope from an aim trace.
static mobj_t* SM_SpawnMortar (mobj_t* source, mobj_t* target, mobjtype_t type)
{
    mobj_t*	th;
    angle_t	an = source->angle;
    fixed_t	slope;

    th = P_SpawnMobj (source->x, source->y, source->z, type);
    th->target = target;
    th->angle  = an;
    an >>= ANGLETOFINESHIFT;
    th->momx = FixedMul (th->info->speed, finecosine[an]);
    th->momy = FixedMul (th->info->speed, finesine[an]);

    SM_CheckMissileSpawn (th);

    slope = P_AimLineAttack (source, source->angle, 1024*FRACUNIT);
    th->momz = FixedMul (th->info->speed, slope);
    return th;
}

// P_CheckRobotRange: Strife's "close enough for a robot's short-range attack".
static boolean SM_CheckRobotRange (mobj_t* actor)
{
    fixed_t dist;

    if (!actor->target)
	return false;
    if (!P_CheckSight (actor, actor->target))
	return false;
    if (actor->reactiontime)
	return false;

    dist = (P_AproxDistance (actor->x - actor->target->x,
			     actor->y - actor->target->y) - 64*FRACUNIT) >> FRACBITS;
    return (dist < 200);
}

// ===========================================================================
// Ported Strife A_* codepointers (strife/p_enemy.c).  Behaviour preserved;
// SVE-only branches (classicmode / d_maxgore / quest tokens / spoken voice)
// dropped, and P_SetTarget()'s ref-counting collapses to a plain assignment
// (BuddyDoom is vanilla-style).
// ===========================================================================

// --- generic combat -------------------------------------------------------

static void A_RaiseAlarm (mobj_t* actor)
{
    if (actor->target && actor->target->player)
	P_NoiseAlert (actor->target, actor);
}

static void A_ClearSoundTarget (mobj_t* actor)
{
    actor->subsector->sector->soundtarget = NULL;
}

// Wander with no fixed target (townsfolk/allies idle).  Simplified from Strife's
// A_RandomWalk (P_NewRandomDir): pick a fresh random direction when blocked.
static void A_RandomWalk (mobj_t* actor)
{
    if (actor->reactiontime)
    {
	actor->reactiontime--;
	return;
    }
    if (--actor->movecount < 0 || !P_Move (actor))
    {
	actor->movedir   = P_Random () % 8;
	actor->movecount = 5 + (P_Random () & 7);
    }
}

static void A_CheckTargetVisible (mobj_t* actor)
{
    A_FaceTarget (actor);
    if (P_Random () >= 30)
    {
	mobj_t* target = actor->target;
	if (!target || target->health <= 0 || !P_CheckSight (actor, target)
	    || P_Random () < 40)
	    P_SetMobjState (actor, actor->info->seestate);
    }
}

static void A_CheckTargetVisible2 (mobj_t* actor)
{
    if (!actor->target || actor->target->health <= 0
	|| !P_CheckSight (actor, actor->target))
	P_SetMobjState (actor, actor->info->seestate);
}

// Float bob for the Bishop / flying things.
static void A_FloatWeave (mobj_t* actor)
{
    fixed_t height, z;

    if (actor->threshold)
	return;
    if (actor->flags & MF_INFLOAT)
	return;

    height = actor->info->height;
    z      = actor->floorz + 96*FRACUNIT;

    if (z > actor->ceilingz - height - 16*FRACUNIT)
	z = actor->ceilingz - height - 16*FRACUNIT;

    if (z >= actor->z)
	actor->momz += FRACUNIT;
    else
	actor->momz -= FRACUNIT;

    actor->threshold = (z == actor->z) ? 4 : 8;
}

// Shadow/visibility toggles (MF_MVIS has no engine bit -> only MF_SHADOW kept).
static void A_ShadowOn (mobj_t* actor)      { actor->flags |=  MF_SHADOW; }
static void A_ShadowOff (mobj_t* actor)     { actor->flags &= ~MF_SHADOW; }
static void A_ModifyVisibility (mobj_t* actor) { actor->flags |= MF_SHADOW; }

// --- radius-attack death payloads -----------------------------------------

static void A_DeathExplode1 (mobj_t* actor)
{
    P_RadiusAttack (actor, actor->target, 128);
    if (actor->target && actor->target->player)
	P_NoiseAlert (actor->target, actor);
}
static void A_DeathExplode2 (mobj_t* actor)
{
    P_RadiusAttack (actor, actor->target, 64);
    if (actor->target && actor->target->player)
	P_NoiseAlert (actor->target, actor);
}
static void A_DeathExplode3 (mobj_t* actor)
{
    P_RadiusAttack (actor, actor->target, 32);
    if (actor->target && actor->target->player)
	P_NoiseAlert (actor->target, actor);
}
static void A_DeathExplode5 (mobj_t* actor)
{
    P_RadiusAttack (actor, actor->target, 192);
    if (actor->target && actor->target->player)
	P_NoiseAlert (actor->target, actor);
}

// --- peasants / civilians --------------------------------------------------

static void A_PeasantPunch (mobj_t* actor)
{
    if (!actor->target)
	return;
    A_FaceTarget (actor);
    if (P_CheckMeleeRange (actor))
	P_DamageMobj (actor->target, actor, actor, 2 * (P_Random () % 5) + 2);
}

static void A_PeasantCrash (mobj_t* actor)
{
    if (!(P_Random () % 5))
    {
	A_Pain (actor);
	actor->health--;
    }
    if (actor->health <= 0)
	P_DamageMobj (actor, NULL, NULL, 10000);	// finish off (was P_KillMobj)
}

// --- Reaver / robots -------------------------------------------------------

static void A_ReaverAttack (mobj_t* actor)
{
    int     i = 0;
    fixed_t slope;

    if (!actor->target)
	return;

    S_StartSound (actor, sfx_s_reavat);
    A_FaceTarget (actor);
    slope = P_AimLineAttack (actor, actor->angle, 2048*FRACUNIT);

    do
    {
	int     t          = P_Random ();
	angle_t shootangle = actor->angle + ((t - P_Random ()) << 20);
	int     damage     = 3 * ((P_Random () & 7) + 1);
	P_LineAttack (actor, shootangle, 2048*FRACUNIT, slope, damage);
	++i;
    } while (i < 3);
}

static void A_BulletAttack (mobj_t* actor)
{
    int     t, damage;
    fixed_t slope;
    angle_t shootangle;

    if (!actor->target)
	return;

    S_StartSound (actor, sfx_s_rifle);
    A_FaceTarget (actor);
    slope      = P_AimLineAttack (actor, actor->angle, 2048*FRACUNIT);
    t          = P_Random ();
    shootangle = ((t - P_Random ()) << 19) + actor->angle;
    damage     = 3 * (P_Random () % 5 + 1);
    P_LineAttack (actor, shootangle, 2048*FRACUNIT, slope, damage);
}

static void A_RobotMelee (mobj_t* actor)
{
    if (!actor->target)
	return;
    A_FaceTarget (actor);
    if (P_CheckMeleeRange (actor))
    {
	S_StartSound (actor, sfx_s_revbld);
	P_DamageMobj (actor->target, actor, actor, 3 * (P_Random () % 8 + 1));
    }
}

static void A_BossMeleeAtk (mobj_t* actor)
{
    if (!actor->target)
	return;
    P_DamageMobj (actor->target, actor, actor, 10 * (P_Random () & 9));
}

// --- Crusader --------------------------------------------------------------

static void A_CrusaderAttack (mobj_t* actor)
{
    if (!actor->target)
	return;

    actor->z += 8*FRACUNIT;

    if (SM_CheckRobotRange (actor))
    {
	A_FaceTarget (actor);
	actor->angle -= (ANG90 / 8);
	SM_SpawnFacingMissile (actor, actor->target, MT_S_C_FLAME);
    }
    else if (P_CheckMissileRange (actor))
    {
	A_FaceTarget (actor);
	actor->z += 16*FRACUNIT;
	SM_SpawnFacingMissile (actor, actor->target, MT_S_C_MISSILE);
	actor->angle -= (ANG45 / 32);
	actor->z -= 16*FRACUNIT;
	SM_SpawnFacingMissile (actor, actor->target, MT_S_C_MISSILE);
	actor->angle += (ANG45 / 16);
	SM_SpawnFacingMissile (actor, actor->target, MT_S_C_MISSILE);
	P_SetMobjState (actor, actor->info->seestate);
	actor->reactiontime += 15;
    }
    else
	P_SetMobjState (actor, actor->info->seestate);

    actor->z -= 8*FRACUNIT;
}

static void A_CrusaderLeft (mobj_t* actor)
{
    mobj_t* mo;
    actor->angle += (ANG90 / 16);
    mo = SM_SpawnFacingMissile (actor, actor->target, MT_S_C_FLAME);
    mo->momz = FRACUNIT;
    mo->z   += 16*FRACUNIT;
}

static void A_CrusaderRight (mobj_t* actor)
{
    mobj_t* mo;
    actor->angle -= (ANG90 / 16);
    mo = SM_SpawnFacingMissile (actor, actor->target, MT_S_C_FLAME);
    mo->momz = FRACUNIT;
    mo->z   += 16*FRACUNIT;
}

// --- Sentinel --------------------------------------------------------------

static void A_SentinelAttack (mobj_t* actor)
{
    mobj_t* mo;
    mobj_t* mo2;
    fixed_t x, y, z;
    angle_t an;
    int     i;

    if (!actor->target)
	return;

    mo = SM_SpawnFacingMissile (actor, actor->target, MT_S_L_LASER);
    an = actor->angle >> ANGLETOFINESHIFT;

    if (mo->momy | mo->momx)
    {
	for (i = 8; i > 1; i--)
	{
	    x = mo->x + FixedMul (mobjinfo[MT_S_L_LASER].radius * i, finecosine[an]);
	    y = mo->y + FixedMul (mobjinfo[MT_S_L_LASER].radius * i, finesine[an]);
	    z = mo->z + i * (mo->momz >> 2);
	    mo2 = P_SpawnMobj (x, y, z, MT_S_R_LASER);
	    mo2->target = actor;
	    mo2->momx = mo->momx;
	    mo2->momy = mo->momy;
	    mo2->momz = mo->momz;
	    SM_CheckMissileSpawn (mo2);
	}
    }
    mo->z += mo->momz >> 2;
}

// --- Templar (mauler-style spread) ----------------------------------------

static void A_TemplarMauler (mobj_t* actor)
{
    int i, t, angle, bangle, damage, slope;

    if (!actor->target)
	return;

    S_StartSound (actor, sfx_s_pgrdat);
    A_FaceTarget (actor);
    bangle = actor->angle;
    slope  = P_AimLineAttack (actor, bangle, 2048*FRACUNIT);

    for (i = 0; i < 10; i++)
    {
	damage = (P_Random () & 4) * 2;
	t      = P_Random ();
	angle  = bangle + ((t - P_Random ()) << 19);
	t      = P_Random ();
	slope  = ((t - P_Random ()) << 5) + slope;
	P_LineAttack (actor, angle, 2112*FRACUNIT, slope, damage);
    }
}

// --- Bishop ----------------------------------------------------------------

static void A_BishopAttack (mobj_t* actor)
{
    mobj_t* mo;

    if (!actor->target)
	return;

    actor->z += MAXRADIUS;
    mo = P_SpawnMissile (actor, actor->target, MT_S_SEEKMISSILE);
    if (mo)
	mo->tracer = actor->target;
    actor->z -= MAXRADIUS;
}

// --- Inquisitor ------------------------------------------------------------

static void A_InqChase (mobj_t* actor)
{
    S_StartSound (actor, sfx_s_inqact);
    A_Chase (actor);
}

static void A_InqFlyCheck (mobj_t* actor)
{
    if (!actor->target)
	return;
    A_FaceTarget (actor);

    if (!SM_CheckRobotRange (actor))
	P_SetMobjState (actor, S_S_ROB3_14);	// throw grenades

    if (actor->z != actor->target->z)
    {
	if (actor->z + actor->height + 54*FRACUNIT < actor->ceilingz)
	    P_SetMobjState (actor, S_S_ROB3_17);	// take off
    }
}

static void A_InqGrenade (mobj_t* actor)
{
    mobj_t* mo;

    if (!actor->target)
	return;

    A_FaceTarget (actor);
    actor->z += MAXRADIUS;

    actor->angle -= (ANG45 / 32);
    mo = SM_SpawnFacingMissile (actor, actor->target, MT_S_INQGRENADE);
    mo->momz += 9*FRACUNIT;

    actor->angle += (ANG45 / 16);
    mo = SM_SpawnFacingMissile (actor, actor->target, MT_S_INQGRENADE);
    mo->momz += 16*FRACUNIT;

    actor->z -= MAXRADIUS;
}

static void A_InqTakeOff (mobj_t* actor)
{
    angle_t an;
    fixed_t speed = actor->info->speed * (2 * FRACUNIT / 3);
    fixed_t dist;

    if (!actor->target)
	return;

    S_StartSound (actor, sfx_s_inqjmp);
    actor->z += 64*FRACUNIT;
    A_FaceTarget (actor);

    an = actor->angle >> ANGLETOFINESHIFT;
    actor->momx = FixedMul (finecosine[an], speed);
    actor->momy = FixedMul (finesine[an],   speed);

    dist = P_AproxDistance (actor->target->x - actor->x,
			    actor->target->y - actor->y);
    dist /= speed;
    if (dist < 1)
	dist = 1;

    actor->momz = (actor->target->z - actor->z) / dist;
    actor->reactiontime = 60;
    actor->flags |= MF_NOGRAVITY;
}

static void A_InqFly (mobj_t* actor)
{
    if (!(leveltime & 7))
	S_StartSound (actor, sfx_s_inqjmp);

    if (--actor->reactiontime < 0 || !actor->momx || !actor->momy
	|| actor->z <= actor->floorz)
    {
	P_SetMobjState (actor, actor->info->seestate);
	actor->reactiontime = 0;
	actor->flags &= ~MF_NOGRAVITY;
    }
}

static void A_InqTossArm (mobj_t* actor)
{
    int     r;
    angle_t an;
    mobj_t* mo;

    mo = P_SpawnMobj (actor->x, actor->y, actor->z + 24*FRACUNIT, MT_S_INQARM);
    r  = P_Random ();
    an = ((r - P_Random ()) << 22) + actor->angle - ANG90;
    mo->angle = an;
    SM_Thrust (mo, an, mo->info->speed);
    mo->momz = P_Random () << 10;
}

// --- Stalker ---------------------------------------------------------------

static void A_StalkerChase (mobj_t* actor)
{
    S_StartSound (actor, sfx_s_spdwlk);
    A_Chase (actor);
}

static void A_StalkerScratch (mobj_t* actor)
{
    if (actor->flags & MF_NOGRAVITY)
    {
	P_SetMobjState (actor, S_S_SPID_11);	// drop off the ceiling first
	return;
    }
    if (!actor->target)
	return;
    A_FaceTarget (actor);
    if (P_CheckMeleeRange (actor))
	P_DamageMobj (actor->target, actor, actor, 2 * (P_Random () % 8) + 2);
}

static void A_StalkerSetLook (mobj_t* actor)
{
    statenum_t statenum;

    if (!actor)
	return;

    if (actor->flags & MF_NOGRAVITY)
    {
	if (actor->state->nextstate == S_S_SPID_01)
	    return;
	statenum = S_S_SPID_01;
    }
    else
    {
	if (actor->state->nextstate == S_S_SPID_02)
	    return;
	statenum = S_S_SPID_02;
    }
    P_SetMobjState (actor, statenum);
}

static void A_StalkerThink (mobj_t* actor)
{
    statenum_t statenum;

    if (actor->flags & MF_NOGRAVITY)
    {
	if (actor->ceilingz - actor->info->height <= actor->z)
	    return;
	statenum = S_S_SPID_11;
    }
    else
	statenum = S_S_SPID_18;

    P_SetMobjState (actor, statenum);
}

static void A_StalkerDrop (mobj_t* actor)
{
    actor->flags &= ~MF_NOGRAVITY;
}

// --- Programmer (boss) -----------------------------------------------------

static void A_ProgrammerMelee (mobj_t* actor)
{
    if (!actor->target)
	return;
    A_FaceTarget (actor);
    if (P_CheckMeleeRange (actor))
    {
	int damage = 6 * (P_Random () % 10 + 1);
	S_StartSound (actor, sfx_s_mtalht);
	P_DamageMobj (actor->target, actor, actor, damage);
    }
}

// Programmer's Sigil-A ground attack: plant a lightning nexus on the target.
static void A_ProgrammerAttack (mobj_t* actor)
{
    mobj_t* mo;

    if (!actor->target)
	return;

    mo = P_SpawnMobj (actor->target->x, actor->target->y, ONFLOORZ,
		      MT_S_SIGIL_A_GROUND);
    mo->threshold = 25;
    mo->target    = actor;
    mo->health    = -2;
    mo->tracer    = actor->target;
}

static void A_ProgrammerDie (mobj_t* actor)
{
    int     r;
    angle_t an;
    mobj_t* mo;

    mo = P_SpawnMobj (actor->x, actor->y, actor->z + 24*FRACUNIT, MT_S_PROGRAMMERBASE);
    r  = P_Random ();
    an = ((r - P_Random ()) << 22) + actor->angle + ANG180;
    mo->angle = an;
    SM_Thrust (mo, an, mo->info->speed);
    mo->momz = P_Random () << 9;
}

// --- gib chunk spawners ----------------------------------------------------

static void A_HeadChunk (mobj_t* actor)
{
    int     r;
    mobj_t* mo;
    mo = P_SpawnMobj (actor->x, actor->y, actor->z + 10*FRACUNIT, MT_S_SPECTREHEAD);
    r = P_Random (); mo->momx = ((r & 7) - (P_Random () & 0x0f)) << FRACBITS;
    r = P_Random (); mo->momy = ((r & 0x0f) - (P_Random () & 7)) << FRACBITS;
    mo->momz = (P_Random () & 7) << FRACBITS;
}

static void A_NodeChunk (mobj_t* actor)
{
    int     r;
    mobj_t* mo;
    mo = P_SpawnMobj (actor->x, actor->y, actor->z + 10*FRACUNIT, MT_S_NODE);
    r = P_Random (); mo->momx = ((r & 0x0f) - (P_Random () & 7)) << FRACBITS;
    r = P_Random (); mo->momy = ((r & 7) - (P_Random () & 0x0f)) << FRACBITS;
    mo->momz = (P_Random () & 0x0f) << FRACBITS;
}

// --- Entity / Sub-Entity (final boss) --------------------------------------

static void A_SpawnEntity (mobj_t* actor)
{
    mobj_t* mo;
    mo = P_SpawnMobj (actor->x, actor->y, actor->z + 70*FRACUNIT, MT_S_ENTITY);
    mo->momz = 5*FRACUNIT;
    entity_pos_x = mo->x;
    entity_pos_y = mo->y;
    entity_pos_z = mo->z;
    mo->spawnpoint.x     = mo->x / FRACUNIT;
    mo->spawnpoint.y     = mo->y / FRACUNIT;
    mo->spawnpoint.angle = mo->z / FRACUNIT;
}

static void A_EntityDeath (mobj_t* actor)
{
    mobj_t* subentity;
    angle_t an;
    fixed_t dist;

    dist = 2 * mobjinfo[MT_S_SUBENTITY].radius;

    if (entity_pos_x == 0 && entity_pos_y == 0 && entity_pos_z == 0)
    {
	entity_pos_x = actor->spawnpoint.x     << FRACBITS;
	entity_pos_y = actor->spawnpoint.y     << FRACBITS;
	entity_pos_z = actor->spawnpoint.angle << FRACBITS;
    }

    an = actor->angle >> ANGLETOFINESHIFT;
    subentity = P_SpawnMobj (FixedMul (finecosine[an], dist) + entity_pos_x,
			     FixedMul (finesine[an],   dist) + entity_pos_y,
			     entity_pos_z, MT_S_SUBENTITY);
    subentity->target = actor->target;
    A_FaceTarget (subentity);
    SM_Thrust (subentity, subentity->angle, 625 << 13);

    an = (actor->angle + ANG90) >> ANGLETOFINESHIFT;
    subentity = P_SpawnMobj (FixedMul (finecosine[an], dist) + entity_pos_x,
			     FixedMul (finesine[an],   dist) + entity_pos_y,
			     entity_pos_z, MT_S_SUBENTITY);
    subentity->target = actor->target;
    SM_Thrust (subentity, actor->angle + ANG90, 4);
    A_FaceTarget (subentity);

    an = (actor->angle - ANG90) >> ANGLETOFINESHIFT;
    subentity = P_SpawnMobj (FixedMul (finecosine[an], dist) + entity_pos_x,
			     FixedMul (finesine[an],   dist) + entity_pos_y,
			     entity_pos_z, MT_S_SUBENTITY);
    subentity->target = actor->target;
    SM_Thrust (subentity, actor->angle - ANG90, 4);
    A_FaceTarget (subentity);
}

// --- spectral (alien spectre) attacks + spawns -----------------------------
// Forward declares so A_FireSigilWeapon can dispatch to them.
static void A_ProgrammerAttack (mobj_t*);
static void A_FireSigilEOffshoot (mobj_t*);
static void A_SpectreCAttack (mobj_t*);
static void A_SpectreDAttack (mobj_t*);
static void A_SpectreEAttack (mobj_t*);

// Oracle's Spectre (C): overhead zap + a fan of ground Sigil shots.
static void A_SpectreCAttack (mobj_t* actor)
{
    mobj_t* mo;
    int     i;

    if (!actor->target)
	return;

    mo = P_SpawnMobj (actor->x, actor->y, actor->z + 32*FRACUNIT, MT_S_SIGIL_A_ZAP_RIGHT);
    mo->momz   = -(18*FRACUNIT);
    mo->target = actor;
    mo->health = -2;
    mo->tracer = actor->target;

    actor->angle -= ANG90;
    for (i = 0; i < 20; i++)
    {
	actor->angle += (ANG90 / 10);
	mo = SM_SpawnMortar (actor, actor, MT_S_SIGIL_C_SHOT);
	mo->health = -2;
	mo->z = actor->z + 32*FRACUNIT;
    }
    actor->angle -= ANG90;
}

// Macil's Spectre (D): a homing Sigil-D shot.
static void A_SpectreDAttack (mobj_t* actor)
{
    mobj_t* mo;
    if (!actor->target)
	return;
    mo = P_SpawnMissile (actor, actor->target, MT_S_SIGIL_SD_SHOT);
    mo->health = -2;
    mo->tracer = actor->target;
}

// Loremaster's Spectre (E): the final Sigil shot.
static void A_SpectreEAttack (mobj_t* actor)
{
    mobj_t* mo;
    if (!actor->target)
	return;
    mo = P_SpawnMissile (actor, actor->target, MT_S_SIGIL_SE_SHOT);
    mo->health = -2;
}

static void A_FireSigilEOffshoot (mobj_t* actor)
{
    mobj_t* mo;
    if (!actor->target)
	return;
    mo = P_SpawnMissile (actor, actor->target, MT_S_SIGIL_E_OFFSHOOT);
    mo->health = -2;
}

// Entity's grab-bag Sigil weapon: randomly picks one of the sub-attacks.
static void A_FireSigilWeapon (mobj_t* actor)
{
    switch (P_Random () % 5)
    {
      case 0: A_ProgrammerAttack (actor);   break;
      case 2: A_FireSigilEOffshoot (actor); break;
      case 3: A_SpectreCAttack (actor);     break;
      case 4: A_SpectreDAttack (actor);     break;
      default: break;
    }
}

static void A_AlertSpectreC (mobj_t* actor)
{
    thinker_t* th;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	if (th->function.acp1 == (actionf_p1) P_MobjThinker)
	{
	    mobj_t* mo = (mobj_t*) th;
	    if (mo->type == MT_S_SPECTRE_C)
	    {
		if (mo->health > 0)
		{
		    P_SetMobjState (mo, mo->info->seestate);
		    mo->target = actor->target;
		}
		return;
	    }
	}
    }
}

static void A_SpawnSpectreB (mobj_t* actor)
{
    mobj_t* mo = P_SpawnMobj (actor->x, actor->y, actor->z, MT_S_SPECTRE_B);
    mo->momz = P_Random () << 9;
}
static void A_SpawnSpectreD (mobj_t* actor)
{
    mobj_t* mo = P_SpawnMobj (actor->x, actor->y, actor->z, MT_S_SPECTRE_D);
    mo->momz = P_Random () << 9;
}
static void A_SpawnSpectreE (mobj_t* actor)
{
    mobj_t* mo = P_SpawnMobj (actor->x, actor->y, actor->z, MT_S_SPECTRE_E);
    mo->momz = P_Random () << 9;
}

static void A_SpawnZombie (mobj_t* actor)
{
    P_SpawnMobj (actor->x, actor->y, actor->z, MT_S_ZOMBIE);
}

// --- Sigil projectile FX ---------------------------------------------------

// MT_SIGIL_A_GROUND: zap anyone nearby with lightning bolts from the ceiling.
static void A_Sigil_A_Action (mobj_t* actor)
{
    int     t, x, y, type;
    mobj_t* mo;

    if (actor->threshold)
	actor->threshold--;

    t = P_Random (); actor->momx += ((t & 3) - (P_Random () & 3)) << FRACBITS;
    t = P_Random (); actor->momy += ((t & 3) - (P_Random () & 3)) << FRACBITS;

    t = P_Random (); x = 50*FRACUNIT * ((t & 3) - (P_Random () & 3)) + actor->x;
    t = P_Random (); y = 50*FRACUNIT * ((t & 3) - (P_Random () & 3)) + actor->y;

    type = (actor->threshold <= 25) ? MT_S_SIGIL_A_ZAP_LEFT : MT_S_SIGIL_A_ZAP_RIGHT;

    mo = P_SpawnMobj (x, y, ONCEILINGZ, type);
    mo->momz   = -18*FRACUNIT;
    mo->target = actor->target;
    mo->health = actor->health;

    mo = P_SpawnMobj (actor->x, actor->y, ONCEILINGZ, MT_S_SIGIL_A_ZAP_RIGHT);
    mo->momz   = -18*FRACUNIT;
    mo->target = actor->target;
    mo->health = actor->health;
}

// MT_SIGIL_E_SHOT: spray three room-filling offshoots.
static void A_Sigil_E_Action (mobj_t* actor)
{
    actor->angle += ANG90;
    SM_SpawnMortar (actor, actor->target, MT_S_SIGIL_E_OFFSHOOT);
    actor->angle -= ANG180;
    SM_SpawnMortar (actor, actor->target, MT_S_SIGIL_E_OFFSHOOT);
    actor->angle += ANG90;
    SM_SpawnMortar (actor, actor->target, MT_S_SIGIL_E_OFFSHOOT);
}

static void A_SigilTrail (mobj_t* actor)
{
    mobj_t* mo;
    mo = P_SpawnMobj (actor->x - actor->momx, actor->y - actor->momy, actor->z,
		      MT_S_SIGIL_TRAIL);
    mo->angle  = actor->angle;
    mo->health = actor->health;
}

// --- hookshot / chainshot / missile smoke / tracer -------------------------

static void A_FireHookShot (mobj_t* actor)
{
    if (!actor->target)
	return;
    P_SpawnMissile (actor, actor->target, MT_S_HOOKSHOT);
}

static void A_FireChainShot (mobj_t* actor)
{
    S_StartSound (actor, sfx_s_tend);
    P_SpawnMobj (actor->x, actor->y, actor->z, MT_S_CHAINSHOT);
    P_SpawnMobj (actor->x - (actor->momx >> 1),
		 actor->y - (actor->momy >> 1), actor->z, MT_S_CHAINSHOT);
    P_SpawnMobj (actor->x - actor->momx,
		 actor->y - actor->momy, actor->z, MT_S_CHAINSHOT);
}

static void A_MissileSmoke (mobj_t* actor)
{
    mobj_t* mo;
    S_StartSound (actor, sfx_s_rflite);
    P_SpawnPuff (actor->x, actor->y, actor->z);
    mo = P_SpawnMobj (actor->x - actor->momx, actor->y - actor->momy, actor->z,
		      MT_S_MISSILESMOKE);
    mo->momz = FRACUNIT;
}

static void A_MissileTick (mobj_t* actor)
{
    if (--actor->reactiontime <= 0)
    {
	P_ExplodeMissile (actor);
	actor->flags &= ~MF_MISSILE;
    }
}

static void A_FlameDeath (mobj_t* actor)
{
    actor->flags |= MF_NOGRAVITY;
    actor->momz = (P_Random () & 3) << FRACBITS;
}

// Strife tracer (widened turn rate, no puff/randomization).
static void A_Tracer (mobj_t* actor)
{
    angle_t exact;
    fixed_t dist, slope;
    mobj_t* dest;

    dest = actor->tracer;
    if (!dest || dest->health <= 0)
	return;

    exact = R_PointToAngle2 (actor->x, actor->y, dest->x, dest->y);
    if (exact != actor->angle)
    {
	if (exact - actor->angle <= 0x80000000)
	{
	    actor->angle += TRACEANGLE;
	    if (exact - actor->angle > 0x80000000)
		actor->angle = exact;
	}
	else
	{
	    actor->angle -= TRACEANGLE;
	    if (exact - actor->angle < 0x80000000)
		actor->angle = exact;
	}
    }

    exact = actor->angle >> ANGLETOFINESHIFT;
    actor->momx = FixedMul (actor->info->speed, finecosine[exact]);
    actor->momy = FixedMul (actor->info->speed, finesine[exact]);

    dist = P_AproxDistance (dest->x - actor->x, dest->y - actor->y);
    dist = dist / actor->info->speed;
    if (dist < 1)
	dist = 1;
    slope = (dest->z + 40*FRACUNIT - actor->z) / dist;

    if (slope < actor->momz)
	actor->momz -= FRACUNIT / 8;
    else
	actor->momz += FRACUNIT / 8;
}

// --- boss death triggers ---------------------------------------------------
// Faithful gate (all bosses of this type dead + a live player) then the level
// event.  Quest-token / spoken-voice / sequence-break handling is dropped.
static void A_BossDeath (mobj_t* actor)
{
    int        i;
    thinker_t* th;
    line_t     junk;

    switch (actor->type)
    {
      case MT_S_CRUSADER:  case MT_S_SPECTRE_A: case MT_S_SPECTRE_B:
      case MT_S_SPECTRE_C: case MT_S_SPECTRE_D: case MT_S_SPECTRE_E:
      case MT_S_SUBENTITY: case MT_S_PROGRAMMER:
	break;
      default:
	return;
    }

    for (i = 0; i < MAXPLAYERS; i++)
	if (playeringame[i] && players[i].health > 0)
	    break;
    if (i == MAXPLAYERS)
	return;					// everybody's dead

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	if (th->function.acp1 == (actionf_p1) P_MobjThinker)
	{
	    mobj_t* mo = (mobj_t*) th;
	    if (mo != actor && mo->type == actor->type && mo->health > 0)
		return;				// another of this boss lives
	}
    }

    switch (actor->type)
    {
      case MT_S_CRUSADER:
	junk.tag = 667; EV_DoFloor (&junk, lowerFloorToLowest);
	break;
      case MT_S_SPECTRE_A:
	junk.tag = 999; EV_DoFloor (&junk, lowerFloorToLowest);
	break;
      case MT_S_SPECTRE_C:
	junk.tag = 222; EV_DoDoor (&junk, open);
	break;
      case MT_S_SPECTRE_E:
	junk.tag = 666; EV_DoFloor (&junk, lowerFloorToLowest);
	break;
      case MT_S_SUBENTITY:
	F_StartFinale ();
	break;
      case MT_S_PROGRAMMER:
	F_StartFinale ();
	G_ExitLevel ();
	break;
      default:
	break;
    }
}

// TODO (dropped to NULL in the tables; no engine equivalent, actor still
// spawns/moves/attacks/dies):
//   A_BodyParts (ludicrous-gibs -- needs MEAT/JUNK chunk frames deco left
//     unfilled), A_ActiveSound (ambient loops), A_AcolyteSpecial (quest token),
//   A_ClearForceField (line special 148), A_Listen (alarm), A_HideZombie /
//   A_MerchantPain (shop-door dialog), A_SetTLOptions (MTF translucency),
//   A_ZombieInSpecialSector (nukage insta-kill).
//   A_FriendLook is mapped to the engine A_Look.

// ---------------------------------------------------------------------------
// Table fill helper (identical to strife_deco.c).
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

// Map a name to a Strife mobjtype for the console `summon`, or -1 if unknown.
// Names are GZDoom's Strife actor class names lowercased (see
// ../gzdoom/wadsrc/static/zscript/actors/strife + mapinfo/strife.txt DoomEdNums),
// with the short aliases people actually type.  Where a name is already taken by an
// earlier game's table in C_MobjByName (bishop/stalker/zombie/spectre are Hexen or
// DOOM), the "strife*" spelling is the one that always resolves.
int Strife_Mon_TypeByName (const char* s)
{
    static const struct { const char* n; short t; } tbl[] = {
	// grunts
	{"acolyte",MT_S_GUARD1},	{"acolytetan",MT_S_GUARD1},
	{"acolytered",MT_S_GUARD2},	{"acolyterust",MT_S_GUARD3},
	{"acolytegray",MT_S_GUARD4},	{"acolytedgreen",MT_S_GUARD5},
	{"acolytegold",MT_S_GUARD6},	{"acolytelgreen",MT_S_GUARD7},
	{"acolyteblue",MT_S_GUARD8},	{"acolyteshadow",MT_S_SHADOWGUARD},
	{"acolytetobe",MT_S_BECOMING},	{"becoming",MT_S_BECOMING},
	{"templar",MT_S_PGUARD},
	{"reaver",MT_S_REAVER},
	{"zombie",MT_S_ZOMBIE},		{"strifezombie",MT_S_ZOMBIE},
	{"zombiespawner",MT_S_ZOMBIESPAWNER},
	{"rebel",MT_S_REBEL1},		{"rebel1",MT_S_REBEL1},
	{"rebel2",MT_S_REBEL2},		{"rebel3",MT_S_REBEL3},
	{"rebel4",MT_S_REBEL4},		{"rebel5",MT_S_REBEL5},
	{"rebel6",MT_S_REBEL6},
	{"peasant",MT_S_PEASANT1},	{"beggar",MT_S_BEGGAR1},
	{"kneelingguy",MT_S_KNEELING_GUY},
	// machines / bosses
	{"crusader",MT_S_CRUSADER},
	{"sentinel",MT_S_SENTINEL},
	{"inquisitor",MT_S_INQUISITOR},
	{"stalker",MT_S_STALKER},	{"strifestalker",MT_S_STALKER},
	{"strifebishop",MT_S_BISHOP},	{"bishop",MT_S_BISHOP},
	{"programmer",MT_S_PROGRAMMER},
	{"loremaster",MT_S_PRIEST},	{"priest",MT_S_PRIEST},
	{"oracle",MT_S_ORACLE},
	{"macil",MT_S_RLEADER},		{"macil1",MT_S_RLEADER},
	{"macil2",MT_S_RLEADER2},
	{"entityboss",MT_S_ENTITY},	{"entity",MT_S_ENTITY},
	{"entitysecond",MT_S_SUBENTITY},{"subentity",MT_S_SUBENTITY},
	{"alienspectre",MT_S_SPECTRE_A},{"alienspectre1",MT_S_SPECTRE_A},
	{"alienspectre2",MT_S_SPECTRE_B},{"alienspectre3",MT_S_SPECTRE_C},
	{"alienspectre4",MT_S_SPECTRE_D},{"alienspectre5",MT_S_SPECTRE_E},
	// turrets / hazards
	{"ceilingturret",MT_S_TURRET},	{"strifeturret",MT_S_TURRET},
	{"forcefieldguard",MT_S_FIELDGUARD},
	{NULL,0}
    };
    int i;

    if (!s || !s[0] || !Strife_Available ())
	return -1;
    for (i = 0; tbl[i].n; i++)
	if (!strcmp (s, tbl[i].n))
	    return tbl[i].t;
    return -1;
}

void Strife_Mon_Init (void)
{
    mobjinfo_t*	m;

    // ====================================================================
    // STATES
    // ====================================================================
    ST (S_S_POW1_05, SPR_S_POW1, 5, 4, NULL, S_S_POW1_06);
    ST (S_S_POW1_06, SPR_S_POW1, 6, 4, NULL, S_S_POW1_07);
    ST (S_S_POW1_07, SPR_S_POW1, 7, 4, NULL, S_S_POW1_08);
    ST (S_S_POW1_08, SPR_S_POW1, 8, 4, NULL, S_S_POW1_09);
    ST (S_S_POW1_09, SPR_S_POW1, 9, 4, NULL, S_NULL);
    ST (S_S_ZAP1_00, SPR_S_ZAP1, 1, 3, (actionf_p1)A_DeathExplode3, S_S_ZAP1_02);
    ST (S_S_ZAP1_01, SPR_S_ZAP1, 0, 3, (actionf_p1)A_RaiseAlarm, S_S_ZAP1_02);
    ST (S_S_ZAP1_02, SPR_S_ZAP1, 1, 3, NULL, S_S_ZAP1_03);
    ST (S_S_ZAP1_03, SPR_S_ZAP1, 2, 3, NULL, S_S_ZAP1_04);
    ST (S_S_ZAP1_04, SPR_S_ZAP1, 3, 3, NULL, S_S_ZAP1_05);
    ST (S_S_ZAP1_05, SPR_S_ZAP1, 4, 3, NULL, S_S_ZAP1_06);
    ST (S_S_ZAP1_06, SPR_S_ZAP1, 5, 3, NULL, S_S_ZAP1_07);
    ST (S_S_ZAP1_07, SPR_S_ZAP1, 4, 3, NULL, S_S_ZAP1_08);
    ST (S_S_ZAP1_08, SPR_S_ZAP1, 3, 2, NULL, S_S_ZAP1_09);
    ST (S_S_ZAP1_09, SPR_S_ZAP1, 2, 2, NULL, S_S_ZAP1_10);
    ST (S_S_ZAP1_10, SPR_S_ZAP1, 1, 2, NULL, S_S_ZAP1_11);
    ST (S_S_ZAP1_11, SPR_S_ZAP1, 0, 1, NULL, S_NULL);
    ST (S_S_SHT1_00, SPR_S_SHT1, 0, 4, NULL, S_S_SHT1_01);
    ST (S_S_SHT1_01, SPR_S_SHT1, 1, 4, NULL, S_S_SHT1_00);
    ST (S_S_UBAM_00, SPR_S_UBAM, 0, 3, (actionf_p1)A_MissileTick, S_S_UBAM_01);
    ST (S_S_UBAM_01, SPR_S_UBAM, 1, 3, (actionf_p1)A_MissileTick, S_S_UBAM_00);
    ST (S_S_BNG2_00, SPR_S_BNG2, 32768, 4, (actionf_p1)A_DeathExplode5, S_S_BNG2_01);
    ST (S_S_BNG2_01, SPR_S_BNG2, 32769, 4, NULL, S_S_BNG2_02);
    ST (S_S_BNG2_02, SPR_S_BNG2, 32770, 4, NULL, S_S_BNG2_03);
    ST (S_S_BNG2_03, SPR_S_BNG2, 32771, 4, NULL, S_S_BNG2_04);
    ST (S_S_BNG2_04, SPR_S_BNG2, 32772, 4, NULL, S_S_BNG2_05);
    ST (S_S_BNG2_05, SPR_S_BNG2, 32773, 4, NULL, S_S_BNG2_06);
    ST (S_S_BNG2_06, SPR_S_BNG2, 32774, 4, NULL, S_S_BNG2_07);
    ST (S_S_BNG2_07, SPR_S_BNG2, 32775, 4, NULL, S_S_BNG2_08);
    ST (S_S_BNG2_08, SPR_S_BNG2, 32776, 4, NULL, S_NULL);
    ST (S_S_XPRK_00, SPR_S_XPRK, 0, 1, NULL, S_NULL);
    ST (S_S_OCLW_00, SPR_S_OCLW, 0, 2, (actionf_p1)A_FireChainShot, S_S_OCLW_00);
    ST (S_S_CCLW_00, SPR_S_CCLW, 0, 6, NULL, S_NULL);
    ST (S_S_TEND_00, SPR_S_TEND, 0, 20, NULL, S_NULL);
    ST (S_S_MICR_00, SPR_S_MICR, 32768, 6, (actionf_p1)A_MissileSmoke, S_S_MICR_00);
    ST (S_S_MISS_00, SPR_S_MISS, 32768, 4, (actionf_p1)A_MissileSmoke, S_S_MISS_01);
    ST (S_S_MISS_01, SPR_S_MISS, 32769, 3, (actionf_p1)A_Tracer, S_S_MISS_00);
    ST (S_S_MISL_00, SPR_S_MISL, 32768, 5, NULL, S_S_MISL_02);
    ST (S_S_MISL_01, SPR_S_MISL, 32768, 5, (actionf_p1)A_DeathExplode2, S_S_MISL_02);
    ST (S_S_MISL_02, SPR_S_MISL, 32769, 5, NULL, S_S_MISL_03);
    ST (S_S_MISL_03, SPR_S_MISL, 32770, 4, NULL, S_S_MISL_04);
    ST (S_S_MISL_04, SPR_S_MISL, 32771, 2, NULL, S_S_MISL_05);
    ST (S_S_MISL_05, SPR_S_MISL, 32772, 2, NULL, S_S_MISL_06);
    ST (S_S_MISL_06, SPR_S_MISL, 32773, 2, NULL, S_S_MISL_07);
    ST (S_S_MISL_07, SPR_S_MISL, 32774, 2, NULL, S_NULL);
    ST (S_S_TFOG_00, SPR_S_TFOG, 32768, 6, NULL, S_S_TFOG_01);
    ST (S_S_TFOG_01, SPR_S_TFOG, 32769, 6, NULL, S_S_TFOG_02);
    ST (S_S_TFOG_02, SPR_S_TFOG, 32770, 6, NULL, S_S_TFOG_03);
    ST (S_S_TFOG_03, SPR_S_TFOG, 32771, 6, NULL, S_S_TFOG_04);
    ST (S_S_TFOG_04, SPR_S_TFOG, 32772, 6, NULL, S_S_TFOG_05);
    ST (S_S_TFOG_05, SPR_S_TFOG, 32773, 6, NULL, S_S_TFOG_06);
    ST (S_S_TFOG_06, SPR_S_TFOG, 32772, 6, NULL, S_S_TFOG_07);
    ST (S_S_TFOG_07, SPR_S_TFOG, 32771, 6, NULL, S_S_TFOG_08);
    ST (S_S_TFOG_08, SPR_S_TFOG, 32770, 6, NULL, S_S_TFOG_09);
    ST (S_S_TFOG_09, SPR_S_TFOG, 32769, 6, NULL, S_NULL);
    ST (S_S_MRST_00, SPR_S_MRST, 0, 10, (actionf_p1)A_Look, S_S_MRST_00);
    ST (S_S_MRPN_00, SPR_S_MRPN, 0, 3, NULL, S_S_MRPN_01);
    ST (S_S_MRPN_01, SPR_S_MRPN, 1, 3, (actionf_p1)A_Pain, S_S_MRPN_02);
    ST (S_S_MRPN_02, SPR_S_MRPN, 2, 3, NULL, S_S_MRPN_03);
    ST (S_S_MRPN_03, SPR_S_MRPN, 3, 9, NULL, S_S_MRPN_04);
    ST (S_S_MRPN_04, SPR_S_MRPN, 2, 4, NULL, S_S_MRPN_05);
    ST (S_S_MRPN_05, SPR_S_MRPN, 1, 3, NULL, S_S_MRPN_06);
    ST (S_S_MRPN_06, SPR_S_MRPN, 0, 3, (actionf_p1)A_ClearSoundTarget, S_S_MRST_00);
    ST (S_S_PEAS_00, SPR_S_PEAS, 0, 10, (actionf_p1)A_Look, S_S_PEAS_00);
    ST (S_S_PEAS_01, SPR_S_PEAS, 0, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_02);
    ST (S_S_PEAS_02, SPR_S_PEAS, 0, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_03);
    ST (S_S_PEAS_03, SPR_S_PEAS, 1, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_04);
    ST (S_S_PEAS_04, SPR_S_PEAS, 1, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_05);
    ST (S_S_PEAS_05, SPR_S_PEAS, 2, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_06);
    ST (S_S_PEAS_06, SPR_S_PEAS, 2, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_07);
    ST (S_S_PEAS_07, SPR_S_PEAS, 3, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_08);
    ST (S_S_PEAS_08, SPR_S_PEAS, 3, 5, (actionf_p1)A_RandomWalk, S_S_PEAS_00);
    ST (S_S_PEAS_09, SPR_S_PEAS, 4, 10, (actionf_p1)A_FaceTarget, S_S_PEAS_10);
    ST (S_S_PEAS_10, SPR_S_PEAS, 5, 8, (actionf_p1)A_PeasantPunch, S_S_PEAS_11);
    ST (S_S_PEAS_11, SPR_S_PEAS, 4, 8, NULL, S_S_PEAS_01);
    ST (S_S_PEAS_12, SPR_S_PEAS, 14, 3, NULL, S_S_PEAS_13);
    ST (S_S_PEAS_13, SPR_S_PEAS, 14, 3, (actionf_p1)A_Pain, S_S_PEAS_09);
    ST (S_S_PEAS_14, SPR_S_PEAS, 6, 5, NULL, S_S_PEAS_15);
    ST (S_S_PEAS_15, SPR_S_PEAS, 7, 10, (actionf_p1)A_PeasantCrash, S_S_PEAS_16);
    ST (S_S_PEAS_16, SPR_S_PEAS, 8, 6, NULL, S_S_PEAS_15);
    ST (S_S_PEAS_17, SPR_S_PEAS, 6, 5, NULL, S_S_PEAS_18);
    ST (S_S_PEAS_18, SPR_S_PEAS, 7, 5, (actionf_p1)A_Scream, S_S_PEAS_19);
    ST (S_S_PEAS_19, SPR_S_PEAS, 8, 6, NULL, S_S_PEAS_20);
    ST (S_S_PEAS_20, SPR_S_PEAS, 9, 5, (actionf_p1)A_Fall, S_S_PEAS_21);
    ST (S_S_PEAS_21, SPR_S_PEAS, 10, 5, NULL, S_S_PEAS_22);
    ST (S_S_PEAS_22, SPR_S_PEAS, 11, 6, NULL, S_S_PEAS_23);
    ST (S_S_PEAS_23, SPR_S_PEAS, 12, 8, NULL, S_S_PEAS_24);
    ST (S_S_PEAS_24, SPR_S_PEAS, 13, 1400, NULL, S_S_GIBS_08);
    ST (S_S_GIBS_00, SPR_S_GIBS, 12, 5, NULL, S_S_GIBS_01);
    ST (S_S_GIBS_01, SPR_S_GIBS, 13, 5, (actionf_p1)A_XScream, S_S_GIBS_02);
    ST (S_S_GIBS_02, SPR_S_GIBS, 14, 5, (actionf_p1)A_Fall, S_S_GIBS_03);
    ST (S_S_GIBS_03, SPR_S_GIBS, 15, 4, NULL, S_S_GIBS_04);
    ST (S_S_GIBS_04, SPR_S_GIBS, 16, 4, NULL, S_S_GIBS_05);
    ST (S_S_GIBS_05, SPR_S_GIBS, 17, 4, NULL, S_S_GIBS_06);
    ST (S_S_GIBS_06, SPR_S_GIBS, 18, 4, NULL, S_S_GIBS_07);
    ST (S_S_GIBS_07, SPR_S_GIBS, 19, 4, NULL, S_S_GIBS_08);
    ST (S_S_GIBS_08, SPR_S_GIBS, 20, 5, NULL, S_S_GIBS_09);
    ST (S_S_GIBS_09, SPR_S_GIBS, 21, 1400, NULL, S_NULL);
    ST (S_S_PEAS_25, SPR_S_PEAS, 0, 5, NULL, S_S_PEAS_25);
    ST (S_S_AGRD_00, SPR_S_AGRD, 0, 5, NULL, S_S_AGRD_00);
    ST (S_S_ARMR_00, SPR_S_ARMR, 0, -1, NULL, S_NULL);
    ST (S_S_ARMR_01, SPR_S_ARMR, 0, -1, NULL, S_NULL);
    ST (S_S_PLAY_19, SPR_S_PLAY, 0, 175, (actionf_p1)A_SpawnZombie, S_S_PLAY_19);
    ST (S_S_TNK1_00, SPR_S_TNK1, 0, 15, NULL, S_S_TNK1_01);
    ST (S_S_TNK1_01, SPR_S_TNK1, 1, 11, NULL, S_S_TNK1_02);
    ST (S_S_TNK1_02, SPR_S_TNK1, 2, 40, NULL, S_S_TNK1_00);
    ST (S_S_TNK2_00, SPR_S_TNK2, 0, 15, NULL, S_S_TNK2_01);
    ST (S_S_TNK2_01, SPR_S_TNK2, 1, 11, NULL, S_S_TNK2_02);
    ST (S_S_TNK2_02, SPR_S_TNK2, 2, 40, NULL, S_S_TNK2_00);
    ST (S_S_TNK3_00, SPR_S_TNK3, 0, 15, NULL, S_S_TNK3_01);
    ST (S_S_TNK3_01, SPR_S_TNK3, 1, 11, NULL, S_S_TNK3_02);
    ST (S_S_TNK3_02, SPR_S_TNK3, 2, 40, NULL, S_S_TNK3_00);
    ST (S_S_TNK4_00, SPR_S_TNK4, 0, 15, NULL, S_S_TNK4_01);
    ST (S_S_TNK4_01, SPR_S_TNK4, 1, 11, NULL, S_S_TNK4_02);
    ST (S_S_TNK4_02, SPR_S_TNK4, 2, 40, NULL, S_S_TNK4_00);
    ST (S_S_TNK5_00, SPR_S_TNK5, 0, 15, NULL, S_S_TNK5_01);
    ST (S_S_TNK5_01, SPR_S_TNK5, 1, 11, NULL, S_S_TNK5_02);
    ST (S_S_TNK5_02, SPR_S_TNK5, 2, 40, NULL, S_S_TNK5_00);
    ST (S_S_TNK6_00, SPR_S_TNK6, 0, 15, NULL, S_S_TNK6_01);
    ST (S_S_TNK6_01, SPR_S_TNK6, 1, 11, NULL, S_S_TNK6_02);
    ST (S_S_TNK6_02, SPR_S_TNK6, 2, 40, NULL, S_S_TNK6_00);
    ST (S_S_NEAL_00, SPR_S_NEAL, 0, 15, NULL, S_S_NEAL_01);
    ST (S_S_NEAL_01, SPR_S_NEAL, 1, 40, NULL, S_S_NEAL_00);
    ST (S_S_NEAL_02, SPR_S_NEAL, 2, 5, (actionf_p1)A_ShadowOn, S_S_NEAL_03);
    ST (S_S_NEAL_03, SPR_S_NEAL, 1, 4, (actionf_p1)A_Pain, S_S_NEAL_04);
    ST (S_S_NEAL_04, SPR_S_NEAL, 2, 5, (actionf_p1)A_ShadowOff, S_S_NEAL_00);
    ST (S_S_NEAL_05, SPR_S_NEAL, 1, 6, NULL, S_S_NEAL_06);
    ST (S_S_NEAL_06, SPR_S_NEAL, 2, 13, (actionf_p1)A_PeasantCrash, S_S_NEAL_05);
    ST (S_S_NEAL_07, SPR_S_NEAL, 3, 5, NULL, S_S_NEAL_08);
    ST (S_S_NEAL_08, SPR_S_NEAL, 4, 5, (actionf_p1)A_Scream, S_S_NEAL_09);
    ST (S_S_NEAL_09, SPR_S_NEAL, 5, 6, NULL, S_S_NEAL_10);
    ST (S_S_NEAL_10, SPR_S_NEAL, 6, 5, (actionf_p1)A_Fall, S_S_NEAL_11);
    ST (S_S_NEAL_11, SPR_S_NEAL, 7, 5, NULL, S_S_NEAL_12);
    ST (S_S_NEAL_12, SPR_S_NEAL, 8, 6, NULL, S_S_NEAL_13);
    ST (S_S_NEAL_13, SPR_S_NEAL, 9, -1, NULL, S_NULL);
    ST (S_S_BEGR_00, SPR_S_BEGR, 0, 10, (actionf_p1)A_Look, S_S_BEGR_00);
    ST (S_S_BEGR_01, SPR_S_BEGR, 0, 4, (actionf_p1)A_RandomWalk, S_S_BEGR_02);
    ST (S_S_BEGR_02, SPR_S_BEGR, 0, 4, (actionf_p1)A_RandomWalk, S_S_BEGR_03);
    ST (S_S_BEGR_03, SPR_S_BEGR, 1, 4, (actionf_p1)A_RandomWalk, S_S_BEGR_04);
    ST (S_S_BEGR_04, SPR_S_BEGR, 1, 4, (actionf_p1)A_RandomWalk, S_S_BEGR_05);
    ST (S_S_BEGR_05, SPR_S_BEGR, 2, 4, (actionf_p1)A_RandomWalk, S_S_BEGR_06);
    ST (S_S_BEGR_06, SPR_S_BEGR, 2, 4, (actionf_p1)A_RandomWalk, S_S_BEGR_01);
    ST (S_S_BEGR_07, SPR_S_BEGR, 3, 8, NULL, S_S_BEGR_08);
    ST (S_S_BEGR_08, SPR_S_BEGR, 4, 8, (actionf_p1)A_PeasantPunch, S_S_BEGR_09);
    ST (S_S_BEGR_09, SPR_S_BEGR, 4, 1, (actionf_p1)A_Chase, S_S_BEGR_10);
    ST (S_S_BEGR_10, SPR_S_BEGR, 3, 8, (actionf_p1)A_CheckTargetVisible, S_S_BEGR_07);
    ST (S_S_BEGR_11, SPR_S_BEGR, 0, 3, (actionf_p1)A_Pain, S_S_BEGR_12);
    ST (S_S_BEGR_12, SPR_S_BEGR, 0, 3, (actionf_p1)A_Chase, S_S_BEGR_07);
    ST (S_S_BEGR_13, SPR_S_BEGR, 5, 4, NULL, S_S_BEGR_14);
    ST (S_S_BEGR_14, SPR_S_BEGR, 6, 4, (actionf_p1)A_Scream, S_S_BEGR_15);
    ST (S_S_BEGR_15, SPR_S_BEGR, 7, 4, NULL, S_S_BEGR_16);
    ST (S_S_BEGR_16, SPR_S_BEGR, 8, 4, (actionf_p1)A_Fall, S_S_BEGR_17);
    ST (S_S_BEGR_17, SPR_S_BEGR, 9, 4, NULL, S_S_BEGR_18);
    ST (S_S_BEGR_18, SPR_S_BEGR, 10, 4, NULL, S_S_BEGR_19);
    ST (S_S_BEGR_19, SPR_S_BEGR, 11, 4, NULL, S_S_BEGR_20);
    ST (S_S_BEGR_20, SPR_S_BEGR, 12, 4, NULL, S_S_BEGR_21);
    ST (S_S_BEGR_21, SPR_S_BEGR, 13, -1, NULL, S_NULL);
    ST (S_S_BEGR_22, SPR_S_BEGR, 5, 5, NULL, S_S_GIBS_01);
    ST (S_S_HMN1_00, SPR_S_HMN1, 15, 5, (actionf_p1)A_Look, S_S_HMN1_00);
    ST (S_S_HMN1_11, SPR_S_HMN1, 0, 3, (actionf_p1)A_Chase, S_S_HMN1_12);
    ST (S_S_HMN1_12, SPR_S_HMN1, 0, 3, (actionf_p1)A_Chase, S_S_HMN1_13);
    ST (S_S_HMN1_13, SPR_S_HMN1, 1, 3, (actionf_p1)A_Chase, S_S_HMN1_14);
    ST (S_S_HMN1_14, SPR_S_HMN1, 1, 3, (actionf_p1)A_Chase, S_S_HMN1_15);
    ST (S_S_HMN1_15, SPR_S_HMN1, 2, 3, (actionf_p1)A_Chase, S_S_HMN1_16);
    ST (S_S_HMN1_16, SPR_S_HMN1, 2, 3, (actionf_p1)A_Chase, S_S_HMN1_17);
    ST (S_S_HMN1_17, SPR_S_HMN1, 3, 3, (actionf_p1)A_Chase, S_S_HMN1_18);
    ST (S_S_HMN1_18, SPR_S_HMN1, 3, 3, (actionf_p1)A_Chase, S_S_HMN1_11);
    ST (S_S_HMN1_19, SPR_S_HMN1, 4, 10, (actionf_p1)A_FaceTarget, S_S_HMN1_20);
    ST (S_S_HMN1_20, SPR_S_HMN1, 32773, 10, (actionf_p1)A_BulletAttack, S_S_HMN1_21);
    ST (S_S_HMN1_21, SPR_S_HMN1, 4, 10, (actionf_p1)A_BulletAttack, S_S_HMN1_11);
    ST (S_S_HMN1_22, SPR_S_HMN1, 14, 3, NULL, S_S_HMN1_23);
    ST (S_S_HMN1_23, SPR_S_HMN1, 14, 3, (actionf_p1)A_Pain, S_S_HMN1_11);
    ST (S_S_HMN1_24, SPR_S_HMN1, 6, 5, NULL, S_S_HMN1_25);
    ST (S_S_HMN1_25, SPR_S_HMN1, 7, 5, (actionf_p1)A_Scream, S_S_HMN1_26);
    ST (S_S_HMN1_26, SPR_S_HMN1, 8, 3, (actionf_p1)A_Fall, S_S_HMN1_27);
    ST (S_S_HMN1_27, SPR_S_HMN1, 9, 4, NULL, S_S_HMN1_28);
    ST (S_S_HMN1_28, SPR_S_HMN1, 10, 3, NULL, S_S_HMN1_29);
    ST (S_S_HMN1_29, SPR_S_HMN1, 11, 3, NULL, S_S_HMN1_30);
    ST (S_S_HMN1_30, SPR_S_HMN1, 12, 3, NULL, S_S_HMN1_31);
    ST (S_S_HMN1_31, SPR_S_HMN1, 13, -1, NULL, S_NULL);
    ST (S_S_RGIB_08, SPR_S_RGIB, 0, 4, NULL, S_S_RGIB_09);
    ST (S_S_RGIB_09, SPR_S_RGIB, 1, 4, (actionf_p1)A_XScream, S_S_RGIB_10);
    ST (S_S_RGIB_10, SPR_S_RGIB, 2, 3, (actionf_p1)A_Fall, S_S_RGIB_11);
    ST (S_S_RGIB_11, SPR_S_RGIB, 3, 3, NULL, S_S_RGIB_12);
    ST (S_S_RGIB_12, SPR_S_RGIB, 4, 3, NULL, S_S_RGIB_13);
    ST (S_S_RGIB_13, SPR_S_RGIB, 5, 3, NULL, S_S_RGIB_14);
    ST (S_S_RGIB_14, SPR_S_RGIB, 6, 3, NULL, S_S_RGIB_15);
    ST (S_S_RGIB_15, SPR_S_RGIB, 7, 1400, NULL, S_NULL);
    ST (S_S_LEDR_00, SPR_S_LEDR, 2, 5, (actionf_p1)A_Look, S_S_LEDR_00);
    ST (S_S_LEAD_04, SPR_S_LEAD, 0, 3, (actionf_p1)A_Chase, S_S_LEAD_05);
    ST (S_S_LEAD_05, SPR_S_LEAD, 0, 3, (actionf_p1)A_Chase, S_S_LEAD_06);
    ST (S_S_LEAD_06, SPR_S_LEAD, 1, 3, (actionf_p1)A_Chase, S_S_LEAD_07);
    ST (S_S_LEAD_07, SPR_S_LEAD, 1, 3, (actionf_p1)A_Chase, S_S_LEAD_08);
    ST (S_S_LEAD_08, SPR_S_LEAD, 2, 3, (actionf_p1)A_Chase, S_S_LEAD_09);
    ST (S_S_LEAD_09, SPR_S_LEAD, 2, 3, (actionf_p1)A_Chase, S_S_LEAD_10);
    ST (S_S_LEAD_10, SPR_S_LEAD, 3, 3, (actionf_p1)A_Chase, S_S_LEAD_11);
    ST (S_S_LEAD_11, SPR_S_LEAD, 3, 3, (actionf_p1)A_Chase, S_S_LEAD_04);
    ST (S_S_LEAD_12, SPR_S_LEAD, 4, 2, (actionf_p1)A_FaceTarget, S_S_LEAD_13);
    ST (S_S_LEAD_13, SPR_S_LEAD, 32773, 2, (actionf_p1)A_BulletAttack, S_S_LEAD_14);
    ST (S_S_LEAD_14, SPR_S_LEAD, 4, 1, (actionf_p1)A_CheckTargetVisible, S_S_LEAD_12);
    ST (S_S_LEAD_15, SPR_S_LEAD, 24, 3, NULL, S_S_LEAD_16);
    ST (S_S_LEAD_16, SPR_S_LEAD, 24, 3, (actionf_p1)A_Pain, S_S_LEAD_04);
    ST (S_S_LEAD_17, SPR_S_LEAD, 4, 4, (actionf_p1)A_FaceTarget, S_S_LEAD_18);
    ST (S_S_LEAD_18, SPR_S_LEAD, 32773, 4, (actionf_p1)A_BulletAttack, S_S_LEAD_19);
    ST (S_S_LEAD_19, SPR_S_LEAD, 4, 2, (actionf_p1)A_CheckTargetVisible, S_S_LEAD_17);
    ST (S_S_LEAD_20, SPR_S_LEAD, 6, 5, NULL, S_S_LEAD_21);
    ST (S_S_LEAD_21, SPR_S_LEAD, 7, 5, (actionf_p1)A_Scream, S_S_LEAD_22);
    ST (S_S_LEAD_22, SPR_S_LEAD, 8, 4, NULL, S_S_LEAD_23);
    ST (S_S_LEAD_23, SPR_S_LEAD, 9, 4, NULL, S_S_LEAD_24);
    ST (S_S_LEAD_24, SPR_S_LEAD, 10, 3, NULL, S_S_LEAD_25);
    ST (S_S_LEAD_25, SPR_S_LEAD, 11, 3, (actionf_p1)A_Fall, S_S_LEAD_26);
    ST (S_S_LEAD_26, SPR_S_LEAD, 12, 3, NULL, S_S_LEAD_27);
    ST (S_S_LEAD_27, SPR_S_LEAD, 13, 3, NULL, S_S_LEAD_28);
    ST (S_S_LEAD_28, SPR_S_LEAD, 14, 3, NULL, S_S_LEAD_29);
    ST (S_S_LEAD_29, SPR_S_LEAD, 15, 3, NULL, S_S_LEAD_30);
    ST (S_S_LEAD_30, SPR_S_LEAD, 16, 3, NULL, S_S_LEAD_31);
    ST (S_S_LEAD_31, SPR_S_LEAD, 17, 3, NULL, S_S_LEAD_32);
    ST (S_S_LEAD_32, SPR_S_LEAD, 18, 3, NULL, S_S_LEAD_33);
    ST (S_S_LEAD_33, SPR_S_LEAD, 19, 3, NULL, S_S_LEAD_34);
    ST (S_S_LEAD_34, SPR_S_LEAD, 20, 3, NULL, S_S_LEAD_35);
    ST (S_S_LEAD_35, SPR_S_LEAD, 21, 3, NULL, S_S_LEAD_36);
    ST (S_S_LEAD_36, SPR_S_LEAD, 22, 3, (actionf_p1)A_SpawnSpectreD, S_S_LEAD_37);
    ST (S_S_LEAD_37, SPR_S_LEAD, 23, -1, NULL, S_NULL);
    ST (S_S_PUFY_04, SPR_S_PUFY, 1, 4, NULL, S_S_PUFY_05);
    ST (S_S_PUFY_05, SPR_S_PUFY, 2, 4, NULL, S_S_PUFY_06);
    ST (S_S_PUFY_06, SPR_S_PUFY, 1, 4, NULL, S_S_PUFY_07);
    ST (S_S_PUFY_07, SPR_S_PUFY, 2, 4, NULL, S_S_PUFY_08);
    ST (S_S_PUFY_08, SPR_S_PUFY, 3, 4, NULL, S_NULL);
    ST (S_S_ROB1_00, SPR_S_ROB1, 0, 10, (actionf_p1)A_Look, S_S_ROB1_01);
    ST (S_S_ROB1_01, SPR_S_ROB1, 0, 10, (actionf_p1)A_Look, S_S_ROB1_00);
    ST (S_S_ROB1_02, SPR_S_ROB1, 1, 3, (actionf_p1)A_Chase, S_S_ROB1_03);
    ST (S_S_ROB1_03, SPR_S_ROB1, 1, 3, (actionf_p1)A_Chase, S_S_ROB1_04);
    ST (S_S_ROB1_04, SPR_S_ROB1, 2, 3, (actionf_p1)A_Chase, S_S_ROB1_05);
    ST (S_S_ROB1_05, SPR_S_ROB1, 2, 3, (actionf_p1)A_Chase, S_S_ROB1_06);
    ST (S_S_ROB1_06, SPR_S_ROB1, 3, 3, (actionf_p1)A_Chase, S_S_ROB1_07);
    ST (S_S_ROB1_07, SPR_S_ROB1, 3, 3, (actionf_p1)A_Chase, S_S_ROB1_08);
    ST (S_S_ROB1_08, SPR_S_ROB1, 4, 3, (actionf_p1)A_Chase, S_S_ROB1_09);
    ST (S_S_ROB1_09, SPR_S_ROB1, 4, 3, (actionf_p1)A_Chase, S_S_ROB1_02);
    ST (S_S_ROB1_10, SPR_S_ROB1, 7, 6, (actionf_p1)A_FaceTarget, S_S_ROB1_11);
    ST (S_S_ROB1_11, SPR_S_ROB1, 8, 8, (actionf_p1)A_RobotMelee, S_S_ROB1_12);
    ST (S_S_ROB1_12, SPR_S_ROB1, 7, 6, NULL, S_S_ROB1_02);
    ST (S_S_ROB1_13, SPR_S_ROB1, 5, 8, (actionf_p1)A_FaceTarget, S_S_ROB1_14);
    ST (S_S_ROB1_14, SPR_S_ROB1, 32774, 11, (actionf_p1)A_ReaverAttack, S_S_ROB1_02);
    ST (S_S_ROB1_15, SPR_S_ROB1, 0, 2, NULL, S_S_ROB1_16);
    ST (S_S_ROB1_16, SPR_S_ROB1, 0, 2, (actionf_p1)A_Pain, S_S_ROB1_02);
    ST (S_S_ROB1_17, SPR_S_ROB1, 32777, 6, NULL, S_S_ROB1_18);
    ST (S_S_ROB1_18, SPR_S_ROB1, 32778, 6, (actionf_p1)A_Scream, S_S_ROB1_19);
    ST (S_S_ROB1_19, SPR_S_ROB1, 32779, 5, NULL, S_S_ROB1_20);
    ST (S_S_ROB1_20, SPR_S_ROB1, 32780, 5, (actionf_p1)A_Fall, S_S_ROB1_21);
    ST (S_S_ROB1_21, SPR_S_ROB1, 32781, 5, NULL, S_S_ROB1_22);
    ST (S_S_ROB1_22, SPR_S_ROB1, 32782, 5, NULL, S_S_ROB1_23);
    ST (S_S_ROB1_23, SPR_S_ROB1, 32783, 5, NULL, S_S_ROB1_24);
    ST (S_S_ROB1_24, SPR_S_ROB1, 32784, 6, (actionf_p1)A_DeathExplode3, S_S_ROB1_25);
    ST (S_S_ROB1_25, SPR_S_ROB1, 17, -1, NULL, S_NULL);
    ST (S_S_ROB1_26, SPR_S_ROB1, 32779, 5, NULL, S_S_ROB1_27);
    ST (S_S_ROB1_27, SPR_S_ROB1, 32780, 5, (actionf_p1)A_XScream, S_S_ROB1_28);
    ST (S_S_ROB1_28, SPR_S_ROB1, 32781, 5, NULL, S_S_ROB1_29);
    ST (S_S_ROB1_29, SPR_S_ROB1, 32782, 5, (actionf_p1)A_Fall, S_S_ROB1_30);
    ST (S_S_ROB1_30, SPR_S_ROB1, 32783, 5, NULL, S_S_ROB1_31);
    ST (S_S_ROB1_31, SPR_S_ROB1, 32784, 5, (actionf_p1)A_DeathExplode3, S_S_ROB1_32);
    ST (S_S_ROB1_32, SPR_S_ROB1, 17, -1, NULL, S_NULL);
    ST (S_S_AGRD_01, SPR_S_AGRD, 0, 5, (actionf_p1)A_Look, S_S_AGRD_01);
    ST (S_S_AGRD_12, SPR_S_AGRD, 0, 6, (actionf_p1)A_ModifyVisibility, S_S_AGRD_14);
    ST (S_S_AGRD_13, SPR_S_AGRD, 0, 6, NULL, S_S_AGRD_14);
    ST (S_S_AGRD_14, SPR_S_AGRD, 1, 6, (actionf_p1)A_Chase, S_S_AGRD_15);
    ST (S_S_AGRD_15, SPR_S_AGRD, 2, 6, (actionf_p1)A_Chase, S_S_AGRD_16);
    ST (S_S_AGRD_16, SPR_S_AGRD, 3, 6, (actionf_p1)A_Chase, S_S_AGRD_13);
    ST (S_S_AGRD_17, SPR_S_AGRD, 4, 8, (actionf_p1)A_FaceTarget, S_S_AGRD_18);
    ST (S_S_AGRD_18, SPR_S_AGRD, 5, 4, (actionf_p1)A_BulletAttack, S_S_AGRD_19);
    ST (S_S_AGRD_19, SPR_S_AGRD, 4, 4, (actionf_p1)A_BulletAttack, S_S_AGRD_20);
    ST (S_S_AGRD_20, SPR_S_AGRD, 5, 6, (actionf_p1)A_BulletAttack, S_S_AGRD_13);
    ST (S_S_AGRD_21, SPR_S_AGRD, 14, 0, (actionf_p1)A_ShadowOn, S_S_AGRD_22);
    ST (S_S_AGRD_22, SPR_S_AGRD, 14, 8, (actionf_p1)A_Pain, S_S_AGRD_12);
    ST (S_S_AGRD_23, SPR_S_AGRD, 14, 8, (actionf_p1)A_Pain, S_S_AGRD_13);
    ST (S_S_AGRD_24, SPR_S_AGRD, 6, 4, NULL, S_S_AGRD_25);
    ST (S_S_AGRD_25, SPR_S_AGRD, 7, 4, (actionf_p1)A_Scream, S_S_AGRD_26);
    ST (S_S_AGRD_26, SPR_S_AGRD, 8, 4, NULL, S_S_AGRD_27);
    ST (S_S_AGRD_27, SPR_S_AGRD, 9, 3, NULL, S_S_AGRD_28);
    ST (S_S_AGRD_28, SPR_S_AGRD, 10, 3, (actionf_p1)A_Fall, S_S_AGRD_29);
    ST (S_S_AGRD_29, SPR_S_AGRD, 11, 3, NULL, S_S_AGRD_30);
    ST (S_S_AGRD_30, SPR_S_AGRD, 12, 3, NULL, S_S_AGRD_31);
    ST (S_S_AGRD_31, SPR_S_AGRD, 13, 1400, NULL, S_S_GIBS_20);
    ST (S_S_GIBS_10, SPR_S_GIBS, 0, 5, (actionf_p1)A_Fall, S_S_GIBS_11);
    ST (S_S_GIBS_11, SPR_S_GIBS, 1, 5, NULL, S_S_GIBS_12);
    ST (S_S_GIBS_12, SPR_S_GIBS, 2, 5, NULL, S_S_GIBS_13);
    ST (S_S_GIBS_13, SPR_S_GIBS, 3, 4, NULL, S_S_GIBS_14);
    ST (S_S_GIBS_14, SPR_S_GIBS, 4, 4, (actionf_p1)A_XScream, S_S_GIBS_15);
    ST (S_S_GIBS_15, SPR_S_GIBS, 5, 4, NULL, S_S_GIBS_16);
    ST (S_S_GIBS_16, SPR_S_GIBS, 6, 4, NULL, S_S_GIBS_17);
    ST (S_S_GIBS_17, SPR_S_GIBS, 7, 4, NULL, S_S_GIBS_18);
    ST (S_S_GIBS_18, SPR_S_GIBS, 8, 5, NULL, S_S_GIBS_19);
    ST (S_S_GIBS_19, SPR_S_GIBS, 9, 5, NULL, S_S_GIBS_20);
    ST (S_S_GIBS_20, SPR_S_GIBS, 10, 5, NULL, S_S_GIBS_21);
    ST (S_S_GIBS_21, SPR_S_GIBS, 11, 1400, NULL, S_NULL);
    ST (S_S_PGRD_00, SPR_S_PGRD, 0, 5, (actionf_p1)A_Look, S_S_PGRD_00);
    ST (S_S_PGRD_04, SPR_S_PGRD, 0, 3, (actionf_p1)A_Chase, S_S_PGRD_05);
    ST (S_S_PGRD_05, SPR_S_PGRD, 0, 3, (actionf_p1)A_Chase, S_S_PGRD_06);
    ST (S_S_PGRD_06, SPR_S_PGRD, 1, 3, (actionf_p1)A_Chase, S_S_PGRD_07);
    ST (S_S_PGRD_07, SPR_S_PGRD, 1, 3, (actionf_p1)A_Chase, S_S_PGRD_08);
    ST (S_S_PGRD_08, SPR_S_PGRD, 2, 3, (actionf_p1)A_Chase, S_S_PGRD_09);
    ST (S_S_PGRD_09, SPR_S_PGRD, 2, 3, (actionf_p1)A_Chase, S_S_PGRD_10);
    ST (S_S_PGRD_10, SPR_S_PGRD, 3, 3, (actionf_p1)A_Chase, S_S_PGRD_11);
    ST (S_S_PGRD_11, SPR_S_PGRD, 3, 3, (actionf_p1)A_Chase, S_S_PGRD_04);
    ST (S_S_PGRD_12, SPR_S_PGRD, 4, 8, (actionf_p1)A_FaceTarget, S_S_PGRD_13);
    ST (S_S_PGRD_13, SPR_S_PGRD, 5, 8, (actionf_p1)A_RobotMelee, S_S_PGRD_04);
    ST (S_S_PGRD_14, SPR_S_PGRD, 32774, 8, (actionf_p1)A_FaceTarget, S_S_PGRD_15);
    ST (S_S_PGRD_15, SPR_S_PGRD, 32775, 8, (actionf_p1)A_TemplarMauler, S_S_PGRD_04);
    ST (S_S_PGRD_16, SPR_S_PGRD, 0, 2, NULL, S_S_PGRD_17);
    ST (S_S_PGRD_17, SPR_S_PGRD, 0, 2, (actionf_p1)A_Pain, S_S_PGRD_04);
    ST (S_S_PGRD_18, SPR_S_PGRD, 32776, 4, NULL, S_S_PGRD_19);
    ST (S_S_PGRD_19, SPR_S_PGRD, 32777, 4, (actionf_p1)A_Scream, S_S_PGRD_20);
    ST (S_S_PGRD_20, SPR_S_PGRD, 32778, 4, NULL, S_S_PGRD_21);
    ST (S_S_PGRD_21, SPR_S_PGRD, 32779, 4, (actionf_p1)A_Fall, S_S_PGRD_22);
    ST (S_S_PGRD_22, SPR_S_PGRD, 32780, 4, NULL, S_S_PGRD_23);
    ST (S_S_PGRD_23, SPR_S_PGRD, 32781, 4, NULL, S_S_PGRD_24);
    ST (S_S_PGRD_24, SPR_S_PGRD, 14, 4, NULL, S_S_PGRD_25);
    ST (S_S_PGRD_25, SPR_S_PGRD, 15, 4, NULL, S_S_PGRD_26);
    ST (S_S_PGRD_26, SPR_S_PGRD, 16, 4, NULL, S_S_PGRD_27);
    ST (S_S_PGRD_27, SPR_S_PGRD, 17, 4, NULL, S_S_PGRD_28);
    ST (S_S_PGRD_28, SPR_S_PGRD, 18, 3, NULL, S_S_PGRD_29);
    ST (S_S_PGRD_29, SPR_S_PGRD, 19, 3, NULL, S_S_PGRD_30);
    ST (S_S_PGRD_30, SPR_S_PGRD, 20, 3, NULL, S_S_PGRD_31);
    ST (S_S_PGRD_31, SPR_S_PGRD, 21, 3, NULL, S_S_PGRD_32);
    ST (S_S_PGRD_32, SPR_S_PGRD, 22, 3, NULL, S_S_PGRD_33);
    ST (S_S_PGRD_33, SPR_S_PGRD, 23, 3, NULL, S_S_PGRD_34);
    ST (S_S_PGRD_34, SPR_S_PGRD, 24, 3, NULL, S_S_PGRD_35);
    ST (S_S_PGRD_35, SPR_S_PGRD, 25, 3, NULL, S_S_PGRD_36);
    ST (S_S_PGRD_36, SPR_S_PGRD, 26, 3, NULL, S_S_PGRD_37);
    ST (S_S_PGRD_37, SPR_S_PGRD, 27, -1, NULL, S_NULL);
    ST (S_S_ROB2_00, SPR_S_ROB2, 16, 10, (actionf_p1)A_Look, S_S_ROB2_00);
    ST (S_S_ROB2_01, SPR_S_ROB2, 0, 3, (actionf_p1)A_Chase, S_S_ROB2_02);
    ST (S_S_ROB2_02, SPR_S_ROB2, 0, 3, (actionf_p1)A_Chase, S_S_ROB2_03);
    ST (S_S_ROB2_03, SPR_S_ROB2, 1, 3, (actionf_p1)A_Chase, S_S_ROB2_04);
    ST (S_S_ROB2_04, SPR_S_ROB2, 1, 3, (actionf_p1)A_Chase, S_S_ROB2_05);
    ST (S_S_ROB2_05, SPR_S_ROB2, 2, 3, (actionf_p1)A_Chase, S_S_ROB2_06);
    ST (S_S_ROB2_06, SPR_S_ROB2, 2, 3, (actionf_p1)A_Chase, S_S_ROB2_07);
    ST (S_S_ROB2_07, SPR_S_ROB2, 3, 3, (actionf_p1)A_Chase, S_S_ROB2_08);
    ST (S_S_ROB2_08, SPR_S_ROB2, 3, 3, (actionf_p1)A_Chase, S_S_ROB2_01);
    ST (S_S_ROB2_09, SPR_S_ROB2, 4, 3, (actionf_p1)A_FaceTarget, S_S_ROB2_10);
    ST (S_S_ROB2_10, SPR_S_ROB2, 32773, 2, (actionf_p1)A_CrusaderAttack, S_S_ROB2_11);
    ST (S_S_ROB2_11, SPR_S_ROB2, 32772, 2, (actionf_p1)A_CrusaderLeft, S_S_ROB2_12);
    ST (S_S_ROB2_12, SPR_S_ROB2, 32773, 3, (actionf_p1)A_CrusaderLeft, S_S_ROB2_13);
    ST (S_S_ROB2_13, SPR_S_ROB2, 32772, 2, (actionf_p1)A_CrusaderLeft, S_S_ROB2_14);
    ST (S_S_ROB2_14, SPR_S_ROB2, 32773, 2, (actionf_p1)A_CrusaderLeft, S_S_ROB2_15);
    ST (S_S_ROB2_15, SPR_S_ROB2, 32772, 2, (actionf_p1)A_CrusaderRight, S_S_ROB2_16);
    ST (S_S_ROB2_16, SPR_S_ROB2, 32773, 2, (actionf_p1)A_CrusaderRight, S_S_ROB2_17);
    ST (S_S_ROB2_17, SPR_S_ROB2, 32772, 2, (actionf_p1)A_CrusaderRight, S_S_ROB2_18);
    ST (S_S_ROB2_18, SPR_S_ROB2, 5, 2, (actionf_p1)A_CheckTargetVisible2, S_S_ROB2_09);
    ST (S_S_ROB2_19, SPR_S_ROB2, 3, 1, (actionf_p1)A_Pain, S_S_ROB2_01);
    ST (S_S_ROB2_20, SPR_S_ROB2, 6, 3, (actionf_p1)A_Scream, S_S_ROB2_21);
    ST (S_S_ROB2_21, SPR_S_ROB2, 7, 5, NULL, S_S_ROB2_22);
    ST (S_S_ROB2_22, SPR_S_ROB2, 32776, 4, NULL, S_S_ROB2_23);
    ST (S_S_ROB2_23, SPR_S_ROB2, 32777, 4, (actionf_p1)A_DeathExplode2, S_S_ROB2_24);
    ST (S_S_ROB2_24, SPR_S_ROB2, 32778, 4, (actionf_p1)A_Fall, S_S_ROB2_25);
    ST (S_S_ROB2_25, SPR_S_ROB2, 11, 4, (actionf_p1)A_DeathExplode2, S_S_ROB2_26);
    ST (S_S_ROB2_26, SPR_S_ROB2, 12, 4, NULL, S_S_ROB2_27);
    ST (S_S_ROB2_27, SPR_S_ROB2, 13, 4, NULL, S_S_ROB2_28);
    ST (S_S_ROB2_28, SPR_S_ROB2, 14, 4, (actionf_p1)A_DeathExplode2, S_S_ROB2_29);
    ST (S_S_ROB2_29, SPR_S_ROB2, 15, -1, (actionf_p1)A_BossDeath, S_NULL);
    ST (S_S_MLDR_00, SPR_S_MLDR, 0, 10, (actionf_p1)A_Look, S_S_MLDR_00);
    ST (S_S_MLDR_01, SPR_S_MLDR, 0, 3, (actionf_p1)A_Chase, S_S_MLDR_02);
    ST (S_S_MLDR_02, SPR_S_MLDR, 0, 3, (actionf_p1)A_Chase, S_S_MLDR_03);
    ST (S_S_MLDR_03, SPR_S_MLDR, 1, 3, (actionf_p1)A_Chase, S_S_MLDR_04);
    ST (S_S_MLDR_04, SPR_S_MLDR, 1, 3, (actionf_p1)A_Chase, S_S_MLDR_05);
    ST (S_S_MLDR_05, SPR_S_MLDR, 2, 3, (actionf_p1)A_Chase, S_S_MLDR_06);
    ST (S_S_MLDR_06, SPR_S_MLDR, 2, 3, (actionf_p1)A_Chase, S_S_MLDR_07);
    ST (S_S_MLDR_07, SPR_S_MLDR, 3, 3, (actionf_p1)A_Chase, S_S_MLDR_08);
    ST (S_S_MLDR_08, SPR_S_MLDR, 3, 3, (actionf_p1)A_Chase, S_S_MLDR_01);
    ST (S_S_MLDR_09, SPR_S_MLDR, 4, 3, (actionf_p1)A_FaceTarget, S_S_MLDR_10);
    ST (S_S_MLDR_10, SPR_S_MLDR, 32773, 2, (actionf_p1)A_BishopAttack, S_S_MLDR_01);
    ST (S_S_MLDR_11, SPR_S_MLDR, 3, 1, (actionf_p1)A_Pain, S_S_MLDR_01);
    ST (S_S_MLDR_12, SPR_S_MLDR, 32774, 3, NULL, S_S_MLDR_13);
    ST (S_S_MLDR_13, SPR_S_MLDR, 32775, 5, (actionf_p1)A_Scream, S_S_MLDR_14);
    ST (S_S_MLDR_14, SPR_S_MLDR, 32776, 4, NULL, S_S_MLDR_15);
    ST (S_S_MLDR_15, SPR_S_MLDR, 32777, 4, (actionf_p1)A_DeathExplode2, S_S_MLDR_16);
    ST (S_S_MLDR_16, SPR_S_MLDR, 32778, 4, NULL, S_S_MLDR_17);
    ST (S_S_MLDR_17, SPR_S_MLDR, 32779, 4, NULL, S_S_MLDR_18);
    ST (S_S_MLDR_18, SPR_S_MLDR, 32780, 4, (actionf_p1)A_Fall, S_S_MLDR_19);
    ST (S_S_MLDR_19, SPR_S_MLDR, 32781, 4, NULL, S_S_MLDR_20);
    ST (S_S_MLDR_20, SPR_S_MLDR, 32782, 4, NULL, S_S_MLDR_21);
    ST (S_S_MLDR_21, SPR_S_MLDR, 32783, 4, NULL, S_S_MLDR_22);
    ST (S_S_MLDR_22, SPR_S_MLDR, 32784, 4, NULL, S_S_MLDR_23);
    ST (S_S_MLDR_23, SPR_S_MLDR, 32785, 4, NULL, S_S_MLDR_24);
    ST (S_S_MLDR_24, SPR_S_MLDR, 32786, 4, NULL, S_S_MLDR_25);
    ST (S_S_MLDR_25, SPR_S_MLDR, 32787, 4, NULL, S_S_MLDR_26);
    ST (S_S_MLDR_26, SPR_S_MLDR, 32788, 4, NULL, S_S_MLDR_27);
    ST (S_S_MLDR_27, SPR_S_MLDR, 21, 4, (actionf_p1)A_SpawnSpectreB, S_NULL);
    ST (S_S_ORCL_00, SPR_S_ORCL, 0, -1, NULL, S_NULL);
    ST (S_S_ORCL_01, SPR_S_ORCL, 1, 5, NULL, S_S_ORCL_02);
    ST (S_S_ORCL_02, SPR_S_ORCL, 2, 5, NULL, S_S_ORCL_03);
    ST (S_S_ORCL_03, SPR_S_ORCL, 3, 5, NULL, S_S_ORCL_04);
    ST (S_S_ORCL_04, SPR_S_ORCL, 4, 5, NULL, S_S_ORCL_05);
    ST (S_S_ORCL_05, SPR_S_ORCL, 5, 5, NULL, S_S_ORCL_06);
    ST (S_S_ORCL_06, SPR_S_ORCL, 6, 5, NULL, S_S_ORCL_07);
    ST (S_S_ORCL_07, SPR_S_ORCL, 7, 5, NULL, S_S_ORCL_08);
    ST (S_S_ORCL_08, SPR_S_ORCL, 8, 5, NULL, S_S_ORCL_09);
    ST (S_S_ORCL_09, SPR_S_ORCL, 9, 5, NULL, S_S_ORCL_10);
    ST (S_S_ORCL_10, SPR_S_ORCL, 10, 5, NULL, S_S_ORCL_11);
    ST (S_S_ORCL_11, SPR_S_ORCL, 11, 5, (actionf_p1)A_Fall, S_S_ORCL_12);
    ST (S_S_ORCL_12, SPR_S_ORCL, 12, 5, NULL, S_S_ORCL_13);
    ST (S_S_ORCL_13, SPR_S_ORCL, 13, 5, (actionf_p1)A_AlertSpectreC, S_S_ORCL_14);
    ST (S_S_ORCL_14, SPR_S_ORCL, 14, 5, NULL, S_S_ORCL_15);
    ST (S_S_ORCL_15, SPR_S_ORCL, 15, 5, NULL, S_S_ORCL_16);
    ST (S_S_ORCL_16, SPR_S_ORCL, 16, -1, NULL, S_NULL);
    ST (S_S_PRST_00, SPR_S_PRST, 0, 10, (actionf_p1)A_Look, S_S_PRST_01);
    ST (S_S_PRST_01, SPR_S_PRST, 1, 10, (actionf_p1)A_FloatWeave, S_S_PRST_00);
    ST (S_S_PRST_02, SPR_S_PRST, 0, 4, (actionf_p1)A_Chase, S_S_PRST_03);
    ST (S_S_PRST_03, SPR_S_PRST, 0, 4, (actionf_p1)A_FloatWeave, S_S_PRST_04);
    ST (S_S_PRST_04, SPR_S_PRST, 1, 4, (actionf_p1)A_Chase, S_S_PRST_05);
    ST (S_S_PRST_05, SPR_S_PRST, 1, 4, (actionf_p1)A_FloatWeave, S_S_PRST_06);
    ST (S_S_PRST_06, SPR_S_PRST, 2, 4, (actionf_p1)A_Chase, S_S_PRST_07);
    ST (S_S_PRST_07, SPR_S_PRST, 2, 4, (actionf_p1)A_FloatWeave, S_S_PRST_08);
    ST (S_S_PRST_08, SPR_S_PRST, 3, 4, (actionf_p1)A_Chase, S_S_PRST_09);
    ST (S_S_PRST_09, SPR_S_PRST, 3, 4, (actionf_p1)A_FloatWeave, S_S_PRST_02);
    ST (S_S_PRST_10, SPR_S_PRST, 4, 4, (actionf_p1)A_FaceTarget, S_S_PRST_11);
    ST (S_S_PRST_11, SPR_S_PRST, 5, 4, (actionf_p1)A_BossMeleeAtk, S_S_PRST_12);
    ST (S_S_PRST_12, SPR_S_PRST, 4, 4, (actionf_p1)A_FloatWeave, S_S_PRST_02);
    ST (S_S_PRST_13, SPR_S_PRST, 4, 4, (actionf_p1)A_FaceTarget, S_S_PRST_14);
    ST (S_S_PRST_14, SPR_S_PRST, 5, 4, (actionf_p1)A_FireHookShot, S_S_PRST_15);
    ST (S_S_PRST_15, SPR_S_PRST, 4, 4, (actionf_p1)A_FloatWeave, S_S_PRST_02);
    ST (S_S_PDED_00, SPR_S_PDED, 0, 6, NULL, S_S_PDED_01);
    ST (S_S_PDED_01, SPR_S_PDED, 1, 6, (actionf_p1)A_Scream, S_S_PDED_02);
    ST (S_S_PDED_02, SPR_S_PDED, 2, 6, NULL, S_S_PDED_03);
    ST (S_S_PDED_03, SPR_S_PDED, 3, 6, (actionf_p1)A_Fall, S_S_PDED_04);
    ST (S_S_PDED_04, SPR_S_PDED, 4, 6, NULL, S_S_PDED_05);
    ST (S_S_PDED_05, SPR_S_PDED, 5, 5, NULL, S_S_PDED_06);
    ST (S_S_PDED_06, SPR_S_PDED, 6, 5, NULL, S_S_PDED_07);
    ST (S_S_PDED_07, SPR_S_PDED, 7, 5, NULL, S_S_PDED_08);
    ST (S_S_PDED_08, SPR_S_PDED, 8, 5, NULL, S_S_PDED_09);
    ST (S_S_PDED_09, SPR_S_PDED, 9, 5, NULL, S_S_PDED_10);
    ST (S_S_PDED_10, SPR_S_PDED, 8, 5, NULL, S_S_PDED_11);
    ST (S_S_PDED_11, SPR_S_PDED, 9, 5, NULL, S_S_PDED_12);
    ST (S_S_PDED_12, SPR_S_PDED, 8, 5, NULL, S_S_PDED_14);
    ST (S_S_PDED_14, SPR_S_PDED, 10, 5, NULL, S_S_PDED_15);
    ST (S_S_PDED_15, SPR_S_PDED, 11, 5, NULL, S_S_PDED_16);
    ST (S_S_PDED_16, SPR_S_PDED, 12, 4, NULL, S_S_PDED_17);
    ST (S_S_PDED_17, SPR_S_PDED, 13, 4, NULL, S_S_PDED_18);
    ST (S_S_PDED_18, SPR_S_PDED, 14, 4, NULL, S_S_PDED_19);
    ST (S_S_PDED_19, SPR_S_PDED, 15, 4, NULL, S_S_PDED_20);
    ST (S_S_PDED_20, SPR_S_PDED, 16, 4, (actionf_p1)A_SpawnSpectreE, S_S_PDED_21);
    ST (S_S_PDED_21, SPR_S_PDED, 17, 4, NULL, S_S_PDED_22);
    ST (S_S_PDED_22, SPR_S_PDED, 18, 4, NULL, S_S_PDED_23);
    ST (S_S_PDED_23, SPR_S_PDED, 19, -1, NULL, S_NULL);
    ST (S_S_ALN1_00, SPR_S_ALN1, 0, 10, (actionf_p1)A_Look, S_S_ALN1_01);
    ST (S_S_ALN1_01, SPR_S_ALN1, 1, 10, (actionf_p1)A_FloatWeave, S_S_ALN1_00);
    ST (S_S_ALN1_02, SPR_S_ALN1, 32768, 4, (actionf_p1)A_Chase, S_S_ALN1_03);
    ST (S_S_ALN1_03, SPR_S_ALN1, 32769, 4, (actionf_p1)A_Chase, S_S_ALN1_04);
    ST (S_S_ALN1_04, SPR_S_ALN1, 32770, 4, (actionf_p1)A_FloatWeave, S_S_ALN1_05);
    ST (S_S_ALN1_05, SPR_S_ALN1, 32771, 4, (actionf_p1)A_Chase, S_S_ALN1_06);
    ST (S_S_ALN1_06, SPR_S_ALN1, 32772, 4, (actionf_p1)A_Chase, S_S_ALN1_07);
    ST (S_S_ALN1_07, SPR_S_ALN1, 32773, 4, (actionf_p1)A_Chase, S_S_ALN1_08);
    ST (S_S_ALN1_08, SPR_S_ALN1, 32774, 4, (actionf_p1)A_FloatWeave, S_S_ALN1_09);
    ST (S_S_ALN1_09, SPR_S_ALN1, 32775, 4, (actionf_p1)A_Chase, S_S_ALN1_10);
    ST (S_S_ALN1_10, SPR_S_ALN1, 32776, 4, (actionf_p1)A_Chase, S_S_ALN1_11);
    ST (S_S_ALN1_11, SPR_S_ALN1, 32777, 4, (actionf_p1)A_Chase, S_S_ALN1_12);
    ST (S_S_ALN1_12, SPR_S_ALN1, 32778, 4, (actionf_p1)A_FloatWeave, S_S_ALN1_02);
    ST (S_S_ALN1_13, SPR_S_ALN1, 32777, 4, (actionf_p1)A_FaceTarget, S_S_ALN1_14);
    ST (S_S_ALN1_14, SPR_S_ALN1, 32776, 4, (actionf_p1)A_BossMeleeAtk, S_S_ALN1_15);
    ST (S_S_ALN1_15, SPR_S_ALN1, 32775, 4, NULL, S_S_ALN1_04);
    ST (S_S_ALN1_16, SPR_S_ALN1, 32777, 4, (actionf_p1)A_FaceTarget, S_S_ALN1_17);
    ST (S_S_ALN1_17, SPR_S_ALN1, 32776, 4, (actionf_p1)A_ProgrammerAttack, S_S_ALN1_18);
    ST (S_S_ALN1_18, SPR_S_ALN1, 32775, 4, NULL, S_S_ALN1_12);
    ST (S_S_ALN1_19, SPR_S_ALN1, 9, 2, (actionf_p1)A_Pain, S_S_ALN1_08);
    ST (S_S_AL1P_00, SPR_S_AL1P, 32768, 6, (actionf_p1)A_NodeChunk, S_S_AL1P_01);
    ST (S_S_AL1P_01, SPR_S_AL1P, 32769, 6, (actionf_p1)A_Scream, S_S_AL1P_02);
    ST (S_S_AL1P_02, SPR_S_AL1P, 32770, 6, (actionf_p1)A_NodeChunk, S_S_AL1P_03);
    ST (S_S_AL1P_03, SPR_S_AL1P, 32771, 6, NULL, S_S_AL1P_04);
    ST (S_S_AL1P_04, SPR_S_AL1P, 32772, 6, NULL, S_S_AL1P_05);
    ST (S_S_AL1P_05, SPR_S_AL1P, 32773, 6, (actionf_p1)A_NodeChunk, S_S_AL1P_06);
    ST (S_S_AL1P_06, SPR_S_AL1P, 32774, 6, NULL, S_S_AL1P_07);
    ST (S_S_AL1P_07, SPR_S_AL1P, 32775, 6, (actionf_p1)A_NodeChunk, S_S_AL1P_08);
    ST (S_S_AL1P_08, SPR_S_AL1P, 32776, 6, NULL, S_S_AL1P_09);
    ST (S_S_AL1P_09, SPR_S_AL1P, 32777, 6, NULL, S_S_AL1P_10);
    ST (S_S_AL1P_10, SPR_S_AL1P, 32778, 6, NULL, S_S_AL1P_11);
    ST (S_S_AL1P_11, SPR_S_AL1P, 32779, 5, NULL, S_S_AL1P_12);
    ST (S_S_AL1P_12, SPR_S_AL1P, 32780, 5, NULL, S_S_AL1P_13);
    ST (S_S_AL1P_13, SPR_S_AL1P, 32781, 5, (actionf_p1)A_HeadChunk, S_S_AL1P_14);
    ST (S_S_AL1P_14, SPR_S_AL1P, 32782, 5, NULL, S_S_AL1P_15);
    ST (S_S_AL1P_15, SPR_S_AL1P, 32783, 5, NULL, S_S_AL1P_16);
    ST (S_S_AL1P_16, SPR_S_AL1P, 32784, 5, NULL, S_S_AL1P_17);
    ST (S_S_AL1P_17, SPR_S_AL1P, 32785, 5, (actionf_p1)A_BossDeath, S_NULL);
    ST (S_S_NODE_00, SPR_S_NODE, 32768, 6, NULL, S_S_NODE_01);
    ST (S_S_NODE_01, SPR_S_NODE, 32769, 6, NULL, S_S_NODE_02);
    ST (S_S_NODE_02, SPR_S_NODE, 32770, 6, NULL, S_S_NODE_03);
    ST (S_S_NODE_03, SPR_S_NODE, 32771, 6, NULL, S_S_NODE_04);
    ST (S_S_NODE_04, SPR_S_NODE, 32772, 6, NULL, S_S_NODE_05);
    ST (S_S_NODE_05, SPR_S_NODE, 32773, 6, NULL, S_S_NODE_06);
    ST (S_S_NODE_06, SPR_S_NODE, 32774, 6, NULL, S_NULL);
    ST (S_S_MTHD_00, SPR_S_MTHD, 32768, 5, NULL, S_S_MTHD_01);
    ST (S_S_MTHD_01, SPR_S_MTHD, 32769, 5, NULL, S_S_MTHD_02);
    ST (S_S_MTHD_02, SPR_S_MTHD, 32770, 5, NULL, S_S_MTHD_03);
    ST (S_S_MTHD_03, SPR_S_MTHD, 32771, 5, NULL, S_S_MTHD_04);
    ST (S_S_MTHD_04, SPR_S_MTHD, 32772, 5, NULL, S_S_MTHD_05);
    ST (S_S_MTHD_05, SPR_S_MTHD, 32773, 5, NULL, S_S_MTHD_06);
    ST (S_S_MTHD_06, SPR_S_MTHD, 32774, 5, NULL, S_S_MTHD_07);
    ST (S_S_MTHD_07, SPR_S_MTHD, 32775, 5, NULL, S_S_MTHD_08);
    ST (S_S_MTHD_08, SPR_S_MTHD, 32776, 5, NULL, S_S_MTHD_09);
    ST (S_S_MTHD_09, SPR_S_MTHD, 32777, 5, NULL, S_S_MTHD_10);
    ST (S_S_MTHD_10, SPR_S_MTHD, 32778, 5, NULL, S_NULL);
    ST (S_S_ALN1_20, SPR_S_ALN1, 5, 4, (actionf_p1)A_FaceTarget, S_S_ALN1_21);
    ST (S_S_ALN1_21, SPR_S_ALN1, 8, 4, (actionf_p1)A_FireSigilEOffshoot, S_S_ALN1_22);
    ST (S_S_ALN1_22, SPR_S_ALN1, 4, 4, NULL, S_S_ALN1_12);
    ST (S_S_ALN1_23, SPR_S_ALN1, 0, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_24);
    ST (S_S_ALN1_24, SPR_S_ALN1, 1, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_25);
    ST (S_S_ALN1_25, SPR_S_ALN1, 2, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_26);
    ST (S_S_ALN1_26, SPR_S_ALN1, 3, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_27);
    ST (S_S_ALN1_27, SPR_S_ALN1, 4, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_28);
    ST (S_S_ALN1_28, SPR_S_ALN1, 5, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_29);
    ST (S_S_ALN1_29, SPR_S_ALN1, 6, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_30);
    ST (S_S_ALN1_30, SPR_S_ALN1, 7, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_31);
    ST (S_S_ALN1_31, SPR_S_ALN1, 8, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_32);
    ST (S_S_ALN1_32, SPR_S_ALN1, 9, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_33);
    ST (S_S_ALN1_33, SPR_S_ALN1, 10, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_23);
    ST (S_S_ALN1_34, SPR_S_ALN1, 0, 5, (actionf_p1)A_Chase, S_S_ALN1_35);
    ST (S_S_ALN1_35, SPR_S_ALN1, 1, 5, (actionf_p1)A_Chase, S_S_ALN1_36);
    ST (S_S_ALN1_36, SPR_S_ALN1, 2, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_37);
    ST (S_S_ALN1_37, SPR_S_ALN1, 3, 5, (actionf_p1)A_Chase, S_S_ALN1_38);
    ST (S_S_ALN1_38, SPR_S_ALN1, 4, 5, (actionf_p1)A_Chase, S_S_ALN1_39);
    ST (S_S_ALN1_39, SPR_S_ALN1, 5, 5, (actionf_p1)A_Chase, S_S_ALN1_40);
    ST (S_S_ALN1_40, SPR_S_ALN1, 6, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_41);
    ST (S_S_ALN1_41, SPR_S_ALN1, 7, 5, (actionf_p1)A_Chase, S_S_ALN1_42);
    ST (S_S_ALN1_42, SPR_S_ALN1, 8, 5, (actionf_p1)A_Chase, S_S_ALN1_43);
    ST (S_S_ALN1_43, SPR_S_ALN1, 9, 5, (actionf_p1)A_Chase, S_S_ALN1_44);
    ST (S_S_ALN1_44, SPR_S_ALN1, 10, 5, (actionf_p1)A_FloatWeave, S_S_ALN1_34);
    ST (S_S_ALN1_45, SPR_S_ALN1, 9, 4, (actionf_p1)A_FaceTarget, S_S_ALN1_46);
    ST (S_S_ALN1_46, SPR_S_ALN1, 8, 4, (actionf_p1)A_BossMeleeAtk, S_S_ALN1_47);
    ST (S_S_ALN1_47, SPR_S_ALN1, 2, 4, NULL, S_S_ALN1_36);
    ST (S_S_ALN1_48, SPR_S_ALN1, 5, 4, (actionf_p1)A_FaceTarget, S_S_ALN1_49);
    ST (S_S_ALN1_49, SPR_S_ALN1, 8, 4, (actionf_p1)A_SpectreCAttack, S_S_ALN1_50);
    ST (S_S_ALN1_50, SPR_S_ALN1, 4, 4, NULL, S_S_ALN1_44);
    ST (S_S_ALN1_51, SPR_S_ALN1, 9, 2, (actionf_p1)A_Pain, S_S_ALN1_40);
    ST (S_S_ALN1_52, SPR_S_ALN1, 5, 4, (actionf_p1)A_FaceTarget, S_S_ALN1_53);
    ST (S_S_ALN1_53, SPR_S_ALN1, 8, 4, (actionf_p1)A_SpectreDAttack, S_S_ALN1_54);
    ST (S_S_ALN1_54, SPR_S_ALN1, 4, 4, NULL, S_S_ALN1_12);
    ST (S_S_ALN1_55, SPR_S_ALN1, 5, 4, (actionf_p1)A_FaceTarget, S_S_ALN1_56);
    ST (S_S_ALN1_56, SPR_S_ALN1, 8, 4, (actionf_p1)A_SpectreEAttack, S_S_ALN1_57);
    ST (S_S_ALN1_57, SPR_S_ALN1, 4, 4, NULL, S_S_ALN1_12);
    ST (S_S_MNAM_00, SPR_S_MNAM, 0, 100, (actionf_p1)A_FloatWeave, S_S_MNAM_01);
    ST (S_S_MNAM_01, SPR_S_MNAM, 32769, 60, (actionf_p1)A_FloatWeave, S_S_MNAM_02);
    ST (S_S_MNAM_02, SPR_S_MNAM, 32770, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_03);
    ST (S_S_MNAM_03, SPR_S_MNAM, 32771, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_04);
    ST (S_S_MNAM_04, SPR_S_MNAM, 32772, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_05);
    ST (S_S_MNAM_05, SPR_S_MNAM, 32773, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_06);
    ST (S_S_MNAM_06, SPR_S_MNAM, 32774, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_07);
    ST (S_S_MNAM_07, SPR_S_MNAM, 32775, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_08);
    ST (S_S_MNAM_08, SPR_S_MNAM, 32776, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_09);
    ST (S_S_MNAM_09, SPR_S_MNAM, 32777, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_10);
    ST (S_S_MNAM_10, SPR_S_MNAM, 32778, 4, (actionf_p1)A_FloatWeave, S_S_MNAM_11);
    ST (S_S_MNAM_11, SPR_S_MNAM, 32779, 4, (actionf_p1)A_FloatWeave, S_S_MNAL_00);
    ST (S_S_MNAL_00, SPR_S_MNAL, 32768, 4, (actionf_p1)A_Look, S_S_MNAL_01);
    ST (S_S_MNAL_01, SPR_S_MNAL, 32769, 4, (actionf_p1)A_FloatWeave, S_S_MNAL_00);
    ST (S_S_MNAL_02, SPR_S_MNAL, 32768, 4, (actionf_p1)A_Chase, S_S_MNAL_03);
    ST (S_S_MNAL_03, SPR_S_MNAL, 32769, 4, (actionf_p1)A_Chase, S_S_MNAL_04);
    ST (S_S_MNAL_04, SPR_S_MNAL, 32770, 4, (actionf_p1)A_FloatWeave, S_S_MNAL_05);
    ST (S_S_MNAL_05, SPR_S_MNAL, 32771, 4, (actionf_p1)A_Chase, S_S_MNAL_06);
    ST (S_S_MNAL_06, SPR_S_MNAL, 32772, 4, (actionf_p1)A_Chase, S_S_MNAL_07);
    ST (S_S_MNAL_07, SPR_S_MNAL, 32773, 4, (actionf_p1)A_Chase, S_S_MNAL_08);
    ST (S_S_MNAL_08, SPR_S_MNAL, 32774, 4, (actionf_p1)A_FloatWeave, S_S_MNAL_09);
    ST (S_S_MNAL_09, SPR_S_MNAL, 32775, 4, (actionf_p1)A_Chase, S_S_MNAL_10);
    ST (S_S_MNAL_10, SPR_S_MNAL, 32776, 4, (actionf_p1)A_Chase, S_S_MNAL_11);
    ST (S_S_MNAL_11, SPR_S_MNAL, 32777, 4, (actionf_p1)A_Chase, S_S_MNAL_12);
    ST (S_S_MNAL_12, SPR_S_MNAL, 32778, 4, (actionf_p1)A_FloatWeave, S_S_MNAL_02);
    ST (S_S_MNAL_13, SPR_S_MNAL, 32777, 4, (actionf_p1)A_FaceTarget, S_S_MNAL_14);
    ST (S_S_MNAL_14, SPR_S_MNAL, 32776, 4, (actionf_p1)A_BossMeleeAtk, S_S_MNAL_15);
    ST (S_S_MNAL_15, SPR_S_MNAL, 32770, 4, NULL, S_S_MNAL_04);
    ST (S_S_MNAL_16, SPR_S_MNAL, 32773, 4, (actionf_p1)A_FaceTarget, S_S_MNAL_17);
    ST (S_S_MNAL_17, SPR_S_MNAL, 32776, 4, (actionf_p1)A_FireSigilWeapon, S_S_MNAL_18);
    ST (S_S_MNAL_18, SPR_S_MNAL, 32772, 4, NULL, S_S_MNAL_12);
    ST (S_S_MNAL_19, SPR_S_MNAL, 32777, 2, (actionf_p1)A_Pain, S_S_MNAL_08);
    ST (S_S_MNAL_20, SPR_S_MNAL, 32779, 7, (actionf_p1)A_NodeChunk, S_S_MNAL_21);
    ST (S_S_MNAL_21, SPR_S_MNAL, 32780, 7, (actionf_p1)A_Scream, S_S_MNAL_22);
    ST (S_S_MNAL_22, SPR_S_MNAL, 32781, 7, (actionf_p1)A_NodeChunk, S_S_MNAL_23);
    ST (S_S_MNAL_23, SPR_S_MNAL, 32782, 7, (actionf_p1)A_NodeChunk, S_S_MNAL_24);
    ST (S_S_MNAL_24, SPR_S_MNAL, 32783, 7, (actionf_p1)A_HeadChunk, S_S_MNAL_25);
    ST (S_S_MNAL_25, SPR_S_MNAL, 32784, 64, (actionf_p1)A_NodeChunk, S_S_MNAL_26);
    ST (S_S_MNAL_26, SPR_S_MNAL, 32784, 6, (actionf_p1)A_EntityDeath, S_NULL);
    ST (S_S_MNAL_27, SPR_S_MNAL, 32785, 10, (actionf_p1)A_Look, S_S_MNAL_27);
    ST (S_S_MNAL_28, SPR_S_MNAL, 32785, 5, (actionf_p1)A_FloatWeave, S_S_MNAL_29);
    ST (S_S_MNAL_29, SPR_S_MNAL, 32786, 5, (actionf_p1)A_Chase, S_S_MNAL_30);
    ST (S_S_MNAL_30, SPR_S_MNAL, 32787, 5, (actionf_p1)A_Chase, S_S_MNAL_31);
    ST (S_S_MNAL_31, SPR_S_MNAL, 32788, 5, (actionf_p1)A_FloatWeave, S_S_MNAL_32);
    ST (S_S_MNAL_32, SPR_S_MNAL, 32789, 5, (actionf_p1)A_Chase, S_S_MNAL_33);
    ST (S_S_MNAL_33, SPR_S_MNAL, 32790, 5, (actionf_p1)A_FloatWeave, S_S_MNAL_28);
    ST (S_S_MNAL_34, SPR_S_MNAL, 32786, 4, (actionf_p1)A_FaceTarget, S_S_MNAL_35);
    ST (S_S_MNAL_35, SPR_S_MNAL, 32785, 4, (actionf_p1)A_BossMeleeAtk, S_S_MNAL_36);
    ST (S_S_MNAL_36, SPR_S_MNAL, 32787, 4, (actionf_p1)A_FloatWeave, S_S_MNAL_29);
    ST (S_S_MNAL_37, SPR_S_MNAL, 32790, 4, (actionf_p1)A_FaceTarget, S_S_MNAL_38);
    ST (S_S_MNAL_38, SPR_S_MNAL, 32788, 4, (actionf_p1)A_FireSigilEOffshoot, S_S_MNAL_39);
    ST (S_S_MNAL_39, SPR_S_MNAL, 32789, 4, (actionf_p1)A_FloatWeave, S_S_MNAL_32);
    ST (S_S_MNAL_40, SPR_S_MNAL, 32785, 2, (actionf_p1)A_Pain, S_S_MNAL_28);
    ST (S_S_MDTH_00, SPR_S_MDTH, 32768, 3, (actionf_p1)A_Scream, S_S_MDTH_01);
    ST (S_S_MDTH_01, SPR_S_MDTH, 32769, 3, NULL, S_S_MDTH_02);
    ST (S_S_MDTH_02, SPR_S_MDTH, 32770, 3, (actionf_p1)A_Fall, S_S_MDTH_03);
    ST (S_S_MDTH_03, SPR_S_MDTH, 32771, 3, NULL, S_S_MDTH_04);
    ST (S_S_MDTH_04, SPR_S_MDTH, 32772, 3, NULL, S_S_MDTH_05);
    ST (S_S_MDTH_05, SPR_S_MDTH, 32773, 3, NULL, S_S_MDTH_06);
    ST (S_S_MDTH_06, SPR_S_MDTH, 32774, 3, NULL, S_S_MDTH_07);
    ST (S_S_MDTH_07, SPR_S_MDTH, 32775, 3, NULL, S_S_MDTH_08);
    ST (S_S_MDTH_08, SPR_S_MDTH, 32776, 3, NULL, S_S_MDTH_09);
    ST (S_S_MDTH_09, SPR_S_MDTH, 32777, 3, NULL, S_S_MDTH_10);
    ST (S_S_MDTH_10, SPR_S_MDTH, 32778, 3, NULL, S_S_MDTH_11);
    ST (S_S_MDTH_11, SPR_S_MDTH, 32779, 3, NULL, S_S_MDTH_12);
    ST (S_S_MDTH_12, SPR_S_MDTH, 32780, 3, NULL, S_S_MDTH_13);
    ST (S_S_MDTH_13, SPR_S_MDTH, 32781, 3, NULL, S_S_MDTH_14);
    ST (S_S_MDTH_14, SPR_S_MDTH, 32782, 3, (actionf_p1)A_BossDeath, S_NULL);
    ST (S_S_NEST_00, SPR_S_NEST, 0, -1, NULL, S_NULL);
    ST (S_S_PODD_00, SPR_S_PODD, 0, 60, (actionf_p1)A_Look, S_S_PODD_00);
    ST (S_S_PODD_01, SPR_S_PODD, 0, 360, NULL, S_S_PODD_02);
    ST (S_S_PODD_02, SPR_S_PODD, 1, 9, (actionf_p1)A_Fall, S_S_PODD_03);
    ST (S_S_PODD_03, SPR_S_PODD, 2, 9, NULL, S_S_PODD_04);
    ST (S_S_PODD_04, SPR_S_PODD, 3, 9, (actionf_p1)A_SpawnEntity, S_S_PODD_05);
    ST (S_S_PODD_05, SPR_S_PODD, 4, -1, NULL, S_NULL);
    ST (S_S_ZAP6_00, SPR_S_ZAP6, 32768, 4, NULL, S_S_ZAP6_01);
    ST (S_S_ZAP6_01, SPR_S_ZAP6, 32769, 4, (actionf_p1)A_SigilTrail, S_S_ZAP6_02);
    ST (S_S_ZAP6_02, SPR_S_ZAP6, 32770, 4, (actionf_p1)A_SigilTrail, S_S_ZAP6_00);
    ST (S_S_ZOT3_00, SPR_S_ZOT3, 32768, 4, NULL, S_S_ZOT3_01);
    ST (S_S_ZOT3_01, SPR_S_ZOT3, 32769, 4, NULL, S_S_ZOT3_02);
    ST (S_S_ZOT3_02, SPR_S_ZOT3, 32770, 4, NULL, S_S_ZOT3_03);
    ST (S_S_ZOT3_03, SPR_S_ZOT3, 32771, 4, NULL, S_S_ZOT3_04);
    ST (S_S_ZOT3_04, SPR_S_ZOT3, 32772, 4, NULL, S_S_ZOT3_00);
    ST (S_S_ZAP6_03, SPR_S_ZAP6, 32768, 5, NULL, S_S_ZAP6_04);
    ST (S_S_ZAP6_04, SPR_S_ZAP6, 32769, 5, NULL, S_S_ZAP6_05);
    ST (S_S_ZAP6_05, SPR_S_ZAP6, 32770, 5, NULL, S_NULL);
    ST (S_S_ZAP7_00, SPR_S_ZAP7, 32768, 4, (actionf_p1)A_Sigil_E_Action, S_S_ZAP7_01);
    ST (S_S_ZAP7_01, SPR_S_ZAP7, 32769, 4, (actionf_p1)A_Sigil_E_Action, S_S_ZAP7_02);
    ST (S_S_ZAP7_02, SPR_S_ZAP7, 32770, 6, (actionf_p1)A_Sigil_E_Action, S_S_ZAP7_03);
    ST (S_S_ZAP7_03, SPR_S_ZAP7, 32771, 6, (actionf_p1)A_Sigil_E_Action, S_S_ZAP7_04);
    ST (S_S_ZAP7_04, SPR_S_ZAP7, 32772, 6, (actionf_p1)A_Sigil_E_Action, S_S_ZAP7_00);
    ST (S_S_ZOT1_00, SPR_S_ZOT1, 32768, 4, NULL, S_S_ZOT1_01);
    ST (S_S_ZOT1_01, SPR_S_ZOT1, 32769, 4, NULL, S_S_ZOT1_02);
    ST (S_S_ZOT1_02, SPR_S_ZOT1, 32770, 6, NULL, S_S_ZOT1_03);
    ST (S_S_ZOT1_03, SPR_S_ZOT1, 32771, 6, NULL, S_S_ZOT1_04);
    ST (S_S_ZOT1_04, SPR_S_ZOT1, 32771, 6, NULL, S_S_ZOT1_00);
    ST (S_S_ZAP5_00, SPR_S_ZAP5, 32768, 4, (actionf_p1)A_MissileTick, S_S_ZAP5_01);
    ST (S_S_ZAP5_01, SPR_S_ZAP5, 32769, 4, (actionf_p1)A_Sigil_A_Action, S_S_ZAP5_02);
    ST (S_S_ZAP5_02, SPR_S_ZAP5, 32770, 4, (actionf_p1)A_MissileTick, S_S_ZAP5_03);
    ST (S_S_ZAP5_03, SPR_S_ZAP5, 32771, 4, (actionf_p1)A_MissileTick, S_S_ZAP5_00);
    ST (S_S_ZOT2_00, SPR_S_ZOT2, 32768, 4, (actionf_p1)A_Tracer, S_S_ZOT2_01);
    ST (S_S_ZOT2_01, SPR_S_ZOT2, 32769, 4, (actionf_p1)A_Tracer, S_S_ZOT2_02);
    ST (S_S_ZOT2_02, SPR_S_ZOT2, 32770, 6, (actionf_p1)A_Tracer, S_S_ZOT2_03);
    ST (S_S_ZOT2_03, SPR_S_ZOT2, 32771, 6, (actionf_p1)A_Tracer, S_S_ZOT2_04);
    ST (S_S_ZOT2_04, SPR_S_ZOT2, 32772, 5, (actionf_p1)A_Tracer, S_S_ZOT2_00);
    ST (S_S_SEWR_00, SPR_S_SEWR, 0, 10, (actionf_p1)A_Look, S_S_SEWR_00);
    ST (S_S_SEWR_01, SPR_S_SEWR, 0, 6, (actionf_p1)A_FloatWeave, S_S_SEWR_02);
    ST (S_S_SEWR_02, SPR_S_SEWR, 0, 6, (actionf_p1)A_Chase, S_S_SEWR_01);
    ST (S_S_SEWR_03, SPR_S_SEWR, 1, 4, (actionf_p1)A_FaceTarget, S_S_SEWR_04);
    ST (S_S_SEWR_04, SPR_S_SEWR, 32770, 8, (actionf_p1)A_SentinelAttack, S_S_SEWR_05);
    ST (S_S_SEWR_05, SPR_S_SEWR, 32770, 4, (actionf_p1)A_CheckTargetVisible, S_S_SEWR_04);
    ST (S_S_SEWR_06, SPR_S_SEWR, 3, 5, (actionf_p1)A_Pain, S_S_SEWR_05);
    ST (S_S_SEWR_07, SPR_S_SEWR, 3, 7, (actionf_p1)A_Fall, S_S_SEWR_08);
    ST (S_S_SEWR_08, SPR_S_SEWR, 32772, 8, NULL, S_S_SEWR_09);
    ST (S_S_SEWR_09, SPR_S_SEWR, 32773, 5, (actionf_p1)A_Scream, S_S_SEWR_10);
    ST (S_S_SEWR_10, SPR_S_SEWR, 32774, 4, NULL, S_S_SEWR_11);
    ST (S_S_SEWR_11, SPR_S_SEWR, 32775, 4, NULL, S_S_SEWR_12);
    ST (S_S_SEWR_12, SPR_S_SEWR, 8, 4, NULL, S_S_SEWR_13);
    ST (S_S_SEWR_13, SPR_S_SEWR, 9, 44, NULL, S_S_SEWR_14);
    ST (S_S_SPID_00, SPR_S_SPID, 0, 1, (actionf_p1)A_StalkerSetLook, S_S_SPID_00);
    ST (S_S_SPID_01, SPR_S_SPID, 0, 10, (actionf_p1)A_Look, S_S_SPID_01);
    ST (S_S_SPID_02, SPR_S_SPID, 9, 10, (actionf_p1)A_Look, S_S_SPID_02);
    ST (S_S_SPID_03, SPR_S_SPID, 0, 1, (actionf_p1)A_StalkerThink, S_S_SPID_04);
    ST (S_S_SPID_04, SPR_S_SPID, 0, 3, (actionf_p1)A_Chase, S_S_SPID_05);
    ST (S_S_SPID_05, SPR_S_SPID, 1, 3, (actionf_p1)A_Chase, S_S_SPID_06);
    ST (S_S_SPID_06, SPR_S_SPID, 1, 3, (actionf_p1)A_Chase, S_S_SPID_07);
    ST (S_S_SPID_07, SPR_S_SPID, 2, 3, (actionf_p1)A_StalkerChase, S_S_SPID_08);
    ST (S_S_SPID_08, SPR_S_SPID, 2, 3, (actionf_p1)A_Chase, S_S_SPID_03);
    ST (S_S_SPID_09, SPR_S_SPID, 9, 3, (actionf_p1)A_FaceTarget, S_S_SPID_10);
    ST (S_S_SPID_10, SPR_S_SPID, 10, 3, (actionf_p1)A_StalkerScratch, S_S_SPID_18);
    ST (S_S_SPID_11, SPR_S_SPID, 2, 2, (actionf_p1)A_StalkerDrop, S_S_SPID_12);
    ST (S_S_SPID_12, SPR_S_SPID, 8, 3, NULL, S_S_SPID_13);
    ST (S_S_SPID_13, SPR_S_SPID, 7, 3, NULL, S_S_SPID_14);
    ST (S_S_SPID_14, SPR_S_SPID, 6, 3, NULL, S_S_SPID_15);
    ST (S_S_SPID_15, SPR_S_SPID, 5, 3, NULL, S_S_SPID_16);
    ST (S_S_SPID_16, SPR_S_SPID, 4, 3, NULL, S_S_SPID_17);
    ST (S_S_SPID_17, SPR_S_SPID, 3, 3, NULL, S_S_SPID_09);
    ST (S_S_SPID_18, SPR_S_SPID, 9, 3, (actionf_p1)A_StalkerChase, S_S_SPID_19);
    ST (S_S_SPID_19, SPR_S_SPID, 9, 3, (actionf_p1)A_Chase, S_S_SPID_20);
    ST (S_S_SPID_20, SPR_S_SPID, 10, 3, (actionf_p1)A_Chase, S_S_SPID_21);
    ST (S_S_SPID_21, SPR_S_SPID, 10, 3, (actionf_p1)A_Chase, S_S_SPID_22);
    ST (S_S_SPID_22, SPR_S_SPID, 11, 3, (actionf_p1)A_StalkerChase, S_S_SPID_23);
    ST (S_S_SPID_23, SPR_S_SPID, 11, 3, (actionf_p1)A_Chase, S_S_SPID_18);
    ST (S_S_SPID_24, SPR_S_SPID, 11, 1, (actionf_p1)A_Pain, S_S_SPID_03);
    ST (S_S_SPID_25, SPR_S_SPID, 14, 4, NULL, S_S_SPID_26);
    ST (S_S_SPID_26, SPR_S_SPID, 15, 4, (actionf_p1)A_Scream, S_S_SPID_27);
    ST (S_S_SPID_27, SPR_S_SPID, 16, 4, NULL, S_S_SPID_28);
    ST (S_S_SPID_28, SPR_S_SPID, 17, 4, NULL, S_S_SPID_29);
    ST (S_S_SPID_29, SPR_S_SPID, 18, 4, NULL, S_S_SPID_30);
    ST (S_S_SPID_30, SPR_S_SPID, 19, 4, NULL, S_S_SPID_31);
    ST (S_S_SPID_31, SPR_S_SPID, 20, 4, (actionf_p1)A_Fall, S_S_SPID_32);
    ST (S_S_SPID_32, SPR_S_SPID, 21, 4, NULL, S_S_SPID_33);
    ST (S_S_SPID_33, SPR_S_SPID, 22, 4, NULL, S_S_SPID_34);
    ST (S_S_SPID_34, SPR_S_SPID, 32791, 4, NULL, S_S_SPID_35);
    ST (S_S_SPID_35, SPR_S_SPID, 32792, 4, NULL, S_S_SPID_36);
    ST (S_S_SPID_36, SPR_S_SPID, 32793, 4, NULL, S_S_SPID_37);
    ST (S_S_SPID_37, SPR_S_SPID, 32794, 4, NULL, S_NULL);
    ST (S_S_ROB3_00, SPR_S_ROB3, 0, 10, (actionf_p1)A_Look, S_S_ROB3_01);
    ST (S_S_ROB3_01, SPR_S_ROB3, 1, 10, (actionf_p1)A_Look, S_S_ROB3_00);
    ST (S_S_ROB3_02, SPR_S_ROB3, 1, 3, (actionf_p1)A_InqChase, S_S_ROB3_03);
    ST (S_S_ROB3_03, SPR_S_ROB3, 1, 3, (actionf_p1)A_Chase, S_S_ROB3_04);
    ST (S_S_ROB3_04, SPR_S_ROB3, 2, 4, (actionf_p1)A_Chase, S_S_ROB3_05);
    ST (S_S_ROB3_05, SPR_S_ROB3, 2, 4, (actionf_p1)A_Chase, S_S_ROB3_06);
    ST (S_S_ROB3_06, SPR_S_ROB3, 3, 4, (actionf_p1)A_Chase, S_S_ROB3_07);
    ST (S_S_ROB3_07, SPR_S_ROB3, 3, 4, (actionf_p1)A_Chase, S_S_ROB3_08);
    ST (S_S_ROB3_08, SPR_S_ROB3, 4, 3, (actionf_p1)A_InqChase, S_S_ROB3_09);
    ST (S_S_ROB3_09, SPR_S_ROB3, 4, 3, (actionf_p1)A_InqFlyCheck, S_S_ROB3_02);
    ST (S_S_ROB3_10, SPR_S_ROB3, 0, 2, (actionf_p1)A_InqFlyCheck, S_S_ROB3_11);
    ST (S_S_ROB3_11, SPR_S_ROB3, 5, 6, (actionf_p1)A_FaceTarget, S_S_ROB3_12);
    ST (S_S_ROB3_12, SPR_S_ROB3, 32774, 8, (actionf_p1)A_ReaverAttack, S_S_ROB3_13);
    ST (S_S_ROB3_13, SPR_S_ROB3, 6, 8, (actionf_p1)A_ReaverAttack, S_S_ROB3_02);
    ST (S_S_ROB3_14, SPR_S_ROB3, 10, 12, (actionf_p1)A_FaceTarget, S_S_ROB3_15);
    ST (S_S_ROB3_15, SPR_S_ROB3, 32777, 6, (actionf_p1)A_InqGrenade, S_S_ROB3_16);
    ST (S_S_ROB3_16, SPR_S_ROB3, 10, 12, NULL, S_S_ROB3_02);
    ST (S_S_ROB3_17, SPR_S_ROB3, 32775, 8, (actionf_p1)A_InqTakeOff, S_S_ROB3_18);
    ST (S_S_ROB3_18, SPR_S_ROB3, 32776, 4, (actionf_p1)A_InqFly, S_S_ROB3_19);
    ST (S_S_ROB3_19, SPR_S_ROB3, 32775, 4, (actionf_p1)A_InqFly, S_S_ROB3_18);
    ST (S_S_ROB3_20, SPR_S_ROB3, 11, 4, NULL, S_S_ROB3_21);
    ST (S_S_ROB3_21, SPR_S_ROB3, 12, 4, (actionf_p1)A_Scream, S_S_ROB3_22);
    ST (S_S_ROB3_22, SPR_S_ROB3, 13, 4, NULL, S_S_ROB3_23);
    ST (S_S_ROB3_23, SPR_S_ROB3, 32782, 4, (actionf_p1)A_DeathExplode1, S_S_ROB3_24);
    ST (S_S_ROB3_24, SPR_S_ROB3, 32783, 4, NULL, S_S_ROB3_25);
    ST (S_S_ROB3_25, SPR_S_ROB3, 32784, 4, (actionf_p1)A_Fall, S_S_ROB3_26);
    ST (S_S_ROB3_26, SPR_S_ROB3, 17, 4, NULL, S_S_ROB3_27);
    ST (S_S_ROB3_27, SPR_S_ROB3, 18, 4, NULL, S_S_ROB3_28);
    ST (S_S_ROB3_28, SPR_S_ROB3, 19, 4, NULL, S_S_ROB3_29);
    ST (S_S_ROB3_29, SPR_S_ROB3, 20, 4, NULL, S_S_ROB3_30);
    ST (S_S_ROB3_30, SPR_S_ROB3, 21, 4, NULL, S_S_ROB3_31);
    ST (S_S_ROB3_31, SPR_S_ROB3, 32790, 4, (actionf_p1)A_DeathExplode1, S_S_ROB3_32);
    ST (S_S_ROB3_32, SPR_S_ROB3, 32791, 4, NULL, S_S_ROB3_33);
    ST (S_S_ROB3_33, SPR_S_ROB3, 32792, 4, NULL, S_S_ROB3_34);
    ST (S_S_ROB3_34, SPR_S_ROB3, 25, 4, NULL, S_S_ROB3_35);
    ST (S_S_ROB3_35, SPR_S_ROB3, 26, 4, NULL, S_S_ROB3_36);
    ST (S_S_ROB3_36, SPR_S_ROB3, 27, 3, NULL, S_S_ROB3_37);
    ST (S_S_ROB3_37, SPR_S_ROB3, 32796, 3, (actionf_p1)A_DeathExplode1, S_S_RBB3_00);
    ST (S_S_RBB3_00, SPR_S_RBB3, 32768, 3, (actionf_p1)A_InqTossArm, S_S_RBB3_01);
    ST (S_S_RBB3_01, SPR_S_RBB3, 32769, 3, NULL, S_S_RBB3_02);
    ST (S_S_RBB3_02, SPR_S_RBB3, 2, 3, NULL, S_S_RBB3_03);
    ST (S_S_RBB3_03, SPR_S_RBB3, 3, 3, NULL, S_S_RBB3_04);
    ST (S_S_RBB3_04, SPR_S_RBB3, 4, -1, (actionf_p1)A_BossDeath, S_NULL);
    ST (S_S_RBB3_05, SPR_S_RBB3, 32773, 5, NULL, S_S_RBB3_06);
    ST (S_S_RBB3_06, SPR_S_RBB3, 32774, 5, NULL, S_S_RBB3_07);
    ST (S_S_RBB3_07, SPR_S_RBB3, 7, -1, NULL, S_NULL);
    ST (S_S_PRGR_00, SPR_S_PRGR, 0, 5, (actionf_p1)A_Look, S_S_PRGR_01);
    ST (S_S_PRGR_01, SPR_S_PRGR, 0, 1, (actionf_p1)A_FloatWeave, S_S_PRGR_00);
    ST (S_S_PRGR_02, SPR_S_PRGR, 0, 160, (actionf_p1)A_FloatWeave, S_S_PRGR_03);
    ST (S_S_PRGR_03, SPR_S_PRGR, 1, 5, (actionf_p1)A_FloatWeave, S_S_PRGR_04);
    ST (S_S_PRGR_04, SPR_S_PRGR, 2, 5, (actionf_p1)A_FloatWeave, S_S_PRGR_05);
    ST (S_S_PRGR_05, SPR_S_PRGR, 3, 5, (actionf_p1)A_FloatWeave, S_S_PRGR_06);
    ST (S_S_PRGR_06, SPR_S_PRGR, 4, 2, (actionf_p1)A_FloatWeave, S_S_PRGR_07);
    ST (S_S_PRGR_07, SPR_S_PRGR, 5, 2, (actionf_p1)A_FloatWeave, S_S_PRGR_08);
    ST (S_S_PRGR_08, SPR_S_PRGR, 4, 3, (actionf_p1)A_Chase, S_S_PRGR_09);
    ST (S_S_PRGR_09, SPR_S_PRGR, 5, 3, (actionf_p1)A_Chase, S_S_PRGR_06);
    ST (S_S_PRGR_10, SPR_S_PRGR, 4, 2, (actionf_p1)A_FloatWeave, S_S_PRGR_11);
    ST (S_S_PRGR_11, SPR_S_PRGR, 5, 3, (actionf_p1)A_FloatWeave, S_S_PRGR_12);
    ST (S_S_PRGR_12, SPR_S_PRGR, 4, 3, (actionf_p1)A_FaceTarget, S_S_PRGR_13);
    ST (S_S_PRGR_13, SPR_S_PRGR, 5, 4, (actionf_p1)A_ProgrammerMelee, S_S_PRGR_06);
    ST (S_S_PRGR_14, SPR_S_PRGR, 6, 5, (actionf_p1)A_FaceTarget, S_S_PRGR_15);
    ST (S_S_PRGR_15, SPR_S_PRGR, 7, 5, (actionf_p1)A_FloatWeave, S_S_PRGR_16);
    ST (S_S_PRGR_16, SPR_S_PRGR, 32776, 5, (actionf_p1)A_FaceTarget, S_S_PRGR_17);
    ST (S_S_PRGR_17, SPR_S_PRGR, 32777, 5, (actionf_p1)A_ProgrammerAttack, S_S_PRGR_06);
    ST (S_S_PRGR_18, SPR_S_PRGR, 10, 5, (actionf_p1)A_Pain, S_S_PRGR_19);
    ST (S_S_PRGR_19, SPR_S_PRGR, 11, 5, (actionf_p1)A_FloatWeave, S_S_PRGR_06);
    ST (S_S_PRGR_20, SPR_S_PRGR, 32779, 7, NULL, S_S_PRGR_21);
    ST (S_S_PRGR_21, SPR_S_PRGR, 32780, 7, (actionf_p1)A_Scream, S_S_PRGR_22);
    ST (S_S_PRGR_22, SPR_S_PRGR, 32781, 7, NULL, S_S_PRGR_23);
    ST (S_S_PRGR_23, SPR_S_PRGR, 32782, 7, (actionf_p1)A_Fall, S_S_PRGR_24);
    ST (S_S_PRGR_24, SPR_S_PRGR, 32783, 7, NULL, S_S_PRGR_25);
    ST (S_S_PRGR_25, SPR_S_PRGR, 32784, 7, (actionf_p1)A_ProgrammerDie, S_S_PRGR_26);
    ST (S_S_PRGR_26, SPR_S_PRGR, 32785, 7, NULL, S_S_PRGR_27);
    ST (S_S_PRGR_27, SPR_S_PRGR, 32786, 6, NULL, S_S_PRGR_28);
    ST (S_S_PRGR_28, SPR_S_PRGR, 32787, 5, NULL, S_S_PRGR_29);
    ST (S_S_PRGR_29, SPR_S_PRGR, 32788, 5, NULL, S_S_PRGR_30);
    ST (S_S_PRGR_30, SPR_S_PRGR, 32789, 5, NULL, S_S_PRGR_31);
    ST (S_S_PRGR_31, SPR_S_PRGR, 32790, 5, NULL, S_S_PRGR_32);
    ST (S_S_PRGR_32, SPR_S_PRGR, 32791, 32, NULL, S_S_PRGR_33);
    ST (S_S_PRGR_33, SPR_S_PRGR, 32791, -1, (actionf_p1)A_BossDeath, S_NULL);
    ST (S_S_BASE_00, SPR_S_BASE, 32768, 5, (actionf_p1)A_DeathExplode3, S_S_BASE_01);
    ST (S_S_BASE_01, SPR_S_BASE, 32769, 5, NULL, S_S_BASE_02);
    ST (S_S_BASE_02, SPR_S_BASE, 32770, 5, NULL, S_S_BASE_03);
    ST (S_S_BASE_03, SPR_S_BASE, 32771, 5, NULL, S_S_BASE_04);
    ST (S_S_BASE_04, SPR_S_BASE, 4, 5, NULL, S_S_BASE_05);
    ST (S_S_BASE_05, SPR_S_BASE, 5, 5, NULL, S_S_BASE_06);
    ST (S_S_BASE_06, SPR_S_BASE, 6, 5, NULL, S_S_BASE_07);
    ST (S_S_BASE_07, SPR_S_BASE, 7, -1, NULL, S_NULL);
    ST (S_S_FRBL_00, SPR_S_FRBL, 32768, 3, NULL, S_S_FRBL_01);
    ST (S_S_FRBL_01, SPR_S_FRBL, 32769, 3, NULL, S_S_FRBL_02);
    ST (S_S_FRBL_02, SPR_S_FRBL, 32770, 3, (actionf_p1)A_MissileTick, S_S_FRBL_00);
    ST (S_S_FRBL_03, SPR_S_FRBL, 32771, 5, (actionf_p1)A_FlameDeath, S_S_FRBL_04);
    ST (S_S_FRBL_04, SPR_S_FRBL, 32772, 5, NULL, S_S_FRBL_05);
    ST (S_S_FRBL_05, SPR_S_FRBL, 32773, 5, NULL, S_S_FRBL_06);
    ST (S_S_FRBL_06, SPR_S_FRBL, 32774, 5, NULL, S_S_FRBL_07);
    ST (S_S_FRBL_07, SPR_S_FRBL, 32775, 5, NULL, S_S_FRBL_08);
    ST (S_S_FRBL_08, SPR_S_FRBL, 32776, 5, NULL, S_NULL);
    ST (S_S_TURT_00, SPR_S_TURT, 0, 5, NULL, S_S_TURT_00);
    ST (S_S_TURT_01, SPR_S_TURT, 0, 2, (actionf_p1)A_Chase, S_S_TURT_01);
    ST (S_S_TURT_02, SPR_S_TURT, 1, 4, (actionf_p1)A_BulletAttack, S_S_TURT_03);
    ST (S_S_TURT_03, SPR_S_TURT, 3, 3, (actionf_p1)A_CheckTargetVisible, S_S_TURT_04);
    ST (S_S_TURT_04, SPR_S_TURT, 0, 4, (actionf_p1)A_CheckTargetVisible, S_S_TURT_02);
    ST (S_S_BALL_00, SPR_S_BALL, 32768, 6, NULL, S_S_BALL_01);
    ST (S_S_BALL_01, SPR_S_BALL, 32769, 6, NULL, S_S_BALL_02);
    ST (S_S_BALL_02, SPR_S_BALL, 32770, 6, NULL, S_S_BALL_03);
    ST (S_S_BALL_03, SPR_S_BALL, 32771, 6, NULL, S_S_BALL_04);
    ST (S_S_BALL_04, SPR_S_BALL, 32772, 6, NULL, S_S_TURT_05);
    ST (S_S_TURT_05, SPR_S_TURT, 2, -1, NULL, S_NULL);
    ST (S_S_RATT_00, SPR_S_RATT, 0, 10, (actionf_p1)A_Look, S_S_RATT_00);
    ST (S_S_RATT_01, SPR_S_RATT, 0, 4, (actionf_p1)A_Chase, S_S_RATT_02);
    ST (S_S_RATT_02, SPR_S_RATT, 0, 4, (actionf_p1)A_Chase, S_S_RATT_03);
    ST (S_S_RATT_03, SPR_S_RATT, 1, 4, (actionf_p1)A_Chase, S_S_RATT_04);
    ST (S_S_RATT_04, SPR_S_RATT, 1, 4, (actionf_p1)A_Chase, S_S_RATT_01);
    ST (S_S_RATT_05, SPR_S_RATT, 0, 8, (actionf_p1)A_RandomWalk, S_S_RATT_06);
    ST (S_S_RATT_06, SPR_S_RATT, 1, 4, (actionf_p1)A_RandomWalk, S_S_RATT_01);
    ST (S_S_TOKN_00, SPR_S_TOKN, 0, -1, NULL, S_NULL);
    ST (S_S_MEAT_16, SPR_S_MEAT, 16, 700, NULL, S_NULL);
    ST (S_S_SEWR_14, SPR_S_SEWR, 32775, 4, NULL, S_S_SEWR_15);
    ST (S_S_SEWR_15, SPR_S_SEWR, 32778, 3, NULL, S_S_SEWR_16);
    ST (S_S_SEWR_16, SPR_S_SEWR, 32779, 3, NULL, S_S_SEWR_17);
    ST (S_S_SEWR_17, SPR_S_JUNK, 6, 35, NULL, S_NULL);

    // ====================================================================
    // MOBJINFO (per-actor; flags translated per STRIFE_PORT_GUIDE.md)
    // ====================================================================
    // ---- MT_FIELDGUARD (ednum 25)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_FIELDGUARD];
    m->doomednum=25; m->spawnstate=S_S_TOKN_00; m->spawnhealth=10;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_XPRK_00; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=2*FRACUNIT; m->height=1*FRACUNIT; m->mass=10000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SHOOTABLE|MF_NOSECTOR; m->raisestate=0; m->flags2=0;

    // ---- MT_SHOPKEEPER_W (ednum 116)
    m = &mobjinfo[MT_S_SHOPKEEPER_W];
    m->doomednum=116; m->spawnstate=S_S_MRST_00; m->spawnhealth=10000000;
    m->seestate=S_S_MRPN_00; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_MRPN_00; m->painchance=150; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=5000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_SHOPKEEPER_B (ednum 72)  [dropped flags: MF_COLORSWAP1,MF_COLORSWAP3]
    m = &mobjinfo[MT_S_SHOPKEEPER_B];
    m->doomednum=72; m->spawnstate=S_S_MRST_00; m->spawnhealth=10000000;
    m->seestate=S_S_MRPN_00; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_MRPN_00; m->painchance=150; m->painsound=sfx_s_ambbar;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=5000; m->damage=0;
    m->activesound=sfx_s_ambppl; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_SHOPKEEPER_A (ednum 73)  [dropped flags: MF_COLORSWAP2,MF_COLORSWAP3]
    m = &mobjinfo[MT_S_SHOPKEEPER_A];
    m->doomednum=73; m->spawnstate=S_S_MRST_00; m->spawnhealth=10000000;
    m->seestate=S_S_MRPN_00; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_MRPN_00; m->painchance=150; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=5000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_SHOPKEEPER_M (ednum 74)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_SHOPKEEPER_M];
    m->doomednum=74; m->spawnstate=S_S_MRST_00; m->spawnhealth=10000000;
    m->seestate=S_S_MRPN_00; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_MRPN_00; m->painchance=150; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_None; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT2_A (ednum 3004)
    m = &mobjinfo[MT_S_PEASANT2_A];
    m->doomednum=3004; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=4; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT2_B (ednum 130)
    m = &mobjinfo[MT_S_PEASANT2_B];
    m->doomednum=130; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=5; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT2_C (ednum 131)
    m = &mobjinfo[MT_S_PEASANT2_C];
    m->doomednum=131; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=5; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT5_A (ednum 65)  [dropped flags: MF_COLORSWAP1]
    m = &mobjinfo[MT_S_PEASANT5_A];
    m->doomednum=65; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=7; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT5_B (ednum 132)  [dropped flags: MF_COLORSWAP1]
    m = &mobjinfo[MT_S_PEASANT5_B];
    m->doomednum=132; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=7; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT5_C (ednum 133)  [dropped flags: MF_COLORSWAP1]
    m = &mobjinfo[MT_S_PEASANT5_C];
    m->doomednum=133; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=7; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT4_A (ednum 66)  [dropped flags: MF_COLORSWAP2]
    m = &mobjinfo[MT_S_PEASANT4_A];
    m->doomednum=66; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT4_B (ednum 134)  [dropped flags: MF_COLORSWAP2]
    m = &mobjinfo[MT_S_PEASANT4_B];
    m->doomednum=134; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT4_C (ednum 135)  [dropped flags: MF_COLORSWAP2]
    m = &mobjinfo[MT_S_PEASANT4_C];
    m->doomednum=135; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT6_A (ednum 67)  [dropped flags: MF_COLORSWAP2]
    m = &mobjinfo[MT_S_PEASANT6_A];
    m->doomednum=67; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT6_B (ednum 136)  [dropped flags: MF_COLORSWAP2]
    m = &mobjinfo[MT_S_PEASANT6_B];
    m->doomednum=136; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=7; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT6_C (ednum 137)  [dropped flags: MF_COLORSWAP2]
    m = &mobjinfo[MT_S_PEASANT6_C];
    m->doomednum=137; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT3_A (ednum 172)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT3_A];
    m->doomednum=172; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT3_B (ednum 173)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT3_B];
    m->doomednum=173; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT3_C (ednum 174)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT3_C];
    m->doomednum=174; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT8_A (ednum 175)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT8_A];
    m->doomednum=175; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT8_B (ednum 176)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT8_B];
    m->doomednum=176; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT8_C (ednum 177)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT8_C];
    m->doomednum=177; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT7_A (ednum 178)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT7_A];
    m->doomednum=178; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT7_B (ednum 179)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT7_B];
    m->doomednum=179; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT7_C (ednum 180)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT7_C];
    m->doomednum=180; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_PEASANT1 (ednum 181)  [dropped flags: MF_COLORSWAP2,MF_COLORSWAP3]
    m = &mobjinfo[MT_S_PEASANT1];
    m->doomednum=181; m->spawnstate=S_S_PEAS_00; m->spawnhealth=31;
    m->seestate=S_S_PEAS_01; m->seesound=sfx_s_rebact; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_PEAS_12; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_PEAS_09; m->missilestate=S_NULL;
    m->deathstate=S_S_PEAS_17; m->xdeathstate=S_S_GIBS_00; m->deathsound=sfx_s_psdtha;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_ZOMBIE (ednum 169)  [dropped flags: MF_COLORSWAP1]
    m = &mobjinfo[MT_S_ZOMBIE];
    m->doomednum=169; m->spawnstate=S_S_PEAS_25; m->spawnhealth=31;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_AGRD_00; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_GIBS_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_psdtha;
    m->speed=0; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_BECOMING (ednum 201)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_BECOMING];
    m->doomednum=201; m->spawnstate=S_S_ARMR_00; m->spawnhealth=61;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_ARMR_01; m->painchance=255; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_GIBS_10; m->xdeathstate=S_NULL; m->deathsound=sfx_s_psdtha;
    m->speed=0; m->radius=16*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_ZOMBIESPAWNER (ednum 170)
    m = &mobjinfo[MT_S_ZOMBIESPAWNER];
    m->doomednum=170; m->spawnstate=S_S_PLAY_19; m->spawnhealth=20;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_telept; m->flags=MF_SHOOTABLE|MF_NOSECTOR; m->raisestate=0; m->flags2=0;

    // ---- MT_HUGE_TANK_1 (ednum 209)
    m = &mobjinfo[MT_S_HUGE_TANK_1];
    m->doomednum=209; m->spawnstate=S_S_TNK1_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=16*FRACUNIT; m->height=192*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID; m->raisestate=0; m->flags2=0;

    // ---- MT_HUGE_TANK_2 (ednum 210)
    m = &mobjinfo[MT_S_HUGE_TANK_2];
    m->doomednum=210; m->spawnstate=S_S_TNK2_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=16*FRACUNIT; m->height=192*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID; m->raisestate=0; m->flags2=0;

    // ---- MT_HUGE_TANK_3 (ednum 211)
    m = &mobjinfo[MT_S_HUGE_TANK_3];
    m->doomednum=211; m->spawnstate=S_S_TNK3_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=16*FRACUNIT; m->height=192*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID; m->raisestate=0; m->flags2=0;

    // ---- MT_TANK_4 (ednum 213)
    m = &mobjinfo[MT_S_TANK_4];
    m->doomednum=213; m->spawnstate=S_S_TNK4_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=16*FRACUNIT; m->height=56*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID; m->raisestate=0; m->flags2=0;

    // ---- MT_TANK_5 (ednum 214)
    m = &mobjinfo[MT_S_TANK_5];
    m->doomednum=214; m->spawnstate=S_S_TNK5_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=16*FRACUNIT; m->height=56*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID; m->raisestate=0; m->flags2=0;

    // ---- MT_TANK_6 (ednum 229)
    m = &mobjinfo[MT_S_TANK_6];
    m->doomednum=229; m->spawnstate=S_S_TNK6_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=16*FRACUNIT; m->height=56*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID; m->raisestate=0; m->flags2=0;

    // ---- MT_KNEELING_GUY (ednum 204)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_KNEELING_GUY];
    m->doomednum=204; m->spawnstate=S_S_NEAL_00; m->spawnhealth=51;
    m->seestate=S_S_NEAL_00; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_NEAL_02; m->painchance=255; m->painsound=sfx_s_static;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_NEAL_07; m->xdeathstate=S_NULL; m->deathsound=sfx_s_static;
    m->speed=0; m->radius=6*FRACUNIT; m->height=6*FRACUNIT; m->mass=50000; m->damage=0;
    m->activesound=sfx_s_chant; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_BEGGAR1 (ednum 141)
    m = &mobjinfo[MT_S_BEGGAR1];
    m->doomednum=141; m->spawnstate=S_S_BEGR_00; m->spawnhealth=20;
    m->seestate=S_S_BEGR_01; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_BEGR_11; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_BEGR_07; m->missilestate=S_NULL;
    m->deathstate=S_S_BEGR_13; m->xdeathstate=S_S_BEGR_22; m->deathsound=sfx_s_psdtha;
    m->speed=3; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_BEGGAR2 (ednum 155)
    m = &mobjinfo[MT_S_BEGGAR2];
    m->doomednum=155; m->spawnstate=S_S_BEGR_00; m->spawnhealth=20;
    m->seestate=S_S_BEGR_01; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_BEGR_11; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_BEGR_07; m->missilestate=S_NULL;
    m->deathstate=S_S_BEGR_13; m->xdeathstate=S_S_BEGR_22; m->deathsound=sfx_s_psdtha;
    m->speed=3; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_BEGGAR3 (ednum 156)
    m = &mobjinfo[MT_S_BEGGAR3];
    m->doomednum=156; m->spawnstate=S_S_BEGR_00; m->spawnhealth=20;
    m->seestate=S_S_BEGR_01; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_BEGR_11; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_BEGR_07; m->missilestate=S_NULL;
    m->deathstate=S_S_BEGR_13; m->xdeathstate=S_S_BEGR_22; m->deathsound=sfx_s_psdtha;
    m->speed=3; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_BEGGAR4 (ednum 157)
    m = &mobjinfo[MT_S_BEGGAR4];
    m->doomednum=157; m->spawnstate=S_S_BEGR_00; m->spawnhealth=20;
    m->seestate=S_S_BEGR_01; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_BEGR_11; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_BEGR_07; m->missilestate=S_NULL;
    m->deathstate=S_S_BEGR_13; m->xdeathstate=S_S_BEGR_22; m->deathsound=sfx_s_psdtha;
    m->speed=3; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_BEGGAR5 (ednum 158)
    m = &mobjinfo[MT_S_BEGGAR5];
    m->doomednum=158; m->spawnstate=S_S_BEGR_00; m->spawnhealth=20;
    m->seestate=S_S_BEGR_01; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_s_meatht;
    m->painstate=S_S_BEGR_11; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_S_BEGR_07; m->missilestate=S_NULL;
    m->deathstate=S_S_BEGR_13; m->xdeathstate=S_S_BEGR_22; m->deathsound=sfx_s_psdtha;
    m->speed=3; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_JUSTHIT|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_REBEL1 (ednum 9)
    m = &mobjinfo[MT_S_REBEL1];
    m->doomednum=9; m->spawnstate=S_S_HMN1_00; m->spawnhealth=60;
    m->seestate=S_S_HMN1_11; m->seesound=sfx_s_wpnup; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_HMN1_22; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_HMN1_19;
    m->deathstate=S_S_HMN1_24; m->xdeathstate=S_S_RGIB_08; m->deathsound=sfx_s_rebdth;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FRIEND; m->raisestate=0; m->flags2=0;

    // ---- MT_REBEL2 (ednum 144)
    m = &mobjinfo[MT_S_REBEL2];
    m->doomednum=144; m->spawnstate=S_S_HMN1_00; m->spawnhealth=60;
    m->seestate=S_S_HMN1_11; m->seesound=sfx_s_wpnup; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_HMN1_22; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_HMN1_19;
    m->deathstate=S_S_HMN1_24; m->xdeathstate=S_S_RGIB_08; m->deathsound=sfx_s_rebdth;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FRIEND; m->raisestate=0; m->flags2=0;

    // ---- MT_REBEL3 (ednum 145)
    m = &mobjinfo[MT_S_REBEL3];
    m->doomednum=145; m->spawnstate=S_S_HMN1_00; m->spawnhealth=60;
    m->seestate=S_S_HMN1_11; m->seesound=sfx_s_wpnup; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_HMN1_22; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_HMN1_19;
    m->deathstate=S_S_HMN1_24; m->xdeathstate=S_S_RGIB_08; m->deathsound=sfx_s_rebdth;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FRIEND; m->raisestate=0; m->flags2=0;

    // ---- MT_REBEL4 (ednum 149)
    m = &mobjinfo[MT_S_REBEL4];
    m->doomednum=149; m->spawnstate=S_S_HMN1_00; m->spawnhealth=60;
    m->seestate=S_S_HMN1_11; m->seesound=sfx_s_wpnup; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_HMN1_22; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_HMN1_19;
    m->deathstate=S_S_HMN1_24; m->xdeathstate=S_S_RGIB_08; m->deathsound=sfx_s_rebdth;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FRIEND; m->raisestate=0; m->flags2=0;

    // ---- MT_REBEL5 (ednum 150)
    m = &mobjinfo[MT_S_REBEL5];
    m->doomednum=150; m->spawnstate=S_S_HMN1_00; m->spawnhealth=60;
    m->seestate=S_S_HMN1_11; m->seesound=sfx_s_wpnup; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_HMN1_22; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_HMN1_19;
    m->deathstate=S_S_HMN1_24; m->xdeathstate=S_S_RGIB_08; m->deathsound=sfx_s_rebdth;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FRIEND; m->raisestate=0; m->flags2=0;

    // ---- MT_REBEL6 (ednum 151)
    m = &mobjinfo[MT_S_REBEL6];
    m->doomednum=151; m->spawnstate=S_S_HMN1_00; m->spawnhealth=60;
    m->seestate=S_S_HMN1_11; m->seesound=sfx_s_wpnup; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_HMN1_22; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_HMN1_19;
    m->deathstate=S_S_HMN1_24; m->xdeathstate=S_S_RGIB_08; m->deathsound=sfx_s_rebdth;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FRIEND; m->raisestate=0; m->flags2=0;

    // ---- MT_RLEADER (ednum 64)
    m = &mobjinfo[MT_S_RLEADER];
    m->doomednum=64; m->spawnstate=S_S_LEDR_00; m->spawnhealth=95;
    m->seestate=S_S_LEAD_04; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_LEAD_15; m->painchance=250; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_LEAD_12;
    m->deathstate=S_S_LEAD_04; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_RLEADER2 (ednum 200)  [dropped flags: MF_SPECTRAL]
    m = &mobjinfo[MT_S_RLEADER2];
    m->doomednum=200; m->spawnstate=S_S_LEDR_00; m->spawnhealth=95;
    m->seestate=S_S_LEAD_04; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_LEAD_15; m->painchance=200; m->painsound=sfx_s_pespna;
    m->meleestate=S_NULL; m->missilestate=S_S_LEAD_17;
    m->deathstate=S_S_LEAD_20; m->xdeathstate=S_S_LEAD_20; m->deathsound=sfx_s_slop;
    m->speed=8; m->radius=20*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_rebact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_MISSILESMOKE (ednum -1)
    m = &mobjinfo[MT_S_MISSILESMOKE];
    m->doomednum=-1; m->spawnstate=S_S_PUFY_04; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_rflite; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_SHADOW; m->raisestate=0; m->flags2=0;

    // ---- MT_REAVER (ednum 3001)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_REAVER];
    m->doomednum=3001; m->spawnstate=S_S_ROB1_00; m->spawnhealth=150;
    m->seestate=S_S_ROB1_02; m->seesound=sfx_s_revsee; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_ROB1_15; m->painchance=128; m->painsound=sfx_s_reavpn;
    m->meleestate=S_S_ROB1_10; m->missilestate=S_S_ROB1_13;
    m->deathstate=S_S_ROB1_17; m->xdeathstate=S_S_ROB1_26; m->deathsound=sfx_s_revdth;
    m->speed=12; m->radius=20*FRACUNIT; m->height=60*FRACUNIT; m->mass=500; m->damage=0;
    m->activesound=sfx_s_revact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD1 (ednum 3002)
    m = &mobjinfo[MT_S_GUARD1];
    m->doomednum=3002; m->spawnstate=S_S_AGRD_01; m->spawnhealth=70;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac1; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD2 (ednum 142)  [dropped flags: MF_COLORSWAP1]
    m = &mobjinfo[MT_S_GUARD2];
    m->doomednum=142; m->spawnstate=S_S_AGRD_01; m->spawnhealth=70;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac2; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD3 (ednum 143)  [dropped flags: MF_COLORSWAP2]
    m = &mobjinfo[MT_S_GUARD3];
    m->doomednum=143; m->spawnstate=S_S_AGRD_01; m->spawnhealth=70;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac3; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD4 (ednum 146)  [dropped flags: MF_COLORSWAP1,MF_COLORSWAP2]
    m = &mobjinfo[MT_S_GUARD4];
    m->doomednum=146; m->spawnstate=S_S_AGRD_01; m->spawnhealth=70;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac1; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD5 (ednum 147)  [dropped flags: MF_COLORSWAP3]
    m = &mobjinfo[MT_S_GUARD5];
    m->doomednum=147; m->spawnstate=S_S_AGRD_01; m->spawnhealth=70;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac2; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD6 (ednum 148)  [dropped flags: MF_COLORSWAP1,MF_COLORSWAP3]
    m = &mobjinfo[MT_S_GUARD6];
    m->doomednum=148; m->spawnstate=S_S_AGRD_01; m->spawnhealth=70;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac3; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD7 (ednum 232)  [dropped flags: MF_COLORSWAP2,MF_COLORSWAP3]
    m = &mobjinfo[MT_S_GUARD7];
    m->doomednum=232; m->spawnstate=S_S_AGRD_01; m->spawnhealth=60;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac3; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_GUARD8 (ednum 231)  [dropped flags: MF_COLORSWAP3,MF_NODIALOG]
    m = &mobjinfo[MT_S_GUARD8];
    m->doomednum=231; m->spawnstate=S_S_AGRD_01; m->spawnhealth=60;
    m->seestate=S_S_AGRD_13; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_23; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac3; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_SHADOWGUARD (ednum 58)
    m = &mobjinfo[MT_S_SHADOWGUARD];
    m->doomednum=58; m->spawnstate=S_S_AGRD_01; m->spawnhealth=70;
    m->seestate=S_S_AGRD_12; m->seesound=sfx_s_agrsee; m->reactiontime=8; m->attacksound=sfx_s_rifle;
    m->painstate=S_S_AGRD_21; m->painchance=150; m->painsound=sfx_s_agrdpn;
    m->meleestate=S_NULL; m->missilestate=S_S_AGRD_17;
    m->deathstate=S_S_AGRD_24; m->xdeathstate=S_S_GIBS_10; m->deathsound=sfx_s_agrdth;
    m->speed=7; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_agrac2; m->flags=MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_PGUARD (ednum 3003)
    m = &mobjinfo[MT_S_PGUARD];
    m->doomednum=3003; m->spawnstate=S_S_PGRD_00; m->spawnhealth=300;
    m->seestate=S_S_PGRD_04; m->seesound=sfx_s_pgrsee; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_PGRD_16; m->painchance=100; m->painsound=sfx_s_pgrdpn;
    m->meleestate=S_S_PGRD_12; m->missilestate=S_S_PGRD_14;
    m->deathstate=S_S_PGRD_18; m->xdeathstate=S_NULL; m->deathsound=sfx_s_pgrdth;
    m->speed=8; m->radius=20*FRACUNIT; m->height=60*FRACUNIT; m->mass=500; m->damage=0;
    m->activesound=sfx_s_pgract; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_CRUSADER (ednum 3005)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_CRUSADER];
    m->doomednum=3005; m->spawnstate=S_S_ROB2_00; m->spawnhealth=400;
    m->seestate=S_S_ROB2_01; m->seesound=sfx_s_rb2see; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_ROB2_19; m->painchance=128; m->painsound=sfx_s_rb2pn;
    m->meleestate=S_NULL; m->missilestate=S_S_ROB2_09;
    m->deathstate=S_S_ROB2_20; m->xdeathstate=S_NULL; m->deathsound=sfx_s_rb2dth;
    m->speed=8; m->radius=40*FRACUNIT; m->height=56*FRACUNIT; m->mass=400; m->damage=0;
    m->activesound=sfx_s_rb2act; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_BISHOP (ednum 187)
    m = &mobjinfo[MT_S_BISHOP];
    m->doomednum=187; m->spawnstate=S_S_MLDR_00; m->spawnhealth=500;
    m->seestate=S_S_MLDR_01; m->seesound=sfx_s_rb2see; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_MLDR_11; m->painchance=128; m->painsound=sfx_s_rb2pn;
    m->meleestate=S_NULL; m->missilestate=S_S_MLDR_09;
    m->deathstate=S_S_MLDR_12; m->xdeathstate=S_NULL; m->deathsound=sfx_s_pgrdth;
    m->speed=8; m->radius=40*FRACUNIT; m->height=56*FRACUNIT; m->mass=500; m->damage=0;
    m->activesound=sfx_s_rb2act; m->flags=MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_ORACLE (ednum 199)
    m = &mobjinfo[MT_S_ORACLE];
    m->doomednum=199; m->spawnstate=S_S_ORCL_00; m->spawnhealth=1;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ORCL_01; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=15*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD|MF_COUNTKILL|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_PRIEST (ednum 12)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_PRIEST];
    m->doomednum=12; m->spawnstate=S_S_PRST_00; m->spawnhealth=800;
    m->seestate=S_S_PRST_02; m->seesound=sfx_s_lorsee; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_s_lorpn;
    m->meleestate=S_S_PRST_10; m->missilestate=S_S_PRST_13;
    m->deathstate=S_S_PDED_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_slop;
    m->speed=10; m->radius=15*FRACUNIT; m->height=56*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_tend; m->flags=MF_NOBLOOD|MF_COUNTKILL|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_SPECTRE_A (ednum 129)  [dropped flags: MF_MVIS,MF_SPECTRAL]
    m = &mobjinfo[MT_S_SPECTRE_A];
    m->doomednum=129; m->spawnstate=S_S_ALN1_00; m->spawnhealth=1000;
    m->seestate=S_S_ALN1_02; m->seesound=sfx_s_alnsee; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_ALN1_19; m->painchance=250; m->painsound=sfx_s_alnpn;
    m->meleestate=S_S_ALN1_13; m->missilestate=S_S_ALN1_16;
    m->deathstate=S_S_AL1P_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_alndth;
    m->speed=12; m->radius=64*FRACUNIT; m->height=64*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_alnact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_NODE (ednum -1)
    m = &mobjinfo[MT_S_NODE];
    m->doomednum=-1; m->spawnstate=S_S_NODE_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOCLIP|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_SPECTREHEAD (ednum -1)
    m = &mobjinfo[MT_S_SPECTREHEAD];
    m->doomednum=-1; m->spawnstate=S_S_MTHD_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOCLIP|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_SPECTRE_B (ednum 75)  [dropped flags: MF_MVIS,MF_SPECTRAL]
    m = &mobjinfo[MT_S_SPECTRE_B];
    m->doomednum=75; m->spawnstate=S_S_ALN1_00; m->spawnhealth=1200;
    m->seestate=S_S_ALN1_02; m->seesound=sfx_s_alnsee; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_ALN1_19; m->painchance=50; m->painsound=sfx_s_alnpn;
    m->meleestate=S_S_ALN1_13; m->missilestate=S_S_ALN1_20;
    m->deathstate=S_S_AL1P_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_alndth;
    m->speed=12; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_alnact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_SPECTRE_C (ednum 76)  [dropped flags: MF_MVIS,MF_SPECTRAL]
    m = &mobjinfo[MT_S_SPECTRE_C];
    m->doomednum=76; m->spawnstate=S_S_ALN1_23; m->spawnhealth=1500;
    m->seestate=S_S_ALN1_34; m->seesound=sfx_s_alnsee; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_ALN1_51; m->painchance=50; m->painsound=sfx_s_alnpn;
    m->meleestate=S_S_ALN1_45; m->missilestate=S_S_ALN1_48;
    m->deathstate=S_S_AL1P_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_alndth;
    m->speed=12; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_alnact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_SPECTRE_D (ednum 167)  [dropped flags: MF_MVIS,MF_SPECTRAL]
    m = &mobjinfo[MT_S_SPECTRE_D];
    m->doomednum=167; m->spawnstate=S_S_ALN1_00; m->spawnhealth=1700;
    m->seestate=S_S_ALN1_02; m->seesound=sfx_s_alnsee; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_ALN1_19; m->painchance=50; m->painsound=sfx_s_alnpn;
    m->meleestate=S_S_ALN1_13; m->missilestate=S_S_ALN1_52;
    m->deathstate=S_S_AL1P_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_alndth;
    m->speed=12; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_alnact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_SPECTRE_E (ednum 168)  [dropped flags: MF_MVIS,MF_SPECTRAL]
    m = &mobjinfo[MT_S_SPECTRE_E];
    m->doomednum=168; m->spawnstate=S_S_ALN1_00; m->spawnhealth=2000;
    m->seestate=S_S_ALN1_02; m->seesound=sfx_s_alnsee; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_ALN1_19; m->painchance=50; m->painsound=sfx_s_alnpn;
    m->meleestate=S_S_ALN1_13; m->missilestate=S_S_ALN1_55;
    m->deathstate=S_S_AL1P_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_alndth;
    m->speed=12; m->radius=24*FRACUNIT; m->height=64*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_alnact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_ENTITY (ednum 128)  [dropped flags: MF_MVIS,MF_SPECTRAL]
    m = &mobjinfo[MT_S_ENTITY];
    m->doomednum=128; m->spawnstate=S_S_MNAM_00; m->spawnhealth=2500;
    m->seestate=S_S_MNAL_02; m->seesound=sfx_s_mnalse; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_MNAL_19; m->painchance=255; m->painsound=sfx_s_alnpn;
    m->meleestate=S_S_MNAL_13; m->missilestate=S_S_MNAL_16;
    m->deathstate=S_S_MNAL_20; m->xdeathstate=S_NULL; m->deathsound=sfx_s_mnaldt;
    m->speed=13; m->radius=130*FRACUNIT; m->height=200*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_alnact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_SUBENTITY (ednum -1)  [dropped flags: MF_MVIS,MF_SPECTRAL]
    m = &mobjinfo[MT_S_SUBENTITY];
    m->doomednum=-1; m->spawnstate=S_S_MNAL_27; m->spawnhealth=990;
    m->seestate=S_S_MNAL_28; m->seesound=sfx_s_alnsee; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_MNAL_40; m->painchance=255; m->painsound=sfx_s_alnpn;
    m->meleestate=S_S_MNAL_34; m->missilestate=S_S_MNAL_37;
    m->deathstate=S_S_MDTH_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_alndth;
    m->speed=14; m->radius=130*FRACUNIT; m->height=200*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_alnact; m->flags=0; m->raisestate=0; m->flags2=0;

    // ---- MT_NEST (ednum 26)
    m = &mobjinfo[MT_S_NEST];
    m->doomednum=26; m->spawnstate=S_S_NEST_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=84*FRACUNIT; m->height=47*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_POD (ednum 198)
    m = &mobjinfo[MT_S_POD];
    m->doomednum=198; m->spawnstate=S_S_PODD_00; m->spawnhealth=1000;
    m->seestate=S_S_PODD_01; m->seesound=sfx_s_slop; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=25*FRACUNIT; m->height=91*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_SOLID|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_C_SHOT (ednum -1)  [dropped flags: MF_SPECTRAL]
    m = &mobjinfo[MT_S_SIGIL_C_SHOT];
    m->doomednum=-1; m->spawnstate=S_S_ZOT3_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_sigil; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ZAP1_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_sglhit;
    m->speed=30*FRACUNIT; m->radius=8*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=70;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_E_OFFSHOOT (ednum -1)  [dropped flags: MF_SPECTRAL]
    m = &mobjinfo[MT_S_SIGIL_E_OFFSHOOT];
    m->doomednum=-1; m->spawnstate=S_S_ZAP6_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_sigil; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ZAP1_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_sglhit;
    m->speed=30*FRACUNIT; m->radius=8*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=10;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_TRAIL (ednum -1)
    m = &mobjinfo[MT_S_SIGIL_TRAIL];
    m->doomednum=-1; m->spawnstate=S_S_ZAP6_03; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_SE_SHOT (ednum -1)  [dropped flags: MF_SPECTRAL]
    m = &mobjinfo[MT_S_SIGIL_SE_SHOT];
    m->doomednum=-1; m->spawnstate=S_S_ZAP7_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_sigil; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ZAP1_02; m->xdeathstate=S_NULL; m->deathsound=sfx_s_sglhit;
    m->speed=18*FRACUNIT; m->radius=20*FRACUNIT; m->height=40*FRACUNIT; m->mass=100; m->damage=30;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_A_ZAP_LEFT (ednum -1)  [dropped flags: MF_SPECTRAL]
    m = &mobjinfo[MT_S_SIGIL_A_ZAP_LEFT];
    m->doomednum=-1; m->spawnstate=S_S_ZOT1_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_sigil; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ZAP1_06; m->xdeathstate=S_NULL; m->deathsound=sfx_s_sglhit;
    m->speed=22*FRACUNIT; m->radius=8*FRACUNIT; m->height=24*FRACUNIT; m->mass=100; m->damage=100;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_A_ZAP_RIGHT (ednum -1)  [dropped flags: MF_SPECTRAL]
    m = &mobjinfo[MT_S_SIGIL_A_ZAP_RIGHT];
    m->doomednum=-1; m->spawnstate=S_S_ZOT1_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_sigil; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ZAP1_06; m->xdeathstate=S_NULL; m->deathsound=sfx_s_sglhit;
    m->speed=22*FRACUNIT; m->radius=8*FRACUNIT; m->height=24*FRACUNIT; m->mass=100; m->damage=50;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_A_GROUND (ednum -1)
    m = &mobjinfo[MT_S_SIGIL_A_GROUND];
    m->doomednum=-1; m->spawnstate=S_S_ZAP5_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=70; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ZAP1_01; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=18*FRACUNIT; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_SHADOW; m->raisestate=0; m->flags2=0;

    // ---- MT_SIGIL_SD_SHOT (ednum -1)  [dropped flags: MF_SPECTRAL]
    m = &mobjinfo[MT_S_SIGIL_SD_SHOT];
    m->doomednum=-1; m->spawnstate=S_S_ZOT2_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_sigil; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_ZAP1_01; m->xdeathstate=S_NULL; m->deathsound=sfx_s_sglhit;
    m->speed=28*FRACUNIT; m->radius=8*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=60;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_SENTINEL (ednum 3006)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_SENTINEL];
    m->doomednum=3006; m->spawnstate=S_S_SEWR_00; m->spawnhealth=100;
    m->seestate=S_S_SEWR_01; m->seesound=sfx_s_sntsee; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_SEWR_06; m->painchance=255; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_S_SEWR_03;
    m->deathstate=S_S_SEWR_07; m->xdeathstate=S_NULL; m->deathsound=sfx_s_sntdth;
    m->speed=7; m->radius=23*FRACUNIT; m->height=53*FRACUNIT; m->mass=300; m->damage=0;
    m->activesound=sfx_s_sntact; m->flags=MF_FLOAT|MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_STALKER (ednum 186)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_STALKER];
    m->doomednum=186; m->spawnstate=S_S_SPID_00; m->spawnhealth=80;
    m->seestate=S_S_SPID_03; m->seesound=sfx_s_spisit; m->reactiontime=8; m->attacksound=sfx_s_spdatk;
    m->painstate=S_S_SPID_24; m->painchance=40; m->painsound=sfx_s_spdatk;
    m->meleestate=S_S_SPID_09; m->missilestate=S_NULL;
    m->deathstate=S_S_SPID_25; m->xdeathstate=S_NULL; m->deathsound=sfx_s_spidth;
    m->speed=16; m->radius=31*FRACUNIT; m->height=25*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_spisit; m->flags=MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_INQUISITOR (ednum 16)
    m = &mobjinfo[MT_S_INQUISITOR];
    m->doomednum=16; m->spawnstate=S_S_ROB3_00; m->spawnhealth=1000;
    m->seestate=S_S_ROB3_02; m->seesound=sfx_s_inqsee; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_S_ROB3_10;
    m->deathstate=S_S_ROB3_20; m->xdeathstate=S_NULL; m->deathsound=sfx_s_inqdth;
    m->speed=12; m->radius=40*FRACUNIT; m->height=110*FRACUNIT; m->mass=1000; m->damage=0;
    m->activesound=sfx_s_inqact; m->flags=MF_SOLID|MF_SHOOTABLE|MF_DROPOFF|MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_INQARM (ednum -1)
    m = &mobjinfo[MT_S_INQARM];
    m->doomednum=-1; m->spawnstate=S_S_RBB3_05; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=25; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOCLIP|MF_NOBLOOD; m->raisestate=0; m->flags2=0;

    // ---- MT_PROGRAMMER (ednum 71)
    m = &mobjinfo[MT_S_PROGRAMMER];
    m->doomednum=71; m->spawnstate=S_S_PRGR_00; m->spawnhealth=1100;
    m->seestate=S_S_PRGR_02; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_s_revbld;
    m->painstate=S_S_PRGR_18; m->painchance=50; m->painsound=sfx_s_prgpn;
    m->meleestate=S_S_PRGR_10; m->missilestate=S_S_PRGR_14;
    m->deathstate=S_S_PRGR_20; m->xdeathstate=S_NULL; m->deathsound=sfx_s_rb2dth;
    m->speed=26; m->radius=45*FRACUNIT; m->height=60*FRACUNIT; m->mass=800; m->damage=4;
    m->activesound=sfx_s_progac; m->flags=MF_NOBLOOD|MF_COUNTKILL|MF_NOTDMATCH; m->raisestate=0; m->flags2=0;

    // ---- MT_PROGRAMMERBASE (ednum -1)
    m = &mobjinfo[MT_S_PROGRAMMERBASE];
    m->doomednum=-1; m->spawnstate=S_S_BASE_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOCLIP|MF_NOBLOOD; m->raisestate=0; m->flags2=0;

    // ---- MT_HOOKSHOT (ednum -1)
    m = &mobjinfo[MT_S_HOOKSHOT];
    m->doomednum=-1; m->spawnstate=S_S_OCLW_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_chain; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_CCLW_00; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=20*FRACUNIT; m->radius=10*FRACUNIT; m->height=14*FRACUNIT; m->mass=100; m->damage=2;
    m->activesound=sfx_s_swish; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_CHAINSHOT (ednum -1)
    m = &mobjinfo[MT_S_CHAINSHOT];
    m->doomednum=-1; m->spawnstate=S_S_TEND_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_tend; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_NULL; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY; m->raisestate=0; m->flags2=0;

    // ---- MT_C_MISSILE (ednum -1)
    m = &mobjinfo[MT_S_C_MISSILE];
    m->doomednum=-1; m->spawnstate=S_S_MICR_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_rlaunc; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_MISL_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_mislht;
    m->speed=20*FRACUNIT; m->radius=10*FRACUNIT; m->height=14*FRACUNIT; m->mass=100; m->damage=7;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_SEEKMISSILE (ednum -1)
    m = &mobjinfo[MT_S_SEEKMISSILE];
    m->doomednum=-1; m->spawnstate=S_S_MISS_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_rlaunc; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_MISL_01; m->xdeathstate=S_NULL; m->deathsound=sfx_s_mislht;
    m->speed=20*FRACUNIT; m->radius=10*FRACUNIT; m->height=14*FRACUNIT; m->mass=100; m->damage=10;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_R_LASER (ednum -1)
    m = &mobjinfo[MT_S_R_LASER];
    m->doomednum=-1; m->spawnstate=S_S_SHT1_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_POW1_09; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=40*FRACUNIT; m->radius=10*FRACUNIT; m->height=8*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_L_LASER (ednum -1)
    m = &mobjinfo[MT_S_L_LASER];
    m->doomednum=-1; m->spawnstate=S_S_SHT1_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_plasma; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_POW1_05; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=40*FRACUNIT; m->radius=10*FRACUNIT; m->height=8*FRACUNIT; m->mass=100; m->damage=1;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_INQGRENADE (ednum -1)
    m = &mobjinfo[MT_S_INQGRENADE];
    m->doomednum=-1; m->spawnstate=S_S_UBAM_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_phoot; m->reactiontime=15; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_BNG2_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_explod;
    m->speed=25*FRACUNIT; m->radius=13*FRACUNIT; m->height=13*FRACUNIT; m->mass=15; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_C_FLAME (ednum -1)
    m = &mobjinfo[MT_S_C_FLAME];
    m->doomednum=-1; m->spawnstate=S_S_FRBL_00; m->spawnhealth=1000;
    m->seestate=S_NULL; m->seesound=sfx_s_flburn; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_NULL;
    m->deathstate=S_S_FRBL_03; m->xdeathstate=S_NULL; m->deathsound=sfx_None;
    m->speed=35*FRACUNIT; m->radius=8*FRACUNIT; m->height=11*FRACUNIT; m->mass=50; m->damage=1;
    m->activesound=sfx_None; m->flags=MF_NOBLOCKMAP|MF_DROPOFF|MF_MISSILE; m->raisestate=0; m->flags2=0;

    // ---- MT_TURRET (ednum 27)
    m = &mobjinfo[MT_S_TURRET];
    m->doomednum=27; m->spawnstate=S_S_TURT_00; m->spawnhealth=125;
    m->seestate=S_S_TURT_01; m->seesound=sfx_None; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_S_TURT_02; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_NULL; m->missilestate=S_S_TURT_02;
    m->deathstate=S_S_BALL_00; m->xdeathstate=S_NULL; m->deathsound=sfx_s_mislht;
    m->speed=0; m->radius=20*FRACUNIT; m->height=16*FRACUNIT; m->mass=10000000; m->damage=0;
    m->activesound=sfx_None; m->flags=MF_COUNTKILL; m->raisestate=0; m->flags2=0;

    // ---- MT_RAT (ednum 85)  [dropped flags: MF_NODIALOG]
    m = &mobjinfo[MT_S_RAT];
    m->doomednum=85; m->spawnstate=S_S_RATT_00; m->spawnhealth=5;
    m->seestate=S_S_RATT_01; m->seesound=sfx_s_ratact; m->reactiontime=8; m->attacksound=sfx_None;
    m->painstate=S_NULL; m->painchance=0; m->painsound=sfx_None;
    m->meleestate=S_S_RATT_05; m->missilestate=S_NULL;
    m->deathstate=S_S_MEAT_16; m->xdeathstate=S_NULL; m->deathsound=sfx_s_ratact;
    m->speed=13; m->radius=10*FRACUNIT; m->height=16*FRACUNIT; m->mass=100; m->damage=0;
    m->activesound=sfx_s_ratact; m->flags=MF_NOBLOOD|MF_COUNTKILL; m->raisestate=0; m->flags2=0;
}
