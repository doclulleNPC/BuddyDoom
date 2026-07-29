// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(S) The full Strife player WEAPON set in the DOOM engine (PL1 primaries).
//
//	Ported from strife-ve src/strife/{p_pspr.c,d_items.c,info.c}.  The psprite
//	states, weapon-projectile mobjs and firing code pointers are appended to the
//	engine tables at runtime in Strife_Weapons_Init (the caller gates on
//	strife_mode), overwriting the DOOM weaponinfo[] slots.
//
//	Slot map (DOOM slot <- Strife weapon, ammo pool).  DOOM has only 4 ammo
//	pools (am_clip/am_shell/am_cell/am_misl) for 6 ammo-using Strife weapons, so
//	some pools are shared -- exactly like real Strife shares am_cell between the
//	Flamethrower and Mauler:
//	  wp_fist    <- Punch Dagger        (am_noammo)
//	  wp_pistol  <- Assault Rifle        (am_clip)
//	  wp_shotgun <- Crossbow / e-bolts   (am_shell)
//	  wp_chaingun<- Mauler (single)      (am_cell)
//	  wp_missile <- Mini-Missile Launcher(am_misl)
//	  wp_plasma  <- Flamethrower         (am_cell)   [shares cell w/ Mauler]
//	  wp_bfg     <- Sigil                (am_noammo)
//	  wp_chainsaw<- Grenade Launcher (HE)(am_shell)  [shares shell w/ Crossbow]
//
//	SCOPE / approximations (this engine is the 1993 id C core; it lacks Strife's
//	MF2_* physics, MF_BOUNCE, MF_SPECTRAL, P_SpawnMortar, and the player_t
//	stamina/accuracy/sigiltype/pitch-recoil fields):
//	  * The Crossbow's poison-bolt mode, the WP (white-phosphorus) grenade, and
//	    the Mauler's Torpedo mode are secondary Strife weapons with no DOOM
//	    weaponinfo[] slot, so they are unreachable (like the Heretic PL2 variants
//	    in heretic_weapons.c).  Their psprite states + code pointers are still
//	    filled so the tables stay self-consistent.
//	  * Grenades fly straight (no MF_BOUNCE / lob / pitch arc) and detonate on
//	    impact via A_Explode; the WP grenade's phosphorus-fire spawn is dropped.
//	    // TODO: bouncing/arcing grenades need MF_BOUNCE + P_SpawnMortar.
//	  * The Sigil is reduced to a single attack (the type-4 "mega blast",
//	    MT_S_SIGIL_E_SHOT) because there is no player->sigiltype field.
//	    // TODO: the 5 Sigil charge levels need a player_t.sigiltype field.
//	  * Missile-trail / active-sound cosmetics (A_MissileSmoke/A_MissileTick/
//	    A_ActiveSound) are dropped to NULL; detonations use the engine A_Explode.
//	  * Strife's per-shot accuracy spread and d_recoil pitch kick are omitted.
//
//	De-confliction: strife_mon.c OWNS (fills) the shared Sigil/smoke projectile
//	slots MT_S_SIGIL_A_GROUND, _A_ZAP_LEFT, _A_ZAP_RIGHT, _C_SHOT, _E_OFFSHOOT,
//	_TRAIL, _SD_SHOT, _SE_SHOT and MT_S_MISSILESMOKE, plus the shared states
//	ZAP1/ZAP5/ZAP6/ZAP7/ZOT2/MICR/MISS/MISL/BNG2/PUFY_04-08.  We only REFERENCE
//	those; we FILL the player-weapon-exclusive projectiles/states below.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "info.h"
#include "m_fixed.h"
#include "tables.h"		// angle_t, ANG*, finecosine/finesine
#include "m_random.h"		// P_Random
#include "sounds.h"
#include "doomstat.h"		// players[], consoleplayer
#include "d_player.h"		// player_t, pspdef_t
#include "d_items.h"		// weaponinfo_t, weaponinfo[]
#include "p_mobj.h"
#include "p_pspr.h"		// pspdef_t, ps_weapon, ps_flash
#include "p_local.h"		// MELEERANGE, MISSILERANGE, linetarget, P_LineAttack, P_SpawnMobj, P_Thrust family
#include "s_sound.h"		// S_StartSound
#include "r_main.h"		// R_PointToAngle2

#define BRIGHT		32768		// FF_FULLBRIGHT frame bit

extern state_t   *states;
extern mobjinfo_t *mobjinfo;
extern weaponinfo_t weaponinfo[];

// --- engine pieces we call (declared by hand, like heretic_weapons.c) ---
// (linetarget, MELEERANGE, MISSILERANGE, P_AimLineAttack, P_LineAttack,
//  P_SpawnMobj, P_SpawnPlayerMissile, P_PlayerLookSlope come from p_local.h)
extern fixed_t		bulletslope;		// set by P_BulletSlope
extern mobjtype_t	PuffType;		// which puff the next P_SpawnPuff spawns
extern void		P_BulletSlope (mobj_t* mo);
extern void		P_CheckMissileSpawn (mobj_t* th);	// p_mobj.c
extern void		A_Explode (mobj_t* thingy);		// p_enemy.c -- radius damage
extern int		autoaim;				// p_pspr.c
extern int		strife_mode;
extern void		P_Thrust (player_t* player, angle_t angle, fixed_t move);	// p_user.c

// Shared psprite code pointers (defined in p_pspr.c; not all in p_pspr.h).
extern void		A_WeaponReady (player_t* player, pspdef_t* psp);
extern void		A_ReFire (player_t* player, pspdef_t* psp);
extern void		A_Lower (player_t* player, pspdef_t* psp);
extern void		A_Raise (player_t* player, pspdef_t* psp);
extern void		A_Light0 (player_t* player, pspdef_t* psp);
extern void		A_Light1 (player_t* player, pspdef_t* psp);
extern void		A_Light2 (player_t* player, pspdef_t* psp);
extern void		A_GunFlash (player_t* player, pspdef_t* psp);
extern boolean		P_CheckAmmo (player_t* player);
extern void		P_SetPsprite (player_t* player, int position, statenum_t stnum);

// strife P_SubRandom(): signed [-255..255]
#define P_SubRandom()	(P_Random() - P_Random())
#define WEAPONTOP	(32*FRACUNIT)	// psprite top (p_pspr.c private)

