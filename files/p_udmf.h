// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Universal Doom Map Format (UDMF) support -- "doom" namespace.
//
//	Parses a map's TEXTMAP lump into the engine's vertex/sector/side/line
//	arrays and spawns its things.  Only the "doom" (and geometry-identical
//	heretic/strife) namespace is supported: this is the 1993 playsim, so the
//	Hexen/ZDoom parameterised-special namespaces are rejected.  The BSP tree
//	comes from the map's ZNODES lump (GL/ZDBSP extended nodes) -- see
//	P_LoadNodes_Extended in p_setup.c.
//
//-----------------------------------------------------------------------------

#ifndef __P_UDMF__
#define __P_UDMF__

#include "doomtype.h"

// True if the lump AFTER the map label (lumpnum+1) is a TEXTMAP lump, i.e. the
// map is UDMF rather than the classic binary format.
boolean	UDMF_IsMap (int lumpnum);

// Parse the TEXTMAP at lumpnum+1 into vertexes/sectors/sides/lines, and locate
// the map's ZNODES/BLOCKMAP/REJECT sub-lumps (query with the accessors below).
// Things are stashed and spawned later by UDMF_LoadThings.
void	UDMF_LoadMap (int lumpnum);

// Spawn the things parsed by UDMF_LoadMap (call after the level is otherwise set
// up, in place of P_LoadThings).
void	UDMF_LoadThings (void);

// Sub-lump indices discovered by UDMF_LoadMap (-1 if the map omits them).
int	UDMF_ZnodesLump (void);
int	UDMF_BlockmapLump (void);
int	UDMF_RejectLump (void);

#endif
