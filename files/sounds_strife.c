// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(S) Strife sound table -- the native Strife IWAD SFX lump names for the additive
//	Strife actors (files/strife*.c).  Split out of the DOOM sounds.c to keep each
//	game's sound data in its own file; the sfx_s_* enum slots live in sounds.h
//	(strife_sfx.inc).
//
//	Names + priorities are transcribed from strife-ve / crispy-doom strife/sounds.c
//	(SOUND(name, priority)).  In Strife the lump name IS the sfx name (raw, no "ds"
//	prefix) -- I_SfxLumpFor resolves it by bare name in strife_mode.  Written straight
//	into the sfx_s_* slots (contiguous, in enum order) at startup by Sounds_Strife_Init,
//	called from D_DoomMain before I_InitSound precaches.
//
//-----------------------------------------------------------------------------

#include "doomtype.h"
#include "sounds.h"

// name + priority, in EXACT sfx_s_* enum order (sounds.h / strife_sfx.inc).  swish is first.
static const struct { char* name; int priority; } strife_sfx[] =
{
    { "swish", 64 }, { "meatht", 64 }, { "mtalht", 64 }, { "wpnup", 78 },
    { "rifle", 64 }, { "mislht", 64 }, { "barexp", 32 }, { "flburn", 64 },
    { "flidl", 118 }, { "agrsee", 98 }, { "plpain", 96 }, { "pcrush", 96 },
    { "pespna", 98 }, { "pespnb", 98 }, { "pespnc", 98 }, { "pespnd", 98 },
    { "agrdpn", 98 }, { "pldeth", 32 }, { "plxdth", 32 }, { "slop", 78 },
    { "rebdth", 98 }, { "agrdth", 98 }, { "lgfire", 211 }, { "smfire", 211 },
    { "alarm", 210 }, { "drlmto", 98 }, { "drlmtc", 98 }, { "drsmto", 98 },
    { "drsmtc", 98 }, { "drlwud", 98 }, { "drswud", 98 }, { "drston", 98 },
    { "bdopn", 98 }, { "bdcls", 98 }, { "swtchn", 78 }, { "swbolt", 98 },
    { "swscan", 98 }, { "yeah", 10 }, { "mask", 210 }, { "pstart", 100 },
    { "pstop", 100 }, { "itemup", 78 }, { "bglass", 200 }, { "wriver", 201 },
    { "wfall", 201 }, { "wdrip", 201 }, { "wsplsh", 95 }, { "rebact", 200 },
    { "agrac1", 98 }, { "agrac2", 98 }, { "agrac3", 98 }, { "agrac4", 98 },
    { "ambppl", 218 }, { "ambbar", 218 }, { "telept", 32 }, { "ratact", 99 },
    { "itmbk", 100 }, { "xbow", 99 }, { "burnme", 95 }, { "oof", 96 },
    { "wbrldt", 98 }, { "psdtha", 109 }, { "psdthb", 109 }, { "psdthc", 109 },
    { "rb2pn", 96 }, { "rb2dth", 32 }, { "rb2see", 98 }, { "rb2act", 98 },
    { "firxpl", 70 }, { "stnmov", 100 }, { "noway", 78 }, { "rlaunc", 64 },
    { "rflite", 65 }, { "radio", 60 }, { "pulchn", 98 }, { "swknob", 98 },
    { "keycrd", 98 }, { "swston", 98 }, { "sntsee", 98 }, { "sntdth", 98 },
    { "sntact", 98 }, { "pgrdat", 64 }, { "pgrsee", 90 }, { "pgrdpn", 96 },
    { "pgrdth", 32 }, { "pgract", 120 }, { "proton", 64 }, { "protfl", 64 },
    { "plasma", 64 }, { "dsrptr", 30 }, { "reavat", 64 }, { "revbld", 64 },
    { "revsee", 90 }, { "reavpn", 96 }, { "revdth", 32 }, { "revact", 120 },
    { "spisit", 90 }, { "spdwlk", 65 }, { "spidth", 32 }, { "spdatk", 32 },
    { "chant", 218 }, { "static", 32 }, { "chain", 70 }, { "tend", 100 },
    { "phoot", 32 }, { "explod", 32 }, { "sigil", 32 }, { "sglhit", 32 },
    { "siglup", 32 }, { "prgpn", 96 }, { "progac", 120 }, { "lorpn", 96 },
    { "lorsee", 90 }, { "difool", 32 }, { "inqdth", 32 }, { "inqact", 98 },
    { "inqsee", 90 }, { "inqjmp", 65 }, { "amaln1", 99 }, { "amaln2", 99 },
    { "amaln3", 99 }, { "amaln4", 99 }, { "amaln5", 99 }, { "amaln6", 99 },
    { "mnalse", 64 }, { "alnsee", 64 }, { "alnpn", 96 }, { "alnact", 120 },
    { "alndth", 32 }, { "mnaldt", 32 }, { "reactr", 31 }, { "airlck", 98 },
    { "drchno", 98 }, { "drchnc", 98 }, { "valve", 98 }, 
};

// Write the Strife SFX names/priorities into their sfx_s_* slots (in enum order,
// starting at sfx_s_swish).  Mirrors Sounds_Heretic_Init.
void Sounds_Strife_Init (void)
{
    int	n = (int)(sizeof strife_sfx / sizeof strife_sfx[0]);
    int	i;

    // guard against enum drift: only fill the contiguous sfx_s_* range we own
    if (n != (sfx_s_valve - sfx_s_swish + 1))
	return;

    for (i = 0; i < n; i++)
    {
	sfxinfo_t* s = &S_sfx_builtin[sfx_s_swish + i];
	s->name        = strife_sfx[i].name;
	s->singularity = false;
	s->priority    = strife_sfx[i].priority;
	s->link        = 0;
	s->pitch       = -1;
	s->volume      = -1;
    }
}
