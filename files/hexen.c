// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Additive Hexen monsters in the DOOM engine (HERETIC_HEXEN.md approach A) --
//	the same mechanism as files/heretic.c.  States/mobjinfo are appended to the
//	engine tables at runtime (Hexen_Init) so info.c's generated initializers stay
//	untouched; the enum slots live at the end of statenum_t/mobjtype_t/spritenum_t
//	(info.h).  Sprites: hexenstuff.wad (renamed X*, by tools/extract_hexen.py;
//	see tools/hexen_sprite_map.txt).  Sounds: DOOM SFX reused for now.
//
//	First monster: Ettin (a club-wielding melee brute).  More follow the pattern.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "info.h"
#include "m_random.h"
#include "m_fixed.h"
#include "tables.h"		// finesine/FINEANGLES/FINEMASK -- poison cloud bob
#include "sounds.h"
#include "w_wad.h"
#include "p_mobj.h"
#include "r_state.h"		// sprites[] -- presence test by parsed sprite, not lump name
#include "hexen.h"
#include "hexen_items.h"	// Hexen_ItemTypeByName -- chained into Hexen_TypeByName

extern state_t *states;
extern mobjinfo_t *mobjinfo;

// engine action funcs we call (no public header -- declare by hand)
extern void	A_Look (mobj_t*);
extern void	A_Chase (mobj_t*);
extern void	A_FaceTarget (mobj_t*);
extern void	A_Pain (mobj_t*);
extern void	A_Scream (mobj_t*);
extern void	A_Fall (mobj_t*);
extern boolean	P_CheckMeleeRange (mobj_t*);
extern void	P_DamageMobj (mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage);
extern void	S_StartSound (void* origin, int sfx_id);
extern mobj_t*	P_SpawnMobj (fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
extern boolean	P_SetMobjState (mobj_t* mobj, statenum_t state);
extern mobj_t*	P_SpawnMonsterChecked (fixed_t x, fixed_t y, mobjtype_t type);
extern mobj_t*	P_SpawnMissile (mobj_t* source, mobj_t* dest, mobjtype_t type);
extern void	P_PoisonRadiusAttack (mobj_t* spot, mobj_t* source);	// poison cloud (p_map.c)

// Hexen's HITDICE(d) melee damage = ((P_Random() & 7) + 1) * d.
#define HITDICE(d)	(((P_Random () & 7) + 1) * (d))

// ---------------------------------------------------------------------------
// Action functions (crispy hexen/p_enemy.c, adapted to DOOM's 1-arg signature).
// ---------------------------------------------------------------------------

// Ettin: pure melee, HITDICE(2) = 2..16.
void A_EttinAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, actor->info->attacksound);
    if (P_CheckMeleeRange (actor))
	P_DamageMobj (actor->target, actor, actor, HITDICE (2));
}

// Centaur: pure melee swipe, P_Random()%7 + 3 = 3..9 (crispy A_CentaurAttack).
void A_CentaurAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, actor->info->attacksound);
    if (P_CheckMeleeRange (actor))
	P_DamageMobj (actor->target, actor, actor, P_Random () % 7 + 3);
}

// Slaughtaur ranged: lob a (here non-reflecting, simplified) bolt
// (crispy A_CentaurAttack2 fires MT_CENTAUR_FX).
void A_CentaurAttack2 (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, actor->info->attacksound);
    P_SpawnMissile (actor, actor->target, MT_XCENTAUR_FX);
}

// Chaos Serpent melee (crispy A_DemonAttack1), HITDICE(2) = 2..16.
void A_DemonAttack1 (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_CheckMeleeRange (actor))
	P_DamageMobj (actor->target, actor, actor, HITDICE (2));
}

// Chaos Serpent fire-breath (crispy A_DemonAttack2); spawn the fireball a bit high.
void A_DemonAttack2 (mobj_t* actor)
{
    mobj_t* mo;
    if (!actor->target)
	return;
    mo = P_SpawnMissile (actor, actor->target, MT_XDEMON_FX);
    if (mo)
    {
	mo->z += 30*FRACUNIT;
	S_StartSound (actor, actor->info->attacksound);
    }
}

// Fire Demon / Afrit ranged fireball (crispy A_FiredAttack; rock-throw variants
// simplified away -- it just lobs the MT_XFIREDEMON_FX missile).
void A_FiredAttack (mobj_t* actor)
{
    mobj_t* mo;
    if (!actor->target)
	return;
    mo = P_SpawnMissile (actor, actor->target, MT_XFIREDEMON_FX);
    if (mo)
	S_StartSound (actor, actor->info->attacksound);
}

// Afrit death burst (crispy A_FiredSplotch): as the corpse bursts it throws out the
// two scorch-mark splotches, which arc away and settle on the floor.
void A_FiredSplotch (mobj_t* actor)
{
    mobj_t*	mo;
    int		i;

    for (i = 0; i < 2; i++)
    {
	mo = P_SpawnMobj (actor->x, actor->y, actor->z,
			  i ? MT_XFIREDEMON_SPL2 : MT_XFIREDEMON_SPL1);
	if (!mo)
	    continue;
	mo->momx = (P_Random () - 128) << 11;
	mo->momy = (P_Random () - 128) << 11;
	mo->momz = FRACUNIT*3 + (P_Random () << 10);
    }
}

// Reiver / Wraith melee: drains health (crispy A_WraithMelee).  HITDICE(2)=2..16.
void A_WraithMelee (mobj_t* actor)
{
    int amount;
    if (!actor->target)
	return;
    if (P_CheckMeleeRange (actor) && (P_Random () < 220))
    {
	amount = HITDICE (2);
	P_DamageMobj (actor->target, actor, actor, amount);
	actor->health += amount;	// steal life
    }
}

// Reiver / Wraith ranged bolt (crispy A_WraithMissile).
void A_WraithMissile (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_SpawnMissile (actor, actor->target, MT_XWRAITH_FX))
	S_StartSound (actor, actor->info->attacksound);
}

// Dark Bishop attack (crispy A_BishopAttack/A_BishopAttack2 merged + simplified:
// no special1 burst counter, no homing -- melee swing in range, else a plain
// (non-seeking) missile).  HITDICE(4) = 4..32 in melee.
void A_BishopAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    S_StartSound (actor, actor->info->attacksound);
    if (P_CheckMeleeRange (actor))
    {
	P_DamageMobj (actor->target, actor, actor, HITDICE (4));
	return;
    }
    P_SpawnMissile (actor, actor->target, MT_XBISHOP_FX);
}

// Wendigo / Ice Guy ranged ice shard (crispy A_IceGuyAttack; the symmetric
// dual-missile fan + wisp spawns are simplified to a single straight shard).
void A_IceGuyAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_SpawnMissile (actor, actor->target, MT_XICEGUY_FX))
	S_StartSound (actor, actor->info->attacksound);
}

// Ice Guy death = the Hexen ICE SHATTER (crispy A_IceGuyDie/A_FreezeDeathChunks, simplified):
// the floater bursts into a scatter of the shard's ice-shatter puffs and then vanishes (it
// leaves no corpse).  Fixes the death that used to freeze on one frame floating mid-air.
void A_IceGuyShatter (mobj_t* actor)
{
    int		i;
    int		hsteps = (actor->height >> FRACBITS) > 1 ? (actor->height >> FRACBITS) : 1;
    actor->momx = actor->momy = actor->momz = 0;
    for (i = 0; i < 8; i++)
    {
	fixed_t	dx = (P_Random () - 128) * (actor->radius >> 7);
	fixed_t	dy = (P_Random () - 128) * (actor->radius >> 7);
	fixed_t	dz = (fixed_t)(P_Random () % hsteps) << FRACBITS;
	mobj_t*	sh = P_SpawnMobj (actor->x + dx, actor->y + dy, actor->z + dz, MT_XICEGUY_FX);
	if (sh)
	{
	    sh->momx = sh->momy = sh->momz = 0;
	    sh->flags &= ~MF_MISSILE;			// harmless ice debris, not an attack
	    P_SetMobjState (sh, S_XICP_BOOM1);		// the ice-shatter puff frames
	}
    }
}

// Stalker / Serpent melee (crispy A_SerpentMeleeAttack; the re-check-for-attack
// chain dropped).  HITDICE(5) = 5..40.
void A_StalkerMelee (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_CheckMeleeRange (actor))
    {
	P_DamageMobj (actor->target, actor, actor, HITDICE (5));
	S_StartSound (actor, actor->info->attacksound);
    }
}

// Stalker / Serpent spit (crispy A_SerpentMissileAttack).
void A_StalkerMissile (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_SpawnMissile (actor, actor->target, MT_XSTALKER_FX))
	S_StartSound (actor, actor->info->attacksound);
}

// ---- authentic ZDoom Serpent ritual (g_hexen/a_serpent.cpp) ----------------
// The Serpent lurks submerged: INVISIBLE (MF2_DONTDRAW) and INVULNERABLE (no
// MF_SHOOTABLE) while it chases underwater; it humps to telegraph, then surfaces
// to attack (becoming visible + shootable) and dives back under afterwards.
void A_SerpentHide       (mobj_t* a) { a->flags2 |=  MF2_DONTDRAW; }	// submerge -> invisible
void A_SerpentUnHide     (mobj_t* a) { a->flags2 &= ~MF2_DONTDRAW; }	// surface  -> visible
void A_SerpentShootable  (mobj_t* a) { a->flags  |=  MF_SHOOTABLE; }	// surfaced -> vulnerable
void A_SerpentUnShootable(mobj_t* a) { a->flags  &= ~MF_SHOOTABLE; }	// diving   -> invulnerable

// In the underwater See loop: occasionally raise a hump to telegraph when the
// target is out of melee reach.  (A_Chase surfaces it to attack when in range.)
void A_SerpentHumpDecide (mobj_t* a)
{
    if (a->target && !P_CheckMeleeRange (a) && P_Random () < 30)
	P_SetMobjState (a, S_XSSP_HUMP1);
}

