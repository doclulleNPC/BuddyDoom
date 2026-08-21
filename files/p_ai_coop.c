// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Co-op companion (player 2).  Pick one of:
//	  -coop    : rule-based bot (current default; runs without any LLM)
//	  -aicoop  : AI-driven companion layer (see AI_IMPROVEMENTS.md #1)
//	          -- until that ships, -aicoop is a stub that falls back to
//	          the rule-based bot.  Once the AI layer is implemented, the
//	          same flag will instead route ticcmd generation through an LLM
//	          director (with the rule bot as the timeout/failure fallback).
//
//	Specifying both flags at once is a user error -- the two are mutually
//	exclusive.  P_AICoop_Init prints a warning and disables the buddy.
//
//	Single-machine only (no real netgame): the cmd is generated locally and
//	never carried over the wire.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdarg.h>		// NavPrint -- console + run/navdbg.txt in one call
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "doomdef.h"
#include "doomdata.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_items.h"
#include "p_invent.h"		// inventory: spend a stimpack/medikit to revive
#include "p_secdrone.h"		// P_AICoop_MaybeSpawnDrone (deploy a friendly Security Drone)
#include "p_companion.h"		// Companion_IsEnemy -- the shared enemy-filter predicate
#include "revmarine.h"		// RevMarine_BuddyTryRevive (buddy revives a dead marine)
#include "m_argv.h"
#include "p_local.h"
#include "p_mobj.h"
#include "info.h"
#include "r_main.h"
#include "r_state.h"
#include "tables.h"
#include "m_fixed.h"

#include "p_ai_coop.h"
#include "c_console.h"		// C_Printf -- the "navdbg" pathfinding dump
#include "p_buddydef.h"		// buddystats_t / P_Buddy_GetStats -- apply the selected buddy's body
#include "sounds.h"		// sfx_bd_* -- buddy see/pain/death/active slots
#include "s_sound.h"		// S_StartSound for the buddy's own voice

// Buddy player-colour (v_png.c / r_things.c / m_menu.c)
extern int		buddy_color;			// selected colour index (Buddy menu, config)
extern int		buddy_select;			// selected roster slot (0 = Marine, 1..N = BUDDYDEF)
extern const byte*	V_BuddyColorTable (int);	// 256-entry remap, NULL = Green(0)/identity
extern void		R_SetBuddyColor (mobj_t*, const byte*);
extern void		R_SetBuddySkin (mobj_t*, int);	// render player 2 with the buddy's body sprite
extern int		P_Buddy_Sprite (int slot);	// BUDDYDEF preview/skin spritenum

static int	companion_active;	// buddy enabled (-coop OR -aicoop)
static int	aicoop_layer;		// -aicoop given: AI-driven layer requested
static int	buddy_react;		// reaction delay (tics) before firing a fresh target (-buddyreact)
static fixed_t	buddy_movescale = FRACUNIT;	// (buddy) BUDDYDEF speed -> movement scale (1.0 = Marine)
static int	coop_state;		// 0 follow 1 fight 2 heal 3 hold 4 come 5 items
static int	summon;			// "come": tics left running to the player
static int	summon_stay;		// "come" leash: stay near the player (LOS) until another order
static int	hold;			// "wait/stay": hold position (director tactic BUD_HOLD)
static int	user_hold;		// "stay" issued by the HUMAN: sticky, overrides the
					// director, cleared only by come/attack (not by SetTactic)
static int	forceaggro;		// "attack": tics left charging forcetarget
static mobj_t*	forcetarget;		// the forced attack target
static int	ai_goto;		// LLM "goto": tics left moving to (ai_gx,ai_gy)
static fixed_t	ai_gx, ai_gy;		// LLM goto destination
static int	react_timer;		// tics left before firing a freshly-sighted target
static mobj_t*	react_last;		// the target the reaction timer is counting for

#define COOP_BLAST_SAFE	(176*FRACUNIT)	// don't fire rocket/BFG at a target closer than this (splash)
#define COOP_BARREL_SAFE (141*FRACUNIT)	// barrel blast (A_Explode radius 128) + 10%, rounded up = ceil(128*1.1)
#define COOP_DODGE_RANGE (256*FRACUNIT)	// react to incoming missiles within this range

// Breadcrumb trail: the human's recent walkable positions.  When the buddy can't
// make progress toward the player (the portal pathfinder is stuck in some tight
// geometry), it replays this trail -- every crumb is a spot the human actually
// stood on, so it is guaranteed reachable and threads the exact gap the human used.
#define CRUMB_MAX	48
#define CRUMB_GAP	(48*FRACUNIT)	// drop a crumb each ~48u the human moves
static fixed_t	crumbx[CRUMB_MAX], crumby[CRUMB_MAX];
static int	crumb_n;		// crumbs held (crumb[0]=oldest .. crumb[n-1]=newest≈player)
static int	trail_active;		// currently following the breadcrumb trail
// Drop the nav graph and the cached path corridor (defined with the pathfinder).
// The graph bakes in sub-sector indices and door state, so a level load / savegame
// load must not keep reusing it.
static void PF_NavReset (void);

// Chain links between consecutive crumbs -- see AICoop_CrumbRelink (below the
// pathfinder, where the walkability probe it uses lives).  Runtime-only: not saved,
// rebuilt from the trail on first use after a load.
static byte	crumb_link[CRUMB_MAX];	// link[i]: crumb i -> crumb i+1 is walkable
static int	crumb_link_tic = -1;	// gametic of the last rebuild (-1 = never)

// "Am I getting anywhere?" watchdog state.  File scope so a new level / a load can
// reset it -- a stale best_pld from the previous map reads as "no progress" forever.
static int	prog_tic;		// gametic of the last 1 Hz check
static fixed_t	best_pld = 0x7fffffff;	// closest we have ever gotten to the human
static int	noprog;			// consecutive 1 Hz checks with no new best
static int	buddy_dbg = -1;		// BUDDYDBG env var, resolved once (-1 = not yet)

// Where the buddy has been for the last ~2 s, one entry per tic (ring).  Dumped by
// AICoop_VoidLog: the END position only tells you where a geometry leak comes out --
// the tic it crossed from a real BSP leaf into solid space tells you where the leak is.
#define VTRACE_MAX	70
static fixed_t	vtrace_x[VTRACE_MAX], vtrace_y[VTRACE_MAX], vtrace_z[VTRACE_MAX];
static int	vtrace_head;		// next slot to write
static int	vtrace_n;		// entries written since level start (caps at VTRACE_MAX)
static boolean	void_was_outside;	// buddy was in solid space last tic (edge detect)

static void AICoop_CrumbAdd (fixed_t x, fixed_t y)
{
    if (crumb_n > 0 &&
	P_AproxDistance (x - crumbx[crumb_n-1], y - crumby[crumb_n-1]) < CRUMB_GAP)
	return;				// human hasn't moved a full step yet
    if (crumb_n == CRUMB_MAX)		// full -> drop the oldest
    {
	memmove (crumbx, crumbx+1, (CRUMB_MAX-1)*sizeof(fixed_t));
	memmove (crumby, crumby+1, (CRUMB_MAX-1)*sizeof(fixed_t));
	crumb_n--;
    }
    crumbx[crumb_n] = x; crumby[crumb_n] = y; crumb_n++;
}


#define COOP_SIGHT	(1280*FRACUNIT)	// monster acquisition range
#define COOP_TURN	1300		// max angleturn per tic (~7 deg)
#define COOP_FACING	1500		// |remaining turn| under which we open fire
#define COOP_LOOKMAX	56		// vertical aim clamp (mirrors g_game.c LOOKDIRMAX)
#define COOP_NEAR	(256*FRACUNIT)	// follow distance to the human
#define YIELD_DIST	(48*FRACUNIT)	// human this close -> step out of the way
#define COOP_KEEP	(192*FRACUNIT)	// advance toward a monster until this close
#define COOP_RUN	0x32		// forwardmove "run" magnitude
#define COOP_MINMOVE	12		// floor for the damped move (see AICoop_ThrustToward)
#define COOP_HEAL_HP	50		// seek a med-pack below this health
#define COOP_SAFE_HP	40		// below this HP the buddy routes home the low-danger way
#define COOP_REVIVE_RANGE (96*FRACUNIT)	// human must stand this close (and press USE) to revive
					// (64 was too tight -- a corpse slid into a corner/onto
					// a ledge couldn't be reached; sight check guards walls)
#define COOP_HEAL_RANGE	(1024*FRACUNIT)	// how far to look for one
#define COOP_ITEM_RANGE	(128*FRACUNIT)	// idle pickups only when right nearby (not "miles away")
#define COOP_GRAB_NEAR	(512*FRACUNIT)	// only grab items while still near the human (else follow)
#define COOP_SUMMON_TICS (7*TICRATE)	// "come" runs to you for this long
#define COOP_CAUTION_HP	50		// below this HP the buddy plays safe: stays near you
#define COOP_LEASH	(640*FRACUNIT)	// come-stay / cautious: max stray from the player
#define COOP_ENGAGE_NEAR (448*FRACUNIT)	// while staying close, only fight threats this near us
#define COOP_ATTACK_TICS (10*TICRATE)	// "attack" charges the target for this long


static int	coop_slot = 1;		// player index the buddy occupies (single-player: 1)

// Call after D_CheckNetGame.  The buddy is SINGLE-PLAYER ONLY: in a netgame it is
// disabled, so real network games run without it (clean lockstep, no extra slot).
void P_AICoop_Init (void)
{
    // -coop  : autonomous rule-based bot.
    // -aicoop: the rule-based bot PLUS the LLM director layer -- an external
    //          director (run/director) sets the buddy's high-level tactic each
    //          cycle (engage/defend/hold/regroup/retreat/grab) over the same TCP
    //          transport it uses for the monsters; the rule-based primitives
    //          execute it, and the buddy reverts to autonomous when the director
    //          goes quiet.  See AGENT_CONTROL.md / p_ai_llm.c.
    int coop    = M_CheckParm ("-coop")    > 0;
    int aicoop  = M_CheckParm ("-aicoop")  > 0;
    int rp      = M_CheckParm ("-buddyreact");	// reaction-time / skill knob (tics)

    // -buddyreact <tics>: delay between sighting a fresh target and opening fire
    // (0 = frame-perfect, the old behaviour; ~14 = a human-ish ~0.4s; higher = dumber).
    if (rp && rp < myargc-1)
    {
	buddy_react = atoi (myargv[rp+1]);
	if (buddy_react < 0)  buddy_react = 0;
	if (buddy_react > 70) buddy_react = 70;
    }

    // Default ON: a plain single-player launch (no flags) gets the rule-based AI buddy
    // (player 2).  Opt out for -vanilla (purist 1993 mode), a netgame (co-op is SP-only),
    // or demo RECORDING.  Demo PLAYBACK is self-gating: G_DoPlayDemo overwrites
    // playeringame[] from the demo header, and P_AICoop_BuildCmd early-outs when
    // playeringame[coop_slot] is false -- so attract demos stay vanilla.
    if (!coop && !aicoop && !vanilla_mode && !netgame && !M_CheckParm ("-record"))
	coop = 1;

    if (!coop && !aicoop)
	return;

    if (coop && aicoop)
    {
	printf ("P_AICoop: -coop and -aicoop are mutually exclusive.  Pick one:\n"
		"  -coop    rule-based buddy (no LLM needed)\n"
		"  -aicoop  AI-driven buddy (LLM-backed; today falls back to -coop)\n"
		"P_AICoop: co-op companion disabled.  Relaunch with one of the two.\n");
	return;
    }

    if (netgame)
    {
	printf ("P_AICoop: co-op companion is single-player only -- disabled in netgames.\n");
	return;
    }

    coop_slot = 1;
    companion_active = 1;
    aicoop_layer = aicoop;		// -aicoop: accept director tactics for the buddy
				// (P_AICoop_AIMode / P_AICoop_SetDirective) + expose
				// it in the AI `observe` stream.
    playeringame[1] = true;	// spawn the buddy at the co-op start

    if (aicoop)
	printf ("P_AICoop: AI-directed co-op companion enabled (player 2) -- "
		"connect the director to drive its tactics.\n");
    else
	printf ("P_AICoop: rule-based co-op companion enabled (player 2)\n");
}

// Called from P_SetupLevel after P_LoadThings.  If -coop/-aicoop was given
// but the map has no Player_2_Start, the buddy can't spawn this level --
// disable it (so P_AICoop_Slot() returns -1 and the build-cmd path is a no-op)
// and emit a one-shot warning telling the user what to fix.
// Called from P_SetupLevel just before P_LoadThings.  Drops the buddy slot's
// stale mobj AND its stale playerstarts[] entry so that P_AICoop_VerifySpawn
// can reliably distinguish "this map's THINGS contain a Player_2_Start" from
// "this map has no Player_2_Start".
//
// Why we have to touch both:
//   - players[coop_slot].mo is a dangling pointer across map loads (the mobj
//     is freed by Z_FreeTags but the pointer field in the static players[]
//     struct is not zeroed).  Nulling it here makes the post-load "mo != NULL"
//     check reliable.
//   - playerstarts[coop_slot] is also static across map loads and only updated
//     by P_LoadThings if the map has a matching Player_X_Start thing.  If we
//     don't reset it, an E?M? that doesn't override the IWAD's playerstarts[]
//     (e.g. testmap's E2M1 PWAD overlay on doom2's E2M1, which retains the
//     IWAD's P2_Start) will falsely look like it had a P2_Start.  Resetting it
//     to a sentinel (type=0 = "no thing here") forces the post-load check to
//     see type=2 only when THIS map's THINGS explicitly set it.
//
// We deliberately do NOT touch playeringame[coop_slot]: it stays true (set
// by P_AICoop_Init), so P_SpawnPlayer inside P_LoadThings will spawn Player 2
// normally when the map has a Player_2_Start thing.
void P_AICoop_ResetSlot (void)
{
    if (!companion_active) return;
    if (netgame) return;

    players[coop_slot].mo = NULL;
    playerstarts[coop_slot].type = 0;	// sentinel: "no P2_Start thing on this map"
    playerstarts[coop_slot].x = 0;
    playerstarts[coop_slot].y = 0;
    playerstarts[coop_slot].angle = 0;
    playerstarts[coop_slot].options = 0;

    crumb_n = 0; trail_active = 0;	// drop the previous map's breadcrumb trail
    crumb_link_tic = -1;		// ...and its link table (rebuilt on first use)
    best_pld = 0x7fffffff; noprog = 0;	// ...and the progress watchdog's running minimum
    vtrace_head = 0; vtrace_n = 0;	// ...and the position trace ring
    void_was_outside = false;		// ...and the "in solid space" edge detector
    PF_NavReset ();			// ...and the nav graph + cached corridor
    summon = 0; summon_stay = 0;	// drop any come/leash order from the previous map
}

// ---------------------------------------------------------------------------
//  Savegame: persist the breadcrumb trail so the buddy keeps following the
//  human's path across a save/load (otherwise the trail is empty on load and the
//  buddy can be stranded behind a door the human already walked through).  Written
//  AFTER the consistency marker (see g_game.c), so older saves without the block
//  still load -- the loader only reads it when there are bytes left in the file.
// ---------------------------------------------------------------------------
extern byte* save_p;

void P_AICoop_ArchiveTrail (void)
{
    int i;
    memcpy (save_p, &crumb_n, sizeof(int)); save_p += sizeof(int);
    for (i = 0; i < crumb_n; i++)
    {
	memcpy (save_p, &crumbx[i], sizeof(fixed_t)); save_p += sizeof(fixed_t);
	memcpy (save_p, &crumby[i], sizeof(fixed_t)); save_p += sizeof(fixed_t);
    }
}

void P_AICoop_UnArchiveTrail (void)
{
    int i, n = 0;
    memcpy (&n, save_p, sizeof(int)); save_p += sizeof(int);
    if (n < 0 || n > CRUMB_MAX)		// corrupt/short block -> ignore, start fresh
	{ crumb_n = 0; trail_active = 0; return; }
    for (i = 0; i < n; i++)
    {
	memcpy (&crumbx[i], save_p, sizeof(fixed_t)); save_p += sizeof(fixed_t);
	memcpy (&crumby[i], save_p, sizeof(fixed_t)); save_p += sizeof(fixed_t);
    }
    crumb_n = n;
    trail_active = 0;			// let the watchdog re-engage if the buddy lags
    crumb_link_tic = -1;		// links aren't saved -- rebuild from the loaded trail
    best_pld = 0x7fffffff; noprog = 0;	// don't judge progress against the pre-save distance
    PF_NavReset ();			// P_SetupLevel reloaded subsectors/segs under the graph
}

static boolean P_AICoop_VerifySpawn_warned = false;	// one-shot per process

// Where the buddy spawned on the current map (captured in VerifySpawn), so the
// "buddyhome" console command can teleport it back there if it gets stuck/lost.
static fixed_t	coop_home_x, coop_home_y;
static angle_t	coop_home_angle;
static boolean	coop_home_set;

// A point is only safe for the buddy to stand on if it lies inside the BLOCKMAP
// grid.  Outside it, P_CheckPosition's line iteration walks no cells, so collision
// is off and the buddy floats in the void (the exact failure that stranded it past
// a boundary wall).  Used to confirm a teleport-home actually landed it back inside
// the map rather than dropping it into another void spot.
static boolean AICoop_OnGrid (fixed_t x, fixed_t y)
{
    int	cx = (x - bmaporgx) >> MAPBLOCKSHIFT;
    int	cy = (y - bmaporgy) >> MAPBLOCKSHIFT;
    return (cx >= 0 && cx < bmapwidth && cy >= 0 && cy < bmapheight);
}

// (buddy) The sfx id to play for the buddy body's <which> sound (BUDDYSND_*), or -1 to
// fall back to the default player sound.  Only for the active alternative buddy whose
// BUDDYDEF actually set that sound.  Called from A_Pain / A_PlayerScream (p_enemy.c) and
// the co-op ticker, so player 2 speaks with the selected buddy's own voice.
int P_Buddy_BodySfx (mobj_t* mo, int which)
{
    static const int slot[4] = { sfx_bd_see, sfx_bd_pain, sfx_bd_death, sfx_bd_active };
    const char*	nm;

    if (!companion_active || buddy_select <= 0 || which < 0 || which > 3)
	return -1;
    if (!mo || mo != players[coop_slot].mo)
	return -1;
    nm = P_Buddy_Sound (buddy_select, which);
    if (!nm || !*nm)
	return -1;
    return slot[which];
}

// (buddy) The selected buddy's painchance for its body (player 2), or -1 to use the
// mobjinfo default.  Lets P_DamageMobj roll the buddy's own flinch chance -- a low value
// makes a tough buddy shrug off hits, a high one makes it stagger.
int P_Buddy_BodyPainchance (mobj_t* mo)
{
    buddystats_t	st;
    if (!companion_active || buddy_select <= 0 || !mo || mo != players[coop_slot].mo)
	return -1;
    P_Buddy_GetStats (buddy_select, &st);
    return st.painchance;
}

// ---------------------------------------------------------------------------
// (buddy) Monster-style attack: an alternative buddy can borrow a Doom actor's melee /
// ranged attack (BUDDYDEF meleeattack/rangedattack) instead of firing the player weapons.
// The Doom attack codepointers are self-contained -- they hard-code their own missile type
// + damage and act on actor->target -- so calling one on the player-2 body just works.
// The Heretic/Hexen/Strife attacks live in static per-game modules (not linkable here), so
// those names fall back to the closest Doom attack (bite for melee, imp fireball for ranged).
// ---------------------------------------------------------------------------
typedef void (*buddyatk_t)(mobj_t*);
extern void A_PosAttack(mobj_t*), A_SPosAttack(mobj_t*), A_CPosAttack(mobj_t*),
	    A_TroopAttack(mobj_t*), A_SargAttack(mobj_t*), A_HeadAttack(mobj_t*),
	    A_BruisAttack(mobj_t*), A_VileAttack(mobj_t*), A_SkelFist(mobj_t*),
	    A_SkelMissile(mobj_t*), A_FatAttack1(mobj_t*), A_BspiAttack(mobj_t*),
	    A_CyberAttack(mobj_t*), A_SkullAttack(mobj_t*), A_PainAttack(mobj_t*),
	    A_FaceTarget(mobj_t*);
extern boolean P_CheckMeleeRange (mobj_t* actor);

static buddyatk_t Buddy_MeleeFn (const char* n)
{
    if (!n || !*n || !strcasecmp (n, "none")) return NULL;
    if (!strcasecmp(n,"revenant"))			return A_SkelFist;
    if (!strcasecmp(n,"imp")||!strcasecmp(n,"gargoyle"))return A_TroopAttack;
    if (!strcasecmp(n,"cacodemon"))			return A_HeadAttack;
    if (!strcasecmp(n,"baron")||!strcasecmp(n,"hellknight")) return A_BruisAttack;
    if (!strcasecmp(n,"archvile"))			return A_VileAttack;
    return A_SargAttack;				// demon bite -- generic melee
}
static buddyatk_t Buddy_RangedFn (const char* n)
{
    if (!n || !*n || !strcasecmp (n, "none")) return NULL;
    if (!strcasecmp(n,"zombieman"))			return A_PosAttack;
    if (!strcasecmp(n,"shotgunguy"))			return A_SPosAttack;
    if (!strcasecmp(n,"chaingunner"))			return A_CPosAttack;
    if (!strcasecmp(n,"cacodemon"))			return A_HeadAttack;
    if (!strcasecmp(n,"baron")||!strcasecmp(n,"hellknight")) return A_BruisAttack;
    if (!strcasecmp(n,"revenant"))			return A_SkelMissile;
    if (!strcasecmp(n,"mancubus"))			return A_FatAttack1;
    if (!strcasecmp(n,"arachnotron"))			return A_BspiAttack;
    if (!strcasecmp(n,"cyberdemon"))			return A_CyberAttack;
    if (!strcasecmp(n,"lostsoul"))			return A_SkullAttack;
    if (!strcasecmp(n,"painelemental"))			return A_PainAttack;
    return A_TroopAttack;				// imp fireball -- generic ranged
}

// Perform the buddy's borrowed attack on `target`.  Returns:  1 = attacked this tic,
// 0 = has a monster attack but not firing now (cooling down / closing to melee),
// -1 = no monster attack -> the caller should fire the player weapon as usual.
int P_Buddy_DoAttack (mobj_t* buddy, mobj_t* target)
{
    static int	cd;
    buddystats_t st;
    buddyatk_t	melee, ranged, fn;

    if (buddy_select <= 0) return -1;
    P_Buddy_GetStats (buddy_select, &st);
    melee  = Buddy_MeleeFn  (st.melee);
    ranged = Buddy_RangedFn (st.ranged);
    if (!melee && !ranged) return -1;			// plain buddy -> weapon
    if (cd > 0) { cd--; return 0; }			// between swings/shots
    if (!buddy || !target || target->health <= 0) return 0;

    buddy->target = target;
    A_FaceTarget (buddy);				// aim the missile/bite
    if (melee && P_CheckMeleeRange (buddy)) { fn = melee;  cd = 12; }
    else if (ranged)			    { fn = ranged; cd = 18; }
    else return 0;					// melee-only + out of range: keep closing
    fn (buddy);
    return 1;
}

void P_AICoop_VerifySpawn (void)
{
    mobj_t*	buddy_mo;

    if (!companion_active) return;			// buddy not requested
    if (netgame) return;					// not single-player, off

    // Has the map's Player_2_Start thing produced a real mobj?  P_LoadThings
    // would have written playerstarts[1] and called P_SpawnPlayer.  The
    // authoritative check is: does the buddy player have a mobj?
    // (P_AICoop_ResetSlot nulled players[coop_slot].mo before P_LoadThings,
    // so any non-NULL mo now is the result of THIS map's THINGS, not a leftover
    // from the previous map.  playeringame[coop_slot] stays true throughout
    // because P_AICoop_Init set it; P_SpawnPlayer in P_LoadThings respected
    // that, so the buddy is in the game if and only if there was a real P2 thing.)
    buddy_mo = players[coop_slot].mo;
    // The mobj-pointer check alone is unreliable because players[coop_slot].mo
    // can hold a dangling heap pointer from the previous map's spawn (Z_FreeTags
    // frees the mobj but doesn't NULL the field in the static players[] struct).
    // We cross-check against playerstarts[coop_slot].type, which P_LoadThings
    // set to 2 only if THIS map's THINGS contained a matching Player_2_Start;
    // we reset it to a sentinel in P_AICoop_ResetSlot, so type==2 here is
    // authoritative for "this map had a P2_Start thing".
    if (buddy_mo != NULL && playerstarts[coop_slot].type == 2)
    {
	// Remember the spawn point for "buddyhome".
	coop_home_x = buddy_mo->x; coop_home_y = buddy_mo->y;
	coop_home_angle = buddy_mo->angle; coop_home_set = true;

	// (buddy) Apply the selected BUDDYDEF buddy's body stats to player 2 once, at spawn
	// (slot 0 = Marine keeps the stock player body).  Per-instance fields only, so this
	// never touches mobjinfo/savegames.  health seeds the spawn HP; radius/height resize
	// the collision box (BUDDYDEF stores plain map units -> <<FRACBITS).
	buddy_movescale = FRACUNIT;		// Marine default (overridden below for alt buddies)
	if (buddy_select > 0)
	{
	    buddystats_t st;
	    P_Buddy_GetStats (buddy_select, &st);
	    if (st.health > 0)
	    { buddy_mo->health = st.health; players[coop_slot].health = st.health; }
	    if (st.radius > 0) buddy_mo->radius = st.radius << FRACBITS;
	    if (st.height > 0) buddy_mo->height = st.height << FRACBITS;
	    // Behaviour stats: reactiontime -> the fire delay on a fresh target; speed ->
	    // a movement scale (relative to the BUDDYDEF default of 8, clamped 0.5x..2x);
	    // painchance is read per-hit in P_DamageMobj (P_Buddy_BodyPainchance).
	    if (st.reactiontime >= 0 && st.reactiontime <= 70) buddy_react = st.reactiontime;
	    buddy_movescale = st.speed > 0
		? (st.speed * FRACUNIT / 8 < FRACUNIT/2 ? FRACUNIT/2
		   : st.speed * FRACUNIT / 8 > 2*FRACUNIT ? 2*FRACUNIT
		   : st.speed * FRACUNIT / 8)
		: FRACUNIT;
	    // Load the buddy's custom see/pain/death/active sounds into the reserved sfx_bd_*
	    // slots (silent if the BUDDYDEF set none).  Loads once per session per sound.
	    { extern void I_LoadBuddySfx (int, const char*);
	      I_LoadBuddySfx (sfx_bd_see,    P_Buddy_Sound (buddy_select, BUDDYSND_SEE));
	      I_LoadBuddySfx (sfx_bd_pain,   P_Buddy_Sound (buddy_select, BUDDYSND_PAIN));
	      I_LoadBuddySfx (sfx_bd_death,  P_Buddy_Sound (buddy_select, BUDDYSND_DEATH));
	      I_LoadBuddySfx (sfx_bd_active, P_Buddy_Sound (buddy_select, BUDDYSND_ACTIVE)); }
	}
	return;				// all good, buddy spawned
    }

    // Map has no Player_2_Start.  Disable for this level (and all subsequent
    // levels until the user fixes the WAD or removes -coop), and tell them.
    companion_active = 0;				// local-only; not persisted

    // CRITICAL: P_AICoop_Init set playeringame[coop_slot]=true so P_LoadThings would
    // spawn the buddy.  With no P2_Start it never got a mobj, yet P_Ticker still calls
    // P_PlayerThink(&players[coop_slot]) for every in-game slot -- which dereferences
    // players[coop_slot].mo (NULL here) and crashes.  Clear the slot now so the mo-less
    // buddy is skipped (also keeps intermission/HUD from counting a phantom player 2).
    playeringame[coop_slot] = false;

    if (!P_AICoop_VerifySpawn_warned)
    {
	printf ("\n"
		"P_AICoop: WARNING -- -coop/-aicoop requested but this map has no\n"
		"  Player_2_Start thing.  The co-op buddy will not spawn this level.\n"
		"  Fix: add a Player_2_Start to the map (any editor), or remove -coop.\n");
	P_AICoop_VerifySpawn_warned = true;
    }
}

