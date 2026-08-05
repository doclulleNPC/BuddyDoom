// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(X) Hexen sound table -- the native Hexen SFX lump names for the additive Hexen
//	monsters (files/hexen.c) and the poison cloud/shroom.  Split out of the DOOM
//	sounds.c to keep each game's sound data in its own file; the sfx_x_* enum slots
//	still live in sounds.h.
//
//	Names are the REAL Hexen IWAD lump names.  Hexen (unlike Heretic) ships its own
//	SNDINFO lump mapping logical names -> lumps (e.g. EttinSight -> cent2,
//	PoisonShroomDeath -> puff1); this engine has no SNDINFO parser, so those mappings
//	are baked here.  I_SfxLumpFor resolves the bare lump name (it tries "ds"+name then
//	the name as-is), so the sounds play once the Hexen SFX lumps are in a loaded WAD --
//	no DS-prefix rename needed.  Written into the sfx_x_* slots (contiguous, in enum
//	order) at startup by Sounds_Hexen_Init, before I_InitSound precaches.
//
//	Source of the name->lump mappings: hexen.wad SNDINFO (verified against the IWAD).
//
//-----------------------------------------------------------------------------

#include "doomtype.h"
#include "sounds.h"

// { real Hexen lump, priority }, in EXACT sfx_x_* enum order (sounds.h), from etsit.
// The comment on each row is the Hexen SNDINFO logical name it comes from.
static const struct { char* name; int priority; } hexen_sfx[] =
{
    { "cent2",    98  },	// EttinSight
    { "cent1",    96  },	// EttinPain
    { "ethit1",   70  },	// EttinAttack
    { "cntdth1",  70  },	// EttinDeath
    { "taur1",    98  },	// CentaurSight
    { "taur2",    120 },	// CentaurActive
    { "taur4",    96  },	// CentaurPain
    { "centhit2", 70  },	// CentaurAttack
    { "cntdth1",  70  },	// CentaurDeath
    { "cntshld4", 70  },	// CentaurLeaderAttack (slaughtaur)
    { "sbtsit5",  98  },	// DemonSight (chaos serpent)
    { "minact1",  96  },	// DemonPain
    { "dematk2",  70  },	// DemonAttack
    { "sbtdth3",  70  },	// DemonDeath
    { "fired5",   120 },	// FireDemonActive (afrit)
    { "fired2",   96  },	// FireDemonPain
    { "spit6",    70  },	// FireDemonAttack
    { "fired3",   70  },	// FireDemonDeath
    { "firedhit", 70  },	// FireDemonMissileHit
    { "raith5a",  98  },	// WraithSight (reiver)
    { "raith3",   120 },	// WraithActive
    { "raith4a",  96  },	// WraithPain
    { "raith1b",  70  },	// WraithAttack
    { "rathdth2", 70  },	// WraithDeath
    { "syab2d",   98  },	// BishopSight
    { "stb1d",    120 },	// BishopActive
    { "bshpn1",   96  },	// BishopPain
    { "pop",      70  },	// BishopAttack
    { "bishdth1", 70  },	// BishopDeath
    { "bshhit2",  70  },	// BishopMissileExplode
    { "frosty1",  98  },	// IceGuySight (wendigo)
    { "frosty2",  70  },	// IceGuyAttack
    { "shards1b", 70  },	// IceGuyMissileExplode
    { "wtrcrt7",  98  },	// SerpentSight (stalker)
    { "srfc3",    120 },	// SerpentActive
    { "serppn1",  96  },	// SerpentPain
    { "wtrswip",  70  },	// SerpentAttack
    { "srpdth1",  70  },	// SerpentDeath
    { "glbhit4",  70  },	// SerpentFXHit
    { "dragsit1", 98  },	// DragonSight (death wyvern)
    { "dragpn2",  96  },	// DragonPain
    { "mage4",    70  },	// DragonAttack
    { "dragdie2", 70  },	// DragonDeath
    { "mageball", 70  },	// DragonFireballExplode
    { "minsit1",  98  },	// MaulatorSight  (minotaur / dark servant)
    { "minact2",  120 },	// MaulatorActive
    { "minpain4", 96  },	// MaulatorPain
    { "hamblo8a", 70  },	// MaulatorHamSwing
    { "hamfir1",  70  },	// MaulatorHamHit  (floor-fire mortar)
    { "impact3",  70  },	// MaulatorMissileHit / FighterSwordExplode
    { "mindth4",  70  },	// MaulatorDeath
    { "fgtpain",  96  },	// PlayerFighterPain       (fighter boss)
    { "fgtcdth",  70  },	// PlayerFighterCrazyDeath
    { "sword2",   70  },	// FighterSwordFire
    { "plrpain3", 96  },	// PlayerClericPain        (cleric boss)
    { "plrcdth",  70  },	// PlayerClericCrazyDeath
    { "holy3",    70  },	// HolySymbolFire
    { "mgpain",   96  },	// PlayerMagePain          (mage boss)
    { "mgcdth",   70  },	// PlayerMageCrazyDeath
    { "mage4",    70  },	// MageStaffFire
    { "mageball", 70  },	// MageStaffExplode
    { "pigrunt1", 98  },	// PigActive1
    { "pigpain2", 96  },	// PigPain
    { "bite4",    70  },	// PigAttack
    { "pigdth2",  70  },	// PigDeath
    { "bats",     32  },	// BatScream
    { "stretch3", 20  },	// PoisonShroomPain  (shroom pulse/pain flinch)
    { "puff1",    32  },	// PoisonShroomDeath (cloud burst via A_Scream; shroom death)
};

// Write the Hexen SFX names/priorities into their sfx_x_* slots (in enum order,
// starting at sfx_x_etsit and ending at sfx_x_psdth).
void Sounds_Hexen_Init (void)
{
    int	n = (int)(sizeof hexen_sfx / sizeof hexen_sfx[0]);
    int	i;

    // guard against enum drift: only fill the contiguous sfx_x_* range we own
    if (n != (sfx_x_psdth - sfx_x_etsit + 1))
	return;

    for (i = 0; i < n; i++)
    {
	sfxinfo_t* s = &S_sfx_builtin[sfx_x_etsit + i];
	s->name        = hexen_sfx[i].name;
	s->singularity = false;
	s->priority    = hexen_sfx[i].priority;
	s->link        = 0;
	s->pitch       = -1;
	s->volume      = -1;
    }
}