// Spawn a player missile, DOOM-auto-aimed like P_SpawnPlayerMissile, but RETURN
// the mobj and optionally override the horizontal launch angle.  BuddyDoom's own
// P_SpawnPlayerMissile is void with no angle override.  (Copied from the Heretic
// port's H_SPMAngle.)
static mobj_t* S_SPMAngle (mobj_t* source, mobjtype_t type, angle_t angle, boolean useangle)
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
#define S_SpawnPlayerMissile(s,t)	S_SPMAngle ((s), (t), 0, false)


// ---------------------------------------------------------------------------
// FIRING CODE POINTERS  (DOOM 2-arg psprite signature: player_t*, pspdef_t*)
// ---------------------------------------------------------------------------

// strife A_Punch (Punch Dagger), simplified: this engine has no player stamina,
// so use a plain melee damage roll; berserk (pw_strength) still multiplies.
static void A_Punch (player_t* player, pspdef_t* psp)
{
    angle_t	angle;
    int		damage, slope, t;

    damage = 3 * (P_Random() % 10 + 1);
    if (player->powers[pw_strength])
	damage *= 10;

    angle = player->mo->angle;
    t = P_Random();
    angle += (t - P_Random()) << 18;
    slope = P_AimLineAttack (player->mo, angle, MELEERANGE);
    PuffType = MT_S_STRIFEPUFF;
    P_LineAttack (player->mo, angle, MELEERANGE, slope, damage);

    if (linetarget)
    {
	S_StartSound (player->mo,
		      (linetarget->flags & MF_NOBLOOD) ? sfx_s_mtalht : sfx_s_meatht);
	player->mo->angle = R_PointToAngle2 (player->mo->x, player->mo->y,
					     linetarget->x, linetarget->y);
	player->mo->flags |= MF_JUSTATTACKED;
    }
    else
	S_StartSound (player->mo, sfx_s_swish);
}

// strife P_GunShot (rifle pellet), inlined here.
static void S_GunShot (mobj_t* mo, boolean accurate)
{
    angle_t	angle = mo->angle;
    int		damage;

    if (!accurate)
    {
	int t = P_Random();
	angle += (t - P_Random()) << 18;
    }
    damage = 4 * (P_Random() % 3 + 1);
    PuffType = MT_S_STRIFEPUFF;
    P_LineAttack (mo, angle, MISSILERANGE, bulletslope, damage);
}

// strife A_FireRifle (Assault Rifle) -- full-auto hitscan.
static void A_FireRifle (player_t* player, pspdef_t* psp)
{
    S_StartSound (player->mo, sfx_s_rifle);
    if (player->ammo[weaponinfo[player->readyweapon].ammo])
    {
	player->ammo[weaponinfo[player->readyweapon].ammo]--;
	P_BulletSlope (player->mo);
	S_GunShot (player->mo, !player->refire);
    }
}

// strife A_FireElectricBolt (Crossbow primary) -- one electric bolt.
static void A_FireElectricBolt (player_t* player, pspdef_t* psp)
{
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_SpawnPlayerMissile (player->mo, MT_S_ELECARROW);
    S_StartSound (player->mo, sfx_s_xbow);
}

// strife A_FirePoisonBolt (Crossbow secondary -- unreachable) -- one poison bolt.
static void A_FirePoisonBolt (player_t* player, pspdef_t* psp)
{
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_SpawnPlayerMissile (player->mo, MT_S_POISARROW);
    S_StartSound (player->mo, sfx_s_xbow);
}

// strife A_FireMissile (Mini-Missile Launcher), simplified (no accuracy spread,
// no d_recoil pitch kick).
static void A_FireMissile (player_t* player, pspdef_t* psp)
{
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_SpawnPlayerMissile (player->mo, MT_S_MINIMISSLE);
}

// strife A_FireFlameThrower (Flamethrower) -- lobbed fireball.
static void A_FireFlameThrower (player_t* player, pspdef_t* psp)
{
    mobj_t*	mo;
    int		t;

    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    t = P_Random();
    player->mo->angle += (t - P_Random()) << 18;
    mo = S_SpawnPlayerMissile (player->mo, MT_S_SFIREBALL);
    if (mo)
	mo->momz += (5*FRACUNIT);
}

// strife A_FireGrenade (Grenade Launcher), simplified: fires one straight-flying
// HE grenade that detonates on impact (no bounce/lob).  // TODO: MF_BOUNCE arc.
static void A_FireGrenade (player_t* player, pspdef_t* psp)
{
    player->ammo[weaponinfo[player->readyweapon].ammo]--;
    S_StartSound (player->mo, sfx_s_phoot);
    S_SpawnPlayerMissile (player->mo, MT_S_HEGRENADE);
}

// strife A_FireMauler1 (Mauler single) -- a 20-pellet energy shotgun burst.
static void A_FireMauler1 (player_t* player, pspdef_t* psp)
{
    int		i, damage, t;
    angle_t	angle;

    if (player->ammo[weaponinfo[player->readyweapon].ammo] >= 20)
    {
	player->ammo[weaponinfo[player->readyweapon].ammo] -= 20;
	P_BulletSlope (player->mo);
	S_StartSound (player->mo, sfx_s_pgrdat);
	PuffType = MT_S_SPARKPUFF;

	for (i = 0; i < 20; i++)
	{
	    damage = 5 * (P_Random() % 3 + 1);
	    angle  = player->mo->angle;
	    t = P_Random();
	    angle += (t - P_Random()) << 19;
	    t = P_Random();
	    P_LineAttack (player->mo, angle, 2112*FRACUNIT,
			  bulletslope + ((t - P_Random()) << 5), damage);
	}
    }
}

// strife A_FireMauler2 (Mauler Torpedo -- unreachable): self-damage recoil, big
// missile, hard backward thrust.  am_cell in this port; guard the ammo subtract.
static void A_FireMauler2 (player_t* player, pspdef_t* psp)
{
    P_DamageMobj (player->mo, player->mo, NULL, 20);
    if (player->ammo[weaponinfo[player->readyweapon].ammo] >= 30)
	player->ammo[weaponinfo[player->readyweapon].ammo] -= 30;
    else
	player->ammo[weaponinfo[player->readyweapon].ammo] = 0;
    S_SpawnPlayerMissile (player->mo, MT_S_TORPEDO);
    P_Thrust (player, player->mo->angle + ANG180, 512000);
}