// After surfacing: bite if in reach, else the Serpent Leader spits; otherwise dive.
void A_SerpentChooseAttack (mobj_t* a)
{
    if (!a->target)
	return;
    if (P_CheckMeleeRange (a))
	P_SetMobjState (a, S_XSSP_MEL1);
    else if (a->type == MT_XSTALKERBOSS)
	P_SetMobjState (a, S_XSSP_MIS1);
    // else fall through to the dive (S_XSSP_ATK2's nextstate)
}

// Death Wyvern / Dragon fireball (crispy A_DragonAttack; the homing FX2 trails
// are simplified to a single straight fireball).
void A_DragonAttack (mobj_t* actor)
{
    if (!actor->target)
	return;
    if (P_SpawnMissile (actor, actor->target, MT_XDRAGON_FX))
	S_StartSound (actor, actor->info->attacksound);
}

// ---------------------------------------------------------------------------
// Poison cloud (crispy hexen/a_action.c A_PoisonBag*).  The lingering green gas
// left by the Cleric's Flechette (MT_XPOISONCLOUD).  Each damage frame does a
// short-radius POISON hit (routed to the poison-over-time path in P_DamageMobj,
// files/p_inter.c) and bobs the cloud; it lives for `reactiontime` check-cycles
// (crispy's special1 lifetime), then fades out.  Reuses mobj_t.reactiontime as the
// lifetime counter and mobj_t.movecount as the bob phase (this engine's mobj_t has
// no special1/special2), so no savegame-layout change.
// ---------------------------------------------------------------------------

// Repeated poison tick + gentle vertical bob (crispy A_PoisonBagDamage:
// A_Explode + z += FloatBobOffsets[i] >> 4, a 64-step sine period, amp 8 units).
void A_PoisonBagDamage (mobj_t* actor)
{
    int	bob;

    P_PoisonRadiusAttack (actor, actor->target);

    bob = actor->movecount & 63;
    actor->z += FixedMul (finesine[(bob * (FINEANGLES/64)) & FINEMASK], 8*FRACUNIT) >> 4;
    actor->movecount = (bob + 1) & 63;
}

// Count down the cloud's lifetime; when it runs out, start the fade-out
// (crispy A_PoisonBagCheck decrements special1, then -> S_POISONCLOUD_X1).
void A_PoisonBagCheck (mobj_t* actor)
{
    // On nightmare skill P_SpawnMobj leaves reactiontime at 0 (it skips info->reactiontime),
    // which would underflow the countdown and make the cloud immortal -- seed it here.
    if (actor->reactiontime <= 0)
	actor->reactiontime = 27;
    if (!--actor->reactiontime)
	P_SetMobjState (actor, S_XPCL_X1);	// lifetime spent -> fade out
    // else: keep looping the damage frames until the counter reaches 0
}

// The thrown Flechette poison bag settles, then bursts into the cloud a little above
// it (crispy A_PoisonBagInit, adapted to DOOM's 1-arg action signature).  The cloud
// inherits the bag's ->target so its poison kills attribute to whoever threw the bag.
void A_PoisonBagInit (mobj_t* actor)
{
    mobj_t*	cloud = P_SpawnMobj (actor->x, actor->y, actor->z + 28*FRACUNIT,
				     MT_XPOISONCLOUD);
    if (!cloud)
	return;
    cloud->target = actor->target;
    cloud->reactiontime = 24 + (P_Random () & 7);	// lifetime (crispy special1)
}

// Poison shroom idle throttle: hold the pulse frame a long random time between
// twitches (crispy A_PoisonShroom), so the plant sits mostly still.
void A_PoisonShroom (mobj_t* actor)
{
    actor->tics = 128 + (P_Random () << 1);
}

// ---------------------------------------------------------------------------
// Table fill
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

