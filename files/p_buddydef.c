// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Native modder co-op buddies (BuddyDoom) -- see p_buddydef.h.
//
//	BUDDYDEF lump grammar (case-insensitive keys, "#" comments, JSON-ish braces
//	optional -- commas/quotes tolerated):
//
//	    # frank.buddydef
//	    buddy {
//	      name        "Frank N. Stein"
//	      desc        "Gamma bruiser. Tanky, slow, hits like a Hell Knight."
//	      sprite      FRAN            # 4-char sprite base (needs FRANA1.. in the WAD)
//	      health      999
//	      speed       12
//	      radius      24              # map units (auto *FRACUNIT)
//	      height      64
//	      mass        1000
//	      painchance  100
//	      color       green           # default colour on the Buddy select screen
//	      seesound    FRANKN          # sound-lump name (FRANKN, or DSFRANKN -- both work)
//	      painsound   FRANKN
//	      deathsound  FRANKN
//	      activesound FRANKN
//	      special     "Tanky bruiser" # free-text blurb for the Buddy select screen
//	      ability     poisonbag       # NAMED power: none | drone | poisonbag | turret
//	    }
//
//	A BUDDYDEF record is a ROSTER entry: the Buddy select menu reads its name,
//	description, preview sprite, colour and stats from here.  It no longer builds
//	a standalone actor -- see the note below.  `attack` and `ednum` are still
//	accepted (old lumps keep loading) but inert: player 2 fights with weapons and
//	cannot be placed in a map.
//
//	--- the player-2 rule -------------------------------------------------------
//	A buddy is ALWAYS player 2, so it inherits the whole co-op bot (p_ai_coop.c):
//	door use, orders, HUD, automap marker, revive, pathfinder, weapons, pickups,
//	savegame.  The earlier design built each BUDDYDEF buddy as its own MF_FRIEND
//	mobj with 29 generated states, which made it a monster -- and a monster cannot
//	open a door, be ordered, be revived or appear on the HUD.  Everything that
//	path needed (the state builder, the attack codepointer, the thinker-list
//	lookup, the duplicate console/HUD/automap routing) is gone.
//
//	Applying a record to player 2 -- skin, stats, sounds, behaviour -- is the work
//	in progress; until it lands, picking a modder buddy still gives you the Marine.
//	Keys are parsed and shown on the select screen either way, so a BUDDYDEF lump
//	written today stays valid.  Design: docs/BUDDYDEF.md.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "doomdef.h"
#include "doomstat.h"			// players[], playeringame[], vanilla_mode, netgame, demoplayback
#include "info.h"
#include "sounds.h"
#include "s_sound.h"			// S_StartSound -- turret deploy blip
#include "m_fixed.h"
#include "r_main.h"		// R_PointToAngle2 -- aiming the thrown flechette
#include "tables.h"			// finesine/finecosine, ANGLETOFINESHIFT (drone placement)
#include "w_wad.h"
#include "z_zone.h"
#include "p_local.h"			// MF_*, P_SpawnMobj, P_TryMove, P_AproxDistance
#include "p_ai_coop.h"			// P_AICoop_Slot
#include "strife.h"		// Strife_Mon_TypeByName  -- basemonster donors
#include "hexen.h"		// Hexen_Mon_TypeByName
#include "heretic.h"		// Heretic_TypeByName
#include "p_buddydef.h"

// engine tables (grown by the DSDHacked API) -- only the sprite table now: a roster
// entry needs a spritenum for its menu preview, nothing else.
extern char**		sprnames;
extern int		num_sprites;

extern void dsdh_EnsureSpritesCapacity  (int);

extern mobj_t*	P_SpawnMobj (fixed_t, fixed_t, fixed_t, mobjtype_t);

// Buddy player-colour (v_png.c / r_things.c / m_menu.c).  Applying it to the live
// companion is the co-op bot's job (p_ai_coop.c) -- here we only map a name to an index
// for the BUDDYDEF `color` key.
extern int		V_BuddyColorCount (void);
extern const char*	V_BuddyColorName  (int);

// The active selection (Buddy menu / config): 0 = Marine, 1..N = a BUDDYDEF roster slot.
extern int		buddy_select;

