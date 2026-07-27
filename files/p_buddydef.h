// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Native modder co-op buddies (BuddyDoom).
//
//	Reads BUDDYDEF text lumps straight out of the loaded WADs -- no external
//	compiler (no decohack/DEHACKED needed).  Each record defines a friendly
//	MF_FRIEND actor built entirely in C: BuddyDoom allocates the mobjtype,
//	its states (with A_BuddyLook / A_BuddyChase / a chosen attack pointer),
//	its sprite name and its sounds via the DSDHacked table-growth API, then
//	records name/description/preview-sprite in a roster for the Buddy menu.
//
//	The hard-coded player-2 marine buddy (files/p_ai_coop.c) is roster slot 0
//	("Marine") and is unchanged.  Selecting a BUDDYDEF buddy (slot > 0) makes
//	that mobj your companion instead (P_Buddy_SpawnSelected).
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
int		P_Buddy_TypeByName (const char* s);	// mobjtype of a buddy by name prefix, or -1

// Stats shown on the Buddy select screen (all definable in BUDDYDEF).
typedef struct {
    int		health, speed, radius, height, mass, painchance, reactiontime;
    const char*	attack;		// attack-style name
    const char*	special;	// free-text special abilities (blurb)
    const char*	ability;	// NAMED ability: none | drone | poisoncloud | turret
} buddystats_t;
void	P_Buddy_GetStats (int slot, buddystats_t* out);

// BUDDYDEF `ability` -- the mechanic the buddy actually uses in play, as opposed to the
// `special` blurb.  P_Buddy_AbilityTicker runs it once per tic (called from P_Ticker)
// for the live mobj companion; the Marine's own "drone" runs in the marine bot instead.
const char*	P_Buddy_Ability (int slot);
void		P_Buddy_AbilityTicker (void);

// Level hook: spawn the currently-selected mobj buddy (config `buddy_select`)
// next to player 1, suppressing the marine.  No-op for slot 0 (Marine) or when
// buddy mode is off.  Called from P_SetupLevel after players spawn.
void	P_Buddy_SpawnSelected (void);

// --- the live mobj companion -----------------------------------------------
// The marine buddy is player 2, so the HUD (hu_buddy.c), the automap marker
// (am_map.c) and the console orders (c_console.c) are all keyed on
// playeringame[P_AICoop_Slot()] -- which is FALSE for a BUDDYDEF buddy, because it
// is an mobj rather than a player.  Those systems fall back to these accessors so a
// modder buddy is recognised exactly like the marine.
struct mobj_s;
struct mobj_s*	P_Buddy_Mobj (void);		// live companion mobj, or NULL
const char*	P_Buddy_ActiveName (void);	// display name of the active buddy ("" = none)
int		P_Buddy_MaxHealth (void);	// its spawnhealth (for the HP bar / report)

// Standing orders, asked by A_BuddyChase (p_enemy.c) each time it runs.
int	P_Buddy_Recalled (void);		// "come": ignore enemies, pad back to the human
int	P_Buddy_Held (void);			// "wait": hold position

// Console commands -- same "[Buddy] ..." reply convention as the marine's.
const char*	P_Buddy_Report (void);		// where / buddy / comp
const char*	P_Buddy_StatusReport (void);	// report / status
const char*	P_Buddy_Summon (void);		// come / follow
const char*	P_Buddy_Wait (void);		// wait / stay
const char*	P_Buddy_Attack (void);		// attack
const char*	P_Buddy_Warp (void);		// buddyhome / buddytp
const char*	P_Buddy_ToggleMode (void);	// key_buddy_mode (RMouse): hold <-> follow

#endif
