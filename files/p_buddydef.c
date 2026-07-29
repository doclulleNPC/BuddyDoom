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
//	      ability     poisoncloud     # NAMED power: none | drone | poisoncloud | turret
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
#include "m_random.h"			// P_Random -- poisoncloud puff scatter (playsim RNG)
#include "tables.h"			// finesine/finecosine, ANGLETOFINESHIFT (drone placement)
#include "w_wad.h"
#include "z_zone.h"
#include "p_local.h"			// MF_*, P_SpawnMobj, P_TryMove, P_AproxDistance
#include "p_ai_coop.h"			// P_AICoop_Slot
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
    char	attack[24];	// attack-style name (for the stats panel)
    char	special[96];	// modder-supplied "special abilities" text
    char	ability[24];	// BUDDYDEF `ability`: the named special ABILITY the buddy
				// actually uses in play (none|drone|poisoncloud)
    int		spritenum;	// preview sprite
    int		color;		// declared default player-colour index, -1 = none (BUDDYDEF `color`)
    int		health, speed, radius, height, mass, painchance, reactiontime;
} buddyrec_t;

static buddyrec_t	roster[MAXBUDDIES];
static int		nroster = 0;

// ---------------------------------------------------------------------------
// Named special abilities (BUDDYDEF `ability`).  The blurb in `special` is just
// text for the select screen; THIS is the mechanic the buddy actually uses, run
// once per tic by P_Buddy_AbilityTicker.
// ---------------------------------------------------------------------------
enum { BA_NONE = 0, BA_DRONE, BA_POISONCLOUD, BA_TURRET, BA_NUM };