// Map a BUDDYDEF `color <name>` string to a colour index (-1 if unknown).
static int Buddy_ColorIndex (const char* s)
{
    int i, n = V_BuddyColorCount ();
    if (!s || !*s) return -1;
    for (i = 0; i < n; i++)
	if (!strcasecmp (s, V_BuddyColorName (i))) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Roster (slot 0 = built-in Marine, always present).
// ---------------------------------------------------------------------------
#define MAXBUDDIES	24

typedef struct
{
    char	name[40];
    char	desc[160];
    char	melee[24];	// close-range attack style (stats panel; BUDDYDEF `meleeattack`)
    char	ranged[24];	// at-distance attack style (BUDDYDEF `rangedattack`)
    char	monster[24];	// base monster the buddy was derived from (BUDDYDEF `monster`)
    char	special[96];	// modder-supplied "special abilities" text
    char	ability[24];	// BUDDYDEF `ability`: the named special ABILITY the buddy
				// actually uses in play (none|drone|poisonbag|...)
    char	seesnd[16], painsnd[16], deathsnd[16], activesnd[16];	// BUDDYDEF sound lumps
    int		spritenum;	// preview sprite
    int		color;		// declared default player-colour index, -1 = none (BUDDYDEF `color`)
    int		colorlock;	// BUDDYDEF `color` given as false -> never recolour, lock the menu row
    int		health, speed, radius, height, mass, painchance, reactiontime;
    char	framesrc[24];	// BUDDYDEF `basemonster`: donor whose frame LAYOUT is mirrored
    char	spritebase[8];	// BUDDYDEF `sprite`, kept for the frame-mirror check
    int		damagescale;	// BUDDYDEF `damagescale`, percent (100 = unchanged)
    byte	framemap[BUDDY_NFRAMES];	// player frame -> this buddy's sheet frame
    int		has_framemap;	// 0 = draw player frames unchanged
} buddyrec_t;

static buddyrec_t	roster[MAXBUDDIES];
static int		nroster = 0;

// ---------------------------------------------------------------------------
// Frame remap (BUDDYDEF `frames <monster>`)
//
// The buddy's body is player 2, so it runs the PLAYER state machine -- but its art is a
// MONSTER sheet, and those put their frames somewhere else entirely.  Compare the layouts:
//
//     player  A-D run   E,F atk   G pain    H-N death   O-W gibs
//     knight  A-D run   E-G atk   H pain    I-O death   (none)
//     stalker A-C run   J,K atk   L pain    O-] death   (none)
//
// so a buddy drawn with player frame numbers shows its attack pose when it flinches and
// its walk cycle when it dies.  Rather than guess a "monster convention" (there isn't one
// -- see the three rows above), inherit the layout from a donor monster the modder names:
// its own state chains say exactly which frames mean run/attack/pain/death.  Only the
// FRAME NUMBERS are taken; the player states keep their own tics and action pointers, so
// death still screams and falls.  This is presentation only -- no new states, no mobjinfo
// change, nothing in the savegame.
static int Buddy_TypeByName (const char* n)
{
    static const struct { const char* name; int type; } tab[] =
    {
	{ "zombieman",   MT_POSSESSED }, { "shotgunguy",    MT_SHOTGUY },
	{ "chaingunner", MT_CHAINGUY  }, { "imp",           MT_TROOP   },
	{ "demon",       MT_SERGEANT  }, { "pinky",         MT_SERGEANT},
	{ "spectre",     MT_SHADOWS   }, { "cacodemon",     MT_HEAD    },
	{ "hellknight",  MT_KNIGHT    }, { "baron",         MT_BRUISER },
	{ "revenant",    MT_UNDEAD    }, { "mancubus",      MT_FATSO   },
	{ "arachnotron", MT_BABY      }, { "painelemental", MT_PAIN    },
	{ "lostsoul",    MT_SKULL     }, { "archvile",      MT_VILE    },
	{ "cyberdemon",  MT_CYBORG    }, { "spidermastermind", MT_SPIDER },
	{ "wolfss",      MT_WOLFSS    },
    };
    char norm[32];
    int i, k = 0;

    if (!n || !*n || !strcasecmp (n, "none")) return -1;
    // Fold to a bare token so the display spellings modders already write work as keys:
    // "Strife Stalker", "hell_knight" and "hellknight" all name the same donor.
    for (i = 0; n[i] && k < (int)sizeof norm - 1; i++)
	if (n[i] != ' ' && n[i] != '_' && n[i] != '-')
	    norm[k++] = (char)tolower ((unsigned char)n[i]);
    norm[k] = 0;

    for (i = 0; i < (int)(sizeof tab / sizeof tab[0]); i++)
	if (!strcmp (norm, tab[i].name)) return tab[i].type;

    // Not a Doom monster -- hand the folded name to the per-game resolvers, which already
    // own these tables (and their spelling variants).  A Heretic/Hexen/Strife buddy can
    // therefore mirror its own game's monsters without a second name list here.
    {
	int t;
	if ((t = Strife_Mon_TypeByName (norm)) >= 0) return t;
	if ((t = Hexen_Mon_TypeByName  (norm)) >= 0) return t;
	if ((t = Heretic_TypeByName    (norm)) >= 0) return t;
    }
    return -1;
}

// Distinct frame numbers along one state chain, in order.  Stops at the terminal frame
// (tics < 0, the corpse), when the chain loops back to its start (a run cycle), or on a
// guard -- a donor chain is data and may be anything after a DEHACKED patch.
static int Buddy_ChainFrames (int start, byte* out, int max)
{
    int st = start, n = 0, guard = 0;
    while (st > 0 && st < num_states && n < max && guard++ < 128)
    {
	int f = states[st].frame & FF_FRAMEMASK;
	if (f < BUDDY_NFRAMES && (!n || out[n-1] != (byte)f))
	    out[n++] = (byte)f;
	if (states[st].tics < 0) break;			// terminal (corpse) frame
	st = states[st].nextstate;
	if (st == start) break;				// cycle closed
    }
    return n;
}

static void Buddy_BuildFrameMap (buddyrec_t* r, int donor)
{
    byte run[8], atk[8], pain[4], die[20], xdie[20];
    int  nrun, natk, npain, ndie, nxdie, i;

    for (i = 0; i < BUDDY_NFRAMES; i++) r->framemap[i] = (byte)i;   // identity
    r->has_framemap = 0;
    if (donor < 0 || donor >= num_mobjtypes) return;

    nrun  = Buddy_ChainFrames (mobjinfo[donor].seestate,   run,  8);
    natk  = Buddy_ChainFrames (mobjinfo[donor].missilestate > 0
			       ? mobjinfo[donor].missilestate
			       : mobjinfo[donor].meleestate,   atk,  8);
    npain = Buddy_ChainFrames (mobjinfo[donor].painstate,   pain, 4);
    ndie  = Buddy_ChainFrames (mobjinfo[donor].deathstate,  die,  20);
    nxdie = Buddy_ChainFrames (mobjinfo[donor].xdeathstate, xdie, 20);

    // Player A-D (0..3): idle + the 4 run frames.
    for (i = 0; i < 4 && nrun; i++)  r->framemap[i]    = run [i < nrun  ? i : nrun -1];
    // Player E,F (4,5): the two attack frames.
    for (i = 0; i < 2 && natk; i++)  r->framemap[4+i]  = atk [i < natk  ? i : natk -1];
    // Player G (6): pain.
    if (npain)			     r->framemap[6]    = pain[0];
    // Player H..N (7..13): the 7 death frames.
    for (i = 0; i < 7 && ndie; i++)  r->framemap[7+i]  = die [i < ndie  ? i : ndie -1];
    // Player O..W (14..22): gibs.  Monster sheets almost never have an xdeath -- holding
    // the last death frame leaves the corpse lying there, which beats the alternative of
    // falling back to the sprite the buddy is NOT (a gibbing marine).
    for (i = 0; i < 9; i++)
	r->framemap[14+i] = nxdie ? xdie[i < nxdie ? i : nxdie-1]
			  : ndie  ? die [ndie-1]
			  :         r->framemap[14+i];
    r->has_framemap = 1;
}

// Strict mirror check: every frame the donor's layout asks for must exist in the buddy's
// own sheet.  A buddy that inherits Hell Knight frames but whose art stops at N dies on a
// frame it does not have -- the renderer then falls back to the marine mid-animation,
// which is exactly the class of bug this rule exists to prevent.  Reports, does not
// refuse: partial art still plays, it just tells you where it will break.
static void Buddy_CheckFrames (const buddyrec_t* r, const char* sprite, const char* who)
{
    char miss[64];
    int  i, n = 0, seen[BUDDY_NFRAMES];

    for (i = 0; i < BUDDY_NFRAMES; i++) seen[i] = 0;
    // Only the frames the PLAYER sheet actually reaches (A..W).  The map is sized for the
    // full Doom frame range so a donor may point anywhere in it, but entries above W are
    // never looked up -- checking them reported the untouched identity tail as "missing".
    for (i = 0; i < BUDDY_NPLAYFRAMES; i++)
    {
	int f = r->framemap[i];
	if (f >= BUDDY_NFRAMES || seen[f]) continue;
	seen[f] = 1;
	if (Buddy_FramePresent (sprite, f)) continue;
	if (n < (int)sizeof miss - 2) miss[n++] = (char)('A' + f);
    }
    miss[n] = 0;
    if (n)
	printf ("BUDDYDEF: '%s' mirrors %s but its sprite %.4s is missing frame(s) %s "
		"-- those poses fall back to the marine.\n", who, r->framesrc, sprite, miss);
}

// Resolve every buddy's `basemonster` into its frame map.  Deliberately NOT done while
// parsing: the per-game name resolvers gate on their game's art being present, and
// Strife_Available() reads sprites[SPR_S_PLAY] -- a table built by R_InitSprites, which
// is called from P_Init (p_setup.c), NOT from R_Init as the name suggests, and in either
// case long after P_Buddy_LoadDefs.  Resolving during the parse dereferenced sprites[]
// before it existed and crashed on startup.  D_DoomMain calls this right after P_Init.
void P_Buddy_ResolveFrames (void)
{
    int s;
    for (s = 1; s < nroster; s++)
    {
	buddyrec_t* r = &roster[s];
	if (!r->framesrc[0])
	    continue;
	Buddy_BuildFrameMap (r, Buddy_TypeByName (r->framesrc));
	if (!r->has_framemap)
	    printf ("BUDDYDEF: '%s' has unknown basemonster \"%s\" -- drawing player frames.\n",
		    r->name, r->framesrc);
	else
	    Buddy_CheckFrames (r, r->spritebase, r->name);
    }
}

// The remap for a roster slot, or NULL when that buddy draws player frames unchanged.
const byte* P_Buddy_FrameMap (int s)
{
    return (s > 0 && s < nroster && roster[s].has_framemap) ? roster[s].framemap : NULL;
}

// ---------------------------------------------------------------------------
// Named special abilities (BUDDYDEF `ability`).  The blurb in `special` is just
// text for the select screen; THIS is the mechanic the buddy actually uses, run
// once per tic by P_Buddy_AbilityTicker.
// ---------------------------------------------------------------------------
enum { BA_NONE = 0, BA_DRONE, BA_TURRET, BA_LICHLING, BA_STALKER,
       BA_POISONBAG, BA_NUM };

static const char* const buddy_ability_name[BA_NUM] =
{
    "none", "drone", "turret", "lichling", "stalker", "poisonbag"
};

// Ability name -> id, or -1 when the name isn't one we know.  "" counts as none, so a
// BUDDYDEF that simply omits the key is valid.
static int Buddy_AbilityId (const char* s)
{
    int i;
    if (!s || !*s) return BA_NONE;
    for (i = 0; i < BA_NUM; i++)
	if (!strcasecmp (s, buddy_ability_name[i]))
	    return i;
    return -1;
}

int         P_Buddy_Count  (void)          { return nroster; }
const char* P_Buddy_Name   (int s)         { return (s >= 0 && s < nroster) ? roster[s].name : ""; }
const char* P_Buddy_Desc   (int s)         { return (s >= 0 && s < nroster) ? roster[s].desc : ""; }
int         P_Buddy_Sprite (int s)         { return (s >= 0 && s < nroster) ? roster[s].spritenum : SPR_PLAY; }
int         P_Buddy_Color  (int s)         { return (s >= 0 && s < nroster) ? roster[s].color : -1; }
// 1 = BUDDYDEF locked the colour (`color 0`): do not recolour, do not offer the choice.
int         P_Buddy_ColorLocked (int s)    { return (s > 0 && s < nroster) ? roster[s].colorlock : 0; }

// Fill `out` with the buddy's stats (for the Buddy select screen).
void P_Buddy_GetStats (int s, buddystats_t* out)
{
    if (!out) return;
    if (s < 0 || s >= nroster)
    { memset (out, 0, sizeof *out);
      out->melee = out->ranged = out->monster = out->special = out->ability = ""; return; }
    out->health       = roster[s].health;
    out->speed        = roster[s].speed;
    out->radius       = roster[s].radius;
    out->height       = roster[s].height;
    out->mass         = roster[s].mass;
    out->painchance   = roster[s].painchance;
    out->reactiontime = roster[s].reactiontime;
    out->melee        = roster[s].melee;
    out->ranged       = roster[s].ranged;
    out->monster      = roster[s].monster;
    out->special      = roster[s].special;
    out->ability      = roster[s].ability;
}

// BUDDYDEF sound lump name for the co-op driver (which: BUDDYSND_*).  "" = not set.
const char* P_Buddy_Sound (int s, int which)
{
    if (s < 0 || s >= nroster) return "";
    switch (which)
    {
      case BUDDYSND_SEE:    return roster[s].seesnd;
      case BUDDYSND_PAIN:   return roster[s].painsnd;
      case BUDDYSND_DEATH:  return roster[s].deathsnd;
      case BUDDYSND_ACTIVE: return roster[s].activesnd;
    }
    return "";
}

// The named special ability of a roster slot ("" = none).  P_Buddy_AbilityTicker runs it.
const char* P_Buddy_Ability (int s)
{
    return (s >= 0 && s < nroster) ? roster[s].ability : "";
}

// ---------------------------------------------------------------------------
// Sprite registration (DSDHacked table growth) -- for the select-screen preview.
// ---------------------------------------------------------------------------

// Return true if <name>A1 or <name>A0 exists as a lump (a rotation-0 or 8-rot
// front frame).  Guards against registering a sprite with no art (R_InitSpriteDefs
// I_Errors on a named sprite with zero frames).
// Is <base><frame> in the WAD?  Sprite lumps are BASE + frame letter + rotation, so a
// frame exists if either its 8-rotation form (...A1) or its single-rotation form (...A0)
// is there.  Runs at BUDDYDEF load time, which is BEFORE R_InitSprites (d_main.c), so it
// has to ask the lump directory -- sprites[] does not exist yet.
static boolean Buddy_FramePresent (const char base[4], int frame)
{
    char n[9];
    if (frame < 0 || frame > 28) return false;
    memcpy (n, base, 4);
    n[4] = (char)('A' + frame); n[5] = '1'; n[6] = 0;
    if (W_CheckNumForName (n) >= 0) return true;
    n[5] = '0';
    return W_CheckNumForName (n) >= 0;
}

static boolean Buddy_SpritePresent (const char base[4])
{
    return Buddy_FramePresent (base, 0);
}

// Cross-game sprite-name collisions (docs/BUDDY_SPRITE_COLLISIONS.md).  A buddy pack
// borrowing a monster's art names it with that monster's NATIVE 4-char code, but this is
// one shared sprite namespace and a few Strife codes duplicate a Doom one.  The flagged
// case: the Strife Stalker's frames are SPID* in strife1.wad, and SPID is Doom's Spider
// Mastermind -- a `sprite SPID` buddy loaded in Doom therefore rendered as the Spider
// Mastermind.  BuddyDoom ships those frames under the collision-free base STLK in
// buddydoom.wad (exactly what ZDoom does), so redirect the native code to it.
//
// Add a row here for each collision as its art lands in the asset WAD; the redirect only
// fires when the placeholder actually HAS art, so a pack keeps working if it doesn't.
static const struct { const char native[5], placeholder[5]; } buddy_sprite_alias[] =
{
    { "SPID", "STLK" },		// Strife Stalker vs Doom Spider Mastermind
};

// Find or append a 4-char sprite name; returns the spritenum (SPR_TNT1 on failure).
static int Buddy_RegSprite (const char* raw)
{
    char base[4];
    int i;
    for (i = 0; i < 4; i++)
	base[i] = raw[i] ? toupper((unsigned char)raw[i]) : ' ';

    for (i = 0; i < (int)(sizeof buddy_sprite_alias / sizeof buddy_sprite_alias[0]); i++)
	if (!strncmp (base, buddy_sprite_alias[i].native, 4)
	    && Buddy_SpritePresent (buddy_sprite_alias[i].placeholder))
	{
	    printf ("Buddy: sprite %.4s -> %.4s (cross-game collision; see "
		    "docs/BUDDY_SPRITE_COLLISIONS.md).\n",
		    base, buddy_sprite_alias[i].placeholder);
	    memcpy (base, buddy_sprite_alias[i].placeholder, 4);
	    break;
	}

    // already known?
    for (i = 0; i < num_sprites; i++)
	if (sprnames[i] && !strncasecmp (sprnames[i], base, 4))
	    return i;

    if (!Buddy_SpritePresent (base))
	return -1;					// no art in the WAD -> skip this buddy

    {   // append
	char* nm = malloc (5);
	int idx = num_sprites;
	memcpy (nm, base, 4); nm[4] = 0;
	dsdh_EnsureSpritesCapacity (idx);
	sprnames[idx] = nm;
	return idx;
    }
}

// ---------------------------------------------------------------------------
// One parsed record.
// ---------------------------------------------------------------------------
typedef struct
{
    char	name[40];
    char	desc[160];
    char	sprite[8];
    char	melee[24];	// BUDDYDEF `meleeattack` / `melee`
    char	ranged[24];	// BUDDYDEF `rangedattack` / `ranged` / `missile`
    char	monster[24];	// BUDDYDEF `monster` / `basemonster` (preset origin)
    char	attack[24];	// legacy single `attack` key (mapped to melee if melee unset)
    char	seesnd[16], painsnd[16], deathsnd[16], activesnd[16];
    char	special[96];
    char	ability[24];
    int		colorlock;	// `color` given as a boolean false -> colour is fixed
    int		damagescale;	// BUDDYDEF `damagescale` -- percent, 100 = unchanged
    int		health, speed, radius, height, mass, painchance, reactiontime, ednum;
    int		color;		// player-colour index, -1 = none declared
    boolean	have_any;
} buddyparse_t;

static void Buddy_Defaults (buddyparse_t* b)
{
    memset (b, 0, sizeof *b);
    strcpy (b->name, "Buddy");
    strcpy (b->sprite, "PLAY");
    strcpy (b->melee, "none");
    strcpy (b->ranged, "none");
    strcpy (b->ability, "none");
    /* monster/attack default to "" (memset above) */
    b->health = 200; b->speed = 8; b->radius = 20; b->height = 56;
    b->mass = 100;   b->painchance = 120; b->reactiontime = 8; b->ednum = -1;
    b->damagescale = 100;
    b->color = -1;
}

// Turn a completed record into a roster entry (the Buddy select screen reads it).
// No mobjtype and no states: a buddy is player 2, so its body is the player mobj --
// the record supplies properties, never a state machine.
static void Buddy_Register (buddyparse_t* b)
{
    int spr;

    if (nroster >= MAXBUDDIES) return;

    spr = Buddy_RegSprite (b->sprite);
    if (spr < 0)
    {
	printf ("Buddy: '%s' skipped -- sprite %.4s not found in WADs.\n", b->name, b->sprite);
	return;
    }

    // roster entry (+ the stats shown on the Buddy select screen)
    {
	buddyrec_t* r = &roster[nroster++];
	// Legacy: a BUDDYDEF with only the old single `attack` key seeds melee.
	if ((!b->melee[0] || !strcmp (b->melee, "none")) && b->attack[0])
	    strncpy (b->melee, b->attack, sizeof b->melee - 1);
	strncpy (r->name, b->name, sizeof r->name - 1);
	strncpy (r->desc, b->desc, sizeof r->desc - 1);
	strncpy (r->melee, b->melee, sizeof r->melee - 1);
	strncpy (r->ranged, b->ranged, sizeof r->ranged - 1);
	strncpy (r->monster, b->monster, sizeof r->monster - 1);
	strncpy (r->special, b->special, sizeof r->special - 1);
	strncpy (r->ability, b->ability, sizeof r->ability - 1);
	strncpy (r->seesnd,   b->seesnd,   sizeof r->seesnd - 1);
	strncpy (r->painsnd,  b->painsnd,  sizeof r->painsnd - 1);
	strncpy (r->deathsnd, b->deathsnd, sizeof r->deathsnd - 1);
	strncpy (r->activesnd,b->activesnd,sizeof r->activesnd - 1);
	r->seesnd[sizeof r->seesnd - 1] = r->painsnd[sizeof r->painsnd - 1] = 0;
	r->deathsnd[sizeof r->deathsnd - 1] = r->activesnd[sizeof r->activesnd - 1] = 0;
	r->name[sizeof r->name - 1] = 0;
	r->desc[sizeof r->desc - 1] = 0;
	r->melee[sizeof r->melee - 1] = 0;
	r->ranged[sizeof r->ranged - 1] = 0;
	r->monster[sizeof r->monster - 1] = 0;
	r->special[sizeof r->special - 1] = 0;
	r->ability[sizeof r->ability - 1] = 0;
	// Unknown ability -> refuse it rather than pretending the buddy has a power.
	if (Buddy_AbilityId (r->ability) < 0)
	{
	    printf ("BUDDYDEF: '%s' has unknown ability \"%s\" -- ignored "
		    "(known: none, drone, poisonbag, turret, lichling, stalker).\n",
		    b->name, r->ability);
	    strcpy (r->ability, "none");
	}
	r->spritenum    = spr;
	r->color        = b->color;
	r->colorlock    = b->colorlock;
	r->health       = b->health;
	r->speed        = b->speed;
	r->radius       = b->radius;
	r->height       = b->height;
	r->mass         = b->mass;
	r->painchance   = b->painchance;
	r->reactiontime = b->reactiontime;
	r->damagescale  = b->damagescale;
	// Frame layout comes from `basemonster` and nowhere else: naming a base monster means
	// the buddy's sheet MIRRORS it 1:1 (same frame letters for run/attack/pain/death).
	// Only RECORD the name here -- resolving it is P_Buddy_ResolveFrames's job, which runs
	// after R_Init (see the note there).
	strncpy (r->framesrc, b->monster, sizeof r->framesrc - 1);
	r->framesrc[sizeof r->framesrc - 1] = 0;
	// The RESOLVED base, not the declared one: buddy_sprite_alias may have redirected it
	// (SPID -> STLK), and checking the name the modder typed then asks the wrong sheet.
	snprintf (r->spritebase, sizeof r->spritebase, "%.4s",
		  (spr >= 0 && spr < num_sprites && sprnames[spr]) ? sprnames[spr] : b->sprite);
    }
    printf ("Buddy: registered '%s' (roster slot %d, sprite %.4s).\n",
	    b->name, nroster - 1, (spr >= 0 && spr < num_sprites && sprnames[spr])
				  ? sprnames[spr] : b->sprite);	// the RESOLVED name (see the alias table)
}

// ---------------------------------------------------------------------------
// Text parsing.
// ---------------------------------------------------------------------------

// Copy the value part of a "key value" line into dst: skip leading ws / ':' / '=',
// strip surrounding quotes, drop a trailing comment / comma / CR.
static void Buddy_Value (const char* p, char* dst, int cap)
{
    int n = 0;
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') p++;
    if (*p == '"')
    {
	p++;
	while (*p && *p != '"' && n < cap-1) dst[n++] = *p++;
    }
    else
    {
	while (*p && *p != '#' && *p != '\r' && *p != '\n' && n < cap-1) dst[n++] = *p++;
	while (n > 0 && (dst[n-1]==' '||dst[n-1]=='\t'||dst[n-1]==','||dst[n-1]=='{')) n--;
    }
    dst[n] = 0;
}

static void Buddy_ParseText (const char* text, int len)
{
    char line[512];
    buddyparse_t cur;
    boolean inrec = false;
    int i = 0;

    Buddy_Defaults (&cur);

    while (i < len)
    {
	// pull one line
	int n = 0;
	while (i < len && text[i] != '\n' && n < (int)sizeof(line)-1) line[n++] = text[i++];
	if (i < len && text[i] == '\n') i++;
	line[n] = 0;

	{   // strip comment + trim
	    char *c = strchr (line, '#'); if (c) *c = 0;
	    char key[32]; const char* p = line; int k = 0;
	    while (*p == ' ' || *p == '\t') p++;
	    if (!*p) continue;

	    // brace handling (may sit alone or after "buddy")
	    if (*p == '{') { Buddy_Defaults (&cur); inrec = true; continue; }
	    if (*p == '}') { if (inrec) { cur.have_any = true; Buddy_Register (&cur); } inrec = false; continue; }

	    // first token = key
	    while (*p && *p != ' ' && *p != '\t' && *p != ':' && *p != '=' && *p != '{' && k < 31)
		key[k++] = tolower((unsigned char)*p++);
	    key[k] = 0;

	    if (!strcmp (key, "buddy"))			// "buddy {" or "buddy" then "{"
	    {
		if (strchr (p, '{')) { Buddy_Defaults (&cur); inrec = true; }
		continue;
	    }
	    if (!inrec) continue;			// ignore stray lines outside a record

	    if      (!strcmp(key,"name"))	Buddy_Value (p, cur.name, sizeof cur.name);
	    else if (!strcmp(key,"desc")
		  || !strcmp(key,"about")
		  || !strcmp(key,"info"))	Buddy_Value (p, cur.desc, sizeof cur.desc);
	    else if (!strcmp(key,"sprite"))	Buddy_Value (p, cur.sprite, sizeof cur.sprite);
	    // attack style -- split into melee + ranged (mybuddy writes these).  The old
	    // single `attack` key is still read as a legacy alias (mapped to melee below).
	    else if (!strcmp(key,"meleeattack")
		  || !strcmp(key,"melee"))	Buddy_Value (p, cur.melee, sizeof cur.melee);
	    else if (!strcmp(key,"rangedattack")
		  || !strcmp(key,"ranged")
		  || !strcmp(key,"missile"))	Buddy_Value (p, cur.ranged, sizeof cur.ranged);
	    else if (!strcmp(key,"monster")
		  || !strcmp(key,"basemonster"))Buddy_Value (p, cur.monster, sizeof cur.monster);
	    else if (!strcmp(key,"attack"))	Buddy_Value (p, cur.attack, sizeof cur.attack);
	    else if (!strcmp(key,"seesound"))	Buddy_Value (p, cur.seesnd, sizeof cur.seesnd);
	    else if (!strcmp(key,"painsound"))	Buddy_Value (p, cur.painsnd, sizeof cur.painsnd);
	    else if (!strcmp(key,"deathsound"))	Buddy_Value (p, cur.deathsnd, sizeof cur.deathsnd);
	    else if (!strcmp(key,"activesound"))Buddy_Value (p, cur.activesnd, sizeof cur.activesnd);
	    // `special`/`abilities` = free-text blurb for the select screen;
	    // `ability` = the NAMED mechanic the buddy actually uses in play.
	    else if (!strcmp(key,"special")
		  || !strcmp(key,"abilities"))	Buddy_Value (p, cur.special, sizeof cur.special);
	    else if (!strcmp(key,"ability"))	Buddy_Value (p, cur.ability, sizeof cur.ability);
	    else if (!strcmp(key,"color") || !strcmp(key,"colour"))
	    {
		char cbuf[24]; int ci;
		Buddy_Value (p, cbuf, sizeof cbuf);
		// `color` doubles as a BOOLEAN.  False locks the colour: the buddy is never
		// recoloured and the menu's Colour row stops responding -- what a buddy with
		// hand-drawn art wants, since the marine green->X remap only smears it.
		// Spending "0" on the lock costs nothing: index 0 is Green, whose remap table is
		// the identity, so `color 0` was never distinguishable from saying nothing.
		if (!strcasecmp (cbuf, "0")      || !strcasecmp (cbuf, "false")
		 || !strcasecmp (cbuf, "off")    || !strcasecmp (cbuf, "no")
		 || !strcasecmp (cbuf, "locked") || !strcasecmp (cbuf, "fixed"))
		    cur.colorlock = 1;
		else if (!strcasecmp (cbuf, "1")  || !strcasecmp (cbuf, "true")
		      || !strcasecmp (cbuf, "on") || !strcasecmp (cbuf, "yes"))
		    cur.colorlock = 0;			// explicitly selectable (the default anyway)
		else
		{
		    ci = Buddy_ColorIndex (cbuf);			// name -> index
		    if (ci < 0 && (cbuf[0] >= '0' && cbuf[0] <= '9')) ci = atoi (cbuf);
		    if (ci >= 0 && ci < V_BuddyColorCount ()) cur.color = ci;
		}
	    }
	    else
	    {
		char v[32]; Buddy_Value (p, v, sizeof v);
		int iv = atoi (v);
		if      (!strcmp(key,"health") || !strcmp(key,"hp"))	cur.health = iv;
		else if (!strcmp(key,"speed"))				cur.speed = iv;
		else if (!strcmp(key,"radius"))				cur.radius = iv;
		else if (!strcmp(key,"height"))				cur.height = iv;
		else if (!strcmp(key,"mass"))				cur.mass = iv;
		else if (!strcmp(key,"painchance"))			cur.painchance = iv;
		else if (!strcmp(key,"reactiontime")
		      || !strcmp(key,"reaction"))			cur.reactiontime = iv;
		else if (!strcmp(key,"ednum") || !strcmp(key,"doomednum"))cur.ednum = iv;
		else if (!strcmp(key,"damagescale")
		      || !strcmp(key,"damage"))			cur.damagescale = iv;
	    }
	}
    }
    // a record left open without a closing brace still counts
    if (inrec && cur.have_any) Buddy_Register (&cur);
}

// ---------------------------------------------------------------------------
// Entry point: seed the Marine, then parse every BUDDYDEF lump.
// ---------------------------------------------------------------------------
void P_Buddy_LoadDefs (void)
{
    int i;

    nroster = 0;
    // slot 0 -- the built-in player-2 marine buddy (files/p_ai_coop.c)
    memset (&roster[0], 0, sizeof roster[0]);
    strcpy (roster[0].name, "Marine");
    strcpy (roster[0].desc, "Standard-issue AI marine. Hunts monsters, guards "
			    "you and revives the fallen. The default buddy.");
    strcpy (roster[0].ranged, "hitscan");	// fires the player weapons
    strcpy (roster[0].melee, "punch");
    strcpy (roster[0].monster, "marine");
    strcpy (roster[0].special, "Revives downed marines, seeks health when hurt, "
			       "orderable (come/wait/attack), has its own HUD.");
    // The marine's named ability -- it already deploys Security Drones when it is under
    // heavy fire / surrounded / ammo-capped (P_AICoop_MaybeSpawnDrone, p_secdrone.c).
    strcpy (roster[0].ability, "drone");
    roster[0].spritenum    = SPR_PLAY;
    roster[0].color        = -1;	// no declared default -> menu uses Green
    roster[0].health       = 100;	// a real player marine
    roster[0].speed        = 25;
    roster[0].radius       = 16;
    roster[0].height       = 56;
    roster[0].mass         = 100;
    roster[0].painchance   = 255;
    roster[0].reactiontime = 0;
    nroster = 1;

    for (i = 0; i < numlumps; i++)
    {
	if (strncasecmp (lumpinfo[i].name, "BUDDYDEF", 8) != 0) continue;
	{
	    int len = W_LumpLength (i);
	    char* raw = (char*) W_CacheLumpNum (i, PU_STATIC);
	    if (raw && len > 0) Buddy_ParseText (raw, len);
	    Z_ChangeTag (raw, PU_CACHE);
	}
    }

    if (nroster > 1)
	printf ("P_Buddy_LoadDefs: %d modder buddy(ies) available.\n", nroster - 1);
}

// ---------------------------------------------------------------------------
// Thinker-list access, for the ability scans below.
// ---------------------------------------------------------------------------
extern thinker_t	thinkercap;
extern void		P_MobjThinker (mobj_t*);

// ---------------------------------------------------------------------------
// Special abilities (BUDDYDEF `ability`), run once per tic from P_Ticker for the live
// mobj companion.  The MARINE's "drone" is not driven from here: it already runs inside
// the marine bot (P_AICoop_MaybeSpawnDrone, p_secdrone.c), which needs its player_t.
//
// Both abilities are playsim state, so they only ever use the game RNG and gametic --
// nothing here reads wall-clock time.
// ---------------------------------------------------------------------------
#define BA_BAG_PERIOD		(4*TICRATE)		// one flechette every 4 s
#define BA_BAG_RANGE		(768*FRACUNIT)		// ...at an enemy no further than this
#define BA_BAG_SPEED		(12*FRACUNIT)		// ground speed of the lob (Hexen's ThrowingBomb)
#define BA_BAG_MAXFLIGHT	60			// tics: cap the arc so a far shot is not a mortar
#define BA_DRONE_PERIOD		(20*TICRATE)		// at most one drone per 20 s
#define BA_DRONE_RANGE		(1024*FRACUNIT)		// ...and only with an enemy this close
#define BA_TURRET_PERIOD	(30*TICRATE)		// at most one turret per 30 s
#define BA_TURRET_RANGE		(1024*FRACUNIT)
#define BA_TURRET_THROW		(18*FRACUNIT)		// same toss as the player's (p_turret.c)
#define BA_TURRET_ARC		(6*FRACUNIT)

extern void	P_DamageMobj (mobj_t*, mobj_t*, mobj_t*, int);

// A live enemy monster (not another ally) within `range` of mo?
static mobj_t* Buddy_EnemyWithin (mobj_t* mo, fixed_t range)
{
    thinker_t* th;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t* e;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	e = (mobj_t*)th;
	if (e == mo || e->health <= 0)		continue;
	if (!(e->flags & MF_COUNTKILL))		continue;
	if (e->flags & (MF_FRIEND|MF_CORPSE))	continue;
	if (!(e->flags & MF_SHOOTABLE))		continue;
	if (P_AproxDistance (e->x - mo->x, e->y - mo->y) <= range)
	    return e;
    }
    return NULL;
}

