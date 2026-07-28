// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(H) Map-placeable Heretic ITEM / PICKUP actors in the DOOM engine.
//
//	Ported from crispy-doom's heretic/info.c (mobjinfo doomednum/spawnstate/
//	radius/height) so the real Heretic keys, ammo, ground weapons, shields and
//	the health vial APPEAR on Heretic maps with the correct sprite at the
//	correct doomednum -- instead of being skipped by P_HereticThingType.
//
//	Same additive mechanism as files/heretic.c and files/p_inv_heretic.c:
//	the states/mobjinfo are appended to the engine tables at runtime
//	(Heretic_Items_Init, called from D_DoomMain next to HereticInv_Init); the
//	enum slots live at the end of spritenum_t/statenum_t/mobjtype_t (info.h).
//	Sprites use the native 4-char Heretic codes (AKYY/AMG1/WMCE/...) registered
//	directly in sprnames_builtin[] (info.c) -- the same convention the artifact
//	pickups use -- so they render straight out of heretic.wad in heretic_mode.
//
//	*** PICKUP EFFECTS ARE OUT OF SCOPE. ***  This engine has NO Heretic weapon /
//	ammo / key subsystem, so these pickups cannot grant real Heretic ammo, keys
//	or weapons.  On touch (P_TouchHereticItem, dispatched by mobjtype from
//	P_TouchSpecialThing before the sprite switch) each is handled minimally:
//	  - Crystal Vial   -> +10 HP           (DOOM P_GiveBody, the clean equivalent)
//	  - Silver Shield   -> green armor       (P_GiveArmor 1)
//	  - Enchanted Shield-> blue armor        (P_GiveArmor 2)
//	  - keys / ammo / ground weapons -> pocketed as a no-op (message only; no
//	    faithful effect -- there is no Heretic inventory to grant into).
//	Every one is removed on touch so it can't re-trigger.  Without this handler
//	an unrecognised MF_SPECIAL sprite hits the I_Error default in p_inter.c.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "info.h"
#include "m_fixed.h"
#include "sounds.h"
#include "d_player.h"		// player_t
#include "p_mobj.h"
#include "heretic_items.h"

#define BRIGHT		32768		// FF_FULLBRIGHT frame bit

extern state_t   *states;
extern mobjinfo_t *mobjinfo;

// engine pieces we call (declared by hand, like heretic.c / p_inv_heretic.c)
extern boolean	P_GiveBody  (player_t* player, int num);
extern boolean	P_GiveArmor (player_t* player, int armortype);
extern boolean	P_GiveWeapon (player_t* player, weapontype_t weapon, boolean dropped);


// ---------------------------------------------------------------------------
// Table fill helpers (identical style to p_inv_heretic.c).
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
// looping spinning-icon spawnstate.  `ednum` is the real Heretic doomednum;
// `extra` adds flags (MF_NOTDMATCH for keys).  Radius/height are crispy's 20/16.
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


