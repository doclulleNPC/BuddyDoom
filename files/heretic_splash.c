// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.  (fork additions, DOOM Source License)
// Heretic terrain-splash content ported from crispy-doom (GPL) as a reference.
//
// DESCRIPTION:
//	(H) Heretic liquid-terrain splashes.  When a thing lands on (or a missile
//	strikes) a water/lava/sludge flat, spawn the matching splash actors.  Ported
//	from crispy-doom heretic/p_mobj.c (P_HitFloor / P_GetThingFloorType) and
//	heretic/p_spec.c (P_InitTerrainTypes).  heretic_mode only.
//
//	The splash actors + states live in the reserved MT_HSPLASH.. / S_HSPLASH..
//	slots (info.h); their sprites are heretic.wad's native SPSH/LVAS/SLDG codes.
//	Table fill: Heretic_Splash_Init (startup).  Terrain lookup table: built once
//	by P_InitTerrainTypes after R_Init (floorpic indices are stable across levels).
//	The land hook lives in p_mobj.c P_ZMovement (calls P_HitFloor).
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "z_zone.h"
#include "w_wad.h"
#include "p_local.h"
#include "r_state.h"		// subsector/sector -> floorpic
#include "s_sound.h"
#include "sounds.h"
#include "info.h"

extern state_t*		states;
extern mobjinfo_t*	mobjinfo;
extern int		firstflat, numflats;	// r_data.c

// Terrain classes (crispy heretic p_local.h)
enum { FLOOR_SOLID, FLOOR_WATER, FLOOR_LAVA, FLOOR_SLUDGE };

static int*	TerrainTypes;		// [flat index] -> FLOOR_*

// Flat lump name -> terrain type (crispy heretic p_spec.c TerrainTypeDefs).
static const struct { const char* name; int type; } TerrainTypeDefs[] =
{
    { "FLTWAWA1", FLOOR_WATER },
    { "FLTFLWW1", FLOOR_WATER },
    { "FLTLAVA1", FLOOR_LAVA  },
    { "FLATHUH1", FLOOR_LAVA  },
    { "FLTSLUD1", FLOOR_SLUDGE },
    { "FLTFLWS1", FLOOR_WATER },	// H+H IWAD extras
    { "FLTLAVF1", FLOOR_LAVA  },
    { "FLTLAVS1", FLOOR_LAVA  },
    { NULL, -1 }
};

//
// P_InitTerrainTypes
// Build the floorpic->terrain table.  Call after R_Init (needs firstflat/numflats).
//
void P_InitTerrainTypes (void)
{
    int	i, lump, size;

    if (!heretic_mode || numflats <= 0)
	return;
    size = (numflats + 1) * (int)sizeof (int);
    TerrainTypes = Z_Malloc (size, PU_STATIC, 0);
    memset (TerrainTypes, 0, size);
    for (i = 0; TerrainTypeDefs[i].type != -1; i++)
    {
	lump = W_CheckNumForName ((char*)TerrainTypeDefs[i].name);
	if (lump != -1 && lump - firstflat >= 0 && lump - firstflat < numflats)
	    TerrainTypes[lump - firstflat] = TerrainTypeDefs[i].type;
    }
}

static int P_GetThingFloorType (mobj_t* thing)
{
    int	pic;

    if (!TerrainTypes)
	return FLOOR_SOLID;
    pic = thing->subsector->sector->floorpic;
    if (pic < 0 || pic >= numflats)
	return FLOOR_SOLID;
    return TerrainTypes[pic];
}