void Hexen_Init (void)
{
    mobjinfo_t*	m;

    // ---- Ettin (crispy S_ETTIN_*; one death sequence, ice/mace deaths omitted) ----
    ST (S_XETT_LOOK1,  SPR_XETT,  0, 10, (actionf_p1)A_Look,        S_XETT_LOOK2);
    ST (S_XETT_LOOK2,  SPR_XETT,  0, 10, (actionf_p1)A_Look,        S_XETT_LOOK1);
    ST (S_XETT_CHASE1, SPR_XETT,  0,  5, (actionf_p1)A_Chase,       S_XETT_CHASE2);
    ST (S_XETT_CHASE2, SPR_XETT,  1,  5, (actionf_p1)A_Chase,       S_XETT_CHASE3);
    ST (S_XETT_CHASE3, SPR_XETT,  2,  5, (actionf_p1)A_Chase,       S_XETT_CHASE4);
    ST (S_XETT_CHASE4, SPR_XETT,  3,  5, (actionf_p1)A_Chase,       S_XETT_CHASE1);
    ST (S_XETT_PAIN1,  SPR_XETT,  7,  7, (actionf_p1)A_Pain,        S_XETT_CHASE1);
    ST (S_XETT_ATK1,   SPR_XETT,  4,  6, (actionf_p1)A_FaceTarget,  S_XETT_ATK2);
    ST (S_XETT_ATK2,   SPR_XETT,  5,  6, (actionf_p1)A_FaceTarget,  S_XETT_ATK3);
    ST (S_XETT_ATK3,   SPR_XETT,  6,  8, (actionf_p1)A_EttinAttack, S_XETT_CHASE1);
    ST (S_XETT_DIE1,   SPR_XETT,  8,  4, NULL,                      S_XETT_DIE2);
    ST (S_XETT_DIE2,   SPR_XETT,  9,  4, NULL,                      S_XETT_DIE3);
    ST (S_XETT_DIE3,   SPR_XETT, 10,  4, (actionf_p1)A_Scream,      S_XETT_DIE4);
    ST (S_XETT_DIE4,   SPR_XETT, 11,  4, (actionf_p1)A_Fall,        S_XETT_DIE5);
    ST (S_XETT_DIE5,   SPR_XETT, 12,  4, NULL,                      S_XETT_DIE6);
    ST (S_XETT_DIE6,   SPR_XETT, 13,  4, NULL,                      S_XETT_DIE7);
    ST (S_XETT_DIE7,   SPR_XETT, 14,  4, NULL,                      S_XETT_DIE8);
    ST (S_XETT_DIE8,   SPR_XETT, 15,  4, NULL,                      S_XETT_DIE9);
    ST (S_XETT_DIE9,   SPR_XETT, 16, -1, NULL,                      S_NULL);

    m = &mobjinfo[MT_XETTIN];
    m->doomednum = -1;        m->spawnstate  = S_XETT_LOOK1; m->spawnhealth = 175;
    m->seestate  = S_XETT_CHASE1; m->seesound  = sfx_x_etsit; m->reactiontime = 8;
    m->attacksound = sfx_x_etatk; m->painstate = S_XETT_PAIN1; m->painchance = 60;
    m->painsound = sfx_x_etpai;   m->meleestate = S_XETT_ATK1; m->missilestate = S_NULL;
    m->deathstate = S_XETT_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_etdth;
    m->speed = 13; m->radius = 25*FRACUNIT; m->height = 68*FRACUNIT; m->mass = 175;
    m->damage = 0; m->activesound = sfx_x_etsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate = S_NULL;

    // ---- Centaur / Slaughtaur (crispy S_CENTAUR_*; reflect-shield + ice/sword
    //      deaths simplified away -- one plain death sequence, like the Ettin) ----
    ST (S_XCEN_LOOK1, SPR_XCEN,  0, 10, (actionf_p1)A_Look,         S_XCEN_LOOK2);
    ST (S_XCEN_LOOK2, SPR_XCEN,  1, 10, (actionf_p1)A_Look,         S_XCEN_LOOK1);
    ST (S_XCEN_WALK1, SPR_XCEN,  0,  4, (actionf_p1)A_Chase,        S_XCEN_WALK2);
    ST (S_XCEN_WALK2, SPR_XCEN,  1,  4, (actionf_p1)A_Chase,        S_XCEN_WALK3);
    ST (S_XCEN_WALK3, SPR_XCEN,  2,  4, (actionf_p1)A_Chase,        S_XCEN_WALK4);
    ST (S_XCEN_WALK4, SPR_XCEN,  3,  4, (actionf_p1)A_Chase,        S_XCEN_WALK1);
    ST (S_XCEN_ATK1,  SPR_XCEN,  7,  5, (actionf_p1)A_FaceTarget,   S_XCEN_ATK2);
    ST (S_XCEN_ATK2,  SPR_XCEN,  8,  4, (actionf_p1)A_FaceTarget,   S_XCEN_ATK3);
    ST (S_XCEN_ATK3,  SPR_XCEN,  9,  7, (actionf_p1)A_CentaurAttack,S_XCEN_WALK1);
    ST (S_XCEN_MIS1,  SPR_XCEN,  4, 10, (actionf_p1)A_FaceTarget,   S_XCEN_MIS2);
    ST (S_XCEN_MIS2,  SPR_XCEN, 32773, 8,(actionf_p1)A_CentaurAttack2,S_XCEN_MIS3);
    ST (S_XCEN_MIS3,  SPR_XCEN,  4, 10, (actionf_p1)A_FaceTarget,   S_XCEN_MIS4);
    ST (S_XCEN_MIS4,  SPR_XCEN, 32773, 8,(actionf_p1)A_CentaurAttack2,S_XCEN_WALK1);
    ST (S_XCEN_PAIN1, SPR_XCEN,  6,  6, NULL,                       S_XCEN_PAIN2);
    ST (S_XCEN_PAIN2, SPR_XCEN,  6,  6, (actionf_p1)A_Pain,         S_XCEN_WALK1);
    ST (S_XCEN_DIE1,  SPR_XCEN, 10,  4, NULL,                       S_XCEN_DIE2);
    ST (S_XCEN_DIE2,  SPR_XCEN, 11,  4, (actionf_p1)A_Scream,       S_XCEN_DIE3);
    ST (S_XCEN_DIE3,  SPR_XCEN, 12,  4, NULL,                       S_XCEN_DIE4);
    ST (S_XCEN_DIE4,  SPR_XCEN, 13,  4, NULL,                       S_XCEN_DIE5);
    ST (S_XCEN_DIE5,  SPR_XCEN, 14,  4, (actionf_p1)A_Fall,         S_XCEN_DIE6);
    ST (S_XCEN_DIE6,  SPR_XCEN, 15,  4, NULL,                       S_XCEN_DIE7);
    ST (S_XCEN_DIE7,  SPR_XCEN, 16,  4, NULL,                       S_XCEN_DIE8);
    ST (S_XCEN_DIE8,  SPR_XCEN, 17,  4, NULL,                       S_XCEN_DIE9);
    ST (S_XCEN_DIE9,  SPR_XCEN, 18,  4, NULL,                       S_XCEN_DIE10);
    ST (S_XCEN_DIE10, SPR_XCEN, 19, -1, NULL,                       S_NULL);

    // Slaughtaur bolt projectile (crispy S_CENTAUR_FX*; reflection dropped).
    ST (S_XCTF_MOVE1, SPR_XCTF, 32768, -1, NULL,                    S_NULL);
    ST (S_XCTF_X1,    SPR_XCTF, 32769, 4, NULL,                     S_XCTF_X2);
    ST (S_XCTF_X2,    SPR_XCTF, 32770, 3, NULL,                     S_XCTF_X3);
    ST (S_XCTF_X3,    SPR_XCTF, 32771, 4, NULL,                     S_XCTF_X4);
    ST (S_XCTF_X4,    SPR_XCTF, 32772, 3, NULL,                     S_XCTF_X5);
    ST (S_XCTF_X5,    SPR_XCTF, 32773, 2, NULL,                     S_NULL);

    // Centaur: pure melee.  doomednum 107 in Hexen but -1 here (summon-only).
    m = &mobjinfo[MT_XCENTAUR];
    m->doomednum = -1;        m->spawnstate  = S_XCEN_LOOK1; m->spawnhealth = 200;
    m->seestate  = S_XCEN_WALK1; m->seesound  = sfx_x_cesit; m->reactiontime = 8;
    m->attacksound = sfx_x_ceatk;m->painstate = S_XCEN_PAIN1; m->painchance = 135;
    m->painsound = sfx_x_cepai;  m->meleestate = S_XCEN_ATK1; m->missilestate = S_NULL;
    m->deathstate = S_XCEN_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_x_cedth;
    m->speed = 13; m->radius = 20*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 120;
    m->damage = 0; m->activesound = sfx_x_ceact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate = S_NULL;

    // Slaughtaur: tougher, also lobs a bolt at range.
    m = &mobjinfo[MT_XSLAUGHTAUR];
    m->doomednum = -1;        m->spawnstate  = S_XCEN_LOOK1; m->spawnhealth = 250;
    m->seestate  = S_XCEN_WALK1; m->seesound  = sfx_x_cesit; m->reactiontime = 8;
    m->attacksound = sfx_x_slatk;m->painstate = S_XCEN_PAIN1; m->painchance = 96;
    m->painsound = sfx_x_cepai;  m->meleestate = S_XCEN_ATK1; m->missilestate = S_XCEN_MIS1;
    m->deathstate = S_XCEN_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_x_cedth;
    m->speed = 10; m->radius = 20*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 120;
    m->damage = 0; m->activesound = sfx_x_ceact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate = S_NULL;

    // Slaughtaur bolt.
    m = &mobjinfo[MT_XCENTAUR_FX];
    m->doomednum = -1;        m->spawnstate  = S_XCTF_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XCTF_X1;   m->xdeathstate = S_NULL;  m->deathsound = sfx_firxpl;
    m->speed = 20*FRACUNIT; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- Chaos Serpent / Demon (crispy S_DEMN_*; gib XDeath simplified to the
    //      plain death so no chunk actors are needed) ----
    ST (S_XDEM_LOOK1,  SPR_XDEM,  0, 10, (actionf_p1)A_Look,        S_XDEM_LOOK2);
    ST (S_XDEM_LOOK2,  SPR_XDEM,  0, 10, (actionf_p1)A_Look,        S_XDEM_LOOK1);
    ST (S_XDEM_CHASE1, SPR_XDEM,  0,  4, (actionf_p1)A_Chase,       S_XDEM_CHASE2);
    ST (S_XDEM_CHASE2, SPR_XDEM,  1,  4, (actionf_p1)A_Chase,       S_XDEM_CHASE3);
    ST (S_XDEM_CHASE3, SPR_XDEM,  2,  4, (actionf_p1)A_Chase,       S_XDEM_CHASE4);
    ST (S_XDEM_CHASE4, SPR_XDEM,  3,  4, (actionf_p1)A_Chase,       S_XDEM_CHASE1);
    ST (S_XDEM_ATK1_1, SPR_XDEM,  4,  6, (actionf_p1)A_FaceTarget,  S_XDEM_ATK1_2);
    ST (S_XDEM_ATK1_2, SPR_XDEM,  5,  8, (actionf_p1)A_FaceTarget,  S_XDEM_ATK1_3);
    ST (S_XDEM_ATK1_3, SPR_XDEM,  6,  6, (actionf_p1)A_DemonAttack1,S_XDEM_CHASE1);
    ST (S_XDEM_ATK2_1, SPR_XDEM,  4,  5, (actionf_p1)A_FaceTarget,  S_XDEM_ATK2_2);
    ST (S_XDEM_ATK2_2, SPR_XDEM,  5,  6, (actionf_p1)A_FaceTarget,  S_XDEM_ATK2_3);
    ST (S_XDEM_ATK2_3, SPR_XDEM,  6,  5, (actionf_p1)A_DemonAttack2,S_XDEM_CHASE1);
    ST (S_XDEM_PAIN1,  SPR_XDEM,  4,  4, NULL,                      S_XDEM_PAIN2);
    ST (S_XDEM_PAIN2,  SPR_XDEM,  4,  4, (actionf_p1)A_Pain,        S_XDEM_CHASE1);
    ST (S_XDEM_DIE1,   SPR_XDEM,  7,  6, NULL,                      S_XDEM_DIE2);
    ST (S_XDEM_DIE2,   SPR_XDEM,  8,  6, NULL,                      S_XDEM_DIE3);
    ST (S_XDEM_DIE3,   SPR_XDEM,  9,  6, (actionf_p1)A_Scream,      S_XDEM_DIE4);
    ST (S_XDEM_DIE4,   SPR_XDEM, 10,  6, (actionf_p1)A_Fall,        S_XDEM_DIE5);
    ST (S_XDEM_DIE5,   SPR_XDEM, 11,  6, NULL,                      S_XDEM_DIE6);
    ST (S_XDEM_DIE6,   SPR_XDEM, 12,  6, NULL,                      S_XDEM_DIE7);
    ST (S_XDEM_DIE7,   SPR_XDEM, 13,  6, NULL,                      S_XDEM_DIE8);
    ST (S_XDEM_DIE8,   SPR_XDEM, 14,  6, NULL,                      S_XDEM_DIE9);
    ST (S_XDEM_DIE9,   SPR_XDEM, 15, -1, NULL,                      S_NULL);

    // Chaos Serpent fireball (crispy S_DEMONFX_*).
    ST (S_XDMF_MOVE1, SPR_XDMF, 32768, 4, NULL,                     S_XDMF_MOVE2);
    ST (S_XDMF_MOVE2, SPR_XDMF, 32769, 4, NULL,                     S_XDMF_MOVE3);
    ST (S_XDMF_MOVE3, SPR_XDMF, 32770, 4, NULL,                     S_XDMF_MOVE1);
    ST (S_XDMF_BOOM1, SPR_XDMF, 32771, 4, NULL,                     S_XDMF_BOOM2);
    ST (S_XDMF_BOOM2, SPR_XDMF, 32772, 4, NULL,                     S_XDMF_BOOM3);
    ST (S_XDMF_BOOM3, SPR_XDMF, 32773, 3, NULL,                     S_XDMF_BOOM4);
    ST (S_XDMF_BOOM4, SPR_XDMF, 32774, 3, NULL,                     S_XDMF_BOOM5);
    ST (S_XDMF_BOOM5, SPR_XDMF, 32775, 3, NULL,                     S_NULL);

    m = &mobjinfo[MT_XDEMON];
    m->doomednum = -1;        m->spawnstate  = S_XDEM_LOOK1; m->spawnhealth = 250;
    m->seestate  = S_XDEM_CHASE1; m->seesound = sfx_x_desit; m->reactiontime = 8;
    m->attacksound = sfx_x_deatk;m->painstate = S_XDEM_PAIN1; m->painchance = 50;
    m->painsound = sfx_x_depai;  m->meleestate = S_XDEM_ATK1_1; m->missilestate = S_XDEM_ATK2_1;
    m->deathstate = S_XDEM_DIE1; m->xdeathstate = S_NULL;    m->deathsound = sfx_x_dedth;
    m->speed = 13; m->radius = 32*FRACUNIT; m->height = 64*FRACUNIT; m->mass = 220;
    m->damage = 0; m->activesound = sfx_x_desit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XDEMON_FX];
    m->doomednum = -1;        m->spawnstate  = S_XDMF_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XDMF_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_firxpl;
    m->speed = 15*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 6*FRACUNIT; m->mass = 100;
    m->damage = 5; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- Fire Demon / Afrit (crispy S_FIRED_*; the multi-stage spawn/look ritual
    //      and rock-throw "split" deaths are simplified to a plain flyer that lobs
    //      a fireball, like a flying Slaughtaur).  Sprite frames are fullbright. ----
    ST (S_XFDM_LOOK1, SPR_XFDM, 32768, 10, (actionf_p1)A_Look,        S_XFDM_LOOK2);
    ST (S_XFDM_LOOK2, SPR_XFDM, 32769, 10, (actionf_p1)A_Look,        S_XFDM_LOOK3);
    ST (S_XFDM_LOOK3, SPR_XFDM, 32770, 10, (actionf_p1)A_Look,        S_XFDM_LOOK1);
    ST (S_XFDM_WALK1, SPR_XFDM, 32768,  5, (actionf_p1)A_Chase,       S_XFDM_WALK2);
    ST (S_XFDM_WALK2, SPR_XFDM, 32769,  5, (actionf_p1)A_Chase,       S_XFDM_WALK3);
    ST (S_XFDM_WALK3, SPR_XFDM, 32770,  5, (actionf_p1)A_Chase,       S_XFDM_WALK1);
    ST (S_XFDM_ATK1,  SPR_XFDM, 32778,  3, (actionf_p1)A_FaceTarget,  S_XFDM_ATK2);
    ST (S_XFDM_ATK2,  SPR_XFDM, 32778,  5, (actionf_p1)A_FiredAttack, S_XFDM_ATK3);
    ST (S_XFDM_ATK3,  SPR_XFDM, 32778,  5, (actionf_p1)A_FiredAttack, S_XFDM_ATK4);
    ST (S_XFDM_ATK4,  SPR_XFDM, 32778,  5, (actionf_p1)A_FiredAttack, S_XFDM_WALK1);
    ST (S_XFDM_PAIN1, SPR_XFDM, 32771,  6, (actionf_p1)A_Pain,        S_XFDM_WALK1);
    // Death runs all the way through Hexen's burst: the body burns down (D, L) and
    // then bursts apart (M, N, O) throwing out two scorch splotches.  crispy splits
    // this over deathstate + crashstate (played when the falling corpse lands); this
    // engine has no crashstate field, so the burst is chained onto the death frames.
    ST (S_XFDM_DIE1,  SPR_XFDM, 32771,  4, (actionf_p1)A_FaceTarget,  S_XFDM_DIE2);
    ST (S_XFDM_DIE2,  SPR_XFDM, 32779,  4, (actionf_p1)A_Scream,      S_XFDM_DIE3);
    ST (S_XFDM_DIE3,  SPR_XFDM, 32779,  4, (actionf_p1)A_Fall,        S_XFDM_DIE4);
    ST (S_XFDM_DIE4,  SPR_XFDM, 32779,  4, NULL,                      S_XFDM_DIE5);
    ST (S_XFDM_DIE5,  SPR_XFDM,    12,  5, NULL,                      S_XFDM_DIE6);
    ST (S_XFDM_DIE6,  SPR_XFDM,    13,  5, NULL,                      S_XFDM_DIE7);
    ST (S_XFDM_DIE7,  SPR_XFDM,    14,  5, (actionf_p1)A_FiredSplotch,S_NULL);

    // The two splotches the burst throws out (crispy S_FIRED_CORPSE1..6): they arc
    // away for a few tics and then rest as scorch marks (frames Y / Z).
    ST (S_XFDS_DROP1, SPR_XFDM, 15,  3, NULL,                         S_XFDS_LAND1);
    ST (S_XFDS_LAND1, SPR_XFDM, 15,  6, NULL,                         S_XFDS_REST1);
    ST (S_XFDS_REST1, SPR_XFDM, 24, -1, NULL,                         S_NULL);
    ST (S_XFDS_DROP2, SPR_XFDM, 16,  3, NULL,                         S_XFDS_LAND2);
    ST (S_XFDS_LAND2, SPR_XFDM, 16,  6, NULL,                         S_XFDS_REST2);
    ST (S_XFDS_REST2, SPR_XFDM, 25, -1, NULL,                         S_NULL);

    // Fire Demon fireball (crispy S_FIRED_FX6_*).
    ST (S_XFDB_MOVE1, SPR_XFDB, 32768, 5, NULL,                       S_XFDB_MOVE2);
    ST (S_XFDB_MOVE2, SPR_XFDB, 32768, 5, NULL,                       S_XFDB_MOVE3);
    ST (S_XFDB_MOVE3, SPR_XFDB, 32768, 5, NULL,                       S_XFDB_MOVE1);
    ST (S_XFDB_BOOM1, SPR_XFDB, 32769, 4, NULL,                       S_XFDB_BOOM2);
    ST (S_XFDB_BOOM2, SPR_XFDB, 32770, 4, NULL,                       S_XFDB_BOOM3);
    ST (S_XFDB_BOOM3, SPR_XFDB, 32771, 4, NULL,                       S_XFDB_BOOM4);
    ST (S_XFDB_BOOM4, SPR_XFDB, 32772, 4, NULL,                       S_XFDB_BOOM5);
    ST (S_XFDB_BOOM5, SPR_XFDB, 32772, 3, NULL,                       S_NULL);

    m = &mobjinfo[MT_XFIREDEMON];
    m->doomednum = -1;        m->spawnstate  = S_XFDM_LOOK1; m->spawnhealth = 80;
    m->seestate  = S_XFDM_WALK1; m->seesound  = sfx_x_fdact; m->reactiontime = 8;
    m->attacksound = sfx_x_fdatk; m->painstate = S_XFDM_PAIN1; m->painchance = 1;
    m->painsound = sfx_x_fdpai;   m->meleestate = S_NULL;     m->missilestate = S_XFDM_ATK1;
    m->deathstate = S_XFDM_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_fddth;
    m->speed = 13; m->radius = 20*FRACUNIT; m->height = 68*FRACUNIT; m->mass = 75;
    m->damage = 1; m->activesound = sfx_x_fdact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FLOAT|MF_NOGRAVITY; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XFIREDEMON_FX];
    m->doomednum = -1;        m->spawnstate  = S_XFDB_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XFDB_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_x_fdhit;
    m->speed = 10*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 6*FRACUNIT; m->mass = 15;
    m->damage = 1; m->activesound = sfx_None;	// crispy MT_FIREDEMON_FX6 damage = 1
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // Afrit death splotches (crispy MT_FIREDEMON_SPLOTCH1/2): inert scorch marks,
    // thrown by A_FiredSplotch and left lying where they land.
    m = &mobjinfo[MT_XFIREDEMON_SPL1];
    m->doomednum = -1;        m->spawnstate  = S_XFDS_DROP1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_NULL;      m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 0; m->radius = 3*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_DROPOFF|MF_CORPSE; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XFIREDEMON_SPL2];
    *m = mobjinfo[MT_XFIREDEMON_SPL1];
    m->spawnstate = S_XFDS_DROP2;

    // ---- Reiver / Wraith (crispy S_WRAITH_*; the rise-from-ground init and ice
    //      death are simplified away).  Floating undead: melee drains health, also
    //      lobs a bolt at range.  Sprite: XWRT (A-D walk, E-G attack, H pain, I-R die). ----
    ST (S_XWRT_LOOK1,  SPR_XWRT,  0, 15, (actionf_p1)A_Look,         S_XWRT_LOOK2);
    ST (S_XWRT_LOOK2,  SPR_XWRT,  1, 15, (actionf_p1)A_Look,         S_XWRT_LOOK1);
    ST (S_XWRT_CHASE1, SPR_XWRT,  0,  4, (actionf_p1)A_Chase,        S_XWRT_CHASE2);
    ST (S_XWRT_CHASE2, SPR_XWRT,  1,  4, (actionf_p1)A_Chase,        S_XWRT_CHASE3);
    ST (S_XWRT_CHASE3, SPR_XWRT,  2,  4, (actionf_p1)A_Chase,        S_XWRT_CHASE4);
    ST (S_XWRT_CHASE4, SPR_XWRT,  3,  4, (actionf_p1)A_Chase,        S_XWRT_CHASE1);
    ST (S_XWRT_ATK1_1, SPR_XWRT,  4,  6, (actionf_p1)A_FaceTarget,   S_XWRT_ATK1_2);
    ST (S_XWRT_ATK1_2, SPR_XWRT,  5,  6, (actionf_p1)A_FaceTarget,   S_XWRT_ATK1_3);
    ST (S_XWRT_ATK1_3, SPR_XWRT,  6,  6, (actionf_p1)A_WraithMelee,  S_XWRT_CHASE1);
    ST (S_XWRT_ATK2_1, SPR_XWRT,  4,  6, (actionf_p1)A_FaceTarget,   S_XWRT_ATK2_2);
    ST (S_XWRT_ATK2_2, SPR_XWRT,  5,  6, (actionf_p1)A_FaceTarget,   S_XWRT_ATK2_3);
    ST (S_XWRT_ATK2_3, SPR_XWRT,  6,  6, (actionf_p1)A_WraithMissile,S_XWRT_CHASE1);
    ST (S_XWRT_PAIN1,  SPR_XWRT,  7,  2, NULL,                       S_XWRT_PAIN2);
    ST (S_XWRT_PAIN2,  SPR_XWRT,  7,  6, (actionf_p1)A_Pain,         S_XWRT_CHASE1);
    ST (S_XWRT_DIE1,   SPR_XWRT,  8,  4, NULL,                       S_XWRT_DIE2);
    ST (S_XWRT_DIE2,   SPR_XWRT,  9,  4, (actionf_p1)A_Scream,       S_XWRT_DIE3);
    ST (S_XWRT_DIE3,   SPR_XWRT, 10,  4, NULL,                       S_XWRT_DIE4);
    ST (S_XWRT_DIE4,   SPR_XWRT, 11,  4, (actionf_p1)A_Fall,         S_XWRT_DIE5);
    ST (S_XWRT_DIE5,   SPR_XWRT, 12,  4, NULL,                       S_XWRT_DIE6);
    ST (S_XWRT_DIE6,   SPR_XWRT, 13,  4, NULL,                       S_XWRT_DIE7);
    ST (S_XWRT_DIE7,   SPR_XWRT, 14,  4, NULL,                       S_XWRT_DIE8);
    ST (S_XWRT_DIE8,   SPR_XWRT, 15,  5, NULL,                       S_XWRT_DIE9);
    ST (S_XWRT_DIE9,   SPR_XWRT, 16,  5, NULL,                       S_XWRT_DIE10);
    ST (S_XWRT_DIE10,  SPR_XWRT, 17, -1, NULL,                       S_NULL);

    // Reiver bolt (crispy S_WRTHFX_MOVE*/BOOM*).
    ST (S_XWRB_MOVE1, SPR_XWRB, 32768, 3, NULL,                      S_XWRB_MOVE2);
    ST (S_XWRB_MOVE2, SPR_XWRB, 32769, 3, NULL,                      S_XWRB_MOVE3);
    ST (S_XWRB_MOVE3, SPR_XWRB, 32770, 3, NULL,                      S_XWRB_MOVE1);
    ST (S_XWRB_BOOM1, SPR_XWRB, 32771, 4, NULL,                      S_XWRB_BOOM2);
    ST (S_XWRB_BOOM2, SPR_XWRB, 32772, 4, NULL,                      S_XWRB_BOOM3);
    ST (S_XWRB_BOOM3, SPR_XWRB, 32773, 4, NULL,                      S_XWRB_BOOM4);
    ST (S_XWRB_BOOM4, SPR_XWRB, 32774, 3, NULL,                      S_XWRB_BOOM5);
    ST (S_XWRB_BOOM5, SPR_XWRB, 32775, 3, NULL,                      S_XWRB_BOOM6);
    ST (S_XWRB_BOOM6, SPR_XWRB, 32776, 3, NULL,                      S_NULL);

    m = &mobjinfo[MT_XWRAITH];
    m->doomednum = -1;        m->spawnstate  = S_XWRT_LOOK1; m->spawnhealth = 150;
    m->seestate  = S_XWRT_CHASE1; m->seesound  = sfx_x_wrsit; m->reactiontime = 8;
    m->attacksound = sfx_x_wratk; m->painstate = S_XWRT_PAIN1; m->painchance = 25;
    m->painsound = sfx_x_wrpai;   m->meleestate = S_XWRT_ATK1_1; m->missilestate = S_XWRT_ATK2_1;
    m->deathstate = S_XWRT_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_wrdth;
    m->speed = 11; m->radius = 20*FRACUNIT; m->height = 55*FRACUNIT; m->mass = 75;
    m->damage = 0; m->activesound = sfx_x_wract;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FLOAT|MF_NOGRAVITY; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XWRAITH_FX];
    m->doomednum = -1;        m->spawnstate  = S_XWRB_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XWRB_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_firxpl;
    m->speed = 14*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 6*FRACUNIT; m->mass = 5;
    m->damage = 5; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- Dark Bishop (crispy S_BISHOP_*; the teleport-blur evasion + homing
    //      missile + special1 burst counter are simplified away).  Floating caster:
    //      melee swing in range, else a plain missile.  Sprite XBIS (A-F frames
    //      0-5 rotated, attack/death frames fullbright). ----
    ST (S_XBIS_LOOK1, SPR_XBIS,  0, 10, (actionf_p1)A_Look,          S_XBIS_LOOK1);
    ST (S_XBIS_WALK1, SPR_XBIS,  0,  3, (actionf_p1)A_Chase,         S_XBIS_WALK2);
    ST (S_XBIS_WALK2, SPR_XBIS,  1,  3, (actionf_p1)A_Chase,         S_XBIS_WALK3);
    ST (S_XBIS_WALK3, SPR_XBIS,  2,  3, (actionf_p1)A_Chase,         S_XBIS_WALK1);
    ST (S_XBIS_ATK1,  SPR_XBIS,  0,  3, (actionf_p1)A_FaceTarget,    S_XBIS_ATK2);
    ST (S_XBIS_ATK2,  SPR_XBIS, 32771, 3, (actionf_p1)A_FaceTarget,  S_XBIS_ATK3);
    ST (S_XBIS_ATK3,  SPR_XBIS, 32772, 3, (actionf_p1)A_FaceTarget,  S_XBIS_ATK4);
    ST (S_XBIS_ATK4,  SPR_XBIS, 32773, 3, (actionf_p1)A_BishopAttack,S_XBIS_ATK5);
    ST (S_XBIS_ATK5,  SPR_XBIS, 32773, 5, (actionf_p1)A_FaceTarget,  S_XBIS_WALK1);
    ST (S_XBIS_PAIN1, SPR_XBIS,  2,  6, (actionf_p1)A_Pain,          S_XBIS_WALK1);
    ST (S_XBIS_DIE1,  SPR_XBIS,  6,  6, NULL,                        S_XBIS_DIE2);
    ST (S_XBIS_DIE2,  SPR_XBIS, 32775, 6, (actionf_p1)A_Scream,      S_XBIS_DIE3);
    ST (S_XBIS_DIE3,  SPR_XBIS, 32776, 5, (actionf_p1)A_Fall,        S_XBIS_DIE4);
    ST (S_XBIS_DIE4,  SPR_XBIS, 32777, 5, NULL,                      S_XBIS_DIE5);
    ST (S_XBIS_DIE5,  SPR_XBIS, 32778, 5, NULL,                      S_XBIS_DIE6);
    ST (S_XBIS_DIE6,  SPR_XBIS, 32779, 4, NULL,                      S_XBIS_DIE7);
    ST (S_XBIS_DIE7,  SPR_XBIS, 32780, 4, NULL,                      S_NULL);

    // Dark Bishop missile (crispy S_BISHFX*; seeking dropped -- straight flight).
    ST (S_XBPF_MOVE1, SPR_XBPF, 32768, 2, NULL,                      S_XBPF_MOVE2);
    ST (S_XBPF_MOVE2, SPR_XBPF, 32769, 2, NULL,                      S_XBPF_MOVE1);
    ST (S_XBPF_BOOM1, SPR_XBPF, 32770, 4, NULL,                      S_XBPF_BOOM2);
    ST (S_XBPF_BOOM2, SPR_XBPF, 32771, 4, NULL,                      S_XBPF_BOOM3);
    ST (S_XBPF_BOOM3, SPR_XBPF, 32772, 4, NULL,                      S_XBPF_BOOM4);
    ST (S_XBPF_BOOM4, SPR_XBPF, 32773, 3, NULL,                      S_XBPF_BOOM5);
    ST (S_XBPF_BOOM5, SPR_XBPF, 32774, 3, NULL,                      S_NULL);

    m = &mobjinfo[MT_XBISHOP];
    m->doomednum = -1;        m->spawnstate  = S_XBIS_LOOK1; m->spawnhealth = 130;
    m->seestate  = S_XBIS_WALK1; m->seesound  = sfx_x_bisit; m->reactiontime = 8;
    m->attacksound = sfx_x_biatk; m->painstate = S_XBIS_PAIN1; m->painchance = 110;
    m->painsound = sfx_x_bipai;   m->meleestate = S_NULL;     m->missilestate = S_XBIS_ATK1;
    m->deathstate = S_XBIS_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_bidth;
    m->speed = 10; m->radius = 22*FRACUNIT; m->height = 65*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_x_biact;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FLOAT|MF_NOGRAVITY|MF_NOBLOOD; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XBISHOP_FX];
    m->doomednum = -1;        m->spawnstate  = S_XBPF_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XBPF_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_x_bihit;
    m->speed = 10*FRACUNIT; m->radius = 10*FRACUNIT; m->height = 6*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- Wendigo / Ice Guy (crispy S_ICEGUY_*; the dormant spawn, wisp/bit
    //      spawns and dual-missile fan are simplified to a plain floating caster
    //      that lobs one straight ice shard).  Sprite XICE (A-D walk, E-G attack
    //      fullbright).  Death = the ice SHATTER (frame A like crispy, then burst into
    //      ice-shatter puffs and vanish -- A_IceGuyShatter). ----
    ST (S_XICE_LOOK1, SPR_XICE,  0, 10, (actionf_p1)A_Look,         S_XICE_LOOK2);
    ST (S_XICE_LOOK2, SPR_XICE,  0, 10, (actionf_p1)A_Look,         S_XICE_LOOK1);
    ST (S_XICE_WALK1, SPR_XICE,  0,  4, (actionf_p1)A_Chase,        S_XICE_WALK2);
    ST (S_XICE_WALK2, SPR_XICE,  1,  4, (actionf_p1)A_Chase,        S_XICE_WALK3);
    ST (S_XICE_WALK3, SPR_XICE,  2,  4, (actionf_p1)A_Chase,        S_XICE_WALK4);
    ST (S_XICE_WALK4, SPR_XICE,  3,  4, (actionf_p1)A_Chase,        S_XICE_WALK1);
    ST (S_XICE_ATK1,  SPR_XICE,  4,  3, (actionf_p1)A_FaceTarget,   S_XICE_ATK2);
    ST (S_XICE_ATK2,  SPR_XICE,  5,  3, (actionf_p1)A_FaceTarget,   S_XICE_ATK3);
    ST (S_XICE_ATK3,  SPR_XICE, 32774, 8,(actionf_p1)A_IceGuyAttack,S_XICE_ATK4);
    ST (S_XICE_ATK4,  SPR_XICE,  5,  4, (actionf_p1)A_FaceTarget,   S_XICE_WALK1);
    ST (S_XICE_PAIN1, SPR_XICE,  0,  2, (actionf_p1)A_Pain,         S_XICE_WALK1);
    ST (S_XICE_DIE1,  SPR_XICE,  0,  5, (actionf_p1)A_Scream,         S_XICE_DIE2);
    ST (S_XICE_DIE2,  SPR_XICE,  0,  5, (actionf_p1)A_Fall,           S_XICE_DIE3);
    ST (S_XICE_DIE3,  SPR_XICE,  0,  3, (actionf_p1)A_IceGuyShatter,  S_NULL);

    // Wendigo ice shard (crispy S_ICEGUY_FX*/FX_X*).
    ST (S_XICP_MOVE1, SPR_XICP, 32768, 3, NULL,                     S_XICP_MOVE2);
    ST (S_XICP_MOVE2, SPR_XICP, 32769, 3, NULL,                     S_XICP_MOVE3);
    ST (S_XICP_MOVE3, SPR_XICP, 32770, 3, NULL,                     S_XICP_MOVE1);
    ST (S_XICP_BOOM1, SPR_XICP, 32771, 4, NULL,                     S_XICP_BOOM2);
    ST (S_XICP_BOOM2, SPR_XICP, 32772, 4, NULL,                     S_XICP_BOOM3);
    ST (S_XICP_BOOM3, SPR_XICP, 32773, 4, NULL,                     S_XICP_BOOM4);
    ST (S_XICP_BOOM4, SPR_XICP, 32774, 4, NULL,                     S_XICP_BOOM5);
    ST (S_XICP_BOOM5, SPR_XICP, 32775, 3, NULL,                     S_NULL);

    m = &mobjinfo[MT_XICEGUY];
    m->doomednum = -1;        m->spawnstate  = S_XICE_LOOK1; m->spawnhealth = 120;
    m->seestate  = S_XICE_WALK1; m->seesound  = sfx_x_icsit; m->reactiontime = 8;
    m->attacksound = sfx_x_icatk; m->painstate = S_XICE_PAIN1; m->painchance = 144;
    m->painsound = sfx_None;      m->meleestate = S_NULL;     m->missilestate = S_XICE_ATK1;
    m->deathstate = S_XICE_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_None;
    m->speed = 14; m->radius = 22*FRACUNIT; m->height = 75*FRACUNIT; m->mass = 150;
    m->damage = 0; m->activesound = sfx_x_icsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FLOAT|MF_NOGRAVITY|MF_NOBLOOD; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XICEGUY_FX];
    m->doomednum = -1;        m->spawnstate  = S_XICP_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XICP_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_x_ichit;
    m->speed = 14*FRACUNIT; m->radius = 8*FRACUNIT; m->height = 10*FRACUNIT; m->mass = 100;
    m->damage = 1; m->activesound = sfx_None;	// crispy MT_ICEGUY_FX damage = 1
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- Stalker / Serpent (crispy S_SERPENT_*; the underwater hide/dive/surface
    //      ritual is simplified away -- a plain ground ambusher that chases, then
    //      swings in melee or spits at range).  Sprite XSSP (8-9 walk, 10-13 attack,
    //      14-25 die). ----
    // LOOK: submerged (invisible + invulnerable), scanning for prey.
    ST (S_XSSP_LOOK1, SPR_XSSP,  7, 10, (actionf_p1)A_Look,              S_XSSP_LOOK2);
    ST (S_XSSP_LOOK2, SPR_XSSP,  7, 10, (actionf_p1)A_Look,              S_XSSP_LOOK1);
    // SEE: invisible underwater chase.  A_Chase surfaces it (-> meleestate/missilestate =
    // S_XSSP_SURF1) when the prey is in range; A_SerpentHumpDecide telegraphs at range.
    ST (S_XSSP_SEE1,  SPR_XSSP,  7,  2, (actionf_p1)A_Chase,             S_XSSP_SEE2);
    ST (S_XSSP_SEE2,  SPR_XSSP,  7,  3, (actionf_p1)A_SerpentHumpDecide, S_XSSP_SEE1);
    // HUMP: a telegraph bump breaks the surface (visible, still NOT shootable), then submerges.
    ST (S_XSSP_HUMP1, SPR_XSSP,  4,  3, (actionf_p1)A_SerpentUnHide,     S_XSSP_HUMP2);
    ST (S_XSSP_HUMP2, SPR_XSSP,  5,  3, NULL,                           S_XSSP_HUMP3);
    ST (S_XSSP_HUMP3, SPR_XSSP,  6,  3, NULL,                           S_XSSP_HUMP4);
    ST (S_XSSP_HUMP4, SPR_XSSP,  5,  3, (actionf_p1)A_SerpentHide,       S_XSSP_SEE1);
    // SURF: rise to attack -- become visible then shootable (the vulnerable window).
    ST (S_XSSP_SURF1, SPR_XSSP,  0,  2, (actionf_p1)A_SerpentUnHide,     S_XSSP_SURF2);
    ST (S_XSSP_SURF2, SPR_XSSP,  1,  3, (actionf_p1)A_SerpentShootable,  S_XSSP_ATK1);
    // ATK: face + choose bite / spit, then dive.
    ST (S_XSSP_ATK1,  SPR_XSSP, 10,  6, (actionf_p1)A_FaceTarget,          S_XSSP_ATK2);
    ST (S_XSSP_ATK2,  SPR_XSSP, 11,  5, (actionf_p1)A_SerpentChooseAttack, S_XSSD_DIVE1);
    ST (S_XSSP_MEL1,  SPR_XSSP, 13,  6, (actionf_p1)A_StalkerMelee,        S_XSSD_DIVE1);
    ST (S_XSSP_MIS1,  SPR_XSSP, 13,  6, (actionf_p1)A_StalkerMissile,      S_XSSD_DIVE1);
    // DIVE: sink back under -- drop shootable, then invisible, resume the underwater chase.
    ST (S_XSSD_DIVE1, SPR_XSSD,  0,  4, (actionf_p1)A_SerpentUnShootable, S_XSSD_DIVE2);
    ST (S_XSSD_DIVE2, SPR_XSSD,  3,  4, NULL,                            S_XSSD_DIVE3);
    ST (S_XSSD_DIVE3, SPR_XSSD,  6,  3, NULL,                            S_XSSD_DIVE4);
    ST (S_XSSD_DIVE4, SPR_XSSD,  9,  3, (actionf_p1)A_SerpentHide,       S_XSSP_SEE1);
    // WALK: kept valid (unused by the new ritual) -- fall back to the chase.
    ST (S_XSSP_WALK1, SPR_XSSP,  8,  5, (actionf_p1)A_Chase,        S_XSSP_WALK2);
    ST (S_XSSP_WALK2, SPR_XSSP,  9,  5, (actionf_p1)A_Chase,        S_XSSP_WALK3);
    ST (S_XSSP_WALK3, SPR_XSSP,  8,  5, (actionf_p1)A_Chase,        S_XSSP_WALK4);
    ST (S_XSSP_WALK4, SPR_XSSP,  9,  5, (actionf_p1)A_Chase,        S_XSSP_SEE1);
    // PAIN: only reachable while surfaced/shootable; flinch, then re-attack.
    ST (S_XSSP_PAIN1, SPR_XSSP, 11,  4, NULL,                       S_XSSP_PAIN2);
    ST (S_XSSP_PAIN2, SPR_XSSP, 11,  4, (actionf_p1)A_Pain,         S_XSSP_ATK1);
    ST (S_XSSP_DIE1,  SPR_XSSP, 14,  4, NULL,                       S_XSSP_DIE2);
    ST (S_XSSP_DIE2,  SPR_XSSP, 15,  4, (actionf_p1)A_Scream,       S_XSSP_DIE3);
    ST (S_XSSP_DIE3,  SPR_XSSP, 16,  4, (actionf_p1)A_Fall,         S_XSSP_DIE4);
    ST (S_XSSP_DIE4,  SPR_XSSP, 17,  4, NULL,                       S_XSSP_DIE5);
    ST (S_XSSP_DIE5,  SPR_XSSP, 18,  4, NULL,                       S_XSSP_DIE6);
    ST (S_XSSP_DIE6,  SPR_XSSP, 19,  4, NULL,                       S_XSSP_DIE7);
    ST (S_XSSP_DIE7,  SPR_XSSP, 20,  4, NULL,                       S_XSSP_DIE8);
    ST (S_XSSP_DIE8,  SPR_XSSP, 21,  4, NULL,                       S_XSSP_DIE9);
    ST (S_XSSP_DIE9,  SPR_XSSP, 22,  4, NULL,                       S_XSSP_DIE10);
    ST (S_XSSP_DIE10, SPR_XSSP, 23,  4, NULL,                       S_XSSP_DIE11);
    ST (S_XSSP_DIE11, SPR_XSSP, 24,  4, NULL,                       S_XSSP_DIE12);
    ST (S_XSSP_DIE12, SPR_XSSP, 25, -1, NULL,                       S_NULL);	// corpse

    // Stalker spit (crispy S_SERPENT_FX*/FX_X*).
    ST (S_XSSF_MOVE1, SPR_XSSF, 32768, 3, NULL,                     S_XSSF_MOVE2);
    ST (S_XSSF_MOVE2, SPR_XSSF, 32769, 3, NULL,                     S_XSSF_MOVE3);
    ST (S_XSSF_MOVE3, SPR_XSSF, 32768, 3, NULL,                     S_XSSF_MOVE4);
    ST (S_XSSF_MOVE4, SPR_XSSF, 32769, 3, NULL,                     S_XSSF_MOVE1);
    ST (S_XSSF_BOOM1, SPR_XSSF, 32770, 4, NULL,                     S_XSSF_BOOM2);
    ST (S_XSSF_BOOM2, SPR_XSSF, 32771, 4, NULL,                     S_XSSF_BOOM3);
    ST (S_XSSF_BOOM3, SPR_XSSF, 32772, 4, NULL,                     S_XSSF_BOOM4);
    ST (S_XSSF_BOOM4, SPR_XSSF, 32773, 4, NULL,                     S_XSSF_BOOM5);
    ST (S_XSSF_BOOM5, SPR_XSSF, 32774, 4, NULL,                     S_XSSF_BOOM6);
    ST (S_XSSF_BOOM6, SPR_XSSF, 32775, 4, NULL,                     S_NULL);

    m = &mobjinfo[MT_XSTALKER];
    m->doomednum = -1;        m->spawnstate  = S_XSSP_LOOK1; m->spawnhealth = 90;
    m->seestate  = S_XSSP_SEE1; m->seesound  = sfx_x_stsit; m->reactiontime = 8;
    m->attacksound = sfx_x_statk; m->painstate = S_XSSP_PAIN1; m->painchance = 96;
    // Melee-only: A_Chase surfaces it (-> SURF) ONLY in melee range (no missilestate).
    m->painsound = sfx_x_stpai;   m->meleestate = S_XSSP_SURF1; m->missilestate = S_NULL;
    m->deathstate = S_XSSP_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_stdth;
    m->speed = 12; m->radius = 32*FRACUNIT; m->height = 70*FRACUNIT; m->mass = 0x7fffffff;
    m->damage = 0; m->activesound = sfx_None;	// crispy serpent: immovable (INT_MAX mass), no active sound
    // Starts submerged: NOT shootable + invisible.  Surfaces to attack (see the ritual states).
    m->flags = MF_SOLID|MF_COUNTKILL; m->flags2 = MF2_DONTDRAW; m->raisestate = S_NULL;

    // ---- Stalker boss / Serpent Leader (crispy MT_SERPENTLEADER): same XSSP
    //      states, tougher, prefers the ranged spit.  Liquid-only like the base
    //      stalker.  Reuses every serpent state (incl. the full 12-frame death). ----
    m = &mobjinfo[MT_XSTALKERBOSS];
    m->doomednum = -1;        m->spawnstate  = S_XSSP_LOOK1; m->spawnhealth = 250;
    m->seestate  = S_XSSP_SEE1; m->seesound  = sfx_x_stsit; m->reactiontime = 8;
    m->attacksound = sfx_x_statk; m->painstate = S_XSSP_PAIN1; m->painchance = 64;
    // Leader also surfaces at range to SPIT (missilestate set) -- A_SerpentChooseAttack spits.
    m->painsound = sfx_x_stpai;   m->meleestate = S_XSSP_SURF1; m->missilestate = S_XSSP_SURF1;
    m->deathstate = S_XSSP_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_stdth;
    m->speed = 12; m->radius = 32*FRACUNIT; m->height = 70*FRACUNIT; m->mass = 0x7fffffff;
    m->damage = 0; m->activesound = sfx_None;	// crispy serpent leader: immovable mass
    m->flags = MF_SOLID|MF_COUNTKILL; m->flags2 = MF2_DONTDRAW; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XSTALKER_FX];
    m->doomednum = -1;        m->spawnstate  = S_XSSF_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XSSF_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_x_sthit;
    m->speed = 15*FRACUNIT; m->radius = 8*FRACUNIT; m->height = 10*FRACUNIT; m->mass = 100;
    m->damage = 4; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- Death Wyvern / Dragon (crispy S_DRAGON_*; the take-off flight ritual,
    //      A_DragonFlight steering and crash sequence are simplified to a plain
    //      flying boss that lobs a straight fireball).  Sprite XDRA (0-3 flight,
    //      4 attack, 5 pain, 6-12 die).  Big, high HP. ----
    ST (S_XDRA_LOOK1, SPR_XDRA,  3, 10, (actionf_p1)A_Look,         S_XDRA_LOOK2);
    ST (S_XDRA_LOOK2, SPR_XDRA,  3, 10, (actionf_p1)A_Look,         S_XDRA_LOOK1);
    ST (S_XDRA_WALK1, SPR_XDRA,  0,  3, (actionf_p1)A_Chase,        S_XDRA_WALK2);
    ST (S_XDRA_WALK2, SPR_XDRA,  1,  3, (actionf_p1)A_Chase,        S_XDRA_WALK3);
    ST (S_XDRA_WALK3, SPR_XDRA,  2,  3, (actionf_p1)A_Chase,        S_XDRA_WALK4);
    ST (S_XDRA_WALK4, SPR_XDRA,  3,  3, (actionf_p1)A_Chase,        S_XDRA_WALK1);
    ST (S_XDRA_ATK1,  SPR_XDRA,  4,  8, (actionf_p1)A_DragonAttack, S_XDRA_WALK1);
    ST (S_XDRA_PAIN1, SPR_XDRA,  5, 10, (actionf_p1)A_Pain,         S_XDRA_WALK1);
    ST (S_XDRA_DIE1,  SPR_XDRA,  6,  5, (actionf_p1)A_Scream,       S_XDRA_DIE2);
    ST (S_XDRA_DIE2,  SPR_XDRA,  7,  4, (actionf_p1)A_Fall,         S_XDRA_DIE3);
    ST (S_XDRA_DIE3,  SPR_XDRA,  8,  4, NULL,                       S_XDRA_DIE4);
    ST (S_XDRA_DIE4,  SPR_XDRA,  9,  4, NULL,                       S_XDRA_DIE5);
    ST (S_XDRA_DIE5,  SPR_XDRA, 10, -1, NULL,                       S_NULL);

    // Dragon fireball (crispy S_DRAGON_FX1_*).
    ST (S_XDRF_MOVE1, SPR_XDRF, 32768, 4, NULL,                     S_XDRF_MOVE2);
    ST (S_XDRF_MOVE2, SPR_XDRF, 32769, 4, NULL,                     S_XDRF_MOVE3);
    ST (S_XDRF_MOVE3, SPR_XDRF, 32770, 4, NULL,                     S_XDRF_MOVE4);
    ST (S_XDRF_MOVE4, SPR_XDRF, 32771, 4, NULL,                     S_XDRF_MOVE5);
    ST (S_XDRF_MOVE5, SPR_XDRF, 32772, 4, NULL,                     S_XDRF_MOVE6);
    ST (S_XDRF_MOVE6, SPR_XDRF, 32773, 4, NULL,                     S_XDRF_MOVE1);
    ST (S_XDRF_BOOM1, SPR_XDRF, 32774, 4, NULL,                     S_XDRF_BOOM2);
    ST (S_XDRF_BOOM2, SPR_XDRF, 32775, 4, NULL,                     S_XDRF_BOOM3);
    ST (S_XDRF_BOOM3, SPR_XDRF, 32776, 4, NULL,                     S_XDRF_BOOM4);
    ST (S_XDRF_BOOM4, SPR_XDRF, 32777, 4, NULL,                     S_XDRF_BOOM5);
    ST (S_XDRF_BOOM5, SPR_XDRF, 32778, 3, NULL,                     S_XDRF_BOOM6);
    ST (S_XDRF_BOOM6, SPR_XDRF, 32779, 3, NULL,                     S_NULL);

    m = &mobjinfo[MT_XDRAGON];
    m->doomednum = -1;        m->spawnstate  = S_XDRA_LOOK1; m->spawnhealth = 640;
    m->seestate  = S_XDRA_WALK1; m->seesound  = sfx_x_drsit; m->reactiontime = 8;
    m->attacksound = sfx_x_dratk; m->painstate = S_XDRA_PAIN1; m->painchance = 128;
    m->painsound = sfx_x_drpai;   m->meleestate = S_NULL;     m->missilestate = S_XDRA_ATK1;
    m->deathstate = S_XDRA_DIE1;  m->xdeathstate = S_NULL;    m->deathsound = sfx_x_drdth;
    m->speed = 10; m->radius = 20*FRACUNIT; m->height = 65*FRACUNIT; m->mass = 0x7fffffff;	// crispy dragon: immovable (INT_MAX)
    m->damage = 0; m->activesound = sfx_x_drsit;
    m->flags = MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL|MF_FLOAT|MF_NOGRAVITY|MF_NOBLOOD; m->raisestate = S_NULL;

    m = &mobjinfo[MT_XDRAGON_FX];
    m->doomednum = -1;        m->spawnstate  = S_XDRF_MOVE1; m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_XDRF_BOOM1; m->xdeathstate = S_NULL; m->deathsound = sfx_x_drhit;
    m->speed = 24*FRACUNIT; m->radius = 12*FRACUNIT; m->height = 10*FRACUNIT; m->mass = 100;
    m->damage = 6; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF|MF_NOGRAVITY; m->raisestate = S_NULL;

    // ---- Poison cloud (crispy S_POISONCLOUD*; the Flechette's lingering gas).
    //      18 damage-tick frames looping via A_PoisonBagCheck until the lifetime
    //      (reactiontime) runs out, then a 4-frame fade.  Frames use PSBG 3-8. ----
    ST (S_XPCL1,   SPR_PSBG, 3, 1, NULL,                       S_XPCL2);
    ST (S_XPCL2,   SPR_PSBG, 3, 1, (actionf_p1)A_Scream,       S_XPCL3);	// gas-burst hiss (deathsound)
    ST (S_XPCL3,   SPR_PSBG, 3, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL4);
    ST (S_XPCL4,   SPR_PSBG, 4, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL5);
    ST (S_XPCL5,   SPR_PSBG, 4, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL6);
    ST (S_XPCL6,   SPR_PSBG, 4, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL7);
    ST (S_XPCL7,   SPR_PSBG, 5, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL8);
    ST (S_XPCL8,   SPR_PSBG, 5, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL9);
    ST (S_XPCL9,   SPR_PSBG, 5, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL10);
    ST (S_XPCL10,  SPR_PSBG, 6, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL11);
    ST (S_XPCL11,  SPR_PSBG, 6, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL12);
    ST (S_XPCL12,  SPR_PSBG, 6, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL13);
    ST (S_XPCL13,  SPR_PSBG, 7, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL14);
    ST (S_XPCL14,  SPR_PSBG, 7, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL15);
    ST (S_XPCL15,  SPR_PSBG, 7, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL16);
    ST (S_XPCL16,  SPR_PSBG, 8, 2, (actionf_p1)A_PoisonBagDamage, S_XPCL17);
    ST (S_XPCL17,  SPR_PSBG, 8, 1, (actionf_p1)A_PoisonBagDamage, S_XPCL18);
    ST (S_XPCL18,  SPR_PSBG, 8, 1, (actionf_p1)A_PoisonBagCheck,  S_XPCL4);
    ST (S_XPCL_X1, SPR_PSBG, 7, 7, NULL,                       S_XPCL_X2);
    ST (S_XPCL_X2, SPR_PSBG, 6, 7, NULL,                       S_XPCL_X3);
    ST (S_XPCL_X3, SPR_PSBG, 5, 6, NULL,                       S_XPCL_X4);
    ST (S_XPCL_X4, SPR_PSBG, 3, 6, NULL,                       S_NULL);

    // MT_XPOISONCLOUD (crispy MT_POISONCLOUD): inert floating gas, huge mass so it's
    // never shoved, passes through walls (MF_NOCLIP) and off the blockmap.  reactiontime
    // is the lifetime in A_PoisonBagCheck cycles (~27 cycles ~= 20s, crispy 24 + rnd&7).
    m = &mobjinfo[MT_XPOISONCLOUD];
    m->doomednum = -1;        m->spawnstate  = S_XPCL1;   m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 27;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_NULL;      m->xdeathstate = S_NULL;  m->deathsound = sfx_x_psdth;	// A_Scream burst
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 30*FRACUNIT; m->mass = 0x7fffffff;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOGRAVITY|MF_NOBLOCKMAP|MF_SHADOW|MF_NOCLIP|MF_DROPOFF; m->raisestate = S_NULL;

    // ---- Flechette poison bag (crispy S_POISONBAG*/MT_POISONBAG): sits where it's
    //      thrown for a short fuse (frames PSBG A/B fullbright, then C), then
    //      A_PoisonBagInit pops the cloud above it and the bag vanishes (-> S_NULL). ----
    ST (S_XPBAG1, SPR_PSBG, 32768, 18, NULL,                        S_XPBAG2);
    ST (S_XPBAG2, SPR_PSBG, 32769,  4, NULL,                        S_XPBAG3);
    ST (S_XPBAG3, SPR_PSBG, 2,      3, NULL,                        S_XPBAG4);
    ST (S_XPBAG4, SPR_PSBG, 2,      1, (actionf_p1)A_PoisonBagInit, S_NULL);

    m = &mobjinfo[MT_XPOISONBAG];
    m->doomednum = -1;        m->spawnstate  = S_XPBAG1;  m->spawnhealth = 1000;
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_NULL;    m->painchance = 0;
    m->painsound = sfx_None;     m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate = S_NULL;      m->xdeathstate = S_NULL;  m->deathsound = sfx_None;
    m->speed = 0; m->radius = 5*FRACUNIT; m->height = 5*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOGRAVITY|MF_NOBLOCKMAP; m->raisestate = S_NULL;

    // ---- Poison shroom (crispy S_ZPOISONSHROOM*/MT_ZPOISONSHROOM): a shootable plant
    //      that idles with a slow pulse; when destroyed its death sequence pops a poison
    //      cloud via A_PoisonBagInit.  Frames SHRM A(idle) B(pulse) C-E(burst) F(corpse). ----
    ST (S_XSHRM1,   SPR_SHRM, 0,  5, (actionf_p1)A_PoisonShroom,  S_XSHRM_P2);
    ST (S_XSHRM_P1, SPR_SHRM, 0,  6, NULL,                        S_XSHRM_P2);	// pain flinch
    ST (S_XSHRM_P2, SPR_SHRM, 1,  8, (actionf_p1)A_Pain,          S_XSHRM1);
    ST (S_XSHRM_X1, SPR_SHRM, 2,  5, NULL,                        S_XSHRM_X2);	// death burst
    ST (S_XSHRM_X2, SPR_SHRM, 3,  5, NULL,                        S_XSHRM_X3);
    ST (S_XSHRM_X3, SPR_SHRM, 4,  5, (actionf_p1)A_PoisonBagInit, S_XSHRM_X4);
    ST (S_XSHRM_X4, SPR_SHRM, 5, -1, NULL,                        S_NULL);		// broken corpse

    m = &mobjinfo[MT_XPOISONSHROOM];
    m->doomednum = -1;        m->spawnstate  = S_XSHRM1;  m->spawnhealth = 30;	// crispy ednum 8104
    m->seestate  = S_NULL;       m->seesound  = sfx_None;  m->reactiontime = 8;
    m->attacksound = sfx_None;   m->painstate = S_XSHRM_P1; m->painchance = 255;
    m->painsound = sfx_x_pspai;  m->meleestate = S_NULL;   m->missilestate = S_NULL;	// A_Pain pulse/flinch
    m->deathstate = S_XSHRM_X1;  m->xdeathstate = S_NULL;  m->deathsound = sfx_x_psdth;	// burst via the spawned cloud
    m->speed = 0; m->radius = 6*FRACUNIT; m->height = 20*FRACUNIT; m->mass = 0x7fffffff;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SHOOTABLE|MF_SOLID|MF_NOBLOOD; m->raisestate = S_NULL;
}

