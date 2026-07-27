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
//	      attack      baron           # baron|imp|poss|spos|cpos|sarg|head|skel|fatt|bspi|melee|none
//	      seesound    FRANKN          # sound-lump name (FRANKN, or DSFRANKN -- both work)
//	      painsound   FRANKN
//	      deathsound  FRANKN
//	      activesound FRANKN
//	      ednum       30001           # optional DoomEd number for map placement
//	      special     "Tanky bruiser" # free-text blurb for the Buddy select screen
//	      ability     poisoncloud     # NAMED power: none | drone | poisoncloud | turret
//	    }
//
//	Sprite frames follow the standard DOOM monster convention (like the FRAN
//	sheet): A-B idle, A-D walk, E-G attack, H pain, I-O death.  BuddyDoom builds
//	all 29 states from that; the modder only supplies properties.
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
#include "p_local.h"			// MF_*, P_SpawnMobj, P_SetMobjState, P_TryMove, P_CheckPosition
#include "r_main.h"			// R_PointToAngle2 (companion bearing for "where")
#include "p_ai_coop.h"			// P_AICoop_Slot
#include "p_buddydef.h"

// engine tables (grown by the DSDHacked API)
extern state_t*		states;
extern mobjinfo_t*	mobjinfo;
extern char**		sprnames;
extern sfxinfo_t*	S_sfx;
extern int		num_states, num_mobjtypes, num_sprites, num_sfx;

extern void dsdh_EnsureStatesCapacity   (int);
extern void dsdh_EnsureMobjInfoCapacity (int);
extern void dsdh_EnsureSpritesCapacity  (int);
extern void dsdh_EnsureSFXCapacity      (int);

// action functions we wire into buddy states (all in p_enemy.c)
extern void A_BuddyLook (mobj_t*);
extern void A_BuddyChase (mobj_t*);
extern void A_FaceTarget (mobj_t*);
extern void A_Pain (mobj_t*);
extern void A_Scream (mobj_t*);
extern void A_Fall (mobj_t*);
extern void A_PosAttack (mobj_t*);
extern void A_SPosAttack (mobj_t*);
extern void A_CPosAttack (mobj_t*);
extern void A_TroopAttack (mobj_t*);
extern void A_SargAttack (mobj_t*);
extern void A_HeadAttack (mobj_t*);
extern void A_BruisAttack (mobj_t*);
extern void A_SkelMissile (mobj_t*);
extern void A_FatAttack1 (mobj_t*);
extern void A_BspiAttack (mobj_t*);

extern mobj_t*	P_SpawnMobj (fixed_t, fixed_t, fixed_t, mobjtype_t);
extern void	P_RemoveMobj (mobj_t*);

// Buddy player-colour (v_png.c / r_things.c / m_menu.c)
extern int		buddy_color;			// active colour index (Buddy menu, config)
extern int		V_BuddyColorCount (void);
extern const char*	V_BuddyColorName  (int);
extern const byte*	V_BuddyColorTable (int);
extern void		R_SetBuddyColor (mobj_t*, const byte*);

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
    int		mobjtype;	// -1 for the built-in Marine
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
static int  Buddy_MobjType (int s)         { return (s >= 0 && s < nroster) ? roster[s].mobjtype : -1; }

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

// Mobjtype of a modder buddy whose name starts with `s` (case-insensitive), or -1.
// Lets the console `summon <buddyname>` (e.g. "summon frank") find BUDDYDEF buddies.
int P_Buddy_TypeByName (const char* s)
{
    int i, n;
    if (!s || !*s) return -1;
    n = (int) strlen (s);
    for (i = 1; i < nroster; i++)		// slot 0 is the built-in Marine (no mobjtype)
	if (roster[i].mobjtype >= 0 && !strncasecmp (roster[i].name, s, n))
	    return roster[i].mobjtype;
    return -1;
}

