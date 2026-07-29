// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(X) Map-placeable Hexen ITEM / PICKUP + PUZZLE actors in the DOOM engine.
//
//	Ported from crispy-doom's hexen/info.c (mobjinfo doomednum/spawnstate/
//	radius/height) so the real Hexen mana, keys, armor, artifacts, puzzle
//	pieces and class weapon pieces APPEAR on Hexen maps with the correct native
//	sprite at the correct doomednum -- instead of being skipped as "unknown
//	type" by P_SpawnMapThing.
//
//	Same additive mechanism as files/hexen.c and files/heretic_items.c: the
//	states/mobjinfo are appended to the engine tables at runtime
//	(Hexen_Items_Init, called from D_DoomMain next to Hexen_Init); the enum
//	slots live at the end of spritenum_t/statenum_t/mobjtype_t (info.h).
//	Sprites use the native 4-char Hexen codes (MAN1/KEY1/ASKU/WFR1/...)
//	registered directly in sprnames_builtin[] (info.c) -- the same convention
//	the Heretic item pickups use -- so they render straight out of the Hexen
//	art (a hexenstuff-style PWAD) when it is present.  A handful of artifacts
//	share their icon with the Heretic artifacts already in this engine and
//	simply reuse those sprites (SPR_PTN1/INVU/SPHL/TRCH/ATLP); the two Hexen
//	base armor pieces borrow DOOM's armor-bonus sprites (SPR_ARM1/ARM2) because
//	the native "ARM1"/"ARM2" codes are already claimed -- see [NOTES] in the
//	snippet.
//
//	*** PICKUP EFFECTS ARE OUT OF SCOPE. ***  This engine has NO Hexen class /
//	weapon / mana / key / puzzle subsystem, so these pickups cannot grant real
//	Hexen mana, keys, weapon assembly or puzzle progress.  On touch
//	(P_TouchHexenItem, dispatched by mobjtype from P_TouchSpecialThing before
//	the sprite switch) each is handled minimally:
//	  - Quartz Flask / Health Flask -> +25 HP   (DOOM P_GiveBody equivalent)
//	  - Mystic Urn                  -> +100 HP
//	  - armor pieces / Dragonskin Bracers -> DOOM armor (P_GiveArmor)
//	  - mana / keys / artifacts / puzzle / weapon pieces -> pocketed as a no-op
//	    (message only; there is no Hexen subsystem to grant into).
//	Every one is removed on touch so it can't re-trigger.  Without this handler
//	an unrecognised MF_SPECIAL sprite hits the I_Error default in p_inter.c.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "info.h"
#include "m_fixed.h"
#include "sounds.h"
#include "d_player.h"		// player_t
#include "p_mobj.h"
#include "hexen_items.h"

#define BRIGHT		32768		// FF_FULLBRIGHT frame bit

extern state_t   *states;
extern mobjinfo_t *mobjinfo;

// engine pieces we call (declared by hand, like heretic_items.c)
extern boolean	P_GiveBody  (player_t* player, int num);
extern boolean	P_GiveArmor (player_t* player, int armortype);


// ---------------------------------------------------------------------------
// Table fill helpers (identical style to files/heretic_items.c).
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
// looping spinning-icon spawnstate.  `ednum` is the real Hexen doomednum;
// `extra` adds flags (MF_NOTDMATCH for keys/puzzle/weapon pieces).  Radius/
// height are crispy's generic 20/16 (exact clip size is irrelevant for a
// touch-only pickup).
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


