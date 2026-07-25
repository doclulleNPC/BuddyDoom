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
//	      seesound    FRANKN          # DS-lump suffix -> dsfrankn
//	      painsound   FRANKN
//	      deathsound  FRANKN
//	      activesound FRANKN
//	      ednum       30001           # optional DoomEd number for map placement
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
#include "m_fixed.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_local.h"			// MF_*, P_SpawnMobj, P_SetMobjState, P_TryMove, P_CheckPosition
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

// ---------------------------------------------------------------------------
// Roster (slot 0 = built-in Marine, always present).
// ---------------------------------------------------------------------------
#define MAXBUDDIES	24

typedef struct
{
    char	name[40];
    char	desc[160];
    int		spritenum;	// preview sprite
    int		mobjtype;	// -1 for the built-in Marine
} buddyrec_t;

static buddyrec_t	roster[MAXBUDDIES];
static int		nroster = 0;

int         P_Buddy_Count  (void)          { return nroster; }
const char* P_Buddy_Name   (int s)         { return (s >= 0 && s < nroster) ? roster[s].name : ""; }
const char* P_Buddy_Desc   (int s)         { return (s >= 0 && s < nroster) ? roster[s].desc : ""; }
int         P_Buddy_Sprite (int s)         { return (s >= 0 && s < nroster) ? roster[s].spritenum : SPR_PLAY; }
static int  Buddy_MobjType (int s)         { return (s >= 0 && s < nroster) ? roster[s].mobjtype : -1; }

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

// Find or append a sound; `raw` is the DS-lump suffix (e.g. "FRANKN" -> dsfrankn).
static int Buddy_RegSound (const char* raw)
{
    char nm[16];
    int i, n;
    if (!raw || !*raw) return 0;			// sfx_None
    for (n = 0; raw[n] && n < 15; n++)
	nm[n] = tolower((unsigned char)raw[n]);
    nm[n] = 0;

    for (i = 1; i < num_sfx; i++)
	if (S_sfx[i].name && !strcasecmp (S_sfx[i].name, nm))
	    return i;

    {   // append
	int idx = num_sfx;
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
    int		health, speed, radius, height, mass, painchance, ednum;
    boolean	have_any;
} buddyparse_t;

static void Buddy_Defaults (buddyparse_t* b)
{
    memset (b, 0, sizeof *b);
    strcpy (b->name, "Buddy");
    strcpy (b->sprite, "PLAY");
    strcpy (b->attack, "melee");
    b->health = 200; b->speed = 8; b->radius = 20; b->height = 56;
    b->mass = 100;   b->painchance = 120; b->ednum = -1;
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
    m->reactiontime = 8;    m->attacksound = 0;
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

    // roster entry
    {
	buddyrec_t* r = &roster[nroster++];
	strncpy (r->name, b->name, sizeof r->name - 1);
	strncpy (r->desc, b->desc, sizeof r->desc - 1);
	r->name[sizeof r->name - 1] = 0;
	r->desc[sizeof r->desc - 1] = 0;
	r->spritenum = spr;
	r->mobjtype  = mt;
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
    strcpy (roster[0].name, "Marine");
    strcpy (roster[0].desc, "Standard-issue AI marine. Hunts monsters, guards "
			    "you and revives the fallen. The default buddy.");
    roster[0].spritenum = SPR_PLAY;
    roster[0].mobjtype  = -1;
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
}