// ---------------------------------------------------------------------------
// Attack style -> codepointer.  All referenced projectiles (bruiser/troop/etc.)
// are built-in mobjtypes, so nothing extra needs registering.
// ---------------------------------------------------------------------------
static void* Buddy_AttackPtr (const char* a)
{
    if (!a || !*a || !strcasecmp(a,"none"))				return NULL;
    if (!strcasecmp(a,"baron") || !strcasecmp(a,"bruiser")
     || !strcasecmp(a,"hellknight") || !strcasecmp(a,"knight"))		return (void*)A_BruisAttack;
    if (!strcasecmp(a,"imp") || !strcasecmp(a,"troop"))			return (void*)A_TroopAttack;
    if (!strcasecmp(a,"poss") || !strcasecmp(a,"zombie")
     || !strcasecmp(a,"pistol") || !strcasecmp(a,"zombieman"))		return (void*)A_PosAttack;
    if (!strcasecmp(a,"spos") || !strcasecmp(a,"shotgun")
     || !strcasecmp(a,"shotgunguy"))					return (void*)A_SPosAttack;
    if (!strcasecmp(a,"cpos") || !strcasecmp(a,"chaingun")
     || !strcasecmp(a,"chaingunner"))					return (void*)A_CPosAttack;
    if (!strcasecmp(a,"sarg") || !strcasecmp(a,"demon")
     || !strcasecmp(a,"melee") || !strcasecmp(a,"bite"))		return (void*)A_SargAttack;
    if (!strcasecmp(a,"head") || !strcasecmp(a,"caco")
     || !strcasecmp(a,"cacodemon"))					return (void*)A_HeadAttack;
    if (!strcasecmp(a,"skel") || !strcasecmp(a,"revenant"))		return (void*)A_SkelMissile;
    if (!strcasecmp(a,"fatt") || !strcasecmp(a,"mancubus"))		return (void*)A_FatAttack1;
    if (!strcasecmp(a,"bspi") || !strcasecmp(a,"arachnotron"))		return (void*)A_BspiAttack;
    return (void*)A_SargAttack;						// unknown -> harmless melee
}

// ---------------------------------------------------------------------------
// Sprite / sound registration (DSDHacked table growth).
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