// The slot the buddy occupies (-1 if disabled).  Used by g_game.c to skip the
// netgame consistency check for it -- the buddy is local-but-deterministic, never
// networked, so no remote command to validate against.
int P_AICoop_Slot (void)
{
    if (!companion_active) return -1;
    return coop_slot;
}

boolean P_AICoop_IsBuddy (player_t* p)
{
    return companion_active && p == &players[coop_slot];
}

// True if the AI companion (rule -coop or AI -aicoop) is in play -- the keyless buddy
// that benefits from doors the human has unlocked (see EV_VerticalDoor demotion).
int P_AICoop_Active (void)
{
    return companion_active;
}

// Public read-only accessor for coop_state (used by c_console.c for the voice
// tag mapping).  Returns -1 if the buddy is inactive.
int P_AICoop_State (void)
{
    if (!companion_active) return -1;
    return coop_state;
}

// The live companion mobj, or NULL if there isn't one right now.
static mobj_t* AICoop_Mo (void)
{
    if (!companion_active || !playeringame[coop_slot])		return NULL;
    if (players[coop_slot].playerstate != PST_LIVE)	return NULL;
    return players[coop_slot].mo;
}

// Nearest live human player to (x,y) -- the buddy follows/defends whoever's near
// (in single-player that's just player 0).  Deterministic (index tie-break).
static mobj_t* AICoop_NearestHuman (fixed_t x, fixed_t y)
{
    mobj_t*	best = NULL;
    fixed_t	bestd = 0;
    int		i;

    for (i = 0 ; i < MAXPLAYERS ; i++)
    {
	mobj_t*	m; fixed_t d;
	if (i == coop_slot || !playeringame[i])		continue;
	if (players[i].playerstate != PST_LIVE || !players[i].mo) continue;
	m = players[i].mo;
	d = P_AproxDistance (m->x - x, m->y - y);
	if (!best || d < bestd) { best = m; bestd = d; }
    }
    return best;
}


// ----------------------------------------------------------------- voice
// The buddy speaks through i_voice.c, which plays an offline-baked OGG from
// buddydoom.wad via a dedicated SDL3 audio stream.  We pass a "tag" (e.g.
// "contact:0", "state:fighting") and i_voice maps it to the right lump.
// All best-effort: if buddydoom.wad isn't present or the lump is missing, the
// call is a silent no-op and the deterministic playsim is unaffected.
#include "i_voice.h"

static const char* AICOOP_STATE_TAGS[] =
{
    "state:following",   // COOP_STATE_FOLLOW
    "state:fighting",    // COOP_STATE_FIGHT
    "state:healing",     // COOP_STATE_HEAL
    "state:holding",     // COOP_STATE_HOLD
    "state:coming",      // COOP_STATE_COME
    "state:grabbing",    // COOP_STATE_GRAB
};

// Doom positional-sound constants (mirror s_sound.c) for spatialising the voice.
#define VOICE_CLIPDIST  (1200*0x10000)
#define VOICE_CLOSEDIST (160*0x10000)
#define VOICE_ATTEN     ((VOICE_CLIPDIST-VOICE_CLOSEDIST)>>FRACBITS)
#define VOICE_SWING     (96*0x10000)

// Per-channel 0..127 gains so the buddy's voice comes from *its* world position
// (distance attenuation + stereo pan) -- exactly like Doom SFX
// (S_AdjustSoundParams + i_sound.c's x^2 separation).  lis = the listening human,
// src = the buddy.
static void AICoop_VoicePan (mobj_t* lis, mobj_t* src, int* lvol, int* rvol)
{
    fixed_t adx = abs (lis->x - src->x);
    fixed_t ady = abs (lis->y - src->y);
    fixed_t dist = adx + ady - ((adx < ady ? adx : ady) >> 1);
    angle_t ang;
    int     vol, sep, s;

    if (lis == src) { *lvol = *rvol = 127; return; }		// shouldn't happen
    if (dist > VOICE_CLIPDIST) { *lvol = *rvol = 0; return; }	// too far -> silent

    ang = R_PointToAngle2 (lis->x, lis->y, src->x, src->y) - lis->angle;
    sep = 128 - (FixedMul (VOICE_SWING, finesine[ang >> ANGLETOFINESHIFT]) >> FRACBITS);

    if (dist < VOICE_CLOSEDIST) vol = 127;
    else vol = 127 * ((VOICE_CLIPDIST - dist) >> FRACBITS) / VOICE_ATTEN;

    s = sep + 1;   *lvol = vol - ((vol*s*s) >> 16);		// Doom's x^2 pan
    s = s - 257;   *rvol = vol - ((vol*s*s) >> 16);
    if (*lvol < 0) *lvol = 0; if (*lvol > 127) *lvol = 127;
    if (*rvol < 0) *rvol = 0; if (*rvol > 127) *rvol = 127;
}

static void AICoop_SayTag (const char* tag)
{
    if (I_Voice_Busy ()) return;			// don't overlap the buddy's own line
    mobj_t*	src = AICoop_Mo ();				// the buddy = sound source
    mobj_t*	lis = playeringame[displayplayer] ? players[displayplayer].mo : NULL;
    int		lvol = 127, rvol = 127;
    if (src && lis && src != lis) AICoop_VoicePan (lis, src, &lvol, &rvol);
    I_Voice_Say (tag, lvol, rvol);
}

// --- Voice priority --------------------------------------------------------
enum { VP_AMBIENT = 0, VP_KILL = 1, VP_WEAPON = 2, VP_COMMAND = 3 };
static const int VP_GAP[4] = { 5*TICRATE, 3*TICRATE, 3*TICRATE, 0 };
static int vp_last[4];           // last gametic each tier spoke
static int vp_cur = -1;          // tier of the line currently sounding (-1 = idle)

// Decide whether a line of priority `prio` may speak right now.
static boolean AICoop_VoiceGate (int prio)
{
    extern int I_Director_Busy (void);
    // Defer ONLY ambient chatter to the Director (it keeps the voice-of-god dominant).
    // Important lines -- kills, weapon pickups, command acks, going down -- still play
    // on the buddy's OWN stream (the two personas have separate streams so they mix).
    // Fully muting the buddy while the Director had any audio queued silenced it in
    // -director mode, where the rule Director talks almost continuously.
    if (prio == VP_AMBIENT && I_Director_Busy ()) return false;
    if (I_Voice_Busy ()) return false;            // Only 1 buddy line at once! Never self-overlap.

    if (gametic - vp_last[prio] < VP_GAP[prio])   // per-tier rate limit
        return false;
    vp_last[prio] = gametic;
    vp_cur        = prio;
    return true;
}

// Rate-limited automatic line at a given priority: rotates the tag suffixes
// "0".."n-1" so the buddy doesn't repeat the same phrase back-to-back.
// tagprefix is e.g. "contact:" or "kill:"; the index is appended ("contact:2").
static void AICoop_CalloutP (const char* tagprefix, int n, int prio)
{
    static int idx;
    if (!AICoop_VoiceGate (prio)) return;
    char buf[32];
    snprintf (buf, sizeof(buf), "%s%d", tagprefix, idx++ % n);
    AICoop_SayTag (buf);
}

// Most callouts are low-value ambient chatter.
static void AICoop_Callout (const char* tagprefix, int n)
{
    AICoop_CalloutP (tagprefix, n, VP_AMBIENT);
}

// A specific (non-rotated) tag at a given priority -- for command acks etc.
static void AICoop_SayTagP (const char* tag, int prio)
{
    if (!AICoop_VoiceGate (prio)) return;
    AICoop_SayTag (tag);
}

// Speak a tagged phrase through i_voice.c (offline OGG via buddydoom.wad).
// The "[Buddy] ..." console text is unaffected -- this is just the audio.
// Callers pick the exact tag (e.g. "summon_ok", "state:fighting"); the
// tag -> lump-name mapping lives in i_voice.c.  This is the console-command
// reply path (come/wait/attack/where), so it runs at VP_COMMAND: it always
// answers the player and preempts any lower-tier chatter in progress.
void P_AICoop_VoiceTag (const char* tag)
{
    if (!companion_active || !tag) return;
    AICoop_SayTagP (tag, VP_COMMAND);
}

// Public wrapper so other modules (p_inter.c) can trigger a rotated callout.
void P_AICoop_Callout (const char* prefix, int n)
{
    if (!companion_active) return;
    AICoop_Callout (prefix, n);
}

// Duke-style per-monster kill quip: tag (+ variant count in *n) for a victim type.
static const char* AICoop_KillTag (mobjtype_t t, int* n)
{
    *n = 1;
    switch (t)
    {
      case MT_TROOP:     *n = 3; return "killimp:";
      case MT_POSSESSED:        return "killzm:";
      case MT_SHOTGUY:          return "killsg:";
      case MT_CHAINGUY:         return "killcg:";
      case MT_SERGEANT:         return "killpk:";
      case MT_SHADOWS:          return "killsc:";
      case MT_SKULL:            return "killsl:";
      case MT_HEAD:             return "killcd:";
      case MT_PAIN:             return "killpe:";
      case MT_KNIGHT:           return "killhk:";
      case MT_BRUISER:          return "killbn:";
      case MT_UNDEAD:           return "killrv:";
      case MT_FATSO:            return "killmc:";
      case MT_BABY:             return "killar:";
      case MT_SPIDER:           return "killmm:";
      case MT_CYBORG:           return "killcy:";
      case MT_VILE:             return "killav:";
      case MT_WOLFSS:           return "killns:";
      case MT_KEEN:             return "killkn:";
      default:           *n = 4; return "kill:";
    }
}

// A monster just died.  From P_DamageMobj.  Buddy kill -> a (rare) per-monster quip
// + spree milestone; human kill near the buddy -> "nice".  Monsters only.
//
// Anti-spam: each kill only ~1-in-4 even attempts a quip, and AICoop_Callout's global
// 4s cooldown means at most one line plays at a time -- so it's an occasional treat,
// not a line every kill.  P_Random keeps it demo-deterministic.
void P_AICoop_NoteKill (mobj_t* victim, mobj_t* killer)
{
    mobj_t* buddy;
    if (!companion_active || !victim || !(victim->flags & MF_COUNTKILL)) return;
    buddy = AICoop_Mo ();
    if (!buddy) return;
    if (killer == buddy)
    {
	static int cnt, t;
	if (victim->info && victim->health < -victim->info->spawnhealth && P_Random () < 200)
	    AICoop_CalloutP ("gib:", 3, VP_KILL);	// satisfying overkill
	else if (P_Random () < 180)			// frequent per-type quip
	{
	    int n; const char* tag = AICoop_KillTag (victim->type, &n);
	    AICoop_CalloutP (tag, n, VP_KILL);
	}
	if (gametic - t < 5*TICRATE) { if (++cnt >= 4) { AICoop_CalloutP ("spree:", 4, VP_KILL); cnt = 0; } }
	else cnt = 1;
	t = gametic;
    }
    else if (killer && killer->player == &players[0]
	     && P_AproxDistance (victim->x - buddy->x, victim->y - buddy->y) < COOP_SIGHT)
	AICoop_CalloutP ("nice:", 2, VP_KILL);		// the human scored, buddy approves
}


// ---- "hopeless target" blacklist ------------------------------------------
// If the buddy fires at a monster and its health just won't drop (shots blocked /
// can't reach / can't elevate), it blacklists that monster for a few seconds so
// target acquisition skips it and picks another foe (or falls back to following)
// instead of freezing on one it can't hurt.  Keyed by mobj_t* like the rest of the
// buddy's side state; entries simply expire, so a freed/reused pointer self-heals.
#define COOP_BL_MAX	8
#define COOP_BL_TICS	(5*TICRATE)	// how long a hopeless target stays ignored
static struct { mobj_t* mon; int until; } coop_bl[COOP_BL_MAX];

static void AICoop_Blacklist (mobj_t* m)
{
    int	i, slot = 0, oldest = 0x7fffffff;
    for (i = 0; i < COOP_BL_MAX; i++)
    {
	if (coop_bl[i].mon == m)       { slot = i; break; }		// refresh existing
	if (coop_bl[i].until < oldest) { oldest = coop_bl[i].until; slot = i; }
    }
    coop_bl[slot].mon   = m;
    coop_bl[slot].until = gametic + COOP_BL_TICS;
}

static boolean AICoop_IsBlacklisted (mobj_t* m)
{
    int	i;
    for (i = 0; i < COOP_BL_MAX; i++)
	if (coop_bl[i].mon == m && gametic < coop_bl[i].until)
	    return true;
    return false;
}

//
// AICoop_FindTarget
// Nearest live, shootable, visible monster within range (never the human).
//
static mobj_t* AICoop_FindTarget (mobj_t* self)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	m;
	fixed_t	d;

	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	m = (mobj_t*)th;

	if (m == self)			continue;
	if (!Companion_IsEnemy (m))	continue;	// shared filter: live shootable non-friendly monster
	if (AICoop_IsBlacklisted (m))	continue;	// shots weren't connecting -> skip it

	d = P_AproxDistance (m->x - self->x, m->y - self->y);
	if (d > COOP_SIGHT)		continue;
	if (!P_CheckSight (self, m))	continue;

	if (!best || d < bestd) { best = m; bestd = d; }
    }
    return best;
}


// Nearest live shootable monster to a point (used by the "attack" order).
static mobj_t* AICoop_NearestMonsterTo (fixed_t x, fixed_t y)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	m;
	fixed_t	d;

	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	m = (mobj_t*)th;

	if (!Companion_IsEnemy (m))	continue;	// shared filter: live shootable non-friendly monster

	d = P_AproxDistance (m->x - x, m->y - y);
	if (!best || d < bestd) { best = m; bestd = d; }
    }
    return best;
}


// Is the floor at (x,y) a damaging sector (nukage / lava / blood / death exit)?
static boolean AICoop_DamagingFloor (fixed_t x, fixed_t y)
{
    sector_t* s = R_PointInSubsector (x, y)->sector;
    switch (s->special)
    {
      case 4:		// lightning + 20% damage
      case 5:		// 10% damage
      case 7:		// 5% damage (nukage)
      case 11:		// 20% damage + end level on death
      case 16:		// 20% damage
	return true;
    }
    return false;
}


//
// AICoop_CanReach
// Can the companion walk in a straight line from its feet to a pickup?  We
// march the segment in ~24-unit steps and at each point require that the marine
// fits (P_CheckPosition: no wall/obstacle, head-room) and that the floor never
// rises more than a 24-unit step.  Rejects items behind a wall or up a ledge so
// the bot doesn't run face-first into geometry trying to fetch them.
//
// Why a reach probe failed -- see AICoop_ReachWhy[] (diagnostics only; `navdbg`).
enum { REACH_OK = 0, REACH_FAR, REACH_WALL, REACH_FIT, REACH_HEAD,
       REACH_STEPUP, REACH_DROP, REACH_LEDGE, REACH_HAZARD };

static const char* AICoop_ReachWhy[] = {
    "ok", "too far", "wall/thing", "doesn't fit", "no head room",
    "step up >24", "drop >24", "off a ledge", "damaging floor"
};

// The full probe: same as AICoop_CanReach but also reports WHY it failed and the
// sample point it failed at (NULL out-params to ignore).
static boolean AICoop_ReachProbe (mobj_t* self, fixed_t tx, fixed_t ty, boolean avoiddmg,
				  int* why, fixed_t* fx, fixed_t* fy)
{
    extern int	pf_ignore_actors;
    fixed_t	dx = tx - self->x;
    fixed_t	dy = ty - self->y;
    fixed_t	dist = P_AproxDistance (dx, dy);
    fixed_t	fz = self->z;			// walking surface, from the buddy's feet
    fixed_t	dz = self->dropoffz;		// lowest floor its box currently overhangs
    int		steps, i;
    boolean	res = true;

    if (why) *why = REACH_OK;
    if (fx)  *fx  = tx;
    if (fy)  *fy  = ty;

    // Already standing in the hazard?  Then refusing every square that hurts is
    // exactly backwards -- it pins the buddy inside the nukage until something drags
    // it out.  Escaping takes priority over not getting wet.
    if (avoiddmg && AICoop_DamagingFloor (self->x, self->y))
	avoiddmg = false;

    if (dist < 16*FRACUNIT)
	return true;				// practically there
    // step by the buddy radius (16) so consecutive P_CheckPosition boxes (32 wide)
    // overlap -> a wall BETWEEN samples can't slip through (a 24-unit step left a
    // gap, so a waypoint just behind a thin wall looked reachable and the buddy
    // wedged against it).
    steps = dist / (16*FRACUNIT);
    if (steps > 96)
    {
	if (why) *why = REACH_FAR;
	return false;				// too far -- don't bother (bounds cost)
    }

    pf_ignore_actors = 1;

    for (i = 1; i <= steps; i++)
    {
	fixed_t	frac = (i << 16) / steps;	// i/steps as 16.16
	fixed_t	px   = self->x + FixedMul (dx, frac);
	fixed_t	py   = self->y + FixedMul (dy, frac);
	int	w    = REACH_OK;

	// Replicate P_TryMove's feasibility so "reachable" means the buddy can
	// actually WALK there (point-sampling P_CheckPosition alone said yes to spots
	// behind a step/ledge the move physics reject, so the buddy wedged there).
	if (!P_CheckPosition (self, px, py))			w = REACH_WALL;
	else if (tmceilingz - tmfloorz < self->height)		w = REACH_FIT;
	else if (tmceilingz - fz < self->height)		w = REACH_HEAD;
	else if (tmfloorz - fz > 24*FRACUNIT)			w = REACH_STEPUP;
	// Drop-off, MBF "monkeys" form (both clauses, exactly as p_map.c applies them
	// to smart monsters).  The ABSOLUTE rule vanilla uses -- tmfloorz-tmdropoffz>24
	// -- rejects every sample whose 32x32 box merely straddles a step edge or the
	// open side of a staircase, which made whole stairways read as "unreachable":
	// the buddy refused to climb them, detoured, or wedged at the bottom.  (It does
	// not even apply to the buddy: it is a PLAYER mobj, and MT_PLAYER has MF_DROPOFF,
	// so P_TryMove never runs that test on it.)  The relative pair keeps stairs
	// walkable while still refusing a cliff:
	//   1. the walking surface must not fall more than 24 below where we came from
	//   2. the overhang must not get more than 24 WORSE than it already is.
	// (2) matters because tmfloorz is the HIGHEST floor in the box: right at a cliff
	// lip the box still covers the high floor, so (1) alone sees nothing until the
	// box has cleared the edge entirely -- long enough for the buddy to run off it.
	else if (fz - tmfloorz > 24*FRACUNIT)			w = REACH_DROP;
	else if (dz - tmdropoffz > 24*FRACUNIT)			w = REACH_LEDGE;
	else if (avoiddmg && AICoop_DamagingFloor (px, py))	w = REACH_HAZARD;

	if (w != REACH_OK)
	{
	    res = false;
	    if (why) *why = w;
	    if (fx)  *fx  = px;
	    if (fy)  *fy  = py;
	    break;
	}
	fz = tmfloorz;
	dz = tmdropoffz;
    }

    pf_ignore_actors = 0;
    return res;
}

boolean AICoop_CanReach (mobj_t* self, fixed_t tx, fixed_t ty, boolean avoiddmg)
{
    return AICoop_ReachProbe (self, tx, ty, avoiddmg, NULL, NULL, NULL);
}


//
// AICoop_FindHealth
// Nearest health pickup still lying in the world (stimpack/medikit/soul/mega).
//
static mobj_t* AICoop_FindHealth (mobj_t* self)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	m;
	fixed_t	d;

	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	m = (mobj_t*)th;

	if (!(m->flags & MF_SPECIAL))	continue;	// not a pickup (or already taken)
	switch (m->sprite)
	{
	  case SPR_STIM: case SPR_MEDI:
	  case SPR_SOUL: case SPR_MEGA:
	    break;
	  default:
	    continue;
	}

	d = P_AproxDistance (m->x - self->x, m->y - self->y);
	if (d > COOP_HEAL_RANGE)	continue;
	if (best && d >= bestd)		continue;	// not closer -> skip the trace
	if (!AICoop_CanReach (self, m->x, m->y, true)) continue;	// can't walk there

	best = m; bestd = d;
    }
    return best;
}


//
// AICoop_FindItem
// Nearest worth-grabbing pickup: health, bonuses, armor, ammo, weapons,
// backpack.  Deliberately skips keys (the human may need them in co-op).
//
static mobj_t* AICoop_FindItem (mobj_t* self)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = 0;
    mobj_t*	pl = AICoop_NearestHuman (self->x, self->y);	// don't steal the human's items

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	m;
	fixed_t	d;

	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	m = (mobj_t*)th;

	if (!(m->flags & MF_SPECIAL))	continue;	// a pickup still in the world
	switch (m->sprite)
	{
	  case SPR_STIM: case SPR_MEDI: case SPR_SOUL: case SPR_MEGA:	// health
	  case SPR_BON1: case SPR_BON2:					// bonuses
	  case SPR_ARM1: case SPR_ARM2:					// armor
	  case SPR_CLIP: case SPR_AMMO: case SPR_SHEL: case SPR_SBOX:	// ammo
	  case SPR_ROCK: case SPR_BROK: case SPR_CELL: case SPR_CELP:
	  case SPR_BPAK:						// backpack
	  case SPR_SHOT: case SPR_SGN2: case SPR_MGUN: case SPR_LAUN:	// weapons
	  case SPR_PLAS: case SPR_BFUG: case SPR_CSAW:
	    break;
	  default:
	    continue;						// keys & everything else
	}

	d = P_AproxDistance (m->x - self->x, m->y - self->y);
	if (d > COOP_ITEM_RANGE)	continue;
	// Leave items the human is closer to -- otherwise the buddy snatches the
	// pickup right as the player walks up to it (looks like it "vanishes").
	// EXCEPTION: an item the player deliberately DROPPED (MF_DROPPED, e.g. a
	// medikit handed to the buddy from the inventory) is fair game even right
	// next to the human -- that's the whole point of dropping it.
	if (!(m->flags & MF_DROPPED)
	    && pl && P_AproxDistance (m->x - pl->x, m->y - pl->y) < d) continue;
	if (best && d >= bestd)		continue;	// not closer -> skip the trace
	if (!AICoop_CanReach (self, m->x, m->y, true)) continue;	// can't walk there

	best = m; bestd = d;
    }
    return best;
}


// ----------------------------------------------------------------- commands
// Replies are short strings the console prints; they start with "[Buddy] ".

const char* P_AICoop_Report (void)
{
    static char		buf[120];
    static const char*	what[]    = { "following you", "fighting", "getting health",
				      "holding position", "coming to you", "grabbing an item" };
    static const char*	compass[8] = { "east","north-east","north","north-west",
				       "west","south-west","south","south-east" };
    mobj_t*	mo = AICoop_Mo ();
    mobj_t*	pl;

    if (!mo)
	return "[Buddy] (no companion -- launch with -coop)";
    pl = AICoop_NearestHuman (mo->x, mo->y);

    if (pl)
    {
	int	units = (int)(P_AproxDistance (mo->x - pl->x, mo->y - pl->y) >> FRACBITS);
	angle_t	a     = R_PointToAngle2 (pl->x, pl->y, mo->x, mo->y);
	int	oct   = (int)((a + (1u<<28)) >> 29) & 7;
	snprintf (buf, sizeof(buf), "[Buddy] %d units to your %s, %d HP -- %s.",
		  units, compass[oct], players[coop_slot].health, what[coop_state]);
    }
    else
	snprintf (buf, sizeof(buf), "[Buddy] %d HP -- %s.", players[coop_slot].health, what[coop_state]);

    return buf;
}

// Order commands are single-machine only: in a netgame they would set state on
// just one node and desync the lockstep, so they are refused there.
int P_AICoop_Summon (void)
{
    if (!AICoop_Mo ())		return 0;
    if (netgame)		return 0;
    summon = COOP_SUMMON_TICS;
    summon_stay = 1;		// ...and then stay tethered near you (LOS) until another order
    hold   = 0;
    user_hold = 0;		// "come" releases a manual stay
    return 1;
}

const char* P_AICoop_Wait (void)
{
    if (!AICoop_Mo ())
	return "[Buddy] (no companion -- launch with -coop)";
    if (netgame)
	return "[Buddy] (orders unavailable in netplay)";
    // Sticky stay: holds position until you order otherwise (come/attack), and the
    // LLM director can't override it (see P_AICoop_SetTactic).  Toggle to release.
    user_hold = !user_hold;
    summon_stay = 0;		// "wait" is another order -> drop the come leash
    if (user_hold) { summon = 0; forceaggro = 0; forcetarget = NULL; ai_goto = 0; }
    return user_hold ? "[Buddy] Holding position -- staying put until you say otherwise."
		     : "[Buddy] Moving out.";
}

// (F) One-button buddy mode cycle (default: right mouse button).  attacking OR following
// -> STAY (hold position); STAY -> FOLLOW (run to + tail the human).  A single flick the
// player can hit mid-fight without opening the console.
const char* P_AICoop_ToggleMode (void)
{
    if (!AICoop_Mo ())
	return "[Buddy] (no companion -- launch with -coop)";
    if (netgame)
	return "[Buddy] (orders unavailable in netplay)";
    if (user_hold)
    {
	// currently holding -> resume following the human
	P_AICoop_Summon ();			// summon + tether (clears user_hold)
	P_AICoop_VoiceTag ("summon_ok");
	return "[Buddy] Following you.";
    }
    // attacking or following -> hold position
    user_hold = 1;
    summon = summon_stay = forceaggro = 0;
    forcetarget = NULL;
    ai_goto = 0;
    P_AICoop_VoiceTag ("wait_hold");
    return "[Buddy] Holding position.";
}

const char* P_AICoop_Attack (void)
{
    mobj_t*	mo = AICoop_Mo ();
    mobj_t*	pl;
    mobj_t*	t;

    if (!mo)
	return "[Buddy] (no companion -- launch with -coop)";
    if (netgame)
	return "[Buddy] (orders unavailable in netplay)";
    pl = AICoop_NearestHuman (mo->x, mo->y);
    t = AICoop_NearestMonsterTo (pl ? pl->x : mo->x, pl ? pl->y : mo->y);
    if (!t)
	return "[Buddy] No targets around.";
    forcetarget = t;
    forceaggro  = COOP_ATTACK_TICS;
    hold = 0;
    user_hold = 0;		// "attack" releases a manual stay
    summon_stay = 0;		// ...and the come leash
    return "[Buddy] Attacking!";
}

// ---------------------------------------------------------------------------
//  AI (LLM) director layer
// ---------------------------------------------------------------------------
int P_AICoop_AIMode (void)
{
    return aicoop_layer;
}

