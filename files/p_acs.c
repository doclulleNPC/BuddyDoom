// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1993-2008 Raven Software (Hexen ACS -- the reference for this port)
//
// DESCRIPTION:
//	(X) ACS -- Hexen's Action Code Script virtual machine.  See p_acs.h.
//
//	Ported from ../crispy-doom/src/hexen/p_acs.c, which is Raven's original.  The
//	VM itself is faithful: same ACS0 container, same script/string tables, same
//	opcode numbering and semantics, same map/world variable spaces, same deferred
//	cross-map store.  What differs is everything it talks TO, because this is the
//	DOOM engine wearing Hexen content:
//
//	  * P_ExecuteLineSpecial maps Hexen's specials onto DOOM's EV_* machinery.
//	    A useful subset is wired (doors, floors, ceilings, lights, teleport-to-map,
//	    the ACS specials themselves); the rest are logged ONCE each and ignored,
//	    so an unported special is a missing effect and a diagnostic line, never a
//	    crash or a wrong effect.
//	  * No polyobjects, no sound sequences, no Hexen inventory/classes.  The
//	    opcodes that drive them are accepted and do nothing (PolyWait returns
//	    immediately, SoundSequence is a no-op) rather than being rejected, so a
//	    script that uses them still runs to completion instead of stalling.
//
//	Diagnostics matter here: a script that silently does nothing is very hard to
//	tell from a script that never ran, so unknown opcodes terminate the script
//	with a printf naming the opcode, and unhandled specials name themselves once.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_random.h"
#include "m_swap.h"
#include "p_local.h"
#include "p_spec.h"
#include "s_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "hu_stuff.h"
#include "p_acs.h"
#include "po_man.h"
#include "hexen_things.h"	// (X) Thing_* specials + glass shards

// ACS0 container header: "ACS\0", then the offset of the info block.
typedef struct
{
    char	marker[4];
    int		infoOffset;
} acsHeader_t;

// One entry of the map's script table.
typedef struct
{
    int		number;
    int*	address;
    int		argCount;
    int		state;
    int		waitValue;
} acsInfo_t;

// acsInfo_t.state
enum
{
    ASTE_INACTIVE,
    ASTE_RUNNING,
    ASTE_SUSPENDED,
    ASTE_WAITINGFORTAG,
    ASTE_WAITINGFORPOLYOBJ,
    ASTE_WAITINGFORSCRIPT,
    ASTE_TERMINATING
};

#define OPEN_SCRIPTS_BASE	1000	// script numbers >= this run at level start
#define PRINT_BUFFER_SIZE	256

// Return values from an opcode handler.
#define SCRIPT_CONTINUE		0
#define SCRIPT_STOP		1
#define SCRIPT_TERMINATE	2

int		ACSWorldVars[MAX_ACS_WORLD_VARS];
acsstore_t	ACSStore[MAX_ACS_STORE + 1];

static int		ACScriptCount;
static byte*		ActionCodeBase;
static acsInfo_t*	ACSInfo;
static int		MapVars[MAX_ACS_MAP_VARS];
static int		ACStringCount;
static char**		ACStrings;

// Interpreter state for the script currently being stepped.
static acs_t*		ACScript;
static int*		PCodePtr;
static int		SpecArgs[8];
static char		PrintBuffer[PRINT_BUFFER_SIZE];
static acs_t*		NewScript;

extern boolean	P_SetMobjState (mobj_t*, statenum_t);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void Push (int value)
{
    if (ACScript->stackPtr >= ACS_STACK_DEPTH)
    {
	printf ("ACS: script %d stack overflow -- terminated\n", ACScript->number);
	ACScript->stackPtr = 0;
	return;
    }
    ACScript->stack[ACScript->stackPtr++] = value;
}

static int Pop (void)
{
    if (ACScript->stackPtr <= 0)
	return 0;			// underflow: yield 0 rather than corrupt memory
    return ACScript->stack[--ACScript->stackPtr];
}

static int Top (void)
{
    return (ACScript->stackPtr > 0) ? ACScript->stack[ACScript->stackPtr - 1] : 0;
}

static void Drop (void)
{
    if (ACScript->stackPtr > 0) ACScript->stackPtr--;
}

// Index of a script number in the map's table, or -1.
static int GetACSIndex (int number)
{
    int i;
    for (i = 0; i < ACScriptCount; i++)
	if (ACSInfo[i].number == number)
	    return i;
    return -1;
}

static void ScriptFinished (int number)
{
    int i;
    for (i = 0; i < ACScriptCount; i++)
	if (ACSInfo[i].state == ASTE_WAITINGFORSCRIPT && ACSInfo[i].waitValue == number)
	    ACSInfo[i].state = ASTE_RUNNING;
}

void P_ACScriptFinished (int number) { ScriptFinished (number); }

// Is any sector with this tag still running a floor/ceiling/door thinker?
// ACS TagWait() blocks on exactly this.
static boolean TagBusy (int tag)
{
    int i;
    for (i = 0; i < numsectors; i++)
	if (sectors[i].tag == tag && sectors[i].specialdata)
	    return true;
    return false;
}