// Find or append a sound.  `raw` is simply the name of the sound lump in the WAD:
// "FRANKN" plays a FRANKN lump, and -- because DOOM's own sounds are all prefixed --
// a DSFRANKN lump just as well (i_sound.c I_SfxLumpFor tries "ds"+name first, then
// the bare name).  So the modder writes whatever their lump is actually called.
static int Buddy_RegSound (const char* raw)
{
    char nm[16];
    int i, n;
    if (!raw || !*raw) return 0;			// sfx_None
    for (n = 0; raw[n] && n < 8; n++)			// lump names are 8 bytes
	nm[n] = tolower((unsigned char)raw[n]);
    nm[n] = 0;

    for (i = 1; i < num_sfx; i++)
	if (S_sfx[i].name && !strcasecmp (S_sfx[i].name, nm))
	    return i;

    {   // append
	int idx = num_sfx;
	char ds[16];
	sprintf (ds, "ds%s", nm);			// n<=8, so <=10 chars
	if (W_CheckNumForName (nm) < 0 && (n > 6 || W_CheckNumForName (ds) < 0))
	    printf ("BUDDYDEF: sound \"%s\" -- no %s or DS%s lump in the loaded WADs "
		    "(will be silent)\n", raw, nm, nm);
	dsdh_EnsureSFXCapacity (idx);
	S_sfx[idx].name = strdup (nm);
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

static void ST (int s, int frame, int tics, void* act, int next)
{
    states[s].frame       = frame;
    states[s].tics        = tics;
    states[s].action.acp1 = (actionf_p1)act;
    states[s].nextstate   = next;
    states[s].misc1 = states[s].misc2 = 0;
}

// Turn a completed record into a live mobjtype + states + roster entry.
static void Buddy_Register (buddyparse_t* b)
{
    int spr, mt, B, i;
    void* atk;
    mobjinfo_t* m;

    if (nroster >= MAXBUDDIES) return;

    spr = Buddy_RegSprite (b->sprite);
    if (spr < 0)
    {
	printf ("Buddy: '%s' skipped -- sprite %.4s not found in WADs.\n", b->name, b->sprite);
	return;
    }

    // 29 contiguous states
    B = num_states;
    dsdh_EnsureStatesCapacity (B + 28);
    for (i = 0; i < 29; i++) states[B+i].sprite = spr;

    atk = Buddy_AttackPtr (b->attack);

    // Spawn (idle): A B, loop -- A_BuddyLook acquires an enemy, then See takes over
    ST (B+0,  0, 10, (void*)A_BuddyLook,  B+1);
    ST (B+1,  1, 10, (void*)A_BuddyLook,  B+0);
    // See (walk AABBCCDD): A_BuddyChase fights nearest enemy, else follows the human
    ST (B+2,  0,  3, (void*)A_BuddyChase, B+3);
    ST (B+3,  0,  3, (void*)A_BuddyChase, B+4);
    ST (B+4,  1,  3, (void*)A_BuddyChase, B+5);
    ST (B+5,  1,  3, (void*)A_BuddyChase, B+6);
    ST (B+6,  2,  3, (void*)A_BuddyChase, B+7);
    ST (B+7,  2,  3, (void*)A_BuddyChase, B+8);
    ST (B+8,  3,  3, (void*)A_BuddyChase, B+9);
    ST (B+9,  3,  3, (void*)A_BuddyChase, B+2);
    // Missile (attack E F G)
    ST (B+10, 4,  8, (void*)A_FaceTarget, B+11);
    ST (B+11, 5,  8, (void*)A_FaceTarget, B+12);
    ST (B+12, 6,  8, atk,                 B+2);
    // Pain (H)
    ST (B+13, 7,  3, NULL,                B+14);
    ST (B+14, 7,  3, (void*)A_Pain,       B+2);
    // Death (I..O)
    ST (B+15, 8,  8, NULL,                B+16);
    ST (B+16, 9,  8, (void*)A_Scream,     B+17);
    ST (B+17, 10, 8, NULL,                B+18);
    ST (B+18, 11, 8, (void*)A_Fall,       B+19);
    ST (B+19, 12, 8, NULL,                B+20);
    ST (B+20, 13, 8, NULL,                B+21);
    ST (B+21, 14,-1, NULL,                S_NULL);
    // Raise (O..I) -- Arch-Vile-revivable
    ST (B+22, 14, 8, NULL,                B+23);
    ST (B+23, 13, 8, NULL,                B+24);
    ST (B+24, 12, 8, NULL,                B+25);
    ST (B+25, 11, 8, NULL,                B+26);
    ST (B+26, 10, 8, NULL,                B+27);
    ST (B+27, 9,  8, NULL,                B+28);
    ST (B+28, 8,  8, NULL,                B+2);

    // mobjtype
    mt = num_mobjtypes;
    dsdh_EnsureMobjInfoCapacity (mt);
    m = &mobjinfo[mt];
    m->doomednum   = b->ednum;
    m->spawnstate  = B+0;   m->spawnhealth = b->health;
    m->seestate    = B+2;   m->seesound    = Buddy_RegSound (b->seesnd);
    m->reactiontime = b->reactiontime;   m->attacksound = 0;
    m->painstate   = B+13;  m->painchance  = b->painchance;
    m->painsound   = Buddy_RegSound (b->painsnd);
    m->meleestate  = 0;
    m->missilestate = atk ? B+10 : 0;			// "none" -> never attacks
    m->deathstate  = B+15;  m->xdeathstate = 0;
    m->deathsound  = Buddy_RegSound (b->deathsnd);
    m->speed       = b->speed;
    m->radius      = b->radius * FRACUNIT;
    m->height      = b->height * FRACUNIT;
    m->mass        = b->mass;
    m->damage      = 0;
    m->activesound = Buddy_RegSound (b->activesnd);
    // MF_FRIEND: player ally (hunts enemies, no friendly fire).  No MF_COUNTKILL --
    // a companion must not count toward the level's monster total.
    m->flags       = MF_SOLID | MF_SHOOTABLE | MF_FRIEND;
    m->raisestate  = B+22;

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
	r->mobjtype     = mt;
	r->color        = b->color;
	r->health       = b->health;
	r->speed        = b->speed;
	r->radius       = b->radius;
	r->height       = b->height;
	r->mass         = b->mass;
	r->painchance   = b->painchance;
	r->reactiontime = b->reactiontime;
    }
    printf ("Buddy: registered '%s' (thing %d, sprite %.4s, attack %s).\n",
	    b->name, mt, b->sprite, b->attack);
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
    roster[0].mobjtype     = -1;
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
// Level hook: make the selected mobj buddy your companion (replacing the marine).
// ---------------------------------------------------------------------------
extern int buddy_select;			// config: 0 = Marine, 1..N = roster index

// Standing orders for the mobj companion (console "come" / "wait"), see below.
static int	buddy_recall;			// gametic the "come" leash expires (0 = off)
static int	buddy_hold;			// "wait" toggle -- hold position

void P_Buddy_SpawnSelected (void)
{
    int slot = P_AICoop_Slot ();		// player index the marine occupies (1)
    int mt;
    mobj_t* b;

    if (vanilla_mode || netgame || demoplayback) return;	// never in purist / net / demo
    if (buddy_select <= 0) return;				// Marine chosen -> marine path already ran
    mt = Buddy_MobjType (buddy_select);
    if (mt < 0) return;						// invalid selection

    // Suppress the player-2 marine for this level: drop its mobj and disable its
    // slot (P_AICoop_BuildCmd early-outs when playeringame[slot] is false).
    if (slot >= 0 && slot < MAXPLAYERS)
    {
	if (players[slot].mo) { P_RemoveMobj (players[slot].mo); players[slot].mo = NULL; }
	playeringame[slot] = false;
    }

    if (!players[0].mo) return;					// no human yet -> nothing to escort
    b = P_SpawnMobj (players[0].mo->x, players[0].mo->y, players[0].mo->z, mt);
    b->angle  = players[0].mo->angle;
    b->target = NULL;
    P_SetMobjState (b, b->info->seestate);			// straight into follow/fight
    // Apply the chosen buddy colour to this companion's sprite (buddy_color is the
    // menu/config selection; a BUDDYDEF `color` only seeds the menu default).
    R_SetBuddyColor (b, V_BuddyColorTable (buddy_color));
    printf ("P_Buddy: '%s' is now your companion (thing %d).\n", P_Buddy_Name(buddy_select), mt);

    // If it materialised inside geometry, shove it to a nearby free spot.
    if (!P_CheckPosition (b, b->x, b->y))
    {
	static const int ox[8] = { 1, 1, 0, -1, -1, -1,  0,  1 };
	static const int oy[8] = { 0, 1, 1,  1,  0, -1, -1, -1 };
	fixed_t step = b->radius * 2 + 8*FRACUNIT;
	int k;
	for (k = 0; k < 8; k++)
	    if (P_TryMove (b, b->x + ox[k]*step, b->y + oy[k]*step))
		break;
    }

    buddy_recall = buddy_hold = 0;		// fresh level -> no standing orders
}

// ---------------------------------------------------------------------------
// The live companion, for everything that "knows about your buddy".
//
// The marine buddy is a PLAYER (slot 1), so the HUD, the automap marker and the
// console orders all keyed off playeringame[slot] -- which P_Buddy_SpawnSelected
// switches OFF for a BUDDYDEF buddy (it is an mobj, not a player).  Those systems
// ask here instead when the marine slot is empty.
//
// Found by scanning the thinker list for the selected buddy's mobjtype rather than
// by caching the pointer from the spawn: that survives a savegame load (which
// rebuilds every mobj) and can never dangle -- P_RemoveThinker only marks the
// thinker (p_tick.c), the memory is freed in P_RunThinkers, and a marked thinker no
// longer matches P_MobjThinker.  Cached per gametic so the per-frame HUD/automap
// callers don't re-walk the list.
// ---------------------------------------------------------------------------
extern thinker_t	thinkercap;
extern void		P_MobjThinker (mobj_t*);
extern mobj_t*		P_FriendNearestEnemy (mobj_t*);		// p_enemy.c

mobj_t* P_Buddy_Mobj (void)
{
    static mobj_t*	cache;
    static int		cachetic = -1;
    thinker_t*		th;
    int			mt;

    if (cachetic == gametic)
	return cache;
    cachetic = gametic;
    cache    = NULL;

    if (buddy_select <= 0 || vanilla_mode || netgame || demoplayback)
	return NULL;
    if ((mt = Buddy_MobjType (buddy_select)) < 0)
	return NULL;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t* mo;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	mo = (mobj_t*)th;
	if (mo->type == mt) { cache = mo; break; }	// corpses included -- the HUD says DOWN
    }
    return cache;
}

const char* P_Buddy_ActiveName (void)
{
    return (buddy_select > 0) ? P_Buddy_Name (buddy_select) : "";
}

int P_Buddy_MaxHealth (void)
{
    int mt = (buddy_select > 0) ? Buddy_MobjType (buddy_select) : -1;
    return (mt >= 0) ? mobjinfo[mt].spawnhealth : 0;
}

// ---------------------------------------------------------------------------
// Standing orders (console "come" / "wait").  A_BuddyChase asks for these each
// time it runs; both are plain gametic deadlines, so nothing is added to mobj_t
// and savegames are unaffected.  Like the marine's orders these are single-machine
// only (they would desync a netgame's lockstep), which P_Buddy_Mobj already gates.
// ---------------------------------------------------------------------------
#define BUDDY_RECALL_TICS	(8*TICRATE)

int P_Buddy_Recalled (void)	{ return buddy_recall && gametic < buddy_recall; }
int P_Buddy_Held (void)		{ return buddy_hold; }

// ------------------------------------------------------------------ commands
// Replies match the marine's: short strings starting with "[Buddy] ".

const char* P_Buddy_Report (void)
{
    static char		buf[160];
    static const char*	compass[8] = { "east","north-east","north","north-west",
				       "west","south-west","south","south-east" };
    mobj_t*	mo = P_Buddy_Mobj ();
    mobj_t*	pl = playeringame[consoleplayer] ? players[consoleplayer].mo : NULL;
    const char*	what;

    if (!mo)
	return "[Buddy] (no companion)";

    if      (mo->health <= 0)	what = "down";
    else if (buddy_hold)	what = "holding position";
    else if (P_Buddy_Recalled ())	what = "coming to you";
    else if (mo->target)	what = "fighting";
    else			what = "following you";

    if (pl)
    {
	int	units = (int)(P_AproxDistance (mo->x - pl->x, mo->y - pl->y) >> FRACBITS);
	angle_t	a     = R_PointToAngle2 (pl->x, pl->y, mo->x, mo->y);
	int	oct   = (int)((a + (1u<<28)) >> 29) & 7;
	snprintf (buf, sizeof buf, "[Buddy] %s: %d units to your %s, %d HP -- %s.",
		  P_Buddy_ActiveName (), units, compass[oct], mo->health, what);
    }
    else
	snprintf (buf, sizeof buf, "[Buddy] %s: %d HP -- %s.",
		  P_Buddy_ActiveName (), mo->health, what);
    return buf;
}

const char* P_Buddy_StatusReport (void)
{
    static char	buf[160];
    mobj_t*	mo  = P_Buddy_Mobj ();
    int		max = P_Buddy_MaxHealth ();

    if (!mo)
	return "[Buddy] (no companion)";
    if (mo->health <= 0)
	snprintf (buf, sizeof buf, "[Buddy] %s is down.", P_Buddy_ActiveName ());
    else
	snprintf (buf, sizeof buf, "[Buddy] %s: %d/%d HP, %s.", P_Buddy_ActiveName (),
		  mo->health, max ? max : mo->health,
		  mo->target ? "engaging a target" : "no target");
    return buf;
}

const char* P_Buddy_Summon (void)
{
    mobj_t* mo = P_Buddy_Mobj ();
    if (!mo || mo->health <= 0)	return "[Buddy] (no companion)";
    buddy_recall = gametic + BUDDY_RECALL_TICS;
    buddy_hold   = 0;
    mo->target   = NULL;			// break off the fight and pad back
    return "[Buddy] On my way!";
}

const char* P_Buddy_Wait (void)
{
    mobj_t* mo = P_Buddy_Mobj ();
    if (!mo || mo->health <= 0)	return "[Buddy] (no companion)";
    buddy_hold = !buddy_hold;
    if (buddy_hold) { buddy_recall = 0; mo->target = NULL; }
    return buddy_hold ? "[Buddy] Holding position." : "[Buddy] Moving out.";
}

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
#define BA_TURRET_THROW		(11*FRACUNIT)		// same toss as the player's (p_turret.c)
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
    P_TryMove (t, x, y);			// blocked by a wall -> stays at the buddy's feet

    t->angle  = ang;
    t->target = NULL;
    t->momx   = FixedMul (BA_TURRET_THROW, finecosine[fine]);
    t->momy   = FixedMul (BA_TURRET_THROW, finesine[fine]);
    t->momz   = BA_TURRET_ARC;

    S_StartSound (t, sfx_itemup);
    players[consoleplayer].message = "[Buddy] Turret deployed!";
}