void Hexen_Items_Init (void)
{
    // ---- Spinning-icon spawnstates (single looping frame; frame 0). ----------
    // Fullbright items (mana, disc of repulsion, puzzle gears, class weapon
    // pieces) carry the BRIGHT bit as in crispy.  Like heretic_items.c the
    // multi-frame crispy animations are collapsed to one looping frame -- the
    // goal is faithful appearance, not the exact spin cycle.

    // Mana (crispy S_MANA1_1/S_MANA2_1/S_MANA3_1 -- all fullbright).
    ST (S_ZMANA1, SPR_ZMAN1, BRIGHT|0, 4, NULL, S_ZMANA1);	// blue mana
    ST (S_ZMANA2, SPR_ZMAN2, BRIGHT|0, 4, NULL, S_ZMANA2);	// green mana
    ST (S_ZMANA3, SPR_ZMAN3, BRIGHT|0, 4, NULL, S_ZMANA3);	// combined mana

    // Armor (crispy S_ARMOR_1..4, static).  1&2 reuse DOOM's SPR_ARM1/ARM2 as a
    // placeholder icon (native "ARM1"/"ARM2" codes are taken); 3&4 use native.
    ST (S_ZARMOR1, SPR_ARM1, 0, 4, NULL, S_ZARMOR1);	// mesh armor
    ST (S_ZARMOR2, SPR_ARM2, 0, 4, NULL, S_ZARMOR2);	// falcon shield
    ST (S_ZARMOR3, SPR_ZARM3, 0, 4, NULL, S_ZARMOR3);	// platinum helmet
    ST (S_ZARMOR4, SPR_ZARM4, 0, 4, NULL, S_ZARMOR4);	// amulet of warding

    // Keys (crispy S_KEY1..S_KEYB, static).
    ST (S_ZKEY1, SPR_ZKEY1, 0, 4, NULL, S_ZKEY1);
    ST (S_ZKEY2, SPR_ZKEY2, 0, 4, NULL, S_ZKEY2);
    ST (S_ZKEY3, SPR_ZKEY3, 0, 4, NULL, S_ZKEY3);
    ST (S_ZKEY4, SPR_ZKEY4, 0, 4, NULL, S_ZKEY4);
    ST (S_ZKEY5, SPR_ZKEY5, 0, 4, NULL, S_ZKEY5);
    ST (S_ZKEY6, SPR_ZKEY6, 0, 4, NULL, S_ZKEY6);
    ST (S_ZKEY7, SPR_ZKEY7, 0, 4, NULL, S_ZKEY7);
    ST (S_ZKEY8, SPR_ZKEY8, 0, 4, NULL, S_ZKEY8);
    ST (S_ZKEY9, SPR_ZKEY9, 0, 4, NULL, S_ZKEY9);
    ST (S_ZKEYA, SPR_ZKEYA, 0, 4, NULL, S_ZKEYA);
    ST (S_ZKEYB, SPR_ZKEYB, 0, 4, NULL, S_ZKEYB);

    // Artifacts.  PTN1/INVU/SPHL/TRCH/ATLP reuse the Heretic artifact sprites
    // already registered in this engine (identical icons).
    ST (S_ZHEALBOTTLE,     SPR_PTN1, 0, 4, NULL, S_ZHEALBOTTLE);	// quartz flask
    ST (S_ZHEALFLASK,      SPR_ZPTN2, 0, 4, NULL, S_ZHEALFLASK);	// health flask
    ST (S_ZINVUL,          SPR_INVU, 0, 4, NULL, S_ZINVUL);	// icon of the defender
    ST (S_ZSUPERHEAL,      SPR_SPHL, 0, 4, NULL, S_ZSUPERHEAL);	// mystic urn
    ST (S_ZTORCH,          SPR_TRCH, 0, 4, NULL, S_ZTORCH);	// torch
    ST (S_ZTELEPORT,       SPR_ATLP, 0, 4, NULL, S_ZTELEPORT);	// chaos device
    ST (S_ZTELEPORTOTHER,  SPR_ZTELO, 0, 4, NULL, S_ZTELEPORTOTHER);	// banishment device
    ST (S_ZSPEEDBOOTS,     SPR_ZSPED, 0, 4, NULL, S_ZSPEEDBOOTS);	// boots of speed
    ST (S_ZBOOSTMANA,      SPR_ZBMAN, 0, 4, NULL, S_ZBOOSTMANA);	// krater of might
    ST (S_ZBOOSTARMOR,     SPR_ZBRAC, 0, 4, NULL, S_ZBOOSTARMOR);	// dragonskin bracers
    ST (S_ZBLASTRADIUS,    SPR_ZBLST, BRIGHT|0, 4, NULL, S_ZBLASTRADIUS);	// disc of repulsion
    ST (S_ZHEALRADIUS,     SPR_ZHRAD, 0, 4, NULL, S_ZHEALRADIUS);	// mystic ambit incant

    // Puzzle pieces (crispy S_ARTIPUZZ*, static; gears are fullbright animated).
    ST (S_ZPUZZSKULL,     SPR_ZASKU, 0, 4, NULL, S_ZPUZZSKULL);		// yorick's skull
    ST (S_ZPUZZGEMBIG,    SPR_ZABGM, 0, 4, NULL, S_ZPUZZGEMBIG);		// heart of d'sparil
    ST (S_ZPUZZGEMRED,    SPR_ZAGMR, 0, 4, NULL, S_ZPUZZGEMRED);		// ruby planet
    ST (S_ZPUZZGEMGREEN1, SPR_ZAGMG, 0, 4, NULL, S_ZPUZZGEMGREEN1);	// emerald planet
    ST (S_ZPUZZGEMGREEN2, SPR_ZAGG2, 0, 4, NULL, S_ZPUZZGEMGREEN2);	// emerald planet 2
    ST (S_ZPUZZGEMBLUE1,  SPR_ZAGMB, 0, 4, NULL, S_ZPUZZGEMBLUE1);	// sapphire planet
    ST (S_ZPUZZGEMBLUE2,  SPR_ZAGB2, 0, 4, NULL, S_ZPUZZGEMBLUE2);	// sapphire planet 2
    ST (S_ZPUZZBOOK1,     SPR_ZABK1, 0, 4, NULL, S_ZPUZZBOOK1);		// daemon codex
    ST (S_ZPUZZBOOK2,     SPR_ZABK2, 0, 4, NULL, S_ZPUZZBOOK2);		// liber oscura
    ST (S_ZPUZZSKULL2,    SPR_ZASK2, 0, 4, NULL, S_ZPUZZSKULL2);		// glaive seal
    ST (S_ZPUZZFWEAPON,   SPR_ZAFWP, 0, 4, NULL, S_ZPUZZFWEAPON);	// fighter's key part
    ST (S_ZPUZZCWEAPON,   SPR_ZACWP, 0, 4, NULL, S_ZPUZZCWEAPON);	// cleric's key part
    ST (S_ZPUZZMWEAPON,   SPR_ZAMWP, 0, 4, NULL, S_ZPUZZMWEAPON);	// mage's key part
    ST (S_ZPUZZGEAR,      SPR_ZAGER, BRIGHT|0, 4, NULL, S_ZPUZZGEAR);	// clock gear (steel)
    ST (S_ZPUZZGEAR2,     SPR_ZAGR2, BRIGHT|0, 4, NULL, S_ZPUZZGEAR2);	// clock gear (bronze)
    ST (S_ZPUZZGEAR3,     SPR_ZAGR3, BRIGHT|0, 4, NULL, S_ZPUZZGEAR3);	// clock gear (steel/wood)
    ST (S_ZPUZZGEAR4,     SPR_ZAGR4, BRIGHT|0, 4, NULL, S_ZPUZZGEAR4);	// clock gear (bronze/wood)

    // Class weapon pieces (crispy S_FSWORD*/S_CHOLY*/S_MSTAFF*, fullbright).
    ST (S_ZFSWORD1, SPR_ZWFR1, BRIGHT|0, 4, NULL, S_ZFSWORD1);	// quietus hilt
    ST (S_ZFSWORD2, SPR_ZWFR2, BRIGHT|0, 4, NULL, S_ZFSWORD2);	// quietus cross
    ST (S_ZFSWORD3, SPR_ZWFR3, BRIGHT|0, 4, NULL, S_ZFSWORD3);	// quietus blade
    ST (S_ZCHOLY1,  SPR_ZWCH1, BRIGHT|0, 4, NULL, S_ZCHOLY1);	// wraithverge shaft
    ST (S_ZCHOLY2,  SPR_ZWCH2, BRIGHT|0, 4, NULL, S_ZCHOLY2);	// wraithverge cross
    ST (S_ZCHOLY3,  SPR_ZWCH3, BRIGHT|0, 4, NULL, S_ZCHOLY3);	// wraithverge arc
    ST (S_ZMSTAFF1, SPR_ZWMS1, BRIGHT|0, 4, NULL, S_ZMSTAFF1);	// bloodscourge shaft
    ST (S_ZMSTAFF2, SPR_ZWMS2, BRIGHT|0, 4, NULL, S_ZMSTAFF2);	// bloodscourge head
    ST (S_ZMSTAFF3, SPR_ZWMS3, BRIGHT|0, 4, NULL, S_ZMSTAFF3);	// bloodscourge stub

    // ---- Map-placeable pickup actors (crispy Hexen doomednums) ---------------
    // Mana (crispy MT_MANA1=122, MT_MANA2=124, MT_MANA3=8004).
    Item (MT_ZMANA1, 122,  S_ZMANA1, 0);
    Item (MT_ZMANA2, 124,  S_ZMANA2, 0);
    Item (MT_ZMANA3, 8004, S_ZMANA3, 0);

    // Armor (crispy MT_ARMOR_1..4 = 8005..8008).
    Item (MT_ZARMOR1, 8005, S_ZARMOR1, 0);
    Item (MT_ZARMOR2, 8006, S_ZARMOR2, 0);
    Item (MT_ZARMOR3, 8007, S_ZARMOR3, 0);
    Item (MT_ZARMOR4, 8008, S_ZARMOR4, 0);

    // Keys (crispy MT_KEY1..KEYA = 8030..8039, MT_KEYB = 8200).  Quest items ->
    // MF_NOTDMATCH so they don't spawn in deathmatch.
    Item (MT_ZKEY1, 8030, S_ZKEY1, MF_NOTDMATCH);
    Item (MT_ZKEY2, 8031, S_ZKEY2, MF_NOTDMATCH);
    Item (MT_ZKEY3, 8032, S_ZKEY3, MF_NOTDMATCH);
    Item (MT_ZKEY4, 8033, S_ZKEY4, MF_NOTDMATCH);
    Item (MT_ZKEY5, 8034, S_ZKEY5, MF_NOTDMATCH);
    Item (MT_ZKEY6, 8035, S_ZKEY6, MF_NOTDMATCH);
    Item (MT_ZKEY7, 8036, S_ZKEY7, MF_NOTDMATCH);
    Item (MT_ZKEY8, 8037, S_ZKEY8, MF_NOTDMATCH);
    Item (MT_ZKEY9, 8038, S_ZKEY9, MF_NOTDMATCH);
    Item (MT_ZKEYA, 8039, S_ZKEYA, MF_NOTDMATCH);
    Item (MT_ZKEYB, 8200, S_ZKEYB, MF_NOTDMATCH);

    // Artifacts (crispy doomednums).
    Item (MT_ZHEALBOTTLE,    81,    S_ZHEALBOTTLE,    0);	// quartz flask
    Item (MT_ZHEALFLASK,     82,    S_ZHEALFLASK,     0);	// health flask
    Item (MT_ZINVUL,         84,    S_ZINVUL,         0);	// icon of the defender
    Item (MT_ZSUPERHEAL,     32,    S_ZSUPERHEAL,     0);	// mystic urn
    Item (MT_ZTORCH,         33,    S_ZTORCH,         0);	// torch
    Item (MT_ZTELEPORT,      36,    S_ZTELEPORT,      0);	// chaos device
    Item (MT_ZTELEPORTOTHER, 10040, S_ZTELEPORTOTHER, 0);	// banishment device
    Item (MT_ZSPEEDBOOTS,    8002,  S_ZSPEEDBOOTS,    0);	// boots of speed
    Item (MT_ZBOOSTMANA,     8003,  S_ZBOOSTMANA,     0);	// krater of might
    Item (MT_ZBOOSTARMOR,    8041,  S_ZBOOSTARMOR,    0);	// dragonskin bracers
    Item (MT_ZBLASTRADIUS,   10110, S_ZBLASTRADIUS,   0);	// disc of repulsion
    Item (MT_ZHEALRADIUS,    10120, S_ZHEALRADIUS,    0);	// mystic ambit incant

    // Puzzle pieces (crispy MT_ARTIPUZZ* = 9002..9021).
    Item (MT_ZPUZZSKULL,     9002, S_ZPUZZSKULL,     MF_NOTDMATCH);
    Item (MT_ZPUZZGEMBIG,    9003, S_ZPUZZGEMBIG,    MF_NOTDMATCH);
    Item (MT_ZPUZZGEMRED,    9004, S_ZPUZZGEMRED,    MF_NOTDMATCH);
    Item (MT_ZPUZZGEMGREEN1, 9005, S_ZPUZZGEMGREEN1, MF_NOTDMATCH);
    Item (MT_ZPUZZGEMGREEN2, 9009, S_ZPUZZGEMGREEN2, MF_NOTDMATCH);
    Item (MT_ZPUZZGEMBLUE1,  9006, S_ZPUZZGEMBLUE1,  MF_NOTDMATCH);
    Item (MT_ZPUZZGEMBLUE2,  9010, S_ZPUZZGEMBLUE2,  MF_NOTDMATCH);
    Item (MT_ZPUZZBOOK1,     9007, S_ZPUZZBOOK1,     MF_NOTDMATCH);
    Item (MT_ZPUZZBOOK2,     9008, S_ZPUZZBOOK2,     MF_NOTDMATCH);
    Item (MT_ZPUZZSKULL2,    9014, S_ZPUZZSKULL2,    MF_NOTDMATCH);
    Item (MT_ZPUZZFWEAPON,   9015, S_ZPUZZFWEAPON,   MF_NOTDMATCH);
    Item (MT_ZPUZZCWEAPON,   9016, S_ZPUZZCWEAPON,   MF_NOTDMATCH);
    Item (MT_ZPUZZMWEAPON,   9017, S_ZPUZZMWEAPON,   MF_NOTDMATCH);
    Item (MT_ZPUZZGEAR,      9018, S_ZPUZZGEAR,      MF_NOTDMATCH);
    Item (MT_ZPUZZGEAR2,     9019, S_ZPUZZGEAR2,     MF_NOTDMATCH);
    Item (MT_ZPUZZGEAR3,     9020, S_ZPUZZGEAR3,     MF_NOTDMATCH);
    Item (MT_ZPUZZGEAR4,     9021, S_ZPUZZGEAR4,     MF_NOTDMATCH);

    // Class weapon pieces (crispy MT_FW_/CW_/MW_ = 12,13,16 / 18,19,20 / 21,22,23).
    // NOTE: these low doomednums collide with the Heretic ammo pickups in
    // files/heretic_items.c and with DOOM's own low editor numbers -- see [NOTES].
    Item (MT_ZFSWORD1, 12, S_ZFSWORD1, MF_NOTDMATCH);	// quietus (fighter)
    Item (MT_ZFSWORD2, 13, S_ZFSWORD2, MF_NOTDMATCH);
    Item (MT_ZFSWORD3, 16, S_ZFSWORD3, MF_NOTDMATCH);
    Item (MT_ZCHOLY1,  18, S_ZCHOLY1,  MF_NOTDMATCH);	// wraithverge (cleric)
    Item (MT_ZCHOLY2,  19, S_ZCHOLY2,  MF_NOTDMATCH);
    Item (MT_ZCHOLY3,  20, S_ZCHOLY3,  MF_NOTDMATCH);
    Item (MT_ZMSTAFF1, 21, S_ZMSTAFF1, MF_NOTDMATCH);	// bloodscourge (mage)
    Item (MT_ZMSTAFF2, 22, S_ZMSTAFF2, MF_NOTDMATCH);
    Item (MT_ZMSTAFF3, 23, S_ZMSTAFF3, MF_NOTDMATCH);
}