// poisonbag: lob Hexen's Flechette (the ArtiPoisonBag) at the nearest visible enemy.
// The thrown bag is the REAL artifact actor from files/hexen.c -- MT_XPOISONBAG keeps its
// own fuse and then runs A_PoisonBagInit, which pops the lingering MT_XPOISONCLOUD gas
// (A_PoisonBagDamage ticks POISON damage in it).  So there is no new damage code here:
// all this does is aim the bag and credit the kills.  Hexen_Init() runs unconditionally
// in D_DoomMain, so the actor exists in Doom too; its PSBG* art must be in a loaded WAD.
static void Buddy_ThrowPoisonBag (mobj_t* mo)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    mobj_t*	bag;
    fixed_t	bestd = 0;
    angle_t	ang;
    unsigned	fine;

    // Nearest VISIBLE enemy: the bag flies in a straight line and ignores the blockmap,
    // so without the sight check the buddy would happily lob it through a wall.
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	e;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	e = (mobj_t*)th;
	if (e == mo || e->health <= 0)		continue;
	if (!(e->flags & MF_COUNTKILL))		continue;
	if (e->flags & (MF_FRIEND|MF_CORPSE))	continue;
	if (!(e->flags & MF_SHOOTABLE))		continue;
	d = P_AproxDistance (e->x - mo->x, e->y - mo->y);
	if (d > BA_BAG_RANGE)			continue;
	if (best && d >= bestd)			continue;
	if (!P_CheckSight (mo, e))		continue;
	best = e; bestd = d;
    }
    if (!best)
	return;

    ang  = R_PointToAngle2 (mo->x, mo->y, best->x, best->y);
    fine = ang >> ANGLETOFINESHIFT;
    bag  = P_SpawnMobj (mo->x + FixedMul (mo->radius + 8*FRACUNIT, finecosine[fine]),
			mo->y + FixedMul (mo->radius + 8*FRACUNIT, finesine[fine]),
			mo->z + 32*FRACUNIT, MT_XPOISONBAG);
    if (!bag)
	return;
    bag->target = mo;		// the poison kills belong to the buddy, not to nobody
    bag->angle  = ang;

    // Turn THIS bag into a thrown missile.  Per-instance flags only: the mobjinfo keeps
    // MF_NOGRAVITY and no MF_MISSILE, so the Cleric's artifact (p_inv_heretic.c) still
    // drops a bag at your feet that fuses on a timer, exactly as in Hexen.  The thrown
    // one instead falls, and bursts on contact -- P_XYMovement explodes it against a wall
    // or a monster, P_ZMovement against the floor (p_mobj.c:353), and both routes land in
    // the deathstate wired up in files/hexen.c, whose A_PoisonBagInit pops the cloud.
    bag->flags &= ~MF_NOGRAVITY;
    bag->flags |=  MF_MISSILE | MF_DROPOFF;

    {   // Ballistic lob: fly at a fixed ground speed and climb just enough that gravity
	// (GRAVITY per tic) drops it onto the target after t tics --  vz = dz/t + g*t/2.
	fixed_t	dz;
	int	t;

	t = FixedDiv (bestd, BA_BAG_SPEED) >> FRACBITS;		// tics of flight
	if (t < 1)			t = 1;
	if (t > BA_BAG_MAXFLIGHT)	t = BA_BAG_MAXFLIGHT;

	dz = (best->z + (best->height >> 1)) - bag->z;		// aim at centre mass
	bag->momx = FixedMul (BA_BAG_SPEED, finecosine[fine]);
	bag->momy = FixedMul (BA_BAG_SPEED, finesine[fine]);
	bag->momz = dz / t + (t * GRAVITY) / 2;
    }
}
// drone: deploy a friendly Security Drone (the marine's signature power) when enemies
// are about and none of ours is already out.  Free for an mobj buddy -- it has no ammo
// pool to spend, unlike the marine's version.
static void Buddy_DeployDrone (mobj_t* mo)
{
    thinker_t*	th;
    mobj_t*	d;
    angle_t	ang;
    unsigned	fine;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)	// one at a time
    {
	mobj_t* o;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	o = (mobj_t*)th;
	if (o->type == MT_SECDRONE && o->health > 0)
	    return;
    }
    if (!Buddy_EnemyWithin (mo, BA_DRONE_RANGE))
	return;

    ang  = mo->angle;
    fine = ang >> ANGLETOFINESHIFT;
    d = P_SpawnMobj (mo->x + FixedMul (mo->radius + 32*FRACUNIT, finecosine[fine]),
		     mo->y + FixedMul (mo->radius + 32*FRACUNIT, finesine[fine]),
		     mo->z + 48*FRACUNIT, MT_SECDRONE);
    if (!d)
	return;
    d->angle  = ang;
    d->flags |= MF_FRIEND;
    players[consoleplayer].message = "[Buddy] Deploying security drone!";
}