//
// P_HitFloor
// Spawn the terrain splash for a thing that just hit the floor.  Returns the
// terrain type (FLOOR_SOLID if no splash).  Mirrors crispy heretic P_HitFloor.
//
int P_HitFloor (mobj_t* thing)
{
    mobj_t*	mo;

    // don't splash if landing on a ledge/step ABOVE the liquid surface
    if (thing->floorz != thing->subsector->sector->floorheight)
	return FLOOR_SOLID;

    switch (P_GetThingFloorType (thing))
    {
      case FLOOR_WATER:
	P_SpawnMobj (thing->x, thing->y, ONFLOORZ, MT_HSPLASHBASE);
	mo = P_SpawnMobj (thing->x, thing->y, ONFLOORZ, MT_HSPLASH);
	mo->target = thing;
	mo->momx = (P_Random () - P_Random ()) << 8;
	mo->momy = (P_Random () - P_Random ()) << 8;
	mo->momz = 2 * FRACUNIT + (P_Random () << 8);
	S_StartSound (mo, sfx_h_gloop);
	return FLOOR_WATER;
      case FLOOR_LAVA:
	P_SpawnMobj (thing->x, thing->y, ONFLOORZ, MT_HLAVASPLASH);
	mo = P_SpawnMobj (thing->x, thing->y, ONFLOORZ, MT_HLAVASMOKE);
	mo->momz = FRACUNIT + (P_Random () << 7);
	S_StartSound (mo, sfx_h_burn);
	return FLOOR_LAVA;
      case FLOOR_SLUDGE:
	P_SpawnMobj (thing->x, thing->y, ONFLOORZ, MT_HSLUDGESPLASH);
	mo = P_SpawnMobj (thing->x, thing->y, ONFLOORZ, MT_HSLUDGECHUNK);
	mo->target = thing;
	mo->momx = (P_Random () - P_Random ()) << 8;
	mo->momy = (P_Random () - P_Random ()) << 8;
	mo->momz = FRACUNIT + (P_Random () << 8);
	return FLOOR_SLUDGE;
    }
    return FLOOR_SOLID;
}

// --- table fill --------------------------------------------------------------

#define BRIGHT 32768

static void ST (statenum_t s, spritenum_t spr, int frame, int tics, statenum_t next)
{
    states[s].sprite = spr; states[s].frame = frame; states[s].tics = tics;
    states[s].action.acp1 = NULL; states[s].nextstate = next;
}

static void MI (mobjtype_t mt, statenum_t spawn, statenum_t death,
		fixed_t radius, fixed_t height, int flags)
{
    mobjinfo_t*	m = &mobjinfo[mt];
    m->doomednum = -1;			// spawned by P_HitFloor, never map-placed
    m->spawnstate = spawn;
    m->spawnhealth = 1000;
    m->seestate = m->painstate = m->meleestate = m->missilestate = S_NULL;
    m->deathstate = death;
    m->xdeathstate = m->raisestate = S_NULL;
    m->reactiontime = 8;
    m->radius = radius; m->height = height; m->mass = 100;
    m->flags = flags;
}

