// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(H) The full Heretic player WEAPON set in the DOOM engine (PL1 / un-Tomed).
//
//	Ported 1:1 from crispy-doom src/heretic/{p_pspr.c,info.c}.  The psprite states,
//	projectile/puff mobjs and firing code pointers are appended to the engine
//	tables at runtime in Heretic_Weapons_Init (heretic_mode only), overwriting the
//	DOOM weaponinfo[] slots.  Slot map (DOOM slot <- Heretic weapon, ammo pool):
//	  wp_fist    <- Staff              (am_noammo)   wp_shotgun <- Crossbow  (am_shell)
//	  wp_pistol  <- Gold Wand          (am_clip)     wp_chaingun<- DragonClaw(am_cell)
//	  wp_chainsaw<- Gauntlets          (am_noammo)   wp_missile <- Hellstaff (am_misl)
//	                                                 wp_plasma  <- Phoenix   (am_fuel)
//	                                                 wp_bfg     <- Firemace  (am_mace)
//	The player starts owning fist+pistol (=> Staff+Gold Wand); the rest are picked
//	up on Heretic maps (heretic_items.c ground pickups -> P_GiveWeapon).
//
//	SCOPE: PL1 only -- this engine has a single weaponinfo[] table and no
//	Tome-of-Power / pw_weaponlevel2 dispatch, so the crispy PL2 ("powered") variants
//	are unreachable and intentionally omitted.  This engine also lacks Heretic's
//	MF2_* physics (floorbounce/windthrust/lowgrav) and MF_TRANSLUCENT, so the
//	projectiles are straight-flying opaque NOGRAVITY missiles (the Firemace ball
//	doesn't floor-bounce; no lob variant).  Fire/hit SOUNDS use the native Heretic
//	lumps registered by Sounds_HWeapons_Init (sfx_hw_*, sounds_heretic.c).
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "info.h"
#include "m_fixed.h"
#include "tables.h"		// angle_t, ANG*
#include "m_random.h"		// P_Random
#include "sounds.h"
#include "doomstat.h"		// players[], consoleplayer
#include "d_player.h"		// player_t, pspdef_t
#include "d_items.h"		// weaponinfo_t, weaponinfo[]
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_local.h"		// MELEERANGE, MISSILERANGE, linetarget, P_LineAttack, P_SpawnMobj
#include "s_sound.h"		// S_StartSound
#include "r_main.h"		// R_PointToAngle2
#include "heretic_weapons.h"

#define BRIGHT		32768		// FF_FULLBRIGHT frame bit

extern state_t   *states;
extern mobjinfo_t *mobjinfo;
extern weaponinfo_t weaponinfo[];

// --- engine pieces we call (declared by hand, like heretic.c / heretic_items.c) ---
// (linetarget, P_AimLineAttack, P_LineAttack, MELEERANGE, MISSILERANGE come from p_local.h)
extern fixed_t		bulletslope;		// set by P_BulletSlope
extern mobjtype_t	PuffType;		// which puff the next P_SpawnPuff spawns
extern void		P_BulletSlope (mobj_t* mo);
extern int		heretic_mode;

// Shared psprite code pointers (defined in p_pspr.c; not in p_pspr.h).
extern void		A_WeaponReady (player_t* player, pspdef_t* psp);
extern void		A_ReFire (player_t* player, pspdef_t* psp);
extern void		A_Lower (player_t* player, pspdef_t* psp);
extern void		A_Raise (player_t* player, pspdef_t* psp);
extern void		A_Light0 (player_t* player, pspdef_t* psp);

// More engine pieces for the projectile weapons (phase 2-4).
extern void		P_CheckMissileSpawn (mobj_t* th);	// p_mobj.c (void here)
extern void		A_Explode (mobj_t* thingy);		// p_enemy.c -- radius damage (phoenix)
extern int		autoaim;				// p_pspr.c
// (finecosine / finesine come from tables.h)

// crispy P_SubRandom(): signed [-255..255]
#define P_SubRandom()	(P_Random() - P_Random())
// crispy HITDICE(a): (1 + (rnd&7)) * a
#define HITDICE(a)	((1 + (P_Random() & 7)) * (a))
#define WEAPONTOP	(32*FRACUNIT)	// psprite top (p_pspr.c private) -- for the shake jitter

// Spawn a player missile, DOOM-auto-aimed like P_SpawnPlayerMissile, but RETURN the
// mobj and optionally override the horizontal launch angle (crispy P_SPMAngle).
// BuddyDoom's own P_SpawnPlayerMissile is void and has no angle override.
static mobj_t* H_SPMAngle (mobj_t* source, mobjtype_t type, angle_t angle, boolean useangle)
{
    mobj_t*	th;
    angle_t	an = source->angle;
    fixed_t	slope;

    if (!autoaim && source->player == &players[consoleplayer])
	slope = P_PlayerLookSlope (source);		// "shoot where you look"
    else
    {
	slope = P_AimLineAttack (source, an, 16*64*FRACUNIT);
	if (!linetarget) { an += 1<<26; slope = P_AimLineAttack (source, an, 16*64*FRACUNIT); }
	if (!linetarget) { an -= 2<<26; slope = P_AimLineAttack (source, an, 16*64*FRACUNIT); }
	if (!linetarget) { an = source->angle; slope = P_PlayerLookSlope (source); }
    }
    if (useangle) an = angle;

    th = P_SpawnMobj (source->x, source->y, source->z + 4*8*FRACUNIT, type);
    if (th->info->seesound) S_StartSound (th, th->info->seesound);
    th->target = source;
    th->angle  = an;
    th->momx   = FixedMul (th->info->speed, finecosine[an>>ANGLETOFINESHIFT]);
    th->momy   = FixedMul (th->info->speed, finesine  [an>>ANGLETOFINESHIFT]);
    th->momz   = FixedMul (th->info->speed, slope);
    P_CheckMissileSpawn (th);
    return th;
}
#define H_SpawnPlayerMissile(s,t)	H_SPMAngle ((s), (t), 0, false)