// strife A_MaulerSound -- proton charge-up (unreachable Torpedo path).
static void A_MaulerSound (player_t* player, pspdef_t* psp)
{
    int t;
    S_StartSound (player->mo, sfx_s_proton);
    t = P_Random(); psp->sx += (t - P_Random()) << 10;
    t = P_Random(); psp->sy += (t - P_Random()) << 10;
}

// strife A_SigilSound / A_SigilShock -- muzzle-light cues.
static void A_SigilSound (player_t* player, pspdef_t* psp)
{
    S_StartSound (player->mo, sfx_s_siglup);
    player->extralight = 2;
}
static void A_SigilShock (player_t* player, pspdef_t* psp)
{
    player->extralight = -3;
}

// strife A_GunFlashThinker -- clears the flash / extralight (no sigiltype here).
static void A_GunFlashThinker (player_t* player, pspdef_t* psp)
{
    P_SetPsprite (player, ps_flash, S_NULL);
    player->extralight = 0;
}

// strife A_CheckReload -- just revalidate ammo (crossbow reload sprite dropped).
static void A_CheckReload (player_t* player, pspdef_t* psp)
{
    P_CheckAmmo (player);
}

// strife A_FireSigil, reduced to the type-4 "mega blast" (no sigiltype field).
static void A_FireSigil (player_t* player, pspdef_t* psp)
{
    mobj_t*	mo;
    int		savedarmor, damage;
    fixed_t	thrust;

    // Sigil does piercing damage: temporarily zero armor for the self-hit.
    savedarmor = player->armortype;
    player->armortype = 0;
    damage = 4;					// 4 * (sigiltype 0 + 1)
    P_DamageMobj (player->mo, NULL, player->mo, damage);
    thrust = damage * (FRACUNIT>>3) * 100 / player->mo->info->mass;
    P_Thrust (player, player->mo->angle + ANG180, thrust);
    player->armortype = savedarmor;

    S_StartSound (player->mo, sfx_s_siglup);

    mo = S_SpawnPlayerMissile (player->mo, MT_S_SIGIL_E_SHOT);
    if (mo)
	mo->health = -1;			// strife spectral bookkeeping
}

// ---------------------------------------------------------------------------
// PROJECTILE CODE POINTERS  (mobj signature: mobj_t*)
// ---------------------------------------------------------------------------

// strife A_TorpedoExplode -- burst a ring of sub-torpedoes (unreachable path).
static void A_TorpedoExplode (mobj_t* actor)
{
    int		i;
    angle_t	an;
    mobj_t*	sub;

    for (i = 0; i < 16; i++)
    {
	an  = actor->angle + (angle_t)((unsigned)i * (ANG45 / 2));	// 16 * 22.5deg = full circle
	sub = P_SpawnMobj (actor->x, actor->y, actor->z, MT_S_TORPEDOSPREAD);
	sub->target = actor->target;
	sub->angle  = an;
	sub->momx   = FixedMul (sub->info->speed, finecosine[an>>ANGLETOFINESHIFT]);
	sub->momy   = FixedMul (sub->info->speed, finesine  [an>>ANGLETOFINESHIFT]);
	P_CheckMissileSpawn (sub);
    }
}


// ---------------------------------------------------------------------------
// TABLE FILL HELPERS (identical style to heretic_weapons.c)
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

// A player MISSILE.  BuddyDoom lacks Strife's MF2_*/MF_BOUNCE/MF_SPECTRAL, so we
// use the DOOM-compatible flag subset supplied by the caller.
static void Proj (mobjtype_t mt, statenum_t spawn, statenum_t death,
		  int speed, int dmg, int radius, int height, int mass,
		  int seesnd, int deathsnd, int activesnd, int flags)
{
    mobjinfo_t*	m = &mobjinfo[mt];
    m->doomednum   = -1;
    m->spawnstate  = spawn;  m->spawnhealth = 1000;
    m->seestate    = S_NULL; m->seesound    = seesnd;  m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;  m->painchance = 0;
    m->painsound   = sfx_None; m->meleestate = S_NULL; m->missilestate = S_NULL;
    m->deathstate  = death;  m->xdeathstate = S_NULL;  m->deathsound = deathsnd;
    m->speed = speed*FRACUNIT; m->radius = radius*FRACUNIT; m->height = height*FRACUNIT;
    m->mass = mass; m->damage = dmg; m->activesound = activesnd;
    m->flags = flags;
    m->raisestate = S_NULL;
}

// A non-blocking, non-gravity hit-puff actor.
static void Puff (mobjtype_t mt, statenum_t spawn, int flags)
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
    m->flags = flags;
    m->raisestate = S_NULL;
}

#define PROJFLAGS	(MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE)
#define GRENFLAGS	(MF_NOBLOCKMAP|MF_DROPOFF|MF_MISSILE)		// has gravity