void P_Buddy_AbilityTicker (void)
{
    mobj_t*	mo;
    int		ab;

    if (vanilla_mode || netgame || demoplayback)
	return;
    if (!(mo = P_Buddy_Mobj ()) || mo->health <= 0)
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

// One-button mode cycle (the key_buddy_mode bind, default right mouse button):
// fighting/following -> hold position, holding -> come back and follow.  Same shape as
// the marine's P_AICoop_ToggleMode.
const char* P_Buddy_ToggleMode (void)
{
    mobj_t* mo = P_Buddy_Mobj ();
    if (!mo || mo->health <= 0)	return "[Buddy] (no companion)";
    if (buddy_hold)
    {
	buddy_hold   = 0;
	buddy_recall = gametic + BUDDY_RECALL_TICS;	// run back to you, then tail along
	mo->target   = NULL;
	return "[Buddy] Following you.";
    }
    buddy_hold   = 1;
    buddy_recall = 0;
    mo->target   = NULL;
    return "[Buddy] Holding position.";
}

const char* P_Buddy_Attack (void)
{
    mobj_t* mo = P_Buddy_Mobj ();
    mobj_t* e;
    if (!mo || mo->health <= 0)	return "[Buddy] (no companion)";
    buddy_hold = buddy_recall = 0;
    if (!(e = P_FriendNearestEnemy (mo)))
	return "[Buddy] No targets around.";
    mo->target = e;
    P_SetMobjState (mo, mo->info->seestate);
    return "[Buddy] Attacking!";
}

// "buddyhome"/"buddytp" for the marine teleports it to its map spawn point; a mobj
// buddy has none (it spawns beside you), so this warps it back to your side -- which
// is what the command is actually used for when the companion gets stuck.
const char* P_Buddy_Warp (void)
{
    mobj_t* mo = P_Buddy_Mobj ();
    mobj_t* pl = playeringame[consoleplayer] ? players[consoleplayer].mo : NULL;
    if (!mo || mo->health <= 0)	return "[Buddy] (no companion)";
    if (!pl)			return "[Buddy] (nobody to warp to)";
    if (!P_TryMove (mo, pl->x, pl->y))
    {
	static const int ox[8] = { 1, 1, 0, -1, -1, -1,  0,  1 };
	static const int oy[8] = { 0, 1, 1,  1,  0, -1, -1, -1 };
	fixed_t step = mo->radius * 2 + 8*FRACUNIT;
	int k;
	for (k = 0; k < 8; k++)
	    if (P_TryMove (mo, pl->x + ox[k]*step, pl->y + oy[k]*step))
		break;
	if (k == 8) return "[Buddy] (no room next to you)";
    }
    return "[Buddy] Right behind you.";
}
