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

// Level hook: spawn the currently-selected mobj buddy (config `buddy_select`)
// next to player 1, suppressing the marine.  No-op for slot 0 (Marine) or when
// buddy mode is off.  Called from P_SetupLevel after players spawn.
void	P_Buddy_SpawnSelected (void);

#endif