// ---------------------------------------------------------------------------
// FIRING CODE POINTERS  (DOOM 2-arg psprite signature: player_t*, pspdef_t*)
// ---------------------------------------------------------------------------

// crispy A_StaffAttackPL1: melee, 5 + rand&15 damage, ~1.4deg random spread,
// spawns MT_HWP_STAFFPUFF and turns to face whatever it hit.
void A_StaffAttackPL1 (player_t* player, pspdef_t* psp)
{
    angle_t	angle;
    int		damage;
    int		slope;

    damage = 5 + (P_Random() & 15);
    angle  = player->mo->angle;
    angle += P_SubRandom() << 18;
    slope  = P_AimLineAttack (player->mo, angle, MELEERANGE);
    PuffType = MT_HWP_STAFFPUFF;
    P_LineAttack (player->mo, angle, MELEERANGE, slope, damage);
    if (linetarget)
    {
	S_StartSound (player->mo, sfx_hw_stfhit);
	// turn to face target
	player->mo->angle = R_PointToAngle2 (player->mo->x, player->mo->y,
					     linetarget->x, linetarget->y);
    }
}

// crispy A_FireGoldWandPL1: hitscan, uses 1 ammo, 7 + rand&7 damage, spreads on
// refire (held fire), spawns MT_HWP_GWANDPUFF.
void A_FireGoldWandPL1 (player_t* player, pspdef_t* psp)
{
    mobj_t*	mo = player->mo;
    angle_t	angle;
    int		damage;

    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    P_BulletSlope (mo);
    damage = 7 + (P_Random() & 7);
    angle  = mo->angle;
    if (player->refire)
	angle += P_SubRandom() << 18;
    PuffType = MT_HWP_GWANDPUFF;
    P_LineAttack (mo, angle, MISSILERANGE, bulletslope, damage);
    S_StartSound (mo, sfx_hw_gldhit);
}

// ---- Phase 2-4 fire code pointers -----------------------------------------

// Crossbow (crispy A_FireCrossbowPL1): 1 straight bolt + 2 side bolts at +/-ANG45/10.
void A_FireCrossbowPL1 (player_t* player, pspdef_t* psp)
{
    mobj_t* mo = player->mo;
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_StartSound (mo, sfx_hw_bowsht);
    H_SpawnPlayerMissile (mo, MT_HWP_CBOWFX1);
    H_SPMAngle (mo, MT_HWP_CBOWFX3, mo->angle - (ANG45 / 10), true);
    H_SPMAngle (mo, MT_HWP_CBOWFX3, mo->angle + (ANG45 / 10), true);
}

// Dragon Claw / Blaster PL1 (crispy A_FireBlasterPL1): hitscan, HITDICE(4), spreads
// on refire, spawns MT_HWP_BLSRPUFF.
void A_FireBlasterPL1 (player_t* player, pspdef_t* psp)
{
    mobj_t* mo = player->mo;
    angle_t angle;
    int     damage;

    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    P_BulletSlope (mo);
    damage = HITDICE(4);
    angle  = mo->angle;
    if (player->refire)
	angle += P_SubRandom() << 18;
    PuffType = MT_HWP_BLSRPUFF;
    P_LineAttack (mo, angle, MISSILERANGE, bulletslope, damage);
    S_StartSound (mo, sfx_hw_blssht);
}

// Hellstaff / Skull Rod PL1 (crispy A_FireSkullRodPL1): one MT_HWP_HRODFX1 missile,
// random start frame.
void A_FireSkullRodPL1 (player_t* player, pspdef_t* psp)
{
    mobj_t* mo;
    if (player->ammo[weaponinfo[player->readyweapon].ammo] < 1)
	return;
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_StartSound (player->mo, sfx_hw_hrnsht);
    mo = H_SpawnPlayerMissile (player->mo, MT_HWP_HRODFX1);
    if (mo && P_Random() > 128)
	P_SetMobjState (mo, S_HWP_HRODFX1_2);
}

// Phoenix flame trail (crispy A_PhoenixPuff, simplified: no MF2 wind, puff sits).
void A_PhoenixPuff (mobj_t* actor)
{
    P_SpawnMobj (actor->x, actor->y, actor->z, MT_HWP_PHNXPUFF);
}

// Phoenix Rod PL1 (crispy A_FirePhoenixPL1): one MT_HWP_PHNXFX1 + backward recoil.
void A_FirePhoenixPL1 (player_t* player, pspdef_t* psp)
{
    angle_t angle;
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_StartSound (player->mo, sfx_hw_phosht);
    H_SpawnPlayerMissile (player->mo, MT_HWP_PHNXFX1);
    angle = (player->mo->angle + ANG180) >> ANGLETOFINESHIFT;
    player->mo->momx += FixedMul (4 * FRACUNIT, finecosine[angle]);
    player->mo->momy += FixedMul (4 * FRACUNIT, finesine  [angle]);
}

