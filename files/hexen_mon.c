// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Additive Hexen MONSTERS + BOSSES, wave 2 -- the combatants missing from
//	files/hexen.c.  Same mechanism as hexen.c / heretic.c: states + mobjinfo are
//	appended to the engine tables at runtime (Hexen_Mon_Init), the enum slots live
//	at the end of statenum_t/mobjtype_t/spritenum_t (info.h), and the sprites use
//	the NATIVE Hexen 4-char codes registered directly in info.c sprnames_builtin[]
//	(KORX/SORC/DEM2/... , no remap; enum names X-prefixed).
//
//	Ships here (files/hexen.c already covers Ettin/Centaur/Slaughtaur/ChaosSerpent/
//	Afrit/Reiver/DarkBishop/Wendigo/Stalker+Leader/DeathWyvern + poison gas):
//	  - MT_XDEMON2      -- the second Chaos Serpent variant (Hexen ednum 8080),
//	                       reuses the demon logic + its own MT_XDEMON2_FX fireball.
//	  - MT_XWRAITHB     -- the buried Reiver/Wraith (ednum 10011): invisible +
//	                       intangible until it sees you, then rises and fights
//	                       (reuses every MT_XWRAITH state for chase/attack/death).
//	  - MT_XKORAX       -- Korax, the final boss (ednum 10200): faithful state
//	                       graph, 6-arm missile volley, lightning command, and the
//	                       real bone-pop death that scatters 6 spirits.
//	  - MT_XHERESIARCH  -- the Heresiarch / Sorcerer boss (ednum 10080): faithful
//	                       chase + spell-cast attack + the real 19-frame death.
//	  + projectiles: MT_XKORAX_BOLT, MT_XKORAX_SPIRIT, MT_XSORCFX1..4.
//
//	The Fighter/Cleric/Mage class bosses are SKIPPED (they need the player-class
//	weapon FX actors, out of scope).  See hxmon_snippets.txt [NOTES] for every
//	boss simplification (Korax's ACS teleport ritual + Heresiarch's orbiting balls
//	are the big ones, both dropped -- this engine's mobj_t has no special1/args).
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "info.h"
#include "m_random.h"
#include "m_fixed.h"
#include "tables.h"		// finesine/finecosine/ANG45/ANG90/ANGLETOFINESHIFT
#include "sounds.h"
#include "p_mobj.h"
#include "d_player.h"		// player_t -- A_XMinotaurAtk3 squishes the view
#include "hexen.h"

#ifndef ONFLOORZ
#define ONFLOORZ	MININT		// p_local.h; not pulled in here (enum clashes)
#endif

extern state_t *states;
extern mobjinfo_t *mobjinfo;
extern int num_mobjtypes;

// engine action funcs / helpers we call (declared by hand -- no public header)
extern void	A_Look (mobj_t*);
extern void	A_Chase (mobj_t*);
extern void	A_FaceTarget (mobj_t*);
extern void	A_Pain (mobj_t*);
extern void	A_Scream (mobj_t*);
extern void	A_Fall (mobj_t*);
extern void	A_Tracer (mobj_t*);		// revenant homing -- reused for MT_XSORCFX1
extern boolean	P_CheckMeleeRange (mobj_t*);
extern void	P_DamageMobj (mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage);
extern void	S_StartSound (void* origin, int sfx_id);
extern mobj_t*	P_SpawnMobj (fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
extern boolean	P_SetMobjState (mobj_t* mobj, statenum_t state);
extern mobj_t*	P_SpawnMonsterChecked (fixed_t x, fixed_t y, mobjtype_t type);
extern mobj_t*	P_SpawnMissile (mobj_t* source, mobj_t* dest, mobjtype_t type);
extern void	P_CheckMissileSpawn (mobj_t* th);
extern fixed_t	P_AproxDistance (fixed_t dx, fixed_t dy);

// Hexen's HITDICE(d) melee damage = ((P_Random() & 7) + 1) * d.  (mirrors hexen.c)
#define HITDICE(d)	(((P_Random () & 7) + 1) * (d))

// engine action funcs shared with hexen.c (Chaos Serpent behaviours reused by the
// second variant): declared there, linked at load time.
extern void	A_DemonAttack1 (mobj_t*);	// melee, HITDICE(2)
extern void	A_DemonAttack2 (mobj_t*);	// fire-breath fireball (spawns MT_XDEMON_FX)

// A_DemonAttack2 in hexen.c hardcodes MT_XDEMON_FX; the second serpent wants its own
// MT_XDEMON2_FX, so it gets a local variant.
void A_Demon2Attack2 (mobj_t* actor)
{
    mobj_t* mo;
    if (!actor->target)
	return;
    mo = P_SpawnMissile (actor, actor->target, MT_XDEMON2_FX);
    if (mo)
    {
	mo->z += 30*FRACUNIT;
	S_StartSound (actor, actor->info->attacksound);
    }
}

// ---------------------------------------------------------------------------
// Buried Wraith (crispy MT_WRAITHB + A_WraithRaiseInit/A_WraithRaise).  It spawns
// intangible + invisible (no MF_SOLID/MF_SHOOTABLE, MF2_DONTDRAW), scans for prey,
// then on sight rises out of the ground -- becoming visible + shootable -- and drops
// into the ordinary Reiver chase/attack/death states (files/hexen.c, MT_XWRAITH).
// Simplification: no floorclip sink or dirt spray (this engine lacks A_RaiseMobj /
// P_SpawnDirt); the "burial" is just the invisible/intangible pre-sight state.
// ---------------------------------------------------------------------------
void A_WraithBRaiseInit (mobj_t* actor)
{
    actor->flags2 &= ~MF2_DONTDRAW;			// become visible
    actor->flags  |=  MF_SHOOTABLE | MF_SOLID;		// become tangible
}

// ---------------------------------------------------------------------------
// Korax (crispy hexen/p_enemy.c A_Korax*).  The ACS-driven ritual is stripped:
//   * A_KoraxChase/Step/Step2 just chase (+ a step stomp sound); the two teleport
//     TIDs and the half-health ACS script (249) are gone (no P_FindMobjFromTID /
//     P_StartACS here, and mobj_t has no special1/special2 to latch them).
//   * A_KoraxCommand spawns the lightning column (MT_XKORAX_BOLT) but skips the
//     ACS "summon" scripts 250-254 that would call in reinforcements.
//   * A_KoraxMissile fires all six arm projectiles at once -- faithful: a random
//     one of Korax's six minion missile types, sprayed as a fan (the exact per-arm
//     xyz offsets are simplified to a center-spawned angular fan).
//   * A_KoraxBonePop is the REAL death payload: it scatters six roaming spirits.
// ---------------------------------------------------------------------------

// Fire n copies of `type` as an angular fan aimed at the target (approximates
// Korax's six arm projectiles without needing a P_SpawnMissileXYZ).
static void Korax_FireFan (mobj_t* actor, mobjtype_t type, int n)
{
    int i;
    if (!actor->target)
	return;
    for (i = 0; i < n; i++)
    {
	mobj_t*	mo = P_SpawnMissile (actor, actor->target, type);
	angle_t	a;
	fixed_t	sp;
	if (!mo)
	    continue;
	mo->angle += (angle_t)((i - (n - 1) / 2) * (ANG45 / 8));	// ~5.6 deg spread
	a  = mo->angle >> ANGLETOFINESHIFT;
	sp = mo->info->speed;
	mo->momx = FixedMul (sp, finecosine[a]);
	mo->momy = FixedMul (sp, finesine[a]);
    }
}

void A_KoraxChase (mobj_t* actor)
{
    A_Chase (actor);
    // Faithful "decide to attack" nudge: occasionally break off into the missile
    // sequence even mid-stride (crispy rolls P_Random()<30 here).
    if (actor->target && actor->info->missilestate && P_Random () < 30)
	P_SetMobjState (actor, actor->info->missilestate);
}

void A_KoraxStep  (mobj_t* actor) { A_Chase (actor); }
void A_KoraxStep2 (mobj_t* actor) { S_StartSound (NULL, sfx_metal); A_Chase (actor); }

// Decide: mostly a missile volley, occasionally the lightning command.
void A_KoraxDecide (mobj_t* actor)
{
    if (P_Random () < 220)
	P_SetMobjState (actor, S_XKRX_MIS1);
    else
	P_SetMobjState (actor, S_XKRX_CMD1);
}

void A_KoraxMissile (mobj_t* actor)
{
    static const mobjtype_t types[6] = {
	MT_XWRAITH_FX, MT_XDEMON_FX, MT_XDEMON2_FX,
	MT_XFIREDEMON_FX, MT_XCENTAUR_FX, MT_XSTALKER_FX
    };
    if (!actor->target)
	return;
    S_StartSound (actor, actor->info->attacksound);
    Korax_FireFan (actor, types[P_Random () % 6], 6);
}

void A_KoraxCommand (mobj_t* actor)
{
    fixed_t	x, y, z;
    angle_t	ang;

    S_StartSound (actor, sfx_bospn);

    // Shoot a stream of lightning up beside Korax (crispy: 27u out, 120u up).
    ang = (actor->angle - ANG90) >> ANGLETOFINESHIFT;
    x = actor->x + FixedMul (27*FRACUNIT, finecosine[ang]);
    y = actor->y + FixedMul (27*FRACUNIT, finesine[ang]);
    z = actor->z + 120*FRACUNIT;
    P_SpawnMobj (x, y, z, MT_XKORAX_BOLT);
}

// Death payload: burst into six roaming spirits spread equiangularly.  Simplified
// from crispy (one spirit TYPE instead of six identical ones; the swarm-around-Korax
// P_SeekerMissile weave and the ACS death script 255 are dropped -- the spirits just
// drift outward and expire).  A_Fall (== crispy A_NoBlocking) runs a frame later.
void A_KoraxBonePop (mobj_t* actor)
{
    int	i;
    for (i = 0; i < 6; i++)
    {
	mobj_t*	mo = P_SpawnMobj (actor->x, actor->y, actor->z + 5*FRACUNIT,
				  MT_XKORAX_SPIRIT);
	angle_t	a;
	if (!mo)
	    continue;
	a = (angle_t)((unsigned)i * 0x2AAAAAABu);	// 6 equal angles (360/6)
	mo->angle = a;
	a >>= ANGLETOFINESHIFT;
	mo->momx = FixedMul (mo->info->speed, finecosine[a]);
	mo->momy = FixedMul (mo->info->speed, finesine[a]);
	mo->momz = 3*FRACUNIT;
	mo->health = 35;		// crispy KORAX_SPIRIT_LIFETIME (~1s of roaming)
    }
}

// Spirit: harmless drifter that expires after its lifetime (crispy A_KSpiritRoam).
// The seeker weave toward Korax is simplified to the straight outward drift set up
// in A_KoraxBonePop; here we just count the lifetime down and burst.
void A_KSpiritRoam (mobj_t* actor)
{
    if (actor->health-- <= 0)
    {
	S_StartSound (actor, sfx_firxpl);
	P_SetMobjState (actor, S_XKSP_DIE1);
    }
    else if (P_Random () < 50)
	S_StartSound (NULL, sfx_dmact);
}

// Korax lightning bolt: count its lifetime down, then vanish (crispy A_KBolt uses
// special1; we reuse reactiontime, which P_SpawnMobj seeds from info->reactiontime).
void A_KBolt (mobj_t* actor)
{
    if (actor->reactiontime-- <= 0)
	P_SetMobjState (actor, S_NULL);
}

// Grow the lightning column upward toward the ceiling (crispy A_KBoltRaise).
void A_KBoltRaise (mobj_t* actor)
{
    fixed_t	z = actor->z + 48*FRACUNIT;
    if (z + 48*FRACUNIT < actor->ceilingz)
    {
	mobj_t*	mo = P_SpawnMobj (actor->x, actor->y, z, MT_XKORAX_BOLT);
	if (mo)
	    mo->reactiontime = 3;	// crispy KORAX_BOLT_LIFETIME
    }
}

// ---------------------------------------------------------------------------
// Heresiarch / Sorcerer boss (crispy MT_SORCBOSS + A_Sorc*).  MAJOR simplification:
// the three orbiting Sorcerer Balls (MT_SORCBALL1/2/3) and their whole state machine
// -- A_SorcBallOrbit / A_SorcSpinBalls / A_SpeedBalls / A_SorcBallPop and the
// ball-launched SorcFX split/bishop-spawn spells -- are DROPPED.  That mechanic is
// built entirely on mobj_t.special1/special2/args (host pointer, orbit angle, speed
// state), which this engine's mobj_t does not have.  Instead the boss casts spells
// directly: A_SorcBossAttack fires a fan of MT_XSORCFX4 bolts at the target.  Chase,
// pain, and the full real 19-frame death sequence are faithful.
// ---------------------------------------------------------------------------

// crispy A_SorcSpinBalls kicks off the orbit; with the balls dropped it's a no-op
// kept so the spawn state graph matches (Heresiarch still "wakes up" into its look).
void A_SorcSpinBalls (mobj_t* actor) { (void)actor; }

void A_SorcBossAttack (mobj_t* actor)
{
    int	i;
    if (!actor->target)
	return;
    S_StartSound (actor, sfx_firsht);
    // A short fan of the ball-explosion bolt (crispy's SorcFX4 payload).
    for (i = 0; i < 3; i++)
    {
	mobj_t*	mo = P_SpawnMissile (actor, actor->target, MT_XSORCFX4);
	angle_t	a;
	fixed_t	sp;
	if (!mo)
	    continue;
	mo->angle += (angle_t)((i - 1) * (ANG45 / 6));
	a  = mo->angle >> ANGLETOFINESHIFT;
	sp = mo->info->speed;
	mo->momx = FixedMul (sp, finecosine[a]);
	mo->momy = FixedMul (sp, finesine[a]);
    }
}

// ---------------------------------------------------------------------------
// Table fill (mirrors hexen.c's ST()).
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

// ---------------------------------------------------------------------------
// Minotaur / Dark Servant (crispy hexen/p_enemy.c A_Minotaur*).  Simplifications,
// all forced by this engine's mobj_t having no args[]/special1/special2:
//   * the summoned-servant life cycle is gone -- the fade-in ritual (A_MinotaurFade*,
//     S_MNTR_SPAWN*), the friendly A_MinotaurLook/Chase/Roam wander and the expiry
//     timer.  It spawns hostile, looks and chases like every other monster here.
//   * the CHARGE attack (S_MNTR_ATK4_1 / A_MinotaurCharge) needs a per-actor duration
//     counter (crispy args[4]); dropped, so A_MinotaurDecide is a two-way roll
//     between the floor-fire attack and the hammer swing.
//   * A_MinotaurAtk3's "swing again" repeat used special2; dropped.
// ---------------------------------------------------------------------------

// Hammer swing: pure melee, HITDICE(4) = 4..32.
void A_XMinotaurAtk1 (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, sfx_x_mnatk);
    if (P_CheckMeleeRange (actor))
	P_DamageMobj (actor->target, actor, actor, HITDICE (4));
}