// ---------------------------------------------------------------------------
// Spawn helpers (console "summon" + director)
// ---------------------------------------------------------------------------
// hexenstuff.wad's renamed sprites loaded?  Test the PARSED sprite, not a lump name.
int Hexen_Available (void)
{
    return numsprites > SPR_XETT && sprites[SPR_XETT].numframes > 0;
}

int Hexen_TypeByName (const char* name)
{
    if (!name || !name[0]) return -1;
    if (!strcmp (name, "ettin")) return MT_XETTIN;
    if (!strcmp (name, "centaur")) return MT_XCENTAUR;
    if (!strcmp (name, "slaughtaur") || !strcmp (name, "centaurleader")) return MT_XSLAUGHTAUR;
    // "demon" stays the DOOM pinky (resolved earlier in C_MobjByName); use "serpent".
    // NOTE gzdoom names the Chaos Serpent "Demon1" and reserves "Serpent" for the
    // liquid ambusher we call "stalker" -- both gzdoom spellings resolve too.
    if (!strcmp (name, "serpent") || !strcmp (name, "chaosserpent")
	|| !strcmp (name, "demon1")) return MT_XDEMON;
    if (!strcmp (name, "afrit") || !strcmp (name, "firedemon")) return MT_XFIREDEMON;
    if (!strcmp (name, "reiver") || !strcmp (name, "wraith")) return MT_XWRAITH;
    if (!strcmp (name, "bishop") || !strcmp (name, "darkbishop")) return MT_XBISHOP;
    if (!strcmp (name, "wendigo") || !strcmp (name, "iceguy")) return MT_XICEGUY;
    if (!strcmp (name, "stalkerboss") || !strcmp (name, "serpentleader")) return MT_XSTALKERBOSS;
    if (!strcmp (name, "stalker")) return MT_XSTALKER;
    if (!strcmp (name, "wyvern") || !strcmp (name, "dragon") || !strcmp (name, "deathwyvern")) return MT_XDRAGON;
    // (X) wave-2 monsters/bosses (files/hexen_mon.c): korax, heresiarch, demon2, wraithb
    { int t = Hexen_Mon_TypeByName (name); if (t >= 0) return t; }
    // (X) items/pickups/puzzle pieces (files/hexen_items.c)
    { int t = Hexen_ItemTypeByName (name); if (t >= 0) return t; }
    // (X) decorations / scenery (files/hexen_deco.c)
    if (!strcmp (name, "barrel")) return MT_XZBARREL;
    if (!strcmp (name, "bucket")) return MT_XZBUCKET;
    if (!strcmp (name, "cauldron")) return MT_XZCAULDRON;
    if (!strcmp (name, "firebull")) return MT_XZFIREBULL;
    if (!strcmp (name, "brasstorch")) return MT_XZBRASSTORCH;
    if (!strcmp (name, "firething")) return MT_XZFIRETHING;
    if (!strcmp (name, "walltorch")) return MT_XZWALLTORCH;
    if (!strcmp (name, "twinedtorch")) return MT_XZTWINEDTORCH;
    if (!strcmp (name, "bluecandle")) return MT_XZBLUECANDLE;
    if (!strcmp (name, "candle")) return MT_XZCANDLE;
    if (!strcmp (name, "chandelier")) return MT_XZCHANDELIER;
    if (!strcmp (name, "ironmaiden")) return MT_XZIRONMAIDEN;
    if (!strcmp (name, "bell")) return MT_XZBELL;
    if (!strcmp (name, "xmastree")) return MT_XZXMASTREE;
    if (!strcmp (name, "suitofarmor") || !strcmp (name, "armor")) return MT_XZSUITOFARMOR;
    if (!strcmp (name, "shrub")) return MT_XZSHRUB1;
    if (!strcmp (name, "shrub2")) return MT_XZSHRUB2;
    if (!strcmp (name, "treedestructible") || !strcmp (name, "bustabletree")) return MT_XZTREEDESTRUCTIBLE;
    if (!strcmp (name, "pottery")) return MT_XZPOTTERY1;
    if (!strcmp (name, "pottery2")) return MT_XZPOTTERY2;
    if (!strcmp (name, "pottery3")) return MT_XZPOTTERY3;
    if (!strcmp (name, "wingedstatue")) return MT_XZWINGEDSTATUE;
    if (!strcmp (name, "bloodpool")) return MT_XZBLOODPOOL;
    if (!strcmp (name, "leafspawner")) return MT_XZLEAFSPAWNER;
    if (!strcmp (name, "telesmoke")) return MT_XZTELESMOKE;
    if (!strcmp (name, "flamesmall")) return MT_XZFLAMESMALL;
    if (!strcmp (name, "flamelarge")) return MT_XZFLAMELARGE;
    if (!strcmp (name, "corpsesitting")) return MT_XZCORPSESITTING;
    if (!strcmp (name, "lynchedcorpse")) return MT_XZLYNCHEDNOHEART;
    if (!strcmp (name, "bridge")) return MT_XZBRIDGE;
    if (!strcmp (name, "statue")) return MT_XZGARGGREENTALL;
    if (!strcmp (name, "tree")) return MT_XZTREE;
    if (!strcmp (name, "log")) return MT_XZLOG;
    if (!strcmp (name, "stalagmite")) return MT_XZSTALAGMITELARGE;
    if (!strcmp (name, "stalactite")) return MT_XZSTALACTITELARGE;
    if (!strcmp (name, "mushroom")) return MT_XZSHROOMLARGE1;
    if (!strcmp (name, "vase")) return MT_XZVASEPILLAR;
    return -1;
}

