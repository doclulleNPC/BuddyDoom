// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Additive Heretic MONSTER VARIANTS / extra bosses for the DOOM engine --
//	the "missing" actors that heretic.c (the base 10 monsters + D'Sparil phase 2)
//	did not port.  Ported from crispy-doom's heretic/info.c (frame tables) +
//	heretic/p_enemy.c (action funcs), same additive approach as heretic.c:
//	Heretic_MVar_Init() appends states + mobjinfo to the engine tables at runtime.
//
//	Ported here (see docs / mvar_snippets.txt for the enum + wiring additions):
//	  * Nitrogolem leader (MT_MUMMYLEADER, ednum 45) -- throws a seeking skull
//	    (MT_MUMMYFX1); plus the ghost variants MT_MUMMYGHOST (69) and
//	    MT_MUMMYLEADERGHOST (46) (MF_SHADOW), and MT_MUMMYSOUL released on death.
//	  * Ghost Undead Warrior (MT_KNIGHTGHOST, ednum 65) -- MF_SHADOW, throws the
//	    red/ghost axe MT_REDAXE instead of the green MT_HKNIGHTAXE.
//	  * Gargoyle leader (MT_IMPLEADER, ednum 5) -- fires MT_IMPBALL fireballs.
//	  * D'Sparil phase 1 (MT_SORCERER1, ednum 7) -- the serpent-mounted sorcerer,
//	    fires MT_SRCRFX1; on death "rises" into the existing phase-2 MT_HDSPARIL.
//
//	SIMPLIFICATIONS (kept faithful to frame data, but trimmed to the DOOM engine):
//	  * Ghosts get MF_SHADOW only; the Heretic "ghosts are immune to normal weapons
//	    (hurt only by staff/gauntlets/etc.)" rule is OMITTED (no such hook here).
//	  * MummyFX1 seeking uses the engine's A_Tracer (revenant homing, tracer=target)
//	    exactly like the lich whirlwind in heretic.c, not Heretic's P_SeekerMissile.
//	  * MT_IMPCHUNK1/2 (gib chunks) and the imp CRASH/A_ImpExplode path are OMITTED
//	    (they need IMPX frames 12-17 that hereticstuff.wad's base imp did not extract;
//	    fine in heretic_mode from native art but risky in DOOM+PWAD mode).
//	  * D'Sparil phase 1: the special1-based "walk fast after pain" (A_Sor1Chase/
//	    A_Sor1Pain) and the low-health double-attack loop are dropped (no mobj_t
//	    special1 field); the health-tiered 1-vs-3 fireball spread is kept.  The long
//	    A_SorZap teleport-flicker death is trimmed; A_SorcererRise spawns MT_HDSPARIL
//	    directly in its walk state (no separate S_SOR2_RISE animation / MT_SOR2TELEFADE).
//	  * MF_TRANSLUCENT / MF2_* (FOOTCLIP/PASSMOBJ/WINDTHRUST/THRUGHOST/NOTELEPORT/
//	    FIREDAMAGE/BOSS) from crispy are DROPPED -- not defined / not honoured here.
//
//	MUST be called AFTER Heretic_Init() (it references MT_HDSPARIL, S_HMUM_*,
//	S_HIMP_*, S_HKNI_* filled there) and BEFORE any variant spawns / R_Init.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "info.h"
#include "m_random.h"
#include "m_fixed.h"
#include "tables.h"		// finecosine/finesine, ANGLETOFINESHIFT, ANG90 (fireball spread)
#include "sounds.h"
#include "p_mobj.h"
#include "heretic.h"

extern state_t *states;
extern mobjinfo_t *mobjinfo;