// Floor fire: a mortar that skitters along the ground spawning flames.
void A_XMinotaurAtk3 (mobj_t* actor)
{
    mobj_t* mo;

    if (!actor->target)
	return;
    if (P_CheckMeleeRange (actor))
    {
	P_DamageMobj (actor->target, actor, actor, HITDICE (3));
	if (actor->target->player)		// squish the view (crispy)
	    actor->target->player->deltaviewheight = -16*FRACUNIT;
	return;
    }
    mo = P_SpawnMissile (actor, actor->target, MT_XMNTFX2);
    if (mo)
	S_StartSound (mo, sfx_x_mnhit);
}

// Pick between the floor fire and the hammer (crispy A_MinotaurDecide, minus the
// charge branch): close to a grounded target -> floor fire, else fall through to
// the swing the current state chain already leads into.
void A_XMinotaurDecide (mobj_t* actor)
{
    mobj_t*	target = actor->target;
    fixed_t	dist;

    if (!target)
	return;
    dist = P_AproxDistance (actor->x - target->x, actor->y - target->y);
    if (target->z == target->floorz && dist < 9*64*FRACUNIT && P_Random () < 100)
	P_SetMobjState (actor, S_XMNT_ATK3_1);
    else
	A_FaceTarget (actor);
}

// Mace-ball volley: melee if it can reach, else a five-wide fan of MT_XMNTFX1.
void A_XMinotaurAtk2 (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, sfx_x_mnatk);
    if (P_CheckMeleeRange (actor))
    {
	P_DamageMobj (actor->target, actor, actor, HITDICE (3));
	return;
    }
    Korax_FireFan (actor, MT_XMNTFX1, 5);
}

// The floor-fire mortar lays a flame on the ground under itself each tic
// (crispy A_MntrFloorFire).
void A_XMntrFloorFire (mobj_t* actor)
{
    mobj_t*	mo;
    int		r1 = (P_Random () - P_Random ());
    int		r2 = (P_Random () - P_Random ());

    actor->z = actor->floorz;
    mo = P_SpawnMobj (actor->x + (r2 << 10), actor->y + (r1 << 10), ONFLOORZ,
		      MT_XMNTFX3);
    if (!mo)
	return;
    mo->target = actor->target;
    mo->momx = 1;			// force block checking (crispy)
    P_CheckMissileSpawn (mo);
}

// Puff of smoke partway through the death (crispy A_SmokePuffExit).
void A_XMinotaurSmoke (mobj_t* actor)
{
    P_SpawnMobj (actor->x, actor->y, actor->z, MT_XMNTSMOKE);
}

// Korax's bat: flap along, drifting up and down, and expire on its own.
void A_BatMove (mobj_t* actor)
{
    angle_t	a;

    if (actor->reactiontime && --actor->reactiontime == 0)
    {
	P_SetMobjState (actor, S_NULL);
	return;
    }
    actor->angle += (P_Random () < 128) ? (ANG45/8) : -(ANG45/8);
    a = actor->angle >> ANGLETOFINESHIFT;
    actor->momx = FixedMul (actor->info->speed, finecosine[a]);
    actor->momy = FixedMul (actor->info->speed, finesine[a]);
    actor->momz = (P_Random () < 128) ? FRACUNIT/2 : -FRACUNIT/2;
}

// Pig: a weak bite (crispy A_PigAttack, minus the morph-timer poll which
// files/p_morph.c owns here).
void A_PigAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_CheckMeleeRange (actor))
    {
	P_DamageMobj (actor->target, actor, actor, 2 + (P_Random () & 1));
	S_StartSound (actor, sfx_x_pgatk);
    }
}

// Pig pain: squeal and hop (crispy A_PigPain).
void A_PigPain (mobj_t* actor)
{
    A_Pain (actor);
    if (actor->z <= actor->floorz)
	actor->momz = 7*FRACUNIT/2;
}

// ---------------------------------------------------------------------------
// Fighter / Cleric / Mage class bosses (crispy MT_*_BOSS).  Each is a player-model
// duelist that fires its class's 4th weapon.  Simplifications:
//   * A_FastChase (the strafe-and-shoot chase) needs special2 as a strafe timer;
//     plain A_Chase is used, so they close in rather than circle-strafe.
//   * A_ClassBossHealth (x5 HP in co-op) needs special1; dropped.
//   * the attacks are the real thing minus the weapon-specific plumbing: crispy's
//     Quietus/Bloodscourge fans are reproduced with the same spreads, and the
//     Wraithverge's spirit-splitting seeker is flattened to a single seeker.
//   * xdeath (A_SkullPop) and the ice death are dropped, like every other Hexen
//     monster in this port.
// ---------------------------------------------------------------------------

// Quietus: a five-wide bolt fan (crispy A_FSwordAttack2).
void A_XFighterAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, sfx_x_fbatk);
    Korax_FireFan (actor, MT_XFSWORDFX, 5);
}

// Wraithverge: one holy seeker (crispy A_CHolyAttack3 spawns MT_HOLY_MISSILE,
// which immediately splits into three tracking spirits -- flattened to one here).
void A_XClericAttack (mobj_t* actor)
{
    mobj_t* mo;

    if (!actor->target)
	return;
    S_StartSound (actor, sfx_x_cbatk);
    mo = P_SpawnMissile (actor, actor->target, MT_XHOLYFX);
    if (mo)
	mo->tracer = actor->target;		// A_Tracer needs the homing target
}

// Bloodscourge: three seekers, straight and +/-5 degrees (crispy A_MStaffAttack2).
void A_XMageAttack (mobj_t* actor)
{
    int	i;

    if (!actor->target)
	return;
    S_StartSound (actor, sfx_x_mbatk);
    for (i = 0; i < 3; i++)
    {
	mobj_t*	mo = P_SpawnMissile (actor, actor->target, MT_XMSTAFFFX);
	angle_t	a;
	if (!mo)
	    continue;
	mo->tracer = actor->target;
	mo->angle += (angle_t)((i - 1) * (ANG45 / 9));		// ~5 deg
	a = mo->angle >> ANGLETOFINESHIFT;
	mo->momx = FixedMul (mo->info->speed, finecosine[a]);
	mo->momy = FixedMul (mo->info->speed, finesine[a]);
    }
}