// Map a director tactic onto the buddy's existing rule-based overrides (which
// already age + are executed in P_AICoop_BuildCmd).  The director re-sends the
// order every cycle to keep it fresh; when it stops, the timers lapse and the
// buddy reverts to autonomous behaviour.
void P_AICoop_SetDirective (int tactic, struct mobj_s* focus, fixed_t x, fixed_t y, int tics)
{
    static int	ai_last_tactic = -1;
    mobj_t*	mo = AICoop_Mo ();
    if (!mo || netgame)
	return;
    if (user_hold)		// human ordered "stay" -- ignore the director until released
	return;
    if (tics <= 0)
	tics = 70;				// ~2 s

    // Voice: announce a CHANGED non-combat order once.  Combat orders (engage/
    // defend) stay silent here -- the automatic contact/hurt/clear callouts in
    // BuildCmd already cover the fighting, so we'd only double up.
    if (tactic != ai_last_tactic)
    {
	const char* vtag = NULL;
	switch (tactic)
	{
	  case BUD_HOLD:    vtag = "wait_hold";      break;	// "Holding position"
	  case BUD_REGROUP:
	  case BUD_RETREAT: vtag = "summon_ok";      break;	// "On my way!"
	  case BUD_GOTO:    vtag = "wait_move";       break;	// "Moving out"
	  case BUD_GRAB:    vtag = "state:grabbing";  break;
	  default:          break;				// engage/defend/auto -> silent
	}
	if (vtag) AICoop_SayTagP (vtag, VP_COMMAND);	// orders preempt lower chatter
	ai_last_tactic = tactic;
    }

    // clear all overrides first; the chosen tactic re-arms the ones it needs
    forceaggro = 0; forcetarget = NULL; hold = 0; summon = 0; ai_goto = 0; summon_stay = 0;

    switch (tactic)
    {
      case BUD_ENGAGE:
	forcetarget = (mobj_t*) focus;
	if (!forcetarget) forcetarget = AICoop_FindTarget (mo);
	forceaggro  = tics;
	break;
      case BUD_HOLD:
	hold = 1;
	break;
      case BUD_REGROUP:
      case BUD_RETREAT:
	summon = tics;
	break;
      case BUD_GOTO:
	ai_goto = tics; ai_gx = x; ai_gy = y;
	break;
      case BUD_DEFEND:
      case BUD_GRAB:
      case BUD_AUTO:
      default:
	break;					// overrides cleared -> rule-based
    }
}

const char* P_AICoop_StatusReport (void)
{
    static char		buf[120];
    static const char*	wn[NUMWEAPONS] = { "fists","pistol","shotgun","chaingun",
				"rocket launcher","plasma rifle","BFG9000","chainsaw","super shotgun" };
    player_t*	bot = &players[coop_slot];
    int		w, am;

    if (!AICoop_Mo ())
	return "[Buddy] (no companion -- launch with -coop)";
    w  = bot->readyweapon;
    am = (weaponinfo[w].ammo < NUMAMMO) ? bot->ammo[weaponinfo[w].ammo] : -1;
    if (am >= 0)
	snprintf (buf, sizeof(buf), "[Buddy] %d HP, %d%% armor, %s, %d rounds.",
		  bot->health, bot->armorpoints, wn[w], am);
    else
	snprintf (buf, sizeof(buf), "[Buddy] %d HP, %d%% armor, %s.",
		  bot->health, bot->armorpoints, wn[w]);
    return buf;
}

// Console "buddygod": toggle god mode on the buddy (mirrors the IDDQD flag for its
// player slot, so P_DamageMobj ignores damage to it).
const char* P_AICoop_God (void)
{
    player_t* bot = &players[coop_slot];
    if (!AICoop_Mo ()) return "[Buddy] (no companion -- launch with -coop)";
    bot->cheats ^= CF_GODMODE;
    if (bot->cheats & CF_GODMODE)
    {
	bot->health = 100;
	if (bot->mo) bot->mo->health = 100;
	AICoop_Callout ("god:", 2);
	return "[Buddy] God mode ON.";
    }
    return "[Buddy] God mode OFF.";
}

// Console "buddyarm": all weapons + full ammo + armor for the buddy (like IDFA).
// Deliberately NOT keys -- the human needs those for locked doors (see P_AICoop_IsBuddy).
const char* P_AICoop_GiveAll (void)
{
    player_t* bot = &players[coop_slot];
    int i;
    if (!AICoop_Mo ()) return "[Buddy] (no companion -- launch with -coop)";
    for (i = 0; i < NUMWEAPONS; i++) bot->weaponowned[i] = true;
    for (i = 0; i < NUMAMMO; i++)    bot->ammo[i] = bot->maxammo[i];
    bot->armorpoints = 200; bot->armortype = 2;
    AICoop_Callout ("arm:", 2);
    return "[Buddy] Locked and loaded.";
}

// Console "buddyhome"/"buddytp": teleport the buddy back to where it spawned on
// this map -- a rescue for when it gets stuck on geometry or lost far behind you.
const char* P_AICoop_Home (void)
{
    mobj_t* mo = AICoop_Mo ();
    if (!mo)            return "[Buddy] (no companion -- launch with -aicoop)";
    if (!coop_home_set) return "[Buddy] no spawn point recorded for this level.";
    // Telefrag-move to the spawn point, restore facing, kill momentum and clear
    // any forced/sticky state so the bot re-evaluates cleanly next tic.
    P_TeleportMove (mo, coop_home_x, coop_home_y);
    mo->angle = coop_home_angle;
    mo->momx = mo->momy = mo->momz = 0;
    forcetarget = NULL; forceaggro = 0; user_hold = 0;
    // Verify the recall actually put the buddy back inside the map.  If the
    // recorded home point is itself off the blockmap (a corrupt/void spawn), the
    // teleport merely moved it to another void spot -- report that instead of
    // falsely claiming success.
    if (!AICoop_OnGrid (mo->x, mo->y))
	return "[Buddy] home point is off the map -- recall failed.";
    AICoop_Callout ("home:", 3);		// (C) "Regrouping on you!" / "Beam me back, baby!"
    return "[Buddy] Teleporting back to start.";
}


// ================ BSP sub-sector Dijkstra pathfinder (Pathfinding.md) =======
// Nodes  = walkable sub-sectors, represented by their centroid.
// Edges  = two-sided segs to the neighbouring sub-sector.
// Weight = centroid distance + penalties (closed door, damaging floor).
// A straight-line "string pull" then picks the furthest reachable waypoint.

#define PF_DOOR_PEN	200		// extra cost (units) to route through a door
#define PF_HAZARD_PEN	1000		// extra cost to route over a damaging floor
#define PF_MAXPOP	8000		// cap on Dijkstra node expansions
#define PF_PATHMAX	512		// max sub-sectors in a reconstructed path
#define PF_INF		0x7fffffff
#define PF_MAXADJ	32		// max graph edges per sub-sector
#define PF_DANGER_W	8		// Safe mode: cost added per danger-point on entering a node
#define PF_DANGER_MAX	2000		// clamp per-sub-sector danger so one hot spot can't dominate
#define PF_JUMP_PEN	150		// extra cost to route over a jump link (prefer walking)
#define PF_JUMP_MAX	(48*FRACUNIT)	// tallest step the buddy can clear with BT_JUMP
#define PF_EDGE_JUMP	1		// pf_adjf: crossing this edge needs a jump
#define PF_EDGE_TELEPORT 2		// pf_adjf: crossing this edge is a teleporter
#define PF_EDGE_DOOR	4		// pf_adjf: crossing this edge passes through a door

static int	pf_level = -1;		// episode*100+map the graph was built for
static int	pf_lastbuild;		// gametic of the last graph (re)build
static int	pf_n;			// sub-sector count
static fixed_t*	pf_cx;			// centroid x / y per sub-sector
static fixed_t*	pf_cy;
static int*	pf_nadj;		// edge count per sub-sector
static int*	pf_adj;			// flat [pf_n*PF_MAXADJ] neighbour sub-sectors
static int*	pf_adjw;		// flat [pf_n*PF_MAXADJ] edge weights
static fixed_t*	pf_adjpx;		// flat [pf_n*PF_MAXADJ] PORTAL x: a walkable point
static fixed_t*	pf_adjpy;		//   just inside neighbour v on the u|v boundary
// PORTAL SPAN: the two ends of the u|v boundary the buddy may cross, already inset by
// its radius, oriented LEFT/RIGHT as seen walking u -> v.  Baked at build time (where
// the side each edge was probed from is known) so the funnel never has to re-derive an
// orientation from seg->frontsector -- that test is ambiguous whenever both sides of a
// seg belong to the same sector, and it silently mirrored the funnel when it was.
// A boundary too narrow to inset collapses to (px,py) on both ends: a degenerate
// portal, which the funnel handles as a plain waypoint.
static fixed_t*	pf_adjlx;
static fixed_t*	pf_adjly;
static fixed_t*	pf_adjrx;
static fixed_t*	pf_adjry;
static byte*	pf_adjf;		// flat [pf_n*PF_MAXADJ] edge flags (PF_EDGE_*)
static seg_t**	pf_adjsg;		// flat [pf_n*PF_MAXADJ] seg_t* per edge (NULL if grid link)
static line_t**	pf_adjline;		// flat [pf_n*PF_MAXADJ] line_t* per edge
static byte*	pf_hazard;		// per-sub-sector "centroid stands on a damaging floor"

// Incoming adjacency for Dijkstra Map backward relaxation
static int*	pf_ninadj;		// incoming edge count per sub-sector
static int*	pf_inadj;		// flat [pf_n*PF_MAXADJ] origin sub-sector u for edge u->v
static seg_t**	pf_inadjsg;		// flat [pf_n*PF_MAXADJ] seg_t* for edge u->v
static line_t**	pf_inadjline;		// flat [pf_n*PF_MAXADJ] line_t* for edge u->v
static byte*	pf_inadjf;		// flat [pf_n*PF_MAXADJ] edge flags for u->v
static int*	pf_inadjw;		// flat [pf_n*PF_MAXADJ] base distance for u->v

// Dijkstra Map Flow-Field cached solution for Player's subsector
static int*	pf_flow_dist;		// Dijkstra Map dist to player
static int*	pf_flow_next;		// Dijkstra Map next hop towards player
static int	pf_flow_player_ss = -1;
static int	pf_flow_tic = -1;
static int	pf_flow_level = -1;
static int	pf_flow_safe = -1;	// pf_safemode the cached field was computed with

static int*	pf_dist;		// A* cost-so-far (g)
static int*	pf_prev;		// A* predecessor
static byte*	pf_done;		// A* closed flag
static int*	pf_heap;		// binary min-heap of open nodes (by f)
static int*	pf_hpos;		// node -> its index in pf_heap, -1 = not open
static int	pf_heapn;		// heap size
static int*	pf_danger;		// per-sub-sector "recently took damage here" heatmap
static int	pf_safemode;		// when set, PF_AStar weights edges by pf_danger (Safe route)
static int	pf_noheur;		// when set, PF_H returns 0 (see PF_DijkstraMap)
static byte*	pf_edge_flags;		// PF_EdgeWeight out-param: PF_EDGE_* bits for this edge
static int	pf_path[PF_PATHMAX];

// Diagnostic counters for pathfinding silent caps.  Every one of these is a place the
// route search quietly gives up, which then LOOKS like "the player is unreachable" --
// so they are reported by the `navdbg` console command instead of staying invisible.
static int	pf_cap_maxpop_cnt = 0;
static int	pf_cap_maxadj_cnt = 0;
static int	pf_cap_inadj_cnt = 0;
static int	pf_cap_pathmax_cnt = 0;
static int	pf_cap_lockedlines_cnt = 0;

static int PF_SS (fixed_t x, fixed_t y)
{
    return (int)(R_PointInSubsector (x, y) - subsectors);
}

static boolean PF_IsDoorSpecial (int sp)	// push (DR) doors with no key
{
    return (sp == 1 || sp == 31 || sp == 117 || sp == 118);
}

// Is an openable (unlocked DR) door right in front of the buddy, within USE reach?
// Gates the stuck-handler's USE tap so it only opens REAL doors instead of grinding
// USE on whatever plain wall/ledge it happens to be wedged against.
static boolean AICoop_DoorInFront (mobj_t* mo)
{
    int		i, fa = mo->angle >> ANGLETOFINESHIFT;
    fixed_t	fwx = finecosine[fa], fwy = finesine[fa];
    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	fixed_t	mx, my, dx, dy;
	if (!PF_IsDoorSpecial (ld->special)) continue;			// not an openable door
	mx = (ld->v1->x + ld->v2->x) >> 1;
	my = (ld->v1->y + ld->v2->y) >> 1;
	dx = mx - mo->x; dy = my - mo->y;
	if (P_AproxDistance (dx, dy) > 96*FRACUNIT) continue;		// not adjacent
	if (FixedMul (dx, fwx) + FixedMul (dy, fwy) > 0) return true;	// in the forward arc
    }
    return false;
}

// If a CLOSED push-door (unlocked DR) lies ahead toward the goal, hand back the
// midpoint of its line in (*ox,*oy).  The BSP waypoints are sub-sector centroids,
// which can sit behind the corridor wall beside a doorway -- so the buddy never
// heads INTO the doorway and just grinds the wall next to it.  Steering at the
// door's own midpoint walks it down the corridor into the opening, where the Use
// tap in the stuck handler opens the (unlocked) door.
boolean AICoop_FindDoorAhead (mobj_t* mo, fixed_t gx, fixed_t gy,
				     fixed_t* ox, fixed_t* oy)
{
    int		i;
    fixed_t	bestd = 256*FRACUNIT;		// only doors we're already near
    boolean	found = false;
    angle_t	togoal = R_PointToAngle2 (mo->x, mo->y, gx, gy);

    for (i = 0 ; i < numlines ; i++)
    {
	line_t*		ld = &lines[i];
	sector_t*	fs, *bs;
	fixed_t		mx, my, d, opening;
	angle_t		todoor;

	if (!ld->backsector || !ld->frontsector)	continue;	// not two-sided
	if (!PF_IsDoorSpecial (ld->special))		continue;	// not an openable door
	fs = ld->frontsector; bs = ld->backsector;
	opening = (fs->ceilingheight < bs->ceilingheight ? fs->ceilingheight : bs->ceilingheight)
		- (fs->floorheight   > bs->floorheight   ? fs->floorheight   : bs->floorheight);
	if (opening >= 56*FRACUNIT)			continue;	// already passable
	mx = (ld->v1->x + ld->v2->x) >> 1;
	my = (ld->v1->y + ld->v2->y) >> 1;
	d  = P_AproxDistance (mx - mo->x, my - mo->y);
	if (d >= bestd)					continue;
	todoor = R_PointToAngle2 (mo->x, mo->y, mx, my);
	if ((angle_t)(todoor - togoal + ANG90) > ANG180) continue;	// not ahead (>90 off)
	bestd = d; *ox = mx; *oy = my; found = true;
    }
    return found;
}

// --- Monster-style chase (Doom P_NewChaseDir) -------------------------------
// Steering straight at a waypoint can't round a tight corner: the buddy grinds the
// wall next to the opening.  Doom monsters solve this by trial-walking the 8 compass
// directions (diagonal toward the target first, then the two axes, then a scan),
// keeping a direction for a while so they escape concave nooks.  We do the same, but
// the trial is a no-move reachability probe (AICoop_CanReach mirrors P_TryMove) and
// the chosen heading drives the ticcmd instead of moving the mobj directly.

// Can the buddy walk ~24u along compass heading `d8` (0=E,1=NE,2=N,..,7=SE)?
// Set while re-running a scan that found nothing walkable, to see whether the only
// thing in the way was the hazard rule.  Standing still in a fight is worse than a
// few points of nukage damage.
static boolean	coop_hazard_desperate;

static boolean AICoop_ChaseTry (mobj_t* mo, int d8)
{
    angle_t	a    = ((angle_t)d8 * ANG45) >> ANGLETOFINESHIFT;
    fixed_t	step = mo->radius + 24*FRACUNIT;
    boolean avoiddmg = (mo->player && (mo->player != &players[consoleplayer]))
		       && !coop_hazard_desperate;
    return AICoop_CanReach (mo, mo->x + FixedMul (step, finecosine[a]),
				mo->y + FixedMul (step, finesine[a]), avoiddmg);
}

// Pick (and commit to) a compass heading toward (gx,gy), Doom-monster style.
// Returns the heading as a BAM angle.  Keeps the last heading while it stays
// walkable so the buddy commits to a leg instead of dithering at a corner.
// The committed-heading state lives in the CALLER (a chasedir_t), so the buddy and the
// -aiplayer marine each keep their own and don't clobber each other's heading (which made
// the marine zig-zag/backtrack).  Pass NULL to use a shared internal default.
angle_t AICoop_ChaseDir (mobj_t* mo, fixed_t gx, fixed_t gy, chasedir_t* st)
{
    static chasedir_t	shared = { -1, 0, 0 };
    fixed_t		dx = gx - mo->x, dy = gy - mo->y;
    int			wx = (dx > 10*FRACUNIT) ? 0 : (dx < -10*FRACUNIT) ? 4 : -1;	// E / W
    int			wy = (dy > 10*FRACUNIT) ? 2 : (dy < -10*FRACUNIT) ? 6 : -1;	// N / S
    int			turn, cand[4], nc = 0, i, s;

    if (!st) st = &shared;
    turn = (st->dir >= 0) ? ((st->dir + 4) & 7) : -1;			// reverse of current

    // keep the committed heading while it's still walkable
    if (st->dir >= 0 && st->count > 0 && AICoop_ChaseTry (mo, st->dir))
	{ st->count--; return (angle_t)st->dir * ANG45; }

    if (wx >= 0 && wy >= 0)					// diagonal toward goal
	cand[nc++] = (wy == 2) ? (wx == 0 ? 1 : 3) : (wx == 0 ? 7 : 5);
    if (abs (dx) >= abs (dy)) { if (wx >= 0) cand[nc++] = wx; if (wy >= 0) cand[nc++] = wy; }
    else                      { if (wy >= 0) cand[nc++] = wy; if (wx >= 0) cand[nc++] = wx; }
    if (st->dir >= 0) cand[nc++] = st->dir;			// then continue current

    for (i = 0; i < nc; i++)
	if (cand[i] != turn && AICoop_ChaseTry (mo, cand[i]))
	    { st->dir = cand[i]; st->count = 8; return (angle_t)st->dir * ANG45; }

    st->flip ^= 1;						// scan all 8, alternating sweep side
    for (s = 0; s < 8; s++)
    {
	int d = st->flip ? s : (7 - s);
	if (d != turn && AICoop_ChaseTry (mo, d))
	    { st->dir = d; st->count = 8; return (angle_t)st->dir * ANG45; }
    }
    if (turn >= 0 && AICoop_ChaseTry (mo, turn))			// last resort: turn around
	{ st->dir = turn; st->count = 4; return (angle_t)st->dir * ANG45; }

    // Nothing walkable at all.  Before giving up, ask again ignoring the hazard rule:
    // a buddy ringed by nukage refuses all eight headings and simply stops, and the
    // only thing that ever un-sticks it is the 12-second recall.  Wading out costs a
    // few points; standing there costs the whole fight.
    coop_hazard_desperate = true;
    for (s = 0; s < 8; s++)
    {
	int d = st->flip ? s : (7 - s);
	if (AICoop_ChaseTry (mo, d))
	{
	    coop_hazard_desperate = false;
	    st->dir = d; st->count = 4;
	    return (angle_t)st->dir * ANG45;
	}
    }
    coop_hazard_desperate = false;

    st->dir = -1;						// boxed in -- head straight at goal
    return R_PointToAngle2 (mo->x, mo->y, gx, gy);
}

// Straight feet-trace between two world points (the "item reachability" trick used
// for graph building): every ~24 units require a player-sized box (ref's radius/
// height) to fit (P_CheckPosition) and the floor to step <=24.  `fz` seeds the
// starting floor height.  Independent of ref's own position (P_CheckPosition tests
// the passed x,y), so it's safe to call while building the graph.
static boolean PF_LineWalkable (fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by,
				mobj_t* ref, fixed_t fz)
{
    fixed_t	dx = bx - ax, dy = by - ay;
    fixed_t	dist = P_AproxDistance (dx, dy);
    int		steps, i;

    if (dist < 24*FRACUNIT) return true;
    steps = dist / (24*FRACUNIT);
    if (steps > 48) return false;
    for (i = 1; i <= steps; i++)
    {
	fixed_t	frac = (i << 16) / steps;
	fixed_t	px = ax + FixedMul (dx, frac);
	fixed_t	py = ay + FixedMul (dy, frac);
	if (!P_CheckPosition (ref, px, py))		return false;
	if (tmceilingz - tmfloorz < 56*FRACUNIT)	return false;
	if (tmfloorz - fz > 24*FRACUNIT)		return false;
	fz = tmfloorz;
    }
    return true;
}