// The poison feature's art (PSBG/SHRM) ships separately from the hexenstuff monster
// sprites (e.g. in FRANK.wad), so its spawnables resolve on their OWN sprites -- not the
// SPR_XETT gate in Hexen_Available -- else `summon poisoncloud`/`poisonshroom` would be
// blocked whenever the monster pack isn't loaded.  Returns -1 if the name or art is absent.
int Hexen_PoisonTypeByName (const char* name)
{
    int have_psbg = numsprites > SPR_PSBG && sprites[SPR_PSBG].numframes > 0;
    int have_shrm = numsprites > SPR_SHRM && sprites[SPR_SHRM].numframes > 0;

    if (!name || !name[0])
	return -1;
    if (have_psbg && (!strcmp (name, "poisoncloud") || !strcmp (name, "poison")))
	return MT_XPOISONCLOUD;
    if (have_psbg && (!strcmp (name, "poisonbag") || !strcmp (name, "flechettebag")))
	return MT_XPOISONBAG;
    if (have_shrm && (!strcmp (name, "poisonshroom") || !strcmp (name, "shroom")
		      || !strcmp (name, "mushroom")))
	return MT_XPOISONSHROOM;
    return -1;
}

mobj_t* Hexen_Spawn (int type, fixed_t x, fixed_t y)
{
    if (!Hexen_Available () || type < 0)
	return NULL;
    return P_SpawnMonsterChecked (x, y, type);
}