// engine helpers (same hand-declared set as heretic.c)
extern void	A_Look (mobj_t*);
extern void	A_Chase (mobj_t*);
extern void	A_FaceTarget (mobj_t*);
extern void	A_Pain (mobj_t*);
extern void	A_Scream (mobj_t*);
extern void	A_Fall (mobj_t*);
extern void	A_Tracer (mobj_t*);		// revenant homing -- reused for the mummy skull
extern boolean	P_CheckMeleeRange (mobj_t*);
extern void	P_DamageMobj (mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage);
extern void	S_StartSound (void* origin, int sfx_id);
extern mobj_t*	P_SpawnMobj (fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
extern boolean	P_SetMobjState (mobj_t* mobj, statenum_t state);
extern mobj_t*	P_SpawnMissile (mobj_t* source, mobj_t* dest, mobjtype_t type);

// reused from heretic.c (non-static there)
extern void	A_ContMobjSound (mobj_t*);	// looping projectile whoosh (sfx_firsht)

#define HITDICE(d)	(((P_Random () & 7) + 1) * (d))
#define ANG3		(ANG90 / 30)		// ~3 degrees, the sorcerer fireball spread

// ---------------------------------------------------------------------------
// Action functions (crispy heretic/p_enemy.c, adapted to DOOM's 1-arg signature).
// ---------------------------------------------------------------------------

// Mummy leader ranged attack: melee HITDICE(2), else launch a seeking skull.  The
// skull's tracer is set to the target so the engine's A_Tracer steers it home.
void A_MummyAttack2 (mobj_t* actor)
{
    mobj_t*	mo;
    if (!actor->target)
	return;
    if (P_CheckMeleeRange (actor))
	{ P_DamageMobj (actor->target, actor, actor, HITDICE (2)); return; }
    mo = P_SpawnMissile (actor, actor->target, MT_HMUMMYFX1);
    if (mo)
	mo->tracer = actor->target;		// seek toward the target (A_Tracer)
}

// Soul released when a golem/mummy dies -- floats gently upward.
void A_MummySoul (mobj_t* mummy)
{
    mobj_t*	mo;
    mo = P_SpawnMobj (mummy->x, mummy->y, mummy->z + 10*FRACUNIT, MT_HMUMMYSOUL);
    if (mo)
	mo->momz = FRACUNIT;
}

// Ghost knight melee HITDICE(3), else throw the RED (ghost) axe.
void A_KnightGhostAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_CheckMeleeRange (actor))
	{ P_DamageMobj (actor->target, actor, actor, HITDICE (3)); S_StartSound (actor, sfx_h_kgtat2); return; }
    S_StartSound (actor, actor->info->attacksound);
    P_SpawnMissile (actor, actor->target, MT_HREDAXE);
}

// Gargoyle leader fireball attack: melee 5..12, else hurl a fireball.
void A_ImpMsAttack2 (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, actor->info->attacksound);
    if (P_CheckMeleeRange (actor))
	{ P_DamageMobj (actor->target, actor, actor, 5 + (P_Random () & 7)); return; }
    P_SpawnMissile (actor, actor->target, MT_HIMPBALL);
}

// Spawn one MT_HSRCRFX1 aimed at dest, then re-aim it by an angle offset (the engine
// has no P_SpawnMissileAngle, so we rotate the resulting velocity vector by hand).
static void Srcr1SpreadShot (mobj_t* src, mobj_t* dest, angle_t da)
{
    mobj_t*	mo = P_SpawnMissile (src, dest, MT_HSRCRFX1);
    angle_t	an;
    if (!mo)
	return;
    an = mo->angle + da;
    mo->angle = an;
    an >>= ANGLETOFINESHIFT;
    mo->momx = FixedMul (mo->info->speed, finecosine[an]);
    mo->momy = FixedMul (mo->info->speed, finesine[an]);
}

// D'Sparil phase 1 (serpent-mounted sorcerer) attack: brutal melee HITDICE(8), else
// spit fireballs -- one at high health, a three-way spread once wounded.
void A_Srcr1Attack (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, actor->info->attacksound);
    if (P_CheckMeleeRange (actor))
	{ P_DamageMobj (actor->target, actor, actor, HITDICE (8)); return; }
    if (actor->health > (actor->info->spawnhealth / 3) * 2)
	P_SpawnMissile (actor, actor->target, MT_HSRCRFX1);	// one fireball
    else
    {								// three-way spread
	P_SpawnMissile (actor, actor->target, MT_HSRCRFX1);
	Srcr1SpreadShot (actor, actor->target, (angle_t)(-(int)(ANG3 * 3)));
	Srcr1SpreadShot (actor, actor->target, ANG3 * 3);
    }
}