void Heretic_Items_Init (void)
{
    // ---- Spinning-icon spawnstates (looping single frame) --------------------
    // Keys are FULLBRIGHT in Heretic (crispy S_AKYY1/S_BKYY1/S_CKYY1 use 32768);
    // ammo / weapons / shields / vial are not.
    ST (S_HKEY_GREEN,      SPR_HAKY, BRIGHT|0, 3, NULL, S_HKEY_GREEN);
    ST (S_HKEY_BLUE,       SPR_HBKY, BRIGHT|0, 3, NULL, S_HKEY_BLUE);
    ST (S_HKEY_YELLOW,     SPR_HCKY, BRIGHT|0, 3, NULL, S_HKEY_YELLOW);

    ST (S_HAM_GWNDW,       SPR_HAG1, 0, 6, NULL, S_HAM_GWNDW);	// wand crystal
    ST (S_HAM_GWNDH,       SPR_HAG2, 0, 4, NULL, S_HAM_GWNDH);	// crystal geode
    ST (S_HAM_MACEW,       SPR_HAM1, 0, 6, NULL, S_HAM_MACEW);	// mace spheres (small)
    ST (S_HAM_MACEH,       SPR_HAM2, 0, 6, NULL, S_HAM_MACEH);	// mace spheres (pile)
    ST (S_HAM_CBOWW,       SPR_HAC1, 0, 6, NULL, S_HAM_CBOWW);	// ethereal arrows
    ST (S_HAM_CBOWH,       SPR_HAC2, 0, 5, NULL, S_HAM_CBOWH);	// quiver of arrows
    ST (S_HAM_SKRDW,       SPR_HAS1, 0, 5, NULL, S_HAM_SKRDW);	// lesser runes
    ST (S_HAM_SKRDH,       SPR_HAS2, 0, 5, NULL, S_HAM_SKRDH);	// greater runes
    ST (S_HAM_PHRDW,       SPR_HAP1, 0, 4, NULL, S_HAM_PHRDW);	// flame orb
    ST (S_HAM_PHRDH,       SPR_HAP2, 0, 4, NULL, S_HAM_PHRDH);	// inferno orb
    ST (S_HAM_BLSRW,       SPR_HAB1, 0, 4, NULL, S_HAM_BLSRW);	// claw orb
    ST (S_HAM_BLSRH,       SPR_HAB2, 0, 4, NULL, S_HAM_BLSRH);	// energy orb

    ST (S_HWEP_MACE,       SPR_HWMC, 0, 6, NULL, S_HWEP_MACE);	// firemace
    ST (S_HWEP_SKULLROD,   SPR_HWSK, 0, 6, NULL, S_HWEP_SKULLROD);	// hellstaff
    ST (S_HWEP_PHOENIXROD, SPR_HWPH, 0, 6, NULL, S_HWEP_PHOENIXROD);	// phoenix rod
    ST (S_HWEP_CROSSBOW,   SPR_HWBW, 0, 6, NULL, S_HWEP_CROSSBOW);	// ethereal crossbow
    ST (S_HWEP_GAUNTLETS,  SPR_HWGN, 0, 6, NULL, S_HWEP_GAUNTLETS);	// gauntlets
    ST (S_HWEP_BLASTER,    SPR_HWBL, 0, 6, NULL, S_HWEP_BLASTER);	// dragon claw / blaster

    ST (S_HITEM_SHIELD1,   SPR_HSHL, 0, 6, NULL, S_HITEM_SHIELD1);	// silver shield
    ST (S_HITEM_SHIELD2,   SPR_HSH2, 0, 6, NULL, S_HITEM_SHIELD2);	// enchanted shield
    ST (S_HITEM_VIAL,      SPR_PTN1, 0, 3, NULL, S_HITEM_VIAL);	// crystal vial (reuse PTN1)

    // ---- Map-placeable pickup actors (crispy doomednums) --------------------
    // Keys: crispy MT_AKYY(73)/MT_BKYY(79)/MT_CKEY(80), MF_SPECIAL|MF_NOTDMATCH.
    Item (MT_HAKYY, 73, S_HKEY_GREEN,  MF_NOTDMATCH);	// green key
    Item (MT_HBKYY, 79, S_HKEY_BLUE,   MF_NOTDMATCH);	// blue key
    Item (MT_HCKYY, 80, S_HKEY_YELLOW, MF_NOTDMATCH);	// yellow key

    // Ammo: gold wand / mace / crossbow / hellstaff / phoenix / dragon-claw
    // (wimpy = small pickup, hefty = large pickup).
    Item (MT_HAMGWNDWIMPY, 10, S_HAM_GWNDW, 0);
    Item (MT_HAMGWNDHEFTY, 12, S_HAM_GWNDH, 0);
    Item (MT_HAMMACEWIMPY, 13, S_HAM_MACEW, 0);
    Item (MT_HAMMACEHEFTY, 16, S_HAM_MACEH, 0);
    Item (MT_HAMCBOWWIMPY, 18, S_HAM_CBOWW, 0);
    Item (MT_HAMCBOWHEFTY, 19, S_HAM_CBOWH, 0);
    Item (MT_HAMSKRDWIMPY, 20, S_HAM_SKRDW, 0);
    Item (MT_HAMSKRDHEFTY, 21, S_HAM_SKRDH, 0);
    Item (MT_HAMPHRDWIMPY, 22, S_HAM_PHRDW, 0);
    Item (MT_HAMPHRDHEFTY, 23, S_HAM_PHRDH, 0);
    Item (MT_HAMBLSRWIMPY, 54, S_HAM_BLSRW, 0);
    Item (MT_HAMBLSRHEFTY, 55, S_HAM_BLSRH, 0);

    // Ground weapons (crispy MT_WMACE/MT_WSKULLROD/MT_WPHOENIXROD + the three
    // MT_MISC weapon pickups: crossbow=MISC15, gauntlets=MISC13, blaster=MISC14).
    Item (MT_HWMACE,       2002, S_HWEP_MACE,       0);
    Item (MT_HWSKULLROD,   2004, S_HWEP_SKULLROD,   0);
    Item (MT_HWPHOENIXROD, 2003, S_HWEP_PHOENIXROD, 0);
    Item (MT_HWCROSSBOW,   2001, S_HWEP_CROSSBOW,   0);
    Item (MT_HWGAUNTLETS,  2005, S_HWEP_GAUNTLETS,  0);
    Item (MT_HWBLASTER,      53, S_HWEP_BLASTER,    0);	// dragon claw

    // Armor: silver shield (MT_ITEMSHIELD1=85), enchanted shield (MT_ITEMSHIELD2=31).
    Item (MT_HITEMSHIELD1, 85, S_HITEM_SHIELD1, 0);
    Item (MT_HITEMSHIELD2, 31, S_HITEM_SHIELD2, 0);

    // Health: Crystal Vial (crispy MT_MISC0=81, +10 HP).
    Item (MT_HCRYSTALVIAL, 81, S_HITEM_VIAL, 0);
}