// ---------------------------------------------------------------------------
// A placed/spawned MT_Z* item was touched.  Returns true if handled (so
// P_TouchSpecialThing removes it + plays the pickup sound and stops), false if
// the item is not one of ours.  See the EFFECTS note in the file header:
// health/armor get the clean DOOM equivalent; everything else is a no-op pocket.
// ---------------------------------------------------------------------------
// Pocket a Hexen artifact into the SHARED artifact inventory (player_t.inventory[] /
// invslot -- the same store the native Heretic bar (st_stuff.c) and the HU_Inventory_Drawer
// show, and that P_UseArtifact/ApplyHereticArtifact act on).  Hexen's core artifacts are
// the same items as Heretic's, so they map onto the existing artitype_t slots; used later
// with the inventory key.  (Mirrors P_TouchHereticArtifact's pocket logic.)
static boolean Pocket (player_t* player, artitype_t a)
{
    if (player->inventory[a] < MAXARTICOUNT)
	player->inventory[a]++;
    if (player->invslot == arti_none)
	player->invslot = a;
    return true;
}

boolean P_TouchHexenItem (player_t* player, mobj_t* special)
{
    if (!player)
	return false;

    switch (special->type)
    {
      // ---- mana (no Hexen mana system -> cosmetic pickup) ----
      case MT_ZMANA1: player->message = "BLUE MANA";     return true;
      case MT_ZMANA2: player->message = "GREEN MANA";    return true;
      case MT_ZMANA3: player->message = "COMBINED MANA"; return true;

      // ---- armor -> closest DOOM equivalent ----
      case MT_ZARMOR1:
	P_GiveArmor (player, 1);
	player->message = "MESH ARMOR";
	return true;
      case MT_ZARMOR2:
	P_GiveArmor (player, 1);
	player->message = "FALCON SHIELD";
	return true;
      case MT_ZARMOR3:
	P_GiveArmor (player, 1);
	player->message = "PLATINUM HELMET";
	return true;
      case MT_ZARMOR4:
	P_GiveArmor (player, 1);
	player->message = "AMULET OF WARDING";
	return true;
      case MT_ZBOOSTARMOR:
	P_GiveArmor (player, 2);
	player->message = "DRAGONSKIN BRACERS";
	return true;

      // ---- keys (no Hexen locked-door system here -> cosmetic pickup) ----
      case MT_ZKEY1: player->message = "STEEL KEY";     return true;
      case MT_ZKEY2: player->message = "CAVE KEY";      return true;
      case MT_ZKEY3: player->message = "AXE KEY";       return true;
      case MT_ZKEY4: player->message = "FIRE KEY";      return true;
      case MT_ZKEY5: player->message = "EMERALD KEY";   return true;
      case MT_ZKEY6: player->message = "DUNGEON KEY";   return true;
      case MT_ZKEY7: player->message = "SILVER KEY";    return true;
      case MT_ZKEY8: player->message = "RUSTED KEY";    return true;
      case MT_ZKEY9: player->message = "HORN KEY";      return true;
      case MT_ZKEYA: player->message = "SWAMP KEY";     return true;
      case MT_ZKEYB: player->message = "CASTLE KEY";    return true;

      // ---- inventory artifacts -> pocketed into the shared inventory[] (used later
      //      with the inventory key), mapped onto the equivalent Heretic artitype_t
      //      slot.  Hexen's Quartz Flask/Mystic Urn/Torch/Icon-of-Defender/Chaos Device
      //      ARE the same items as Heretic's flask/urn/torch/ring/chaos. ----
      case MT_ZHEALBOTTLE:    player->message = "QUARTZ FLASK";         return Pocket (player, h_arti_flask);
      case MT_ZHEALFLASK:     player->message = "HEALTH FLASK";         return Pocket (player, h_arti_flask);
      case MT_ZSUPERHEAL:     player->message = "MYSTIC URN";           return Pocket (player, h_arti_urn);
      case MT_ZINVUL:         player->message = "ICON OF THE DEFENDER"; return Pocket (player, h_arti_ring);
      case MT_ZTORCH:         player->message = "TORCH";                return Pocket (player, h_arti_torch);
      case MT_ZTELEPORT:      player->message = "CHAOS DEVICE";         return Pocket (player, h_arti_chaos);

      // ---- Hexen-unique artifacts (no matching artitype_t slot / effect yet) --
      //      still pocket the ones with a usable analog; the rest are cosmetic. ----
      case MT_ZTELEPORTOTHER: player->message = "BANISHMENT DEVICE";    return true;
      case MT_ZSPEEDBOOTS:    player->message = "BOOTS OF SPEED";       return true;
      case MT_ZBOOSTMANA:     player->message = "KRATER OF MIGHT";      return true;
      case MT_ZBLASTRADIUS:   player->message = "DISC OF REPULSION";    return true;
      case MT_ZHEALRADIUS:    player->message = "MYSTIC AMBIT INCANT";  return true;

      // ---- puzzle pieces (no Hexen puzzle system -> cosmetic pickup) ----
      case MT_ZPUZZSKULL:     player->message = "YORICK'S SKULL";    return true;
      case MT_ZPUZZGEMBIG:    player->message = "HEART OF D'SPARIL"; return true;
      case MT_ZPUZZGEMRED:    player->message = "RUBY PLANET";       return true;
      case MT_ZPUZZGEMGREEN1:
      case MT_ZPUZZGEMGREEN2: player->message = "EMERALD PLANET";    return true;
      case MT_ZPUZZGEMBLUE1:
      case MT_ZPUZZGEMBLUE2:  player->message = "SAPPHIRE PLANET";   return true;
      case MT_ZPUZZBOOK1:     player->message = "DAEMON CODEX";      return true;
      case MT_ZPUZZBOOK2:     player->message = "LIBER OSCURA";      return true;
      case MT_ZPUZZSKULL2:    player->message = "GLAIVE SEAL";       return true;
      case MT_ZPUZZFWEAPON:   player->message = "FLAME MASK";        return true;
      case MT_ZPUZZCWEAPON:   player->message = "CLOCK GEAR";        return true;
      case MT_ZPUZZMWEAPON:   player->message = "PUZZLE PIECE";      return true;
      case MT_ZPUZZGEAR:
      case MT_ZPUZZGEAR2:
      case MT_ZPUZZGEAR3:
      case MT_ZPUZZGEAR4:     player->message = "CLOCK GEAR";        return true;

      // ---- class weapon pieces (no Hexen weapon assembly -> cosmetic pickup) ----
      case MT_ZFSWORD1:
      case MT_ZFSWORD2:
      case MT_ZFSWORD3: player->message = "SEGMENT OF QUIETUS";      return true;
      case MT_ZCHOLY1:
      case MT_ZCHOLY2:
      case MT_ZCHOLY3:  player->message = "SEGMENT OF WRAITHVERGE";  return true;
      case MT_ZMSTAFF1:
      case MT_ZMSTAFF2:
      case MT_ZMSTAFF3: player->message = "SEGMENT OF BLOODSCOURGE"; return true;

      default:
	return false;			// not one of ours
    }
}