// Queue a start for a script on another map.
static boolean AddToACSStore (int map, int number, byte* args)
{
    int i, freeEntry = -1;
    for (i = 0; ACSStore[i].map != 0; i++)
    {
	if (ACSStore[i].script == number && ACSStore[i].map == map)
	    return false;			// already pending
	if (i >= MAX_ACS_STORE) return false;
    }
    freeEntry = i;
    if (freeEntry >= MAX_ACS_STORE) return false;
    ACSStore[freeEntry].map    = map;
    ACSStore[freeEntry].script = number;
    memcpy (ACSStore[freeEntry].args, args, 4);
    ACSStore[freeEntry + 1].map = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

static void StartOpenACS (int number, int infoIndex, int* address)
{
    acs_t* script = Z_Malloc (sizeof(*script), PU_LEVEL, 0);
    memset (script, 0, sizeof(*script));
    script->number    = number;
    script->infoIndex = infoIndex;
    script->ip        = address;
    script->thinker.function.acp1 = (actionf_p1) T_InterpretACS;
    P_AddThinker (&script->thinker);
}

void P_LoadACScripts (int lump)
{
    int		i, nopen = 0;
    int*	buffer;
    acsHeader_t* header;

    ACScriptCount  = 0;
    ActionCodeBase = NULL;
    ACSInfo        = NULL;
    ACStringCount  = 0;
    ACStrings      = NULL;
    memset (MapVars, 0, sizeof(MapVars));

    if (lump < 0 || W_LumpLength (lump) < (int)sizeof(acsHeader_t))
	return;

    header = (acsHeader_t*) W_CacheLumpNum (lump, PU_LEVEL);
    if (header->marker[0] != 'A' || header->marker[1] != 'C'
	|| header->marker[2] != 'S' || header->marker[3] != 0)
    {
	printf ("ACS: BEHAVIOR lump is not ACS0 -- scripts disabled for this map\n");
	return;
    }
    ActionCodeBase = (byte*) header;

    buffer = (int*) (ActionCodeBase + LONG(header->infoOffset));
    ACScriptCount = LONG(*buffer++);
    if (ACScriptCount <= 0)
    {
	ACScriptCount = 0;
	return;
    }

    ACSInfo = Z_Malloc (ACScriptCount * sizeof(acsInfo_t), PU_LEVEL, 0);
    memset (ACSInfo, 0, ACScriptCount * sizeof(acsInfo_t));
    for (i = 0; i < ACScriptCount; i++)
    {
	acsInfo_t* info = &ACSInfo[i];
	info->number   = LONG(*buffer++);
	info->address  = (int*) (ActionCodeBase + LONG(*buffer++));
	info->argCount = LONG(*buffer++);
	if (info->argCount > MAX_ACS_SCRIPT_VARS)
	    info->argCount = MAX_ACS_SCRIPT_VARS;
	if (info->number >= OPEN_SCRIPTS_BASE)
	{
	    // OPEN script: runs the moment the map starts.
	    info->number -= OPEN_SCRIPTS_BASE;
	    info->state = ASTE_RUNNING;
	    StartOpenACS (info->number, i, info->address);
	    nopen++;
	}
	else
	    info->state = ASTE_INACTIVE;
    }

    ACStringCount = LONG(*buffer++);
    ACStrings = Z_Malloc (ACStringCount * sizeof(char*), PU_LEVEL, 0);
    for (i = 0; i < ACStringCount; i++)
	ACStrings[i] = (char*) ActionCodeBase + LONG(*buffer++);

    printf ("ACS: %d script(s), %d string(s)\n", ACScriptCount, ACStringCount);
}

void P_ACSInitNewGame (void)
{
    memset (ACSWorldVars, 0, sizeof(ACSWorldVars));
    memset (ACSStore, 0, sizeof(ACSStore));
}

void P_CheckACSStore (void)
{
    acsstore_t* store;
    for (store = ACSStore; store->map != 0; store++)
    {
	if (store->map == gamemap)
	{
	    P_StartACS (store->script, 0, store->args, players[consoleplayer].mo, NULL, 0);
	    // compact the list over the entry we just consumed
	    {
		acsstore_t* s;
		for (s = store; s->map != 0; s++) *s = *(s + 1);
		store--;
	    }
	}
    }
}

// ---------------------------------------------------------------------------
// Start / suspend / terminate
// ---------------------------------------------------------------------------

boolean P_StartACS (int number, int map, byte* args, mobj_t* activator,
		    line_t* line, int side)
{
    int		i, infoIndex;
    acs_t*	script;

    NewScript = NULL;
    if (map && map != gamemap)
	return AddToACSStore (map, number, args);	// defer to that map

    infoIndex = GetACSIndex (number);
    if (infoIndex == -1)
    {
	printf ("ACS: no script %d on this map\n", number);
	return false;
    }
    if (ACSInfo[infoIndex].state == ASTE_SUSPENDED)
    {
	ACSInfo[infoIndex].state = ASTE_RUNNING;	// resume in place
	return true;
    }
    if (ACSInfo[infoIndex].state != ASTE_INACTIVE)
	return false;					// already running

    script = Z_Malloc (sizeof(*script), PU_LEVEL, 0);
    memset (script, 0, sizeof(*script));
    script->number    = number;
    script->infoIndex = infoIndex;
    script->activator = activator;
    script->line      = line;
    script->side      = side;
    script->ip        = ACSInfo[infoIndex].address;
    for (i = 0; i < ACSInfo[infoIndex].argCount; i++)
	script->vars[i] = args[i];
    script->thinker.function.acp1 = (actionf_p1) T_InterpretACS;
    P_AddThinker (&script->thinker);
    ACSInfo[infoIndex].state = ASTE_RUNNING;
    NewScript = script;
    return true;
}

boolean P_SuspendACS (int number, int map)
{
    int infoIndex;
    if (map && map != gamemap) return false;
    infoIndex = GetACSIndex (number);
    if (infoIndex == -1) return false;
    if (ACSInfo[infoIndex].state == ASTE_INACTIVE
	|| ACSInfo[infoIndex].state == ASTE_SUSPENDED
	|| ACSInfo[infoIndex].state == ASTE_TERMINATING)
	return false;
    ACSInfo[infoIndex].state = ASTE_SUSPENDED;
    return true;
}

boolean P_TerminateACS (int number, int map)
{
    int infoIndex;
    if (map && map != gamemap) return false;
    infoIndex = GetACSIndex (number);
    if (infoIndex == -1) return false;
    if (ACSInfo[infoIndex].state == ASTE_INACTIVE
	|| ACSInfo[infoIndex].state == ASTE_TERMINATING)
	return false;
    ACSInfo[infoIndex].state = ASTE_TERMINATING;
    return true;
}

void P_TagFinished (int tag)
{
    int i;
    if (TagBusy (tag)) return;
    for (i = 0; i < ACScriptCount; i++)
	if (ACSInfo[i].state == ASTE_WAITINGFORTAG && ACSInfo[i].waitValue == tag)
	    ACSInfo[i].state = ASTE_RUNNING;
}

// A polyobj has come to rest -- release any script blocked on PolyWait for it.
// Called from po_man.c when a polyobj thinker finishes.
void P_PolyobjFinished (int po)
{
    int i;
    for (i = 0; i < ACScriptCount; i++)
	if (ACSInfo[i].state == ASTE_WAITINGFORPOLYOBJ && ACSInfo[i].waitValue == po)
	    ACSInfo[i].state = ASTE_RUNNING;
}

// ---------------------------------------------------------------------------
// Line specials
//
// Hexen's special numbers are their own set, unrelated to DOOM's.  Map the ones
// this engine can actually perform onto its EV_* calls; everything else is named
// once and ignored.  EV_* take a line for the tag, so synthesise one -- that is
// the standard trick for driving tag-based specials without a real linedef.
// ---------------------------------------------------------------------------

static boolean acs_reported[256];

static line_t* ACS_TagLine (int tag)
{
    static line_t fake;
    memset (&fake, 0, sizeof(fake));
    fake.tag = tag;
    return &fake;
}

boolean P_ExecuteLineSpecial (int special, byte* args, line_t* line, int side,
			      mobj_t* mo)
{
    line_t* tl = ACS_TagLine (args[0]);

    switch (special)
    {
      // ---- polyobjects (Hexen 1-8, 90-93).  See files/po_man.c.
      // 1 and 5 are not actions at all: they are the markers PO_Init reads to
      // find which linedefs make up a polyobj, and doing anything with them here
      // would be wrong.  Say so explicitly rather than letting them fall through
      // to the "not implemented" warning every time a map loads.
      case 1:							// Polyobj_StartLine
      case 5:							// Polyobj_ExplicitLine
	return false;

      case 2:  return EV_RotatePoly (args,  1, false);		// Polyobj_RotateLeft
      case 3:  return EV_RotatePoly (args, -1, false);		// Polyobj_RotateRight
      case 4:  return EV_MovePoly   (args, false, false);	// Polyobj_Move
      case 6:  return EV_MovePoly   (args, true,  false);	// Polyobj_MoveTimes8
      case 7:  return EV_OpenPolyDoor (args, PODOOR_SWING);	// Polyobj_DoorSwing
      case 8:  return EV_OpenPolyDoor (args, PODOOR_SLIDE);	// Polyobj_DoorSlide

      // The OR_ variants override a polyobj that is already moving instead of
      // refusing, which is how a script retargets one mid-motion.
      case 90: return EV_RotatePoly (args,  1, true);		// Polyobj_OR_RotateLeft
      case 91: return EV_RotatePoly (args, -1, true);		// Polyobj_OR_RotateRight
      case 92: return EV_MovePoly   (args, false, true);	// Polyobj_OR_Move
      case 93: return EV_MovePoly   (args, true,  true);	// Polyobj_OR_MoveTimes8

      // ---- doors (Hexen 10-13).  Hexen's speed/delay args have no DOOM
      // equivalent, so the nearest stock door type is used.
      case 10: return EV_DoDoor (tl, close) != 0;		// Door_Close
      case 11: return EV_DoDoor (tl, open) != 0;		// Door_Open
      case 12: return EV_DoDoor (tl, normal) != 0;		// Door_Raise
      case 13: return EV_DoDoor (tl, normal) != 0;		// Door_LockedRaise (lock ignored)

      // ---- floors (Hexen 20-25)
      case 20: return EV_DoFloor (tl, lowerFloor) != 0;		// Floor_LowerByValue
      case 21: return EV_DoFloor (tl, lowerFloorToLowest) != 0;	// Floor_LowerToLowest
      case 22: return EV_DoFloor (tl, lowerFloorToNearest) != 0;// Floor_LowerToNearest
      case 23: return EV_DoFloor (tl, raiseFloor) != 0;		// Floor_RaiseByValue
      case 24: return EV_DoFloor (tl, raiseFloorToNearest) != 0;// Floor_RaiseToHighest
      case 25: return EV_DoFloor (tl, raiseFloorToNearest) != 0;// Floor_RaiseToNearest
      case 28: return EV_DoFloor (tl, raiseFloorCrush) != 0;	// Floor_RaiseAndCrush

      // ---- ceilings (Hexen 40-45)
      case 40: return EV_DoCeiling (tl, lowerToFloor) != 0;	// Ceiling_LowerByValue
      case 41: return EV_DoCeiling (tl, raiseToHighest) != 0;	// Ceiling_RaiseByValue
      case 42: return EV_DoCeiling (tl, crushAndRaise) != 0;	// Ceiling_CrushAndRaise
      case 43: return EV_DoCeiling (tl, lowerAndCrush) != 0;	// Ceiling_LowerAndCrush
      case 44: return EV_CeilingCrushStop (tl) != 0;		// Ceiling_CrushStop

      // ---- Thing_* (Hexen 130-137).  These address things by TID, and they are
      // how Hexen's scripts spawn ambushes, drop items, throw rocks -- and shatter
      // stained glass (Thing_Projectile with the T_STAINEDGLASS types).  See
      // files/hexen_things.c.
      case 130: return EV_ThingActivate   (args, true);		// Thing_Activate
      case 131: return EV_ThingActivate   (args, false);	// Thing_Deactivate
      case 132: return EV_ThingRemove     (args, false);	// Thing_Remove
      case 133: return EV_ThingRemove     (args, true);		// Thing_Destroy
      case 134: return EV_ThingProjectile (args, false);	// Thing_Projectile
      case 135: return EV_ThingSpawn      (args, true);		// Thing_Spawn
      case 136: return EV_ThingProjectile (args, true);		// Thing_ProjectileGravity
      case 137: return EV_ThingSpawn      (args, false);	// Thing_SpawnNoFog

      // ---- teleport
      case 70: return mo ? (EV_Teleport (tl, side, mo) != 0) : false;	// Teleport

      // ---- ACS itself
      case 80:							// ACS_Execute
	return P_StartACS (args[0], args[1], &args[2], mo, line, side);
      case 81:							// ACS_Suspend
	return P_SuspendACS (args[0], args[1]);
      case 82:							// ACS_Terminate
	return P_TerminateACS (args[0], args[1]);
      case 83:							// ACS_LockedExecute (lock ignored)
	return P_StartACS (args[0], args[1], &args[2], mo, line, side);

      // ---- lights (Hexen 110-113)
      case 110: EV_LightTurnOn (tl, args[1] ? args[1] : 255); return true;	// Light_RaiseByValue
      case 111: EV_LightTurnOn (tl, 35);  return true;		// Light_LowerByValue
      case 112: EV_LightTurnOn (tl, args[1]); return true;	// Light_ChangeToValue
      case 113: EV_StartLightStrobing (tl); return true;	// Light_Fade -> nearest

      // ---- level exit.  Hexen's hub travel (Teleport_NewMap) has no equivalent
      // here, so treat it as a normal exit rather than doing nothing.
      case 74:							// Teleport_NewMap
	G_ExitLevel ();
	return true;

      default:
	if (special > 0 && special < 256 && !acs_reported[special])
	{
	    acs_reported[special] = true;
	    printf ("ACS: Hexen line special %d not implemented "
		    "(args %d %d %d %d %d) -- ignored\n",
		    special, args[0], args[1], args[2], args[3], args[4]);
	}
	return false;
    }
}

// ---------------------------------------------------------------------------
// Linedef activation on a Hexen-format map
//
// Hexen does not encode "walk once" / "switch, repeatable" in the special NUMBER
// the way DOOM does -- the special says only WHAT happens, and the linedef's
// flags say WHEN.  Bits 10-12 hold one of SPAC_CROSS/USE/MCROSS/IMPACT/PUSH/
// PCROSS, and ML_REPEAT_SPECIAL says whether it survives being triggered.
//
// This has to exist for polyobjects to be reachable at all -- a Polyobj_DoorSwing
// is just a special on a linedef, and nothing would ever run it otherwise.  It
// also fixes a real bug: P_CrossSpecialLine and friends were interpreting Hexen
// specials with DOOM's table, so walking over a Hexen line numbered 4
// (Polyobj_Move) fired DOOM's special 4, "W1 Door Raise".  Every Hexen line was
// doing something arbitrary.
// ---------------------------------------------------------------------------

#define ML_SPAC_SHIFT		10
#define ML_SPAC_MASK		0x1c00
#define ML_REPEAT_SPECIAL	0x0200

boolean P_ActivateLine (line_t* line, mobj_t* mo, int side, int activationType)
{
    int		lineActivation = (line->flags & ML_SPAC_MASK) >> ML_SPAC_SHIFT;
    boolean	repeat;
    boolean	ok;

    if (!line->special)
	return false;
    if (lineActivation != activationType)
	return false;

    if (mo && !mo->player && !(mo->flags & MF_MISSILE))
    {
	// Monsters may only trip MCROSS lines, and never a secret door -- without
	// this an ettin wandering the level would open the map's doors for you.
	if (lineActivation != SPAC_MCROSS)
	    return false;
	if (line->flags & ML_SECRET)
	    return false;
    }

    repeat = (line->flags & ML_REPEAT_SPECIAL) != 0;
    ok = P_ExecuteLineSpecial (line->special, line->args, line, side, mo);

    if (ok && !repeat)
	line->special = 0;		// one-shot: spent

    return ok;
}

// ---------------------------------------------------------------------------
// The interpreter
// ---------------------------------------------------------------------------

// ACS0 opcodes, in Hexen's order.
enum
{
    PCD_NOP, PCD_TERMINATE, PCD_SUSPEND, PCD_PUSHNUMBER,
    PCD_LSPEC1, PCD_LSPEC2, PCD_LSPEC3, PCD_LSPEC4, PCD_LSPEC5,
    PCD_LSPEC1DIRECT, PCD_LSPEC2DIRECT, PCD_LSPEC3DIRECT, PCD_LSPEC4DIRECT,
    PCD_LSPEC5DIRECT,
    PCD_ADD, PCD_SUBTRACT, PCD_MULTIPLY, PCD_DIVIDE, PCD_MODULUS,
    PCD_EQ, PCD_NE, PCD_LT, PCD_GT, PCD_LE, PCD_GE,
    PCD_ASSIGNSCRIPTVAR, PCD_ASSIGNMAPVAR, PCD_ASSIGNWORLDVAR,
    PCD_PUSHSCRIPTVAR, PCD_PUSHMAPVAR, PCD_PUSHWORLDVAR,
    PCD_ADDSCRIPTVAR, PCD_ADDMAPVAR, PCD_ADDWORLDVAR,
    PCD_SUBSCRIPTVAR, PCD_SUBMAPVAR, PCD_SUBWORLDVAR,
    PCD_MULSCRIPTVAR, PCD_MULMAPVAR, PCD_MULWORLDVAR,
    PCD_DIVSCRIPTVAR, PCD_DIVMAPVAR, PCD_DIVWORLDVAR,
    PCD_MODSCRIPTVAR, PCD_MODMAPVAR, PCD_MODWORLDVAR,
    PCD_INCSCRIPTVAR, PCD_INCMAPVAR, PCD_INCWORLDVAR,
    PCD_DECSCRIPTVAR, PCD_DECMAPVAR, PCD_DECWORLDVAR,
    PCD_GOTO, PCD_IFGOTO, PCD_DROP, PCD_DELAY, PCD_DELAYDIRECT,
    PCD_RANDOM, PCD_RANDOMDIRECT, PCD_THINGCOUNT, PCD_THINGCOUNTDIRECT,
    PCD_TAGWAIT, PCD_TAGWAITDIRECT, PCD_POLYWAIT, PCD_POLYWAITDIRECT,
    PCD_CHANGEFLOOR, PCD_CHANGEFLOORDIRECT, PCD_CHANGECEILING,
    PCD_CHANGECEILINGDIRECT, PCD_RESTART,
    PCD_ANDLOGICAL, PCD_ORLOGICAL, PCD_ANDBITWISE, PCD_ORBITWISE, PCD_EORBITWISE,
    PCD_NEGATELOGICAL, PCD_LSHIFT, PCD_RSHIFT, PCD_UNARYMINUS, PCD_IFNOTGOTO,
    PCD_LINESIDE, PCD_SCRIPTWAIT, PCD_SCRIPTWAITDIRECT, PCD_CLEARLINESPECIAL,
    PCD_CASEGOTO, PCD_BEGINPRINT, PCD_ENDPRINT, PCD_PRINTSTRING, PCD_PRINTNUMBER,
    PCD_PRINTCHARACTER, PCD_PLAYERCOUNT, PCD_GAMETYPE, PCD_GAMESKILL, PCD_TIMER,
    PCD_SECTORSOUND, PCD_AMBIENTSOUND, PCD_SOUNDSEQUENCE, PCD_SETLINETEXTURE,
    PCD_SETLINEBLOCKING, PCD_SETLINESPECIAL, PCD_THINGSOUND, PCD_ENDPRINTBOLD,
    PCODE_COMMAND_COUNT
};

// Count actors of a DOOM type (ACS ThingCount(type, tid) -- tid is ignored, this
// engine has no TIDs, so it degenerates to "how many of that type are alive").
static int ThingCount (int type, int tid)
{
    thinker_t*	th;
    mobj_t*	mo;
    int		count = 0;

    (void) tid;
    if (type <= 0) return 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	if (th->function.acp1 != (actionf_p1) P_MobjThinker) continue;
	mo = (mobj_t*) th;
	if (mo->type == (mobjtype_t) type && mo->health > 0)
	    count++;
    }
    return count;
}

