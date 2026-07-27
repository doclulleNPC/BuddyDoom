// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(H) Heretic player WEAPONS in the DOOM engine -- Phase 1: Staff + Gold Wand.
//
//	Ported 1:1 from crispy-doom src/heretic/{p_pspr.c,info.c}.  Two orthogonal
//	pieces, both appended to the engine tables at runtime (no info.c data edits
//	beyond the enum slots in info.h + the four sprite names in info.c):
//
//	  1. Weapon PSPRITES: the STFF/GWND view-model states, wired into
//	     weaponinfo[wp_fist] (Staff, slot 1) and weaponinfo[wp_pistol]
//	     (Gold Wand, slot 2).  The player already starts owning wp_fist+wp_pistol
//	     (g_game.c G_PlayerReborn), so the Heretic starting kit appears for free.
//	  2. The hit PUFFS the two weapons spawn (MT_HWP_STAFFPUFF using PUF3,
//	     MT_HWP_GWANDPUFF using PUF2), selected per-shot via the global PuffType
//	     (p_mobj.c) the way crispy's P_SpawnPuff does.
//
//	SCOPE (Phase 1): PL1 (un-Tomed) only -- this engine has a single weaponinfo[]
//	table and no Tome-of-Power / pw_weaponlevel2 dispatch, so the crispy PL2
//	("powered") variants are unreachable and intentionally omitted.  No firing
//	SOUND yet: BuddyDoom's Heretic sfx table carries only monster sounds, not the
//	weapon fire/hit lumps, so the weapons are silent on use for now (the psprite
//	swing + puff are the feedback).  Ammo: crispy am_goldwand maps to DOOM am_clip.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "info.h"
#include "m_fixed.h"
#include "tables.h"		// angle_t, ANG*
#include "m_random.h"		// P_Random
#include "sounds.h"
#include "d_player.h"		// player_t, pspdef_t
#include "d_items.h"		// weaponinfo_t, weaponinfo[]
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_local.h"		// MELEERANGE, MISSILERANGE, linetarget, P_LineAttack
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

// crispy P_SubRandom(): signed [-255..255]
#define P_SubRandom()	(P_Random() - P_Random())


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
}