static const char* const buddy_ability_name[BA_NUM] =
{
    "none", "drone", "poisoncloud", "turret"
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

// Fill `out` with the buddy's stats (for the Buddy select screen).
void P_Buddy_GetStats (int s, buddystats_t* out)
{
    if (!out) return;
    if (s < 0 || s >= nroster)
    { memset (out, 0, sizeof *out); out->attack = out->special = out->ability = ""; return; }
    out->health       = roster[s].health;
    out->speed        = roster[s].speed;
    out->radius       = roster[s].radius;
    out->height       = roster[s].height;
    out->mass         = roster[s].mass;
    out->painchance   = roster[s].painchance;
    out->reactiontime = roster[s].reactiontime;
    out->attack       = roster[s].attack;
    out->special      = roster[s].special;
    out->ability      = roster[s].ability;
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
static boolean Buddy_SpritePresent (const char base[4])
{
    char n[9];
    memcpy (n, base, 4);
    n[4] = 'A'; n[5] = '1'; n[6] = 0;
    if (W_CheckNumForName (n) >= 0) return true;
    n[5] = '0';
    return W_CheckNumForName (n) >= 0;
}

// Find or append a 4-char sprite name; returns the spritenum (SPR_TNT1 on failure).
static int Buddy_RegSprite (const char* raw)
{
    char base[4];
    int i;
    for (i = 0; i < 4; i++)
	base[i] = raw[i] ? toupper((unsigned char)raw[i]) : ' ';

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
    char	attack[24];
    char	seesnd[16], painsnd[16], deathsnd[16], activesnd[16];
    char	special[96];
    char	ability[24];
    int		health, speed, radius, height, mass, painchance, reactiontime, ednum;
    int		color;		// player-colour index, -1 = none declared
    boolean	have_any;
} buddyparse_t;

static void Buddy_Defaults (buddyparse_t* b)
{
    memset (b, 0, sizeof *b);
    strcpy (b->name, "Buddy");
    strcpy (b->sprite, "PLAY");
    strcpy (b->attack, "melee");
    strcpy (b->ability, "none");
    b->health = 200; b->speed = 8; b->radius = 20; b->height = 56;
    b->mass = 100;   b->painchance = 120; b->reactiontime = 8; b->ednum = -1;
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
	strncpy (r->name, b->name, sizeof r->name - 1);
	strncpy (r->desc, b->desc, sizeof r->desc - 1);
	strncpy (r->attack, b->attack, sizeof r->attack - 1);
	strncpy (r->special, b->special, sizeof r->special - 1);
	strncpy (r->ability, b->ability, sizeof r->ability - 1);
	r->name[sizeof r->name - 1] = 0;
	r->desc[sizeof r->desc - 1] = 0;
	r->attack[sizeof r->attack - 1] = 0;
	r->special[sizeof r->special - 1] = 0;
	r->ability[sizeof r->ability - 1] = 0;
	// Unknown ability -> refuse it rather than pretending the buddy has a power.
	if (Buddy_AbilityId (r->ability) < 0)
	{
	    printf ("BUDDYDEF: '%s' has unknown ability \"%s\" -- ignored "
		    "(known: none, drone, poisoncloud, turret).\n", b->name, r->ability);
	    strcpy (r->ability, "none");
	}
	r->spritenum    = spr;
	r->color        = b->color;
	r->health       = b->health;
	r->speed        = b->speed;
	r->radius       = b->radius;
	r->height       = b->height;
	r->mass         = b->mass;
	r->painchance   = b->painchance;
	r->reactiontime = b->reactiontime;
    }
    printf ("Buddy: registered '%s' (roster slot %d, sprite %.4s).\n",
	    b->name, nroster - 1, b->sprite);
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
		ci = Buddy_ColorIndex (cbuf);			// name -> index
		if (ci < 0 && (cbuf[0] >= '0' && cbuf[0] <= '9')) ci = atoi (cbuf);	// numeric fallback
		if (ci >= 0 && ci < V_BuddyColorCount ()) cur.color = ci;
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
    strcpy (roster[0].attack, "hitscan");
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

    // Be honest about the half-built state: a modder buddy is listed and previewed, but
    // the body you actually get is still the Marine until the player-2 path lands.
    if (buddy_select > 0 && buddy_select < nroster)
	printf ("P_Buddy: '%s' selected -- BUDDYDEF buddies are being ported to the "
		"player-2 path; the Marine is your companion for now.\n",
		roster[buddy_select].name);
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
#define BA_POISON_PERIOD	(2*TICRATE)		// one cloud every 2 s
#define BA_POISON_RADIUS	(160*FRACUNIT)
#define BA_POISON_DAMAGE	4
#define BA_POISON_PUFFS		3
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

// poisoncloud: a puff of gas around the buddy that eats at every enemy standing in it.
// Friends, the player and corpses are untouched -- it is purely an anti-monster aura.
static void Buddy_PoisonCloud (mobj_t* mo)
{
    thinker_t*	th;
    int		i;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	e;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	e = (mobj_t*)th;
	if (e == mo || e->health <= 0)			continue;
	if (!(e->flags & MF_SHOOTABLE))			continue;
	if (e->flags & (MF_FRIEND|MF_CORPSE))		continue;
	if (!(e->flags & MF_COUNTKILL))			continue;	// monsters only
	if (e->player)					continue;	// never the humans
	if (P_AproxDistance (e->x - mo->x, e->y - mo->y) > BA_POISON_RADIUS) continue;
	if (abs (e->z - mo->z) > 64*FRACUNIT)		continue;	// same-ish floor
	P_DamageMobj (e, mo, mo, BA_POISON_DAMAGE);
    }

    // visible gas: a few smoke puffs drifting up around the buddy
    for (i = 0; i < BA_POISON_PUFFS; i++)
    {
	fixed_t	rx = ((P_Random () - 128) * (BA_POISON_RADIUS >> 8));
	fixed_t	ry = ((P_Random () - 128) * (BA_POISON_RADIUS >> 8));
	mobj_t*	s  = P_SpawnMobj (mo->x + rx, mo->y + ry,
				  mo->z + (P_Random () % 24)*FRACUNIT, MT_SMOKE);
	if (s) s->momz = FRACUNIT/2;
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
      case BA_POISONCLOUD:
	if (!(gametic % BA_POISON_PERIOD))
	    Buddy_PoisonCloud (mo);
	break;

      case BA_DRONE:
	if (!(gametic % BA_DRONE_PERIOD))
	    Buddy_DeployDrone (mo);
	break;

      case BA_TURRET:
	if (!(gametic % BA_TURRET_PERIOD))
	    Buddy_DeployTurret (mo);
	break;

      default:
	break;
    }
}
