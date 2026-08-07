// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1993-2008 Raven Software (Hexen ACS, the reference for this port)
//
// DESCRIPTION:
//	(X) ACS -- Hexen's Action Code Script virtual machine.
//
//	A Hexen-format map ships a BEHAVIOR lump: compiled ACS0 bytecode plus a table
//	of scripts and strings.  Scripts are what make a Hexen level a level -- doors,
//	lifts, ambushes and the whole hub/puzzle structure are scripts, not line
//	specials, so without a VM a Hexen map loads and renders but nothing in it ever
//	moves.  (That is exactly where files/p_setup.c left things.)
//
//	Ported from ../crispy-doom/src/hexen/p_acs.c (Raven's original, GPL), adapted
//	to this engine: no polyobjects, no sound sequences, no Hexen inventory, and
//	the DOOM line-special set instead of Hexen's -- see P_ExecuteLineSpecial in
//	p_acs.c for exactly which specials are wired and which are logged and ignored.
//
//-----------------------------------------------------------------------------

#ifndef __P_ACS__
#define __P_ACS__

#include "doomtype.h"
#include "d_think.h"
#include "r_defs.h"

#define MAX_ACS_SCRIPT_VARS	10
#define MAX_ACS_MAP_VARS	32
#define MAX_ACS_WORLD_VARS	64
#define ACS_STACK_DEPTH		32
#define MAX_ACS_STORE		20	// deferred cross-map script starts

// A running script.  One thinker per active script; `ip` walks the bytecode.
typedef struct
{
    thinker_t	thinker;
    mobj_t*	activator;	// who set it off (may be NULL)
    line_t*	line;		// the line that triggered it (may be NULL)
    int		side;
    int		number;		// script number
    int		infoIndex;	// index into the map's script table
    int		delayCount;	// tics left on a Delay()
    int		stack[ACS_STACK_DEPTH];
    int		stackPtr;
    int		vars[MAX_ACS_SCRIPT_VARS];
    int*	ip;		// instruction pointer into ActionCodeBase
} acs_t;

// Deferred start for a script on a map we are not currently in.
typedef struct
{
    int		map;		// 0 = "this entry is free"
    int		script;
    byte	args[4];
} acsstore_t;

extern int		ACSWorldVars[MAX_ACS_WORLD_VARS];
extern acsstore_t	ACSStore[MAX_ACS_STORE + 1];

// Parse the map's BEHAVIOR lump (lump < 0, or a non-ACS lump, simply leaves the
// VM idle).  Runs every OPEN script.  Call from P_SetupLevel after the geometry.
void	P_LoadACScripts (int lump);

// Start / suspend / terminate a script.  `map` 0 means "this map"; any other value
// defers the start until that map is entered (P_CheckACSStore).
boolean	P_StartACS (int number, int map, byte* args, mobj_t* activator,
		    line_t* line, int side);
boolean	P_SuspendACS (int number, int map);
boolean	P_TerminateACS (int number, int map);

// Wake any script blocked on this sector tag / on another script finishing.
void	P_TagFinished (int tag);
void	P_ACScriptFinished (int number);

// Deferred starts queued for the map we just entered.
void	P_CheckACSStore (void);

// Clear world variables + the deferred store (new game, not a level change).
void	P_ACSInitNewGame (void);

// The thinker.
void	T_InterpretACS (acs_t* script);

// A polyobject has stopped moving; wake any script waiting on it (PolyWait).
void	P_PolyobjFinished (int po);

// Run one Hexen line special.  This is the bridge between ACS (and Hexen linedefs)
// and the engine's DOOM special machinery.  Returns true if the special did
// something.  See p_acs.c for the supported set.
boolean	P_ExecuteLineSpecial (int special, byte* args, line_t* line, int side,
			      mobj_t* mo);

// How a Hexen linedef is triggered.  Unlike DOOM, this is a property of the LINE
// (flag bits 10-12), not of the special number, so the same special can be a walk
// trigger on one line and a switch on another.
#define SPAC_CROSS	0	// player walks over it
#define SPAC_USE	1	// player presses use on it
#define SPAC_MCROSS	2	// monster walks over it
#define SPAC_IMPACT	3	// a projectile hits it
#define SPAC_PUSH	4	// player/monster bumps into it
#define SPAC_PCROSS	5	// a projectile crosses it

// Trigger a Hexen-format linedef, if `activationType` is how it wants to be
// triggered.  Returns true if the special ran.  Hexen-format maps only -- the
// callers gate on hexen_map_format, because these flag bits mean other things in
// DOOM/Boom.
boolean	P_ActivateLine (line_t* line, mobj_t* mo, int side, int activationType);

#endif
