// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Additive STRIFE DECORATION / SCENERY actors in the DOOM engine.
//	Ported from strife-ve's strife/info.c (states[] + mobjinfo[]), adapted
//	to this engine's state_t / mobjinfo_t layout and DOOM MF_* flag set.
//	Same additive approach as heretic_deco.c: Strife_Deco_Init() fills the
//	runtime states[] + mobjinfo[] slots reserved at the tail of
//	statenum_t/mobjtype_t/spritenum_t (info.h, via strife_*.inc), and sets
//	mobjinfo[MT_S_*].doomednum so P_StrifeThingType resolves map things.
//
//	Naming transform applied throughout (see STRIFE_PORT_GUIDE.md):
//	  Strife MT_XXX -> MT_S_XXX, S_XXX -> S_S_XXX, SPR_XXX -> SPR_S_XXX,
//	  sfx_xxx -> sfx_s_xxx.
//
//	Coverage (decorations = doomednum >= 0, not MF_COUNTKILL, not MF_SPECIAL,
//	plus a few decorative sub-actors with doomednum -1):
//	  * statues / pillars / rubble / cave formations (PILLAR*, CAVE*, ROCK*,
//	    RUBBLE*, statues STAT/DSTA, coupling)
//	  * plants / trees / bushes (TREE*, BUSH, SHRB, stalks)
//	  * tech props (klaxon, gate/piston, computer, water, mug, monitor)
//	  * lamps / candles / braziers / torches / light sources (LIGHT*)
//	  * explodables ("thingstoblowup": wooden/metal barrels MISC_05/06,
//	    the power crystal, gate, computer) -- shootable, explode on death
//	  * gibs / meat / dead bodies (GIBS, DEADTHING1-6, MEAT, BIO1/BIO2)
//	  * ambient droplets / splashes (DRIP, CDRP, SPLH, WTFT, JUNK, BURNDROP)
//
//	SIMPLIFICATIONS (no matching engine codepointer -- the actor still
//	spawns and animates, per the port guide "do not invent AI" rule):
//	  - Strife cosmetic/AI codepointers are dropped to NULL: A_ActiveSound
//	    (ambient fire/water loops go silent), A_Listen / A_ClaxonBlare
//	    (klaxon just spins), A_ZombieInSpecialSector, A_QuestMsg,
//	    A_SpawnSparkPuff, A_BodyParts, A_CrystalRadiusAtk / A_CrystalExplode /
//	    A_ExtraLightOff (crystal blast uses A_Explode instead), A_DeathExplode2
//	    (metal barrel uses A_Explode).
//	  - Reused engine codepointers ONLY: A_Scream, A_Fall, A_Explode, A_Pain.
//	  - Strife-specific flags2 (MF2_DRAWBILLBOARD, MF2_MARKDECAL, ...) dropped.
//	    Strife crashstate has no engine field -> dropped. raisestate = 0.
//	  - DEADTHING1/5/6 point their spawnstate at monster corpse frames
//	    (S_S_ROB2_29 / S_S_ROB1_25 / S_S_HMN1_31) that the monster installer
//	    owns -- referenced only, NOT filled here.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "m_fixed.h"
#include "info.h"
#include "sounds.h"
#include "p_mobj.h"	// MF_* flags, mobj_t
#include "r_defs.h"	// (known gotcha: pulls in struct defs info.h leans on)

#define BRIGHT	32768

extern state_t   *states;
extern mobjinfo_t *mobjinfo;

// Engine action codepointers reused verbatim (declared by hand, like heretic_deco.c).
extern void	A_Scream (mobj_t*);
extern void	A_Explode (mobj_t*);
extern void	A_Fall (mobj_t*);
extern void	A_XScream (mobj_t*);
extern void	A_Pain (mobj_t*);

// ---------------------------------------------------------------------------
// Table fill helpers
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

// Common decoration mobjinfo filler. Constant fields (reactiontime, damage,
// active/see/attack sounds, mele/missile/xdeath/raise states, flags2) default.
static void MI (mobjtype_t t, int dn, statenum_t spawn, int health,
		statenum_t death, int deathsnd,
		statenum_t pain, int painchance, int painsnd,
		int speed, int radius, int height, int mass, int flags)
{
    mobjinfo_t*	m = &mobjinfo[t];

    m->doomednum    = dn;
    m->spawnstate   = spawn;
    m->spawnhealth  = health;
    m->seestate     = S_NULL;
    m->seesound     = sfx_None;
    m->reactiontime = 8;
    m->attacksound  = sfx_None;
    m->painstate    = pain;
    m->painchance   = painchance;
    m->painsound    = painsnd;
    m->meleestate   = S_NULL;
    m->missilestate = S_NULL;
    m->deathstate   = death;
    m->xdeathstate  = S_NULL;
    m->deathsound   = deathsnd;
    m->speed        = speed;
    m->radius       = radius;
    m->height       = height;
    m->mass         = mass;
    m->damage       = 0;
    m->activesound  = sfx_None;
    m->flags        = flags;
    m->raisestate   = 0;
    m->flags2       = 0;
}