void Strife_Weapons_Init (void)
{
    // =====================================================================
    //  WEAPON PSPRITE STATES (strife info.c states, S_*->S_S_*)
    // =====================================================================

    // ---- PUNCH DAGGER ----
    ST (S_S_PNCH_00, SPR_S_PNCH, 0, 0, (actionf_p1)A_Light0,      S_NULL);
    ST (S_S_PNCH_01, SPR_S_PNCH, 0, 1, (actionf_p1)A_WeaponReady, S_S_PNCH_01);
    ST (S_S_PNCH_02, SPR_S_PNCH, 0, 1, (actionf_p1)A_Lower,       S_S_PNCH_02);
    ST (S_S_PNCH_03, SPR_S_PNCH, 0, 1, (actionf_p1)A_Raise,       S_S_PNCH_03);
    ST (S_S_PNCH_04, SPR_S_PNCH, 1, 4, NULL,                      S_S_PNCH_05);
    ST (S_S_PNCH_05, SPR_S_PNCH, 2, 4, (actionf_p1)A_Punch,       S_S_PNCH_06);
    ST (S_S_PNCH_06, SPR_S_PNCH, 3, 5, NULL,                      S_S_PNCH_07);
    ST (S_S_PNCH_07, SPR_S_PNCH, 2, 4, NULL,                      S_S_PNCH_08);
    ST (S_S_PNCH_08, SPR_S_PNCH, 1, 5, (actionf_p1)A_ReFire,      S_S_PNCH_01);

    // ---- CROSSBOW (electric primary 00-12, poison secondary 13-22) ----
    ST (S_S_XBOW_00, SPR_S_XBOW, 0, 1, (actionf_p1)A_WeaponReady,       S_S_XBOW_00);
    ST (S_S_XBOW_01, SPR_S_XBOW, 0, 1, (actionf_p1)A_Lower,             S_S_XBOW_01);
    ST (S_S_XBOW_02, SPR_S_XBOW, 0, 1, (actionf_p1)A_Raise,             S_S_XBOW_02);
    ST (S_S_XBOW_03, SPR_S_XBOW, 0, 3, (actionf_p1)A_GunFlashThinker,   S_S_XBOW_04);
    ST (S_S_XBOW_04, SPR_S_XBOW, 1, 6, (actionf_p1)A_FireElectricBolt,  S_S_XBOW_05);
    ST (S_S_XBOW_05, SPR_S_XBOW, 2, 4, NULL,                            S_S_XBOW_06);
    ST (S_S_XBOW_06, SPR_S_XBOW, 3, 6, NULL,                            S_S_XBOW_07);
    ST (S_S_XBOW_07, SPR_S_XBOW, 4, 3, NULL,                            S_S_XBOW_08);
    ST (S_S_XBOW_08, SPR_S_XBOW, 5, 5, NULL,                            S_S_XBOW_09);
    ST (S_S_XBOW_09, SPR_S_XBOW, 6, 5, (actionf_p1)A_CheckReload,       S_S_XBOW_00);
    ST (S_S_XBOW_10, SPR_S_XBOW, 10, 5, NULL,                           S_S_XBOW_11);
    ST (S_S_XBOW_11, SPR_S_XBOW, 11, 5, NULL,                           S_S_XBOW_12);
    ST (S_S_XBOW_12, SPR_S_XBOW, 12, 5, NULL,                           S_S_XBOW_10);
    ST (S_S_XBOW_13, SPR_S_XBOW, 7, 1, (actionf_p1)A_WeaponReady,       S_S_XBOW_13);
    ST (S_S_XBOW_14, SPR_S_XBOW, 7, 1, (actionf_p1)A_Lower,             S_S_XBOW_14);
    ST (S_S_XBOW_15, SPR_S_XBOW, 7, 1, (actionf_p1)A_Raise,             S_S_XBOW_15);
    ST (S_S_XBOW_16, SPR_S_XBOW, 7, 3, NULL,                            S_S_XBOW_17);
    ST (S_S_XBOW_17, SPR_S_XBOW, 1, 6, (actionf_p1)A_FirePoisonBolt,    S_S_XBOW_18);
    ST (S_S_XBOW_18, SPR_S_XBOW, 2, 4, NULL,                            S_S_XBOW_19);
    ST (S_S_XBOW_19, SPR_S_XBOW, 3, 6, NULL,                            S_S_XBOW_20);
    ST (S_S_XBOW_20, SPR_S_XBOW, 4, 3, NULL,                            S_S_XBOW_21);
    ST (S_S_XBOW_21, SPR_S_XBOW, 8, 5, NULL,                            S_S_XBOW_22);
    ST (S_S_XBOW_22, SPR_S_XBOW, 9, 5, (actionf_p1)A_CheckReload,       S_S_XBOW_13);

    // ---- MINI-MISSILE LAUNCHER ----
    ST (S_S_MMIS_00, SPR_S_MMIS, 0, 1, (actionf_p1)A_WeaponReady,  S_S_MMIS_00);
    ST (S_S_MMIS_01, SPR_S_MMIS, 0, 1, (actionf_p1)A_Lower,        S_S_MMIS_01);
    ST (S_S_MMIS_02, SPR_S_MMIS, 0, 1, (actionf_p1)A_Raise,        S_S_MMIS_02);
    ST (S_S_MMIS_03, SPR_S_MMIS, 0, 4, (actionf_p1)A_FireMissile,  S_S_MMIS_04);
    ST (S_S_MMIS_04, SPR_S_MMIS, 1, 4, (actionf_p1)A_Light1,       S_S_MMIS_05);
    ST (S_S_MMIS_05, SPR_S_MMIS, BRIGHT|2, 5, NULL,                S_S_MMIS_06);
    ST (S_S_MMIS_06, SPR_S_MMIS, BRIGHT|3, 2, (actionf_p1)A_Light2,S_S_MMIS_07);
    ST (S_S_MMIS_07, SPR_S_MMIS, BRIGHT|4, 2, NULL,                S_S_MMIS_08);
    ST (S_S_MMIS_08, SPR_S_MMIS, BRIGHT|5, 2, (actionf_p1)A_Light0,S_S_MMIS_09);
    ST (S_S_MMIS_09, SPR_S_MMIS, 5, 0, (actionf_p1)A_ReFire,       S_S_MMIS_00);

    // ---- ASSAULT RIFLE ----
    ST (S_S_RIFG_00, SPR_S_RIFG, 0, 1, (actionf_p1)A_WeaponReady, S_S_RIFG_00);
    ST (S_S_RIFG_01, SPR_S_RIFG, 1, 1, (actionf_p1)A_Lower,       S_S_RIFG_01);
    ST (S_S_RIFG_02, SPR_S_RIFG, 0, 1, (actionf_p1)A_Raise,       S_S_RIFG_02);
    ST (S_S_RIFF_00, SPR_S_RIFF, 0, 3, (actionf_p1)A_FireRifle,   S_S_RIFF_01);
    ST (S_S_RIFF_01, SPR_S_RIFF, 1, 3, (actionf_p1)A_FireRifle,   S_S_RIFG_03);
    ST (S_S_RIFG_03, SPR_S_RIFG, 3, 3, (actionf_p1)A_FireRifle,   S_S_RIFG_04);
    ST (S_S_RIFG_04, SPR_S_RIFG, 2, 0, (actionf_p1)A_ReFire,      S_S_RIFG_05);
    ST (S_S_RIFG_05, SPR_S_RIFG, 1, 2, NULL,                      S_S_RIFG_00);

    // ---- FLAMETHROWER ----
    ST (S_S_FLMT_00, SPR_S_FLMT, 0, 3, (actionf_p1)A_WeaponReady,       S_S_FLMT_01);
    ST (S_S_FLMT_01, SPR_S_FLMT, 1, 3, (actionf_p1)A_WeaponReady,       S_S_FLMT_00);
    ST (S_S_FLMT_02, SPR_S_FLMT, 0, 1, (actionf_p1)A_Lower,             S_S_FLMT_02);
    ST (S_S_FLMT_03, SPR_S_FLMT, 0, 1, (actionf_p1)A_Raise,             S_S_FLMT_03);
    ST (S_S_FLMF_00, SPR_S_FLMF, 0, 2, (actionf_p1)A_FireFlameThrower,  S_S_FLMF_01);
    ST (S_S_FLMF_01, SPR_S_FLMF, 1, 3, (actionf_p1)A_ReFire,            S_S_FLMT_00);

    // ---- MAULER (single 00-12, torpedo 13-24 [unreachable]) ----
    ST (S_S_BLST_00, SPR_S_BLST, 5, 6, (actionf_p1)A_WeaponReady, S_S_BLST_01);
    ST (S_S_BLST_01, SPR_S_BLST, 6, 6, (actionf_p1)A_WeaponReady, S_S_BLST_02);
    ST (S_S_BLST_02, SPR_S_BLST, 7, 6, (actionf_p1)A_WeaponReady, S_S_BLST_03);
    ST (S_S_BLST_03, SPR_S_BLST, 0, 6, (actionf_p1)A_WeaponReady, S_S_BLST_00);
    ST (S_S_BLST_04, SPR_S_BLST, 0, 1, (actionf_p1)A_Lower,       S_S_BLST_04);
    ST (S_S_BLST_05, SPR_S_BLST, 0, 1, (actionf_p1)A_Raise,       S_S_BLST_05);
    ST (S_S_BLSF_00, SPR_S_BLSF, BRIGHT|0, 5, (actionf_p1)A_FireMauler1, S_S_BLST_06);
    ST (S_S_BLST_06, SPR_S_BLST, BRIGHT|1, 3, (actionf_p1)A_Light1, S_S_BLST_07);
    ST (S_S_BLST_07, SPR_S_BLST, 2, 2, (actionf_p1)A_Light2,      S_S_BLST_08);
    ST (S_S_BLST_08, SPR_S_BLST, 3, 2, NULL,                      S_S_BLST_09);
    ST (S_S_BLST_09, SPR_S_BLST, 4, 2, NULL,                      S_S_BLST_10);
    ST (S_S_BLST_10, SPR_S_BLST, 0, 7, (actionf_p1)A_Light0,      S_S_BLST_11);
    ST (S_S_BLST_11, SPR_S_BLST, 7, 7, NULL,                      S_S_BLST_12);
    ST (S_S_BLST_12, SPR_S_BLST, 6, 7, (actionf_p1)A_CheckReload, S_S_BLST_00);
    ST (S_S_BLST_13, SPR_S_BLST, 8, 7, (actionf_p1)A_WeaponReady, S_S_BLST_14);
    ST (S_S_BLST_14, SPR_S_BLST, 9, 7, (actionf_p1)A_WeaponReady, S_S_BLST_15);
    ST (S_S_BLST_15, SPR_S_BLST, 10, 7, (actionf_p1)A_WeaponReady,S_S_BLST_16);
    ST (S_S_BLST_16, SPR_S_BLST, 11, 7, (actionf_p1)A_WeaponReady,S_S_BLST_13);
    ST (S_S_BLST_17, SPR_S_BLST, 8, 1, (actionf_p1)A_Lower,       S_S_BLST_17);
    ST (S_S_BLST_18, SPR_S_BLST, 8, 1, (actionf_p1)A_Raise,       S_S_BLST_18);
    ST (S_S_BLST_19, SPR_S_BLST, 8, 20, (actionf_p1)A_MaulerSound,S_S_BLST_20);
    ST (S_S_BLST_20, SPR_S_BLST, 9, 10, (actionf_p1)A_Light1,     S_S_BLSF_01);
    ST (S_S_BLSF_01, SPR_S_BLSF, BRIGHT|0, 10, (actionf_p1)A_FireMauler2, S_S_BLST_21);
    ST (S_S_BLST_21, SPR_S_BLST, BRIGHT|1, 3, (actionf_p1)A_Light2, S_S_BLST_22);
    ST (S_S_BLST_22, SPR_S_BLST, 2, 2, NULL,                      S_S_BLST_23);
    ST (S_S_BLST_23, SPR_S_BLST, 3, 2, (actionf_p1)A_Light0,      S_S_BLST_24);
    ST (S_S_BLST_24, SPR_S_BLST, 4, 2, (actionf_p1)A_ReFire,      S_S_BLST_13);

    // ---- GRENADE LAUNCHER (HE 00-07/GREF 00-02, WP 08-15/GREF 03-05 [unreach]) ----
    ST (S_S_GREN_00, SPR_S_GREN, 0, 1, (actionf_p1)A_WeaponReady,  S_S_GREN_00);
    ST (S_S_GREN_01, SPR_S_GREN, 0, 1, (actionf_p1)A_Lower,        S_S_GREN_01);
    ST (S_S_GREN_02, SPR_S_GREN, 0, 1, (actionf_p1)A_Raise,        S_S_GREN_02);
    ST (S_S_GREN_03, SPR_S_GREN, 0, 5, (actionf_p1)A_FireGrenade,  S_S_GREN_04);
    ST (S_S_GREN_04, SPR_S_GREN, 1, 10, NULL,                      S_S_GREN_05);
    ST (S_S_GREN_05, SPR_S_GREN, 0, 5, (actionf_p1)A_FireGrenade,  S_S_GREN_06);
    ST (S_S_GREN_06, SPR_S_GREN, 2, 10, NULL,                      S_S_GREN_07);
    ST (S_S_GREN_07, SPR_S_GREN, 0, 0, (actionf_p1)A_ReFire,       S_S_GREN_00);
    ST (S_S_GREF_00, SPR_S_GREF, BRIGHT|0, 5, (actionf_p1)A_Light1, S_S_PNCH_00);
    ST (S_S_GREF_01, SPR_S_GREF, 0, 10, (actionf_p1)A_Light0,      S_S_PNCH_00);
    ST (S_S_GREF_02, SPR_S_GREF, BRIGHT|1, 5, (actionf_p1)A_Light2, S_S_PNCH_00);
    ST (S_S_GREN_08, SPR_S_GREN, 3, 1, (actionf_p1)A_WeaponReady,  S_S_GREN_08);
    ST (S_S_GREN_09, SPR_S_GREN, 3, 1, (actionf_p1)A_Lower,        S_S_GREN_09);
    ST (S_S_GREN_10, SPR_S_GREN, 3, 1, (actionf_p1)A_Raise,        S_S_GREN_10);
    ST (S_S_GREN_11, SPR_S_GREN, 3, 5, (actionf_p1)A_FireGrenade,  S_S_GREN_12);
    ST (S_S_GREN_12, SPR_S_GREN, 4, 10, NULL,                      S_S_GREN_13);
    ST (S_S_GREN_13, SPR_S_GREN, 3, 5, (actionf_p1)A_FireGrenade,  S_S_GREN_14);
    ST (S_S_GREN_14, SPR_S_GREN, 5, 10, NULL,                      S_S_GREN_15);
    ST (S_S_GREN_15, SPR_S_GREN, 0, 0, (actionf_p1)A_ReFire,       S_S_GREN_08);
    ST (S_S_GREF_03, SPR_S_GREF, BRIGHT|2, 5, (actionf_p1)A_Light1, S_S_PNCH_00);
    ST (S_S_GREF_04, SPR_S_GREF, 2, 10, (actionf_p1)A_Light0,      S_S_PNCH_00);
    ST (S_S_GREF_05, SPR_S_GREF, BRIGHT|3, 5, (actionf_p1)A_Light2, S_S_PNCH_00);

    // ---- SIGIL ----
    ST (S_S_SIGH_00, SPR_S_SIGH, BRIGHT|0, 1, (actionf_p1)A_WeaponReady, S_S_SIGH_00);
    ST (S_S_SIGH_01, SPR_S_SIGH, BRIGHT|1, -1, NULL, S_NULL);
    ST (S_S_SIGH_02, SPR_S_SIGH, BRIGHT|2, -1, NULL, S_NULL);
    ST (S_S_SIGH_03, SPR_S_SIGH, BRIGHT|3, -1, NULL, S_NULL);
    ST (S_S_SIGH_04, SPR_S_SIGH, BRIGHT|4, -1, NULL, S_NULL);
    ST (S_S_SIGH_05, SPR_S_SIGH, BRIGHT|0, 1, (actionf_p1)A_Lower,       S_S_SIGH_05);
    ST (S_S_SIGH_06, SPR_S_SIGH, BRIGHT|0, 1, (actionf_p1)A_Raise,       S_S_SIGH_06);
    ST (S_S_SIGH_07, SPR_S_SIGH, BRIGHT|0, 18, (actionf_p1)A_SigilSound, S_S_SIGH_08);
    ST (S_S_SIGH_08, SPR_S_SIGH, BRIGHT|0, 3, (actionf_p1)A_GunFlash,    S_S_SIGH_09);
    ST (S_S_SIGH_09, SPR_S_SIGH, 0, 10, (actionf_p1)A_FireSigil,         S_S_SIGH_10);
    ST (S_S_SIGH_10, SPR_S_SIGH, 0, 5, (actionf_p1)A_GunFlashThinker,    S_S_SIGH_00);
    ST (S_S_SIGF_00, SPR_S_SIGF, BRIGHT|0, 4, (actionf_p1)A_Light2,      S_S_SIGF_01);
    ST (S_S_SIGF_01, SPR_S_SIGF, BRIGHT|1, 6, (actionf_p1)A_SigilShock,  S_S_SIGF_02);
    ST (S_S_SIGF_02, SPR_S_SIGF, BRIGHT|2, 4, (actionf_p1)A_Light1,      S_S_PNCH_00);

    // =====================================================================
    //  WEAPON-EXCLUSIVE PROJECTILE STATES  (not filled by strife_mon.c)
    // =====================================================================

    // Crossbow bolts (A_ActiveSound whistle dropped -> NULL loop).
    ST (S_S_AROW_00, SPR_S_AROW, 0, 10, NULL, S_S_AROW_00);
    ST (S_S_AROW_01, SPR_S_AROW, 0, 1,  NULL, S_NULL);
    ST (S_S_ARWP_00, SPR_S_ARWP, 0, 10, NULL, S_S_ARWP_00);

    // Grenades in flight (A_MissileTick lifetime dropped -> NULL ping-pong).
    ST (S_S_GRAP_00, SPR_S_GRAP, 0, 3, NULL, S_S_GRAP_01);	// HE
    ST (S_S_GRAP_01, SPR_S_GRAP, 1, 3, NULL, S_S_GRAP_00);
    ST (S_S_GRIN_00, SPR_S_GRIN, 0, 3, NULL, S_S_GRIN_01);	// WP (unreachable)
    ST (S_S_GRIN_01, SPR_S_GRIN, 1, 3, NULL, S_S_GRIN_00);

    // HE grenade / mini-missile-class explosion (BNG4).
    ST (S_S_BNG4_00, SPR_S_BNG4, BRIGHT|0, 2, (actionf_p1)A_Explode, S_S_BNG4_01);
    ST (S_S_BNG4_01, SPR_S_BNG4, BRIGHT|1, 3, NULL, S_S_BNG4_02);
    ST (S_S_BNG4_02, SPR_S_BNG4, BRIGHT|2, 3, NULL, S_S_BNG4_03);
    ST (S_S_BNG4_03, SPR_S_BNG4, BRIGHT|3, 3, NULL, S_S_BNG4_04);
    ST (S_S_BNG4_04, SPR_S_BNG4, BRIGHT|4, 3, NULL, S_S_BNG4_05);
    ST (S_S_BNG4_05, SPR_S_BNG4, BRIGHT|5, 3, NULL, S_S_BNG4_06);
    ST (S_S_BNG4_06, SPR_S_BNG4, BRIGHT|6, 3, NULL, S_S_BNG4_07);
    ST (S_S_BNG4_07, SPR_S_BNG4, BRIGHT|7, 3, NULL, S_S_BNG4_08);
    ST (S_S_BNG4_08, SPR_S_BNG4, BRIGHT|8, 3, NULL, S_S_BNG4_09);
    ST (S_S_BNG4_09, SPR_S_BNG4, BRIGHT|9, 3, NULL, S_S_BNG4_10);
    ST (S_S_BNG4_10, SPR_S_BNG4, BRIGHT|10, 3, NULL, S_S_BNG4_11);
    ST (S_S_BNG4_11, SPR_S_BNG4, BRIGHT|11, 3, NULL, S_S_BNG4_12);
    ST (S_S_BNG4_12, SPR_S_BNG4, BRIGHT|12, 3, NULL, S_S_BNG4_13);
    ST (S_S_BNG4_13, SPR_S_BNG4, BRIGHT|13, 3, NULL, S_NULL);

    // WP grenade explosion (BNG3) -- simplified to a plain blast (phosphorus
    // fire A_SpawnGrenadeFire path dropped).  Used only by the unreachable WP.
    ST (S_S_BNG3_00, SPR_S_BNG3, BRIGHT|0, 3, (actionf_p1)A_Explode, S_S_BNG3_01);
    ST (S_S_BNG3_01, SPR_S_BNG3, BRIGHT|1, 3, NULL, S_S_BNG3_02);
    ST (S_S_BNG3_02, SPR_S_BNG3, BRIGHT|2, 3, NULL, S_S_BNG3_03);
    ST (S_S_BNG3_03, SPR_S_BNG3, BRIGHT|3, 3, NULL, S_S_BNG3_04);
    ST (S_S_BNG3_04, SPR_S_BNG3, BRIGHT|4, 3, NULL, S_S_BNG3_05);
    ST (S_S_BNG3_05, SPR_S_BNG3, BRIGHT|5, 3, NULL, S_S_BNG3_06);
    ST (S_S_BNG3_06, SPR_S_BNG3, BRIGHT|6, 3, NULL, S_S_BNG3_07);
    ST (S_S_BNG3_07, SPR_S_BNG3, BRIGHT|7, 3, NULL, S_NULL);

    // Torpedo (unreachable) flight/impact/spread.
    ST (S_S_TORP_00, SPR_S_TORP, BRIGHT|0, 4, NULL, S_S_TORP_01);
    ST (S_S_TORP_01, SPR_S_TORP, BRIGHT|1, 4, NULL, S_S_TORP_02);
    ST (S_S_TORP_02, SPR_S_TORP, BRIGHT|2, 4, NULL, S_S_TORP_03);
    ST (S_S_TORP_03, SPR_S_TORP, BRIGHT|3, 4, NULL, S_S_TORP_00);
    ST (S_S_THIT_00, SPR_S_THIT, BRIGHT|0, 8, NULL, S_S_THIT_01);
    ST (S_S_THIT_01, SPR_S_THIT, BRIGHT|1, 8, NULL, S_S_THIT_02);
    ST (S_S_THIT_02, SPR_S_THIT, BRIGHT|2, 8, (actionf_p1)A_TorpedoExplode, S_S_THIT_03);
    ST (S_S_THIT_03, SPR_S_THIT, BRIGHT|3, 8, NULL, S_S_THIT_04);
    ST (S_S_THIT_04, SPR_S_THIT, BRIGHT|4, 8, NULL, S_NULL);
    ST (S_S_TWAV_00, SPR_S_TWAV, BRIGHT|0, 9, NULL, S_S_TWAV_01);
    ST (S_S_TWAV_01, SPR_S_TWAV, BRIGHT|1, 9, NULL, S_S_TWAV_02);
    ST (S_S_TWAV_02, SPR_S_TWAV, BRIGHT|2, 9, NULL, S_NULL);

    // Bullet puff (PUFY_00-03; 04-08 are strife_mon.c's).
    ST (S_S_PUFY_00, SPR_S_PUFY, BRIGHT|0, 4, NULL, S_S_PUFY_01);
    ST (S_S_PUFY_01, SPR_S_PUFY, 1, 4, NULL, S_S_PUFY_02);
    ST (S_S_PUFY_02, SPR_S_PUFY, 2, 4, NULL, S_S_PUFY_03);
    ST (S_S_PUFY_03, SPR_S_PUFY, 3, 4, NULL, S_NULL);

    // Spark puff (Mauler pellet hit, POW3).
    ST (S_S_POW3_00, SPR_S_POW3, 0, 3, NULL, S_S_POW3_01);
    ST (S_S_POW3_01, SPR_S_POW3, 1, 3, NULL, S_S_POW3_02);
    ST (S_S_POW3_02, SPR_S_POW3, 2, 3, NULL, S_S_POW3_03);
    ST (S_S_POW3_03, SPR_S_POW3, 3, 3, NULL, S_S_POW3_04);
    ST (S_S_POW3_04, SPR_S_POW3, 4, 3, NULL, S_S_POW3_05);
    ST (S_S_POW3_05, SPR_S_POW3, 5, 3, NULL, S_S_POW3_06);
    ST (S_S_POW3_06, SPR_S_POW3, 6, 3, NULL, S_S_POW3_07);
    ST (S_S_POW3_07, SPR_S_POW3, 7, 3, NULL, S_NULL);

    // =====================================================================
    //  WEAPON-PROJECTILE MOBJINFO  (fill; sigil shots reference mon-owned states)
    // =====================================================================

    //    mt,                 spawn,          death,          spd dmg  r   h  mass seesnd            deathsnd          activesnd    flags
    Proj (MT_S_MINIMISSLE,    S_S_MICR_00,    S_S_MISL_01,    20, 10, 10, 14, 100, sfx_s_rlaunc,     sfx_s_mislht,     sfx_None,    PROJFLAGS);
    Proj (MT_S_ELECARROW,     S_S_AROW_00,    S_S_ZAP1_01,    30, 10, 10, 10, 100, sfx_s_swish,      sfx_s_firxpl,     sfx_s_swish, PROJFLAGS);
    Proj (MT_S_POISARROW,     S_S_ARWP_00,    S_S_AROW_01,    30, 500,10, 10, 100, sfx_s_swish,      sfx_None,         sfx_s_swish, PROJFLAGS);
    Proj (MT_S_HEGRENADE,     S_S_GRAP_00,    S_S_BNG4_00,    15, 1,  13, 13, 20,  sfx_s_phoot,      sfx_s_explod,     sfx_None,    GRENFLAGS);
    Proj (MT_S_PGRENADE,      S_S_GRIN_00,    S_S_BNG3_00,    15, 1,  13, 13, 20,  sfx_s_phoot,      sfx_s_explod,     sfx_None,    GRENFLAGS);
    Proj (MT_S_TORPEDO,       S_S_TORP_00,    S_S_THIT_00,    20, 1,  13, 8,  100, sfx_s_protfl,     sfx_s_explod,     sfx_None,    PROJFLAGS);
    Proj (MT_S_TORPEDOSPREAD, S_S_TWAV_00,    S_S_TWAV_02,    35, 10, 13, 13, 100, sfx_None,         sfx_None,         sfx_None,    PROJFLAGS);
    Proj (MT_S_SFIREBALL,     S_S_FRBL_00,    S_S_FRBL_03,    15, 4,  8,  11, 10,  sfx_s_flburn,     sfx_None,         sfx_None,    GRENFLAGS);
    // Sigil player shots (drop MF_SPECTRAL; states are strife_mon.c's).
    Proj (MT_S_SIGIL_B_SHOT,  S_S_ZAP6_00,    S_S_ZAP1_00,    30, 70, 8,  16, 100, sfx_s_sigil,      sfx_s_sglhit,     sfx_None,    PROJFLAGS);
    Proj (MT_S_SIGIL_D_SHOT,  S_S_ZOT2_00,    S_S_ZAP1_01,    28, 120,8,  16, 100, sfx_s_sigil,      sfx_s_sglhit,     sfx_None,    PROJFLAGS);
    Proj (MT_S_SIGIL_E_SHOT,  S_S_ZAP7_00,    S_S_ZAP1_02,    18, 130,20, 40, 100, sfx_s_sigil,      sfx_s_sglhit,     sfx_None,    PROJFLAGS);

    // Puffs.
    Puff (MT_S_STRIFEPUFF, S_S_PUFY_00, MF_NOBLOCKMAP|MF_NOGRAVITY|MF_SHADOW);
    Puff (MT_S_SPARKPUFF,  S_S_POW3_00, MF_NOBLOCKMAP|MF_NOGRAVITY);

    // =====================================================================
    //  Overwrite the DOOM weaponinfo[] slots (caller gates on strife_mode).
    // =====================================================================
    if (!strife_mode)
	return;

    // Punch Dagger -> wp_fist (am_noammo).
    weaponinfo[wp_fist].ammo       = am_noammo;
    weaponinfo[wp_fist].upstate    = S_S_PNCH_03;
    weaponinfo[wp_fist].downstate  = S_S_PNCH_02;
    weaponinfo[wp_fist].readystate = S_S_PNCH_01;
    weaponinfo[wp_fist].atkstate   = S_S_PNCH_04;
    weaponinfo[wp_fist].flashstate = S_NULL;

    // Assault Rifle -> wp_pistol (am_bullets -> am_clip).
    weaponinfo[wp_pistol].ammo       = am_clip;
    weaponinfo[wp_pistol].upstate    = S_S_RIFG_02;
    weaponinfo[wp_pistol].downstate  = S_S_RIFG_01;
    weaponinfo[wp_pistol].readystate = S_S_RIFG_00;
    weaponinfo[wp_pistol].atkstate   = S_S_RIFF_00;
    weaponinfo[wp_pistol].flashstate = S_NULL;

    // Crossbow (electric) -> wp_shotgun (am_elecbolts -> am_shell).
    weaponinfo[wp_shotgun].ammo       = am_shell;
    weaponinfo[wp_shotgun].upstate    = S_S_XBOW_02;
    weaponinfo[wp_shotgun].downstate  = S_S_XBOW_01;
    weaponinfo[wp_shotgun].readystate = S_S_XBOW_00;
    weaponinfo[wp_shotgun].atkstate   = S_S_XBOW_03;
    weaponinfo[wp_shotgun].flashstate = S_NULL;

    // Mauler (single) -> wp_chaingun (am_cell).
    weaponinfo[wp_chaingun].ammo       = am_cell;
    weaponinfo[wp_chaingun].upstate    = S_S_BLST_05;
    weaponinfo[wp_chaingun].downstate  = S_S_BLST_04;
    weaponinfo[wp_chaingun].readystate = S_S_BLST_00;
    weaponinfo[wp_chaingun].atkstate   = S_S_BLSF_00;
    weaponinfo[wp_chaingun].flashstate = S_NULL;

    // Mini-Missile Launcher -> wp_missile (am_missiles -> am_misl).
    weaponinfo[wp_missile].ammo       = am_misl;
    weaponinfo[wp_missile].upstate    = S_S_MMIS_02;
    weaponinfo[wp_missile].downstate  = S_S_MMIS_01;
    weaponinfo[wp_missile].readystate = S_S_MMIS_00;
    weaponinfo[wp_missile].atkstate   = S_S_MMIS_03;
    weaponinfo[wp_missile].flashstate = S_NULL;

    // Flamethrower -> wp_plasma (am_cell, shared with Mauler).
    weaponinfo[wp_plasma].ammo       = am_cell;
    weaponinfo[wp_plasma].upstate    = S_S_FLMT_03;
    weaponinfo[wp_plasma].downstate  = S_S_FLMT_02;
    weaponinfo[wp_plasma].readystate = S_S_FLMT_00;
    weaponinfo[wp_plasma].atkstate   = S_S_FLMF_00;
    weaponinfo[wp_plasma].flashstate = S_NULL;

    // Sigil -> wp_bfg (am_noammo).
    weaponinfo[wp_bfg].ammo       = am_noammo;
    weaponinfo[wp_bfg].upstate    = S_S_SIGH_06;
    weaponinfo[wp_bfg].downstate  = S_S_SIGH_05;
    weaponinfo[wp_bfg].readystate = S_S_SIGH_00;
    weaponinfo[wp_bfg].atkstate   = S_S_SIGH_07;
    weaponinfo[wp_bfg].flashstate = S_S_SIGF_00;

    // Grenade Launcher (HE) -> wp_chainsaw (am_hegrenades -> am_shell, shared).
    weaponinfo[wp_chainsaw].ammo       = am_shell;
    weaponinfo[wp_chainsaw].upstate    = S_S_GREN_02;
    weaponinfo[wp_chainsaw].downstate  = S_S_GREN_01;
    weaponinfo[wp_chainsaw].readystate = S_S_GREN_00;
    weaponinfo[wp_chainsaw].atkstate   = S_S_GREN_03;
    weaponinfo[wp_chainsaw].flashstate = S_S_GREF_00;
}