void Hexen_Mon_Init (void)
{
    mobjinfo_t*	m;

    // ===================================================================
    // Chaos Serpent, 2nd variant (crispy MT_DEMON2, ednum 8080).  Same melee +
    // fire-breath behaviour as MT_XDEMON, distinct sprite (DEM2 / D2FX) so it can
    // stand beside the first serpent.  Gib XDeath + ice-chunk actors simplified to
    // the plain death, like the first Chaos Serpent in hexen.c.
    // ===================================================================
    ST (S_XDE2_LOOK1,  SPR_XDEM2,  0, 10, (actionf_p1)A_Look,          S_XDE2_LOOK2);
    ST (S_XDE2_LOOK2,  SPR_XDEM2,  0, 10, (actionf_p1)A_Look,          S_XDE2_LOOK1);
    ST (S_XDE2_CHASE1, SPR_XDEM2,  0,  4, (actionf_p1)A_Chase,         S_XDE2_CHASE2);
    ST (S_XDE2_CHASE2, SPR_XDEM2,  1,  4, (actionf_p1)A_Chase,         S_XDE2_CHASE3);
    ST (S_XDE2_CHASE3, SPR_XDEM2,  2,  4, (actionf_p1)A_Chase,         S_XDE2_CHASE4);
    ST (S_XDE2_CHASE4, SPR_XDEM2,  3,  4, (actionf_p1)A_Chase,         S_XDE2_CHASE1);
    ST (S_XDE2_ATK1_1, SPR_XDEM2,  4,  6, (actionf_p1)A_FaceTarget,    S_XDE2_ATK1_2);
    ST (S_XDE2_ATK1_2, SPR_XDEM2,  5,  8, (actionf_p1)A_FaceTarget,    S_XDE2_ATK1_3);
    ST (S_XDE2_ATK1_3, SPR_XDEM2,  6,  6, (actionf_p1)A_DemonAttack1,  S_XDE2_CHASE1);
    ST (S_XDE2_ATK2_1, SPR_XDEM2,  4,  5, (actionf_p1)A_FaceTarget,    S_XDE2_ATK2_2);
    ST (S_XDE2_ATK2_2, SPR_XDEM2,  5,  6, (actionf_p1)A_FaceTarget,    S_XDE2_ATK2_3);
    ST (S_XDE2_ATK2_3, SPR_XDEM2,  6,  5, (actionf_p1)A_Demon2Attack2, S_XDE2_CHASE1);
    ST (S_XDE2_PAIN1,  SPR_XDEM2,  4,  4, NULL,                        S_XDE2_PAIN2);
    ST (S_XDE2_PAIN2,  SPR_XDEM2,  4,  4, (actionf_p1)A_Pain,          S_XDE2_CHASE1);
    ST (S_XDE2_DIE1,   SPR_XDEM2,  7,  6, NULL,                        S_XDE2_DIE2);
    ST (S_XDE2_DIE2,   SPR_XDEM2,  8,  6, NULL,                        S_XDE2_DIE3);
    ST (S_XDE2_DIE3,   SPR_XDEM2,  9,  6, (actionf_p1)A_Scream,        S_XDE2_DIE4);
    ST (S_XDE2_DIE4,   SPR_XDEM2, 10,  6, (actionf_p1)A_Fall,          S_XDE2_DIE5);
    ST (S_XDE2_DIE5,   SPR_XDEM2, 11,  6, NULL,                        S_XDE2_DIE6);
    ST (S_XDE2_DIE6,   SPR_XDEM2, 12,  6, NULL,                        S_XDE2_DIE7);
    ST (S_XDE2_DIE7,   SPR_XDEM2, 13,  6, NULL,                        S_XDE2_DIE8);
    ST (S_XDE2_DIE8,   SPR_XDEM2, 14,  6, NULL,                        S_XDE2_DIE9);
    ST (S_XDE2_DIE9,   SPR_XDEM2, 15, -1, NULL,                        S_NULL);

    // Chaos Serpent 2 fireball (crispy S_DEMON2FX_*).
    ST (S_XD2F_MOVE1, SPR_XD2FX, 32768, 4, NULL, S_XD2F_MOVE2);
    ST (S_XD2F_MOVE2, SPR_XD2FX, 32769, 4, NULL, S_XD2F_MOVE3);
    ST (S_XD2F_MOVE3, SPR_XD2FX, 32770, 4, NULL, S_XD2F_MOVE4);
    ST (S_XD2F_MOVE4, SPR_XD2FX, 32771, 4, NULL, S_XD2F_MOVE5);
    ST (S_XD2F_MOVE5, SPR_XD2FX, 32772, 4, NULL, S_XD2F_MOVE6);
    ST (S_XD2F_MOVE6, SPR_XD2FX, 32773, 4, NULL, S_XD2F_MOVE1);
    ST (S_XD2F_BOOM1, SPR_XD2FX, 32774, 4, NULL, S_XD2F_BOOM2);
    ST (S_XD2F_BOOM2, SPR_XD2FX, 32775, 4, NULL, S_XD2F_BOOM3);
    ST (S_XD2F_BOOM3, SPR_XD2FX, 32776, 4, NULL, S_XD2F_BOOM4);
    ST (S_XD2F_BOOM4, SPR_XD2FX, 32777, 4, NULL, S_XD2F_BOOM5);
    ST (S_XD2F_BOOM5, SPR_XD2FX, 32778, 3, NULL, S_XD2F_BOOM6);
    ST (S_XD2F_BOOM6, SPR_XD2FX, 32779, 3, NULL, S_NULL);

    m = &mobjinfo[MT_XDEMON2];
    m->doomednum = -1;        m->spawnstate  = S_XDE2_LOOK1; m->spawnhealth = 250;	// crispy ednum 8080
    m->seestate  = S_XDE2_CHASE1; m->seesound = sfx_x_desit; m->reactiontime = 8;
    m->attacksound = sfx_x_deatk;m->painstate = S_XDE2_PAIN1; m->painchance = 50;
    m->painsound = sfx_x_depai;  m->meleestate = S_XDE2_ATK1_1; m->missilestate = S_XDE2_ATK2_1;
    m->deathstate = S_XDE2_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_x_dedth;
    m->speed = 13; m->radius = 32*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 220;
    m->damage = 0; m->activesound = sfx_x_desit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XDEMON2_FX];
    m->doomednum = -1;        m->spawnstate  = S_XD2F_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XD2F_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_firxpl;
    m->speed = 15*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 6*FRACUNIT; m->mass = 100;
    m->damage = 5; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    // ===================================================================
    // Buried Reiver / Wraith (crispy MT_WRAITHB, ednum 10011).  Reuses the whole
    // MT_XWRAITH state graph from hexen.c (chase/melee/missile/pain/death); adds
    // only a short RAISE sequence played from its seestate when it first spots you.
    // ===================================================================
    ST (S_XWRTB_RISE1, SPR_XWRT, 0, 4, (actionf_p1)A_WraithBRaiseInit, S_XWRTB_RISE2);
    ST (S_XWRTB_RISE2, SPR_XWRT, 0, 4, (actionf_p1)A_FaceTarget,       S_XWRTB_RISE3);
    ST (S_XWRTB_RISE3, SPR_XWRT, 1, 4, NULL,                           S_XWRT_CHASE1);

    m = &mobjinfo[MT_XWRAITHB];
    m->doomednum = -1;        m->spawnstate  = S_XWRT_LOOK1; m->spawnhealth = 150;	// crispy ednum 10011
    m->seestate  = S_XWRTB_RISE1; m->seesound  = sfx_x_wrsit; m->reactiontime = 8;
    m->attacksound = sfx_x_wratk; m->painstate = S_XWRT_PAIN1; m->painchance = 25;
    m->painsound = sfx_x_wrpai;   m->meleestate = S_XWRT_ATK1_1; m->missilestate = S_XWRT_ATK2_1;
    m->deathstate = S_XWRT_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_wrdth;
    m->speed = 11; m->radius = 20*FRACUNIT; m->height = 68*FRACUNIT; m->mass = 75;
    m->damage = 0; m->activesound = sfx_x_wract;
    // Spawns intangible + invisible; A_WraithBRaiseInit adds MF_SOLID|MF_SHOOTABLE.
    m->flags = MF_DROPOFF|MF_NOGRAVITY|MF_FLOAT|MF_COUNTKILL;
    m->flags2 = MF2_DONTDRAW; m->raisestate = S_NULL;

    // ===================================================================
    // Korax -- final boss (crispy MT_KORAX, ednum 10200).
    // ===================================================================
    ST (S_XKRX_LOOK1,  SPR_XKORX, 0, 5, (actionf_p1)A_Look, S_XKRX_LOOK1);
    // 16-step chase: frames A,A,A,A,B,B,B,B,C,C,C,C,D,D,D,D with periodic step stomps.
    ST (S_XKRX_CHASE1,  SPR_XKORX, 0, 3, (actionf_p1)A_KoraxStep2, S_XKRX_CHASE2);
    ST (S_XKRX_CHASE2,  SPR_XKORX, 0, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE3);
    ST (S_XKRX_CHASE3,  SPR_XKORX, 0, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE4);
    ST (S_XKRX_CHASE4,  SPR_XKORX, 0, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE5);
    ST (S_XKRX_CHASE5,  SPR_XKORX, 1, 3, (actionf_p1)A_KoraxStep,  S_XKRX_CHASE6);
    ST (S_XKRX_CHASE6,  SPR_XKORX, 1, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE7);
    ST (S_XKRX_CHASE7,  SPR_XKORX, 1, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE8);
    ST (S_XKRX_CHASE8,  SPR_XKORX, 1, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE9);
    ST (S_XKRX_CHASE9,  SPR_XKORX, 2, 3, (actionf_p1)A_KoraxStep2, S_XKRX_CHASE10);
    ST (S_XKRX_CHASE10, SPR_XKORX, 2, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE11);
    ST (S_XKRX_CHASE11, SPR_XKORX, 2, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE12);
    ST (S_XKRX_CHASE12, SPR_XKORX, 2, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE13);
    ST (S_XKRX_CHASE13, SPR_XKORX, 3, 3, (actionf_p1)A_KoraxStep,  S_XKRX_CHASE14);
    ST (S_XKRX_CHASE14, SPR_XKORX, 3, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE15);
    ST (S_XKRX_CHASE15, SPR_XKORX, 3, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE16);
    ST (S_XKRX_CHASE16, SPR_XKORX, 3, 3, (actionf_p1)A_KoraxChase, S_XKRX_CHASE1);
    ST (S_XKRX_PAIN1,  SPR_XKORX, 7, 5, (actionf_p1)A_Pain,       S_XKRX_PAIN2);
    ST (S_XKRX_PAIN2,  SPR_XKORX, 7, 5, NULL,                     S_XKRX_CHASE1);
    // Attack entry: face, then decide (missile volley vs lightning command).
    ST (S_XKRX_ATK1,   SPR_XKORX, 32772, 2, (actionf_p1)A_FaceTarget,  S_XKRX_ATK2);
    ST (S_XKRX_ATK2,   SPR_XKORX, 32772, 5, (actionf_p1)A_KoraxDecide, S_XKRX_ATK2);
    ST (S_XKRX_MIS1,   SPR_XKORX, 32772, 4, (actionf_p1)A_FaceTarget,   S_XKRX_MIS2);
    ST (S_XKRX_MIS2,   SPR_XKORX, 32773, 8, (actionf_p1)A_KoraxMissile, S_XKRX_MIS3);
    ST (S_XKRX_MIS3,   SPR_XKORX, 32772, 8, NULL,                       S_XKRX_CHASE1);
    ST (S_XKRX_CMD1,   SPR_XKORX, 32772,  5, (actionf_p1)A_FaceTarget,  S_XKRX_CMD2);
    ST (S_XKRX_CMD2,   SPR_XKORX, 32790, 10, (actionf_p1)A_FaceTarget,  S_XKRX_CMD3);
    ST (S_XKRX_CMD3,   SPR_XKORX, 32774, 15, (actionf_p1)A_KoraxCommand,S_XKRX_CMD4);
    ST (S_XKRX_CMD4,   SPR_XKORX, 32790, 10, NULL,                      S_XKRX_CMD5);
    ST (S_XKRX_CMD5,   SPR_XKORX, 32772,  5, NULL,                      S_XKRX_CHASE1);
    ST (S_XKRX_DIE1,   SPR_XKORX,  8,  5, NULL,                       S_XKRX_DIE2);
    ST (S_XKRX_DIE2,   SPR_XKORX,  9,  5, (actionf_p1)A_FaceTarget,   S_XKRX_DIE3);
    ST (S_XKRX_DIE3,   SPR_XKORX, 10,  5, (actionf_p1)A_Scream,       S_XKRX_DIE4);
    ST (S_XKRX_DIE4,   SPR_XKORX, 11,  5, NULL,                       S_XKRX_DIE5);
    ST (S_XKRX_DIE5,   SPR_XKORX, 12,  5, NULL,                       S_XKRX_DIE6);
    ST (S_XKRX_DIE6,   SPR_XKORX, 13,  5, NULL,                       S_XKRX_DIE7);
    ST (S_XKRX_DIE7,   SPR_XKORX, 14,  5, NULL,                       S_XKRX_DIE8);
    ST (S_XKRX_DIE8,   SPR_XKORX, 15,  5, NULL,                       S_XKRX_DIE9);
    ST (S_XKRX_DIE9,   SPR_XKORX, 16, 10, NULL,                       S_XKRX_DIE10);
    ST (S_XKRX_DIE10,  SPR_XKORX, 17,  5, (actionf_p1)A_KoraxBonePop, S_XKRX_DIE11);
    ST (S_XKRX_DIE11,  SPR_XKORX, 18,  5, (actionf_p1)A_Fall,         S_XKRX_DIE12);
    ST (S_XKRX_DIE12,  SPR_XKORX, 19,  5, NULL,                       S_XKRX_DIE13);
    ST (S_XKRX_DIE13,  SPR_XKORX, 20,  5, NULL,                       S_XKRX_DIE14);
    ST (S_XKRX_DIE14,  SPR_XKORX, 21, -1, NULL,                       S_NULL);

    // Korax lightning bolt (crispy S_KBOLT*, MLFX frames I..M = 8..12, fullbright).
    ST (S_XKBOLT1, SPR_XMLFX, 32776, 2, NULL,                      S_XKBOLT2);
    ST (S_XKBOLT2, SPR_XMLFX, 32777, 2, (actionf_p1)A_KBoltRaise,  S_XKBOLT3);
    ST (S_XKBOLT3, SPR_XMLFX, 32776, 2, (actionf_p1)A_KBolt,       S_XKBOLT4);
    ST (S_XKBOLT4, SPR_XMLFX, 32777, 2, (actionf_p1)A_KBolt,       S_XKBOLT5);
    ST (S_XKBOLT5, SPR_XMLFX, 32778, 2, (actionf_p1)A_KBolt,       S_XKBOLT6);
    ST (S_XKBOLT6, SPR_XMLFX, 32779, 2, (actionf_p1)A_KBolt,       S_XKBOLT7);
    ST (S_XKBOLT7, SPR_XMLFX, 32780, 2, (actionf_p1)A_KBolt,       S_XKBOLT3);

    // Korax spirit (crispy S_KSPIRIT_*, SPIR sprite).
    ST (S_XKSP_ROAM1, SPR_XSPIR, 0, 5, (actionf_p1)A_KSpiritRoam, S_XKSP_ROAM2);
    ST (S_XKSP_ROAM2, SPR_XSPIR, 1, 5, (actionf_p1)A_KSpiritRoam, S_XKSP_ROAM1);
    ST (S_XKSP_DIE1,  SPR_XSPIR, 3, 5, NULL, S_XKSP_DIE2);
    ST (S_XKSP_DIE2,  SPR_XSPIR, 4, 5, NULL, S_XKSP_DIE3);
    ST (S_XKSP_DIE3,  SPR_XSPIR, 5, 5, NULL, S_XKSP_DIE4);
    ST (S_XKSP_DIE4,  SPR_XSPIR, 6, 5, NULL, S_XKSP_DIE5);
    ST (S_XKSP_DIE5,  SPR_XSPIR, 7, 5, NULL, S_XKSP_DIE6);
    ST (S_XKSP_DIE6,  SPR_XSPIR, 8, 5, NULL, S_NULL);

    m = &mobjinfo[MT_XKORAX];
    m->doomednum = -1;        m->spawnstate  = S_XKRX_LOOK1; m->spawnhealth = 5000;	// crispy ednum 10200
    m->seestate  = S_XKRX_CHASE1; m->seesound = sfx_bossit;  m->reactiontime = 8;
    m->attacksound = sfx_firsht;  m->painstate = S_XKRX_PAIN1; m->painchance = 20;
    m->painsound = sfx_bospn;     m->meleestate = S_NULL;      m->missilestate = S_XKRX_ATK1;
    m->deathstate = S_XKRX_DIE1;  m->xdeathstate = S_NULL;     m->deathsound = sfx_bosdth;
    m->speed = 10; m->radius = 65*FRACUNIT; m->height = 115*FRACUNIT; m->mass = 2000;
    m->damage = 15; m->activesound = sfx_dmact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XKORAX_BOLT];
    m->doomednum = -1;        m->spawnstate  = S_XKBOLT1;  m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;	// bolt lifetime (A_KBolt)
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_NULL;      m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 0; m->radius = 15*FRACUNIT; m->height = 35*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;	// decorative lightning column (crispy damage 0)
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY|MF_MISSILE|MF_DROPOFF|MF_SHADOW; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XKORAX_SPIRIT];
    m->doomednum = -1;        m->spawnstate  = S_XKSP_ROAM1; m->spawnhealth = 35;	// lifetime seed
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XKSP_DIE1; m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 8*FRACUNIT; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;	// harmless roamer
    m->flags = MF_NOBLOCKMAP|MF_DROPOFF|MF_NOGRAVITY|MF_MISSILE|MF_NOCLIP|MF_SHADOW; m->flags2 = 0; m->raisestate = S_NULL;

    // ===================================================================
    // Heresiarch / Sorcerer boss (crispy MT_SORCBOSS, ednum 10080).
    // ===================================================================
    ST (S_XSOR_SPAWN1, SPR_XSORC, 0, 3, NULL,                        S_XSOR_SPAWN2);
    ST (S_XSOR_SPAWN2, SPR_XSORC, 0, 2, (actionf_p1)A_SorcSpinBalls, S_XSOR_LOOK1);
    ST (S_XSOR_LOOK1,  SPR_XSORC, 0, 10,(actionf_p1)A_Look,          S_XSOR_LOOK1);
    ST (S_XSOR_WALK1,  SPR_XSORC, 0, 5, (actionf_p1)A_Chase,         S_XSOR_WALK2);
    ST (S_XSOR_WALK2,  SPR_XSORC, 1, 5, (actionf_p1)A_Chase,         S_XSOR_WALK3);
    ST (S_XSOR_WALK3,  SPR_XSORC, 2, 5, (actionf_p1)A_Chase,         S_XSOR_WALK4);
    ST (S_XSOR_WALK4,  SPR_XSORC, 3, 5, (actionf_p1)A_Chase,         S_XSOR_WALK1);
    ST (S_XSOR_PAIN1,  SPR_XSORC, 6, 8, NULL,                        S_XSOR_PAIN2);
    ST (S_XSOR_PAIN2,  SPR_XSORC, 6, 8, (actionf_p1)A_Pain,          S_XSOR_WALK1);
    // Simplified spell-cast (crispy: A_SpeedBalls then A_SorcBossAttack launches the balls).
    ST (S_XSOR_ATK1,   SPR_XSORC, 32773, 6, (actionf_p1)A_FaceTarget,    S_XSOR_ATK2);
    ST (S_XSOR_ATK2,   SPR_XSORC, 32772, 6, (actionf_p1)A_SorcBossAttack,S_XSOR_ATK3);
    ST (S_XSOR_ATK3,   SPR_XSORC, 32773, 6, (actionf_p1)A_FaceTarget,    S_XSOR_WALK1);
    // Full 19-frame death (crispy S_SORC_DIE1..DIEI, all fullbright, frames 7..25).
    ST (S_XSOR_DIE1,  SPR_XSORC, 32775, 5, NULL,                     S_XSOR_DIE2);
    ST (S_XSOR_DIE2,  SPR_XSORC, 32776, 5, (actionf_p1)A_FaceTarget, S_XSOR_DIE3);
    ST (S_XSOR_DIE3,  SPR_XSORC, 32777, 5, (actionf_p1)A_Scream,     S_XSOR_DIE4);
    ST (S_XSOR_DIE4,  SPR_XSORC, 32778, 5, NULL,                     S_XSOR_DIE5);
    ST (S_XSOR_DIE5,  SPR_XSORC, 32779, 5, NULL,                     S_XSOR_DIE6);
    ST (S_XSOR_DIE6,  SPR_XSORC, 32780, 5, NULL,                     S_XSOR_DIE7);
    ST (S_XSOR_DIE7,  SPR_XSORC, 32781, 5, NULL,                     S_XSOR_DIE8);
    ST (S_XSOR_DIE8,  SPR_XSORC, 32782, 5, NULL,                     S_XSOR_DIE9);
    ST (S_XSOR_DIE9,  SPR_XSORC, 32783, 5, NULL,                     S_XSOR_DIE10);
    ST (S_XSOR_DIE10, SPR_XSORC, 32784, 5, NULL,                     S_XSOR_DIE11);
    ST (S_XSOR_DIE11, SPR_XSORC, 32785, 5, NULL,                     S_XSOR_DIE12);
    ST (S_XSOR_DIE12, SPR_XSORC, 32786, 5, NULL,                     S_XSOR_DIE13);
    ST (S_XSOR_DIE13, SPR_XSORC, 32787, 5, NULL,                     S_XSOR_DIE14);
    ST (S_XSOR_DIE14, SPR_XSORC, 32788, 5, (actionf_p1)A_Fall,       S_XSOR_DIE15);
    ST (S_XSOR_DIE15, SPR_XSORC, 32789, 5, NULL,                     S_XSOR_DIE16);
    ST (S_XSOR_DIE16, SPR_XSORC, 32790, 5, NULL,                     S_XSOR_DIE17);
    ST (S_XSOR_DIE17, SPR_XSORC, 32791, 5, NULL,                     S_XSOR_DIE18);
    ST (S_XSOR_DIE18, SPR_XSORC, 32792, 5, NULL,                     S_XSOR_DIE19);
    ST (S_XSOR_DIE19, SPR_XSORC, 32793, -1, NULL,                    S_NULL);

    // SorcFX1 -- yellow homing spell (crispy S_SORCFX1_*, SBS1 A..D fullbright).
    // Uses the engine A_Tracer for the seek (tracer set to target at spawn); the
    // FHFX explosion is simplified to a quick on-sprite fizzle.
    ST (S_XSF1_1,  SPR_XSBS1, 32768, 2, NULL,               S_XSF1_2);
    ST (S_XSF1_2,  SPR_XSBS1, 32769, 3, (actionf_p1)A_Tracer, S_XSF1_3);
    ST (S_XSF1_3,  SPR_XSBS1, 32770, 3, (actionf_p1)A_Tracer, S_XSF1_4);
    ST (S_XSF1_4,  SPR_XSBS1, 32771, 3, (actionf_p1)A_Tracer, S_XSF1_1);
    ST (S_XSF1_D1, SPR_XSBS1, 32771, 4, NULL,               S_XSF1_D2);
    ST (S_XSF1_D2, SPR_XSBS1, 32770, 4, NULL,               S_XSF1_D3);
    ST (S_XSF1_D3, SPR_XSBS1, 32769, 4, NULL,               S_NULL);

    // SorcFX2 -- blue defensive spell (crispy S_SORCFX2_ORBIT*, SBS2).  The orbit +
    // split is dropped; it flies straight and fizzles (SORCFX2T1 frame).
    ST (S_XSF2_1,  SPR_XSBS2, 32768, 2, NULL, S_XSF2_2);
    ST (S_XSF2_2,  SPR_XSBS2, 32769, 2, NULL, S_XSF2_3);
    ST (S_XSF2_3,  SPR_XSBS2, 32770, 2, NULL, S_XSF2_4);
    ST (S_XSF2_4,  SPR_XSBS2, 32771, 2, NULL, S_XSF2_1);
    ST (S_XSF2_D1, SPR_XSBS2, 0, 8, NULL, S_NULL);

    // SorcFX3 -- green bishop-spawn spell (crispy S_SORCFX3_*, SBS3).  The bishop
    // morph is dropped; it flies straight (frames A..C) and bursts (EXP frames D..H).
    ST (S_XSF3_1,   SPR_XSBS3, 32768, 2, NULL, S_XSF3_2);
    ST (S_XSF3_2,   SPR_XSBS3, 32769, 2, NULL, S_XSF3_3);
    ST (S_XSF3_3,   SPR_XSBS3, 32770, 2, NULL, S_XSF3_1);
    ST (S_XSF3_EXP1, SPR_XSBS3, 3, 3, NULL, S_XSF3_EXP2);
    ST (S_XSF3_EXP2, SPR_XSBS3, 4, 3, NULL, S_XSF3_EXP3);
    ST (S_XSF3_EXP3, SPR_XSBS3, 5, 3, NULL, S_XSF3_EXP4);
    ST (S_XSF3_EXP4, SPR_XSBS3, 6, 3, NULL, S_XSF3_EXP5);
    ST (S_XSF3_EXP5, SPR_XSBS3, 7, 3, NULL, S_NULL);

    // SorcFX4 -- the ball-explosion bolt the boss actually casts (crispy S_SORCFX4_*,
    // SBS4).  Straight flight (A..C fullbright), self-contained blast (D1..D5).  The
    // crispy A_Explode radius hit is simplified to plain missile-contact damage.
    ST (S_XSF4_1,  SPR_XSBS4, 32768, 2, NULL, S_XSF4_2);
    ST (S_XSF4_2,  SPR_XSBS4, 32769, 2, NULL, S_XSF4_3);
    ST (S_XSF4_3,  SPR_XSBS4, 32770, 2, NULL, S_XSF4_1);
    ST (S_XSF4_D1, SPR_XSBS4, 32771, 2, NULL, S_XSF4_D2);
    ST (S_XSF4_D2, SPR_XSBS4, 32772, 2, NULL, S_XSF4_D3);
    ST (S_XSF4_D3, SPR_XSBS4, 32773, 2, NULL, S_XSF4_D4);
    ST (S_XSF4_D4, SPR_XSBS4, 32774, 2, NULL, S_XSF4_D5);
    ST (S_XSF4_D5, SPR_XSBS4, 32775, 2, NULL, S_NULL);

    m = &mobjinfo[MT_XHERESIARCH];
    m->doomednum = -1;        m->spawnstate  = S_XSOR_SPAWN1; m->spawnhealth = 5000;	// crispy ednum 10080
    m->seestate  = S_XSOR_WALK1; m->seesound = sfx_bossit;   m->reactiontime = 8;
    m->attacksound = sfx_firsht; m->painstate = S_XSOR_PAIN1; m->painchance = 10;
    m->painsound = sfx_bospn;    m->meleestate = S_NULL;      m->missilestate = S_XSOR_ATK1;
    m->deathstate = S_XSOR_DIE1; m->xdeathstate = S_NULL;     m->deathsound = sfx_bosdth;
    m->speed = 16; m->radius = 40*FRACUNIT; m->height = 110*FRACUNIT; m->mass = 500;
    m->damage = 9; m->activesound = sfx_dmact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_NOBLOOD; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XSORCFX1];
    m->doomednum = -1;        m->spawnstate  = S_XSF1_1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XSF1_D1;   m->xdeathstate = S_XSF1_D1; m->deathsound = sfx_firxpl;
    m->speed = 7*FRACUNIT; m->radius = 5*FRACUNIT; m->height = 5*FRACUNIT; m->mass = 100;
    m->damage = 8; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_SHADOW; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XSORCFX2];
    m->doomednum = -1;        m->spawnstate  = S_XSF2_1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XSF2_D1;   m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 15*FRACUNIT; m->radius = 5*FRACUNIT; m->height = 5*FRACUNIT; m->mass = 100;
    m->damage = 6; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_NOGRAVITY|MF_DROPOFF; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XSORCFX3];
    m->doomednum = -1;        m->spawnstate  = S_XSF3_1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XSF3_EXP1; m->xdeathstate = S_NULL;  m->deathsound = sfx_firxpl;
    m->speed = 15*FRACUNIT; m->radius = 22*FRACUNIT; m->height = 65*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_SHADOW; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XSORCFX4];
    m->doomednum = -1;        m->spawnstate  = S_XSF4_1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XSF4_D1;   m->xdeathstate = S_NULL;  m->deathsound = sfx_firxpl;
    m->speed = 12*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 10*FRACUNIT; m->mass = 100;
    m->damage = 10; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_NOGRAVITY|MF_DROPOFF|MF_SHADOW; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Minotaur / Dark Servant (crispy S_MNTR_*).  Sprite XMNT: A-D walk,
    //      E pain/death, F charge, G-H swing wind-up, I hammer, J-K mace throw. ----
    ST (S_XMNT_LOOK1,   SPR_XMNT,  0, 10, (actionf_p1)A_Look,          S_XMNT_LOOK2);
    ST (S_XMNT_LOOK2,   SPR_XMNT,  1, 10, (actionf_p1)A_Look,          S_XMNT_LOOK1);
    ST (S_XMNT_WALK1,   SPR_XMNT,  0,  5, (actionf_p1)A_Chase,         S_XMNT_WALK2);
    ST (S_XMNT_WALK2,   SPR_XMNT,  1,  5, (actionf_p1)A_Chase,         S_XMNT_WALK3);
    ST (S_XMNT_WALK3,   SPR_XMNT,  2,  5, (actionf_p1)A_Chase,         S_XMNT_WALK4);
    ST (S_XMNT_WALK4,   SPR_XMNT,  3,  5, (actionf_p1)A_Chase,         S_XMNT_WALK1);
    ST (S_XMNT_ATK1_1,  SPR_XMNT,  6, 10, (actionf_p1)A_FaceTarget,    S_XMNT_ATK1_2);
    ST (S_XMNT_ATK1_2,  SPR_XMNT,  7,  7, (actionf_p1)A_FaceTarget,    S_XMNT_ATK1_3);
    ST (S_XMNT_ATK1_3,  SPR_XMNT,  8, 12, (actionf_p1)A_XMinotaurAtk1,  S_XMNT_WALK1);
    ST (S_XMNT_ATK2_1,  SPR_XMNT,  6, 10, (actionf_p1)A_XMinotaurDecide,S_XMNT_ATK2_2);
    ST (S_XMNT_ATK2_2,  SPR_XMNT,  9,  4, (actionf_p1)A_FaceTarget,    S_XMNT_ATK2_3);
    ST (S_XMNT_ATK2_3,  SPR_XMNT, 10,  9, (actionf_p1)A_XMinotaurAtk2,  S_XMNT_WALK1);
    ST (S_XMNT_ATK3_1,  SPR_XMNT,  6, 10, (actionf_p1)A_FaceTarget,    S_XMNT_ATK3_2);
    ST (S_XMNT_ATK3_2,  SPR_XMNT,  7,  7, (actionf_p1)A_FaceTarget,    S_XMNT_ATK3_3);
    ST (S_XMNT_ATK3_3,  SPR_XMNT,  8, 12, (actionf_p1)A_XMinotaurAtk3,  S_XMNT_WALK1);
    ST (S_XMNT_PAIN1,   SPR_XMNT,  4,  3, NULL,                        S_XMNT_PAIN2);
    ST (S_XMNT_PAIN2,   SPR_XMNT,  4,  6, (actionf_p1)A_Pain,          S_XMNT_WALK1);
    ST (S_XMNT_DIE1,    SPR_XMNT,  4,  6, NULL,                        S_XMNT_DIE2);
    ST (S_XMNT_DIE2,    SPR_XMNT,  4,  2, (actionf_p1)A_Scream,        S_XMNT_DIE3);
    ST (S_XMNT_DIE3,    SPR_XMNT,  4,  5, (actionf_p1)A_XMinotaurSmoke, S_XMNT_DIE4);
    ST (S_XMNT_DIE4,    SPR_XMNT,  4,  5, NULL,                        S_XMNT_DIE5);
    ST (S_XMNT_DIE5,    SPR_XMNT,  4,  5, (actionf_p1)A_Fall,          S_XMNT_DIE6);
    ST (S_XMNT_DIE6,    SPR_XMNT,  4,  5, NULL,                        S_XMNT_DIE7);
    ST (S_XMNT_DIE7,    SPR_XMNT,  4,  5, NULL,                        S_XMNT_DIE8);
    ST (S_XMNT_DIE8,    SPR_XMNT,  4,  5, NULL,                        S_XMNT_DIE9);
    ST (S_XMNT_DIE9,    SPR_XMNT,  4, 10, NULL,                        S_NULL);

    // Mace ball (crispy S_MNTRFX1_*) -- fullbright.
    ST (S_XMF1_MOVE1, SPR_XFX1, 32768, 6, NULL, S_XMF1_MOVE2);
    ST (S_XMF1_MOVE2, SPR_XFX1, 32769, 6, NULL, S_XMF1_MOVE1);
    ST (S_XMF1_BOOM1, SPR_XFX1, 32770, 5, NULL, S_XMF1_BOOM2);
    ST (S_XMF1_BOOM2, SPR_XFX1, 32771, 5, NULL, S_XMF1_BOOM3);
    ST (S_XMF1_BOOM3, SPR_XFX1, 32772, 5, NULL, S_XMF1_BOOM4);
    ST (S_XMF1_BOOM4, SPR_XFX1, 32773, 5, NULL, S_XMF1_BOOM5);
    ST (S_XMF1_BOOM5, SPR_XFX1, 32774, 5, NULL, S_XMF1_BOOM6);
    ST (S_XMF1_BOOM6, SPR_XFX1, 32775, 5, NULL, S_NULL);

    // Floor-fire mortar (crispy S_MNTRFX2_*): loops on itself laying flames.
    ST (S_XMF2_MOVE1, SPR_XFX3,     0, 2, (actionf_p1)A_XMntrFloorFire, S_XMF2_MOVE1);
    ST (S_XMF2_BOOM1, SPR_XFX3, 32776, 4, NULL,                        S_XMF2_BOOM2);
    ST (S_XMF2_BOOM2, SPR_XFX3, 32777, 4, NULL,                        S_XMF2_BOOM3);
    ST (S_XMF2_BOOM3, SPR_XFX3, 32778, 4, NULL,                        S_XMF2_BOOM4);
    ST (S_XMF2_BOOM4, SPR_XFX3, 32779, 4, NULL,                        S_XMF2_BOOM5);
    ST (S_XMF2_BOOM5, SPR_XFX3, 32780, 4, NULL,                        S_NULL);

    // The flame the mortar leaves behind (crispy S_MNTRFX3_*).
    ST (S_XMF3_MOVE1, SPR_XFX3, 32771, 4, NULL, S_XMF3_MOVE2);
    ST (S_XMF3_MOVE2, SPR_XFX3, 32770, 4, NULL, S_XMF3_MOVE3);
    ST (S_XMF3_MOVE3, SPR_XFX3, 32769, 5, NULL, S_XMF3_MOVE4);
    ST (S_XMF3_MOVE4, SPR_XFX3, 32770, 5, NULL, S_XMF3_MOVE5);
    ST (S_XMF3_MOVE5, SPR_XFX3, 32771, 5, NULL, S_XMF3_MOVE6);
    ST (S_XMF3_MOVE6, SPR_XFX3, 32772, 5, NULL, S_XMF3_MOVE7);
    ST (S_XMF3_MOVE7, SPR_XFX3, 32773, 4, NULL, S_XMF3_MOVE8);
    ST (S_XMF3_MOVE8, SPR_XFX3, 32774, 4, NULL, S_XMF3_MOVE9);
    ST (S_XMF3_MOVE9, SPR_XFX3, 32775, 4, NULL, S_NULL);

    // Death smoke (crispy S_MINOSMOKEX*, trimmed to 9 of the 17 frames).
    ST (S_XMNS_1, SPR_XMNS, 0, 3, NULL, S_XMNS_2);
    ST (S_XMNS_2, SPR_XMNS, 1, 3, NULL, S_XMNS_3);
    ST (S_XMNS_3, SPR_XMNS, 2, 3, NULL, S_XMNS_4);
    ST (S_XMNS_4, SPR_XMNS, 3, 3, NULL, S_XMNS_5);
    ST (S_XMNS_5, SPR_XMNS, 4, 3, NULL, S_XMNS_6);
    ST (S_XMNS_6, SPR_XMNS, 5, 3, NULL, S_XMNS_7);
    ST (S_XMNS_7, SPR_XMNS, 6, 3, NULL, S_XMNS_8);
    ST (S_XMNS_8, SPR_XMNS, 7, 3, NULL, S_XMNS_9);
    ST (S_XMNS_9, SPR_XMNS, 8, 3, NULL, S_NULL);

    m = &mobjinfo[MT_XMINOTAUR];
    m->doomednum = -1;        m->spawnstate  = S_XMNT_LOOK1; m->spawnhealth = 2500;
    m->seestate  = S_XMNT_WALK1; m->seesound = sfx_x_mnsit;  m->reactiontime = 8;
    m->attacksound = sfx_x_mnatk;m->painstate = S_XMNT_PAIN1;m->painchance = 25;
    m->painsound = sfx_x_mnpai;  m->meleestate = S_XMNT_ATK1_1; m->missilestate = S_XMNT_ATK2_1;
    m->deathstate = S_XMNT_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_x_mndth;
    m->speed = 16; m->radius = 28*FRACUNIT; m->height = 100*FRACUNIT; m->mass = 800;
    m->damage = 7; m->activesound = sfx_x_mnact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XMNTFX1];
    m->doomednum = -1;        m->spawnstate  = S_XMF1_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XMF1_BOOM1;m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 20*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 6*FRACUNIT; m->mass = 100;
    m->damage = 3; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XMNTFX2];
    m->doomednum = -1;        m->spawnstate  = S_XMF2_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XMF2_BOOM1;m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 14*FRACUNIT; m->radius = 5*FRACUNIT; m->height = 12*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XMNTFX3];
    m->doomednum = -1;        m->spawnstate  = S_XMF3_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XMF2_BOOM1;m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 0; m->radius = 8*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XMNTSMOKE];
    m->doomednum = -1;        m->spawnstate  = S_XMNS_1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_NULL;      m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY|MF_SHADOW; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Korax's bat (crispy S_BAT*) ----
    ST (S_XBAT_1,   SPR_XABA, 0, 2, (actionf_p1)A_BatMove, S_XBAT_2);
    ST (S_XBAT_2,   SPR_XABA, 1, 2, (actionf_p1)A_BatMove, S_XBAT_3);
    ST (S_XBAT_3,   SPR_XABA, 2, 2, (actionf_p1)A_BatMove, S_XBAT_1);
    ST (S_XBAT_DIE, SPR_XABA, 0, 2, NULL,                  S_NULL);

    m = &mobjinfo[MT_XBAT];
    m->doomednum = -1;        m->spawnstate  = S_XBAT_1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XBAT_DIE;  m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    // reactiontime doubles as the bat's lifetime here (A_BatMove counts it down);
    // crispy keeps that in args[], which this mobj_t hasn't got.  ~3s of flapping.
    m->reactiontime = 3*TICRATE;
    m->speed = 5*FRACUNIT; m->radius = 3*FRACUNIT; m->height = 3*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_x_batscr;
    m->flags = MF_NOBLOCKMAP|MF_NOGRAVITY|MF_MISSILE; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Pig: the Hexen morph creature (crispy S_PIG_*).  Sprite XPIG: A-D walk,
    //      E-L death.  A_PigLook/A_PigChase in crispy only poll the morph timer
    //      before delegating, so plain A_Look/A_Chase are used here (files/p_morph.c
    //      ages the morph itself). ----
    ST (S_XPIG_LOOK1, SPR_XPIG,  1, 10, (actionf_p1)A_Look,       S_XPIG_LOOK1);
    ST (S_XPIG_WALK1, SPR_XPIG,  0,  3, (actionf_p1)A_Chase,      S_XPIG_WALK2);
    ST (S_XPIG_WALK2, SPR_XPIG,  1,  3, (actionf_p1)A_Chase,      S_XPIG_WALK3);
    ST (S_XPIG_WALK3, SPR_XPIG,  2,  3, (actionf_p1)A_Chase,      S_XPIG_WALK4);
    ST (S_XPIG_WALK4, SPR_XPIG,  3,  3, (actionf_p1)A_Chase,      S_XPIG_WALK1);
    ST (S_XPIG_PAIN1, SPR_XPIG,  3,  4, (actionf_p1)A_PigPain,    S_XPIG_WALK1);
    ST (S_XPIG_ATK1,  SPR_XPIG,  0,  5, (actionf_p1)A_FaceTarget, S_XPIG_ATK2);
    ST (S_XPIG_ATK2,  SPR_XPIG,  0, 10, (actionf_p1)A_PigAttack,  S_XPIG_WALK1);
    ST (S_XPIG_DIE1,  SPR_XPIG,  4,  4, (actionf_p1)A_Scream,     S_XPIG_DIE2);
    ST (S_XPIG_DIE2,  SPR_XPIG,  5,  3, (actionf_p1)A_Fall,       S_XPIG_DIE3);
    ST (S_XPIG_DIE3,  SPR_XPIG,  6,  4, NULL,                     S_XPIG_DIE4);
    ST (S_XPIG_DIE4,  SPR_XPIG,  7,  3, NULL,                     S_XPIG_DIE5);
    ST (S_XPIG_DIE5,  SPR_XPIG,  8,  4, NULL,                     S_XPIG_DIE6);
    ST (S_XPIG_DIE6,  SPR_XPIG,  9,  4, NULL,                     S_XPIG_DIE7);
    ST (S_XPIG_DIE7,  SPR_XPIG, 10,  4, NULL,                     S_XPIG_DIE8);
    ST (S_XPIG_DIE8,  SPR_XPIG, 11, -1, NULL,                     S_NULL);

    m = &mobjinfo[MT_XPIG];
    m->doomednum = -1;        m->spawnstate  = S_XPIG_LOOK1; m->spawnhealth = 25;
    m->seestate  = S_XPIG_WALK1; m->seesound = sfx_x_pgact;  m->reactiontime = 8;
    m->attacksound = sfx_x_pgatk;m->painstate = S_XPIG_PAIN1;m->painchance = 128;
    m->painsound = sfx_x_pgpai;  m->meleestate = S_XPIG_ATK1;m->missilestate = S_NULL;
    m->deathstate = S_XPIG_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_x_pgdth;
    m->speed = 10; m->radius = 12*FRACUNIT; m->height = 22*FRACUNIT; m->mass = 60;
    m->damage = 0; m->activesound = sfx_x_pgact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- "Mash" variants (Hexen ednums 100-103): the same actor, but bloodless,
    //      ghostly and with NO death sequence -- they simply vanish when killed.
    //      They reuse their normal counterpart's states, so no new states here.
    //      crispy renders them MF_ALTSHADOW (40% alpha); this engine has no alpha
    //      blending, so MF_SHADOW (the spectre fuzz) stands in. ----
    m = &mobjinfo[MT_XETTIN_MASH];
    *m = mobjinfo[MT_XETTIN];
    m->doomednum = -1; m->deathstate = S_NULL; m->xdeathstate = S_NULL;
    m->flags |= MF_NOBLOOD | MF_SHADOW;

    m = &mobjinfo[MT_XCENTAUR_MASH];
    *m = mobjinfo[MT_XCENTAUR];
    m->doomednum = -1; m->deathstate = S_NULL; m->xdeathstate = S_NULL;
    m->flags |= MF_NOBLOOD | MF_SHADOW;

    m = &mobjinfo[MT_XDEMON_MASH];
    *m = mobjinfo[MT_XDEMON];
    m->doomednum = -1; m->deathstate = S_NULL; m->xdeathstate = S_NULL;
    m->flags |= MF_NOBLOOD | MF_SHADOW;

    m = &mobjinfo[MT_XDEMON2_MASH];
    *m = mobjinfo[MT_XDEMON2];
    m->doomednum = -1; m->deathstate = S_NULL; m->xdeathstate = S_NULL;
    m->flags |= MF_NOBLOOD | MF_SHADOW;

    // ---- Fighter class boss (crispy S_FIGHTER*, sprite XPLA = Hexen PLAY) ----
    ST (S_XFTR_LOOK1, SPR_XPLA,  0,  5, (actionf_p1)A_Look,          S_XFTR_LOOK1);
    ST (S_XFTR_RUN1,  SPR_XPLA,  0,  4, (actionf_p1)A_Chase,         S_XFTR_RUN2);
    ST (S_XFTR_RUN2,  SPR_XPLA,  1,  4, (actionf_p1)A_Chase,         S_XFTR_RUN3);
    ST (S_XFTR_RUN3,  SPR_XPLA,  2,  4, (actionf_p1)A_Chase,         S_XFTR_RUN4);
    ST (S_XFTR_RUN4,  SPR_XPLA,  3,  4, (actionf_p1)A_Chase,         S_XFTR_RUN1);
    ST (S_XFTR_ATK1,  SPR_XPLA,  4,  8, (actionf_p1)A_FaceTarget,    S_XFTR_ATK2);
    ST (S_XFTR_ATK2,  SPR_XPLA,  5,  8, (actionf_p1)A_XFighterAttack,S_XFTR_RUN1);
    ST (S_XFTR_PAIN1, SPR_XPLA,  6,  4, NULL,                        S_XFTR_PAIN2);
    ST (S_XFTR_PAIN2, SPR_XPLA,  6,  4, (actionf_p1)A_Pain,          S_XFTR_RUN1);
    ST (S_XFTR_DIE1,  SPR_XPLA,  7,  6, NULL,                        S_XFTR_DIE2);
    ST (S_XFTR_DIE2,  SPR_XPLA,  8,  6, (actionf_p1)A_Scream,        S_XFTR_DIE3);
    ST (S_XFTR_DIE3,  SPR_XPLA,  9,  6, NULL,                        S_XFTR_DIE4);
    ST (S_XFTR_DIE4,  SPR_XPLA, 10,  6, NULL,                        S_XFTR_DIE5);
    ST (S_XFTR_DIE5,  SPR_XPLA, 11,  6, (actionf_p1)A_Fall,          S_XFTR_DIE6);
    ST (S_XFTR_DIE6,  SPR_XPLA, 12,  6, NULL,                        S_XFTR_DIE7);
    ST (S_XFTR_DIE7,  SPR_XPLA, 13, -1, NULL,                        S_NULL);

    ST (S_XFSF_MOVE1, SPR_XFSF, 32768, 3, NULL,                S_XFSF_MOVE2);
    ST (S_XFSF_MOVE2, SPR_XFSF, 32769, 3, NULL,                S_XFSF_MOVE3);
    ST (S_XFSF_MOVE3, SPR_XFSF, 32770, 3, NULL,                S_XFSF_MOVE1);
    ST (S_XFSF_BOOM1, SPR_XFSF, 32771, 4, NULL,                S_XFSF_BOOM2);
    ST (S_XFSF_BOOM2, SPR_XFSF, 32772, 3, NULL,                S_XFSF_BOOM3);
    ST (S_XFSF_BOOM3, SPR_XFSF, 32773, 4, NULL,                S_XFSF_BOOM4);
    ST (S_XFSF_BOOM4, SPR_XFSF, 32774, 3, NULL,                S_XFSF_BOOM5);
    ST (S_XFSF_BOOM5, SPR_XFSF, 32775, 4, NULL,                S_NULL);

    m = &mobjinfo[MT_XFIGHTERBOSS];
    m->doomednum = -1;        m->spawnstate  = S_XFTR_LOOK1; m->spawnhealth = 800;
    m->seestate  = S_XFTR_RUN1;  m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_x_fbatk;m->painstate = S_XFTR_PAIN1;m->painchance = 50;
    m->painsound = sfx_x_fbpai;  m->meleestate = S_NULL;   m->missilestate = S_XFTR_ATK1;
    m->deathstate = S_XFTR_DIE1; m->xdeathstate = S_NULL;  m->deathsound = sfx_x_fbdth;
    m->speed = 25; m->radius = 16*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XFSWORDFX];
    m->doomednum = -1;        m->spawnstate  = S_XFSF_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XFSF_BOOM1;m->xdeathstate = S_NULL;  m->deathsound = sfx_x_mnexp;
    m->speed = 30*FRACUNIT; m->radius = 16*FRACUNIT; m->height = 8*FRACUNIT; m->mass = 100;
    m->damage = 8; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Cleric class boss (crispy S_CLERIC*, sprite XCLE) ----
    ST (S_XCLR_LOOK1, SPR_XCLE,  0,  5, (actionf_p1)A_Look,         S_XCLR_LOOK1);
    ST (S_XCLR_RUN1,  SPR_XCLE,  0,  4, (actionf_p1)A_Chase,        S_XCLR_RUN2);
    ST (S_XCLR_RUN2,  SPR_XCLE,  1,  4, (actionf_p1)A_Chase,        S_XCLR_RUN3);
    ST (S_XCLR_RUN3,  SPR_XCLE,  2,  4, (actionf_p1)A_Chase,        S_XCLR_RUN4);
    ST (S_XCLR_RUN4,  SPR_XCLE,  3,  4, (actionf_p1)A_Chase,        S_XCLR_RUN1);
    ST (S_XCLR_ATK1,  SPR_XCLE,  4,  8, (actionf_p1)A_FaceTarget,   S_XCLR_ATK2);
    ST (S_XCLR_ATK2,  SPR_XCLE,  5,  8, (actionf_p1)A_FaceTarget,   S_XCLR_ATK3);
    ST (S_XCLR_ATK3,  SPR_XCLE,  6, 10, (actionf_p1)A_XClericAttack,S_XCLR_RUN1);
    ST (S_XCLR_PAIN1, SPR_XCLE,  7,  4, NULL,                       S_XCLR_PAIN2);
    ST (S_XCLR_PAIN2, SPR_XCLE,  7,  4, (actionf_p1)A_Pain,         S_XCLR_RUN1);
    ST (S_XCLR_DIE1,  SPR_XCLE,  8,  6, NULL,                       S_XCLR_DIE2);
    ST (S_XCLR_DIE2,  SPR_XCLE, 10,  6, (actionf_p1)A_Scream,       S_XCLR_DIE3);
    ST (S_XCLR_DIE3,  SPR_XCLE, 11,  6, NULL,                       S_XCLR_DIE4);
    ST (S_XCLR_DIE4,  SPR_XCLE, 11,  6, NULL,                       S_XCLR_DIE5);
    ST (S_XCLR_DIE5,  SPR_XCLE, 12,  6, (actionf_p1)A_Fall,         S_XCLR_DIE6);
    ST (S_XCLR_DIE6,  SPR_XCLE, 13,  6, NULL,                       S_XCLR_DIE7);
    ST (S_XCLR_DIE7,  SPR_XCLE, 14,  6, NULL,                       S_XCLR_DIE8);
    ST (S_XCLR_DIE8,  SPR_XCLE, 15,  6, NULL,                       S_XCLR_DIE9);
    ST (S_XCLR_DIE9,  SPR_XCLE, 16, -1, NULL,                       S_NULL);

    // Holy spirit: crispy tracks with A_CHolySeek; A_Tracer (the revenant homing
    // this port already reuses for MT_XSORCFX1) stands in.
    ST (S_XSPI_MOVE1, SPR_XSPI, 32783, 3, (actionf_p1)A_Tracer, S_XSPI_MOVE2);
    ST (S_XSPI_MOVE2, SPR_XSPI, 32783, 3, (actionf_p1)A_Tracer, S_XSPI_MOVE3);
    ST (S_XSPI_MOVE3, SPR_XSPI, 32783, 3, (actionf_p1)A_Tracer, S_XSPI_MOVE4);
    ST (S_XSPI_MOVE4, SPR_XSPI, 32783, 3, (actionf_p1)A_Tracer, S_XSPI_MOVE1);
    ST (S_XSPI_BOOM1, SPR_XSPI, 16, 3, NULL, S_XSPI_BOOM2);
    ST (S_XSPI_BOOM2, SPR_XSPI, 17, 3, NULL, S_XSPI_BOOM3);
    ST (S_XSPI_BOOM3, SPR_XSPI, 18, 3, NULL, S_XSPI_BOOM4);
    ST (S_XSPI_BOOM4, SPR_XSPI, 19, 3, NULL, S_XSPI_BOOM5);
    ST (S_XSPI_BOOM5, SPR_XSPI, 20, 3, NULL, S_NULL);

    m = &mobjinfo[MT_XCLERICBOSS];
    m->doomednum = -1;        m->spawnstate  = S_XCLR_LOOK1; m->spawnhealth = 800;
    m->seestate  = S_XCLR_RUN1;  m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_x_cbatk;m->painstate = S_XCLR_PAIN1;m->painchance = 50;
    m->painsound = sfx_x_cbpai;  m->meleestate = S_NULL;   m->missilestate = S_XCLR_ATK1;
    m->deathstate = S_XCLR_DIE1; m->xdeathstate = S_NULL;  m->deathsound = sfx_x_cbdth;
    m->speed = 25; m->radius = 16*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XHOLYFX];
    m->doomednum = -1;        m->spawnstate  = S_XSPI_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XSPI_BOOM1;m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 30*FRACUNIT; m->radius = 15*FRACUNIT; m->height = 8*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY|MF_SHADOW;
    m->flags2 = 0; m->raisestate = S_NULL;

    // ---- Mage class boss (crispy S_MAGE*, sprite XMAG) ----
    ST (S_XMGE_LOOK1, SPR_XMAG,     0,  5, (actionf_p1)A_Look,       S_XMGE_LOOK1);
    ST (S_XMGE_RUN1,  SPR_XMAG,     0,  4, (actionf_p1)A_Chase,      S_XMGE_RUN2);
    ST (S_XMGE_RUN2,  SPR_XMAG,     1,  4, (actionf_p1)A_Chase,      S_XMGE_RUN3);
    ST (S_XMGE_RUN3,  SPR_XMAG,     2,  4, (actionf_p1)A_Chase,      S_XMGE_RUN4);
    ST (S_XMGE_RUN4,  SPR_XMAG,     3,  4, (actionf_p1)A_Chase,      S_XMGE_RUN1);
    ST (S_XMGE_ATK1,  SPR_XMAG,     4,  8, (actionf_p1)A_FaceTarget, S_XMGE_ATK2);
    ST (S_XMGE_ATK2,  SPR_XMAG, 32773,  8, (actionf_p1)A_XMageAttack,S_XMGE_RUN1);
    ST (S_XMGE_PAIN1, SPR_XMAG,     6,  4, NULL,                     S_XMGE_PAIN2);
    ST (S_XMGE_PAIN2, SPR_XMAG,     6,  4, (actionf_p1)A_Pain,       S_XMGE_RUN1);
    ST (S_XMGE_DIE1,  SPR_XMAG,     7,  6, NULL,                     S_XMGE_DIE2);
    ST (S_XMGE_DIE2,  SPR_XMAG,     8,  6, (actionf_p1)A_Scream,     S_XMGE_DIE3);
    ST (S_XMGE_DIE3,  SPR_XMAG,     9,  6, NULL,                     S_XMGE_DIE4);
    ST (S_XMGE_DIE4,  SPR_XMAG,    10,  6, NULL,                     S_XMGE_DIE5);
    ST (S_XMGE_DIE5,  SPR_XMAG,    11,  6, (actionf_p1)A_Fall,       S_XMGE_DIE6);
    ST (S_XMGE_DIE6,  SPR_XMAG,    12,  6, NULL,                     S_XMGE_DIE7);
    ST (S_XMGE_DIE7,  SPR_XMAG,    13, -1, NULL,                     S_NULL);

    // Bloodscourge shot: crispy seeks with A_MStaffTrack -- A_Tracer stands in.
    ST (S_XMS2_MOVE1, SPR_XMS2, 32768, 2, (actionf_p1)A_Tracer, S_XMS2_MOVE2);
    ST (S_XMS2_MOVE2, SPR_XMS2, 32769, 2, (actionf_p1)A_Tracer, S_XMS2_MOVE3);
    ST (S_XMS2_MOVE3, SPR_XMS2, 32770, 2, (actionf_p1)A_Tracer, S_XMS2_MOVE4);
    ST (S_XMS2_MOVE4, SPR_XMS2, 32771, 2, (actionf_p1)A_Tracer, S_XMS2_MOVE1);
    ST (S_XMS2_BOOM1, SPR_XMS2, 32772, 4, NULL, S_XMS2_BOOM2);
    ST (S_XMS2_BOOM2, SPR_XMS2, 32773, 5, NULL, S_XMS2_BOOM3);
    ST (S_XMS2_BOOM3, SPR_XMS2, 32774, 5, NULL, S_XMS2_BOOM4);
    ST (S_XMS2_BOOM4, SPR_XMS2, 32775, 5, NULL, S_XMS2_BOOM5);
    ST (S_XMS2_BOOM5, SPR_XMS2, 32776, 4, NULL, S_NULL);

    m = &mobjinfo[MT_XMAGEBOSS];
    m->doomednum = -1;        m->spawnstate  = S_XMGE_LOOK1; m->spawnhealth = 800;
    m->seestate  = S_XMGE_RUN1;  m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_x_mbatk;m->painstate = S_XMGE_PAIN1;m->painchance = 50;
    m->painsound = sfx_x_mbpai;  m->meleestate = S_NULL;   m->missilestate = S_XMGE_ATK1;
    m->deathstate = S_XMGE_DIE1; m->xdeathstate = S_NULL;  m->deathsound = sfx_x_mbdth;
    m->speed = 25; m->radius = 16*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->flags2 = 0; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XMSTAFFFX];
    m->doomednum = -1;        m->spawnstate  = S_XMS2_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XMS2_BOOM1;m->xdeathstate = S_NULL;  m->deathsound = sfx_x_mbexp;
    m->speed = 17*FRACUNIT; m->radius = 20*FRACUNIT; m->height = 8*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->flags2 = 0; m->raisestate = S_NULL;
}