static int PF_FindTeleportTarget (line_t* line)
{
    int i;
    thinker_t* thinker;
    mobj_t* m;
    if (!line || !line->tag) return -1;
    // One pass over the thinkers (not one per tagged sector): a map with many teleport
    // lines otherwise walks the whole thinker list numsectors times per line.
    for (thinker = thinkercap.next; thinker != &thinkercap; thinker = thinker->next)
    {
	if (thinker->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	m = (mobj_t*)thinker;
	if (m->type != MT_TELEPORTMAN || !m->subsector) continue;
	i = (int)(m->subsector->sector - sectors);
	if (i >= 0 && i < numsectors && sectors[i].tag == line->tag)
	    return (int)(m->subsector - subsectors);
    }
    return -1;
}

// Teleport line specials the BUDDY can actually use.
// Deliberately NOT here:
//   125 / 126 -- "teleport MONSTER only": p_spec.c gates them on `!thing->player`, and
//                the buddy is a player mobj, so a route through one is a dead end it
//                walks to and then stands in forever.
//   174 / 195 / 209 / 210 -- not implemented by this engine's p_spec.c at all, so the
//                line does nothing when crossed: same dead end, just less obvious.
// 39/97 are the vanilla W1/WR teleports, 207/208 the Boom silent ones (EV_SilentTeleport).
static boolean PF_IsTeleportSpecial (int sp)
{
    return (sp == 39 || sp == 97 || sp == 207 || sp == 208);
}

static void PF_AddEdgeF (int u, int v, int w, fixed_t px, fixed_t py,
			 fixed_t lx, fixed_t ly, fixed_t rx, fixed_t ry,
			 int flags, seg_t* sg, line_t* ld)
{
    int	k, j;
    if (u < 0 || v < 0 || u == v || w < 0) return;
    for (k = 0; k < pf_nadj[u]; k++)			// dedup, keep cheapest
	if (pf_adj[u*PF_MAXADJ + k] == v)
	{ if (w < pf_adjw[u*PF_MAXADJ + k])
	    { pf_adjw[u*PF_MAXADJ + k] = w; pf_adjpx[u*PF_MAXADJ + k] = px; pf_adjpy[u*PF_MAXADJ + k] = py;
	      pf_adjlx[u*PF_MAXADJ + k] = lx; pf_adjly[u*PF_MAXADJ + k] = ly;
	      pf_adjrx[u*PF_MAXADJ + k] = rx; pf_adjry[u*PF_MAXADJ + k] = ry;
	      pf_adjf[u*PF_MAXADJ + k] = (byte)flags;
	      pf_adjsg[u*PF_MAXADJ + k] = sg;
	      pf_adjline[u*PF_MAXADJ + k] = ld;
	      // Mirror the replacement into v's INCOMING list.  Leaving it stale let the
	      // flow field (which relaxes over pf_inadj*) cost and flag the very same edge
	      // differently from A* (which uses pf_adj*), so the two searches disagreed
	      // about doors and jumps on identical geometry.
	      for (j = 0; j < pf_ninadj[v]; j++)
		  if (pf_inadj[v*PF_MAXADJ + j] == u)
		  { pf_inadjw[v*PF_MAXADJ + j] = w;
		    pf_inadjf[v*PF_MAXADJ + j] = (byte)flags;
		    pf_inadjsg[v*PF_MAXADJ + j] = sg;
		    pf_inadjline[v*PF_MAXADJ + j] = ld;
		    break; } }
	  return; }
    if (pf_nadj[u] >= PF_MAXADJ)
    {
	pf_cap_maxadj_cnt++;
	return;
    }
    pf_adj [u*PF_MAXADJ + pf_nadj[u]] = v;
    pf_adjw[u*PF_MAXADJ + pf_nadj[u]] = w;
    pf_adjpx[u*PF_MAXADJ + pf_nadj[u]] = px;
    pf_adjpy[u*PF_MAXADJ + pf_nadj[u]] = py;
    pf_adjlx[u*PF_MAXADJ + pf_nadj[u]] = lx;
    pf_adjly[u*PF_MAXADJ + pf_nadj[u]] = ly;
    pf_adjrx[u*PF_MAXADJ + pf_nadj[u]] = rx;
    pf_adjry[u*PF_MAXADJ + pf_nadj[u]] = ry;
    pf_adjf[u*PF_MAXADJ + pf_nadj[u]] = (byte)flags;
    pf_adjsg[u*PF_MAXADJ + pf_nadj[u]] = sg;
    pf_adjline[u*PF_MAXADJ + pf_nadj[u]] = ld;
    pf_nadj[u]++;

    if (pf_ninadj && pf_ninadj[v] < PF_MAXADJ)
    {
	pf_inadj [v*PF_MAXADJ + pf_ninadj[v]] = u;
	pf_inadjw[v*PF_MAXADJ + pf_ninadj[v]] = w;
	pf_inadjf[v*PF_MAXADJ + pf_ninadj[v]] = (byte)flags;
	pf_inadjsg[v*PF_MAXADJ + pf_ninadj[v]] = sg;
	pf_inadjline[v*PF_MAXADJ + pf_ninadj[v]] = ld;
	pf_ninadj[v]++;
    }
    else if (pf_ninadj)
	pf_cap_inadj_cnt++;	// v loses an inbound edge -> the flow field can't route through it
}

// Portal point on the u->v edge (a walkable spot just inside v), or v's centroid.
static boolean PF_Portal (int u, int v, fixed_t* px, fixed_t* py)
{
    int	k;
    for (k = 0; k < pf_nadj[u]; k++)
	if (pf_adj[u*PF_MAXADJ + k] == v)
	{ *px = pf_adjpx[u*PF_MAXADJ + k]; *py = pf_adjpy[u*PF_MAXADJ + k]; return true; }
    return false;
}

static boolean PF_HasKey (int color)
{
    player_t* p = &players[consoleplayer];
    boolean res = false;
    switch (color)
    {
      case 1: res = p->cards[it_bluecard]   || p->cards[it_blueskull]; break;
      case 2: res = p->cards[it_yellowcard] || p->cards[it_yellowskull]; break;
      case 3: res = p->cards[it_redcard]    || p->cards[it_redskull]; break;
      default: res = true; break;
    }
    return res;
}

// 2D cross product of (b-a) x (c-a).  Positive = c lies to the LEFT of the ray a->b.
//
// INTEGER, not double, because everything here feeds the buddy's ticcmds and the playsim
// has to stay deterministic.  The deltas are shifted down 8 bits before multiplying: map
// coordinates reach +-2^31 in fixed_t, so a raw product of two deltas reaches 2^64 and
// overflows int64.  >>8 caps each delta at 2^24 (product 2^48, sum 2^49) while keeping
// 1/256 of a map unit of precision -- far finer than the 16-unit radius inset that the
// portals are built with, so the sign is never in question for non-degenerate input.
static int64_t PF_Cross (fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by, fixed_t cx, fixed_t cy)
{
    int64_t abx = ((int64_t)bx - (int64_t)ax) >> 8;
    int64_t aby = ((int64_t)by - (int64_t)ay) >> 8;
    int64_t acx = ((int64_t)cx - (int64_t)ax) >> 8;
    int64_t acy = ((int64_t)cy - (int64_t)ay) >> 8;
    return abx * acy - aby * acx;
}

static boolean PF_LineIntersection (fixed_t a1x, fixed_t a1y, fixed_t a2x, fixed_t a2y,
				    fixed_t b1x, fixed_t b1y, fixed_t b2x, fixed_t b2y)
{
    int64_t d1 = PF_Cross (a1x, a1y, a2x, a2y, b1x, b1y);
    int64_t d2 = PF_Cross (a1x, a1y, a2x, a2y, b2x, b2y);
    int64_t d3 = PF_Cross (b1x, b1y, b2x, b2y, a1x, a1y);
    int64_t d4 = PF_Cross (b1x, b1y, b2x, b2y, a2x, a2y);

    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
	((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
	return true;

    return false;
}

#define MAX_LOCKED_LINES 64
static line_t* pf_locked_lines[MAX_LOCKED_LINES];
static int     pf_num_locked_lines = 0;

static void PF_InitLockedLines (void)
{
    int i;
    pf_num_locked_lines = 0;
    for (i = 0; i < numlines; i++)
    {
	line_t* ld = &lines[i];
	int sp = ld->special;
	if (sp==26 || sp==32 || sp==99 || sp==133 ||
	    sp==27 || sp==34 || sp==136 || sp==137 ||
	    sp==28 || sp==33 || sp==134 || sp==135)
	{
	    if (pf_num_locked_lines < MAX_LOCKED_LINES)
		pf_locked_lines[pf_num_locked_lines++] = ld;
	    else
		pf_cap_lockedlines_cnt++;
	}
    }
}

static boolean PF_LockedLineCrossed (fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by)
{
    int i;
    for (i = 0; i < pf_num_locked_lines; i++)
    {
	line_t* ld = pf_locked_lines[i];
	int sp = ld->special;
	int color = 0;
	if      (sp==26 || sp==32 || sp==99 || sp==133) color = 1;	// blue
	else if (sp==27 || sp==34 || sp==136 || sp==137) color = 2;	// yellow
	else if (sp==28 || sp==33 || sp==134 || sp==135) color = 3;	// red
	if (color && !PF_HasKey (color))
	{
	    if (PF_LineIntersection (ax, ay, bx, by, ld->v1->x, ld->v1->y, ld->v2->x, ld->v2->y))
		return true;
	}
    }
    return false;
}

static int PF_EdgeWeight (seg_t* sg, line_t* ld_param, int u, int v);

// Inset the shared u|v boundary (a..b) by `inset` at both ends and hand back the result
// as a portal span.  A funnel corner gets placed exactly ON one of these points, so it
// has to be a spot the buddy's 32x32 box can stand in -- an un-inset corner sits in the
// wall the boundary ends at.  A boundary too narrow to inset collapses onto (cx,cy),
// i.e. a degenerate portal meaning "cross precisely here".
// P_AproxDistance overestimates by up to ~12%, which shortens the inset rather than
// lengthening it, so the nominal 20 is at worst ~18 -- still clear of the 16 radius.
#define PF_PORTAL_INSET	(20*FRACUNIT)

static void PF_InsetSpan (fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by,
			  fixed_t cx, fixed_t cy,
			  fixed_t* olx, fixed_t* oly, fixed_t* orx, fixed_t* ory)
{
    fixed_t	dx = bx - ax, dy = by - ay;
    fixed_t	len = P_AproxDistance (dx, dy);
    fixed_t	ix, iy;

    if (len <= 2*PF_PORTAL_INSET)
    { *olx = *orx = cx; *oly = *ory = cy; return; }

    ix = (fixed_t)(((int64_t)dx * PF_PORTAL_INSET) / len);
    iy = (fixed_t)(((int64_t)dy * PF_PORTAL_INSET) / len);
    *olx = ax + ix; *oly = ay + iy;
    *orx = bx - ix; *ory = by - iy;
}

static void PF_Build (mobj_t* ref)
{
    int		i, j, s;
    int*	segss;

    PF_InitLockedLines ();

    free (pf_cx); free (pf_cy); free (pf_nadj); free (pf_adj); free (pf_adjw);
    free (pf_adjpx); free (pf_adjpy); free (pf_adjf); free (pf_adjsg); free (pf_adjline);
    free (pf_adjlx); free (pf_adjly); free (pf_adjrx); free (pf_adjry); free (pf_hazard);
    free (pf_ninadj); free (pf_inadj); free (pf_inadjw); free (pf_inadjf); free (pf_inadjsg); free (pf_inadjline);
    free (pf_flow_dist); free (pf_flow_next);
    pf_flow_dist = NULL; pf_flow_next = NULL;
    pf_flow_player_ss = -1; pf_flow_tic = -1; pf_flow_level = -1; pf_flow_safe = -1;
    free (pf_dist); free (pf_prev); free (pf_done); free (pf_danger);
    free (pf_heap); free (pf_hpos);

    pf_n    = numsubsectors;
    pf_danger = calloc (pf_n, sizeof(int));	// fresh (zeroed) danger heatmap for this graph
    pf_cx   = malloc (pf_n * sizeof(fixed_t));
    pf_cy   = malloc (pf_n * sizeof(fixed_t));
    pf_dist = malloc (pf_n * sizeof(int));
    pf_prev = malloc (pf_n * sizeof(int));
    pf_done = malloc (pf_n);
    pf_hazard = calloc (pf_n, 1);
    pf_nadj = calloc (pf_n, sizeof(int));
    pf_adj  = malloc (pf_n * PF_MAXADJ * sizeof(int));
    pf_adjw = malloc (pf_n * PF_MAXADJ * sizeof(int));
    pf_adjpx= malloc (pf_n * PF_MAXADJ * sizeof(fixed_t));
    pf_adjpy= malloc (pf_n * PF_MAXADJ * sizeof(fixed_t));
    pf_adjlx= malloc (pf_n * PF_MAXADJ * sizeof(fixed_t));
    pf_adjly= malloc (pf_n * PF_MAXADJ * sizeof(fixed_t));
    pf_adjrx= malloc (pf_n * PF_MAXADJ * sizeof(fixed_t));
    pf_adjry= malloc (pf_n * PF_MAXADJ * sizeof(fixed_t));
    pf_adjf = calloc (pf_n * PF_MAXADJ, 1);
    pf_adjsg= calloc (pf_n * PF_MAXADJ, sizeof(seg_t*));
    pf_adjline= calloc (pf_n * PF_MAXADJ, sizeof(line_t*));

    pf_ninadj = calloc (pf_n, sizeof(int));
    pf_inadj  = malloc (pf_n * PF_MAXADJ * sizeof(int));
    pf_inadjw = malloc (pf_n * PF_MAXADJ * sizeof(int));
    pf_inadjf = calloc (pf_n * PF_MAXADJ, 1);
    pf_inadjsg= calloc (pf_n * PF_MAXADJ, sizeof(seg_t*));
    pf_inadjline= calloc (pf_n * PF_MAXADJ, sizeof(line_t*));

    pf_heap = malloc (pf_n * sizeof(int));
    pf_hpos = malloc (pf_n * sizeof(int));
    segss   = malloc (numsegs * sizeof(int));

    // centroid of each sub-sector (mean of its segs' endpoints) + seg->ss map
    for (i = 0; i < pf_n; i++)
    {
	subsector_t*	ss = &subsectors[i];
	int64_t		sx = 0, sy = 0;
	int		cnt = 0;
	for (s = 0; s < ss->numlines; s++)
	{
	    seg_t* sg = &segs[ss->firstline + s];
	    sx += sg->v1->x; sy += sg->v1->y;
	    sx += sg->v2->x; sy += sg->v2->y;
	    cnt += 2;
	    segss[ss->firstline + s] = i;
	}
	pf_cx[i] = cnt ? (fixed_t)(sx / cnt) : 0;
	pf_cy[i] = cnt ? (fixed_t)(sy / cnt) : 0;
    }

    // Hazard, baked once per sub-sector instead of probed per edge relaxation.
    // PF_EdgeWeight is now evaluated for EVERY relaxed edge on every search, and its
    // AICoop_DamagingFloor call is an R_PointInSubsector -- a full BSP descent.  At
    // PF_MAXPOP 8000 nodes x up to PF_MAXADJ edges that was a quarter of a million BSP
    // descents per query, several times a second.  Reading a byte here is free.
    // (A sector special changing mid-level is rare, and the graph is rebuilt on
    // P_AICoop_NavDirty / on a failed search anyway.)
    for (i = 0; i < pf_n; i++)
	pf_hazard[i] = AICoop_DamagingFloor (pf_cx[i], pf_cy[i]) ? 1 : 0;

    // (1) Cross-sector edges: each two-sided seg connects to the sub-sector on its
    // far side (probe ~4u off the midpoint).  PF_EdgeWeight handles doors/steps.
    for (j = 0; j < numsegs; j++)
    {
	seg_t*	sg = &segs[j];
	fixed_t	mx, my, nx, ny, len, ox, oy;
	int	a, b, self, v, w;
	byte	eflags;

	if (!sg->backsector) continue;			// one-sided wall
	mx = (sg->v1->x + sg->v2->x) >> 1;
	my = (sg->v1->y + sg->v2->y) >> 1;
	nx = -(sg->v2->y - sg->v1->y);
	ny =  (sg->v2->x - sg->v1->x);
	len = P_AproxDistance (nx, ny);
	if (len < FRACUNIT) continue;
	ox = (fixed_t)(((int64_t)nx * (4*FRACUNIT)) / len);
	oy = (fixed_t)(((int64_t)ny * (4*FRACUNIT)) / len);

	self = segss[j];
	a = PF_SS (mx + ox, my + oy);
	b = PF_SS (mx - ox, my - oy);
	v = (a != self) ? a : (b != self ? b : -1);
	if (v < 0) continue;
	eflags = 0;
	pf_edge_flags = &eflags;
	w = PF_EdgeWeight (sg, NULL, self, v);
	pf_edge_flags = NULL;
	if (w >= 0)
	{
	    // Portal = the seg midpoint nudged ~16u INTO v (the destination side), so
	    // steering at it crosses the shared edge instead of stopping on the wall
	    // line.  ox,oy is the 4u normal; v sits on the +normal side iff v==a.
	    int	s  = (v == a) ? 4 : -4;
	    fixed_t	px = mx + s*ox, py = my + s*oy;
	    fixed_t	plx, ply, prx, pry;

	    // The SPAN, oriented for the funnel: walking self -> v, which end of the seg is
	    // on our left?  (nx,ny) is the LEFT normal of v1->v2, so `a` is the sub-sector on
	    // the seg's left.  Heading toward `a` means forward = +normal, and rotating that
	    // 90 degrees counter-clockwise gives -(v2-v1) -- so v1 is the left end.  Heading
	    // the other way mirrors it.  Derived from the probe that actually picked `v`, not
	    // from sg->frontsector: several sub-sectors share one sector, so comparing sectors
	    // cannot tell the two sides apart and silently mirrored the funnel.
	    if (v == a) PF_InsetSpan (sg->v1->x, sg->v1->y, sg->v2->x, sg->v2->y, px, py,
				      &plx, &ply, &prx, &pry);
	    else	PF_InsetSpan (sg->v2->x, sg->v2->y, sg->v1->x, sg->v1->y, px, py,
				      &plx, &ply, &prx, &pry);

	    // CLEARANCE, baked once here instead of re-probed on every query: a portal
	    // the buddy's 32x32 box cannot actually stand in is not a portal.  The
	    // vertical opening was already checked; nothing checked the horizontal one,
	    // so a gap narrower than the buddy routed fine and then wedged the steering.
	    // (A jump link lands past the step, so probe where it lands, not in the step.)
	    if (P_CheckPosition (ref, px, py)
		&& tmceilingz - tmfloorz >= ref->height)
		PF_AddEdgeF (self, v, w, px, py, plx, ply, prx, pry, eflags, sg, sg->linedef);
	}
    }

    // (2) Grid adjacency -- THE fix for isolated sub-sectors.  In vanilla Doom a
    // sector is split into sub-sectors with NO connecting seg, and an L/concave
    // sector defeats a centroid-to-centroid trace, so some walkable sub-sectors
    // ended up with zero edges (bot standing in one => stuck).  Sample a grid over
    // the map; wherever two adjacent sample points fall in different sub-sectors
    // and a feet-trace between them is clear, connect those sub-sectors.  Catches
    // every walkable adjacency regardless of segs / sector shape.
    {
	const fixed_t	step = 32*FRACUNIT;
	fixed_t		gx, gy;
	fixed_t		xmax = bmaporgx + (fixed_t)bmapwidth *128*FRACUNIT;
	fixed_t		ymax = bmaporgy + (fixed_t)bmapheight*128*FRACUNIT;

	for (gy = bmaporgy; gy < ymax; gy += step)
	for (gx = bmaporgx; gx < xmax; gx += step)
	{
	    int		a = PF_SS (gx, gy);
	    fixed_t	af = subsectors[a].sector->floorheight;
	    int		nb[2], k;

	    nb[0] = PF_SS (gx+step, gy);		// right + down neighbours
	    nb[1] = PF_SS (gx, gy+step);
	    for (k = 0; k < 2; k++)
	    {
		fixed_t	tx = k ? gx : gx+step;
		fixed_t	ty = k ? gy+step : gy;
		int	b = nb[k], w;
		fixed_t	bf;
		boolean	ab, ba;
		if (b == a) continue;
		if (PF_LockedLineCrossed (gx, gy, tx, ty)) continue;
		// Trace EACH direction separately.  PF_LineWalkable only limits the step
		// UP, so a trace DOWN a 64-unit ledge succeeds while the climb back is
		// impossible -- deriving both edges from the single a->b trace put every
		// ledge into the graph as a two-way link.  The buddy then routed "up" a
		// drop it can never climb, walked to the foot of it and wedged there.
		bf = subsectors[b].sector->floorheight;
		ab = PF_LineWalkable (gx, gy, tx, ty, ref, af);
		ba = PF_LineWalkable (tx, ty, gx, gy, ref, bf);
		if (!ab && !ba) continue;
		w = (int)(P_AproxDistance (pf_cx[a]-pf_cx[b], pf_cy[a]-pf_cy[b]) >> FRACBITS);
		if (w < 1) w = 1;
		// Portal = a grid point INSIDE the destination sub-sector (the walkable
		// sample we just trace-verified), so steering at it crosses the boundary.
		//
		// Give it a SPAN as well, perpendicular to the a->b direction, so the funnel
		// has something to string-pull against.  Most adjacency in an open room comes
		// from this pass, and a point portal degenerates the funnel straight back to
		// the old centroid-to-centroid zig-zag.  Each end is verified with the buddy's
		// own box and falls back to the centre when it doesn't fit -- widening blind
		// would put a funnel corner inside a wall.
		{
		    fixed_t	sdx = tx - gx, sdy = ty - gy;		// a -> b
		    fixed_t	slen = P_AproxDistance (sdx, sdy);
		    fixed_t	perpx = 0, perpy = 0;
		    fixed_t	alx, aly, arx, ary, blx, bly, brx, bry;

		    if (slen)	// left of forward = rot90(forward) = (-dy, dx), scaled to 16
		    {
			perpx = (fixed_t)(((int64_t)(-sdy) * (16*FRACUNIT)) / slen);
			perpy = (fixed_t)(((int64_t)( sdx) * (16*FRACUNIT)) / slen);
		    }
		    blx = tx + perpx; bly = ty + perpy;
		    brx = tx - perpx; bry = ty - perpy;
		    if (!P_CheckPosition (ref, blx, bly)) { blx = tx; bly = ty; }
		    if (!P_CheckPosition (ref, brx, bry)) { brx = tx; bry = ty; }
		    // b -> a walks the same boundary the other way, so left and right swap.
		    arx = gx + perpx; ary = gy + perpy;
		    alx = gx - perpx; aly = gy - perpy;
		    if (!P_CheckPosition (ref, arx, ary)) { arx = gx; ary = gy; }
		    if (!P_CheckPosition (ref, alx, aly)) { alx = gx; aly = gy; }

		    if (ab) PF_AddEdgeF (a, b, w + (pf_hazard[b] ? PF_HAZARD_PEN : 0),
					 tx, ty, blx, bly, brx, bry, 0, NULL, NULL);
		    if (ba) PF_AddEdgeF (b, a, w + (pf_hazard[a] ? PF_HAZARD_PEN : 0),
					 gx, gy, alx, aly, arx, ary, 0, NULL, NULL);
		}
	    }
	}
    }

    // (3) Teleporter edges: a walk-over teleport trigger is a real, one-way link from the
    // sub-sectors in FRONT of the line to the MT_TELEPORTMAN's sub-sector.  Without it the
    // graph has no route at all once the human steps through a teleporter, and the buddy
    // burns the full 12 s no-progress watchdog before anything rescues it.
    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	int	dest_ss;
	int	t;

	if (!PF_IsTeleportSpecial (ld->special))	continue;
	if (!ld->backsector)				continue;	// can't be walked across
	dest_ss = PF_FindTeleportTarget (ld);
	if (dest_ss < 0 || dest_ss >= pf_n)		continue;

	// EV_Teleport bails out on `side == 1`, i.e. the trigger only fires when the line is
	// crossed from its FRONT.  Sampling the bare midpoint put the source sub-sector on
	// whichever side R_PointInSubsector happened to pick for a point sitting exactly on
	// the line -- half the time the back -- and the buddy then walked the line from the
	// dead side and nothing happened.  Sample explicitly in front, at three points along
	// the line, because a long trigger spans several sub-sectors.
	for (t = 1; t <= 3; t++)
	{
	    fixed_t	mx = ld->v1->x + (fixed_t)(((int64_t)(ld->v2->x - ld->v1->x) * t) / 4);
	    fixed_t	my = ld->v1->y + (fixed_t)(((int64_t)(ld->v2->y - ld->v1->y) * t) / 4);
	    fixed_t	ldx = ld->v2->x - ld->v1->x, ldy = ld->v2->y - ld->v1->y;
	    fixed_t	llen = P_AproxDistance (ldx, ldy);
	    fixed_t	nx, ny, fx, fy, bx, by;
	    int		src_ss, tw;

	    if (!llen) break;
	    nx = (fixed_t)(((int64_t) ldy * (16*FRACUNIT)) / llen);	// one side normal
	    ny = (fixed_t)(((int64_t)-ldx * (16*FRACUNIT)) / llen);
	    if (P_PointOnLineSide (mx + nx, my + ny, ld) != 0) { nx = -nx; ny = -ny; }
	    fx = mx + nx; fy = my + ny;			// in front  (approach from here)
	    bx = mx - nx; by = my - ny;			// behind    (steer at this to cross)

	    src_ss = PF_SS (fx, fy);
	    if (src_ss < 0 || src_ss >= pf_n || src_ss == dest_ss) continue;

	    // Cost = what walking between the two ends would cost.  A near-free constant
	    // would be truer to the teleport, but PF_H is the straight-line distance to the
	    // goal and stays admissible only while every edge weight is at least the straight
	    // line it spans -- a cheap teleport edge would break A* optimality outright.
	    tw = (int)(P_AproxDistance (pf_cx[src_ss] - pf_cx[dest_ss],
					pf_cy[src_ss] - pf_cy[dest_ss]) >> FRACBITS);
	    if (tw < 1) tw = 1;
	    // Portal = just BEHIND the line, so steering at it actually carries the buddy
	    // across the trigger instead of parking it on the near side.
	    PF_AddEdgeF (src_ss, dest_ss, tw, bx, by, bx, by, bx, by,
			 PF_EDGE_TELEPORT, NULL, ld);
	}
    }

    free (segss);
}

// Force the navigation graph to rebuild on the next pathfind.  The graph bakes in each
// door's passability at build time, so when the map's passability changes mid-level --
// notably a locked door the player just unlocked (EV_VerticalDoor demotes its special) --
// the buddy's cached route is stale.  Calling this invalidates it so the route picks up
// the now-openable door immediately, instead of only after an A* dead-end + rebuild.


void P_AICoop_NavDirty (void)
{
    pf_level = -1;
}

// Cost of crossing seg `sg` / line `ld_param` from sub-sector u to its neighbour v; -1 = blocked.
// Dynamic height & door evaluation at query-time.
// `pf_edge_flags` (set by the caller for the duration of the call) collects PF_EDGE_*
// bits describing HOW the edge is crossed, so the route can carry them to the steering.
static int PF_EdgeWeight (seg_t* sg, line_t* ld_param, int u, int v)
{
    int	w = (int)(P_AproxDistance (pf_cx[u]-pf_cx[v], pf_cy[u]-pf_cy[v]) >> FRACBITS);
    line_t* ld = ld_param ? ld_param : (sg ? sg->linedef : NULL);

    if (w < 1) w = 1;
    if (ld)						// real wall line (not a BSP miniseg)
    {
	int sp = ld->special;
	int color = 0;
	if      (sp==26 || sp==32 || sp==99 || sp==133) color = 1;	// blue
	else if (sp==27 || sp==34 || sp==136 || sp==137) color = 2;	// yellow
	else if (sp==28 || sp==33 || sp==134 || sp==135) color = 3;	// red

	if (color && !PF_HasKey (color)) return -1;		// locked door we don't have the key for is blocked!

	sector_t*	fs = subsectors[u].sector;
	sector_t*	bs = subsectors[v].sector;
	fixed_t		opening, step;

	if (ld->flags & ML_BLOCKING) return -1;		// impassable rail
	opening = (fs->ceilingheight < bs->ceilingheight ? fs->ceilingheight : bs->ceilingheight)
		- (fs->floorheight   > bs->floorheight   ? fs->floorheight   : bs->floorheight);
	step    = bs->floorheight - fs->floorheight;

	// PF_EDGE_DOOR means "there is a SHUT door on this edge", not "this edge has a door
	// linedef".  Flagging an already-open door made the steering tap USE while walking
	// through it, and USE on a DR door that is open or still rising reverses it -- the
	// buddy pulled the door shut on top of itself, then bumped it open again, forever.
	if (opening < 56*FRACUNIT)			// won't fit right now
	{
	    if (PF_IsDoorSpecial (ld->special))
	    {
		w += PF_DOOR_PEN;	// can open it
		if (pf_edge_flags) *pf_edge_flags |= PF_EDGE_DOOR;
	    }
	    else return -1;
	}
	else if (step > 24*FRACUNIT)
	{
	    // Too tall to WALK up -- but the buddy can jump (the human can, so it may).
	    if (netgame || step > PF_JUMP_MAX) return -1;	// no jumping in a netgame
	    if (pf_edge_flags) *pf_edge_flags |= PF_EDGE_JUMP;
	    w += PF_JUMP_PEN;
	}
    }
    if (pf_hazard && pf_hazard[v])
	w += PF_HAZARD_PEN;
    return w;
}

static int	pf_hgx, pf_hgy;		// goal centroid, for PF_H

// Admissible heuristic: straight-line centroid distance to the goal.  Every edge weight is
// a real centroid distance plus only NON-NEGATIVE penalties, so h never overestimates and
// the first pop of the goal is optimal.
//
// pf_noheur switches it off for PF_DijkstraMap.  That search is rooted AT the goal and
// computes a whole field, so "distance to the goal" is really distance back to the source:
// adding it to the key means nodes stop being popped in non-decreasing g order, and a node
// closed at a suboptimal g is never relaxed again.  A field must be plain Dijkstra.
static int PF_H (int u)
{
    if (pf_noheur) return 0;
    return (int)(P_AproxDistance (pf_cx[u]-pf_hgx, pf_cy[u]-pf_hgy) >> FRACBITS);
}

static void PF_HeapSwap (int a, int b)
{
    int t = pf_heap[a]; pf_heap[a] = pf_heap[b]; pf_heap[b] = t;
    pf_hpos[pf_heap[a]] = a;
    pf_hpos[pf_heap[b]] = b;
}

static void PF_HeapUp (int i)
{
    while (i > 0)
    {
	int p = (i - 1) >> 1;
	if (pf_dist[pf_heap[p]] + PF_H (pf_heap[p]) <= pf_dist[pf_heap[i]] + PF_H (pf_heap[i]))
	    break;
	PF_HeapSwap (i, p); i = p;
    }
}

static void PF_HeapDown (int i)
{
    for (;;)
    {
	int l = 2*i + 1, r = l + 1, m = i;
	if (l < pf_heapn && pf_dist[pf_heap[l]] + PF_H (pf_heap[l]) < pf_dist[pf_heap[m]] + PF_H (pf_heap[m])) m = l;
	if (r < pf_heapn && pf_dist[pf_heap[r]] + PF_H (pf_heap[r]) < pf_dist[pf_heap[m]] + PF_H (pf_heap[m])) m = r;
	if (m == i) break;
	PF_HeapSwap (i, m); i = m;
    }
}

static void PF_HeapPush (int v)
{
    if (pf_hpos[v] >= 0) { PF_HeapUp (pf_hpos[v]); return; }	// already open -> its f only ever drops
    pf_heap[pf_heapn] = v; pf_hpos[v] = pf_heapn; pf_heapn++;
    PF_HeapUp (pf_heapn - 1);
}

static int PF_HeapPop (void)
{
    int v;
    if (!pf_heapn) return -1;
    v = pf_heap[0];
    pf_hpos[v] = -1;
    pf_heapn--;
    if (pf_heapn) { pf_heap[0] = pf_heap[pf_heapn]; pf_hpos[pf_heap[0]] = 0; PF_HeapDown (0); }
    return v;
}

// Single-goal REVERSE Dijkstra map ("flow field") rooted at the human's sub-sector.
//
// The buddy, every director-steered monster and the automap overlay all ask the same
// question -- "how do I get to the human?" -- so one backward wave answers all of them at
// once and `pf_flow_next[u]` is the optimal next hop from anywhere.  A per-actor forward
// A* had to be thrown away and redone every time the human stepped into another
// sub-sector, which on an open map is most tics.
//
// Plain Dijkstra, NOT A*: see PF_H / pf_noheur.
static void PF_DijkstraMap (int goal_ss)
{
    int i, pop = 0;

    if (!pf_flow_dist)
    {
	pf_flow_dist = malloc (pf_n * sizeof(int));
	pf_flow_next = malloc (pf_n * sizeof(int));
    }

    pf_flow_level = gameepisode*100 + gamemap;
    pf_flow_player_ss = goal_ss;
    pf_flow_tic = gametic;
    pf_flow_safe = pf_safemode;		// the field bakes in the safe-route weighting

    for (i = 0; i < pf_n; i++)
    {
	pf_flow_dist[i] = PF_INF;
	pf_flow_next[i] = -1;
	pf_done[i] = 0;
	pf_hpos[i] = -1;
	pf_dist[i] = PF_INF;
    }

    pf_hgx = pf_cx[goal_ss]; pf_hgy = pf_cy[goal_ss];
    pf_heapn = 0;
    pf_flow_dist[goal_ss] = 0;
    pf_dist[goal_ss] = 0;
    pf_noheur = 1;
    PF_HeapPush (goal_ss);

    while (pop < PF_MAXPOP)
    {
	int v = PF_HeapPop (), s;
	if (v < 0) break;
	if (pf_done[v]) continue;
	pf_done[v] = 1; pop++;

	// Relax incoming edges: u -> v (u can walk into v)
	for (s = 0; s < pf_ninadj[v]; s++)
	{
	    int u = pf_inadj[v*PF_MAXADJ + s];
	    byte flags = pf_inadjf[v*PF_MAXADJ + s];
	    int w;

	    if (pf_done[u]) continue;

	    if (flags & PF_EDGE_TELEPORT)
		w = pf_inadjw[v*PF_MAXADJ + s];
	    else
	    {
		w = PF_EdgeWeight (pf_inadjsg[v*PF_MAXADJ + s], pf_inadjline[v*PF_MAXADJ + s], u, v);
		if (w < 0) continue;
	    }

	    if (pf_safemode) w += pf_danger[v] * PF_DANGER_W;
	    if (pf_flow_dist[v] + w < pf_flow_dist[u])
	    {
		pf_flow_dist[u] = pf_flow_dist[v] + w;
		pf_flow_next[u] = v; // optimal next hop from u towards goal_ss is v!
		pf_dist[u] = pf_flow_dist[u];
		PF_HeapPush (u);
	    }
	}
    }
    pf_noheur = 0;
    if (pop >= PF_MAXPOP)
	pf_cap_maxpop_cnt++;
}

static boolean PF_AStar (int start, int goal)
{
    int	i, pop = 0;

    for (i = 0; i < pf_n; i++) { pf_dist[i] = PF_INF; pf_prev[i] = -1; pf_done[i] = 0; pf_hpos[i] = -1; }
    pf_hgx = pf_cx[goal]; pf_hgy = pf_cy[goal];
    pf_heapn = 0;
    pf_dist[start] = 0;
    PF_HeapPush (start);

    while (pop < PF_MAXPOP)
    {
	int	u = PF_HeapPop (), s;

	if (u < 0) break;				// nothing left reachable
	if (u == goal) return true;			// goal popped -> optimal path found
	if (pf_done[u]) continue;
	pf_done[u] = 1; pop++;

	for (s = 0; s < pf_nadj[u]; s++)
	{
	    int	v = pf_adj[u*PF_MAXADJ + s];
	    byte flags = pf_adjf[u*PF_MAXADJ + s];
	    int	w;

	    if (pf_done[v]) continue;

	    if (flags & PF_EDGE_TELEPORT)
		w = pf_adjw[u*PF_MAXADJ + s];
	    else
	    {
		w = PF_EdgeWeight (pf_adjsg[u*PF_MAXADJ + s], pf_adjline[u*PF_MAXADJ + s], u, v);
		if (w < 0) continue; // blocked dynamically
	    }

	    if (pf_safemode) w += pf_danger[v] * PF_DANGER_W;	// Safe mode: detour around hot sub-sectors
	    if (pf_dist[u] + w < pf_dist[v])
	    {
		pf_dist[v] = pf_dist[u] + w; pf_prev[v] = u;
		PF_HeapPush (v);
	    }
	}
    }
    if (pop >= PF_MAXPOP)
	pf_cap_maxpop_cnt++;

    return pf_dist[goal] < PF_INF;
}

#define PF_CORR_MAX	64
static fixed_t	corr_x[PF_CORR_MAX], corr_y[PF_CORR_MAX];
static byte	corr_jump[PF_CORR_MAX];	// crossing INTO this portal needs a jump
static byte	corr_door[PF_CORR_MAX];	// crossing INTO this portal passes a door
static int	corr_n;			// portals held
static int	corr_i;			// next portal to aim for
static int	corr_goal = -1;		// goal sub-sector the corridor was built for
static int	corr_next_jump;		// the waypoint just handed back is a jump link
static int	corr_next_door;		// the waypoint just handed back is a door link

static void PF_CorridorReset (void)
{
    corr_n = corr_i = 0; corr_goal = -1; corr_next_jump = 0; corr_next_door = 0;
}

static void PF_NavReset (void)
{
    pf_level = -1;			// force a rebuild on the next query
    PF_CorridorReset ();
}

static int PF_NextIsJump (void)
{
    return corr_next_jump;
}

static int PF_NextIsDoor (void)
{
    return corr_next_door;
}

typedef struct {
    fixed_t lx, ly;		// portal span, LEFT end as seen walking u -> v
    fixed_t rx, ry;		// ... RIGHT end
    fixed_t px, py;		// the trace-verified crossing point on this edge
    byte    flags;		// PF_EDGE_*
} pf_portal_t;

static pf_portal_t pf_portals[PF_PATHMAX];

// Edges that must NOT be smoothed away.  A door has to be walked into head-on so USE
// reaches it, a jump has to be taken at the step, and a teleporter has to be crossed.
// String-pulling past one drops both the waypoint and its flag, and the buddy then never
// pressed BT_JUMP for a jump link the route had specifically planned for.
// Modelling them as ZERO-WIDTH portals gets this for free: a funnel cannot see through a
// degenerate portal, so the apex is always forced onto it and its flags always travel
// with the emitted waypoint -- no special case inside the funnel loop.
#define PF_ANCHOR_FLAGS	(PF_EDGE_JUMP | PF_EDGE_DOOR | PF_EDGE_TELEPORT)

static void PF_FunnelEmit (fixed_t* out_x, fixed_t* out_y, byte* out_j, byte* out_d,
			   int* nout, fixed_t x, fixed_t y, byte flags)
{
    if (*nout >= PF_CORR_MAX) return;
    if (*nout && out_x[*nout - 1] == x && out_y[*nout - 1] == y) return;	// no duplicates
    out_x[*nout] = x; out_y[*nout] = y;
    out_j[*nout] = (flags & PF_EDGE_JUMP) ? 1 : 0;
    out_d[*nout] = (flags & PF_EDGE_DOOR) ? 1 : 0;
    (*nout)++;
}

// Simple Stupid Funnel Algorithm (Mononen) over the portal spans of the planned route.
// The apex starts at the buddy's feet; every emitted point is a corner the straight walk
// actually has to bend around, so the corridor hugs doorways instead of stepping from
// sub-sector centroid to sub-sector centroid.
//
// Orientation note: PF_Cross is the STANDARD cross product (positive = left of the ray),
// so the comparisons below are Mononen's with their signs flipped -- his triarea2 is the
// negated cross.  The previous version used the standard cross with his unflipped signs,
// which silently swapped the meaning of the left and right bounds.
static int PF_BuildFunnelCorridor (mobj_t* mo, int start, fixed_t dx, fixed_t dy,
				   int* fwd_path, int len,
				   fixed_t* out_x, fixed_t* out_y, byte* out_j, byte* out_d)
{
    int		i, num_portals = 0, nout = 0;
    int		prev_ss = start;
    fixed_t	apex_x = mo->x, apex_y = mo->y;
    fixed_t	left_x = mo->x, left_y = mo->y, right_x = mo->x, right_y = mo->y;
    int		left_idx = 0, right_idx = 0;
    boolean	truncated = false;

    for (i = 0; i < len && num_portals < PF_PATHMAX; i++)
    {
	int		next_ss = fwd_path[i];
	pf_portal_t*	p = &pf_portals[num_portals];
	int		k, found = -1;

	for (k = 0; k < pf_nadj[prev_ss]; k++)
	    if (pf_adj[prev_ss*PF_MAXADJ + k] == next_ss) { found = k; break; }
	if (found < 0) break;			// route and graph disagree -- stop here

	k = prev_ss*PF_MAXADJ + found;
	p->px = pf_adjpx[k]; p->py = pf_adjpy[k];
	p->flags = pf_adjf[k];
	if (p->flags & PF_ANCHOR_FLAGS)		// zero-width: force it onto the path
	    { p->lx = p->rx = p->px; p->ly = p->ry = p->py; }
	else
	    { p->lx = pf_adjlx[k]; p->ly = pf_adjly[k];
	      p->rx = pf_adjrx[k]; p->ry = pf_adjry[k]; }
	num_portals++;
	prev_ss = next_ss;

	// A teleporter breaks the funnel's one assumption -- that consecutive portals are
	// spatially adjacent.  Beyond it the route continues somewhere else entirely on the
	// map, and string-pulling "straight" from the trigger line to the far room draws a
	// line through solid walls.  So end the corridor AT the trigger: the buddy walks
	// across it, its start sub-sector changes, and the next query re-plans from there.
	if (p->flags & PF_EDGE_TELEPORT) { truncated = true; break; }
    }

    if (!truncated && num_portals < PF_PATHMAX)		// the goal itself, zero-width
    {
	pf_portal_t* p = &pf_portals[num_portals];
	p->lx = p->rx = p->px = dx;
	p->ly = p->ry = p->py = dy;
	p->flags = 0;
	num_portals++;
    }

    if (num_portals == 0) return 0;

    for (i = 0; i < num_portals; i++)
    {
	fixed_t plx = pf_portals[i].lx, ply = pf_portals[i].ly;
	fixed_t prx = pf_portals[i].rx, pry = pf_portals[i].ry;

	// tighten the RIGHT bound (candidate moved inward, i.e. left of the current bound)
	if (PF_Cross (apex_x, apex_y, right_x, right_y, prx, pry) >= 0)
	{
	    if ((apex_x == right_x && apex_y == right_y) ||
		PF_Cross (apex_x, apex_y, left_x, left_y, prx, pry) < 0)
	    {
		right_x = prx; right_y = pry; right_idx = i;
	    }
	    else					// crossed the left bound -> corner
	    {
		PF_FunnelEmit (out_x, out_y, out_j, out_d, &nout,
			       left_x, left_y, pf_portals[left_idx].flags);
		apex_x = left_x; apex_y = left_y;
		i = left_idx + 1;
		if (i >= num_portals) break;
		left_x = pf_portals[i].lx; left_y = pf_portals[i].ly;
		right_x = pf_portals[i].rx; right_y = pf_portals[i].ry;
		left_idx = right_idx = i;
		continue;
	    }
	}

	// tighten the LEFT bound
	if (PF_Cross (apex_x, apex_y, left_x, left_y, plx, ply) <= 0)
	{
	    if ((apex_x == left_x && apex_y == left_y) ||
		PF_Cross (apex_x, apex_y, right_x, right_y, plx, ply) > 0)
	    {
		left_x = plx; left_y = ply; left_idx = i;
	    }
	    else					// crossed the right bound -> corner
	    {
		PF_FunnelEmit (out_x, out_y, out_j, out_d, &nout,
			       right_x, right_y, pf_portals[right_idx].flags);
		apex_x = right_x; apex_y = right_y;
		i = right_idx + 1;
		if (i >= num_portals) break;
		left_x = pf_portals[i].lx; left_y = pf_portals[i].ly;
		right_x = pf_portals[i].rx; right_y = pf_portals[i].ry;
		left_idx = right_idx = i;
		continue;
	    }
	}
    }

    // Always finish on the last portal's own crossing point: the goal when the route ran
    // to the end, the teleport trigger when it was cut short there.
    PF_FunnelEmit (out_x, out_y, out_j, out_d, &nout,
		   pf_portals[num_portals-1].px, pf_portals[num_portals-1].py,
		   pf_portals[num_portals-1].flags);

    return nout;
}

// Walk the flow field from `start` toward the goal, writing each hop into `out`.
// 0 = the field has no route out of `start` at all.  A path clipped by PF_PATHMAX is
// still returned: every hop on it is an optimal step toward the goal, so the prefix is
// usable and only the far end is missing (and the clip is counted for `navdbg`).
static int PF_FlowPath (int start, int goal, int* out)
{
    int	c = start, len = 0;

    if (!pf_flow_next || start < 0 || start >= pf_n) return 0;
    if (pf_flow_next[start] == -1) return 0;
    while (c != goal && len < PF_PATHMAX && pf_flow_next[c] != -1)
    {
	out[len++] = pf_flow_next[c];
	c = pf_flow_next[c];
    }
    if (len >= PF_PATHMAX) pf_cap_pathmax_cnt++;
    return len;
}

static boolean PF_NextWaypoint (mobj_t* mo, fixed_t dx, fixed_t dy, fixed_t* wx, fixed_t* wy)
{
    int		start, goal, len, i, c, np;
    fixed_t	portx[PF_PATHMAX], porty[PF_PATHMAX];
    byte	portj[PF_PATHMAX], portd[PF_PATHMAX];
    int		fwd_path[PF_PATHMAX];
    boolean	owner;			// this query is the buddy's own
    mobj_t*	pl_human;
    int		pl_ss;
    boolean	use_flow;

    if (pf_level != gameepisode*100 + gamemap || !pf_cx)
    { PF_Build (mo); pf_level = gameepisode*100 + gamemap; pf_lastbuild = gametic;
      PF_CorridorReset (); }

    owner = (mo == AICoop_Mo ());

    if (owner) { corr_next_jump = 0; corr_next_door = 0; }
    start = PF_SS (mo->x, mo->y);
    goal  = PF_SS (dx, dy);
    if (start == goal)
    {
	if (owner) PF_CorridorReset ();
	*wx = dx; *wy = dy; return true;
    }

    if (owner && corr_n && corr_goal == goal)
    {
	while (corr_i < corr_n
	       && P_AproxDistance (mo->x - corr_x[corr_i], mo->y - corr_y[corr_i]) < 32*FRACUNIT)
	    corr_i++;
	if (corr_i >= corr_n)
	{
	    if (AICoop_CanReach (mo, dx, dy, true)) { *wx = dx; *wy = dy; return true; }
	}
	else
	{
	    int hi = corr_i + 6;
	    if (hi > corr_n - 1) hi = corr_n - 1;
	    for (i = hi; i >= corr_i; i--)
		if (P_AproxDistance (mo->x - corr_x[i], mo->y - corr_y[i]) <= 512*FRACUNIT
		    && AICoop_CanReach (mo, corr_x[i], corr_y[i], true))
		{
		    corr_i = i;
		    corr_next_jump = corr_jump[i];
		    corr_next_door = corr_door[i];
		    *wx = corr_x[i]; *wy = corr_y[i];
		    return true;
		}
	}
	PF_CorridorReset ();	// drifted off it -- fall through and re-plan
    }

    pl_human = AICoop_NearestHuman (mo->x, mo->y);
    pl_ss = pl_human ? PF_SS (pl_human->x, pl_human->y) : -1;
    use_flow = (goal == pl_ss && pl_ss >= 0);

    len = 0;
    if (use_flow)
    {
	// Recompute the field when it has gone stale: an older tic bucket, the human moved
	// to a different sub-sector, a new level -- or the safe-route weighting changed,
	// which the check used to ignore, so a hurt buddy kept following a field costed
	// without pf_danger (and vice versa) for up to 10 tics.
	if (pf_flow_tic != gametic
	    && (gametic - pf_flow_tic >= 10 || pf_flow_player_ss != goal
		|| pf_flow_level != gameepisode*100 + gamemap || pf_flow_safe != pf_safemode))
	    PF_DijkstraMap (goal);

	// There is one field and it holds ONE root.  The recompute above is capped at once
	// per tic, so with two humans on the map a second actor asking for a different root
	// in the same tic would otherwise silently follow the first one's field all the way
	// to the wrong human.  If the field isn't ours, fall through to A* this tic.
	if (pf_flow_player_ss != goal)
	    use_flow = false;
	else
	{
	    len = PF_FlowPath (start, goal, fwd_path);
	    if (!len)
	    {
		// Same rescue the A* branch does: the graph is a snapshot, so a door / lift /
		// secret that has opened since is not in it yet.  Rebuild (rate-limited) and
		// retry once.
		if (gametic - pf_lastbuild > 50)
		{
		    PF_Build (mo); pf_lastbuild = gametic;
		    if (owner) PF_CorridorReset ();
		    PF_DijkstraMap (goal);
		    len = PF_FlowPath (start, goal, fwd_path);
		}
		// Still nothing -> report FALSE.  Handing the caller the human's raw position
		// with `true` claimed "reachable, walk straight at them": navok stayed 1, the
		// retry backoff never armed, the breadcrumb trail and the "lost" fallback never
		// engaged, and the buddy just ground into the wall between the two of them.
		if (!len) return false;
	    }
	}
    }

    if (!use_flow)
    {
	if (!PF_AStar (start, goal))
	{
	    if (gametic - pf_lastbuild > 50)
	    {
		PF_Build (mo); pf_lastbuild = gametic;
		if (owner) PF_CorridorReset ();
		if (!PF_AStar (start, goal)) return false;
	    }
	    else
		return false;
	}

	len = 0; c = goal;
	while (c != -1 && c != start && len < PF_PATHMAX) { pf_path[len++] = c; c = pf_prev[c]; }
	if (len >= PF_PATHMAX) pf_cap_pathmax_cnt++;
	if (len == 0) { *wx = dx; *wy = dy; return true; }

	for (i = 0; i < len; i++)
	    fwd_path[i] = pf_path[len - 1 - i];
    }

    np = PF_BuildFunnelCorridor (mo, start, dx, dy, fwd_path, len, portx, porty, portj, portd);

    if (owner)
    {
	corr_n = np < PF_CORR_MAX ? np : PF_CORR_MAX;
	for (i = 0; i < corr_n; i++)
	    { corr_x[i] = portx[i]; corr_y[i] = porty[i]; corr_jump[i] = portj[i]; corr_door[i] = portd[i]; }
	corr_i = 0; corr_goal = goal;
    }

    if (np > 0)
    {
	fixed_t dist = P_AproxDistance (mo->x - portx[0], mo->y - porty[0]);
	fixed_t tx = (np > 1) ? portx[1] : dx;
	fixed_t ty = (np > 1) ? porty[1] : dy;
	if (dist < 32*FRACUNIT || (dist < 56*FRACUNIT && AICoop_CanReach (mo, tx, ty, true)))
	{
	    if (owner)
	    {
		corr_i = (np > 1) ? 1 : corr_n;
		corr_next_jump = (np > 1) ? portj[1] : 0;
		corr_next_door = (np > 1) ? portd[1] : 0;
	    }
	    *wx = tx; *wy = ty;
	    return true;
	}
    }

    if (AICoop_CanReach (mo, dx, dy, true)) { *wx = dx; *wy = dy; return true; }
    for (i = np - 1; i >= 0; i--)
	if (AICoop_CanReach (mo, portx[i], porty[i], true))
	{
	    if (owner && i < corr_n) { corr_i = i; corr_next_jump = portj[i]; corr_next_door = portd[i]; }
	    *wx = portx[i]; *wy = porty[i]; return true;
	}
    if (np > 0)
    {
	if (owner) { corr_i = 0; corr_next_jump = portj[0]; corr_next_door = portd[0]; }
	*wx = portx[0]; *wy = porty[0]; return true;
    }
    // No corridor at all and the goal is not directly walkable -- say so instead of
    // pretending the raw goal is a valid waypoint.
    return false;
}

// Record that a player (human or buddy) took damage where it is standing, so the
// buddy's Safe route mode (low HP / retreat) can later steer around that sub-sector.
// Called from P_DamageMobj.  Runtime-only + decaying (no savegame impact); a no-op
// until the nav graph (and thus pf_danger) exists.
void P_AICoop_NoteDamage (mobj_t* victim, mobj_t* source, int damage)
{
    if (!companion_active || damage <= 0) return;
    if (pf_danger)					// danger heatmap (Safe route)
    {
	int ss = PF_SS (victim->x, victim->y);
	if (ss >= 0 && ss < pf_n)
	{
	    pf_danger[ss] += damage;
	    if (pf_danger[ss] > PF_DANGER_MAX) pf_danger[ss] = PF_DANGER_MAX;
	}
    }
    // Friendly fire: the human shot the buddy -> Duke-style protest.
    if (P_AICoop_IsBuddy (victim->player) && source && source->player == &players[0])
	AICoop_Callout ("ff:", 6);
}

// Public pathfinder: next reachable waypoint for `mo` toward (dx,dy).  The sub-sector
// graph is map-global, so director-controlled MONSTERS use it too (p_ai_llm.c) to
// round corners toward their target instead of the vanilla straight-line 8-dir walk.
boolean P_AICoop_NextWaypoint (mobj_t* mo, fixed_t dx, fixed_t dy, fixed_t* wx, fixed_t* wy)
{
    return PF_NextWaypoint (mo, dx, dy, wx, wy);
}


// --- Breadcrumb chain ------------------------------------------------------
// The crumbs are the human's actual route, so consecutive crumbs LOOK like a ready-
// made path -- but only where the human walked.  Where they jumped a ledge, dropped
// off one, or took a teleporter, the buddy cannot repeat the hop, and a trail picked
// over by straight-line distance happily selects a crumb on the far side of exactly
// that gap.  So link the trail first: one short feet-trace per consecutive pair (the
// same probe the nav graph uses), marking each link walkable or not.  Cheap -- crumbs
// are 48 units apart, so a link is a 2-sample trace, and the whole table is rebuilt
// at most ~3x/s.  (crumb_link[]/crumb_link_tic are declared up with the trail.)
static void AICoop_CrumbRelink (mobj_t* ref)
{
    extern int	pf_ignore_actors;
    int		i;

    if (crumb_link_tic >= 0 && gametic - crumb_link_tic < 10) return;
    crumb_link_tic = gametic;

    pf_ignore_actors = 1;			// a monster standing on the trail doesn't break it
    for (i = 0; i + 1 < crumb_n; i++)
    {
	fixed_t	fz = R_PointInSubsector (crumbx[i], crumby[i])->sector->floorheight;
	crumb_link[i] = PF_LineWalkable (crumbx[i], crumby[i],
					 crumbx[i+1], crumby[i+1], ref, fz) ? 1 : 0;
    }
    pf_ignore_actors = 0;
}

// Oldest crumb still joined to the NEWEST one (~the human) by an unbroken run of
// walkable links.  Crumbs older than this are behind a gap the buddy can't repeat,
// so they can never lead it to the human on foot -- following them is what put it
// under the ledge the human jumped instead of on the stairs around.
static int AICoop_CrumbChainStart (void)
{
    int	i = crumb_n - 1;
    while (i > 0 && crumb_link[i-1]) i--;
    return i < 0 ? 0 : i;
}

// Topological route for the LLM director: fill (xs,ys) with up to `maxpts` reachable
// waypoints along the buddy->player path (the portal route, downsampled), so the
// director has real spatial context + valid coordinates it can steer the buddy to
// with a `goto`.  Returns the number of points (0 if same room / no route).
//
// Also what the automap overlay draws, which is why the result is CACHED per gametic:
// the overlay asks once per rendered frame, and at 100+ fps that was 100+ full route
// searches a second for a route that can only change 35 times a second.
// The search itself is the flow field when the goal is the human (the usual case) --
// the field is already there for the buddy, so this costs a pointer walk.
int P_AICoop_NavRoute (fixed_t* xs, fixed_t* ys, int maxpts)
{
    static fixed_t	cache_x[PF_PATHMAX], cache_y[PF_PATHMAX];
    static int		cache_n = 0;
    static int		cache_tic = -1;
    static int		cache_max = 0;

    mobj_t*	mo = AICoop_Mo ();
    mobj_t*	pl;
    int		start, goal, len, i, c, n, prev, np, step;
    fixed_t	px[PF_PATHMAX], py[PF_PATHMAX];
    int		fwd[PF_PATHMAX];

    if (!mo || maxpts <= 0) return 0;
    if (maxpts > PF_PATHMAX) maxpts = PF_PATHMAX;		// bound the cache copy

    // Serve from the cache only for the SAME maxpts.  The result is downsampled to fit
    // the caller's budget, so the first 6 points of a 64-point answer are the near sixth
    // of the route, not a 6-point summary of it -- and the two callers (automap 64, LLM
    // director 6) would otherwise hand each other exactly that.
    if (cache_tic == gametic && cache_max == maxpts)
    {
	for (i = 0; i < cache_n; i++) { xs[i] = cache_x[i]; ys[i] = cache_y[i]; }
	return cache_n;
    }
    cache_tic = gametic; cache_n = 0; cache_max = maxpts;

    pl = AICoop_NearestHuman (mo->x, mo->y);
    if (!pl) return 0;

    if (pf_level != gameepisode*100 + gamemap || !pf_cx)
	{ PF_Build (mo); pf_level = gameepisode*100 + gamemap; pf_lastbuild = gametic; }

    start = PF_SS (mo->x, mo->y);
    goal  = PF_SS (pl->x, pl->y);
    if (start == goal) return 0;

    // Reuse the buddy's flow field when it is current for this goal; only fall back to a
    // fresh A* when it isn't (another goal, another level, or nothing computed yet).
    if (pf_flow_next && pf_flow_player_ss == goal
	&& pf_flow_level == gameepisode*100 + gamemap
	&& (len = PF_FlowPath (start, goal, fwd)) > 0)
	;
    else
    {
	if (!PF_AStar (start, goal)) return 0;
	len = 0; c = goal;
	while (c != -1 && c != start && len < PF_PATHMAX) { pf_path[len++] = c; c = pf_prev[c]; }
	if (len == 0) return 0;
	for (i = 0; i < len; i++) fwd[i] = pf_path[len - 1 - i];
    }

    n = 0; prev = start;
    for (i = 0; i < len; i++)				// start -> goal portal points
    {
	fixed_t qx, qy;
	if (PF_Portal (prev, fwd[i], &qx, &qy)) { px[n] = qx; py[n] = qy; n++; }
	prev = fwd[i];
    }
    if (n == 0) return 0;

    step = (n + maxpts - 1) / maxpts; if (step < 1) step = 1;	// downsample to fit
    np = 0;
    for (i = 0; i < n && np < maxpts; i += step) { xs[np] = px[i]; ys[np] = py[i]; np++; }
    if (np && np < maxpts && (xs[np-1] != px[n-1] || ys[np-1] != py[n-1]))
	{ xs[np] = px[n-1]; ys[np] = py[n-1]; np++; }	// always keep the last (nearest player)

    cache_n = np;
    for (i = 0; i < np; i++) { cache_x[i] = xs[i]; cache_y[i] = ys[i]; }
    return np;
}


// "navdbg" console command: why the buddy is (or isn't) getting to you right now.
// Dumps the two halves of the nav stack separately, because they fail differently:
//   ROUTE  -- the sub-sector graph (PF_*): does a path to you exist at all, and what
//             is the next portal waypoint.
//   STEER  -- the straight-line walk probe (AICoop_ReachProbe): what stops the buddy
//             walking at that waypoint / at you, and the 8 compass headings it would
//             trial-walk instead.  A stairway that reads "drop >24" on every heading
//             is the classic "won't climb, wanders off, wedges at the bottom" case.
//
// Every line goes to the console AND is APPENDED to run/navdbg.txt (cwd is run/, same
// as buddydoom.cfg), so a stuck-buddy report can be read back outside the game instead
// of transcribed off the screen.  Appending keeps a history: dump at several spots
// while the buddy is wedged and compare.

// Relative, like basedefault ("buddydoom.cfg") -- the game's cwd is run/.
#define NAVDBG_FILE	"navdbg.txt"

static FILE*	navdbg_f;		// open only for the duration of one dump
static boolean	navprint_quiet;		// file only, no console (auto-dump from a rescue)

static void NavPrint (const char* fmt, ...)
{
    char	buf[256];
    va_list	ap;

    va_start (ap, fmt);
    vsnprintf (buf, sizeof(buf), fmt, ap);
    va_end (ap);

    if (!navprint_quiet) C_Printf ("%s", buf);
    if (navdbg_f) { fputs (buf, navdbg_f); fputc ('\n', navdbg_f); }
}

static const char* AICoop_VoidReason (mobj_t* mo);	// defined below

static void AICoop_NavDump (void)
{
    static const char*	dirname[8] = { "E ", "NE", "N ", "NW", "W ", "SW", "S ", "SE" };
    mobj_t*	mo = AICoop_Mo ();
    mobj_t*	pl;
    fixed_t	wx, wy, fx, fy;
    int		why, d, ss, gs;
    boolean	ok;

    if (!mo) { NavPrint ("[nav] no companion (launch with -coop / -aicoop)"); return; }
    pl = AICoop_NearestHuman (mo->x, mo->y);
    if (!pl) { NavPrint ("[nav] no human to follow"); return; }

    if (pf_level != gameepisode*100 + gamemap || !pf_cx)
	{ PF_Build (mo); pf_level = gameepisode*100 + gamemap; pf_lastbuild = gametic; }

    ss = PF_SS (mo->x, mo->y);
    gs = PF_SS (pl->x, pl->y);
    {
	const char*	vr = AICoop_VoidReason (mo);
	sector_t*	sec = mo->subsector->sector;
	NavPrint ("[nav] spot: %s   sector f=%d c=%d", vr ? vr : "valid",
		  sec->floorheight>>FRACBITS, sec->ceilingheight>>FRACBITS);
    }
    NavPrint ("[nav] buddy (%d,%d) z=%d floor=%d drop=%d ss=%d edges=%d",
	      mo->x>>FRACBITS, mo->y>>FRACBITS, mo->z>>FRACBITS,
	      mo->floorz>>FRACBITS, mo->dropoffz>>FRACBITS,
	      ss, (ss >= 0 && ss < pf_n) ? pf_nadj[ss] : -1);
    NavPrint ("[nav] you   (%d,%d) floor=%d ss=%d edges=%d  dist=%d",
	      pl->x>>FRACBITS, pl->y>>FRACBITS, pl->floorz>>FRACBITS,
	      gs, (gs >= 0 && gs < pf_n) ? pf_nadj[gs] : -1,
	      (int)(P_AproxDistance (pl->x-mo->x, pl->y-mo->y) >> FRACBITS));

    // Flow field: the shared buddy/monster route-to-the-human wave.
    NavPrint ("[nav] flow  root ss=%d age=%d tics safe=%d  dist here=%s next=%d",
	      pf_flow_player_ss,
	      pf_flow_tic >= 0 ? gametic - pf_flow_tic : -1, pf_flow_safe,
	      (pf_flow_dist && ss >= 0 && ss < pf_n && pf_flow_dist[ss] < PF_INF)
		  ? "ok" : "UNREACHABLE",
	      (pf_flow_next && ss >= 0 && ss < pf_n) ? pf_flow_next[ss] : -1);

    // Every place the search quietly gave up.  Each of these looks exactly like "the
    // human is unreachable" from the outside, so they are worth seeing before blaming
    // the geometry: MAXADJ/inadj = edges dropped at build, MAXPOP = search truncated,
    // PATHMAX = route truncated, locked = locked-door lines past the table's end.
    NavPrint ("[nav] caps  maxadj=%d inadj=%d maxpop=%d pathmax=%d lockedlines=%d",
	      pf_cap_maxadj_cnt, pf_cap_inadj_cnt, pf_cap_maxpop_cnt,
	      pf_cap_pathmax_cnt, pf_cap_lockedlines_cnt);

    // Graph edges leaving the buddy's sub-sector.  A neighbour whose floor is >24
    // ABOVE ours must never be here: that edge is a ledge the buddy cannot climb,
    // and routing over it is what parks it under a wall instead of sending it round
    // via the stairs.
    if (ss >= 0 && ss < pf_n)
    {
	int	k;
	fixed_t	myf = subsectors[ss].sector->floorheight;
	for (k = 0; k < pf_nadj[ss]; k++)
	{
	    int	v  = pf_adj[ss*PF_MAXADJ + k];
	    int	df = (int)((subsectors[v].sector->floorheight - myf) >> FRACBITS);
	    NavPrint ("[nav]   edge -> ss%d floor%+d w=%d portal(%d,%d)%s",
		      v, df, pf_adjw[ss*PF_MAXADJ + k],
		      pf_adjpx[ss*PF_MAXADJ + k]>>FRACBITS,
		      pf_adjpy[ss*PF_MAXADJ + k]>>FRACBITS,
		      df > 24 ? "   <-- UNCLIMBABLE" : "");
	}
    }

    // Breadcrumb chain: which stretch of the human's trail the buddy can actually walk.
    if (crumb_n > 0)
    {
	int	chain0, k, broken = 0;
	crumb_link_tic = -1;			// force a fresh probe for the dump
	AICoop_CrumbRelink (mo);
	chain0 = AICoop_CrumbChainStart ();
	for (k = 0; k + 1 < crumb_n; k++) if (!crumb_link[k]) broken++;
	NavPrint ("[nav] trail on=%d crumbs=%d broken links=%d  connected run=[%d..%d] joins at (%d,%d)",
		  trail_active, crumb_n, broken, chain0, crumb_n-1,
		  crumbx[chain0]>>FRACBITS, crumby[chain0]>>FRACBITS);
    }
    else
	NavPrint ("[nav] trail on=%d crumbs=0", trail_active);

    // ROUTE
    if (PF_NextWaypoint (mo, pl->x, pl->y, &wx, &wy))
    {
	ok = AICoop_ReachProbe (mo, wx, wy, true, &why, &fx, &fy);
	NavPrint ("[nav] ROUTE ok -> waypoint (%d,%d) d=%d  steer=%s%s",
		  wx>>FRACBITS, wy>>FRACBITS,
		  (int)(P_AproxDistance (wx-mo->x, wy-mo->y) >> FRACBITS),
		  ok ? "reachable" : "BLOCKED: ", ok ? "" : AICoop_ReachWhy[why]);
	if (!ok)
	    NavPrint ("[nav]   blocked at (%d,%d)", fx>>FRACBITS, fy>>FRACBITS);
	NavPrint ("[nav] corridor: %d portals, at #%d, goal ss%d, this hop = %s",
		  corr_n, corr_i, corr_goal, corr_next_jump ? "JUMP" : "walk");
    }
    else
	NavPrint ("[nav] ROUTE FAILED -- no graph path to you (%d sub-sectors)", pf_n);

    // STEER: direct line to the human, then every compass heading.
    ok = AICoop_ReachProbe (mo, pl->x, pl->y, false, &why, &fx, &fy);
    NavPrint ("[nav] direct line to you: %s%s  (at %d,%d)",
	      ok ? "clear" : "blocked: ", ok ? "" : AICoop_ReachWhy[why],
	      fx>>FRACBITS, fy>>FRACBITS);

    for (d = 0; d < 8; d += 2)
    {
	char	line[128];
	int	k, n = 0;
	line[0] = 0;
	for (k = d; k < d+2; k++)
	{
	    angle_t	a    = ((angle_t)k * ANG45) >> ANGLETOFINESHIFT;
	    fixed_t	step = mo->radius + 24*FRACUNIT;
	    ok = AICoop_ReachProbe (mo, mo->x + FixedMul (step, finecosine[a]),
				        mo->y + FixedMul (step, finesine[a]), true,
				    &why, &fx, &fy);
	    n += snprintf (line + n, sizeof(line) - n, "  %s=%s",
			   dirname[k], ok ? "walk" : AICoop_ReachWhy[why]);
	}
	NavPrint ("[nav]%s", line);
    }
}

// Open run/navdbg.txt (append), dump, close -- so the file is complete and readable
// the moment the command returns, even if the game later crashes or is killed.
void P_AICoop_NavDebug (void)
{
    navdbg_f = fopen (NAVDBG_FILE, "a");
    if (navdbg_f)
	fprintf (navdbg_f, "\n=== navdbg  map %d.%d  tic %d  leveltime %d ===\n",
		 gameepisode, gamemap, gametic, leveltime);
    else
	C_Printf ("[nav] (could not open %s -- console only)", NAVDBG_FILE);

    AICoop_NavDump ();

    if (navdbg_f)
    {
	fclose (navdbg_f);
	navdbg_f = NULL;
	C_Printf ("[nav] appended to %s", NAVDBG_FILE);
    }
}


// --- Void detection --------------------------------------------------------
// Why the buddy's current spot is one it can never walk out of, or NULL if it's fine.
//
// This used to be AICoop_OnGrid alone -- but that only asks whether (x,y) lies inside
// the BLOCKMAP rectangle, and the blockmap spans the whole map's bounding box.  So it
// catches exactly one case (knocked clean off the edge of the world) and misses every
// void POCKET *inside* that box: a solid filler sector, the space behind a
// self-referencing line, a shut door, a hole the buddy dropped through.  Those are the
// ones you actually see -- the buddy just running along and then gone -- and the
// rescue never fired for any of them.
// A sub-sector is CONVEX and its segs are wound so the interior lies on the front
// side of every one of them.  A point behind any seg is therefore not in that cell at
// all -- it is in the solid space between rooms, which the BSP still has to hand back
// *some* leaf for.  That is the void you actually see: R_PointInSubsector returns a
// perfectly ordinary sub-sector, so its sector's floor and ceiling look normal and
// every "shape" test below passes while the buddy stands inside a wall.
//
// cross < 0 == in front (interior); the tolerance (one seg length ~ 1 unit of
// penetration) keeps a buddy centred exactly on a two-sided boundary from counting.
static boolean AICoop_PointOutside (fixed_t x, fixed_t y)
{
    subsector_t*	ss = R_PointInSubsector (x, y);
    int			i;

    if (!ss || ss->numlines <= 0) return false;
    for (i = 0; i < ss->numlines; i++)
    {
	seg_t*	sg = &segs[ss->firstline + i];
	int	dx = (sg->v2->x - sg->v1->x) >> FRACBITS;
	int	dy = (sg->v2->y - sg->v1->y) >> FRACBITS;
	int	px = (x - sg->v1->x) >> FRACBITS;
	int	py = (y - sg->v1->y) >> FRACBITS;
	int64_t	cross = (int64_t)dx * py - (int64_t)dy * px;
	int	len   = abs (dx) + abs (dy);

	if (cross > (int64_t)len) return true;
    }
    return false;
}

static boolean AICoop_OutsideSubsector (mobj_t* mo)
{
    return AICoop_PointOutside (mo->x, mo->y);
}

static const char* AICoop_VoidReason (mobj_t* mo)
{
    sector_t*	sec;

    if (!AICoop_OnGrid (mo->x, mo->y))
	return "off the blockmap";
    sec = mo->subsector->sector;
    if (sec->ceilingheight - sec->floorheight < mo->height)
	return "sector has no head room (solid/void filler)";
    if (mo->z < mo->floorz - 8*FRACUNIT)
	return "below its own floor";
    if (mo->z > mo->ceilingz)
	return "above the ceiling";
    if (AICoop_OutsideSubsector (mo))
	return "inside the geometry (outside its own BSP leaf)";
    return NULL;
}

// Log a rescue to run/navdbg.txt, so a void event that happened while nobody was
// looking still leaves the coordinates behind.
static void AICoop_VoidLog (mobj_t* mo, const char* why)
{
    FILE*	f = fopen (NAVDBG_FILE, "a");
    sector_t*	sec = mo->subsector->sector;
    int		i, n, first_bad = -1;

    if (!f) return;
    fprintf (f, "\n=== buddy rescue  map %d.%d  tic %d  leveltime %d ===\n"
		"[void] reason: %s\n"
		"[void] at (%d,%d) z=%d floor=%d ceil=%d  sector f=%d c=%d  ongrid=%d\n"
		"[void] recalled to home (%d,%d)\n",
	     gameepisode, gamemap, gametic, leveltime,
	     why ? why : "?",
	     mo->x>>FRACBITS, mo->y>>FRACBITS, mo->z>>FRACBITS,
	     mo->floorz>>FRACBITS, mo->ceilingz>>FRACBITS,
	     sec->floorheight>>FRACBITS, sec->ceilingheight>>FRACBITS,
	     AICoop_OnGrid (mo->x, mo->y) ? 1 : 0,
	     coop_home_x>>FRACBITS, coop_home_y>>FRACBITS);

    // Is this spot one the collision code would even ALLOW?  The distinction decides
    // where to look next: if P_CheckPosition says the buddy's box fits here while it
    // is a unit from a solid wall, clipping itself is wrong.  If it says NO, then
    // nothing moved him here through P_TryMove -- he was PLACED, and the culprit is a
    // caller that skips the line checks (P_TeleportMove does exactly that).
    {
	extern int	pf_ignore_actors;
	boolean		fits;
	pf_ignore_actors = 1;
	fits = P_CheckPosition (mo, mo->x, mo->y);
	pf_ignore_actors = 0;
	fprintf (f, "[void] radius=%d height=%d flags=%s%s  P_CheckPosition(here)=%s\n",
		 mo->radius>>FRACBITS, mo->height>>FRACBITS,
		 (mo->flags & MF_NOCLIP) ? "NOCLIP " : "",
		 (mo->flags & MF_SOLID)  ? "SOLID"   : "!SOLID",
		 fits ? "FITS" : "BLOCKED");
	fprintf (f, "[void]   (only meaningful on an ENTERED line -- once he is deep in\n"
		    "[void]    the void there are no linedefs near his box, so FITS is\n"
		    "[void]    trivially true and says nothing about how he got in.)\n");
    }

    // The 2 s of movement leading in.  Knowing WHERE it ended up only says where the
    // leak comes out; the tic it crossed from a real leaf into solid space says where
    // the leak IS.  Print the transition with a few tics either side.
    n = vtrace_n < VTRACE_MAX ? vtrace_n : VTRACE_MAX;
    for (i = 0; i < n; i++)
    {
	int	k = (vtrace_head - n + i + VTRACE_MAX*2) % VTRACE_MAX;
	if (AICoop_PointOutside (vtrace_x[k], vtrace_y[k])) { first_bad = i; break; }
    }
    if (first_bad < 0)
    {
	fprintf (f, "[void] trace: buddy was INSIDE valid geometry for all %d traced tics\n"
		    "[void]        -> NOT a void event.  It simply could not reach the human\n"
		    "[void]           (blocked route, hazard, or wedged in real geometry).\n"
		    "[void] full nav diagnosis at the moment of the rescue:\n", n);
	// Dump the route/steer state right here.  These rescues fire unattended -- there
	// is nobody at the console to type `navdbg` at the instant the buddy gives up --
	// and without it all the log can say is "it did not get there".
	navdbg_f = f; navprint_quiet = true;
	AICoop_NavDump ();
	navprint_quiet = false; navdbg_f = NULL;
    }
    else
    {
	int lo = first_bad - 12, hi = first_bad + 3;
	if (lo < 0) lo = 0;
	if (hi > n-1) hi = n-1;
	fprintf (f, "[void] trace around the crossing ('*' = in solid space):\n");
	for (i = lo; i <= hi; i++)
	{
	    int	k = (vtrace_head - n + i + VTRACE_MAX*2) % VTRACE_MAX;
	    int	bad = AICoop_PointOutside (vtrace_x[k], vtrace_y[k]);
	    fprintf (f, "[void]  t%+4d %c (%d,%d) z=%d\n", i - n,
		     bad ? '*' : ' ',
		     vtrace_x[k]>>FRACBITS, vtrace_y[k]>>FRACBITS, vtrace_z[k]>>FRACBITS);
	}
    }
    fclose (f);
}


// Move toward a world point using forward+side thrust, so the buddy heads
// straight there *regardless of which way it's facing*.  Pure forwardmove only
// goes where the marine looks, and the slow turn (COOP_TURN) lags -- so while
// turning it walks the wrong way, drifts into walls and snags on corners.
// angleturn still faces the target separately (for aiming/firing).
// Add to the sideways move, SATURATING at run speed.  ticcmd_t.sidemove is a signed
// char and COOP_RUN is 0x32, so a plain `cmd->sidemove += COOP_RUN` on top of a move
// AICoop_ThrustToward already set to +50 reaches 100, and a second one 150 -- which
// wraps to -106: a thrust twice the legal player maximum, in the OPPOSITE direction.
// Both adders (the friendly-fire strafe and the wedged-in-geometry wiggle) can fire in
// the same tic, which is precisely when the buddy is jammed against a wall.
static void AICoop_AddSide (ticcmd_t* cmd, int add)
{
    int	sm = (int)cmd->sidemove + add;

    if (sm >  COOP_RUN) sm =  COOP_RUN;
    else if (sm < -COOP_RUN) sm = -COOP_RUN;
    cmd->sidemove = (signed char)sm;
}

// Is (x,y) probably in a live human's view right now?  A teleport somebody watches happen
// reads as a bug, so the rescue prefers spots nobody is looking at.  The engine has no
// point-based sight test (P_CheckSight wants two mobjs), so approximate it: inside a 90
// degree cone in front of a human and close enough to make out.
static boolean AICoop_LikelySeen (fixed_t x, fixed_t y)
{
    int	i;

    for (i = 0; i < MAXPLAYERS; i++)
    {
	player_t*	p = &players[i];
	angle_t		a;

	if (!playeringame[i] || p->health <= 0 || !p->mo) continue;
	if (P_AICoop_IsBuddy (p))			  continue;
	if (P_AproxDistance (x - p->mo->x, y - p->mo->y) > 1024*FRACUNIT) continue;
	a = R_PointToAngle2 (p->mo->x, p->mo->y, x, y) - p->mo->angle;
	if (a < ANG45 || a > (angle_t)(0 - ANG45)) return true;
    }
    return false;
}

// A spot the buddy may be dropped on.
//
// The player-clearance test is explicit and deliberately does NOT lean on
// P_CheckPosition, which cannot answer this question.  With over_under on -- the default
// -- PIT_CheckThing (p_map.c) replies "fine, rest on its top" whenever the buddy's
// CURRENT z clears the other thing's head, and when this watchdog fires the buddy is
// very often up a ledge or a stairway.  So the probe returned OK for a spot the human
// was standing on.  What follows is P_TeleportMove, and that has no z test whatsoever:
// PIT_StompThing is a flat 2D radius check that deals 10000 damage.  Probe says free,
// stomp says dead.
//
// Mirrors PIT_StompThing's own test exactly, so anything it would stomp is refused here.
static boolean AICoop_RescueSpotOK (mobj_t* mo, fixed_t x, fixed_t y)
{
    int	i;

    if (AICoop_PointOutside (x, y))	return false;
    if (AICoop_DamagingFloor (x, y))	return false;

    for (i = 0; i < MAXPLAYERS; i++)
    {
	player_t*	p = &players[i];
	fixed_t		blockdist;

	if (!playeringame[i] || p->health <= 0 || !p->mo) continue;
	if (p->mo == mo) continue;
	blockdist = p->mo->radius + mo->radius;
	if (abs (p->mo->x - x) < blockdist && abs (p->mo->y - y) < blockdist)
	    return false;
    }

    return P_CheckPosition (mo, x, y) ? true : false;
}

// Last-resort rescue for a buddy that has made no progress toward the human for ~12 s.
// Returns true when it actually moved it.
static boolean AICoop_RescueSmart (mobj_t* mo, mobj_t* pl)
{
    fixed_t	rx = 0, ry = 0;
    angle_t	rang = mo->angle;
    boolean	found = false;
    int		i;

    // (1) Back onto the human's own trail: the NEWEST crumb of the connected run we can be
    // dropped on unseen.  That is ground the human actually walked, so it is reachable and
    // on the way -- the L4D "put the lost survivor back behind the group" move.  It also
    // beats teleporting to the map spawn, which is regularly FURTHER from the human than
    // wherever the buddy already stood.
    if (pl && crumb_n > 0)
    {
	int	chain0;

	AICoop_CrumbRelink (mo);
	chain0 = AICoop_CrumbChainStart ();
	for (i = crumb_n - 1; i >= chain0 && !found; i--)
	    if (!AICoop_LikelySeen (crumbx[i], crumby[i])
		&& AICoop_RescueSpotOK (mo, crumbx[i], crumby[i]))
	    { rx = crumbx[i]; ry = crumby[i]; rang = pl->angle; found = true; }
    }

    // (2) Straight behind the human.
    if (!found && pl && pl->health > 0)
    {
	unsigned	fa = (pl->angle + ANG180) >> ANGLETOFINESHIFT;
	fixed_t		bx = pl->x + FixedMul (96*FRACUNIT, finecosine[fa]);
	fixed_t		by = pl->y + FixedMul (96*FRACUNIT, finesine[fa]);

	if (!AICoop_LikelySeen (bx, by) && AICoop_RescueSpotOK (mo, bx, by))
	{ rx = bx; ry = by; rang = pl->angle; found = true; }
    }

    // (3) The recorded map spawn point, as before.
    if (!found && coop_home_set && AICoop_OnGrid (coop_home_x, coop_home_y)
	&& AICoop_RescueSpotOK (mo, coop_home_x, coop_home_y))
    { rx = coop_home_x; ry = coop_home_y; rang = coop_home_angle; found = true; }

    // Deliberately NO "otherwise, teleport onto the human" fallback.  P_TeleportMove STOMPS
    // whatever occupies the destination, and PIT_StompThing waves a player mobj through --
    // the buddy is one -- so that branch dealt the human 10000 damage and killed them
    // outright.  Staying stuck for another 12 s and retrying is the better failure.
    if (!found) return false;

    // Honour the result.  P_TeleportMove can refuse now -- PIT_StompThing declines to let
    // the buddy telefrag its human -- and reporting a rescue that did not happen would
    // reset the watchdog and buy the buddy another silent 12 s of being lost.
    if (!P_TeleportMove (mo, rx, ry))
	return false;
    mo->angle = rang;
    mo->momx = mo->momy = mo->momz = 0;
    players[consoleplayer].message = "[Buddy] Rescued to player vicinity.";
    return true;
}

static void AICoop_ThrustToward (ticcmd_t* cmd, mobj_t* mo, fixed_t tx, fixed_t ty)
{
    angle_t want = R_PointToAngle2 (mo->x, mo->y, tx, ty);
    angle_t delta = want - mo->angle;
    angle_t rel = delta >> ANGLETOFINESHIFT;
    fixed_t dist = P_AproxDistance (tx - mo->x, ty - mo->y);
    fixed_t speed = COOP_RUN * FRACUNIT;

    // Angular turn error damping: facing more than ~22 degrees off, ease off so the body
    // turns onto the line first instead of powersliding wide around every corner.
    int turn_err = (short)(delta >> 16);
    if (turn_err < 0) turn_err = -turn_err;
    if (turn_err > 4096)
    {
	int scale = 16384 - turn_err;
	if (scale < 4000) scale = 4000;
	speed = FixedMul (speed, (scale << 16) / 16384);
    }

    // Arrival damping: ease off when closing on the waypoint (< 80 units) so we stop on it
    // instead of overshooting, losing the corridor and forcing a re-plan.
    if (dist < 80*FRACUNIT)
    {
	fixed_t damp = FixedDiv (dist, 80*FRACUNIT);
	if (damp < (FRACUNIT / 3)) damp = FRACUNIT / 3;
	speed = FixedMul (speed, damp);
    }

    // FLOOR the result.  The two dampings multiply, so approaching a portal at an angle
    // hit 0.244 * 0.333 ~= 0.08 of run speed -- forwardmove 4.  A player's terminal speed
    // is move/3 units per tic, so that crawls at ~1.3 u/tic, UNDER the 2 u/tic the
    // progress check calls "wedged": the buddy flagged itself stuck at every single
    // waypoint and fired the wiggle (and, with a door on the route, the USE tap) for it.
    // COOP_MINMOVE 12 keeps it at ~4 u/tic, comfortably clear of that.
    if (speed < COOP_MINMOVE*FRACUNIT) speed = COOP_MINMOVE*FRACUNIT;

    cmd->forwardmove =  (signed char)(FixedMul (speed, finecosine[rel]) >> FRACBITS);
    cmd->sidemove    = -(signed char)(FixedMul (speed, finesine[rel])   >> FRACBITS);
}

// Cajun-bot-style missile dodge: if a live projectile is closing on us roughly on a
// collision heading, sidestep perpendicular to it (toward whichever side is walkable).
// Sets the move to the dodge and returns 1; the caller still aims/fires this tic.
static int AICoop_DodgeMissile (ticcmd_t* cmd, mobj_t* mo)
{
    thinker_t*	th;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	m;
	angle_t	mv;
	int	side;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	m = (mobj_t*)th;
	if (!(m->flags & MF_MISSILE)) continue;
	if (m->target == mo) continue;				// our own shot -- ignore
	if (!m->momx && !m->momy) continue;			// not travelling
	if (P_AproxDistance (m->x - mo->x, m->y - mo->y) > COOP_DODGE_RANGE) continue;
	mv = R_PointToAngle2 (0, 0, m->momx, m->momy);		// the missile's heading
	// is it heading at us? compare its heading to the bearing from it to us
	if (abs ((int)(mv - R_PointToAngle2 (m->x, m->y, mo->x, mo->y))) > (int)(ANG45/2))
	    continue;						// >~22 deg off -> it'll miss
	for (side = 0; side < 2; side++)			// step perpendicular, walkable side
	{
	    angle_t perp = mv + (side ? (angle_t)-ANG90 : ANG90);
	    int     a    = perp >> ANGLETOFINESHIFT;
	    fixed_t dx   = mo->x + FixedMul (96*FRACUNIT, finecosine[a]);
	    fixed_t dy   = mo->y + FixedMul (96*FRACUNIT, finesine[a]);
	    if (AICoop_CanReach (mo, dx, dy, false))
	    {
		AICoop_ThrustToward (cmd, mo, dx, dy);
		AICoop_Callout ("dodge:", 3);
		return 1;
	    }
	}
    }
    return 0;
}

// (B) A step toward (gx,gy) the buddy can't WALK up (>24) but a jump clears
// (<=48; JUMPVELOCITY rises ~36u) -- so it presses BT_JUMP onto low ledges, like the
// human can.  P_CheckPosition true == the box fits (it's a tall step, not a wall).
static boolean AICoop_JumpableStep (mobj_t* mo, fixed_t gx, fixed_t gy)
{
    int     a  = R_PointToAngle2 (mo->x, mo->y, gx, gy) >> ANGLETOFINESHIFT;
    fixed_t px = mo->x + FixedMul (24*FRACUNIT, finecosine[a]);
    fixed_t py = mo->y + FixedMul (24*FRACUNIT, finesine[a]);
    fixed_t up;
    if (!P_CheckPosition (mo, px, py)) return false;		// a wall, not a step
    up = tmfloorz - mo->floorz;
    return up > 24*FRACUNIT && up <= 48*FRACUNIT
	&& tmceilingz - tmfloorz >= mo->height;			// fits above the step
}

// (D) Would the buddy's shot at `tgt` detonate an explosive barrel that's dangerously
// close to a SURVIVOR -- the buddy itself OR the human?  Hold fire if so.  Only counts
// barrels the shot could actually hit (roughly in the firing direction toward the target,
// AND in line of sight -- a barrel behind a wall or behind us is harmless), and only when
// the buddy or the player stands within the barrel blast radius +10% (rounded up), so a
// distant barrel near nobody never stops the buddy from shooting the monster.
static boolean AICoop_BarrelNear (mobj_t* mo, mobj_t* tgt)
{
    thinker_t*	th;
    mobj_t*	pl = (playeringame[0] && players[0].mo) ? players[0].mo : NULL;
    angle_t	at = tgt ? R_PointToAngle2 (mo->x, mo->y, tgt->x, tgt->y) : 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t* m;
	fixed_t	db, dp;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	m = (mobj_t*)th;
	if (m->type != MT_BARREL || m->health <= 0) continue;
	// Both the buddy AND the player outside blast+10% -> detonating it harms no one -> ok.
	db = P_AproxDistance (m->x - mo->x, m->y - mo->y);
	dp = pl ? P_AproxDistance (m->x - pl->x, m->y - pl->y) : COOP_BARREL_SAFE;
	if (db >= COOP_BARREL_SAFE && dp >= COOP_BARREL_SAFE) continue;
	if (tgt)
	{
	    angle_t d = R_PointToAngle2 (mo->x, mo->y, m->x, m->y) - at;
	    if (d > ANG90 && d < ANG270) continue;	// >90 deg off the firing line -> won't hit it
	}
	if (!P_CheckSight (mo, m)) continue;		// behind a wall -> our shot can't reach it
	return true;					// our shot could blow it up next to a survivor
    }
    return false;
}