//
// Heretic_Splash_Init
// Fill the splash actor states + mobjinfo, and the two splash SFX slots.
//
void Heretic_Splash_Init (void)
{
    // water splash droplet
    ST (S_HSPLASH1, SPR_HSPSH, 0, 8,  S_HSPLASH2);
    ST (S_HSPLASH2, SPR_HSPSH, 1, 8,  S_HSPLASH3);
    ST (S_HSPLASH3, SPR_HSPSH, 2, 8,  S_HSPLASH4);
    ST (S_HSPLASH4, SPR_HSPSH, 3, 16, S_NULL);
    ST (S_HSPLASHX, SPR_HSPSH, 3, 10, S_NULL);
    // water splash base (ring)
    ST (S_HSPLASHBASE1, SPR_HSPSH, 4,  5, S_HSPLASHBASE2);
    ST (S_HSPLASHBASE2, SPR_HSPSH, 5,  5, S_HSPLASHBASE3);
    ST (S_HSPLASHBASE3, SPR_HSPSH, 6,  5, S_HSPLASHBASE4);
    ST (S_HSPLASHBASE4, SPR_HSPSH, 7,  5, S_HSPLASHBASE5);
    ST (S_HSPLASHBASE5, SPR_HSPSH, 8,  5, S_HSPLASHBASE6);
    ST (S_HSPLASHBASE6, SPR_HSPSH, 9,  5, S_HSPLASHBASE7);
    ST (S_HSPLASHBASE7, SPR_HSPSH, 10, 5, S_NULL);
    // lava splash + smoke (full-bright)
    ST (S_HLAVASPLASH1, SPR_HLVAS, 0|BRIGHT, 5, S_HLAVASPLASH2);
    ST (S_HLAVASPLASH2, SPR_HLVAS, 1|BRIGHT, 5, S_HLAVASPLASH3);
    ST (S_HLAVASPLASH3, SPR_HLVAS, 2|BRIGHT, 5, S_HLAVASPLASH4);
    ST (S_HLAVASPLASH4, SPR_HLVAS, 3|BRIGHT, 5, S_HLAVASPLASH5);
    ST (S_HLAVASPLASH5, SPR_HLVAS, 4|BRIGHT, 5, S_HLAVASPLASH6);
    ST (S_HLAVASPLASH6, SPR_HLVAS, 5|BRIGHT, 5, S_NULL);
    ST (S_HLAVASMOKE1,  SPR_HLVAS, 6|BRIGHT, 5, S_HLAVASMOKE2);
    ST (S_HLAVASMOKE2,  SPR_HLVAS, 7|BRIGHT, 5, S_HLAVASMOKE3);
    ST (S_HLAVASMOKE3,  SPR_HLVAS, 8|BRIGHT, 5, S_HLAVASMOKE4);
    ST (S_HLAVASMOKE4,  SPR_HLVAS, 9|BRIGHT, 5, S_HLAVASMOKE5);
    ST (S_HLAVASMOKE5,  SPR_HLVAS, 10|BRIGHT,5, S_NULL);
    // sludge chunk + splash
    ST (S_HSLUDGECHUNK1, SPR_HSLDG, 0, 8, S_HSLUDGECHUNK2);
    ST (S_HSLUDGECHUNK2, SPR_HSLDG, 1, 8, S_HSLUDGECHUNK3);
    ST (S_HSLUDGECHUNK3, SPR_HSLDG, 2, 8, S_HSLUDGECHUNK4);
    ST (S_HSLUDGECHUNK4, SPR_HSLDG, 3, 8, S_NULL);
    ST (S_HSLUDGECHUNKX, SPR_HSLDG, 3, 6, S_NULL);
    ST (S_HSLUDGESPLASH1, SPR_HSLDG, 4, 5, S_HSLUDGESPLASH2);
    ST (S_HSLUDGESPLASH2, SPR_HSLDG, 5, 5, S_HSLUDGESPLASH3);
    ST (S_HSLUDGESPLASH3, SPR_HSLDG, 6, 5, S_HSLUDGESPLASH4);
    ST (S_HSLUDGESPLASH4, SPR_HSLDG, 7, 5, S_NULL);

    // droplets: light, non-blocking, "missile" (so they arc + get removed on impact)
    MI (MT_HSPLASH,       S_HSPLASH1,       S_HSPLASHX, 2*FRACUNIT, 4*FRACUNIT,
	MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF);
    MI (MT_HSPLASHBASE,   S_HSPLASHBASE1,   S_NULL,     20*FRACUNIT, 16*FRACUNIT, MF_NOBLOCKMAP);
    MI (MT_HLAVASPLASH,   S_HLAVASPLASH1,   S_NULL,     20*FRACUNIT, 16*FRACUNIT, MF_NOBLOCKMAP);
    MI (MT_HLAVASMOKE,    S_HLAVASMOKE1,    S_NULL,     20*FRACUNIT, 16*FRACUNIT,
	MF_NOBLOCKMAP|MF_NOGRAVITY|MF_SHADOW);
    MI (MT_HSLUDGECHUNK,  S_HSLUDGECHUNK1,  S_HSLUDGECHUNKX, 2*FRACUNIT, 4*FRACUNIT,
	MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF);
    MI (MT_HSLUDGESPLASH, S_HSLUDGESPLASH1, S_NULL,     20*FRACUNIT, 16*FRACUNIT, MF_NOBLOCKMAP);

    // splash SFX (native heretic.wad lump names; resolved bare in heretic_mode)
    S_sfx_builtin[sfx_h_gloop].name = "gloop"; S_sfx_builtin[sfx_h_gloop].priority = 100;
    S_sfx_builtin[sfx_h_gloop].pitch = -1; S_sfx_builtin[sfx_h_gloop].volume = -1;
    S_sfx_builtin[sfx_h_burn].name  = "burn";  S_sfx_builtin[sfx_h_burn].priority  = 100;
    S_sfx_builtin[sfx_h_burn].pitch  = -1; S_sfx_builtin[sfx_h_burn].volume  = -1;
}