// ---------------------------------------------------------------------------
// Name -> mobjtype for the console "summon" command / director (extends the
// hexen.c Hexen_TypeByName table; the orchestrator wires these into the lookup).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Map-thing numbers.
//
// The additive Hexen actors are installed with doomednum -1 because they are
// normally SUMMON-ONLY: this engine's map path is DOOM's, and a real Hexen ednum
// left in the table would shadow a DOOM/Heretic map thing (Hexen's 9 is the
// Maulotaur, DOOM's is a Shotgun Guy spot).  On an actual Hexen-format map that
// is exactly backwards -- the numbers ARE Hexen's -- so P_SetupLevel calls this
// once, in hexen mode only, to hand every ported actor its real number.
//
// Numbers are Hexen's own (crispy-doom/src/hexen/info.c doomednum fields).
// Anything not ported keeps -1 and its map things are skipped with a log.
// ---------------------------------------------------------------------------
// Hexen map-thing number -> mobjtype, or -1 for an unported thing.
//
// Scanning ALL of mobjinfo[] is wrong here and was the MAP02/03/05 crash: DOOM's
// own actors answer first, so Hexen's thing 89 resolved to DOOM's MT_BOSSSPIT and
// A_BrainSpit walked a braintargets list a Hexen map never fills.  Search ONLY the
// additive Hexen block (its monsters through the deco/item types, stopping before
// the Strife reservations), which is the only range Hexen_SetMapEdnums touches.
int P_HexenThingType (int doomednum)
{
    int i;
    if (doomednum <= 0) return -1;
    for (i = MT_XETTIN; i < MT_S_FIELDGUARD && i < num_mobjtypes; i++)
	if (mobjinfo[i].doomednum == doomednum)
	    return i;
    return -1;
}