void T_InterpretACS (acs_t* script)
{
    int		cmd;
    int		action;
    int		i;
    acsInfo_t*	info;

    if (!ACSInfo) return;
    info = &ACSInfo[script->infoIndex];

    if (info->state == ASTE_TERMINATING)
    {
	info->state = ASTE_INACTIVE;
	ScriptFinished (script->number);
	P_RemoveThinker (&script->thinker);
	return;
    }
    if (info->state != ASTE_RUNNING)
	return;					// suspended / waiting

    if (script->delayCount)
    {
	script->delayCount--;
	return;
    }

    ACScript  = script;
    PCodePtr  = script->ip;

    // Run until the script yields (delay/wait) or ends.  The bounded loop is a
    // backstop against a script with a bad jump spinning the game forever.
    for (i = 0; i < 500000; i++)
    {
	cmd = LONG(*PCodePtr++);
	action = SCRIPT_CONTINUE;

	switch (cmd)
	{
	  case PCD_NOP: break;
	  case PCD_TERMINATE: action = SCRIPT_TERMINATE; break;
	  case PCD_SUSPEND: info->state = ASTE_SUSPENDED; action = SCRIPT_STOP; break;
	  case PCD_PUSHNUMBER: Push (LONG(*PCodePtr++)); break;

	  case PCD_LSPEC1: case PCD_LSPEC2: case PCD_LSPEC3:
	  case PCD_LSPEC4: case PCD_LSPEC5:
	    {
		int n = cmd - PCD_LSPEC1 + 1;
		int spec = LONG(*PCodePtr++);
		int a;
		memset (SpecArgs, 0, sizeof(SpecArgs));
		for (a = n - 1; a >= 0; a--) SpecArgs[a] = Pop ();
		{
		    byte b[5];
		    for (a = 0; a < 5; a++) b[a] = (byte) SpecArgs[a];
		    P_ExecuteLineSpecial (spec, b, script->line, script->side,
					  script->activator);
		}
	    }
	    break;

	  case PCD_LSPEC1DIRECT: case PCD_LSPEC2DIRECT: case PCD_LSPEC3DIRECT:
	  case PCD_LSPEC4DIRECT: case PCD_LSPEC5DIRECT:
	    {
		int n = cmd - PCD_LSPEC1DIRECT + 1;
		int spec = LONG(*PCodePtr++);
		int a;
		byte b[5];
		memset (SpecArgs, 0, sizeof(SpecArgs));
		for (a = 0; a < n; a++) SpecArgs[a] = LONG(*PCodePtr++);
		for (a = 0; a < 5; a++) b[a] = (byte) SpecArgs[a];
		P_ExecuteLineSpecial (spec, b, script->line, script->side,
				      script->activator);
	    }
	    break;

	  case PCD_ADD:      { int b = Pop(), a = Pop(); Push (a + b); } break;
	  case PCD_SUBTRACT: { int b = Pop(), a = Pop(); Push (a - b); } break;
	  case PCD_MULTIPLY: { int b = Pop(), a = Pop(); Push (a * b); } break;
	  case PCD_DIVIDE:   { int b = Pop(), a = Pop(); Push (b ? a / b : 0); } break;
	  case PCD_MODULUS:  { int b = Pop(), a = Pop(); Push (b ? a % b : 0); } break;
	  case PCD_EQ: { int b = Pop(), a = Pop(); Push (a == b); } break;
	  case PCD_NE: { int b = Pop(), a = Pop(); Push (a != b); } break;
	  case PCD_LT: { int b = Pop(), a = Pop(); Push (a <  b); } break;
	  case PCD_GT: { int b = Pop(), a = Pop(); Push (a >  b); } break;
	  case PCD_LE: { int b = Pop(), a = Pop(); Push (a <= b); } break;
	  case PCD_GE: { int b = Pop(), a = Pop(); Push (a >= b); } break;

	  case PCD_ASSIGNSCRIPTVAR: script->vars[LONG(*PCodePtr++)] = Pop(); break;
	  case PCD_ASSIGNMAPVAR:    MapVars[LONG(*PCodePtr++)] = Pop(); break;
	  case PCD_ASSIGNWORLDVAR:  ACSWorldVars[LONG(*PCodePtr++)] = Pop(); break;
	  case PCD_PUSHSCRIPTVAR:   Push (script->vars[LONG(*PCodePtr++)]); break;
	  case PCD_PUSHMAPVAR:      Push (MapVars[LONG(*PCodePtr++)]); break;
	  case PCD_PUSHWORLDVAR:    Push (ACSWorldVars[LONG(*PCodePtr++)]); break;
	  case PCD_ADDSCRIPTVAR: script->vars[LONG(*PCodePtr++)] += Pop(); break;
	  case PCD_ADDMAPVAR:    MapVars[LONG(*PCodePtr++)] += Pop(); break;
	  case PCD_ADDWORLDVAR:  ACSWorldVars[LONG(*PCodePtr++)] += Pop(); break;
	  case PCD_SUBSCRIPTVAR: script->vars[LONG(*PCodePtr++)] -= Pop(); break;
	  case PCD_SUBMAPVAR:    MapVars[LONG(*PCodePtr++)] -= Pop(); break;
	  case PCD_SUBWORLDVAR:  ACSWorldVars[LONG(*PCodePtr++)] -= Pop(); break;
	  case PCD_MULSCRIPTVAR: script->vars[LONG(*PCodePtr++)] *= Pop(); break;
	  case PCD_MULMAPVAR:    MapVars[LONG(*PCodePtr++)] *= Pop(); break;
	  case PCD_MULWORLDVAR:  ACSWorldVars[LONG(*PCodePtr++)] *= Pop(); break;
	  case PCD_DIVSCRIPTVAR: { int v = LONG(*PCodePtr++), d = Pop(); if (d) script->vars[v] /= d; } break;
	  case PCD_DIVMAPVAR:    { int v = LONG(*PCodePtr++), d = Pop(); if (d) MapVars[v] /= d; } break;
	  case PCD_DIVWORLDVAR:  { int v = LONG(*PCodePtr++), d = Pop(); if (d) ACSWorldVars[v] /= d; } break;
	  case PCD_MODSCRIPTVAR: { int v = LONG(*PCodePtr++), d = Pop(); if (d) script->vars[v] %= d; } break;
	  case PCD_MODMAPVAR:    { int v = LONG(*PCodePtr++), d = Pop(); if (d) MapVars[v] %= d; } break;
	  case PCD_MODWORLDVAR:  { int v = LONG(*PCodePtr++), d = Pop(); if (d) ACSWorldVars[v] %= d; } break;
	  case PCD_INCSCRIPTVAR: script->vars[LONG(*PCodePtr++)]++; break;
	  case PCD_INCMAPVAR:    MapVars[LONG(*PCodePtr++)]++; break;
	  case PCD_INCWORLDVAR:  ACSWorldVars[LONG(*PCodePtr++)]++; break;
	  case PCD_DECSCRIPTVAR: script->vars[LONG(*PCodePtr++)]--; break;
	  case PCD_DECMAPVAR:    MapVars[LONG(*PCodePtr++)]--; break;
	  case PCD_DECWORLDVAR:  ACSWorldVars[LONG(*PCodePtr++)]--; break;

	  case PCD_GOTO:
	    PCodePtr = (int*) (ActionCodeBase + LONG(*PCodePtr));
	    break;
	  case PCD_IFGOTO:
	    if (Pop()) PCodePtr = (int*) (ActionCodeBase + LONG(*PCodePtr));
	    else       PCodePtr++;
	    break;
	  case PCD_IFNOTGOTO:
	    if (Pop()) PCodePtr++;
	    else       PCodePtr = (int*) (ActionCodeBase + LONG(*PCodePtr));
	    break;
	  case PCD_CASEGOTO:
	    if (Top() == LONG(*PCodePtr))
		{ PCodePtr = (int*) (ActionCodeBase + LONG(*(PCodePtr + 1))); Drop (); }
	    else
		PCodePtr += 2;
	    break;

	  case PCD_DROP: Drop (); break;
	  case PCD_DELAY:       script->delayCount = Pop();            action = SCRIPT_STOP; break;
	  case PCD_DELAYDIRECT: script->delayCount = LONG(*PCodePtr++); action = SCRIPT_STOP; break;
	  case PCD_RANDOM:       { int hi = Pop(), lo = Pop();
				   Push (lo + (P_Random() % (hi - lo + 1))); } break;
	  case PCD_RANDOMDIRECT: { int lo = LONG(*PCodePtr++), hi = LONG(*PCodePtr++);
				   Push (lo + (P_Random() % (hi - lo + 1))); } break;
	  case PCD_THINGCOUNT:       { int tid = Pop(), type = Pop(); Push (ThingCount (type, tid)); } break;
	  case PCD_THINGCOUNTDIRECT: { int type = LONG(*PCodePtr++), tid = LONG(*PCodePtr++);
				       Push (ThingCount (type, tid)); } break;

	  case PCD_TAGWAIT:
	    info->waitValue = Pop(); info->state = ASTE_WAITINGFORTAG;
	    action = SCRIPT_STOP; break;
	  case PCD_TAGWAITDIRECT:
	    info->waitValue = LONG(*PCodePtr++); info->state = ASTE_WAITINGFORTAG;
	    action = SCRIPT_STOP; break;

	  // Block until the polyobj finishes moving.  This used to return straight
	  // away because polyobjs never moved, which was right then and wrong now:
	  // a script that opens a door and waits would run the rest of itself in the
	  // same tic, before the door had opened at all.
	  case PCD_POLYWAIT:
	    info->waitValue = Pop(); info->state = ASTE_WAITINGFORPOLYOBJ;
	    action = SCRIPT_STOP; break;
	  case PCD_POLYWAITDIRECT:
	    info->waitValue = LONG(*PCodePtr++); info->state = ASTE_WAITINGFORPOLYOBJ;
	    action = SCRIPT_STOP; break;

	  case PCD_CHANGEFLOOR: case PCD_CHANGECEILING:
	    { int nameidx = Pop(), tag = Pop(); int flat, s;
	      flat = (nameidx >= 0 && nameidx < ACStringCount)
		     ? R_FlatNumForName (ACStrings[nameidx]) : -1;
	      if (flat >= 0)
		for (s = 0; s < numsectors; s++)
		    if (sectors[s].tag == tag)
		    { if (cmd == PCD_CHANGEFLOOR) sectors[s].floorpic = flat;
		      else                        sectors[s].ceilingpic = flat; } }
	    break;
	  case PCD_CHANGEFLOORDIRECT: case PCD_CHANGECEILINGDIRECT:
	    { int tag = LONG(*PCodePtr++), nameidx = LONG(*PCodePtr++); int flat, s;
	      flat = (nameidx >= 0 && nameidx < ACStringCount)
		     ? R_FlatNumForName (ACStrings[nameidx]) : -1;
	      if (flat >= 0)
		for (s = 0; s < numsectors; s++)
		    if (sectors[s].tag == tag)
		    { if (cmd == PCD_CHANGEFLOORDIRECT) sectors[s].floorpic = flat;
		      else                              sectors[s].ceilingpic = flat; } }
	    break;

	  case PCD_RESTART: PCodePtr = info->address; break;

	  case PCD_ANDLOGICAL: { int b = Pop(), a = Pop(); Push (a && b); } break;
	  case PCD_ORLOGICAL:  { int b = Pop(), a = Pop(); Push (a || b); } break;
	  case PCD_ANDBITWISE: { int b = Pop(), a = Pop(); Push (a & b); } break;
	  case PCD_ORBITWISE:  { int b = Pop(), a = Pop(); Push (a | b); } break;
	  case PCD_EORBITWISE: { int b = Pop(), a = Pop(); Push (a ^ b); } break;
	  case PCD_NEGATELOGICAL: Push (!Pop()); break;
	  case PCD_LSHIFT: { int b = Pop(), a = Pop(); Push (a << b); } break;
	  case PCD_RSHIFT: { int b = Pop(), a = Pop(); Push (a >> b); } break;
	  case PCD_UNARYMINUS: Push (-Pop()); break;

	  case PCD_LINESIDE: Push (script->side); break;

	  case PCD_SCRIPTWAIT:
	    info->waitValue = Pop(); info->state = ASTE_WAITINGFORSCRIPT;
	    action = SCRIPT_STOP; break;
	  case PCD_SCRIPTWAITDIRECT:
	    info->waitValue = LONG(*PCodePtr++); info->state = ASTE_WAITINGFORSCRIPT;
	    action = SCRIPT_STOP; break;

	  case PCD_CLEARLINESPECIAL:
	    if (script->line) script->line->special = 0;
	    break;
	  case PCD_SETLINESPECIAL:
	    { int a5 = Pop(), a4 = Pop(), a3 = Pop(), a2 = Pop(), a1 = Pop();
	      int spec = Pop(), tag = Pop(); int l;
	      (void)a2; (void)a3; (void)a4; (void)a5;
	      for (l = 0; l < numlines; l++)
		  if (lines[l].tag == tag)
		  { lines[l].special = spec; lines[l].tag = a1; } }
	    break;
	  case PCD_SETLINEBLOCKING:
	    { int blocking = Pop(), tag = Pop(); int l;
	      for (l = 0; l < numlines; l++)
		  if (lines[l].tag == tag)
		  { if (blocking) lines[l].flags |= ML_BLOCKING;
		    else          lines[l].flags &= ~ML_BLOCKING; } }
	    break;
	  case PCD_SETLINETEXTURE:
	    // Needs Hexen's side/position texture model; accept and ignore so the
	    // script continues rather than dying on an unknown opcode.
	    Pop(); Pop(); Pop(); Pop();
	    break;

	  case PCD_BEGINPRINT: PrintBuffer[0] = 0; break;
	  case PCD_ENDPRINT:
	  case PCD_ENDPRINTBOLD:
	    // Route to the ordinary HUD message line -- Hexen's centred/bold styles
	    // are not distinguished here.
	    if (script->activator && script->activator->player)
		players[script->activator->player - players].message = PrintBuffer;
	    else
		players[consoleplayer].message = PrintBuffer;
	    break;
	  case PCD_PRINTSTRING:
	    { int idx = Pop();
	      if (idx >= 0 && idx < ACStringCount)
		  strncat (PrintBuffer, ACStrings[idx],
			   PRINT_BUFFER_SIZE - strlen(PrintBuffer) - 1); }
	    break;
	  case PCD_PRINTNUMBER:
	    { char tmp[16]; snprintf (tmp, sizeof tmp, "%d", Pop());
	      strncat (PrintBuffer, tmp, PRINT_BUFFER_SIZE - strlen(PrintBuffer) - 1); }
	    break;
	  case PCD_PRINTCHARACTER:
	    { size_t l = strlen (PrintBuffer);
	      if (l < PRINT_BUFFER_SIZE - 1) { PrintBuffer[l] = (char) Pop(); PrintBuffer[l+1] = 0; }
	      else Pop (); }
	    break;

	  case PCD_PLAYERCOUNT:
	    { int p, n = 0; for (p = 0; p < MAXPLAYERS; p++) if (playeringame[p]) n++; Push (n); }
	    break;
	  case PCD_GAMETYPE:
	    Push (deathmatch ? 2 : (netgame ? 1 : 0));
	    break;
	  case PCD_GAMESKILL: Push (gameskill); break;
	  case PCD_TIMER:     Push (leveltime); break;

	  case PCD_SECTORSOUND:
	    { int vol = Pop(), nameidx = Pop(); (void)vol; (void)nameidx;
	      // Hexen addresses sounds by SNDINFO name; this engine has no such map
	      // for arbitrary script sounds, so the call is accepted and dropped.
	    }
	    break;
	  case PCD_THINGSOUND:  Pop (); Pop (); Pop (); break;
	  case PCD_AMBIENTSOUND: Pop (); Pop (); break;
	  case PCD_SOUNDSEQUENCE: Pop (); break;

	  default:
	    printf ("ACS: script %d hit unknown opcode %d -- terminated\n",
		    script->number, cmd);
	    action = SCRIPT_TERMINATE;
	    break;
	}

	if (action == SCRIPT_TERMINATE)
	{
	    info->state = ASTE_INACTIVE;
	    ScriptFinished (script->number);
	    P_RemoveThinker (&script->thinker);
	    return;
	}
	if (action == SCRIPT_STOP)
	    break;
    }

    script->ip = PCodePtr;
}