// lichling: the Heretic counterpart of the drone -- a little floating Lich the buddy
// summons (files/heretic_lichling.c; its look/chase come from the shared companion AI
// in p_companion.c).  Same gate and cap as the drone, one at a time.
static void Buddy_SummonLichling (mobj_t* mo)
{
    thinker_t*	th;
    mobj_t*	l;
    angle_t	ang;
    unsigned	fine;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)	// one at a time
    {
	mobj_t* o;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	o = (mobj_t*)th;
	if (o->type == MT_LICHLING && o->health > 0)
	    return;
    }
    if (!Buddy_EnemyWithin (mo, BA_DRONE_RANGE))
	return;

    ang  = mo->angle;
    fine = ang >> ANGLETOFINESHIFT;
    l = P_SpawnMobj (mo->x + FixedMul (mo->radius + 32*FRACUNIT, finecosine[fine]),
		     mo->y + FixedMul (mo->radius + 32*FRACUNIT, finesine[fine]),
		     mo->z + 48*FRACUNIT, MT_LICHLING);
    if (!l)
	return;
    l->angle  = ang;
    l->flags |= MF_FRIEND;
    players[consoleplayer].message = "[Buddy] Summoning a lichling!";
}

// stalker: the Strife counterpart -- a four-legged Stalker that walks the floor and
// fires chaingunner-strength bursts (files/strife_stalker.c).  Same gate and cap as the
// drone/lichling.  Spawned at ground level, not floating: it is a walker.
static void Buddy_SummonStalker (mobj_t* mo)
{
    thinker_t*	th;
    mobj_t*	l;
    angle_t	ang;
    unsigned	fine;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)	// one at a time
    {
	mobj_t* o;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	o = (mobj_t*)th;
	if (o->type == MT_STALKERBUDDY && o->health > 0)
	    return;
    }
    if (!Buddy_EnemyWithin (mo, BA_DRONE_RANGE))
	return;

    ang  = mo->angle;
    fine = ang >> ANGLETOFINESHIFT;
    l = P_SpawnMobj (mo->x + FixedMul (mo->radius + 40*FRACUNIT, finecosine[fine]),
		     mo->y + FixedMul (mo->radius + 40*FRACUNIT, finesine[fine]),
		     mo->z, MT_STALKERBUDDY);
    if (!l)
	return;
    l->angle  = ang;
    l->flags |= MF_FRIEND;
    players[consoleplayer].message = "[Buddy] Releasing a Stalker!";
}