void Hexen_SetMapEdnums (void)
{
    static const struct { short type; short ednum; } tbl[] =
    {
	{ MT_XETTIN,        10030 },
	{ MT_XCENTAUR,        107 },
	{ MT_XSLAUGHTAUR,     115 },	// CentaurLeader
	{ MT_XDEMON,           31 },	// Chaos Serpent (green)
	{ MT_XDEMON2,        8080 },	// Chaos Serpent (brown)
	{ MT_XFIREDEMON,    10060 },	// Afrit
	{ MT_XWRAITH,          34 },	// Reiver
	{ MT_XWRAITHB,      10011 },	// Reiver (buried)
	{ MT_XBISHOP,         114 },
	{ MT_XICEGUY,        8020 },	// Wendigo
	{ MT_XSTALKER,        121 },	// Serpent
	{ MT_XSTALKERBOSS,    120 },	// Serpent Leader
	{ MT_XDRAGON,         254 },	// Death Wyvern
	{ MT_XKORAX,        10200 },
	{ MT_XHERESIARCH,   10080 },
	{ MT_XMINOTAUR,         9 },	// Maulotaur / Dark Servant
	{ MT_XFIGHTERBOSS,  10100 },
	{ MT_XCLERICBOSS,   10101 },
	{ MT_XMAGEBOSS,     10102 },
	{ 0, 0 }
    };
    int i;
    for (i = 0; tbl[i].ednum; i++)
	mobjinfo[tbl[i].type].doomednum = tbl[i].ednum;
}

