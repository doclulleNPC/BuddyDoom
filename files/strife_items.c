// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(S) Map-placeable Strife PICKUP actors + touch dispatch in the DOOM engine.
//
//	Ported from strife-ve src/strife/{info.c,p_inter.c}.  These MT_S_* pickup
//	slots (ammo boxes, ground weapons, armor, health, backpack/satchel, keys,
//	money/coin, and the inventory-item world pickups) were deliberately left
//	UNFILLED by the decoration (strife_deco.c), monster (strife_mon.c) and
//	weapon (strife_weapons.c) installers; we fill their states + mobjinfo here
//	at runtime in Strife_Items_Init, and dispatch the on-touch effect in
//	P_TouchStrifeItem (called from p_inter.c's P_TouchSpecialThing before the
//	DOOM-sprite switch, same mechanism as P_TouchHereticItem / P_TouchHexenItem).
//
//	EFFECT MAPPING (this is the 1993 id C core -- no native Strife item ring):
//	  * Ammo pickups grant the DOOM ammo pool the matching Strife weapon uses in
//	    strife_weapons.c (bullets->am_clip, cell->am_cell, missiles->am_misl,
//	    elec/poison bolts + HE/WP grenades -> am_shell).  Backpack/satchel raises
//	    the ammo maxima and tops off one clip of each.
//	  * Ground weapons grant the DOOM weaponinfo[] slot strife_weapons.c mapped
//	    (rifle=pistol, crossbow=shotgun, mauler=chaingun, missile-launcher=missile,
//	    flamethrower=plasma, sigil=bfg, grenade-launcher=chainsaw).
//	  * Armor -> P_GiveArmor (leather=green/1, metal=blue/2).
//	  * Med patch / kit / surgery kit -> pocketed as the Strife inventory items
//	    (s_arti_medpatch / _medkit / _stamina), used later via ApplyStrifeArtifact.
//	  * Inventory usables (shadow armor, targeter, envirosuit, teleporter beacon,
//	    degnin ore) -> pocketed into player->inventory[s_arti_*].
//	  * Money (coin/10/25/50 gold) accumulates into player->inventory[s_arti_coin].
//	  * Full map / radar -> P_GivePower(pw_allmap); communicator -> message only.
//	  * Strife quest keys -> picked up with a message; there is no Strife key ring
//	    or door gating yet (// TODO).  Quest tokens (SPR_S_TOKN) -> message only.
//
//	Every consumed pickup returns true so P_TouchSpecialThing removes it and plays
//	the pickup sound; returning false leaves it on the ground (armor already full,
//	inventory slot capped) -- matching P_TouchSpecialThing's contract.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "info.h"
#include "m_fixed.h"
#include "sounds.h"
#include "d_player.h"		// player_t, artitype_t
#include "p_mobj.h"
#include "strife_items.h"

#define BRIGHT		32768		// FF_FULLBRIGHT frame bit

extern state_t   *states;
extern mobjinfo_t *mobjinfo;

// engine pieces we call (declared by hand, like heretic_items.c)
extern boolean	P_GiveAmmo  (player_t* player, ammotype_t ammo, int num);
extern boolean	P_GiveWeapon(player_t* player, weapontype_t weapon, boolean dropped);
extern boolean	P_GiveArmor (player_t* player, int armortype);
extern boolean	P_GiveBody  (player_t* player, int num);
extern boolean	P_GivePower (player_t* player, int power);


// ---------------------------------------------------------------------------
// Table fill helpers (identical style to heretic_items.c).
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

// Fill a map-placeable pickup actor: MF_SPECIAL (picked up on touch), a single
// spinning-icon spawnstate.  `ednum` is the real Strife doomednum (-1 = script-
// spawned, not map-placed); `extra` adds flags.  Radius/height 20/16 (Strife).
static void Item (mobjtype_t mt, int ednum, statenum_t spawn, int extra)
{
    mobjinfo_t*	m = &mobjinfo[mt];
    m->doomednum   = ednum;
    m->spawnstate  = spawn;  m->spawnhealth = 1000;
    m->seestate    = S_NULL; m->seesound    = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate = S_NULL;   m->painchance = 0;
    m->painsound   = sfx_None; m->meleestate = S_NULL;  m->missilestate = S_NULL;
    m->deathstate  = S_NULL; m->xdeathstate = S_NULL;   m->deathsound = sfx_None;
    m->speed = 0; m->radius = 20*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 100;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPECIAL | extra;
    m->raisestate = S_NULL;
}