// turret: toss out a sentry turret exactly like the player's `key_turret` deploy
// (p_turret.c P_TurretDeploy) -- MT_TURRET, spawned at the buddy and nudged forward so a
// wall can't swallow it, then thrown with a little arc.  No ammo cost: an mobj buddy has
// no inventory to spend, so it is rate-limited and capped at one turret instead.
static void Buddy_DeployTurret (mobj_t* mo)
{
    thinker_t*	th;
    mobj_t*	t;
    angle_t	ang;
    unsigned	fine;
    fixed_t	dist, x, y, z;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)	// one at a time
    {
	mobj_t* o;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	o = (mobj_t*)th;
	if (o->type == MT_TURRET && o->health > 0)
	    return;
    }
    if (!Buddy_EnemyWithin (mo, BA_TURRET_RANGE))
	return;

    ang  = mo->angle;
    fine = ang >> ANGLETOFINESHIFT;
    dist = mo->radius + 24*FRACUNIT;
    x    = mo->x + FixedMul (dist, finecosine[fine]);
    y    = mo->y + FixedMul (dist, finesine[fine]);
    z    = mo->z + 24*FRACUNIT;

    t = P_SpawnMobj (mo->x, mo->y, z, MT_TURRET);
    if (!t)
	return;
    t->height = 16*FRACUNIT;			// low flight profile: fits window openings (p_turret.c)
    P_TryMove (t, x, y);			// a solid wall still stops it (ledges/windows don't)

    t->angle  = ang;
    t->target = NULL;
    t->momx   = FixedMul (BA_TURRET_THROW, finecosine[fine]);
    t->momy   = FixedMul (BA_TURRET_THROW, finesine[fine]);
    t->momz   = BA_TURRET_ARC;

    S_StartSound (t, sfx_itemup);
    players[consoleplayer].message = "[Buddy] Turret deployed!";
}