void Strife_Deco_Init (void)
{
    mobjinfo_t*	m;

    // ====================================================================
    // Tech props (klaxon, gate/piston, computer, water, mug)
    // ====================================================================

    // Klaxon (MT_MISC_01) -- alarm; A_Listen/A_ClaxonBlare dropped -> spins.
    ST (S_S_KLAX_00, SPR_S_KLAX, 0,  5, NULL, S_S_KLAX_00);
    ST (S_S_KLAX_01, SPR_S_KLAX, 1,  6, NULL, S_S_KLAX_02);
    ST (S_S_KLAX_02, SPR_S_KLAX, 2, 60, NULL, S_S_KLAX_01);
    MI (MT_S_MISC_01, 24, S_S_KLAX_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100,
	MF_NOBLOCKMAP|MF_SPAWNCEILING|MF_NOGRAVITY);
    m = &mobjinfo[MT_S_MISC_01]; m->seestate = S_S_KLAX_01; m->reactiontime = 60;

    // Gate / piston (MT_GATE) -- shootable, explodes.
    ST (S_S_PSTN_00, SPR_S_PSTN, 0, 8, NULL, S_S_PSTN_01);
    ST (S_S_PSTN_01, SPR_S_PSTN, 1, 8, NULL, S_S_PSTN_00);
    ST (S_S_PSTN_02, SPR_S_PSTN, BRIGHT+0, 4, (actionf_p1)A_Scream, S_S_PSTN_03);
    ST (S_S_PSTN_03, SPR_S_PSTN, BRIGHT+1, 4, (actionf_p1)A_Fall,   S_S_PSTN_04);
    ST (S_S_PSTN_04, SPR_S_PSTN, BRIGHT+2, 4, NULL, S_S_PSTN_05);	// A_QuestMsg
    ST (S_S_PSTN_05, SPR_S_PSTN, BRIGHT+3, 4, NULL, S_S_PSTN_06);	// A_SpawnSparkPuff
    ST (S_S_PSTN_06, SPR_S_PSTN, BRIGHT+4, 4, NULL, S_S_PSTN_07);	// A_BodyParts
    ST (S_S_PSTN_07, SPR_S_PSTN, BRIGHT+5, 4, NULL, S_S_PSTN_08);
    ST (S_S_PSTN_08, SPR_S_PSTN, BRIGHT+6, 4, NULL, S_S_PSTN_09);	// A_SpawnSparkPuff
    ST (S_S_PSTN_09, SPR_S_PSTN, 7, 4, NULL, S_S_PSTN_10);
    ST (S_S_PSTN_10, SPR_S_PSTN, 8, -1, NULL, S_NULL);
    MI (MT_S_GATE, 45, S_S_PSTN_00, 100, S_S_PSTN_02, sfx_s_explod,
	S_NULL, 0, sfx_None, 16, 20*FRACUNIT, 76*FRACUNIT, 10000000,
	MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD);

    // Computer (MT_COMPUTER) -- shootable, explodes.
    ST (S_S_SECR_00, SPR_S_SECR, BRIGHT+0, 4, NULL, S_S_SECR_01);
    ST (S_S_SECR_01, SPR_S_SECR, BRIGHT+1, 4, NULL, S_S_SECR_02);
    ST (S_S_SECR_02, SPR_S_SECR, BRIGHT+2, 4, NULL, S_S_SECR_03);
    ST (S_S_SECR_03, SPR_S_SECR, BRIGHT+3, 4, NULL, S_S_SECR_00);
    ST (S_S_SECR_04, SPR_S_SECR, BRIGHT+4, 5, NULL, S_S_SECR_05);	// A_SpawnSparkPuff
    ST (S_S_SECR_05, SPR_S_SECR, BRIGHT+5, 5, (actionf_p1)A_Fall, S_S_SECR_06);
    ST (S_S_SECR_06, SPR_S_SECR, BRIGHT+6, 5, NULL, S_S_SECR_07);	// A_QuestMsg
    ST (S_S_SECR_07, SPR_S_SECR, BRIGHT+7, 5, NULL, S_S_SECR_08);	// A_BodyParts
    ST (S_S_SECR_08, SPR_S_SECR, BRIGHT+8, 5, NULL, S_S_SECR_09);	// A_SpawnSparkPuff
    ST (S_S_SECR_09, SPR_S_SECR, 9,  5, NULL, S_S_SECR_10);
    ST (S_S_SECR_10, SPR_S_SECR, 10, 5, NULL, S_S_SECR_11);		// A_SpawnSparkPuff
    ST (S_S_SECR_11, SPR_S_SECR, 11, 5, NULL, S_S_SECR_12);
    ST (S_S_SECR_12, SPR_S_SECR, 12, 5, NULL, S_S_SECR_13);		// A_SpawnSparkPuff
    ST (S_S_SECR_13, SPR_S_SECR, 13, 5, NULL, S_S_SECR_14);
    ST (S_S_SECR_14, SPR_S_SECR, 14, 5, NULL, S_S_SECR_15);		// A_SpawnSparkPuff
    ST (S_S_SECR_15, SPR_S_SECR, 15, -1, NULL, S_NULL);
    MI (MT_S_COMPUTER, 182, S_S_SECR_00, 80, S_S_SECR_04, sfx_s_explod,
	S_NULL, 0, sfx_None, 27, 26*FRACUNIT, 128*FRACUNIT, 100000,
	MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD);

    // Water (MT_MISC_22) and mug (MT_MISC_11) -- inert scenery.
    ST (S_S_WATR_00, SPR_S_WATR, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_22, 2014, S_S_WATR_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    ST (S_S_MUGG_00, SPR_S_MUGG, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_11, 164, S_S_MUGG_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);

    // ====================================================================
    // Power crystal (MT_POWER_CRYSTAL) -- shootable, big blast on death.
    // A_CrystalRadiusAtk -> A_Explode (the two blast frames); rest dropped.
    // ====================================================================
    ST (S_S_CRYS_00, SPR_S_CRYS, 0, 16, NULL, S_S_CRYS_01);
    ST (S_S_CRYS_01, SPR_S_CRYS, 1,  5, NULL, S_S_CRYS_02);
    ST (S_S_CRYS_02, SPR_S_CRYS, 2,  4, NULL, S_S_CRYS_03);
    ST (S_S_CRYS_03, SPR_S_CRYS, 3,  4, NULL, S_S_CRYS_04);
    ST (S_S_CRYS_04, SPR_S_CRYS, 4,  4, NULL, S_S_CRYS_05);
    ST (S_S_CRYS_05, SPR_S_CRYS, 5,  4, NULL, S_S_CRYS_00);
    ST (S_S_BOOM_00, SPR_S_BOOM, BRIGHT+0,  1, (actionf_p1)A_Explode, S_S_BOOM_01);
    ST (S_S_BOOM_01, SPR_S_BOOM, BRIGHT+1,  3, NULL, S_S_BOOM_02);	// A_QuestMsg
    ST (S_S_BOOM_02, SPR_S_BOOM, BRIGHT+2,  2, NULL, S_S_BOOM_03);	// A_CrystalExplode
    ST (S_S_BOOM_03, SPR_S_BOOM, BRIGHT+3,  3, NULL, S_S_BOOM_04);
    ST (S_S_BOOM_04, SPR_S_BOOM, BRIGHT+4,  3, NULL, S_S_BOOM_05);
    ST (S_S_BOOM_05, SPR_S_BOOM, BRIGHT+5,  3, NULL, S_S_BOOM_06);
    ST (S_S_BOOM_06, SPR_S_BOOM, BRIGHT+6,  3, NULL, S_S_BOOM_07);
    ST (S_S_BOOM_07, SPR_S_BOOM, BRIGHT+7,  1, (actionf_p1)A_Explode, S_S_BOOM_08);
    ST (S_S_BOOM_08, SPR_S_BOOM, BRIGHT+8,  3, NULL, S_S_BOOM_09);
    ST (S_S_BOOM_09, SPR_S_BOOM, BRIGHT+9,  3, NULL, S_S_BOOM_10);
    ST (S_S_BOOM_10, SPR_S_BOOM, BRIGHT+10, 3, NULL, S_S_BOOM_11);
    ST (S_S_BOOM_11, SPR_S_BOOM, BRIGHT+11, 3, NULL, S_S_BOOM_12);
    ST (S_S_BOOM_12, SPR_S_BOOM, BRIGHT+12, 3, NULL, S_S_BOOM_13);
    ST (S_S_BOOM_13, SPR_S_BOOM, BRIGHT+13, 3, NULL, S_S_BOOM_14);
    ST (S_S_BOOM_14, SPR_S_BOOM, BRIGHT+14, 3, NULL, S_S_BOOM_15);
    ST (S_S_BOOM_15, SPR_S_BOOM, BRIGHT+15, 3, NULL, S_S_BOOM_16);
    ST (S_S_BOOM_16, SPR_S_BOOM, BRIGHT+16, 3, NULL, S_S_BOOM_17);
    ST (S_S_BOOM_17, SPR_S_BOOM, BRIGHT+17, 3, NULL, S_S_BOOM_18);
    ST (S_S_BOOM_18, SPR_S_BOOM, BRIGHT+18, 3, NULL, S_S_BOOM_19);
    ST (S_S_BOOM_19, SPR_S_BOOM, BRIGHT+19, 3, NULL, S_S_BOOM_20);
    ST (S_S_BOOM_20, SPR_S_BOOM, BRIGHT+20, 3, NULL, S_S_BOOM_21);	// A_ExtraLightOff
    ST (S_S_BOOM_21, SPR_S_BOOM, BRIGHT+21, 3, NULL, S_S_BOOM_22);
    ST (S_S_BOOM_22, SPR_S_BOOM, BRIGHT+22, 3, NULL, S_S_BOOM_23);
    ST (S_S_BOOM_23, SPR_S_BOOM, BRIGHT+23, 3, NULL, S_S_BOOM_24);
    ST (S_S_BOOM_24, SPR_S_BOOM, BRIGHT+24, 3, NULL, S_NULL);
    MI (MT_S_POWER_CRYSTAL, 92, S_S_CRYS_00, 50, S_S_BOOM_00, sfx_s_explod,
	S_NULL, 0, sfx_None, 14, 20*FRACUNIT, 16*FRACUNIT, 99999999,
	MF_SOLID|MF_SHOOTABLE|MF_NOGRAVITY|MF_NOBLOOD);

    // ====================================================================
    // Explodable barrels (MT_MISC_05 wooden, MT_MISC_06 metal)
    // ====================================================================
    ST (S_S_BARW_00, SPR_S_BARW, 0, -1, NULL, S_NULL);
    ST (S_S_BARW_01, SPR_S_BARW, 1, 2, (actionf_p1)A_Scream, S_S_BARW_02);
    ST (S_S_BARW_02, SPR_S_BARW, 2, 2, NULL, S_S_BARW_03);
    ST (S_S_BARW_03, SPR_S_BARW, 3, 2, (actionf_p1)A_Fall, S_S_BARW_04);
    ST (S_S_BARW_04, SPR_S_BARW, 4, 2, NULL, S_S_BARW_05);
    ST (S_S_BARW_05, SPR_S_BARW, 5, 2, NULL, S_S_BARW_06);
    ST (S_S_BARW_06, SPR_S_BARW, 6, 2, NULL, S_S_BARW_07);
    ST (S_S_BARW_07, SPR_S_BARW, 7, -1, NULL, S_NULL);
    MI (MT_S_MISC_05, 82, S_S_BARW_00, 10, S_S_BARW_01, sfx_s_wbrldt,
	S_NULL, 0, sfx_None, 0, 10*FRACUNIT, 32*FRACUNIT, 100,
	MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD);

    ST (S_S_BART_00, SPR_S_BART, 0, -1, NULL, S_NULL);
    ST (S_S_BART_01, SPR_S_BART, BRIGHT+1, 2, (actionf_p1)A_Scream, S_S_BART_02);
    ST (S_S_BART_02, SPR_S_BART, BRIGHT+2, 2, NULL, S_S_BART_03);
    ST (S_S_BART_03, SPR_S_BART, BRIGHT+3, 2, NULL, S_S_BART_04);
    ST (S_S_BART_04, SPR_S_BART, BRIGHT+4, 2, (actionf_p1)A_Fall, S_S_BART_05);
    ST (S_S_BART_05, SPR_S_BART, BRIGHT+5, 2, (actionf_p1)A_Explode, S_S_BART_06);
    ST (S_S_BART_06, SPR_S_BART, BRIGHT+6, 2, NULL, S_S_BART_07);
    ST (S_S_BART_07, SPR_S_BART, BRIGHT+7, 2, NULL, S_S_BART_08);
    ST (S_S_BART_08, SPR_S_BART, BRIGHT+8, 2, NULL, S_S_BART_09);
    ST (S_S_BART_09, SPR_S_BART, BRIGHT+9, 3, NULL, S_S_BART_10);
    ST (S_S_BART_10, SPR_S_BART, BRIGHT+10, 3, NULL, S_S_BART_11);
    ST (S_S_BART_11, SPR_S_BART, 11, -1, NULL, S_NULL);
    MI (MT_S_MISC_06, 94, S_S_BART_00, 30, S_S_BART_01, sfx_s_barexp,
	S_NULL, 0, sfx_None, 0, 10*FRACUNIT, 32*FRACUNIT, 100,
	MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD);

    // Big hanging torture prop (MT_MISC_15) -- shootable, reacts to pain.
    ST (S_S_HOGN_00, SPR_S_HOGN, 0, 2, NULL, S_S_HOGN_00);
    ST (S_S_HOGN_01, SPR_S_HOGN, 1, 1, NULL, S_S_HOGN_02);
    ST (S_S_HOGN_02, SPR_S_HOGN, 2, 1, (actionf_p1)A_Pain, S_S_HOGN_00);
    MI (MT_S_MISC_15, 208, S_S_HOGN_00, 99999999, S_NULL, sfx_None,
	S_S_HOGN_01, 255, sfx_s_mtalht, 0, 10*FRACUNIT, 72*FRACUNIT, 9999999,
	MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD);

    // ====================================================================
    // Light sources (candles, lamps, braziers, torches, glowing lights)
    // ====================================================================
    ST (S_S_LITS_00, SPR_S_LITS, BRIGHT+0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT14, 95, S_S_LITS_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 4*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_LITB_00, SPR_S_LITB, BRIGHT+0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT13, 96, S_S_LITB_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 4*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_LITG_00, SPR_S_LITG, BRIGHT+0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT12, 97, S_S_LITG_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 4*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_LITE_00, SPR_S_LITE, BRIGHT+0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT18, 2028, S_S_LITE_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 16*FRACUNIT, 100, MF_SOLID);

    ST (S_S_CNDL_00, SPR_S_CNDL, BRIGHT+0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT2, 34, S_S_CNDL_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    ST (S_S_CLBR_00, SPR_S_CLBR, BRIGHT+0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT3, 35, S_S_CLBR_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 40*FRACUNIT, 100, MF_SOLID);

    // Fire bowl / brazier / floor torch (animated bright; A_ActiveSound dropped)
    ST (S_S_BOWL_00, SPR_S_BOWL, BRIGHT+0, 4, NULL, S_S_BOWL_01);
    ST (S_S_BOWL_01, SPR_S_BOWL, BRIGHT+1, 4, NULL, S_S_BOWL_02);
    ST (S_S_BOWL_02, SPR_S_BOWL, BRIGHT+2, 4, NULL, S_S_BOWL_03);
    ST (S_S_BOWL_03, SPR_S_BOWL, BRIGHT+3, 4, NULL, S_S_BOWL_00);
    MI (MT_S_LIGHT11, 105, S_S_BOWL_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 16*FRACUNIT, 100, MF_SOLID);
    ST (S_S_BRAZ_00, SPR_S_BRAZ, BRIGHT+0, 4, NULL, S_S_BRAZ_01);
    ST (S_S_BRAZ_01, SPR_S_BRAZ, BRIGHT+1, 4, NULL, S_S_BRAZ_02);
    ST (S_S_BRAZ_02, SPR_S_BRAZ, BRIGHT+2, 4, NULL, S_S_BRAZ_03);
    ST (S_S_BRAZ_03, SPR_S_BRAZ, BRIGHT+3, 4, NULL, S_S_BRAZ_00);
    MI (MT_S_LIGHT10, 106, S_S_BRAZ_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 10*FRACUNIT, 32*FRACUNIT, 100, MF_SOLID);
    ST (S_S_TRCH_00, SPR_S_TRCH, BRIGHT+0, 4, NULL, S_S_TRCH_01);
    ST (S_S_TRCH_01, SPR_S_TRCH, BRIGHT+1, 4, NULL, S_S_TRCH_02);
    ST (S_S_TRCH_02, SPR_S_TRCH, BRIGHT+2, 4, NULL, S_S_TRCH_03);
    ST (S_S_TRCH_03, SPR_S_TRCH, BRIGHT+3, 4, NULL, S_S_TRCH_00);
    MI (MT_S_LIGHT9, 107, S_S_TRCH_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 0, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_TRHO_00, SPR_S_TRHO, 0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT8, 108, S_S_TRHO_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 0, 16*FRACUNIT, 100, MF_NOBLOCKMAP);

    // Ceiling lamp / big lamp / lantern / column lamp / hanging logs
    ST (S_S_LTRH_00, SPR_S_LTRH, BRIGHT+0, 4, NULL, S_S_LTRH_01);
    ST (S_S_LTRH_01, SPR_S_LTRH, BRIGHT+1, 4, NULL, S_S_LTRH_02);
    ST (S_S_LTRH_02, SPR_S_LTRH, BRIGHT+2, 4, NULL, S_S_LTRH_03);
    ST (S_S_LTRH_03, SPR_S_LTRH, BRIGHT+3, 4, NULL, S_S_LTRH_00);
    MI (MT_S_LIGHT15, 111, S_S_LTRH_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 4*FRACUNIT, 72*FRACUNIT, 100, MF_SOLID);
    ST (S_S_LAMP_00, SPR_S_LAMP, 0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT4, 43, S_S_LAMP_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 3*FRACUNIT, 80*FRACUNIT, 100, MF_SOLID);
    ST (S_S_LANT_00, SPR_S_LANT, 0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT5, 46, S_S_LANT_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 3*FRACUNIT, 80*FRACUNIT, 100, MF_SOLID);
    ST (S_S_LMPC_00, SPR_S_LMPC, BRIGHT+0, 4, NULL, S_S_LMPC_01);
    ST (S_S_LMPC_01, SPR_S_LMPC, BRIGHT+1, 4, NULL, S_S_LMPC_02);
    ST (S_S_LMPC_02, SPR_S_LMPC, BRIGHT+2, 4, NULL, S_S_LMPC_03);
    ST (S_S_LMPC_03, SPR_S_LMPC, BRIGHT+3, 4, NULL, S_S_LMPC_00);
    MI (MT_S_LIGHT6, 47, S_S_LMPC_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 10*FRACUNIT, 72*FRACUNIT, 100, MF_SOLID);
    ST (S_S_LOGS_00, SPR_S_LOGS, BRIGHT+0, 4, NULL, S_S_LOGS_01);
    ST (S_S_LOGS_01, SPR_S_LOGS, BRIGHT+1, 4, NULL, S_S_LOGS_02);
    ST (S_S_LOGS_02, SPR_S_LOGS, BRIGHT+2, 4, NULL, S_S_LOGS_03);
    ST (S_S_LOGS_03, SPR_S_LOGS, BRIGHT+3, 4, NULL, S_S_LOGS_00);
    MI (MT_S_LIGHT7, 50, S_S_LOGS_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 10*FRACUNIT, 80*FRACUNIT, 100, MF_SOLID);

    // Table lamps (MT_LIGHT16/17) and hanging cage light (MT_LIGHT1)
    ST (S_S_TLMP_00, SPR_S_TLMP, 0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT16, 196, S_S_TLMP_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 11*FRACUNIT, 64*FRACUNIT, 100, MF_SOLID);
    ST (S_S_TLMP_01, SPR_S_TLMP, 1, -1, NULL, S_NULL);
    MI (MT_S_LIGHT17, 197, S_S_TLMP_01, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 8*FRACUNIT, 64*FRACUNIT, 100, MF_SOLID);
    ST (S_S_CAGE_00, SPR_S_CAGE, 0, -1, NULL, S_NULL);
    MI (MT_S_LIGHT1, 28, S_S_CAGE_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 3*FRACUNIT, 100,
	MF_NOBLOCKMAP|MF_SPAWNCEILING|MF_NOGRAVITY);

    // Ceiling chandelier (MT_MISC_14) and spinning spindle light (MT_LIGHT19)
    ST (S_S_CHAN_00, SPR_S_CHAN, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_14, 109, S_S_CHAN_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 93*FRACUNIT, 100,
	MF_NOBLOCKMAP|MF_SPAWNCEILING|MF_NOGRAVITY);
    ST (S_S_SPDL_00, SPR_S_SPDL, 0, 5, NULL, S_S_SPDL_01);
    ST (S_S_SPDL_01, SPR_S_SPDL, 1, 5, NULL, S_S_SPDL_02);
    ST (S_S_SPDL_02, SPR_S_SPDL, 2, 5, NULL, S_S_SPDL_00);
    MI (MT_S_LIGHT19, 225, S_S_SPDL_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 32*FRACUNIT, 56*FRACUNIT, 100, MF_SOLID);

    // ====================================================================
    // Pillars / statues / monoliths
    // ====================================================================
    ST (S_S_MONI_00, SPR_S_MONI, 0, -1, NULL, S_NULL);
    MI (MT_S_PILLAR2, 48, S_S_MONI_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 128*FRACUNIT, 100, MF_SOLID);
    ST (S_S_STEL_00, SPR_S_STEL, 0, -1, NULL, S_NULL);
    MI (MT_S_PILLAR3, 54, S_S_STEL_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 128*FRACUNIT, 100, MF_SOLID);
    ST (S_S_STLA_00, SPR_S_STLA, 0, -1, NULL, S_NULL);
    MI (MT_S_PILLAR4, 55, S_S_STLA_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 80*FRACUNIT, 100, MF_SOLID);
    ST (S_S_STLE_00, SPR_S_STLE, 0, -1, NULL, S_NULL);
    MI (MT_S_PILLAR5, 56, S_S_STLE_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 40*FRACUNIT, 100, MF_SOLID);
    ST (S_S_HUGE_00, SPR_S_HUGE, 0, 4, NULL, S_S_HUGE_01);
    ST (S_S_HUGE_01, SPR_S_HUGE, 1, 5, NULL, S_S_HUGE_02);
    ST (S_S_HUGE_02, SPR_S_HUGE, 2, 5, NULL, S_S_HUGE_03);
    ST (S_S_HUGE_03, SPR_S_HUGE, 3, 5, NULL, S_S_HUGE_00);
    MI (MT_S_PILLAR6, 57, S_S_HUGE_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 24*FRACUNIT, 192*FRACUNIT, 100, MF_SOLID);
    ST (S_S_APOW_00, SPR_S_APOW, 0, 4, NULL, S_S_APOW_00);
    MI (MT_S_PILLAR7, 227, S_S_APOW_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 24*FRACUNIT, 192*FRACUNIT, 100, MF_SOLID);
    ST (S_S_STAT_00, SPR_S_STAT, 0, -1, NULL, S_NULL);
    MI (MT_S_PILLAR8, 110, S_S_STAT_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 64*FRACUNIT, 100, MF_SOLID);
    ST (S_S_DSTA_00, SPR_S_DSTA, 0, -1, NULL, S_NULL);
    MI (MT_S_PILLAR9, 44, S_S_DSTA_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 56*FRACUNIT, 100, MF_SOLID);
    ST (S_S_BAR1_00, SPR_S_BAR1, 0, -1, NULL, S_NULL);
    MI (MT_S_PILLAR1, 69, S_S_BAR1_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 128*FRACUNIT, 100, MF_SOLID);
    ST (S_S_BUBB_00, SPR_S_BUBB, 0, 4, NULL, S_S_BUBB_00);
    MI (MT_S_PILLAR10, 221, S_S_BUBB_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 128*FRACUNIT, 100, MF_SOLID);
    ST (S_S_BUBF_00, SPR_S_BUBF, 0, 4, NULL, S_S_BUBF_00);
    MI (MT_S_PILLAR11, 222, S_S_BUBF_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 72*FRACUNIT, 100, MF_SOLID);
    MI (MT_S_PILLAR12, 223, S_S_BUBF_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 72*FRACUNIT, 100,
	MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY);
    ST (S_S_ASPR_00, SPR_S_ASPR, 0, 4, NULL, S_S_ASPR_00);
    MI (MT_S_PILLAR13, 224, S_S_ASPR_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 128*FRACUNIT, 100, MF_SOLID);

    // ====================================================================
    // Cave formations (stalactites / stalagmites)
    // ====================================================================
    ST (S_S_STLG_00, SPR_S_STLG, 0, -1, NULL, S_NULL);
    ST (S_S_STLG_01, SPR_S_STLG, 1, -1, NULL, S_NULL);
    ST (S_S_STLG_02, SPR_S_STLG, 2, -1, NULL, S_NULL);
    ST (S_S_STLG_03, SPR_S_STLG, 3, -1, NULL, S_NULL);
    ST (S_S_STLG_04, SPR_S_STLG, 4, -1, NULL, S_NULL);
    ST (S_S_STLG_05, SPR_S_STLG, 5, -1, NULL, S_NULL);
    MI (MT_S_CAVE2, 98, S_S_STLG_02, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 54*FRACUNIT, 100,
	MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY);
    MI (MT_S_CAVE3, 161, S_S_STLG_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 40*FRACUNIT, 100,
	MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY);
    MI (MT_S_CAVE4, 160, S_S_STLG_01, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 40*FRACUNIT, 100, MF_SOLID);
    MI (MT_S_CAVE6, 159, S_S_STLG_03, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 128*FRACUNIT, 100,
	MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY);
    MI (MT_S_CAVE7, 162, S_S_STLG_04, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 128*FRACUNIT, 100, MF_SOLID);
    MI (MT_S_CAVE5, 163, S_S_STLG_05, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 25*FRACUNIT, 100, MF_SOLID);
    ST (S_S_STAK_00, SPR_S_STAK, 0, -1, NULL, S_NULL);
    MI (MT_S_CAVE1, 63, S_S_STAK_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 64*FRACUNIT, 100, MF_SOLID);
    ST (S_S_CRAB_00, SPR_S_CRAB, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_08, 117, S_S_CRAB_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100,
	MF_SOLID|MF_SPAWNCEILING|MF_NOGRAVITY);

    // ====================================================================
    // Rocks and rubble
    // ====================================================================
    ST (S_S_ROK1_00, SPR_S_ROK1, 0, -1, NULL, S_NULL);
    MI (MT_S_ROCK1, 99, S_S_ROK1_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_ROK2_00, SPR_S_ROK2, 0, -1, NULL, S_NULL);
    MI (MT_S_ROCK2, 100, S_S_ROK2_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_ROK3_00, SPR_S_ROK3, 0, -1, NULL, S_NULL);
    MI (MT_S_ROCK3, 101, S_S_ROK3_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_ROK4_00, SPR_S_ROK4, 0, -1, NULL, S_NULL);
    MI (MT_S_ROCK4, 102, S_S_ROK4_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);

    ST (S_S_RUB1_00, SPR_S_RUB1, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE1, 29, S_S_RUB1_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_RUB2_00, SPR_S_RUB2, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE2, 30, S_S_RUB2_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_RUB3_00, SPR_S_RUB3, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE3, 31, S_S_RUB3_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_RUB4_00, SPR_S_RUB4, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE4, 32, S_S_RUB4_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_RUB5_00, SPR_S_RUB5, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE5, 36, S_S_RUB5_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_RUB6_00, SPR_S_RUB6, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE6, 37, S_S_RUB6_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_RUB7_00, SPR_S_RUB7, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE7, 41, S_S_RUB7_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_RUB8_00, SPR_S_RUB8, 0, -1, NULL, S_NULL);
    MI (MT_S_RUBBLE8, 42, S_S_RUB8_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);

    // ====================================================================
    // Trees / bushes / logs
    // ====================================================================
    ST (S_S_TREE_00, SPR_S_TREE, 0, -1, NULL, S_NULL);
    MI (MT_S_TREE2, 51, S_S_TREE_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 15*FRACUNIT, 109*FRACUNIT, 100, MF_SOLID);
    ST (S_S_TREE_01, SPR_S_TREE, 1, -1, NULL, S_NULL);
    MI (MT_S_TREE3, 202, S_S_TREE_01, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 15*FRACUNIT, 109*FRACUNIT, 100, MF_SOLID);
    ST (S_S_TREE_02, SPR_S_TREE, 2, -1, NULL, S_NULL);
    MI (MT_S_TREE4, 203, S_S_TREE_02, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 15*FRACUNIT, 64*FRACUNIT, 100, MF_SOLID);
    ST (S_S_TRE1_00, SPR_S_TRE1, 0, -1, NULL, S_NULL);
    MI (MT_S_TREE1, 33, S_S_TRE1_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 15*FRACUNIT, 80*FRACUNIT, 100, MF_SOLID);
    ST (S_S_BUSH_00, SPR_S_BUSH, 0, -1, NULL, S_NULL);
    MI (MT_S_TREE6, 60, S_S_BUSH_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 15*FRACUNIT, 40*FRACUNIT, 100, MF_SOLID);
    ST (S_S_SHRB_00, SPR_S_SHRB, 0, -1, NULL, S_NULL);
    MI (MT_S_TREE5, 62, S_S_SHRB_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 64*FRACUNIT, 100, MF_SOLID);
    ST (S_S_LOGG_00, SPR_S_LOGG, 0, 5, NULL, S_S_LOGG_01);
    ST (S_S_LOGG_01, SPR_S_LOGG, 1, 5, NULL, S_S_LOGG_02);
    ST (S_S_LOGG_02, SPR_S_LOGG, 2, 5, NULL, S_S_LOGG_03);
    ST (S_S_LOGG_03, SPR_S_LOGG, 3, 5, NULL, S_S_LOGG_00);
    MI (MT_S_TREE7, 215, S_S_LOGG_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);

    // ====================================================================
    // Pots / vases / stools / tubs / anvils / trays / furniture
    // ====================================================================
    ST (S_S_VASE_00, SPR_S_VASE, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_10, 165, S_S_VASE_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 12*FRACUNIT, 24*FRACUNIT, 100, MF_SOLID);
    ST (S_S_VASE_01, SPR_S_VASE, 1, -1, NULL, S_NULL);
    MI (MT_S_MISC_09, 188, S_S_VASE_01, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 12*FRACUNIT, 32*FRACUNIT, 100, MF_SOLID);
    ST (S_S_STOL_00, SPR_S_STOL, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_17, 189, S_S_STOL_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 6*FRACUNIT, 24*FRACUNIT, 100, MF_SOLID);
    ST (S_S_POT1_00, SPR_S_POT1, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_18, 190, S_S_POT1_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_TUB1_00, SPR_S_TUB1, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_19, 191, S_S_TUB1_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_ANVL_00, SPR_S_ANVL, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_20, 194, S_S_ANVL_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 32*FRACUNIT, 100, MF_SOLID);
    ST (S_S_TRAY_00, SPR_S_TRAY, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_21, 68, S_S_TRAY_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 24*FRACUNIT, 40*FRACUNIT, 100, MF_SOLID);
    ST (S_S_AFED_00, SPR_S_AFED, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_12, 228, S_S_AFED_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 12*FRACUNIT, 24*FRACUNIT, 100, MF_SOLID);

    // ====================================================================
    // Hanging banners / signs / miscellaneous wall props
    // ====================================================================
    ST (S_S_SBAN_00, SPR_S_SBAN, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_26, 216, S_S_SBAN_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 24*FRACUNIT, 96*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_BOTR_00, SPR_S_BOTR, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_23, 217, S_S_BOTR_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_HATR_00, SPR_S_HATR, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_24, 218, S_S_HATR_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_TOPR_00, SPR_S_TOPR, 0, -1, NULL, S_NULL);
    MI (MT_S_MISC_25, 219, S_S_TOPR_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);

    // Power coupling (MT_COUPLING) -- shootable quest prop.
    ST (S_S_COUP_00, SPR_S_COUP, 0, 5, NULL, S_S_COUP_01);
    ST (S_S_COUP_01, SPR_S_COUP, 1, 5, NULL, S_S_COUP_00);
    MI (MT_S_COUPLING, 220, S_S_COUP_00, 40, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 6, 17*FRACUNIT, 64*FRACUNIT, 999999,
	MF_SOLID|MF_SHOOTABLE|MF_DROPPED|MF_NOBLOOD|MF_NOTDMATCH);

    // ====================================================================
    // Ambient droplets / splashes / bio props (A_ActiveSound dropped)
    // ====================================================================
    ST (S_S_DRIP_00, SPR_S_DRIP, 0, 6, NULL, S_S_DRIP_01);
    ST (S_S_DRIP_01, SPR_S_DRIP, 1, 4, NULL, S_S_DRIP_02);
    ST (S_S_DRIP_02, SPR_S_DRIP, 2, 4, NULL, S_S_DRIP_03);
    ST (S_S_DRIP_03, SPR_S_DRIP, 3, 4, NULL, S_S_DRIP_04);
    ST (S_S_DRIP_04, SPR_S_DRIP, 4, 4, NULL, S_S_DRIP_05);
    ST (S_S_DRIP_05, SPR_S_DRIP, 5, 4, NULL, S_S_DRIP_06);
    ST (S_S_DRIP_06, SPR_S_DRIP, 6, 4, NULL, S_S_DRIP_07);
    ST (S_S_DRIP_07, SPR_S_DRIP, 7, 4, NULL, S_S_DRIP_00);
    MI (MT_S_MISC_03, 103, S_S_DRIP_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_SPLH_00, SPR_S_SPLH, 0, 4, NULL, S_S_SPLH_01);
    ST (S_S_SPLH_01, SPR_S_SPLH, 1, 4, NULL, S_S_SPLH_02);
    ST (S_S_SPLH_02, SPR_S_SPLH, 2, 4, NULL, S_S_SPLH_03);
    ST (S_S_SPLH_03, SPR_S_SPLH, 3, 8, NULL, S_S_SPLH_04);
    ST (S_S_SPLH_04, SPR_S_SPLH, 4, 4, NULL, S_S_SPLH_05);
    ST (S_S_SPLH_05, SPR_S_SPLH, 5, 4, NULL, S_S_SPLH_06);
    ST (S_S_SPLH_06, SPR_S_SPLH, 6, 4, NULL, S_S_SPLH_07);
    ST (S_S_SPLH_07, SPR_S_SPLH, 7, 4, NULL, S_S_SPLH_00);
    MI (MT_S_MISC_13, 104, S_S_SPLH_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_CDRP_00, SPR_S_CDRP, 0, 10, NULL, S_S_CDRP_01);
    ST (S_S_CDRP_01, SPR_S_CDRP, 1, 8, NULL, S_S_CDRP_02);
    ST (S_S_CDRP_02, SPR_S_CDRP, 2, 8, NULL, S_S_CDRP_03);
    ST (S_S_CDRP_03, SPR_S_CDRP, 3, 8, NULL, S_S_CDRP_00);
    MI (MT_S_MISC_02, 53, S_S_CDRP_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 1*FRACUNIT, 100,
	MF_NOBLOCKMAP|MF_SPAWNCEILING|MF_NOGRAVITY);
    ST (S_S_WTFT_00, SPR_S_WTFT, 0, 4, NULL, S_S_WTFT_01);
    ST (S_S_WTFT_01, SPR_S_WTFT, 1, 4, NULL, S_S_WTFT_02);
    ST (S_S_WTFT_02, SPR_S_WTFT, 2, 4, NULL, S_S_WTFT_03);
    ST (S_S_WTFT_03, SPR_S_WTFT, 3, 4, NULL, S_S_WTFT_00);
    MI (MT_S_MISC_07, 112, S_S_WTFT_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP);
    ST (S_S_HERT_00, SPR_S_HERT, BRIGHT+0, 4, NULL, S_S_HERT_01);
    ST (S_S_HERT_01, SPR_S_HERT, BRIGHT+1, 4, NULL, S_S_HERT_02);
    ST (S_S_HERT_02, SPR_S_HERT, BRIGHT+2, 4, NULL, S_S_HERT_00);
    MI (MT_S_BIO2, 113, S_S_HERT_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 56*FRACUNIT, 100, MF_SOLID);
    ST (S_S_SACR_00, SPR_S_SACR, 0, -1, NULL, S_NULL);
    MI (MT_S_BIO1, 212, S_S_SACR_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);

    // ====================================================================
    // Teleport landing pad (MT_TELEPORTSTAND)
    // ====================================================================
    ST (S_S_TELP_00, SPR_S_TELP, BRIGHT+0, 3, NULL, S_S_TELP_01);
    ST (S_S_TELP_01, SPR_S_TELP, BRIGHT+1, 3, NULL, S_S_TELP_02);
    ST (S_S_TELP_02, SPR_S_TELP, BRIGHT+2, 3, NULL, S_S_TELP_03);
    ST (S_S_TELP_03, SPR_S_TELP, BRIGHT+3, 3, NULL, S_S_TELP_00);
    MI (MT_S_TELEPORTSTAND, 23, S_S_TELP_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100,
	MF_NOBLOCKMAP|MF_SHADOW);

    // ====================================================================
    // Dead bodies / gibs / meat (corpses)
    //   DEADTHING1/5/6 reference monster corpse frames (S_S_ROB2_29 /
    //   S_S_ROB1_25 / S_S_HMN1_31) owned by the monster installer -- NOT
    //   filled here.  The standalone corpse frames are filled below.
    // ====================================================================
    ST (S_S_PLAY_CORPSE, SPR_S_PLAY, 15, -1, NULL, S_NULL);
    ST (S_S_PEAS_CORPSE, SPR_S_PEAS, 13, -1, NULL, S_NULL);
    ST (S_S_AGRD_CORPSE, SPR_S_AGRD, 13, -1, NULL, S_NULL);
    ST (S_S_DEAD_00,     SPR_S_DEAD, 0, -1, NULL, S_NULL);
    MI (MT_S_DEADTHING1, 22, S_S_ROB2_29,     1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    MI (MT_S_DEADTHING2, 15, S_S_PLAY_CORPSE, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    MI (MT_S_DEADTHING3, 18, S_S_PEAS_CORPSE, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    MI (MT_S_DEADTHING4, 21, S_S_AGRD_CORPSE, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    MI (MT_S_DEADTHING5, 20, S_S_ROB1_25,     1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    MI (MT_S_DEADTHING6, 19, S_S_HMN1_31,     1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);
    MI (MT_S_GIBS, 54, S_S_DEAD_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, 0);

    // Animated fire barrel (MT_MISC_04) -- animated, non-shootable.
    ST (S_S_BARL_00, SPR_S_BARL, BRIGHT+0, 4, NULL, S_S_BARL_01);
    ST (S_S_BARL_01, SPR_S_BARL, BRIGHT+1, 4, NULL, S_S_BARL_02);
    ST (S_S_BARL_02, SPR_S_BARL, BRIGHT+2, 4, NULL, S_S_BARL_03);
    ST (S_S_BARL_03, SPR_S_BARL, BRIGHT+3, 4, NULL, S_S_BARL_00);
    MI (MT_S_MISC_04, 70, S_S_BARL_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 16*FRACUNIT, 48*FRACUNIT, 100, MF_SOLID);

    // ====================================================================
    // Decorative sub-actors (no editor number -- spawned by other actors)
    // ====================================================================
    ST (S_S_MEAT_00, SPR_S_MEAT, 0, 700, NULL, S_NULL);
    MI (MT_S_MEAT, -1, S_S_MEAT_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_JUNK_00, SPR_S_JUNK, 0, 700, NULL, S_NULL);
    MI (MT_S_JUNK, -1, S_S_JUNK_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
    ST (S_S_FFOT_00, SPR_S_FFOT, 0, 9, NULL, S_S_FFOT_01);
    ST (S_S_FFOT_01, SPR_S_FFOT, 1, 9, NULL, S_S_FFOT_02);
    ST (S_S_FFOT_02, SPR_S_FFOT, 2, 9, NULL, S_S_FFOT_03);
    ST (S_S_FFOT_03, SPR_S_FFOT, 3, 9, NULL, S_NULL);
    MI (MT_S_BURNDROP, -1, S_S_FFOT_00, 1000, S_NULL, sfx_None,
	S_NULL, 0, sfx_None, 0, 20*FRACUNIT, 16*FRACUNIT, 100, MF_NOBLOCKMAP|MF_NOCLIP);
}
