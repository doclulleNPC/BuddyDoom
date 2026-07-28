// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(H) Heretic sound table -- the native Heretic SFX lump names for the additive
//	Heretic monsters (files/heretic.c).  Split out of the DOOM sounds.c to keep each
//	game's sound data in its own file; the sfx_h_* enum slots still live in sounds.h.
//
//	Names are the REAL Heretic IWAD lump names (from GZDoom's game-heretic/sndinfo.txt),
//	resolved by I_SfxLumpFor -- which tries "ds"+name then the bare name -- so the same
//	lumps play whether they sit in heretic.wad or a repacked hereticstuff.wad.  The
//	table is written straight into the sfx_h_* slots (contiguous, in enum order) at
//	startup by Sounds_Heretic_Init, called from D_DoomMain before I_InitSound precaches.
//
//-----------------------------------------------------------------------------

#include "doomtype.h"
#include "sounds.h"

// name + priority, in EXACT sfx_h_* enum order (sounds.h).  bstact is the first slot.
static const struct { char* name; int priority; } heretic_sfx[] =
{
    { "bstact", 120 }, { "bstatk", 70 }, { "bstdth", 70 }, { "bstpai", 96 }, { "bstsit", 98 },	// beast (weredragon)
    { "clkact", 120 }, { "clkatk", 70 }, { "clkdth", 70 }, { "clkpai", 96 }, { "clksit", 98 },	// clink (sabreclaw)
    { "hedact", 120 }, { "hedat1", 70 }, { "hedat2", 70 }, { "hedat3", 70 },			// iron lich
    { "heddth", 70 },  { "hedpai", 96 }, { "hedsit", 98 },
    { "impat1", 70 },  { "impat2", 70 }, { "impdth", 70 }, { "imppai", 96 }, { "impsit", 98 },	// himp (gargoyle)
    { "kgtat2", 70 },  { "kgtatk", 70 }, { "kgtdth", 70 }, { "kgtpai", 96 }, { "kgtsit", 98 },	// hknight (undead warrior)
    { "minact", 120 }, { "minat1", 70 }, { "minat2", 70 }, { "minat3", 70 },			// minotaur (maulotaur)
    { "mindth", 70 },  { "minpai", 96 }, { "minsit", 98 },
    { "mumat1", 70 },  { "mumat2", 70 }, { "mumdth", 70 }, { "mumhed", 70 },			// mummy (golem)
    { "mumpai", 96 },  { "mumsit", 98 },
    { "snkact", 120 }, { "snkatk", 70 }, { "snkdth", 70 }, { "snkpai", 96 }, { "snksit", 98 },	// snake (ophidian)
    { "sorzap", 70 },  { "sorsit", 98 }, { "soratk", 70 }, { "sorpai", 96 }, { "soract", 120 },	// sorcerer (d'sparil)
    { "sordexp", 70 },										// d'sparil death explosion
    { "wizact", 120 }, { "wizatk", 70 }, { "wizdth", 70 }, { "wizpai", 96 }, { "wizsit", 98 },	// wizard (disciple)
};

// Write the Heretic SFX names/priorities into their sfx_h_* slots (in enum order,
// starting at sfx_h_bstact).  Other fields left at their zero-init defaults except
// link/pitch/volume, which follow the DOOM table convention (no link).
void Sounds_Heretic_Init (void)
{
    int	n = (int)(sizeof heretic_sfx / sizeof heretic_sfx[0]);
    int	i;

    // guard against enum drift: only fill the contiguous sfx_h_* range we own
    if (n != (sfx_h_wizsit - sfx_h_bstact + 1))
	return;

    for (i = 0; i < n; i++)
    {
	sfxinfo_t* s = &S_sfx_builtin[sfx_h_bstact + i];
	s->name        = heretic_sfx[i].name;
	s->singularity = false;
	s->priority    = heretic_sfx[i].priority;
	s->link        = 0;
	s->pitch       = -1;
	s->volume      = -1;
    }
}


// (H) Heretic WEAPON sounds -- native lump names, in EXACT sfx_hw_* enum order
// (sounds.h), from gldhit.  Filled into their S_sfx_builtin slots at startup.
static const struct { char* name; int priority; } hweapon_sfx[] =
{
    { "gldhit", 118 }, { "stfhit", 32 },  { "stfpow", 118 }, { "bowsht", 118 },
    { "blssht", 118 }, { "blshit", 32 },  { "hrnsht", 118 }, { "hrnhit", 32 },
    { "phosht", 118 }, { "phohit", 32 },  { "lobsht", 118 }, { "lobhit", 32 },
    { "gntful", 118 }, { "gnthit", 118 }, { "gntpow", 118 }, { "wpnup",  32 },
};

void Sounds_HWeapons_Init (void)
{
    int	n = (int)(sizeof hweapon_sfx / sizeof hweapon_sfx[0]);
    int	i;

    if (n != (sfx_hw_wpnup - sfx_hw_gldhit + 1))	// guard against enum drift
	return;

    for (i = 0; i < n; i++)
    {
	sfxinfo_t* s = &S_sfx_builtin[sfx_hw_gldhit + i];
	s->name        = hweapon_sfx[i].name;
	s->singularity = false;
	s->priority    = hweapon_sfx[i].priority;
	s->link        = 0;
	s->pitch       = -1;
	s->volume      = -1;
    }
}