// The buddy's body: player 2's mobj.  The ability code below takes a plain mobj_t*
// and never looks at ->player, so it works unchanged now that the body is a player.
static mobj_t* Buddy_Body (void)
{
    int slot = P_AICoop_Slot ();
    if (slot < 0 || slot >= MAXPLAYERS || !playeringame[slot]) return NULL;
    return players[slot].mo;
}

// BUDDYDEF `damagescale`, in percent, for the mobj that dealt the damage -- 100 for
// anything that is not the selected buddy's body.  P_DamageMobj calls this.
int P_Buddy_DamageScale (mobj_t* source)
{
    mobj_t* body;
    if (!source || buddy_select <= 0 || buddy_select >= nroster) return 100;
    body = Buddy_Body ();
    if (!body || source != body) return 100;
    return roster[buddy_select].damagescale > 0 ? roster[buddy_select].damagescale : 100;
}

void P_Buddy_AbilityTicker (void)
{
    mobj_t*	mo;
    int		ab;

    if (vanilla_mode || netgame || demoplayback)
	return;
    if (buddy_select <= 0)		// the Marine's own drone runs in the bot (p_secdrone.c)
	return;
    if (!(mo = Buddy_Body ()) || mo->health <= 0)
	return;
    // Short warm-up: the cadences below are gametic-phased, so without this a level that
    // happens to start on a period boundary would see the buddy deploy on tic 0, before
    // the player has even moved.
    if (leveltime < 3*TICRATE)
	return;

    ab = Buddy_AbilityId (P_Buddy_Ability (buddy_select));
    switch (ab)
    {
      case BA_POISONBAG:
	if (!(gametic % BA_BAG_PERIOD))
	    Buddy_ThrowPoisonBag (mo);
	break;

      case BA_DRONE:
	if (!(gametic % BA_DRONE_PERIOD))
	    Buddy_DeployDrone (mo);
	break;

      case BA_TURRET:
	if (!(gametic % BA_TURRET_PERIOD))
	    Buddy_DeployTurret (mo);
	break;

      case BA_LICHLING:
	if (!(gametic % BA_DRONE_PERIOD))
	    Buddy_SummonLichling (mo);
	break;

      case BA_STALKER:
	if (!(gametic % BA_DRONE_PERIOD))
	    Buddy_SummonStalker (mo);
	break;

      default:
	break;
    }
}