// (E) Does the spot just ahead drop off (>24 below us) or sit on a damaging floor?
// Used to make the buddy creep near nukage pits / ledges instead of running off them.
static boolean AICoop_FallAhead (mobj_t* mo, fixed_t px, fixed_t py)
{
    sector_t* s = R_PointInSubsector (px, py)->sector;
    if (AICoop_DamagingFloor (px, py))             return true;	// nukage/lava ahead
    if (mo->floorz - s->floorheight > 24*FRACUNIT) return true;	// a fall ahead
    return false;
}

// Count alive, killable monsters -- for the "level cleared" callout.
static int AICoop_LiveMonsters (void)
{
    thinker_t* th;
    int        n = 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t* m;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	m = (mobj_t*)th;
	if ((m->flags & MF_COUNTKILL) && m->health > 0) n++;
    }
    return n;
}

// Best ranged weapon the buddy owns with ammo (melee weapons excluded; rockets/BFG
// skipped -- splash would hurt the buddy/human at the ranges it fights).  Used to
// switch off the chainsaw/fist when the target is out of melee reach.
static int AICoop_BestRanged (player_t* p)
{
    static const int	pri[] = { wp_chaingun, wp_supershotgun, wp_shotgun, wp_plasma, wp_pistol };
    int			i;
    for (i = 0; i < 5; i++)
    {
	int		w = pri[i];
	ammotype_t	a;
	if (!p->weaponowned[w]) continue;
	a = weaponinfo[w].ammo;
	if (a == am_noammo || p->ammo[a] > 0) return w;
    }
    return -1;
}