// A_SorZap: crispy spawns teleport-fade decorations; we just play the zap sound.
void A_SorZap (mobj_t* actor)
{
    S_StartSound (actor, sfx_h_sorzap);
}

// D'Sparil phase 1 death climax: "rise" into the phase-2 sorcerer (existing MT_HDSPARIL).
void A_SorcererRise (mobj_t* actor)
{
    mobj_t*	mo;
    actor->flags &= ~MF_SOLID;
    mo = P_SpawnMobj (actor->x, actor->y, actor->z, MT_HDSPARIL);
    if (mo)
    {
	P_SetMobjState (mo, mobjinfo[MT_HDSPARIL].seestate);	// straight into the fight
	mo->angle  = actor->angle;
	mo->target = actor->target;
    }
}

// ---------------------------------------------------------------------------
// Table fill (same helper style as heretic.c's ST()).
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

void Heretic_MVar_Init (void)
{
    mobjinfo_t*	m;

    // ====================================================================
    // Mummy leader (Nitrogolem leader) + ghost variants + soul + seeking skull.
    // Reuses the base golem art (SPR_HMUM) and base LOOK/WALK/ATK/PAIN states.
    // ====================================================================

    // ---- Leader missile attack (SPR_HMUM frames 23 / 24-fullbright) ----
    ST (S_HMUML_ATK1, SPR_HMUM, 23,     5, (actionf_p1)A_FaceTarget,   S_HMUML_ATK2);
    ST (S_HMUML_ATK2, SPR_HMUM, 32792,  5, (actionf_p1)A_FaceTarget,   S_HMUML_ATK3);
    ST (S_HMUML_ATK3, SPR_HMUM, 23,     5, (actionf_p1)A_FaceTarget,   S_HMUML_ATK4);
    ST (S_HMUML_ATK4, SPR_HMUM, 32792,  5, (actionf_p1)A_FaceTarget,   S_HMUML_ATK5);
    ST (S_HMUML_ATK5, SPR_HMUM, 23,     5, (actionf_p1)A_FaceTarget,   S_HMUML_ATK6);
    ST (S_HMUML_ATK6, SPR_HMUM, 32792, 15, (actionf_p1)A_MummyAttack2, S_HMUM_WALK1);

    // ---- Soul-releasing death chain (SPR_HMUM 8..15, A_MummySoul on frame 10) ----
    ST (S_HMUMV_DIE1, SPR_HMUM,  8,  5, NULL,                     S_HMUMV_DIE2);
    ST (S_HMUMV_DIE2, SPR_HMUM,  9,  5, (actionf_p1)A_Scream,     S_HMUMV_DIE3);
    ST (S_HMUMV_DIE3, SPR_HMUM, 10,  5, (actionf_p1)A_MummySoul,  S_HMUMV_DIE4);
    ST (S_HMUMV_DIE4, SPR_HMUM, 11,  5, NULL,                     S_HMUMV_DIE5);
    ST (S_HMUMV_DIE5, SPR_HMUM, 12,  5, (actionf_p1)A_Fall,       S_HMUMV_DIE6);
    ST (S_HMUMV_DIE6, SPR_HMUM, 13,  5, NULL,                     S_HMUMV_DIE7);
    ST (S_HMUMV_DIE7, SPR_HMUM, 14,  5, NULL,                     S_HMUMV_DIE8);
    ST (S_HMUMV_DIE8, SPR_HMUM, 15, -1, NULL,                     S_NULL);

    // ---- The released soul (SPR_HMUM 16..22) ----
    ST (S_HMUM_SOUL1, SPR_HMUM, 16, 5, NULL, S_HMUM_SOUL2);
    ST (S_HMUM_SOUL2, SPR_HMUM, 17, 5, NULL, S_HMUM_SOUL3);
    ST (S_HMUM_SOUL3, SPR_HMUM, 18, 5, NULL, S_HMUM_SOUL4);
    ST (S_HMUM_SOUL4, SPR_HMUM, 19, 9, NULL, S_HMUM_SOUL5);
    ST (S_HMUM_SOUL5, SPR_HMUM, 20, 5, NULL, S_HMUM_SOUL6);
    ST (S_HMUM_SOUL6, SPR_HMUM, 21, 5, NULL, S_HMUM_SOUL7);
    ST (S_HMUM_SOUL7, SPR_HMUM, 22, 5, NULL, S_NULL);

    // ---- Seeking skull projectile (SPR_HMUF = FX15); A_Tracer homing ----
    ST (S_HMUMFX1_1,  SPR_HMUF, 32768, 5, (actionf_p1)A_ContMobjSound, S_HMUMFX1_2);
    ST (S_HMUMFX1_2,  SPR_HMUF, 32769, 5, (actionf_p1)A_Tracer,        S_HMUMFX1_3);
    ST (S_HMUMFX1_3,  SPR_HMUF, 32770, 5, NULL,                        S_HMUMFX1_4);
    ST (S_HMUMFX1_4,  SPR_HMUF, 32769, 5, (actionf_p1)A_Tracer,        S_HMUMFX1_1);
    ST (S_HMUMFXI1_1, SPR_HMUF, 32771, 5, NULL,                        S_HMUMFXI1_2);
    ST (S_HMUMFXI1_2, SPR_HMUF, 32772, 5, NULL,                        S_HMUMFXI1_3);
    ST (S_HMUMFXI1_3, SPR_HMUF, 32773, 5, NULL,                        S_HMUMFXI1_4);
    ST (S_HMUMFXI1_4, SPR_HMUF, 32774, 5, NULL,                        S_NULL);

    // ---- MT_HMUMMYLEADER (golem leader, 100 hp, melee + seeking skull) ----
    m = &mobjinfo[MT_HMUMMYLEADER];
    m->doomednum = -1;        m->spawnstate  = S_HMUM_LOOK1; m->spawnhealth = 100;
    m->seestate  = S_HMUM_WALK1; m->seesound  = sfx_h_mumsit;  m->reactiontime = 8;
    m->attacksound = sfx_h_mumat1; m->painstate = S_HMUM_PAIN1; m->painchance = 64;
    m->painsound = sfx_h_mumpai; m->meleestate = S_HMUM_ATK1;  m->missilestate = S_HMUML_ATK1;
    m->deathstate = S_HMUMV_DIE1; m->xdeathstate = S_NULL;   m->deathsound = sfx_h_mumdth;
    m->speed = 12; m->radius = 22*FRACUNIT; m->height = 62*FRACUNIT; m->mass = 75;
    m->damage = 0; m->activesound = sfx_h_mumsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate = S_NULL;

    // ---- MT_HMUMMYGHOST (ghost golem, 80 hp, melee only, MF_SHADOW) ----
    m = &mobjinfo[MT_HMUMMYGHOST];
    m->doomednum = -1;        m->spawnstate  = S_HMUM_LOOK1; m->spawnhealth = 80;
    m->seestate  = S_HMUM_WALK1; m->seesound  = sfx_h_mumsit;  m->reactiontime = 8;
    m->attacksound = sfx_h_mumat1; m->painstate = S_HMUM_PAIN1; m->painchance = 128;
    m->painsound = sfx_h_mumpai; m->meleestate = S_HMUM_ATK1;  m->missilestate = S_NULL;
    m->deathstate = S_HMUMV_DIE1; m->xdeathstate = S_NULL;   m->deathsound = sfx_h_mumdth;
    m->speed = 12; m->radius = 22*FRACUNIT; m->height = 62*FRACUNIT; m->mass = 75;
    m->damage = 0; m->activesound = sfx_h_mumsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_SHADOW; m->raisestate = S_NULL;

    // ---- MT_HMUMMYLEADERGHOST (ghost golem leader, 100 hp, MF_SHADOW) ----
    m = &mobjinfo[MT_HMUMMYLEADERGHOST];
    m->doomednum = -1;        m->spawnstate  = S_HMUM_LOOK1; m->spawnhealth = 100;
    m->seestate  = S_HMUM_WALK1; m->seesound  = sfx_h_mumsit;  m->reactiontime = 8;
    m->attacksound = sfx_h_mumat1; m->painstate = S_HMUM_PAIN1; m->painchance = 64;
    m->painsound = sfx_h_mumpai; m->meleestate = S_HMUM_ATK1;  m->missilestate = S_HMUML_ATK1;
    m->deathstate = S_HMUMV_DIE1; m->xdeathstate = S_NULL;   m->deathsound = sfx_h_mumdth;
    m->speed = 12; m->radius = 22*FRACUNIT; m->height = 62*FRACUNIT; m->mass = 75;
    m->damage = 0; m->activesound = sfx_h_mumsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_SHADOW; m->raisestate = S_NULL;

    // ---- MT_HMUMMYSOUL (drifting soul, non-interacting) ----
    m = &mobjinfo[MT_HMUMMYSOUL];
    m->doomednum = -1;        m->spawnstate  = S_HMUM_SOUL1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;    m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;      m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;     m->missilestate = S_NULL;
    m->deathstate = S_NULL;      m->xdeathstate = S_NULL;    m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- MT_HMUMMYFX1 (seeking skull projectile) ----
    m = &mobjinfo[MT_HMUMMYFX1];
    m->doomednum = -1;        m->spawnstate  = S_HMUMFX1_1;  m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;    m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;      m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;     m->missilestate = S_NULL;
    m->deathstate = S_HMUMFXI1_1; m->xdeathstate = S_NULL;   m->deathsound = sfx_firxpl;
    m->speed = 9*FRACUNIT; m->radius = 8*FRACUNIT; m->height = 14*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ====================================================================
    // Ghost Undead Warrior (MF_SHADOW knight) + red/ghost axe.
    // Reuses base knight art/states (SPR_HKNI); own attack chain throws MT_HREDAXE.
    // ====================================================================
    ST (S_HKNIG_ATK1, SPR_HKNI, 4, 10, (actionf_p1)A_FaceTarget,       S_HKNIG_ATK2);
    ST (S_HKNIG_ATK2, SPR_HKNI, 5,  8, (actionf_p1)A_FaceTarget,       S_HKNIG_ATK3);
    ST (S_HKNIG_ATK3, SPR_HKNI, 6,  8, (actionf_p1)A_KnightGhostAttack,S_HKNIG_ATK4);
    ST (S_HKNIG_ATK4, SPR_HKNI, 4, 10, (actionf_p1)A_FaceTarget,       S_HKNIG_ATK5);
    ST (S_HKNIG_ATK5, SPR_HKNI, 5,  8, (actionf_p1)A_FaceTarget,       S_HKNIG_ATK6);
    ST (S_HKNIG_ATK6, SPR_HKNI, 6,  8, (actionf_p1)A_KnightGhostAttack,S_HKNI_WALK1);

    // red axe projectile (SPR_HKRX = RAXE); full-bright spin, then splat
    ST (S_HREDAXE1,  SPR_HKRX, 32768, 5, NULL, S_HREDAXE2);
    ST (S_HREDAXE2,  SPR_HKRX, 32769, 5, NULL, S_HREDAXE1);
    ST (S_HREDAXEX1, SPR_HKRX, 32770, 6, NULL, S_HREDAXEX2);
    ST (S_HREDAXEX2, SPR_HKRX, 32771, 6, NULL, S_HREDAXEX3);
    ST (S_HREDAXEX3, SPR_HKRX, 32772, 6, NULL, S_NULL);

    m = &mobjinfo[MT_HKNIGHTGHOST];
    m->doomednum = -1;        m->spawnstate  = S_HKNI_STND1; m->spawnhealth = 200;
    m->seestate  = S_HKNI_WALK1; m->seesound  = sfx_h_kgtsit;  m->reactiontime = 8;
    m->attacksound = sfx_h_kgtatk; m->painstate = S_HKNI_PAIN1; m->painchance = 100;
    m->painsound = sfx_h_kgtpai; m->meleestate = S_HKNIG_ATK1; m->missilestate = S_HKNIG_ATK1;
    m->deathstate = S_HKNI_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_h_kgtdth;
    m->speed = 12; m->radius = 24*FRACUNIT; m->height = 78*FRACUNIT; m->mass = 150;
    m->damage = 0; m->activesound = sfx_h_kgtsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_SHADOW; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HREDAXE];
    m->doomednum = -1;        m->spawnstate  = S_HREDAXE1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;    m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;      m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;     m->missilestate = S_NULL;
    m->deathstate = S_HREDAXEX1; m->xdeathstate = S_NULL;    m->deathsound = sfx_firxpl;
    m->speed = 9*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 8*FRACUNIT; m->mass = 100;
    m->damage = 7; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ====================================================================
    // Gargoyle leader (imp leader): fires fireballs.  Reuses base imp art/states
    // (SPR_HIMP); own missile-attack chain firing MT_HIMPBALL.
    // ====================================================================
    ST (S_HIMPL_MSATK1, SPR_HIMP, 3, 6, (actionf_p1)A_FaceTarget,   S_HIMPL_MSATK2);
    ST (S_HIMPL_MSATK2, SPR_HIMP, 4, 6, (actionf_p1)A_FaceTarget,   S_HIMPL_MSATK3);
    ST (S_HIMPL_MSATK3, SPR_HIMP, 5, 6, (actionf_p1)A_ImpMsAttack2, S_HIMP_FLY1);

    // imp fireball (SPR_HIMB = FX10)
    ST (S_HIMB1,  SPR_HIMB, 32768, 6, NULL, S_HIMB2);
    ST (S_HIMB2,  SPR_HIMB, 32769, 6, NULL, S_HIMB3);
    ST (S_HIMB3,  SPR_HIMB, 32770, 6, NULL, S_HIMB1);
    ST (S_HIMBX1, SPR_HIMB, 32771, 5, NULL, S_HIMBX2);
    ST (S_HIMBX2, SPR_HIMB, 32772, 5, NULL, S_HIMBX3);
    ST (S_HIMBX3, SPR_HIMB, 32773, 5, NULL, S_HIMBX4);
    ST (S_HIMBX4, SPR_HIMB, 32774, 5, NULL, S_NULL);

    m = &mobjinfo[MT_HIMPLEADER];
    m->doomednum = -1;        m->spawnstate  = S_HIMP_LOOK1; m->spawnhealth = 80;
    m->seestate  = S_HIMP_FLY1; m->seesound   = sfx_h_impsit;  m->reactiontime = 8;
    m->attacksound = sfx_h_impat2; m->painstate = S_HIMP_PAIN1; m->painchance = 200;
    m->painsound = sfx_h_imppai; m->meleestate = S_NULL;       m->missilestate = S_HIMPL_MSATK1;
    m->deathstate = S_HIMP_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_h_impdth;
    m->speed = 10; m->radius = 16*FRACUNIT; m->height = 36*FRACUNIT; m->mass = 50;
    m->damage = 0; m->activesound = sfx_h_impsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_FLOAT|MF_NOGRAVITY|MF_COUNTKILL; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HIMPBALL];
    m->doomednum = -1;        m->spawnstate  = S_HIMB1;      m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;    m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;      m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;     m->missilestate = S_NULL;
    m->deathstate = S_HIMBX1;    m->xdeathstate = S_NULL;    m->deathsound = sfx_firxpl;
    m->speed = 10*FRACUNIT; m->radius = 8*FRACUNIT; m->height = 8*FRACUNIT; m->mass = 100;
    m->damage = 1; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ====================================================================
    // D'Sparil phase 1 (MT_SORCERER1): the serpent-mounted sorcerer.  Fires
    // MT_HSRCRFX1; on death rises into the phase-2 boss (existing MT_HDSPARIL).
    // New art SPR_HSR1 (native SRCR).  2000 hp.
    // ====================================================================
    ST (S_HSR1_LOOK1, SPR_HSR1, 0, 10, (actionf_p1)A_Look,        S_HSR1_LOOK2);
    ST (S_HSR1_LOOK2, SPR_HSR1, 1, 10, (actionf_p1)A_Look,        S_HSR1_LOOK1);
    ST (S_HSR1_WALK1, SPR_HSR1, 0,  5, (actionf_p1)A_Chase,       S_HSR1_WALK2);
    ST (S_HSR1_WALK2, SPR_HSR1, 1,  5, (actionf_p1)A_Chase,       S_HSR1_WALK3);
    ST (S_HSR1_WALK3, SPR_HSR1, 2,  5, (actionf_p1)A_Chase,       S_HSR1_WALK4);
    ST (S_HSR1_WALK4, SPR_HSR1, 3,  5, (actionf_p1)A_Chase,       S_HSR1_WALK1);
    ST (S_HSR1_PAIN1, SPR_HSR1, 16, 6, (actionf_p1)A_Pain,        S_HSR1_WALK1);
    ST (S_HSR1_ATK1,  SPR_HSR1, 16, 7, (actionf_p1)A_FaceTarget,  S_HSR1_ATK2);
    ST (S_HSR1_ATK2,  SPR_HSR1, 17, 6, (actionf_p1)A_FaceTarget,  S_HSR1_ATK3);
    ST (S_HSR1_ATK3,  SPR_HSR1, 18, 10, (actionf_p1)A_Srcr1Attack, S_HSR1_WALK1);
    ST (S_HSR1_DIE1,  SPR_HSR1, 4,  7, NULL,                      S_HSR1_DIE2);
    ST (S_HSR1_DIE2,  SPR_HSR1, 5,  7, (actionf_p1)A_Scream,      S_HSR1_DIE3);
    ST (S_HSR1_DIE3,  SPR_HSR1, 6,  7, NULL,                      S_HSR1_DIE4);
    ST (S_HSR1_DIE4,  SPR_HSR1, 7,  6, NULL,                      S_HSR1_DIE5);
    ST (S_HSR1_DIE5,  SPR_HSR1, 8,  6, NULL,                      S_HSR1_DIE6);
    ST (S_HSR1_DIE6,  SPR_HSR1, 9,  6, NULL,                      S_HSR1_DIE7);
    ST (S_HSR1_DIE7,  SPR_HSR1, 10, 6, NULL,                      S_HSR1_DIE8);
    ST (S_HSR1_DIE8,  SPR_HSR1, 11, 25, (actionf_p1)A_SorZap,     S_HSR1_DIE9);
    ST (S_HSR1_DIE9,  SPR_HSR1, 15, -1, (actionf_p1)A_SorcererRise, S_NULL);

    // sorcerer 1 fireball (SPR_HS1B = FX14)
    ST (S_HS1FX1_1,  SPR_HS1B, 32768, 6, NULL, S_HS1FX1_2);
    ST (S_HS1FX1_2,  SPR_HS1B, 32769, 6, NULL, S_HS1FX1_3);
    ST (S_HS1FX1_3,  SPR_HS1B, 32770, 6, NULL, S_HS1FX1_1);
    ST (S_HS1FXI1_1, SPR_HS1B, 32771, 5, NULL, S_HS1FXI1_2);
    ST (S_HS1FXI1_2, SPR_HS1B, 32772, 5, NULL, S_HS1FXI1_3);
    ST (S_HS1FXI1_3, SPR_HS1B, 32773, 5, NULL, S_HS1FXI1_4);
    ST (S_HS1FXI1_4, SPR_HS1B, 32774, 5, NULL, S_HS1FXI1_5);
    ST (S_HS1FXI1_5, SPR_HS1B, 32775, 5, NULL, S_NULL);

    m = &mobjinfo[MT_HSORC1];
    m->doomednum = -1;        m->spawnstate  = S_HSR1_LOOK1; m->spawnhealth = 2000;
    m->seestate  = S_HSR1_WALK1; m->seesound  = sfx_h_sorsit;  m->reactiontime = 8;
    m->attacksound = sfx_h_soratk; m->painstate = S_HSR1_PAIN1; m->painchance = 56;
    m->painsound = sfx_h_sorpai; m->meleestate = S_NULL;       m->missilestate = S_HSR1_ATK1;
    m->deathstate = S_HSR1_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_h_sorzap;
    m->speed = 16; m->radius = 28*FRACUNIT; m->height = 100*FRACUNIT; m->mass = 800;
    m->damage = 0; m->activesound = sfx_h_soract;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate = S_NULL;

    m = &mobjinfo[MT_HSRCRFX1];
    m->doomednum = -1;        m->spawnstate  = S_HS1FX1_1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;    m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;      m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;     m->missilestate = S_NULL;
    m->deathstate = S_HS1FXI1_1; m->xdeathstate = S_NULL;    m->deathsound = sfx_firxpl;
    m->speed = 20*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 10*FRACUNIT; m->mass = 100;
    m->damage = 10; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;
}