// ---------------------------------------------------------------------------
// A placed/spawned MT_H* item was touched.  Returns true if handled (so
// P_TouchSpecialThing removes it + plays the pickup sound and stops), false if
// the item is not one of ours.  See the EFFECTS note in the file header:
// health/armor get the clean DOOM equivalent; everything else is a no-op pocket.
// ---------------------------------------------------------------------------
extern void P_GiveCard (player_t* player, card_t card);

boolean P_TouchHereticItem (player_t* player, mobj_t* special)
{
    if (!player)
	return false;

    switch (special->type)
    {
      // ---- keys: Heretic maps load with DOOM-numbered locked doors (26/27/28 =
      // blue/yellow/"red"), so grant the matching DOOM card slot -- Heretic's green
      // key drives the third ("red") lock.  Now the map's yellow/blue/green doors open.
      case MT_HAKYY: P_GiveCard (player, it_redcard);    player->message = "PICKED UP A GREEN KEY";  return true;
      case MT_HBKYY: P_GiveCard (player, it_bluecard);   player->message = "PICKED UP A BLUE KEY";   return true;
      case MT_HCKYY: P_GiveCard (player, it_yellowcard); player->message = "PICKED UP A YELLOW KEY"; return true;

      // ---- ammo (no Heretic ammo system -> cosmetic pickup) ----
      case MT_HAMGWNDWIMPY:
      case MT_HAMGWNDHEFTY: player->message = "GOLD WAND CRYSTALS";   return true;
      case MT_HAMMACEWIMPY:
      case MT_HAMMACEHEFTY: player->message = "MACE SPHERES";         return true;
      case MT_HAMCBOWWIMPY:
      case MT_HAMCBOWHEFTY: player->message = "ETHEREAL ARROWS";      return true;
      case MT_HAMSKRDWIMPY:
      case MT_HAMSKRDHEFTY: player->message = "GRAVE SCOURGE RUNES";  return true;
      case MT_HAMPHRDWIMPY:
      case MT_HAMPHRDHEFTY: player->message = "FLAME ORB";            return true;
      case MT_HAMBLSRWIMPY:
      case MT_HAMBLSRHEFTY: player->message = "CLAW ORB";             return true;

      // ---- ground weapons -> grant the real weapon + ammo (heretic_weapons.c
      // overwrote these DOOM slots with the Heretic weapon set; P_GiveWeapon also
      // gives the mapped ammo and auto-switches to it).  slot map:
      // crossbow=shotgun, dragonclaw=chaingun, hellstaff=missile, phoenix=plasma,
      // firemace=bfg, gauntlets=chainsaw.
      case MT_HWMACE:       P_GiveWeapon (player, wp_bfg,      false); player->message = "GOT THE FIREMACE";          return true;
      case MT_HWSKULLROD:   P_GiveWeapon (player, wp_missile,  false); player->message = "GOT THE HELLSTAFF";         return true;
      case MT_HWPHOENIXROD: P_GiveWeapon (player, wp_plasma,   false); player->message = "GOT THE PHOENIX ROD";       return true;
      case MT_HWCROSSBOW:   P_GiveWeapon (player, wp_shotgun,  false); player->message = "GOT THE ETHEREAL CROSSBOW"; return true;
      case MT_HWGAUNTLETS:  P_GiveWeapon (player, wp_chainsaw, false); player->message = "GOT THE GAUNTLETS";         return true;
      case MT_HWBLASTER:    P_GiveWeapon (player, wp_chaingun, false); player->message = "GOT THE DRAGON CLAW";       return true;

      // ---- armor -> closest DOOM equivalent (leave on the ground if it wouldn't
      //      upgrade the player's armor, like Heretic/DOOM) ----
      case MT_HITEMSHIELD1:
	if (!P_GiveArmor (player, 1))
	    return false;
	player->message = "SILVER SHIELD";
	return true;
      case MT_HITEMSHIELD2:
	if (!P_GiveArmor (player, 2))
	    return false;
	player->message = "ENCHANTED SHIELD";
	return true;

      // ---- health -> +10 HP (Crystal Vial); left on the ground at full health ----
      case MT_HCRYSTALVIAL:
	if (!P_GiveBody (player, 10))
	    return false;
	player->message = "CRYSTAL VIAL";
	return true;

      default:
	return false;			// not one of ours
    }
}