// Feet-trace steering -- the "item reachability" trick (AICoop_CanReach) applied
// to movement.  Aim at the goal; if the straight floor-trace is blocked, sweep
// the heading outward in 22.5° steps -- the *full* circle (±180°), so it can also
// turn toward a door beside or behind it -- and steer toward the first clear
// direction.  If nothing toward the goal is clear, head to the centre of the
// current room (subsector centroid) to get off the wall, then re-evaluate next
// tic (this is the "go to the middle of the room first, then path" behaviour).
static void AICoop_TraceSteer (mobj_t* mo, fixed_t gx, fixed_t gy, fixed_t* sx, fixed_t* sy)
{
    static const int seq[16] = { 0,1,-1,2,-2,3,-3,4,-4,5,-5,6,-6,7,-7,8 };  // *22.5°
    angle_t	base  = R_PointToAngle2 (mo->x, mo->y, gx, gy);
    fixed_t	gdist = P_AproxDistance (gx - mo->x, gy - mo->y);
    fixed_t	probe = (gdist < 192*FRACUNIT) ? gdist : 192*FRACUNIT;
    int		i, ss;

    if (AICoop_CanReach (mo, gx, gy, true)) { *sx = gx; *sy = gy; return; }
    for (i = 0 ; i < 16 ; i++)
    {
	angle_t a  = (base + (angle_t)seq[i] * (ANG45/2)) >> ANGLETOFINESHIFT;
	fixed_t px = mo->x + FixedMul (probe, finecosine[a]);
	fixed_t py = mo->y + FixedMul (probe, finesine[a]);
	if (AICoop_CanReach (mo, px, py, true)) { *sx = px; *sy = py; return; }
    }
    // Nothing clear toward the goal -> aim for the centre of our own room.
    ss = PF_SS (mo->x, mo->y);
    if (pf_cx && ss >= 0 && ss < pf_n) { *sx = pf_cx[ss]; *sy = pf_cy[ss]; return; }
    *sx = gx; *sy = gy;
}