// ---------------------------------------------------------------------------
// Console-summon name -> MT_Z* type (or -1).  Mirrors Hexen_TypeByName in
// files/hexen.c; the orchestrator can chain this from there / from C_MobjByName.
// ---------------------------------------------------------------------------
int Hexen_ItemTypeByName (const char* name)
{
    if (!name || !name[0]) return -1;

    // mana
    if (!strcmp (name, "bluemana"))     return MT_ZMANA1;
    if (!strcmp (name, "greenmana"))    return MT_ZMANA2;
    if (!strcmp (name, "combinedmana")) return MT_ZMANA3;
    // armor
    if (!strcmp (name, "mesharmor"))    return MT_ZARMOR1;
    if (!strcmp (name, "falconshield")) return MT_ZARMOR2;
    if (!strcmp (name, "platinumhelm")) return MT_ZARMOR3;
    if (!strcmp (name, "amulet"))       return MT_ZARMOR4;
    // keys
    if (!strcmp (name, "steelkey"))     return MT_ZKEY1;
    if (!strcmp (name, "cavekey"))      return MT_ZKEY2;
    if (!strcmp (name, "axekey"))       return MT_ZKEY3;
    if (!strcmp (name, "firekey"))      return MT_ZKEY4;
    if (!strcmp (name, "emeraldkey"))   return MT_ZKEY5;
    if (!strcmp (name, "dungeonkey"))   return MT_ZKEY6;
    if (!strcmp (name, "silverkey"))    return MT_ZKEY7;
    if (!strcmp (name, "rustedkey"))    return MT_ZKEY8;
    if (!strcmp (name, "hornkey"))      return MT_ZKEY9;
    if (!strcmp (name, "swampkey"))     return MT_ZKEYA;
    if (!strcmp (name, "castlekey"))    return MT_ZKEYB;
    // artifacts
    if (!strcmp (name, "quartzflask"))  return MT_ZHEALBOTTLE;
    if (!strcmp (name, "healthflask"))  return MT_ZHEALFLASK;
    if (!strcmp (name, "icondefender") || !strcmp (name, "hexeninvul")) return MT_ZINVUL;
    if (!strcmp (name, "mysticurn"))    return MT_ZSUPERHEAL;
    if (!strcmp (name, "hexentorch"))   return MT_ZTORCH;
    if (!strcmp (name, "chaosdevice"))  return MT_ZTELEPORT;
    if (!strcmp (name, "banishment") || !strcmp (name, "teleportother")) return MT_ZTELEPORTOTHER;
    if (!strcmp (name, "speedboots"))   return MT_ZSPEEDBOOTS;
    if (!strcmp (name, "krater") || !strcmp (name, "boostmana")) return MT_ZBOOSTMANA;
    if (!strcmp (name, "bracers") || !strcmp (name, "boostarmor")) return MT_ZBOOSTARMOR;
    if (!strcmp (name, "discofrepulsion") || !strcmp (name, "blastradius")) return MT_ZBLASTRADIUS;
    if (!strcmp (name, "ambitincant") || !strcmp (name, "healradius")) return MT_ZHEALRADIUS;
    // puzzle
    if (!strcmp (name, "puzzskull"))    return MT_ZPUZZSKULL;
    if (!strcmp (name, "puzzgembig"))   return MT_ZPUZZGEMBIG;
    if (!strcmp (name, "puzzgemred"))   return MT_ZPUZZGEMRED;
    if (!strcmp (name, "puzzgemgreen")) return MT_ZPUZZGEMGREEN1;
    if (!strcmp (name, "puzzgemblue"))  return MT_ZPUZZGEMBLUE1;
    if (!strcmp (name, "puzzbook1"))    return MT_ZPUZZBOOK1;
    if (!strcmp (name, "puzzbook2"))    return MT_ZPUZZBOOK2;
    if (!strcmp (name, "puzzskull2"))   return MT_ZPUZZSKULL2;
    if (!strcmp (name, "puzzfweapon"))  return MT_ZPUZZFWEAPON;
    if (!strcmp (name, "puzzcweapon"))  return MT_ZPUZZCWEAPON;
    if (!strcmp (name, "puzzmweapon"))  return MT_ZPUZZMWEAPON;
    if (!strcmp (name, "puzzgear"))     return MT_ZPUZZGEAR;
    // class weapon pieces
    if (!strcmp (name, "quietus1"))     return MT_ZFSWORD1;
    if (!strcmp (name, "quietus2"))     return MT_ZFSWORD2;
    if (!strcmp (name, "quietus3"))     return MT_ZFSWORD3;
    if (!strcmp (name, "wraithverge1")) return MT_ZCHOLY1;
    if (!strcmp (name, "wraithverge2")) return MT_ZCHOLY2;
    if (!strcmp (name, "wraithverge3")) return MT_ZCHOLY3;
    if (!strcmp (name, "bloodscourge1")) return MT_ZMSTAFF1;
    if (!strcmp (name, "bloodscourge2")) return MT_ZMSTAFF2;
    if (!strcmp (name, "bloodscourge3")) return MT_ZMSTAFF3;

    return -1;
}