// Firemace PL1 (crispy A_FireMacePL1, SIMPLIFIED): fires one MT_HWP_MACEFX1 at a
// small random angle.  The vanilla floor-bouncing ball / lob variant needs Heretic's
// MF2_FLOORBOUNCE/LOGRAV physics (absent here), so the ball flies straight instead.
void A_FireMacePL1 (player_t* player, pspdef_t* psp)
{
    if (player->ammo[weaponinfo[player->readyweapon].ammo] < 1)
	return;
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_StartSound (player->mo, sfx_hw_lobsht);
    psp->sx = ((P_Random() & 3) - 2) * FRACUNIT;
    psp->sy = WEAPONTOP + (P_Random() & 3) * FRACUNIT;
    H_SPMAngle (player->mo, MT_HWP_MACEFX1,
		player->mo->angle + (((P_Random() & 7) - 4) << 24), true);
}

// Gauntlets PL1 (crispy A_GauntletAttack, PL1 path only): melee hitscan, HITDICE(2),
// MELEERANGE+1, MT_HWP_GAUNPUFF, flickers extralight, turns to face the target.
void A_GauntletAttack (player_t* player, pspdef_t* psp)
{
    angle_t angle;
    int     damage, slope, randVal;
    fixed_t dist;

    psp->sx = ((P_Random() & 3) - 2) * FRACUNIT;
    psp->sy = WEAPONTOP + (P_Random() & 3) * FRACUNIT;
    angle  = player->mo->angle;
    damage = HITDICE(2);
    dist   = MELEERANGE + 1;
    angle += P_SubRandom() << 18;
    PuffType = MT_HWP_GAUNPUFF;
    slope = P_AimLineAttack (player->mo, angle, dist);
    P_LineAttack (player->mo, angle, dist, slope, damage);
    if (!linetarget)
    {
	if (P_Random() > 64)
	    player->extralight = !player->extralight;
	S_StartSound (player->mo, sfx_hw_gntful);
	return;
    }
    randVal = P_Random();
    if (randVal < 64)       player->extralight = 0;
    else if (randVal < 160) player->extralight = 1;
    else                    player->extralight = 2;
    S_StartSound (player->mo, sfx_hw_gnthit);
    // turn to face the target (crispy's clamped turn)
    angle = R_PointToAngle2 (player->mo->x, player->mo->y, linetarget->x, linetarget->y);
    if (angle - player->mo->angle > ANG180)
    {
	if ((signed int)(angle - player->mo->angle) < -ANG90 / 20)
	    player->mo->angle = angle + ANG90 / 21;
	else
	    player->mo->angle -= ANG90 / 20;
    }
    else
    {
	if (angle - player->mo->angle > ANG90 / 20)
	    player->mo->angle = angle - ANG90 / 21;
	else
	    player->mo->angle += ANG90 / 20;
    }
    player->mo->flags |= MF_JUSTATTACKED;
}


// ---------------------------------------------------------------------------
// TABLE FILL HELPERS (identical style to heretic_items.c)
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

// A non-blocking, non-gravity hit-puff actor (crispy MT_STAFFPUFF/MT_GOLDWANDPUFF1;
// this engine has no MF_TRANSLUCENT, so they render opaque -- purely cosmetic).
static void Puff (mobjtype_t mt, statenum_t spawn)
{
    mobjinfo_t*	m = &mobjinfo[mt];
    m->doomednum   = -1;
    m->spawnstate  = spawn;  m->spawnhealth = 1000;
    m->seestate    = S_NULL; m->seesound    = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;   m->painchance = 0;
    m->painsound   = sfx_None; m->meleestate = S_NULL;  m->missilestate = S_NULL;
    m->deathstate  = S_NULL; m->xdeathstate = S_NULL;   m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP | MF_NOGRAVITY;
    m->raisestate = S_NULL;
}

// A straight-flying player MISSILE (crispy MT_CRBOWFX1 etc.).  BuddyDoom has no
// Heretic MF2_* physics (floorbounce/windthrust/lowgrav) or MF_TRANSLUCENT, so we
// use the DOOM-compatible flag subset: it flies via momx/y/z (NOGRAVITY), damages,
// and animates its death state on impact.
static void Proj (mobjtype_t mt, statenum_t spawn, statenum_t death,
		  int speed, int dmg, int radius, int height, int seesnd, int deathsnd)
{
    mobjinfo_t*	m = &mobjinfo[mt];
    m->doomednum   = -1;
    m->spawnstate  = spawn;  m->spawnhealth = 1000;
    m->seestate    = S_NULL; m->seesound    = seesnd;  m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;   m->painchance = 0;
    m->painsound   = sfx_None; m->meleestate = S_NULL;  m->missilestate = S_NULL;
    m->deathstate  = death;  m->xdeathstate = S_NULL;   m->deathsound = deathsnd;
    m->speed = speed*FRACUNIT; m->radius = radius*FRACUNIT; m->height = height*FRACUNIT;
    m->mass = 100; m->damage = dmg; m->activesound = sfx_None;
    m->flags = MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY;
    m->raisestate = S_NULL;
}