void Strife_Items_Init (void)
{
    mobjinfo_t*	m;

    // =====================================================================
    //  PICKUP SPINNING-ICON STATES  (strife info.c states, S_*->S_S_*)
    //  Static single-frame (tics -1 -> S_NULL) unless noted as a loop.
    // =====================================================================

    // ---- inventory-item world pickups ----
    ST (S_S_STMP_00, SPR_S_STMP, 0, -1, NULL, S_NULL);		// med patch
    ST (S_S_MDKT_00, SPR_S_MDKT, 0, -1, NULL, S_NULL);		// medical kit
    ST (S_S_FULL_00, SPR_S_FULL, 0, 35, NULL, S_S_FULL_01);	// surgery kit (loop)
    ST (S_S_FULL_01, SPR_S_FULL, 1, 35, NULL, S_S_FULL_00);
    ST (S_S_SHD1_00, SPR_S_SHD1, 0, 17, NULL, S_S_SHD1_01);	// shadow armor (loop)
    ST (S_S_SHD1_01, SPR_S_SHD1, 0, 17, NULL, S_S_SHD1_02);
    ST (S_S_SHD1_02, SPR_S_SHD1, 0, 17, NULL, S_S_SHD1_03);
    ST (S_S_SHD1_03, SPR_S_SHD1, 0, 17, NULL, S_S_SHD1_00);
    ST (S_S_MASK_00, SPR_S_MASK, 0, -1, NULL, S_NULL);		// environmental suit
    ST (S_S_TARG_00, SPR_S_TARG, 0, -1, NULL, S_NULL);		// targeter
    ST (S_S_BEAC_00, SPR_S_BEAC, 0, -1, NULL, S_NULL);		// teleporter beacon
    ST (S_S_COMM_00, SPR_S_COMM, 0, -1, NULL, S_NULL);		// communicator
    ST (S_S_RELC_00, SPR_S_RELC, BRIGHT|0, -1, NULL, S_NULL);	// offering chalice

    // Degnin ore: spins in place (spawn), explodes into the BNG3 blast (filled by
    // strife_weapons.c, A_Explode) when shot.  A_Zombie/ClearForceField dropped.
    ST (S_S_XPRK_01, SPR_S_XPRK, 0, 6, NULL, S_S_XPRK_01);

    // ---- power-like world pickups (BRIGHT loop) ----
    ST (S_S_PMAP_00, SPR_S_PMAP, BRIGHT|0, 6, NULL, S_S_PMAP_01);	// full map
    ST (S_S_PMAP_01, SPR_S_PMAP, BRIGHT|1, 6, NULL, S_S_PMAP_00);
    ST (S_S_PMUP_00, SPR_S_PMUP, BRIGHT|0, 6, NULL, S_S_PMUP_01);	// radar/scanner
    ST (S_S_PMUP_01, SPR_S_PMUP, BRIGHT|1, 6, NULL, S_S_PMUP_00);

    // ---- armor ----
    ST (S_S_ARM1_00, SPR_S_ARM1, 0, -1, NULL, S_NULL);		// leather armor
    ST (S_S_ARM2_00, SPR_S_ARM2, 0, -1, NULL, S_NULL);		// metal armor

    // ---- ammo ----
    ST (S_S_BLIT_00, SPR_S_BLIT, 0, -1, NULL, S_NULL);		// clip of bullets
    ST (S_S_BBOX_00, SPR_S_BBOX, 0, -1, NULL, S_NULL);		// box of bullets
    ST (S_S_MSSL_00, SPR_S_MSSL, 0, -1, NULL, S_NULL);		// mini missile
    ST (S_S_ROKT_00, SPR_S_ROKT, 0, -1, NULL, S_NULL);		// crate of missiles
    ST (S_S_BRY1_00, SPR_S_BRY1, 0, 6, NULL, S_S_BRY1_01);	// battery (loop)
    ST (S_S_BRY1_01, SPR_S_BRY1, 1, 6, NULL, S_S_BRY1_00);
    ST (S_S_CPAC_00, SPR_S_CPAC, 0, 6, NULL, S_S_CPAC_01);	// cell pack (loop)
    ST (S_S_CPAC_01, SPR_S_CPAC, 1, 6, NULL, S_S_CPAC_00);
    ST (S_S_PQRL_00, SPR_S_PQRL, 0, -1, NULL, S_NULL);		// poison bolts
    ST (S_S_XQRL_00, SPR_S_XQRL, 0, -1, NULL, S_NULL);		// electric bolts
    ST (S_S_GRN1_00, SPR_S_GRN1, 0, -1, NULL, S_NULL);		// HE grenades
    ST (S_S_GRN2_00, SPR_S_GRN2, 0, -1, NULL, S_NULL);		// WP grenades
    ST (S_S_BKPK_00, SPR_S_BKPK, 0, -1, NULL, S_NULL);		// ammo satchel

    // ---- money ----
    ST (S_S_COIN_00, SPR_S_COIN, 0, -1, NULL, S_NULL);		// 1 gold
    ST (S_S_CRED_00, SPR_S_CRED, 0, -1, NULL, S_NULL);		// 10 gold
    ST (S_S_SACK_00, SPR_S_SACK, 0, -1, NULL, S_NULL);		// 25 gold
    ST (S_S_CHST_00, SPR_S_CHST, 0, -1, NULL, S_NULL);		// 50 gold

    // ---- ground weapons ----
    ST (S_S_RIFL_00, SPR_S_RIFL, 0, -1, NULL, S_NULL);		// assault rifle
    ST (S_S_RIFL_01, SPR_S_RIFL, 1, -1, NULL, S_NULL);		// assault rifle (stand)
    ST (S_S_FLAM_00, SPR_S_FLAM, 0, -1, NULL, S_NULL);		// flamethrower
    ST (S_S_MMSL_00, SPR_S_MMSL, 0, -1, NULL, S_NULL);		// missile launcher
    ST (S_S_TRPD_00, SPR_S_TRPD, 0, -1, NULL, S_NULL);		// mauler
    ST (S_S_CBOW_00, SPR_S_CBOW, 0, -1, NULL, S_NULL);		// crossbow
    ST (S_S_GRND_00, SPR_S_GRND, 0, -1, NULL, S_NULL);		// grenade launcher
    ST (S_S_SIGL_00, SPR_S_SIGL, 0, -1, NULL, S_NULL);		// sigil piece A
    ST (S_S_SIGL_01, SPR_S_SIGL, 1, -1, NULL, S_NULL);		// sigil piece B
    ST (S_S_SIGL_02, SPR_S_SIGL, 2, -1, NULL, S_NULL);		// sigil piece C
    ST (S_S_SIGL_03, SPR_S_SIGL, 3, -1, NULL, S_NULL);		// sigil piece D
    ST (S_S_SIGL_04, SPR_S_SIGL, 4, -1, NULL, S_NULL);		// sigil piece E
    ST (S_S_BFLM_00, SPR_S_BFLM, 0, -1, NULL, S_NULL);		// flamethrower parts

    // ---- keys (single static frame; crystal keys are FULLBRIGHT) ----
    ST (S_S_FUSL_00, SPR_S_FUSL, 0, -1, NULL, S_NULL);		// base key
    ST (S_S_REBL_00, SPR_S_REBL, 0, -1, NULL, S_NULL);		// govs key
    ST (S_S_TPAS_00, SPR_S_TPAS, 0, -1, NULL, S_NULL);		// passcard
    ST (S_S_CRD1_00, SPR_S_CRD1, 0, -1, NULL, S_NULL);		// ID badge
    ST (S_S_PRIS_00, SPR_S_PRIS, 0, -1, NULL, S_NULL);		// prison key
    ST (S_S_HAND_00, SPR_S_HAND, 0, -1, NULL, S_NULL);		// severed hand
    ST (S_S_PWR1_00, SPR_S_PWR1, 0, -1, NULL, S_NULL);		// power1 key
    ST (S_S_PWR2_00, SPR_S_PWR2, 0, -1, NULL, S_NULL);		// power2 key
    ST (S_S_PWR3_00, SPR_S_PWR3, 0, -1, NULL, S_NULL);		// power3 key
    ST (S_S_KY1G_00, SPR_S_KY1G, 0, -1, NULL, S_NULL);		// gold key
    ST (S_S_CRD2_00, SPR_S_CRD2, 0, -1, NULL, S_NULL);		// ID card
    ST (S_S_KY2S_00, SPR_S_KY2S, 0, -1, NULL, S_NULL);		// silver key
    ST (S_S_ORAC_00, SPR_S_ORAC, 0, -1, NULL, S_NULL);		// oracle key
    ST (S_S_GYID_00, SPR_S_GYID, 0, -1, NULL, S_NULL);		// military ID
    ST (S_S_FUBR_00, SPR_S_FUBR, 0, -1, NULL, S_NULL);		// order key
    ST (S_S_WARE_00, SPR_S_WARE, 0, -1, NULL, S_NULL);		// warehouse key
    ST (S_S_KY3B_00, SPR_S_KY3B, 0, -1, NULL, S_NULL);		// brass key
    ST (S_S_RCRY_00, SPR_S_RCRY, BRIGHT|0, -1, NULL, S_NULL);	// red crystal key
    ST (S_S_BCRY_00, SPR_S_BCRY, BRIGHT|0, -1, NULL, S_NULL);	// blue crystal key
    ST (S_S_CHAP_00, SPR_S_CHAP, 0, -1, NULL, S_NULL);		// chapel key
    ST (S_S_TUNL_00, SPR_S_TUNL, 0, -1, NULL, S_NULL);		// catacomb key
    ST (S_S_SECK_00, SPR_S_SECK, 0, -1, NULL, S_NULL);		// security key
    ST (S_S_GOID_00, SPR_S_GOID, 0, -1, NULL, S_NULL);		// core key
    ST (S_S_BLTK_00, SPR_S_BLTK, 0, -1, NULL, S_NULL);		// mauler key / newkey5
    ST (S_S_PROC_00, SPR_S_PROC, 0, -1, NULL, S_NULL);		// factory key
    ST (S_S_MINE_00, SPR_S_MINE, 0, -1, NULL, S_NULL);		// mine key

    // =====================================================================
    //  PICKUP MOBJINFO  (Strife doomednums)
    // =====================================================================

    // ---- health / inventory items ----
    Item (MT_S_INV_MED1,        2011, S_S_STMP_00, 0);		// med patch
    Item (MT_S_INV_MED2,        2012, S_S_MDKT_00, 0);		// medical kit
    Item (MT_S_INV_MED3,          83, S_S_FULL_00, 0);		// surgery kit
    Item (MT_S_INV_SHADOWARMOR, 2024, S_S_SHD1_00, 0);		// shadow armor
    Item (MT_S_INV_SUIT,        2025, S_S_MASK_00, 0);		// environmental suit
    Item (MT_S_INV_TARGETER,     207, S_S_TARG_00, 0);		// targeter
    Item (MT_S_BEACON,            10, S_S_BEAC_00, MF_DROPPED);	// teleporter beacon
    Item (MT_S_INV_COMMUNICATOR, 206, S_S_COMM_00, MF_NOTDMATCH);	// communicator
    Item (MT_S_INV_CHALICE,      205, S_S_RELC_00, MF_DROPPED);	// offering chalice
    Item (MT_S_INV_SUPERMAP,    2026, S_S_PMAP_00, 0);		// full map
    Item (MT_S_INV_RADAR,       2027, S_S_PMUP_00, 0);		// radar/scanner

    // Degnin ore: shootable solid that detonates when shot.
    m = &mobjinfo[MT_S_DEGNINORE];
    m->doomednum   = 59;
    m->spawnstate  = S_S_XPRK_01; m->spawnhealth = 10;
    m->seestate    = S_NULL;  m->seesound    = sfx_None; m->reactiontime = 8;
    m->attacksound = sfx_None; m->painstate  = S_NULL;   m->painchance = 0;
    m->painsound   = sfx_None; m->meleestate = S_NULL;   m->missilestate = S_NULL;
    m->deathstate  = S_S_BNG3_00; m->xdeathstate = S_NULL; m->deathsound = sfx_s_explod;
    m->speed = 0; m->radius = 16*FRACUNIT; m->height = 16*FRACUNIT; m->mass = 10;
    m->damage = 0; m->activesound = sfx_None;
    m->flags = MF_SPECIAL|MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD;
    m->raisestate = S_NULL;

    // ---- armor ----
    Item (MT_S_INV_ARMOR1, 2018, S_S_ARM2_00, 0);		// metal armor
    Item (MT_S_INV_ARMOR2, 2019, S_S_ARM1_00, 0);		// leather armor

    // ---- ammo ----
    Item (MT_S_ACLIP,    2007, S_S_BLIT_00, 0);			// clip of bullets
    Item (MT_S_AAMMOBOX, 2048, S_S_BBOX_00, 0);			// box of bullets
    Item (MT_S_AMINI,    2010, S_S_MSSL_00, 0);			// mini missile
    Item (MT_S_AMINIBOX, 2046, S_S_ROKT_00, 0);			// crate of missiles
    Item (MT_S_ACELL,    2047, S_S_BRY1_00, 0);			// battery
    Item (MT_S_APCELL,     17, S_S_CPAC_00, 0);			// cell pack
    Item (MT_S_APAROW,    115, S_S_PQRL_00, 0);			// poison bolts
    Item (MT_S_AAROW,     114, S_S_XQRL_00, 0);			// electric bolts
    Item (MT_S_AGREN,     152, S_S_GRN1_00, 0);			// HE grenades
    Item (MT_S_APGREN,    153, S_S_GRN2_00, 0);			// WP grenades
    Item (MT_S_INV_SATCHEL, 183, S_S_BKPK_00, 0);		// ammo satchel

    // ---- money ----
    Item (MT_S_MONY_1,   93, S_S_COIN_00, MF_DROPPED|MF_NOTDMATCH);	// 1 gold
    Item (MT_S_MONY_10, 138, S_S_CRED_00, MF_DROPPED|MF_NOTDMATCH);	// 10 gold
    Item (MT_S_MONY_25, 139, S_S_SACK_00, MF_DROPPED|MF_NOTDMATCH);	// 25 gold
    Item (MT_S_MONY_50, 140, S_S_CHST_00, MF_DROPPED|MF_NOTDMATCH);	// 50 gold

    // ---- ground weapons ----
    Item (MT_S_PULSE,          2002, S_S_RIFL_00, 0);		// assault rifle
    Item (MT_S_RIFLESTAND,     2006, S_S_RIFL_01, 0);		// assault rifle (stand)
    Item (MT_S_FLAMETHROWER,   2005, S_S_FLAM_00, 0);		// flamethrower
    Item (MT_S_MISSILELAUNCHER,2003, S_S_MMSL_00, 0);		// missile launcher
    Item (MT_S_BLASTER,        2004, S_S_TRPD_00, 0);		// mauler
    Item (MT_S_CROSSBOW,       2001, S_S_CBOW_00, 0);		// crossbow
    Item (MT_S_GRENADELAUNCHER, 154, S_S_GRND_00, 0);		// grenade launcher
    Item (MT_S_SIGIL_A, 77, S_S_SIGL_00, 0);			// sigil piece A
    Item (MT_S_SIGIL_B, 78, S_S_SIGL_01, 0);			// sigil piece B
    Item (MT_S_SIGIL_C, 79, S_S_SIGL_02, 0);			// sigil piece C
    Item (MT_S_SIGIL_D, 80, S_S_SIGL_03, 0);			// sigil piece D
    Item (MT_S_SIGIL_E, 81, S_S_SIGL_04, 0);			// sigil piece E

    // ---- keys (map-placed have a real doomednum; script keys use -1) ----
    Item (MT_S_KEY_BASE,        230, S_S_FUSL_00, MF_NOTDMATCH);
    Item (MT_S_GOVSKEY,          -1, S_S_REBL_00, MF_NOTDMATCH);
    Item (MT_S_KEY_TRAVEL,      185, S_S_TPAS_00, MF_NOTDMATCH);
    Item (MT_S_KEY_ID_BLUE,     184, S_S_CRD1_00, MF_NOTDMATCH);
    Item (MT_S_PRISONKEY,        -1, S_S_PRIS_00, MF_NOTDMATCH);
    Item (MT_S_KEY_HAND,         91, S_S_HAND_00, MF_NOTDMATCH);
    Item (MT_S_POWER1KEY,        -1, S_S_PWR1_00, MF_NOTDMATCH);
    Item (MT_S_POWER2KEY,        -1, S_S_PWR2_00, MF_NOTDMATCH);
    Item (MT_S_POWER3KEY,        -1, S_S_PWR3_00, MF_NOTDMATCH);
    Item (MT_S_KEY_GOLD,         40, S_S_KY1G_00, MF_NOTDMATCH);
    Item (MT_S_KEY_ID_GOLD,      13, S_S_CRD2_00, MF_NOTDMATCH);
    Item (MT_S_KEY_SILVER,       38, S_S_KY2S_00, MF_NOTDMATCH);
    Item (MT_S_KEY_ORACLE,       61, S_S_ORAC_00, MF_NOTDMATCH);
    Item (MT_S_MILITARYID,       -1, S_S_GYID_00, MF_NOTDMATCH);
    Item (MT_S_KEY_ORDER,        86, S_S_FUBR_00, MF_NOTDMATCH);
    Item (MT_S_KEY_WAREHOUSE,   166, S_S_WARE_00, MF_NOTDMATCH);
    Item (MT_S_KEY_BRASS,        39, S_S_KY3B_00, MF_NOTDMATCH);
    Item (MT_S_KEY_RED_CRYSTAL, 192, S_S_RCRY_00, MF_NOTDMATCH);
    Item (MT_S_KEY_BLUE_CRYSTAL,193, S_S_BCRY_00, MF_NOTDMATCH);
    Item (MT_S_KEY_CHAPEL,      195, S_S_CHAP_00, MF_NOTDMATCH);
    Item (MT_S_CATACOMBKEY,      -1, S_S_TUNL_00, MF_NOTDMATCH);
    Item (MT_S_SECURITYKEY,      -1, S_S_SECK_00, MF_NOTDMATCH);
    Item (MT_S_KEY_CORE,        236, S_S_GOID_00, MF_NOTDMATCH);
    Item (MT_S_KEY_MAULER,      233, S_S_BLTK_00, MF_NOTDMATCH);
    Item (MT_S_KEY_FACTORY,     234, S_S_PROC_00, MF_NOTDMATCH);
    Item (MT_S_KEY_MINE,        235, S_S_MINE_00, MF_NOTDMATCH);
    Item (MT_S_NEWKEY5,          -1, S_S_BLTK_00, MF_NOTDMATCH);
}


