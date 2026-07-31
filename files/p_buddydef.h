// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Native modder co-op buddies (BuddyDoom).
//
//	Reads BUDDYDEF text lumps straight out of the loaded WADs -- no external
//	compiler (no decohack/DEHACKED needed).  Each record is a ROSTER entry: a
//	name, description, preview sprite, colour and a set of properties, listed on
//	the Buddy select menu.
//
//	A buddy is ALWAYS player 2 (files/p_ai_coop.c), which is what gives it door
//	use, orders, the HUD strip, the automap marker, revive, the pathfinder,
//	weapons and savegame support for free.  A record therefore never defines an
//	actor: no mobjtype, no states, no attack codepointer.  (An earlier design did
//	exactly that and made every modder buddy a monster -- which cannot open a
//	door, be ordered, be revived or show up on the HUD.)
//
//	Applying a record's properties to player 2 -- skin, stats, sounds, behaviour
//	-- is in progress; until it lands, slot 0's Marine is the body whatever is
//	selected.  Design: docs/BUDDYDEF.md.
//
//-----------------------------------------------------------------------------

#ifndef __P_BUDDYDEF_H__
#define __P_BUDDYDEF_H__

// Parse every BUDDYDEF lump in the loaded WADs and register the buddies.
// MUST run after WAD init (and after DEHACKED, so it can see DSDHacked things)
// but BEFORE R_Init -- so R_InitSpriteDefs picks up the buddies' sprite names.
void	P_Buddy_LoadDefs (void);

// --- roster, for the Buddy select menu -------------------------------------
// Slot 0 is always the built-in "Marine"; slots 1..N are BUDDYDEF buddies.
int		P_Buddy_Count (void);		// >= 1 (Marine is always present)
const char*	P_Buddy_Name (int slot);	// display name
const char*	P_Buddy_Desc (int slot);	// one/two-line description
int		P_Buddy_Sprite (int slot);	// spritenum for the preview (SPR_PLAY for Marine)
int		P_Buddy_Color  (int slot);	// declared default colour index (BUDDYDEF `color`), -1 = none

// Stats shown on the Buddy select screen (all definable in BUDDYDEF).
typedef struct {
    int		health, speed, radius, height, mass, painchance, reactiontime;
    const char*	melee;		// close-range attack style (BUDDYDEF `meleeattack`)
    const char*	ranged;		// at-distance attack style (BUDDYDEF `rangedattack`)
    const char*	monster;	// base monster the buddy derives from (BUDDYDEF `monster`)
    const char*	special;	// free-text special abilities (blurb)
    const char*	ability;	// NAMED ability: none | drone | poisoncloud | turret
} buddystats_t;
void	P_Buddy_GetStats (int slot, buddystats_t* out);

// BUDDYDEF sound lump names (see/pain/death/active) -- the co-op driver reprograms the
// buddy sfx slots with these so player 2 uses the selected buddy's voice.  "" = not set.
enum { BUDDYSND_SEE, BUDDYSND_PAIN, BUDDYSND_DEATH, BUDDYSND_ACTIVE };
const char* P_Buddy_Sound (int slot, int which);

// BUDDYDEF `ability` -- the mechanic the buddy actually uses in play, as opposed to the
// `special` blurb.  P_Buddy_AbilityTicker runs it once per tic (called from P_Ticker) on
// the buddy player's body; the Marine's own "drone" runs inside the marine bot instead,
// so the ticker stays out of the way while slot 0 is selected.
const char*	P_Buddy_Ability (int slot);
void		P_Buddy_AbilityTicker (void);

#endif