void Heretic_Weapons_Init (void)
{
    // ---- STAFF psprite (crispy S_STAFFREADY / _DOWN / _UP / _ATK1_*) ----------
    ST (S_HWP_STAFFREADY,  SPR_STFF, 0, 1, (actionf_p1)A_WeaponReady,   S_HWP_STAFFREADY);
    ST (S_HWP_STAFFDOWN,   SPR_STFF, 0, 1, (actionf_p1)A_Lower,         S_HWP_STAFFDOWN);
    ST (S_HWP_STAFFUP,     SPR_STFF, 0, 1, (actionf_p1)A_Raise,         S_HWP_STAFFUP);
    ST (S_HWP_STAFFATK1_1, SPR_STFF, 1, 6, NULL,                        S_HWP_STAFFATK1_2);
    ST (S_HWP_STAFFATK1_2, SPR_STFF, 2, 8, (actionf_p1)A_StaffAttackPL1,S_HWP_STAFFATK1_3);
    ST (S_HWP_STAFFATK1_3, SPR_STFF, 1, 8, (actionf_p1)A_ReFire,        S_HWP_STAFFREADY);

    // Staff PL1 puff (crispy S_STAFFPUFF1..4 on PUF3; first frame fullbright)
    ST (S_HWP_STAFFPUFF1,  SPR_PUF3, BRIGHT|0, 4, NULL, S_HWP_STAFFPUFF2);
    ST (S_HWP_STAFFPUFF2,  SPR_PUF3, 1,        4, NULL, S_HWP_STAFFPUFF3);
    ST (S_HWP_STAFFPUFF3,  SPR_PUF3, 2,        4, NULL, S_HWP_STAFFPUFF4);
    ST (S_HWP_STAFFPUFF4,  SPR_PUF3, 3,        4, NULL, S_NULL);

    // ---- GOLD WAND psprite (crispy S_GOLDWANDREADY / _DOWN / _UP / _ATK1_*) ----
    ST (S_HWP_GWANDREADY,  SPR_GWND, 0, 1, (actionf_p1)A_WeaponReady,     S_HWP_GWANDREADY);
    ST (S_HWP_GWANDDOWN,   SPR_GWND, 0, 1, (actionf_p1)A_Lower,           S_HWP_GWANDDOWN);
    ST (S_HWP_GWANDUP,     SPR_GWND, 0, 1, (actionf_p1)A_Raise,           S_HWP_GWANDUP);
    ST (S_HWP_GWANDATK1_1, SPR_GWND, 1, 3, NULL,                          S_HWP_GWANDATK1_2);
    ST (S_HWP_GWANDATK1_2, SPR_GWND, 2, 5, (actionf_p1)A_FireGoldWandPL1, S_HWP_GWANDATK1_3);
    ST (S_HWP_GWANDATK1_3, SPR_GWND, 3, 3, NULL,                          S_HWP_GWANDATK1_4);
    ST (S_HWP_GWANDATK1_4, SPR_GWND, 3, 0, (actionf_p1)A_ReFire,          S_HWP_GWANDREADY);

    // Gold wand PL1 puff (crispy S_GWANDPUFF1_1..5 on PUF2; all fullbright)
    ST (S_HWP_GWANDPUFF1,  SPR_PUF2, BRIGHT|0, 3, NULL, S_HWP_GWANDPUFF2);
    ST (S_HWP_GWANDPUFF2,  SPR_PUF2, BRIGHT|1, 3, NULL, S_HWP_GWANDPUFF3);
    ST (S_HWP_GWANDPUFF3,  SPR_PUF2, BRIGHT|2, 3, NULL, S_HWP_GWANDPUFF4);
    ST (S_HWP_GWANDPUFF4,  SPR_PUF2, BRIGHT|3, 3, NULL, S_HWP_GWANDPUFF5);
    ST (S_HWP_GWANDPUFF5,  SPR_PUF2, BRIGHT|4, 3, NULL, S_NULL);

    // ---- Puff actors ---------------------------------------------------------
    Puff (MT_HWP_STAFFPUFF, S_HWP_STAFFPUFF1);
    Puff (MT_HWP_GWANDPUFF, S_HWP_GWANDPUFF1);

    // =====================================================================
    //  PHASE 2-4 weapons.  Single ready frame (like staff/goldwand) for input
    //  responsiveness; atkstate = crispy's _1 state.  Sounds are wired in phase 5.
    // =====================================================================

    // ---- CROSSBOW (wp_shotgun) -- 1 straight bolt + 2 side bolts --------------
    ST (S_HWP_CBOWREADY1, SPR_CRBW, 0, 1, (actionf_p1)A_WeaponReady,      S_HWP_CBOWREADY1);
    ST (S_HWP_CBOWDOWN,   SPR_CRBW, 0, 1, (actionf_p1)A_Lower,            S_HWP_CBOWDOWN);
    ST (S_HWP_CBOWUP,     SPR_CRBW, 0, 1, (actionf_p1)A_Raise,            S_HWP_CBOWUP);
    ST (S_HWP_CBOWATK1,   SPR_CRBW, 3, 6, (actionf_p1)A_FireCrossbowPL1,  S_HWP_CBOWATK2);
    ST (S_HWP_CBOWATK2,   SPR_CRBW, 4, 3, NULL,                           S_HWP_CBOWATK3);
    ST (S_HWP_CBOWATK3,   SPR_CRBW, 5, 3, NULL,                           S_HWP_CBOWATK4);
    ST (S_HWP_CBOWATK4,   SPR_CRBW, 6, 3, NULL,                           S_HWP_CBOWATK5);
    ST (S_HWP_CBOWATK5,   SPR_CRBW, 7, 3, NULL,                           S_HWP_CBOWATK6);
    ST (S_HWP_CBOWATK6,   SPR_CRBW, 0, 4, NULL,                           S_HWP_CBOWATK7);
    ST (S_HWP_CBOWATK7,   SPR_CRBW, 1, 4, NULL,                           S_HWP_CBOWATK8);
    ST (S_HWP_CBOWATK8,   SPR_CRBW, 2, 5, (actionf_p1)A_ReFire,           S_HWP_CBOWREADY1);
    ST (S_HWP_CBOWFX1,    SPR_FX03, BRIGHT|1, 1, NULL, S_HWP_CBOWFX1);
    ST (S_HWP_CBOWFXI1_1, SPR_FX03, BRIGHT|7, 8, NULL, S_HWP_CBOWFXI1_2);
    ST (S_HWP_CBOWFXI1_2, SPR_FX03, BRIGHT|8, 8, NULL, S_HWP_CBOWFXI1_3);
    ST (S_HWP_CBOWFXI1_3, SPR_FX03, BRIGHT|9, 8, NULL, S_NULL);
    ST (S_HWP_CBOWFX3,    SPR_FX03, BRIGHT|0, 1, NULL, S_HWP_CBOWFX3);
    ST (S_HWP_CBOWFXI3_1, SPR_FX03, BRIGHT|2, 8, NULL, S_HWP_CBOWFXI3_2);
    ST (S_HWP_CBOWFXI3_2, SPR_FX03, BRIGHT|3, 8, NULL, S_HWP_CBOWFXI3_3);
    ST (S_HWP_CBOWFXI3_3, SPR_FX03, BRIGHT|4, 8, NULL, S_NULL);
    Proj (MT_HWP_CBOWFX1, S_HWP_CBOWFX1, S_HWP_CBOWFXI1_1, 30, 10, 11, 8, sfx_hw_bowsht, sfx_hw_hrnhit);
    Proj (MT_HWP_CBOWFX3, S_HWP_CBOWFX3, S_HWP_CBOWFXI3_1, 20,  2, 11, 8, sfx_None,      sfx_hw_hrnhit);

    // ---- DRAGON CLAW / BLASTER (wp_chaingun) -- hitscan ----------------------
    ST (S_HWP_BLSRREADY, SPR_BLSR, 0, 1, (actionf_p1)A_WeaponReady,     S_HWP_BLSRREADY);
    ST (S_HWP_BLSRDOWN,  SPR_BLSR, 0, 1, (actionf_p1)A_Lower,           S_HWP_BLSRDOWN);
    ST (S_HWP_BLSRUP,    SPR_BLSR, 0, 1, (actionf_p1)A_Raise,           S_HWP_BLSRUP);
    ST (S_HWP_BLSRATK1,  SPR_BLSR, 1, 3, NULL,                          S_HWP_BLSRATK2);
    ST (S_HWP_BLSRATK2,  SPR_BLSR, 2, 3, NULL,                          S_HWP_BLSRATK3);
    ST (S_HWP_BLSRATK3,  SPR_BLSR, 3, 2, (actionf_p1)A_FireBlasterPL1,  S_HWP_BLSRATK4);
    ST (S_HWP_BLSRATK4,  SPR_BLSR, 2, 2, NULL,                          S_HWP_BLSRATK5);
    ST (S_HWP_BLSRATK5,  SPR_BLSR, 1, 2, NULL,                          S_HWP_BLSRATK6);
    ST (S_HWP_BLSRATK6,  SPR_BLSR, 0, 0, (actionf_p1)A_ReFire,          S_HWP_BLSRREADY);
    ST (S_HWP_BLSRPUFF1, SPR_FX17, BRIGHT|0, 4, NULL, S_HWP_BLSRPUFF2);
    ST (S_HWP_BLSRPUFF2, SPR_FX17, BRIGHT|1, 4, NULL, S_HWP_BLSRPUFF3);
    ST (S_HWP_BLSRPUFF3, SPR_FX17, BRIGHT|2, 4, NULL, S_HWP_BLSRPUFF4);
    ST (S_HWP_BLSRPUFF4, SPR_FX17, BRIGHT|3, 4, NULL, S_HWP_BLSRPUFF5);
    ST (S_HWP_BLSRPUFF5, SPR_FX17, BRIGHT|4, 4, NULL, S_NULL);
    Puff (MT_HWP_BLSRPUFF, S_HWP_BLSRPUFF1);

    // ---- HELLSTAFF / SKULL ROD (wp_missile) -- rapid missiles -----------------
    ST (S_HWP_HRODREADY, SPR_HROD, 0, 1, (actionf_p1)A_WeaponReady,      S_HWP_HRODREADY);
    ST (S_HWP_HRODDOWN,  SPR_HROD, 0, 1, (actionf_p1)A_Lower,            S_HWP_HRODDOWN);
    ST (S_HWP_HRODUP,    SPR_HROD, 0, 1, (actionf_p1)A_Raise,            S_HWP_HRODUP);
    ST (S_HWP_HRODATK1,  SPR_HROD, 0, 4, (actionf_p1)A_FireSkullRodPL1,  S_HWP_HRODATK2);
    ST (S_HWP_HRODATK2,  SPR_HROD, 1, 4, (actionf_p1)A_FireSkullRodPL1,  S_HWP_HRODATK3);
    ST (S_HWP_HRODATK3,  SPR_HROD, 1, 0, (actionf_p1)A_ReFire,           S_HWP_HRODREADY);
    ST (S_HWP_HRODFX1_1,  SPR_FX00, BRIGHT|0,  6, NULL, S_HWP_HRODFX1_2);
    ST (S_HWP_HRODFX1_2,  SPR_FX00, BRIGHT|1,  6, NULL, S_HWP_HRODFX1_1);
    ST (S_HWP_HRODFXI1_1, SPR_FX00, BRIGHT|7,  5, NULL, S_HWP_HRODFXI1_2);
    ST (S_HWP_HRODFXI1_2, SPR_FX00, BRIGHT|8,  5, NULL, S_HWP_HRODFXI1_3);
    ST (S_HWP_HRODFXI1_3, SPR_FX00, BRIGHT|9,  4, NULL, S_HWP_HRODFXI1_4);
    ST (S_HWP_HRODFXI1_4, SPR_FX00, BRIGHT|10, 4, NULL, S_HWP_HRODFXI1_5);
    ST (S_HWP_HRODFXI1_5, SPR_FX00, BRIGHT|11, 3, NULL, S_HWP_HRODFXI1_6);
    ST (S_HWP_HRODFXI1_6, SPR_FX00, BRIGHT|12, 3, NULL, S_NULL);
    Proj (MT_HWP_HRODFX1, S_HWP_HRODFX1_1, S_HWP_HRODFXI1_1, 22, 3, 12, 8, sfx_hw_hrnsht, sfx_hw_hrnhit);

    // ---- PHOENIX ROD (wp_plasma) -- exploding missile + flame trail ----------
    ST (S_HWP_PHNXREADY, SPR_PHNX, 0, 1, (actionf_p1)A_WeaponReady,     S_HWP_PHNXREADY);
    ST (S_HWP_PHNXDOWN,  SPR_PHNX, 0, 1, (actionf_p1)A_Lower,           S_HWP_PHNXDOWN);
    ST (S_HWP_PHNXUP,    SPR_PHNX, 0, 1, (actionf_p1)A_Raise,           S_HWP_PHNXUP);
    ST (S_HWP_PHNXATK1,  SPR_PHNX, 1, 5, NULL,                          S_HWP_PHNXATK2);
    ST (S_HWP_PHNXATK2,  SPR_PHNX, 2, 7, (actionf_p1)A_FirePhoenixPL1,  S_HWP_PHNXATK3);
    ST (S_HWP_PHNXATK3,  SPR_PHNX, 3, 4, NULL,                          S_HWP_PHNXATK4);
    ST (S_HWP_PHNXATK4,  SPR_PHNX, 1, 4, NULL,                          S_HWP_PHNXATK5);
    ST (S_HWP_PHNXATK5,  SPR_PHNX, 1, 0, (actionf_p1)A_ReFire,          S_HWP_PHNXREADY);
    ST (S_HWP_PHNXFX1,   SPR_FX04, BRIGHT|0, 4, (actionf_p1)A_PhoenixPuff, S_HWP_PHNXFX1);
    ST (S_HWP_PHNXFXI1,  SPR_FX08, BRIGHT|0, 6, (actionf_p1)A_Explode,   S_HWP_PHNXFXI2);
    ST (S_HWP_PHNXFXI2,  SPR_FX08, BRIGHT|1, 5, NULL, S_HWP_PHNXFXI3);
    ST (S_HWP_PHNXFXI3,  SPR_FX08, BRIGHT|2, 5, NULL, S_HWP_PHNXFXI4);
    ST (S_HWP_PHNXFXI4,  SPR_FX08, BRIGHT|3, 4, NULL, S_HWP_PHNXFXI5);
    ST (S_HWP_PHNXFXI5,  SPR_FX08, BRIGHT|4, 4, NULL, S_HWP_PHNXFXI6);
    ST (S_HWP_PHNXFXI6,  SPR_FX08, BRIGHT|5, 4, NULL, S_HWP_PHNXFXI7);
    ST (S_HWP_PHNXFXI7,  SPR_FX08, BRIGHT|6, 4, NULL, S_HWP_PHNXFXI8);
    ST (S_HWP_PHNXFXI8,  SPR_FX08, BRIGHT|7, 4, NULL, S_NULL);
    ST (S_HWP_PHNXPUFF1, SPR_FX04, 1, 4, NULL, S_HWP_PHNXPUFF2);
    ST (S_HWP_PHNXPUFF2, SPR_FX04, 2, 4, NULL, S_HWP_PHNXPUFF3);
    ST (S_HWP_PHNXPUFF3, SPR_FX04, 3, 4, NULL, S_HWP_PHNXPUFF4);
    ST (S_HWP_PHNXPUFF4, SPR_FX04, 4, 4, NULL, S_HWP_PHNXPUFF5);
    ST (S_HWP_PHNXPUFF5, SPR_FX04, 5, 4, NULL, S_NULL);
    Proj (MT_HWP_PHNXFX1, S_HWP_PHNXFX1, S_HWP_PHNXFXI1, 20, 20, 11, 8, sfx_hw_phosht, sfx_hw_phohit);
    Puff (MT_HWP_PHNXPUFF, S_HWP_PHNXPUFF1);

    // ---- FIREMACE (wp_bfg) -- simplified: straight ball, no floor-bounce -------
    ST (S_HWP_MACEREADY, SPR_MACE, 0, 1, (actionf_p1)A_WeaponReady,     S_HWP_MACEREADY);
    ST (S_HWP_MACEDOWN,  SPR_MACE, 0, 1, (actionf_p1)A_Lower,           S_HWP_MACEDOWN);
    ST (S_HWP_MACEUP,    SPR_MACE, 0, 1, (actionf_p1)A_Raise,           S_HWP_MACEUP);
    ST (S_HWP_MACEATK1,  SPR_MACE, 1, 4, NULL,                          S_HWP_MACEATK2);
    ST (S_HWP_MACEATK2,  SPR_MACE, 2, 3, (actionf_p1)A_FireMacePL1,     S_HWP_MACEATK3);
    ST (S_HWP_MACEATK3,  SPR_MACE, 3, 3, (actionf_p1)A_FireMacePL1,     S_HWP_MACEATK4);
    ST (S_HWP_MACEATK4,  SPR_MACE, 4, 3, (actionf_p1)A_FireMacePL1,     S_HWP_MACEATK5);
    ST (S_HWP_MACEATK5,  SPR_MACE, 5, 3, (actionf_p1)A_FireMacePL1,     S_HWP_MACEATK6);
    ST (S_HWP_MACEATK6,  SPR_MACE, 2, 4, (actionf_p1)A_ReFire,          S_HWP_MACEATK7);
    ST (S_HWP_MACEATK7,  SPR_MACE, 3, 4, NULL,                          S_HWP_MACEATK8);
    ST (S_HWP_MACEATK8,  SPR_MACE, 4, 4, NULL,                          S_HWP_MACEATK9);
    ST (S_HWP_MACEATK9,  SPR_MACE, 5, 4, NULL,                          S_HWP_MACEATK10);
    ST (S_HWP_MACEATK10, SPR_MACE, 1, 4, NULL,                          S_HWP_MACEREADY);
    ST (S_HWP_MACEFX1_1,  SPR_FX02, 0, 4, NULL, S_HWP_MACEFX1_2);
    ST (S_HWP_MACEFX1_2,  SPR_FX02, 1, 4, NULL, S_HWP_MACEFX1_1);
    ST (S_HWP_MACEFXI1_1, SPR_FX02, BRIGHT|5, 4, NULL, S_HWP_MACEFXI1_2);
    ST (S_HWP_MACEFXI1_2, SPR_FX02, BRIGHT|6, 4, NULL, S_HWP_MACEFXI1_3);
    ST (S_HWP_MACEFXI1_3, SPR_FX02, BRIGHT|7, 4, NULL, S_HWP_MACEFXI1_4);
    ST (S_HWP_MACEFXI1_4, SPR_FX02, BRIGHT|8, 4, NULL, S_HWP_MACEFXI1_5);
    ST (S_HWP_MACEFXI1_5, SPR_FX02, BRIGHT|9, 4, NULL, S_NULL);
    Proj (MT_HWP_MACEFX1, S_HWP_MACEFX1_1, S_HWP_MACEFXI1_1, 20, 2, 8, 6, sfx_hw_lobsht, sfx_hw_lobhit);

    // ---- GAUNTLETS (wp_chainsaw) -- melee -------------------------------------
    ST (S_HWP_GAUNREADY, SPR_GAUN, 0, 1, (actionf_p1)A_WeaponReady,      S_HWP_GAUNREADY);
    ST (S_HWP_GAUNDOWN,  SPR_GAUN, 0, 1, (actionf_p1)A_Lower,            S_HWP_GAUNDOWN);
    ST (S_HWP_GAUNUP,    SPR_GAUN, 0, 1, (actionf_p1)A_Raise,            S_HWP_GAUNUP);
    ST (S_HWP_GAUNATK1,  SPR_GAUN, 1, 4, NULL,                           S_HWP_GAUNATK2);
    ST (S_HWP_GAUNATK2,  SPR_GAUN, 2, 4, NULL,                           S_HWP_GAUNATK3);
    ST (S_HWP_GAUNATK3,  SPR_GAUN, BRIGHT|3, 4, (actionf_p1)A_GauntletAttack, S_HWP_GAUNATK4);
    ST (S_HWP_GAUNATK4,  SPR_GAUN, BRIGHT|4, 4, (actionf_p1)A_GauntletAttack, S_HWP_GAUNATK5);
    ST (S_HWP_GAUNATK5,  SPR_GAUN, BRIGHT|5, 4, (actionf_p1)A_GauntletAttack, S_HWP_GAUNATK6);
    ST (S_HWP_GAUNATK6,  SPR_GAUN, 2, 4, (actionf_p1)A_ReFire,           S_HWP_GAUNATK7);
    ST (S_HWP_GAUNATK7,  SPR_GAUN, 1, 4, (actionf_p1)A_Light0,           S_HWP_GAUNREADY);
    ST (S_HWP_GAUNPUFF1, SPR_PUF1, BRIGHT|0, 4, NULL, S_HWP_GAUNPUFF2);
    ST (S_HWP_GAUNPUFF2, SPR_PUF1, BRIGHT|1, 4, NULL, S_HWP_GAUNPUFF3);
    ST (S_HWP_GAUNPUFF3, SPR_PUF1, BRIGHT|2, 4, NULL, S_HWP_GAUNPUFF4);
    ST (S_HWP_GAUNPUFF4, SPR_PUF1, BRIGHT|3, 4, NULL, S_NULL);
    Puff (MT_HWP_GAUNPUFF, S_HWP_GAUNPUFF1);

    // ---- Overwrite the player's slot-1 / slot-2 weapons (heretic_mode only) ---
    // The state/puff appends above are harmless in any game (they occupy the
    // enum slots we reserved); but the weaponinfo swap must NOT touch DOOM/Hexen,
    // or their fist + pistol would turn into the Staff + Gold Wand.
    if (!heretic_mode)
	return;

    // Staff -> wp_fist (slot 1), no ammo (crispy am_noammo).
    weaponinfo[wp_fist].ammo       = am_noammo;
    weaponinfo[wp_fist].upstate    = S_HWP_STAFFUP;
    weaponinfo[wp_fist].downstate  = S_HWP_STAFFDOWN;
    weaponinfo[wp_fist].readystate = S_HWP_STAFFREADY;
    weaponinfo[wp_fist].atkstate   = S_HWP_STAFFATK1_1;
    weaponinfo[wp_fist].flashstate = S_NULL;

    // Gold Wand -> wp_pistol (slot 2); crispy am_goldwand -> DOOM am_clip.
    weaponinfo[wp_pistol].ammo       = am_clip;
    weaponinfo[wp_pistol].upstate    = S_HWP_GWANDUP;
    weaponinfo[wp_pistol].downstate  = S_HWP_GWANDDOWN;
    weaponinfo[wp_pistol].readystate = S_HWP_GWANDREADY;
    weaponinfo[wp_pistol].atkstate   = S_HWP_GWANDATK1_1;
    weaponinfo[wp_pistol].flashstate = S_NULL;

    // Crossbow -> wp_shotgun (slot 3); am_crossbow -> DOOM am_shell.
    weaponinfo[wp_shotgun].ammo       = am_shell;
    weaponinfo[wp_shotgun].upstate    = S_HWP_CBOWUP;
    weaponinfo[wp_shotgun].downstate  = S_HWP_CBOWDOWN;
    weaponinfo[wp_shotgun].readystate = S_HWP_CBOWREADY1;
    weaponinfo[wp_shotgun].atkstate   = S_HWP_CBOWATK1;
    weaponinfo[wp_shotgun].flashstate = S_NULL;

    // Dragon Claw / Blaster -> wp_chaingun (slot 4); am_blaster -> DOOM am_cell.
    weaponinfo[wp_chaingun].ammo       = am_cell;
    weaponinfo[wp_chaingun].upstate    = S_HWP_BLSRUP;
    weaponinfo[wp_chaingun].downstate  = S_HWP_BLSRDOWN;
    weaponinfo[wp_chaingun].readystate = S_HWP_BLSRREADY;
    weaponinfo[wp_chaingun].atkstate   = S_HWP_BLSRATK1;
    weaponinfo[wp_chaingun].flashstate = S_NULL;

    // Hellstaff / Skull Rod -> wp_missile (slot 5); am_skullrod -> DOOM am_misl.
    weaponinfo[wp_missile].ammo       = am_misl;
    weaponinfo[wp_missile].upstate    = S_HWP_HRODUP;
    weaponinfo[wp_missile].downstate  = S_HWP_HRODDOWN;
    weaponinfo[wp_missile].readystate = S_HWP_HRODREADY;
    weaponinfo[wp_missile].atkstate   = S_HWP_HRODATK1;
    weaponinfo[wp_missile].flashstate = S_NULL;

    // Phoenix Rod -> wp_plasma (slot 6); am_phoenixrod -> DOOM am_fuel.
    weaponinfo[wp_plasma].ammo       = am_fuel;
    weaponinfo[wp_plasma].upstate    = S_HWP_PHNXUP;
    weaponinfo[wp_plasma].downstate  = S_HWP_PHNXDOWN;
    weaponinfo[wp_plasma].readystate = S_HWP_PHNXREADY;
    weaponinfo[wp_plasma].atkstate   = S_HWP_PHNXATK1;
    weaponinfo[wp_plasma].flashstate = S_NULL;

    // Firemace -> wp_bfg (slot 7); am_mace (the new 6th ammo pool).
    weaponinfo[wp_bfg].ammo       = am_mace;
    weaponinfo[wp_bfg].upstate    = S_HWP_MACEUP;
    weaponinfo[wp_bfg].downstate  = S_HWP_MACEDOWN;
    weaponinfo[wp_bfg].readystate = S_HWP_MACEREADY;
    weaponinfo[wp_bfg].atkstate   = S_HWP_MACEATK1;
    weaponinfo[wp_bfg].flashstate = S_NULL;

    // Gauntlets -> wp_chainsaw (slot 1 alt, toggles with the Staff); am_noammo.
    weaponinfo[wp_chainsaw].ammo       = am_noammo;
    weaponinfo[wp_chainsaw].upstate    = S_HWP_GAUNUP;
    weaponinfo[wp_chainsaw].downstate  = S_HWP_GAUNDOWN;
    weaponinfo[wp_chainsaw].readystate = S_HWP_GAUNREADY;
    weaponinfo[wp_chainsaw].atkstate   = S_HWP_GAUNATK1;
    weaponinfo[wp_chainsaw].flashstate = S_NULL;
}