int Hexen_Mon_TypeByName (const char* name)
{
    if (!name || !name[0]) return -1;
    if (!strcmp (name, "demon2") || !strcmp (name, "serpent2")
	|| !strcmp (name, "chaosserpent2")) return MT_XDEMON2;
    if (!strcmp (name, "wraithb") || !strcmp (name, "buriedwraith")
	|| !strcmp (name, "reiverb")
	|| !strcmp (name, "wraithburied")) return MT_XWRAITHB;	// gzdoom WraithBuried
    if (!strcmp (name, "korax")) return MT_XKORAX;
    if (!strcmp (name, "heresiarch") || !strcmp (name, "sorcerer")
	|| !strcmp (name, "sorcboss")) return MT_XHERESIARCH;
    // gzdoom calls the Heretic Maulotaur "Minotaur" and the Hexen Dark Servant
    // "MinotaurFriend" (raven/minotaur.zs); "minotaur"/"maulotaur" stay Heretic's
    // (files/heretic.c, resolved first), so this one answers to the Hexen names.
    if (!strcmp (name, "minotaurfriend") || !strcmp (name, "darkservant")
	|| !strcmp (name, "maulator")) return MT_XMINOTAUR;
    if (!strcmp (name, "bat")) return MT_XBAT;
    if (!strcmp (name, "pig")) return MT_XPIG;
    if (!strcmp (name, "fighterboss") || !strcmp (name, "fighter")) return MT_XFIGHTERBOSS;
    if (!strcmp (name, "clericboss")  || !strcmp (name, "cleric"))  return MT_XCLERICBOSS;
    if (!strcmp (name, "mageboss")    || !strcmp (name, "mage"))    return MT_XMAGEBOSS;
    if (!strcmp (name, "ettinmash")) return MT_XETTIN_MASH;
    if (!strcmp (name, "centaurmash")) return MT_XCENTAUR_MASH;
    if (!strcmp (name, "demon1mash") || !strcmp (name, "serpentmash")) return MT_XDEMON_MASH;
    if (!strcmp (name, "demon2mash") || !strcmp (name, "serpent2mash")) return MT_XDEMON2_MASH;
    return -1;
}