// Guard against fixating on a pickup the buddy can reach horizontally but never
// actually collect (e.g. it sits a little above on a ledge/pedestal -> the buddy
// oscillates on it forever, as seen stuck in an E1M2 secret).  If we've targeted
// the same item for >2s without grabbing it, blacklist it for a few seconds so the
// buddy gives up and resumes following.
static boolean AICoop_GrabStuck (mobj_t* item)
{
    static mobj_t*	cur;
    static int		started;
    static mobj_t*	skip;
    static int		skipuntil;

    if (item == skip && gametic < skipuntil)	return true;
    if (item != cur) { cur = item; started = gametic; }
    if (gametic - started > 2*TICRATE)
    { skip = item; skipuntil = gametic + 5*TICRATE; return true; }
    return false;
}

// Stand the downed buddy back up in place (L4D revive) with `hp` health -- undoes
// P_KillMobj on its own mobj instead of reborning at a player start, so it gets up
// where it fell.
static void P_AICoop_Revive (int hp)
{
    player_t*	bot = &players[coop_slot];
    mobj_t*	mo  = bot->mo;
    if (!mo) return;
    mo->flags |= (MF_SOLID | MF_SHOOTABLE);
    mo->flags &= ~MF_CORPSE;			// un-corpse; KEEP MF_DROPOFF (the buddy is a
						// player -- it must still drop off ledges to follow you)
    mo->height = mo->info->height;		// un-squash the corpse
    mo->health = hp;
    bot->health = hp;
    bot->playerstate = PST_LIVE;
    bot->damagecount = 0;
    bot->attacker = NULL;
    bot->viewheight = VIEWHEIGHT;
    bot->deltaviewheight = 0;
    P_SetMobjState (mo, mo->info->spawnstate);	// stand up (S_PLAY)
    bot->pendingweapon = bot->readyweapon;	// raise the weapon again

    // The reviver had to stand within 96u of the DOWNED corpse, which isn't solid -- so
    // the human is often standing right on top of it.  Now that the buddy is solid again
    // they overlap and both wedge (two solids can't separate).  If the buddy stood up
    // inside something, shove it to the nearest free spot (away from whatever it hit).
    if (!P_CheckPosition (mo, mo->x, mo->y))
    {
	extern boolean P_TryMove (mobj_t* thing, fixed_t x, fixed_t y);
	static const int ox[8] = { 1, 1, 0, -1, -1, -1,  0,  1 };
	static const int oy[8] = { 0, 1, 1,  1,  0, -1, -1, -1 };
	fixed_t step = mo->radius * 2 + 8*FRACUNIT;	// clear of the reviver (r_buddy + r_human)
	int k;
	for (k = 0; k < 8; k++)
	    if (P_TryMove (mo, mo->x + ox[k]*step, mo->y + oy[k]*step))
		break;
    }

    AICoop_Callout ("revived:", 3);
}

// Human pressed USE: if standing over the downed buddy, spend a Stimpack or Medikit from the
// human's inventory to bring it back into the fight (no item -> a message, no revive).  Returns
// true if the press was consumed (so the caller doesn't also open a door with the same press).
boolean P_AICoop_RevivePress (player_t* presser)
{
    player_t*	bot;
    mobj_t*	dmo;
    if (!companion_active || coop_slot < 0) return false;
    bot = &players[coop_slot];
    if (presser == bot || bot->playerstate != PST_DEAD) return false;
    dmo = bot->mo;
    if (!dmo || !presser->mo) return false;
    if (P_AproxDistance (presser->mo->x - dmo->x, presser->mo->y - dmo->y) >= COOP_REVIVE_RANGE)
	return false;
    // Line-of-sight so you can't revive through a closed door/wall -- but check it
    // against the buddy's FULL STANDING height, not the squashed corpse.  P_CheckSight
    // seeds its slope window from (t2->z .. t2->z+t2->height); a floor-level downed
    // corpse gives a near-zero window that any step/opening in complex (LoR) geometry
    // closes, so sight failed even point-blank and the USE silently fell through to a
    // door.  Restoring the living height makes LOS behave as it did to the marine you
    // were just following.
    {
	fixed_t	savedh = dmo->height;
	boolean	seen;
	dmo->height = dmo->info->height;
	seen = P_CheckSight (presser->mo, dmo);
	dmo->height = savedh;
	if (!seen) return false;
    }
    // Reviving costs a health artifact from the human's pack, and the buddy comes back up on
    // that item's heal value.  `false` = spend the SMALLEST one available, so the human's
    // medikit survives the favour.  The candidate list is shared with the human's own
    // self-revive (reviveitems[] in p_invent.c) so the two can't drift apart again.
    // With nothing that can pay, the buddy can't be revived: say so and bail.
    artitype_t	cost;
    int		reviveHP;
    if (!P_ReviveItemPick (presser, false, &cost, &reviveHP))
    {
	presser->message = "NEED A HEALTH ITEM TO REVIVE YOUR BUDDY";
	return false;
    }
    presser->inventory[cost]--;			// spend it
    if (presser->invslot == cost && presser->inventory[cost] <= 0)
	P_InvScroll (presser, +1);		// emptied the shown slot -> advance to another
    P_AICoop_Revive (reviveHP);			// buddy back up on the spent stimpack/medikit
    // Thank the human, reliably: VP_COMMAND preempts the rate-limited "help!" screams the
    // downed buddy was making (an ambient "revived" line would be gated out right after them).
    AICoop_CalloutP ("thanks:", 6, VP_COMMAND);
    return true;
}

// Console "buddyheal": patch the companion up to 100 HP -- and stand it up first if it's
// currently downed (so it doubles as a remote revive).
const char* P_AICoop_Heal (void)
{
    player_t* bot = &players[coop_slot];
    if (!AICoop_Mo ()) return "[Buddy] (no companion -- launch with -coop)";
    if (bot->playerstate == PST_DEAD)
    {
	P_AICoop_Revive (100);			// downed -> stand it back up at full health
	return "[Buddy] Revived and patched up to 100 HP.";
    }
    bot->health = 100;
    if (bot->mo) bot->mo->health = 100;
    return "[Buddy] Patched up to 100 HP.";
}

// (auto-heal) When the buddy is hurt, spend a held HEALTH artifact -- instant, and it beats
// hunting around the level for a med-pack.  One per call (called each tic while hurt), in a
// value order that doesn't burn the big Mystic Urn or the trivial +1 health bonuses first.
// P_UseArtifact applies the effect + consumes one, and refuses (no-op) at full health.
//
// Separate from reviveitems[] in p_invent.c on purpose -- this one HEALS rather than paying
// for a revive, so it also carries the +1 bonus, which is no use standing anyone back up.
// Keep the two in step when a new health artifact appears.
extern boolean P_UseArtifact (player_t* player, artitype_t which);
static void AICoop_AutoHeal (player_t* bot)
{
    static const artitype_t healers[] =
	{ arti_medikit, h_arti_flask, s_arti_medkit,		// 25 HP
	  arti_stimpack, s_arti_medpatch,			// 10 HP
	  h_arti_urn, s_arti_stamina,				// full heal -- late
	  arti_healthbonus };					// +1 -- last
    int i;
    for (i = 0; i < (int)(sizeof(healers)/sizeof(healers[0])); i++)
	if (bot->inventory[healers[i]] > 0 && P_UseArtifact (bot, healers[i]))
	    return;			// used one this tic
}

static boolean AICoop_PlayerInLine (mobj_t* mo, mobj_t* tgt)
{
    int i;
    for (i = 0; i < MAXPLAYERS; i++)
    {
	mobj_t* pl;
	int txi, tyi, pxi, pyi, dti, perp;
	if (i == coop_slot || !playeringame[i]) continue;
	if (players[i].playerstate != PST_LIVE || !players[i].mo) continue;
	pl = players[i].mo;
	txi = (tgt->x - mo->x) >> FRACBITS; tyi = (tgt->y - mo->y) >> FRACBITS;
	pxi = (pl->x  - mo->x) >> FRACBITS; pyi = (pl->y  - mo->y) >> FRACBITS;
	dti = P_AproxDistance (tgt->x - mo->x, tgt->y - mo->y) >> FRACBITS;
	if (dti < 1)                         continue;
	if (pxi*txi + pyi*tyi <= 0)          continue;	// player is behind/beside the buddy
	if (pxi*pxi + pyi*pyi >= dti*dti)    continue;	// player is farther than the target
	perp = abs (pxi*tyi - pyi*txi) / dti;		// player's distance from the shot line
	if (perp < 40) return true;
    }
    return false;
}