// ---------------------------------------------------------------------------
// Pocket an inventory-item pickup.  Returns false (leave on ground) if the slot
// is already at the carry cap, else increments it and selects it if nothing is
// selected -- mirrors P_TouchHereticArtifact.
// ---------------------------------------------------------------------------
static boolean Pocket (player_t* player, artitype_t a)
{
    if (player->inventory[a] >= MAXARTICOUNT)
	return false;
    player->inventory[a]++;
    if (player->invslot == arti_none)
	player->invslot = a;
    return true;
}


// ---------------------------------------------------------------------------
// A placed/spawned MT_S_* pickup was touched.  Dispatched on special->sprite,
// porting Strife's P_TouchSpecialThing item switch.  Returns true if the item
// was consumed (so P_TouchSpecialThing removes it + plays the pickup sound),
// false to leave it on the ground.  See the EFFECT MAPPING note in the header.
// ---------------------------------------------------------------------------
boolean P_TouchStrifeItem (player_t* player, mobj_t* special)
{
    if (!player)
	return false;

    // ---- Strife quest keys (by mobjtype range, exactly like strife p_inter.c's
    // default case).  No Strife key ring / door gating yet: pick them up with a
    // message so they don't sit on the map.  // TODO: real Strife key inventory.
    if (special->type >= MT_S_KEY_BASE && special->type <= MT_S_NEWKEY5)
    {
	player->message = "YOU PICKED UP A KEY";
	return true;
    }

    switch (special->sprite)
    {
      // ---- ammo (pool per strife_weapons.c: bullets->clip, cell->cell,
      //      missiles->misl, elec/poison bolts + HE/WP grenades -> shell) ----
      case SPR_S_BLIT:					// clip of bullets
	if (!P_GiveAmmo (player, am_clip, 1)) return false;
	player->message = "YOU PICKED UP THE AMMO CLIP"; return true;
      case SPR_S_BBOX:					// box of bullets
	if (!P_GiveAmmo (player, am_clip, 5)) return false;
	player->message = "YOU PICKED UP THE BOX OF BULLETS"; return true;
      case SPR_S_MSSL:					// mini missile
	if (!P_GiveAmmo (player, am_misl, 1)) return false;
	player->message = "YOU PICKED UP THE MINI MISSILE"; return true;
      case SPR_S_ROKT:					// crate of missiles
	if (!P_GiveAmmo (player, am_misl, 5)) return false;
	player->message = "YOU PICKED UP THE CRATE OF MISSILES"; return true;
      case SPR_S_BRY1:					// battery / energy pod
	if (!P_GiveAmmo (player, am_cell, 1)) return false;
	player->message = "YOU PICKED UP THE ENERGY POD"; return true;
      case SPR_S_CPAC:					// cell pack / energy pack
	if (!P_GiveAmmo (player, am_cell, 5)) return false;
	player->message = "YOU PICKED UP THE ENERGY PACK"; return true;
      case SPR_S_PQRL:					// poison bolts
	if (!P_GiveAmmo (player, am_shell, 5)) return false;
	player->message = "YOU PICKED UP THE POISON BOLTS"; return true;
      case SPR_S_XQRL:					// electric bolts
	if (!P_GiveAmmo (player, am_shell, 5)) return false;
	player->message = "YOU PICKED UP THE ELECTRIC BOLTS"; return true;
      case SPR_S_GRN1:					// HE grenades
	if (!P_GiveAmmo (player, am_shell, 1)) return false;
	player->message = "YOU PICKED UP THE HE-GRENADE ROUNDS"; return true;
      case SPR_S_GRN2:					// WP grenades
	if (!P_GiveAmmo (player, am_shell, 1)) return false;
	player->message = "YOU PICKED UP THE PHOSPHORUS-GRENADE ROUNDS"; return true;

      // ---- backpack / ammo satchel: raise the ammo maxima once, top off a clip
      //      of each pool (mirrors strife's SPR_BKPK case) ----
      case SPR_S_BKPK:
      {
	int i;
	if (!player->backpack)
	{
	    for (i = 0; i < NUMAMMO; i++)
		player->maxammo[i] *= 2;
	    player->backpack = true;
	}
	for (i = 0; i < NUMAMMO; i++)
	    P_GiveAmmo (player, i, 1);
	player->message = "YOU PICKED UP THE AMMO SATCHEL";
	return true;
      }

      // ---- ground weapons (slots per strife_weapons.c) ----
      case SPR_S_RIFL:					// assault rifle -> pistol
	if (!P_GiveWeapon (player, wp_pistol, special->flags & MF_DROPPED)) return false;
	player->message = "YOU PICKED UP THE ASSAULT GUN"; return true;
      case SPR_S_FLAM:					// flamethrower -> plasma
	if (!P_GiveWeapon (player, wp_plasma, false)) return false;
	P_GiveAmmo (player, am_cell, 3);		// strife gives extra ammo
	player->message = "YOU PICKED UP THE FLAME THROWER"; return true;
      case SPR_S_MMSL:					// missile launcher -> missile
	if (!P_GiveWeapon (player, wp_missile, false)) return false;
	player->message = "YOU PICKED UP THE MISSILE LAUNCHER"; return true;
      case SPR_S_GRND:					// grenade launcher -> chainsaw
	if (!P_GiveWeapon (player, wp_chainsaw, special->flags & MF_DROPPED)) return false;
	player->message = "YOU PICKED UP THE GRENADE LAUNCHER"; return true;
      case SPR_S_TRPD:					// mauler -> chaingun
	if (!P_GiveWeapon (player, wp_chaingun, false)) return false;
	player->message = "YOU PICKED UP THE MAULER"; return true;
      case SPR_S_CBOW:					// crossbow -> shotgun
	if (!P_GiveWeapon (player, wp_shotgun, special->flags & MF_DROPPED)) return false;
	player->message = "YOU PICKED UP THE CROSSBOW"; return true;
      case SPR_S_SIGL:					// the Sigil -> bfg
	if (!P_GiveWeapon (player, wp_bfg, special->flags & MF_DROPPED)) return false;
	player->message = "YOU PICKED UP THE SIGIL"; return true;

      // ---- armor (leather=green/1, metal=blue/2) ----
      case SPR_S_ARM1:					// leather armor
	if (!P_GiveArmor (player, 1)) return false;
	player->message = "YOU PICKED UP THE LEATHER ARMOR"; return true;
      case SPR_S_ARM2:					// metal armor
	if (!P_GiveArmor (player, 2)) return false;
	player->message = "YOU PICKED UP THE METAL ARMOR"; return true;

      // ---- health -> pocketed as the Strife inventory med items ----
      case SPR_S_STMP:					// med patch
	if (!Pocket (player, s_arti_medpatch)) return false;
	player->message = "YOU PICKED UP THE MED PATCH"; return true;
      case SPR_S_MDKT:					// medical kit
	if (!Pocket (player, s_arti_medkit)) return false;
	player->message = "YOU PICKED UP THE MEDICAL KIT"; return true;
      case SPR_S_FULL:					// surgery kit -> stamina/full heal
	if (!Pocket (player, s_arti_stamina)) return false;
	player->message = "YOU PICKED UP THE SURGERY KIT"; return true;

      // ---- inventory usables ----
      case SPR_S_SHD1:					// shadow armor
	if (!Pocket (player, s_arti_shadowarmor)) return false;
	player->message = "YOU PICKED UP THE SHADOW ARMOR"; return true;
      case SPR_S_TARG:					// targeter
	if (!Pocket (player, s_arti_targeter)) return false;
	player->message = "YOU PICKED UP THE TARGETER"; return true;
      case SPR_S_MASK:					// environmental suit
	if (!Pocket (player, s_arti_envirosuit)) return false;
	player->message = "YOU PICKED UP THE ENVIRONMENTAL SUIT"; return true;
      case SPR_S_BEAC:					// teleporter beacon
	if (!Pocket (player, s_arti_beacon)) return false;
	player->message = "YOU PICKED UP THE TELEPORTER BEACON"; return true;
      case SPR_S_XPRK:					// degnin ore
	if (!Pocket (player, s_arti_degninore)) return false;
	player->message = "YOU PICKED UP THE DEGNIN ORE"; return true;

      // ---- money: accumulate the gold counter ----
      case SPR_S_COIN:					// 1 gold
	player->inventory[s_arti_coin] += 1;
	player->message = "YOU PICKED UP THE GOLD"; return true;
      case SPR_S_CRED:					// 10 gold
	player->inventory[s_arti_coin] += 10;
	player->message = "YOU PICKED UP 10 GOLD"; return true;
      case SPR_S_SACK:					// 25 gold
	player->inventory[s_arti_coin] += 25;
	player->message = "YOU PICKED UP 25 GOLD"; return true;
      case SPR_S_CHST:					// 50 gold
	player->inventory[s_arti_coin] += 50;
	player->message = "YOU PICKED UP 50 GOLD"; return true;

      // ---- power-like ----
      case SPR_S_PMAP:					// full map
	if (!P_GivePower (player, pw_allmap)) return false;
	player->message = "YOU PICKED UP THE MAP"; return true;
      case SPR_S_PMUP:					// radar/scanner (also reveals map)
	if (!P_GivePower (player, pw_allmap)) return false;
	player->message = "YOU PICKED UP THE SCANNER"; return true;
      case SPR_S_COMM:					// communicator (no subsystem)
	player->message = "YOU PICKED UP THE COMMUNICATOR"; return true;

      // ---- quest tokens / chalice / weapon-part tokens -> message only ----
      // (SPR_S_TOKN is filled by strife_mon.c; RELC/BFLM are ours.)  // TODO quest flags.
      case SPR_S_TOKN:
      case SPR_S_RELC:
      case SPR_S_BFLM:
	player->message = "YOU PICKED UP THE ITEM"; return true;

      default:
	return false;					// not one of ours
    }
}