void P_AICoop_BuildCmd (void)
{
    player_t*	bot;
    mobj_t*	mo;
    mobj_t*	tgt;
    mobj_t*	heal;
    mobj_t*	item;
    mobj_t*	pl;
    mobj_t*	aimmon = NULL;		// the monster we're firing at (for the sight test)
    ticcmd_t*	cmd;
    angle_t	want, delta;
    fixed_t	tx = 0, ty = 0, stx, sty, dist;
    fixed_t	movethresh = -1;	// move when dist > this; -1 = stand still
    int		rem, turn, haveaim = 0, fire = 0, avoiddamage = 0, navigate = 0;
    boolean	stuck, backoff = false;	// backoff: can't hit the target, retreat to open the angle
    boolean	stayclose = false;	// hurt or come-leashed -> keep near the player, fight only near threats
    boolean	leash_return = false;	// come-leash: too far / no LOS -> return to the player
    static fixed_t lastx, lasty;	// where we were last tic (progress check)
    static int	doorwait, triedmove;	// door pulse cooldown / did we try to move
    static int	navtimer;		// pathfinder re-path cooldown
    static fixed_t navwx, navwy;	// cached waypoint
    static boolean navok;
    static int	navjump;		// the cached waypoint is reached by JUMPING
    static mobj_t* dmg_mon;		// monster the damage-watchdog is tracking
    static int	dmg_hp0, dmg_firetics;	// its health at baseline / tics fired with no drop

    if (!companion_active || !playeringame[coop_slot])
	return;

    bot = &players[coop_slot];
    cmd = &bot->cmd;

    // Keep the marine buddy tinted to the chosen player colour (Buddy menu / config).
    // Refreshed every tic so a live colour change (or reborn) takes effect immediately;
    // applies in every state (incl. downed), so do it before the down-state handling.
    if (bot->mo)
    {
	R_SetBuddyColor (bot->mo, V_BuddyColorTable (buddy_color));
	// (buddy) skin: render player 2's body as the selected BUDDYDEF buddy (slot 0 =
	// Marine keeps the stock PLAY body).  -1 clears the override.
	R_SetBuddySkin (bot->mo, buddy_select > 0 ? P_Buddy_Sprite (buddy_select) : -1);
    }

    // Down (L4D-style incapacitation): NOT game over -- the buddy lies on the ground
    // (its corpse) and calls for help.  We clear the cmd (never tap USE, which would
    // reborn it at a start) so it stays put; the human revives it by standing close
    // for a few seconds, and it gets back up where it fell.
    if (bot->playerstate == PST_DEAD)
    {
	// If the downed body fell on (or slid into) a damaging floor -- nukage/lava/etc. --
	// the human can't safely reach it to revive and it would just keep cooking until it
	// dies for good.  So recall it to the recorded spawn point AND stand it straight back
	// up at full health -- P_AICoop_Revive un-downs it (100 HP) and nudges it clear.
	if (bot->mo && coop_home_set && AICoop_DamagingFloor (bot->mo->x, bot->mo->y))
	{
	    P_TeleportMove (bot->mo, coop_home_x, coop_home_y);
	    bot->mo->momx = bot->mo->momy = bot->mo->momz = 0;
	    P_AICoop_Revive (100);		// patched up to 100 HP, back on its feet at home
	    return;
	}
	memset (cmd, 0, sizeof(*cmd));		// never tap USE -> stays down (no reborn)
	AICoop_Callout ("help:", 8);		// scream for help (4s cooldown gates it)
	return;					// revived by the human's USE (P_AICoop_RevivePress)
    }
    if (bot->playerstate != PST_LIVE || !bot->mo)
	return;

    mo = bot->mo;
    memset (cmd, 0, sizeof(*cmd));

    // Position trace (ring, ~2 s) -- recorded before anything can move or recall it.
    vtrace_x[vtrace_head] = mo->x;
    vtrace_y[vtrace_head] = mo->y;
    vtrace_z[vtrace_head] = mo->z;
    vtrace_head = (vtrace_head + 1) % VTRACE_MAX;
    if (vtrace_n < VTRACE_MAX) vtrace_n++;

    // Log the CROSSING, not the aftermath.  A rescue only fires once the buddy has
    // been somewhere invalid for a while -- by then the 2 s ring no longer holds the
    // approach, which is why the last capture could only say "never inside a real
    // leaf in the last 70 tics".  Catch the exact tic it first leaves a real BSP
    // leaf, while the ring still shows how it got there.
    {
	boolean	now_outside = AICoop_PointOutside (mo->x, mo->y);
	if (now_outside && !void_was_outside)
	    AICoop_VoidLog (mo, "ENTERED solid space -- crossing below");
	void_was_outside = now_outside;
    }

    // Void rescue.  Checked every 5 tics; AICoop_VoidReason is a handful of integer ops
    // (no blockmap/line walk), so it's practically free.  Only recall if HOME is itself
    // a good spot, else re-teleporting to a bad spawn would just loop.  (Mirrors the
    // damaging-floor rescue below.)
    {
	static int		voidtics;	// consecutive 5-tic checks in a bad spot
	static const char*	voidwhy;

	if ((gametic % 5) == 0)
	{
	    const char* vr = AICoop_VoidReason (mo);
	    if (vr) { voidwhy = vr; voidtics++; }
	    else    { voidwhy = NULL; voidtics = 0; }
	}
	// ~1 s of being continuously nowhere valid.  The delay filters the transients --
	// a door closing over its head, a lift it is riding -- which are not the void.
	if (voidtics >= 7 && coop_home_set && AICoop_OnGrid (coop_home_x, coop_home_y))
	{
	    AICoop_VoidLog (mo, voidwhy);
	    P_TeleportMove (mo, coop_home_x, coop_home_y);
	    mo->angle = coop_home_angle;
	    mo->momx = mo->momy = mo->momz = 0;
	    voidtics = 0; voidwhy = NULL;
	    players[consoleplayer].message = "[Buddy] Recovered from the void.";
	    return;				// re-evaluate cleanly next tic (cmd stays zeroed)
	}
    }

    // When surrounded by many enemies, the buddy deploys a friendly Security Drone
    // (costs it 50 bullets or 25 shells; throttled + capped inside).  files/p_secdrone.c.
    P_AICoop_MaybeSpawnDrone (bot);

    // The buddy also revives a nearby Dead Marine when it can afford the field surgery
    // (>=1 medikit + >=2 stimpacks from its own pack).  ~3x/sec is plenty.  files/revmarine.c.
    if ((gametic & 15) == 0)
	RevMarine_BuddyTryRevive (bot);

    // Progress check.  "stuck" if we tried to move but either barely moved this tic
    // (wedged solid) OR made no NET progress over a ~0.4s window -- the latter
    // catches oscillating in place in front of a closed door (per-tic it's moving
    // side to side, so the old check never flagged it and the door never got Used).
    {
	static fixed_t	winx, winy;
	static int	wintic;
	static boolean	oscillating;
	if (gametic - wintic >= 14)
	{
	    if (wintic) oscillating = P_AproxDistance (mo->x-winx, mo->y-winy) < 40*FRACUNIT;
	    winx = mo->x; winy = mo->y; wintic = gametic;
	}
	stuck = triedmove
	     && (P_AproxDistance (mo->x - lastx, mo->y - lasty) < 2*FRACUNIT || oscillating);
    }
    lastx = mo->x; lasty = mo->y;
    if (doorwait > 0) doorwait--;

    pl = AICoop_NearestHuman (mo->x, mo->y);

    // Breadcrumb trail upkeep + long-horizon "stuck reaching the player" watchdog.
    if (pl)
    {
	fixed_t	pld = P_AproxDistance (mo->x - pl->x, mo->y - pl->y);

	AICoop_CrumbAdd (pl->x, pl->y);

	// The whole watchdog used to sit inside `if (getenv("BUDDYDBG") && ...)`, so
	// with the debug env var unset -- i.e. always -- noprog never counted and
	// trail_active was never raised.  The breadcrumb fallback and every recovery
	// hanging off it were dead code in normal play.  The debug PRINT is opt-in;
	// the watchdog is not.
	if (gametic - prog_tic >= 35)			// re-check ~1x/s
	{
	    prog_tic = gametic;
	    if (buddy_dbg < 0) buddy_dbg = getenv ("BUDDYDBG") ? 1 : 0;
	    if (buddy_dbg)
	    {
		printf("Buddy Debug: pos=[%d,%d] pld=%d state=%d trail=%d stuck=%d\n",
		       mo->x>>FRACBITS, mo->y>>FRACBITS, pld>>FRACBITS, coop_state, trail_active, stuck);
		fflush(stdout);
	    }
	    // Progress = reaching a NEW minimum distance.  Tracking the best (not the
	    // last) defeats jitter: oscillating in place bounces pld ~+/-100u, which the
	    // old "closer than last second" test mistook for progress, so the watchdog
	    // never tripped and the trail/fallback never kicked in.
	    if (pld < best_pld - 32*FRACUNIT) { best_pld = pld; noprog = 0; }
	    else                                noprog++;
	    // "Arrived" needs a WALKABLE line to the human, not just a short one.  Straight
	    // -line distance alone called it arrived while it stood on the far side of a
	    // wall 168 units away -- which reset noprog every second, so the trail never
	    // engaged and no rescue could ever trip either.
	    if (pld <= COOP_NEAR && AICoop_CanReach (mo, pl->x, pl->y, false))
		{ best_pld = pld; noprog = 0; trail_active = 0; }
	    else if (noprog >= 3)  trail_active = 1;		// ~3 s no closer -> trail/fallback
	    else if (noprog == 0)  trail_active = 0;		// gaining -> normal nav

	    // Last-resort rescue, by BEHAVIOUR rather than by shape.  AICoop_VoidReason
	    // only recognises the voids we thought to describe; this catches everything
	    // else -- a void pocket that looks like an ordinary sector, wedged geometry,
	    // a sealed room -- by noticing that ~12 s of trying has produced no new best
	    // distance to the human.  Gated to following/coming so a long firefight (or a
	    // "wait" order) is never mistaken for being stuck.
	    // Only clear the counters when the rescue actually MOVED it.  It can decline now
	    // (every candidate spot occupied, hazardous, or in plain view of the human), and
	    // resetting on a declined attempt would buy another silent 12 s of nothing.
	    if (noprog >= 12 && !user_hold
		&& (coop_state == 0 || coop_state == 4)
		&& AICoop_RescueSmart (mo, pl))
	    {
		AICoop_VoidLog (mo, "no progress toward the human for 12 s");
		best_pld = 0x7fffffff; noprog = 0; trail_active = 0;
	    }
	}
	else if (pld <= COOP_NEAR && trail_active
		 && AICoop_CanReach (mo, pl->x, pl->y, false))
	    trail_active = 0;					// reached the player for real
	if (pld < best_pld) best_pld = pld;			// keep the running minimum fresh
    }
    else
	trail_active = 0;

    // Yield (top priority): the human is bumping into us -> get out of the way by
    // stepping straight away from them.  Use forward+side move so we slide aside
    // immediately instead of slowly turning around (which keeps blocking).
    if (pl)
    {
	fixed_t yield_d = P_AproxDistance (pl->x - mo->x, pl->y - mo->y);
	if (yield_d < YIELD_DIST)
	{
	    angle_t base_ang = R_PointToAngle2 (pl->x, pl->y, mo->x, mo->y);
	    fixed_t step = 32*FRACUNIT;
	    angle_t choose_ang = base_ang;
	    boolean choose_set = false;
	    

	    
	    // Prefer stepping sideways (left 90 deg or right -90 deg) to clear the path
	    // Try stepping left (90 deg)
	    fixed_t tx = mo->x + FixedMul (step, finecosine[(base_ang + ANG90) >> ANGLETOFINESHIFT]);
	    fixed_t ty = mo->y + FixedMul (step, finesine[(base_ang + ANG90) >> ANGLETOFINESHIFT]);
	    if (AICoop_CanReach (mo, tx, ty, false))
	    {
		choose_ang = base_ang + ANG90;
		choose_set = true;
	    }
	    else
	    {
		// Try stepping right (-90 deg)
		tx = mo->x + FixedMul (step, finecosine[(base_ang - ANG90) >> ANGLETOFINESHIFT]);
		ty = mo->y + FixedMul (step, finesine[(base_ang - ANG90) >> ANGLETOFINESHIFT]);
		if (AICoop_CanReach (mo, tx, ty, false))
		{
		    choose_ang = base_ang - ANG90;
		    choose_set = true;
		}
	    }

	    if (!choose_set)
	    {
		// Fallback: step directly away from the player
		tx = mo->x + FixedMul (step, finecosine[base_ang >> ANGLETOFINESHIFT]);
		ty = mo->y + FixedMul (step, finesine[base_ang >> ANGLETOFINESHIFT]);
		if (AICoop_CanReach (mo, tx, ty, false))
		{
		    choose_ang = base_ang;
		    choose_set = true;
		}
	    }

	    unsigned fa = (choose_ang - mo->angle) >> ANGLETOFINESHIFT;
	    cmd->forwardmove =  (signed char)(FixedMul (COOP_RUN*FRACUNIT, finecosine[fa]) >> FRACBITS);
	    cmd->sidemove    = -(signed char)(FixedMul (COOP_RUN*FRACUNIT, finesine[fa])   >> FRACBITS);
	    triedmove  = 1;
	    coop_state = 0;
	    return;
	}
    }

    tgt  = AICoop_FindTarget (mo);

    // (buddy) an alternative buddy speaks with its own voice: a "see" bark on a fresh
    // target, and an idle "active" grunt now and then when nothing's around.
    {
	static boolean	bd_hadtgt = false;
	int		s;
	if (tgt && !bd_hadtgt && (s = P_Buddy_BodySfx (mo, BUDDYSND_SEE)) >= 0)
	    S_StartSound (mo, s);
	else if (!tgt && !(gametic % (7*TICRATE))
		 && (s = P_Buddy_BodySfx (mo, BUDDYSND_ACTIVE)) >= 0)
	    S_StartSound (mo, s);
	bd_hadtgt = (tgt != NULL);
    }

    if (bot->health < COOP_HEAL_HP)
	AICoop_AutoHeal (bot);		// spend a held heal-artifact first (instant; beats hunting a med-pack)
    heal = (bot->health < COOP_HEAL_HP) ? AICoop_FindHealth (mo) : NULL;

    // "Stay close" to the player when hurt (<50% HP -> cautious, less kamikaze) OR
    // when the human ordered "come" (summon_stay leash).  In this mode the buddy
    // sticks near the player, keeps line of sight, and only fights threats that are
    // near it or near the player instead of charging off after distant monsters.
    // Explicit orders (attack/goto/wait) clear summon_stay and take priority below.
    {
	fixed_t plr_d   = pl ? P_AproxDistance (mo->x - pl->x, mo->y - pl->y) : 0;
	boolean cautious = (bot->health < COOP_CAUTION_HP);
	stayclose   = (cautious || summon_stay) && pl;
	// hard leash (come-stay only): strayed past the leash or lost sight -> return.
	leash_return = summon_stay && pl && (plr_d > COOP_LEASH || !P_CheckSight (mo, pl));
    }

    // Voice: automatic callouts (rate-limited; tags -> OGG lumps in buddydoom.wad).
        {
    	static mobj_t*	lasttgt;
    	static int	lasthp = 100, lastplhp = 100, lastlevel = -1, lastdry = 0, lastfist = 0;
    	player_t*	human = &players[0];
    	int		lvl = gameepisode*100 + gamemap;
    	int		w = bot->readyweapon;
    	int		curammo = (weaponinfo[w].ammo < NUMAMMO) ? bot->ammo[weaponinfo[w].ammo] : 1;

    	if (lvl != lastlevel) { if (lastlevel >= 0) AICoop_Callout ("lvlstart:", 3); lastlevel = lvl; }

    	if (tgt && !lasttgt)				AICoop_Callout ("contact:", 4);
    	else if (!tgt && lasttgt)			AICoop_Callout ("clear:",   3);

    	// A heavy hitter newly in our sights -> a wary callout.
    	if (tgt && tgt != lasttgt &&
    	    (tgt->type==MT_BRUISER||tgt->type==MT_KNIGHT||tgt->type==MT_HEAD||tgt->type==MT_CYBORG
    	     ||tgt->type==MT_SPIDER||tgt->type==MT_BABY||tgt->type==MT_FATSO||tgt->type==MT_VILE
    	     ||tgt->type==MT_PAIN))			AICoop_Callout ("bigmon:", 3);

    	if (bot->health < COOP_HEAL_HP && lasthp >= COOP_HEAL_HP) AICoop_Callout ("hurt:", 3);
    	if (bot->health < 20 && lasthp >= 20)		AICoop_Callout ("crit:", 3);
    	if (bot->health >= COOP_HEAL_HP && lasthp < COOP_HEAL_HP) AICoop_Callout ("healed:", 2);

    	{ int dry = (curammo <= 0); if (dry && !lastdry) AICoop_CalloutP ("dry:", 3, VP_WEAPON); lastdry = dry; }
    	{ int onlyfist = (bot->readyweapon == wp_fist && AICoop_BestRanged (bot) < 0);
    	  if (onlyfist && !lastfist) AICoop_CalloutP ("fists:", 2, VP_WEAPON); lastfist = onlyfist; }

    	if (human->mo) {
    	    if (human->health > 0 && human->health < 35 && lastplhp >= 35) AICoop_Callout ("plhurt:", 3);
    	    if (human->health <= 0 && lastplhp > 0)		AICoop_Callout ("pldown:", 2);
    	    lastplhp = human->health;
    	}

    	lasttgt = tgt; lasthp = bot->health;
        }

    // Once/sec: "all clear -- level done" when the last monster drops, plus idle
    // banter after a long lull with nothing to shoot.
    if ((gametic % 35) == 0)
    {
	static int	lastlive = -1, idletic;
	int		live = AICoop_LiveMonsters ();
	if (lastlive > 0 && live == 0)			AICoop_Callout ("lvlclear:", 3);
	lastlive = live;
	if (tgt) idletic = gametic;
	else if (gametic - idletic > 25*TICRATE)	{ AICoop_Callout ("idle:", 4); idletic = gametic; }
    }

    // Announce a newly picked-up weapon ("Buddy: got the shotgun!") -- text to the
    // human + a voice line (reusing the status:<weapon> phrase that names it).
    {
	static const char* wname[NUMWEAPONS] =
	    { "fists","pistol","shotgun","chaingun","rocket launcher",
	      "plasma rifle","BFG9000","chainsaw","super shotgun" };
	static const char* wtag[NUMWEAPONS] =
	    { "status:fists","status:pistol","status:shotgun","status:chaingun",
	      "status:rocketlauncher","status:plasma","status:bfg","status:chainsaw",
	      "status:supershotgun" };
	static int  ownedmask;
	static char gotmsg[64];
	int newmask = 0, wi;
	for (wi = 0 ; wi < NUMWEAPONS ; wi++)
	    if (bot->weaponowned[wi]) newmask |= (1 << wi);
	if (ownedmask && (newmask & ~ownedmask))		// skip the spawn loadout
	    for (wi = 0 ; wi < NUMWEAPONS ; wi++)
		if ((newmask & ~ownedmask) & (1 << wi))
		{
		    AICoop_SayTagP (wtag[wi], VP_WEAPON);
		    snprintf (gotmsg, sizeof(gotmsg), "Buddy: got the %s!", wname[wi]);
		    if (playeringame[displayplayer]) players[displayplayer].message = gotmsg;
		    break;
		}
	ownedmask = newmask;
    }

    if (summon > 0)     summon--;
    if (forceaggro > 0) forceaggro--;
    if (ai_goto > 0)    ai_goto--;

    // "come" ends as soon as the buddy has reached the player (don't run the
    // whole timer once it's already next to you).
    if (summon > 0 && pl && P_AproxDistance (pl->x - mo->x, pl->y - mo->y) <= COOP_NEAR/2)
	summon = 0;

    coop_state = 0;

    // (attack) ordered: charge the forced target until it (or the timer) dies
    if (forceaggro > 0)
    {
	if (!forcetarget || forcetarget->health <= 0
	    || (forcetarget->flags & MF_CORPSE) || !(forcetarget->flags & MF_SHOOTABLE))
	    forcetarget = AICoop_FindTarget (mo);		// reacquire
	if (forcetarget)
	{
	    coop_state = 1; haveaim = 1; fire = 1; aimmon = forcetarget;
	    movethresh = COOP_KEEP;
	    tx = forcetarget->x; ty = forcetarget->y;
	}
	else
	    forceaggro = 0;
    }

    if (!haveaim)
    {
	// (LLM goto) ordered: move to a point, ignoring fights/items
	if (ai_goto > 0)
	{
	    coop_state = 4; haveaim = 1; movethresh = 24*FRACUNIT; navigate = 1;
	    tx = ai_gx; ty = ai_gy;
	}
	// (wait/stay) ordered: hold position; still face & fire at a monster.
	// user_hold = the human's sticky "stay" (overrides the director); hold =
	// the director's transient BUD_HOLD tactic.
	else if (user_hold || hold)
	{
	    coop_state = 3;
	    if (tgt) { coop_state = 1; haveaim = 1; fire = 1; aimmon = tgt; tx = tgt->x; ty = tgt->y; }
	    // else: no aim -> stand still
	}
	// (come) ordered: run to the player, ignoring fights/items
	else if (summon > 0 && pl)
	{
	    coop_state = 4; haveaim = 1; movethresh = COOP_NEAR/2; navigate = 1;
	    tx = pl->x; ty = pl->y;
	}
	// (come-stay leash) keep near the player with line of sight: strayed past the
	// leash or lost sight of them -> return, overriding fights/items.
	else if (leash_return)
	{
	    coop_state = 4; haveaim = 1; movethresh = COOP_NEAR; navigate = 1;
	    tx = pl->x; ty = pl->y;
	}
	// hurt: break off and grab the nearest med-pack
	else if (heal)
	{
	    coop_state = 2; haveaim = 1; movethresh = 16*FRACUNIT;
	    tx = heal->x; ty = heal->y;
	}
	// fight the nearest monster -- but while staying close (hurt / come-leash) only
	// engage threats near us or near the player, and keep extra distance (no charge).
	else if (tgt)
	{
	    coop_state = 1; haveaim = 1; fire = 1; aimmon = tgt;
	    movethresh = stayclose ? COOP_KEEP*2 : COOP_KEEP;
	    tx = tgt->x; ty = tgt->y;
	    avoiddamage = 1;		// don't charge into nukage/lava chasing a monster
	}
	// idle: collect a nearby item, but ONLY while still near the human (don't
	// wander off / linger for an item while the player walks away), and not one
	// we've been failing to grab (AICoop_GrabStuck) -- else follow the human.
	else if (pl
		 && P_AproxDistance (pl->x - mo->x, pl->y - mo->y) < COOP_GRAB_NEAR
		 && (item = AICoop_FindItem (mo)) != NULL
		 && !AICoop_GrabStuck (item))
	{
	    coop_state = 5; haveaim = 1; movethresh = 16*FRACUNIT;
	    tx = item->x; ty = item->y;
	}
	else if (pl)
	{
	    // follow -- stick closer when hurt / come-leashed.
	    coop_state = 0; haveaim = 1;
	    movethresh = stayclose ? COOP_NEAR/2 : COOP_NEAR;
	    avoiddamage = 1; navigate = 1;
	    tx = pl->x; ty = pl->y;
	}
    }

    if (!haveaim)
	{ triedmove = 0; return; }		// nothing to do -> stand still

    // Breadcrumb override: when stuck reaching the player, replay the human's trail.
    // The crumbs are LINKED into a chain first (AICoop_CrumbRelink): only the run of
    // crumbs joined to the human by links the buddy can actually walk is usable -- the
    // rest sit on the far side of a jump/fall/teleport it cannot repeat.
    int chase_player = 0;
    if (trail_active && pl && (coop_state == 0 || coop_state == 4))
    {
	int i, used = 0, chain0;

	AICoop_CrumbRelink (mo);
	chain0 = AICoop_CrumbChainStart ();

	// (a) Already on/near the trail: steer STRAIGHT at the NEWEST crumb of the
	// connected run we can walk to -- not via the pathfinder, so we never detour
	// the wrong way around a wall, and every crumb past it chains to the human.
	for (i = crumb_n-1; i >= chain0; i--)
	    if (AICoop_CanReach (mo, crumbx[i], crumby[i], false))
	    {
		tx = crumbx[i]; ty = crumby[i];
		navigate = 0; movethresh = 24*FRACUNIT;	// steer straight, no PF detour
		used = 1; break;
	    }
	// (b) Not on the trail yet.  Join it at the CLOSEST crumb of the connected run
	// and ROUTE there with the BSP pathfinder.  This used to pick the nearest crumb
	// of the WHOLE trail and walk straight at it, but the nearest crumb is regularly
	// unreachable -- behind a wall, or on top of the ledge the human jumped up -- so
	// the buddy ground against the geometry underneath it instead of walking around.
	// Restricting to the connected run keeps the join point on a stretch that really
	// leads to the human, and navigate=1 with chase_player=0 makes it walk the graph
	// route (the stairs) rather than beeline at the obstacle.
	if (!used && crumb_n > 0)
	{
	    int best = -1; fixed_t bestd = 0x7fffffff;
	    for (i = crumb_n-1; i >= chain0; i--)
	    {
		fixed_t d = P_AproxDistance (crumbx[i] - mo->x, crumby[i] - mo->y);
		if (d < bestd) { bestd = d; best = i; }
	    }
	    if (best >= 0)
	    {
		tx = crumbx[best]; ty = crumby[best];
		navigate = 1; movethresh = 24*FRACUNIT;
		used = 1;
	    }
	}
	// Truly no trail at all (e.g. a save from before any crumbs) -> beeline the human.
	if (!used)
	{
	    tx = pl->x; ty = pl->y;
	    navigate = 1; chase_player = 1; movethresh = COOP_NEAR/2;
	    AICoop_Callout ("lost:", 3);		// can't path -- beeline to the human
	}
    }

    // Navigate: if asked to walk somewhere, route there via the BSP pathfinder and
    // steer toward the next waypoint (re-pathed every ~10 tics / on goal change).
    // Combat aims directly (monsters are in sight), so it leaves stx/sty == tx/ty.
    stx = tx; sty = ty;

    // Danger heatmap decays toward zero each second so it tracks RECENT damage
    // (~5 s half-life), not everywhere a fight ever happened.
    if (pf_danger && (gametic % 35) == 0)
	{ int di; for (di = 0; di < pf_n; di++) pf_danger[di] -= pf_danger[di] >> 3; }

    if (navigate)
    {
	// Coarse route: BSP portal waypoint toward the player (cached, re-pathed ~3x/s).
	fixed_t goalx = tx, goaly = ty;
	// Re-funnel EVERY tic now that the corridor is kept: while the buddy is still on
	// its planned corridor this is a handful of reach probes, and PF_NextWaypoint
	// only re-runs A* when the goal moves to another sub-sector or the corridor is
	// lost.  Re-PLANNING from scratch on a 10-tic timer was what made the buddy
	// twitch between two waypoints on a marginal route -- the plan changed under it.
	// After a failure back off ~10 tics so an unreachable target can't run A* 35x/s.
	if (navtimer > 0) navtimer--;
	if (navtimer <= 0 || navok)
	{
	    // Safe route when retreating/regrouping (summon) or hurt: weight the A* by
	    // pf_danger so the buddy comes home through calm corridors, not the crossfire.
	    // Scoped to THIS call so monster/observe queries keep using the shortest path.
	    pf_safemode = (summon > 0 || bot->health < COOP_SAFE_HP);
	    navok = PF_NextWaypoint (mo, tx, ty, &navwx, &navwy);
	    pf_safemode = 0;
	    navjump = navok ? PF_NextIsJump () : 0;
	    if (!navok) navtimer = 10;
	}
	if (navok && !chase_player) { goalx = navwx; goaly = navwy; }

	// The route says this hop is a jump link (a 24..48 step the buddy cannot walk
	// up).  Jump on approach, instead of waiting to wedge into the step and letting
	// the stuck-handler discover BT_JUMP by accident.
	if (navok && navjump && !chase_player && !netgame
	    && P_AproxDistance (mo->x - navwx, mo->y - navwy) < 64*FRACUNIT)
	{
	    cmd->buttons |= BT_JUMP;
	    AICoop_Callout ("jump:", 2);
	}
	// A closed door on the way?  Head STRAIGHT at the doorway (no sweep) so we
	// enter the corridor toward it -- TraceSteer would sweep away from the shut
	// door (it reads as a wall) and the buddy would oscillate beside it forever.
	{
	    fixed_t	ddx, ddy;
	    boolean	hasdoor;

	    // ONE scan, shared by the USE tap and the steering below -- it walks every
	    // linedef in the map, so twice per tic is twice too many.
	    //
	    // AICoop_FindDoorAhead only ever reports doors that are still SHUT, and that is
	    // exactly the gate the USE tap needs: the route's PF_EDGE_DOOR flag was planned
	    // some tics ago, so by now the door may well be open -- and USE on an open or
	    // still-rising DR door REVERSES it.  Tapping on the flag alone had the buddy
	    // pulling the door shut on top of itself and then bumping it open again.
	    hasdoor = AICoop_FindDoorAhead (mo, goalx, goaly, &ddx, &ddy);

	    if (navok && PF_NextIsDoor () && hasdoor && doorwait == 0
		&& P_AproxDistance (mo->x - navwx, mo->y - navwy) < 96*FRACUNIT)
	    {
		cmd->buttons |= BT_USE;
		doorwait = 45;
		AICoop_Callout ("door:", 2);
	    }

	    if (AICoop_CanReach (mo, tx, ty, false))
	    {
		// The human is directly reachable -- go straight to them and ignore the
		// BSP waypoint.  Without this, a stale far waypoint (e.g. after the human
		// takes a teleporter/secret the graph doesn't model) makes the buddy leave
		// the human it's standing next to and loop back and forth.  avoiddmg=false:
		// follow the human even across nukage (e.g. MAP01's teleporter lands in it).
		stx = tx; sty = ty;
	    }
	    else if (hasdoor && AICoop_CanReach (mo, ddx, ddy, true))
	    {
		stx = ddx; sty = ddy;		// doorway in reach -> head right at it + Use
	    }
	    else if (AICoop_CanReach (mo, goalx, goaly, true))
	    {
		stx = goalx; sty = goaly;	// clear straight shot -> head right at it
	    }
	    else
	    {
		// Waypoint (or an out-of-reach doorway) is behind a wall/corner: steering
		// straight at it just grinds the wall, so navigate the corner Doom-monster
		// style -- trial-walk the 8 compass headings toward the waypoint and commit
		// to the best one.  This walks us up to the doorway, where the branch above
		// then takes over and Use opens it.
		static chasedir_t buddy_chasedir = { -1, 0, 0 };
		angle_t	cd = AICoop_ChaseDir (mo, goalx, goaly, &buddy_chasedir);
		angle_t	a  = cd >> ANGLETOFINESHIFT;
		stx = mo->x + FixedMul (96*FRACUNIT, finecosine[a]);
		sty = mo->y + FixedMul (96*FRACUNIT, finesine[a]);
	    }
	}
    }

    // turn toward the steer point (waypoint when navigating), clamped
    want  = R_PointToAngle2 (mo->x, mo->y, stx, sty);
    delta = want - mo->angle;
    rem   = (short)(delta >> 16);		// shortest signed turn (BAM>>16)
    turn  = rem;
    if (turn >  COOP_TURN) turn =  COOP_TURN;
    if (turn < -COOP_TURN) turn = -COOP_TURN;
    cmd->angleturn = (short)turn;

    dist = P_AproxDistance (tx - mo->x, ty - mo->y);

    if (fire && aimmon)
    {
	// Wrong weapon?  If holding the chainsaw/fist but the target is out of melee
	// reach, switch to a ranged weapon -- otherwise the buddy revs the saw at a foe
	// it can never touch (the bug: after picking up a chainsaw it got stuck on it,
	// revving at distant monsters).  The engine performs the switch via pendingweapon.
	if ((bot->readyweapon == wp_fist || bot->readyweapon == wp_chainsaw)
	    && bot->pendingweapon == wp_nochange
	    && P_AproxDistance (aimmon->x - mo->x, aimmon->y - mo->y) > 80*FRACUNIT)
	{
	    int w = AICoop_BestRanged (bot);
	    if (w >= 0) bot->pendingweapon = w;
	}

	// Splash-weapon suicide guard: a rocket/BFG fired at a target inside blast range
	// gibs the buddy too.  Switch to a non-splash weapon (BestRanged skips both) and
	// hold the shot until the swap lands.
	int splash_close = (bot->readyweapon == wp_missile || bot->readyweapon == wp_bfg)
		&& P_AproxDistance (aimmon->x - mo->x, aimmon->y - mo->y) < COOP_BLAST_SAFE;
	if (splash_close && bot->pendingweapon == wp_nochange)
	{
	    int w = AICoop_BestRanged (bot);
	    if (w >= 0) bot->pendingweapon = w;
	}

	// Reaction time (-buddyreact): wait a beat after sighting a *fresh* target before
	// opening fire, so the buddy isn't frame-perfect (0 = instant, the old behaviour).
	if (aimmon != react_last) { react_timer = buddy_react; react_last = aimmon; }
	if (react_timer > 0) react_timer--;

	// Aim vertically at the target's centre: if autoaim misses (target above or
	// below) the weapon falls back to lookdir ("shoot where you look"), so the
	// shot elevates instead of plugging the wall/crate in front.
	fixed_t	dz = (aimmon->z + (aimmon->height>>1)) - (mo->z + (mo->height>>1));
	fixed_t	hd = P_AproxDistance (aimmon->x - mo->x, aimmon->y - mo->y);
	int	ld = hd ? (int)((FixedDiv (dz, hd) * 160) >> FRACBITS) : 0;
	bot->lookdir = ld > COOP_LOOKMAX ? COOP_LOOKMAX : (ld < -COOP_LOOKMAX ? -COOP_LOOKMAX : ld);

	// Clear shot?  Autoaim-probe along the bearing: linetarget==NULL means the line
	// is blocked (e.g. the crate the monster stands on) or too steep to reach.  Then
	// don't waste ammo -- retreat to open the angle (it stays facing, so it fires the
	// moment the shot clears).  Capped so it doesn't back off into the next county.
	{
	    angle_t	aang = R_PointToAngle2 (mo->x, mo->y, aimmon->x, aimmon->y);
	    P_AimLineAttack (mo, aang, COOP_SIGHT);
	    if ((linetarget && linetarget->player) || AICoop_PlayerInLine (mo, aimmon))
	    {
		// Friendly fire guard: the autoaim trace hits a PLAYER (the human is
		// between us and the monster) -- DON'T shoot.  Strafe a little to clear
		// the angle so the next tic has a safe shot.
		AICoop_AddSide (cmd, ((gametic / 16) & 1) ? COOP_RUN : -COOP_RUN);
	    }
	    else if (linetarget && abs(rem) < COOP_FACING && react_timer == 0 && !splash_close)
	    {
		if (AICoop_BarrelNear (mo, aimmon))		// (D) don't detonate a barrel on ourselves
		    AICoop_Callout ("barrel:", 3);
		else
		{
		    // (buddy) An alternative buddy with a BUDDYDEF melee/ranged attack uses THAT
		    // (its claws / fireball) instead of the player weapon; -1 = plain buddy -> weapon.
		    int bd = P_Buddy_DoAttack (mo, aimmon);
		    if (bd < 0)
			cmd->buttons |= BT_ATTACK;
		    if ((gametic & 255) == 0) AICoop_Callout ("taunt:", 4);	// occasional swagger
		}
	    }
	    else if (!linetarget && dist < 768*FRACUNIT)
		backoff = true;
	}

	// Damage-progress watchdog: remember the target's health, and while we're
	// actually firing at it count the tics; if its health hasn't dropped after a
	// few attacks' worth (~2s of fire) the shots aren't connecting -- blacklist it
	// so we switch to another target (or fall back to following) instead of
	// freezing here.  Any damage at all resets the window.
	if (aimmon != dmg_mon)
	{
	    dmg_mon = aimmon; dmg_hp0 = aimmon->health; dmg_firetics = 0;
	}
	else if (cmd->buttons & BT_ATTACK)
	{
	    if (aimmon->health < dmg_hp0)		// hurting it -> keep at it
	    {
		dmg_hp0 = aimmon->health; dmg_firetics = 0;
	    }
	    else if (++dmg_firetics >= 2*TICRATE)	// fired ~2s, no damage -> give up on it
	    {
		AICoop_Blacklist (aimmon);
		dmg_mon = NULL;
	    }
	}
    }
    else
	bot->lookdir = 0;

    static int	nukagetics;		// consecutive tics stuck in a damaging floor
    if (AICoop_DamagingFloor (mo->x, mo->y) && pl)
    {
	// TRAPPED: stuck in the hazard too long (e.g. the savegame's sealed nukage room
	// with no way out) -> teleport back to the spawn point instead of slowly dying.
	if (++nukagetics > 4*TICRATE && coop_home_set
	    && !AICoop_DamagingFloor (coop_home_x, coop_home_y))
	{
	    P_TeleportMove (mo, coop_home_x, coop_home_y);
	    mo->angle = coop_home_angle;
	    mo->momx = mo->momy = mo->momz = 0;
	    nukagetics = 0;
	    cmd->angleturn = 0;
	    AICoop_Callout ("home:", 3);	// (C) teleport-home voice (recalled off a hazard)
	    return;
	}
	// Standing in nukage/lava -- get OUT.  Bolt to the nearest human (on safe
	// ground) and never freeze here (the avoidance below would set triedmove=0
	// and the buddy would just stand in the hazard and die).
	want = R_PointToAngle2 (mo->x, mo->y, pl->x, pl->y);
	rem  = (short)((want - mo->angle) >> 16);
	turn = rem;
	if (turn >  COOP_TURN) turn =  COOP_TURN;
	if (turn < -COOP_TURN) turn = -COOP_TURN;
	cmd->angleturn   = (short)turn;
	AICoop_ThrustToward (cmd, mo, pl->x, pl->y);
	triedmove = 1;
    }
    else
    {
	nukagetics = 0;			// out of the hazard -> reset the trapped timer
	triedmove = (movethresh >= 0 && dist > movethresh);

	// "Close enough, hold" uses straight-line distance -- but if the human is right
	// the other side of a wall / (secret) door, the gap is small yet not walkable.
	// So if we'd idle but can't actually walk straight to them, keep following the
	// route instead of parking on the wrong side of the door.
	if (!triedmove && navigate && navok && !AICoop_CanReach (mo, tx, ty, false))
	    triedmove = 1;

	// For low-priority moves (following), don't step onto a damaging floor --
	// check the actual move direction (toward the waypoint), not just facing.
	if (triedmove && avoiddamage)
	{
	    unsigned fa = R_PointToAngle2 (mo->x, mo->y, stx, sty) >> ANGLETOFINESHIFT;
	    fixed_t  ax = mo->x + FixedMul (32*FRACUNIT, finecosine[fa]);
	    fixed_t  ay = mo->y + FixedMul (32*FRACUNIT, finesine[fa]);
	    if (AICoop_DamagingFloor (ax, ay))
		triedmove = 0;
	}

	if (triedmove)
	    AICoop_ThrustToward (cmd, mo, stx, sty);	// straight to the waypoint
    }

    // Can't hit the target (it's above/behind a crate): back straight away from it
    // -- facing stays on it, so the buddy keeps aiming up and fires the instant the
    // angle opens.  Only onto safe, reachable ground (don't reverse into nukage/a
    // wall).  Overrides the combat advance above.
    if (backoff && aimmon)
    {
	// (I) Retreat DIAGONALLY backward -- away from the target AND to one side at once
	// (back-left or back-right), so the buddy weaves back instead of reversing in a
	// dead-straight line.  Prefer one back-diagonal (sticky); if it's blocked or over a
	// hazard, take the other; only as a last resort fall straight back.  Facing stays on
	// the target, so it keeps aiming and fires the instant the angle opens.
	static int	backside = 1;		// +1 = back-left, -1 = back-right; flips when blocked
	angle_t		away = R_PointToAngle2 (aimmon->x, aimmon->y, mo->x, mo->y);	// straight-away bearing
	int		t;
	for (t = 0; t < 3; t++)
	{
	    angle_t	a;
	    unsigned	fa;
	    fixed_t	rx, ry;
	    if (t == 0)      a = (backside > 0) ? away + ANG45 : away - ANG45;	// preferred back-diagonal
	    else if (t == 1) a = (backside > 0) ? away - ANG45 : away + ANG45;	// the other one
	    else             a = away;						// last resort: straight back
	    fa = a >> ANGLETOFINESHIFT;
	    rx = mo->x + FixedMul (64*FRACUNIT, finecosine[fa]);
	    ry = mo->y + FixedMul (64*FRACUNIT, finesine[fa]);
	    if (!AICoop_DamagingFloor (rx, ry) && AICoop_CanReach (mo, rx, ry, true))
	    {
		AICoop_ThrustToward (cmd, mo, rx, ry);
		triedmove = 1;
		if (t == 1) backside = -backside;	// preferred side was blocked -> switch and stick
		break;
	    }
	}
    }

    // Wedged while trying to move -- most often a closed door on the path, else a
    // corner / a blocking thing (e.g. a barrel; we don't shoot those).
    static boolean wasstuck;
    if (triedmove && stuck)
    {
	static int wig;
	// Tap Use for a door in front (we already face the steer point, which is the
	// doorway when one is ahead).  Gated so we don't reverse a DR door mid-rise:
	// 45 tics > the ~32-tic open, so by the next tap the door is passable and we
	// are walking through (no longer stuck), so no second tap fires.
	// (B) Low ledge it can't step up?  Jump it -- the player can (disabled in netgame).
	if (!netgame && AICoop_JumpableStep (mo, stx, sty)) { cmd->buttons |= BT_JUMP; AICoop_Callout ("jump:", 2); }
	// Only tap USE when a REAL door is in front -- otherwise the wedge is a plain
	// wall/ledge and we'd just spam USE on it (jump/wiggle handles those instead).
	if (doorwait == 0 && AICoop_DoorInFront (mo)) { cmd->buttons |= BT_USE; doorwait = 45; AICoop_Callout ("door:", 2); }
	// Sideways wiggle to slip past a barrel / convex corner (non-door wedge).
	AICoop_AddSide (cmd, ((wig++ / 24) & 1) ? COOP_RUN : -COOP_RUN);
	// Announce on the rising edge, but also rate-limit to ~once per 25 s -- on tight
	// geometry the buddy wedges repeatedly and would otherwise complain constantly.
	{
	    static int laststuckvoice = -100000;
	    if (!wasstuck && gametic - laststuckvoice > 25*TICRATE)
		{ AICoop_Callout ("stuck:", 3); laststuckvoice = gametic; }
	}
	wasstuck = true;
    }
    else wasstuck = false;

    // (E) Careful near edges: if the chosen move heads toward a drop-off or a damaging
    // floor, crawl -- a slower step overshoots the lip far less, so the buddy stops
    // sliding off ledges into nukage.  Direction is the actual (forward,side) vector.
    if (cmd->forwardmove || cmd->sidemove)
    {
	int     fa = mo->angle >> ANGLETOFINESHIFT;
	fixed_t vx = cmd->forwardmove*finecosine[fa] + cmd->sidemove*finesine[fa];
	fixed_t vy = cmd->forwardmove*finesine[fa]   - cmd->sidemove*finecosine[fa];
	int     ma = R_PointToAngle2 (0, 0, vx, vy) >> ANGLETOFINESHIFT;
	fixed_t nx = mo->x + FixedMul (40*FRACUNIT, finecosine[ma]);
	fixed_t ny = mo->y + FixedMul (40*FRACUNIT, finesine[ma]);
	if (AICoop_FallAhead (mo, nx, ny)) { cmd->forwardmove /= 3; cmd->sidemove /= 3; AICoop_Callout ("edge:", 3); }
    }

    // Missile dodge has the final say on movement: if a projectile is closing on us,
    // sidestep it (overriding the approach/backoff move for this tic).  Aim and fire
    // are separate fields, so the buddy keeps shooting while it strafes clear.
    AICoop_DodgeMissile (cmd, mo);

    // (buddy) speed stat: scale the final move by the selected buddy's movement factor
    // (1.0 for the Marine).
    //
    // ORDER MATTERS.  Bound the AI's own move to the run speed FIRST -- the steering code
    // accumulates overshoot and unbounded that alone makes every buddy outrun a sprinting
    // player.  Only THEN apply the speed stat.  Clamping after the scale (the old order)
    // capped the result back at run speed, so a BUDDYDEF `speed` ABOVE the default 8 was
    // silently a no-op -- the AI already steers at 0x32, so scaling up and re-clamping
    // gave back exactly 0x32.  Only speeds BELOW 8 did anything, which read as "the Speed
    // parameter is ignored".  ticcmd_t fields are signed char; the 2x movescale cap keeps
    // the scaled value at 0x64 worst case, well inside that.
    {
	int fm = cmd->forwardmove;
	int sm = cmd->sidemove;

	if (fm >  0x32) fm =  0x32; else if (fm < -0x32) fm = -0x32;
	if (sm >  0x32) sm =  0x32; else if (sm < -0x32) sm = -0x32;

	if (buddy_movescale != FRACUNIT)
	{
	    fm = FixedMul (fm << FRACBITS, buddy_movescale) >> FRACBITS;
	    sm = FixedMul (sm << FRACBITS, buddy_movescale) >> FRACBITS;
	    if (fm >  127) fm =  127; else if (fm < -127) fm = -127;
	    if (sm >  127) sm =  127; else if (sm < -127) sm = -127;
	}
	cmd->forwardmove = (signed char)fm;
	cmd->sidemove    = (signed char)sm;
    }
}
