// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	Enemy thinking, AI.
//	Action Pointer Functions
//	that are associated with states/frames. 
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: p_enemy.c,v 1.5 1997/02/03 22:45:11 b1 Exp $";

#include <stdlib.h>

#include "m_random.h"
#include "i_system.h"

#include "doomdef.h"
#include "p_local.h"

#include "s_sound.h"

#include "g_game.h"
#include "u_mapinfo.h"	// UMAPINFO boss actions

// State.
#include "doomstat.h"
#include "m_bbox.h"	// BOX* (MBF ledge avoidance)
#include "d_items.h"	// weaponinfo[] / WPF_FLEEMELEE (MBF monster_backing)
#include "r_state.h"

#include "p_ai_llm.h"

// Data.
#include "sounds.h"
#include "p_buddydef.h"		// BUDDYSND_* -- buddy body's own pain/death voice
#include "p_ai_coop.h"		// P_Buddy_BodySfx


// (cheat) `notarget` console toggle -- when on, the monster AI ignores the human player
// entirely (classic Boom notarget): it can't be acquired and chasing monsters forget it.
int	notarget;

// (mod) Should the monster AI ignore this player as a target?  True for an INVISIBLE player
// (true-invisibility) or, with `notarget` on, the human player.  Centralises the check used
// by P_LookForPlayers + both A_Look acquisition paths + the A_Chase "forget" logic so they
// stay consistent (and so a forgotten target isn't instantly re-acquired -> stack overflow).
static boolean P_AI_IgnorePlayer (player_t* pl)
{
    return pl->powers[pw_invisibility]
	|| (notarget && pl == &players[consoleplayer]);
}


typedef enum
{
    DI_EAST,
    DI_NORTHEAST,
    DI_NORTH,
    DI_NORTHWEST,
    DI_WEST,
    DI_SOUTHWEST,
    DI_SOUTH,
    DI_SOUTHEAST,
    DI_NODIR,
    NUMDIRS
    
} dirtype_t;


//
// P_NewChaseDir related LUT.
//
dirtype_t opposite[] =
{
  DI_WEST, DI_SOUTHWEST, DI_SOUTH, DI_SOUTHEAST,
  DI_EAST, DI_NORTHEAST, DI_NORTH, DI_NORTHWEST, DI_NODIR
};

dirtype_t diags[] =
{
    DI_NORTHWEST, DI_NORTHEAST, DI_SOUTHWEST, DI_SOUTHEAST
};

// (M) MBF movement options and helpers, used above where they are defined.
extern int	monster_backing;	// defined below, next to monster_pack
extern int	monster_dodge;
extern int	monster_smart;
void		A_FaceTarget (mobj_t* actor);

// Beyond this the sidestep is invisible and only delays the fight.
#define MONSTER_DODGE_RANGE	(896*FRACUNIT)

// How far this thing can reach in melee.
//
// mobjinfo_t.meleerange is an MBF21 field where 0 means "the default", and only a
// DEHACKED patch ever sets it -- so for every stock actor it is 0.  Reading it raw
// (as MBF's own monster_backing code does, because there meleerange is always
// populated) makes every "is the target within melee reach" test compare against
// zero and never fire.  Resolve it the same way P_CheckMeleeRange does.
static fixed_t P_MeleeRangeOf (mobj_t* mo)
{
    return mo->info->meleerange ? mo->info->meleerange : MELEERANGE;
}





void A_Fall (mobj_t *actor);


//
// ENEMY THINKING
// Enemies are allways spawned
// with targetplayer = -1, threshold = 0
// Most monsters are spawned unaware of all players,
// but some can be made preaware
//


//
// Called by P_NoiseAlert.
// Recursively traverse adjacent sectors,
// sound blocking lines cut off traversal.
//

mobj_t*		soundtarget;

void
P_RecursiveSound
( sector_t*	sec,
  int		soundblocks )
{
    int		i;
    line_t*	check;
    sector_t*	other;
	
    // wake up all monsters in this sector
    if (sec->validcount == validcount
	&& sec->soundtraversed <= soundblocks+1)
    {
	return;		// already flooded
    }
    
    sec->validcount = validcount;
    sec->soundtraversed = soundblocks+1;
    sec->soundtarget = soundtarget;
	
    for (i=0 ;i<sec->linecount ; i++)
    {
	check = sec->lines[i];
	if (! (check->flags & ML_TWOSIDED) )
	    continue;
	
	P_LineOpening (check);

	if (openrange <= 0)
	    continue;	// closed door
	
	if ( sides[ check->sidenum[0] ].sector == sec)
	    other = sides[ check->sidenum[1] ] .sector;
	else
	    other = sides[ check->sidenum[0] ].sector;
	
	if (check->flags & ML_SOUNDBLOCK)
	{
	    if (!soundblocks)
		P_RecursiveSound (other, 1);
	}
	else
	    P_RecursiveSound (other, soundblocks);
    }
}



//
// P_NoiseAlert
// If a monster yells at a player,
// it will alert other monsters to the player.
//
void
P_NoiseAlert
( mobj_t*	target,
  mobj_t*	emmiter )
{
    soundtarget = target;
    validcount++;
    P_RecursiveSound (emmiter->subsector->sector, 0);
}




//
// P_CheckMeleeRange
//
boolean P_CheckMeleeRange (mobj_t*	actor)
{
    mobj_t*	pl;
    fixed_t	dist;
	
    if (!actor->target)
	return false;
		
    pl = actor->target;
    dist = P_AproxDistance (pl->x-actor->x, pl->y-actor->y);

    if (dist >= MELEERANGE-20*FRACUNIT+pl->info->radius)
	return false;
	
    if (! P_CheckSight (actor, actor->target) )
	return false;
							
    return true;		
}

//
// P_CheckMissileRange
//
boolean P_CheckMissileRange (mobj_t* actor)
{
    fixed_t	dist;
	
    if (! P_CheckSight (actor, actor->target) )
	return false;
	
    if ( actor->flags & MF_JUSTHIT )
    {
	// the target just hit the enemy,
	// so fight back!
	actor->flags &= ~MF_JUSTHIT;
	return true;
    }
	
    if (actor->reactiontime)
	return false;	// do not attack yet
		
    // OPTIMIZE: get this from a global checksight
    dist = P_AproxDistance ( actor->x-actor->target->x,
			     actor->y-actor->target->y) - 64*FRACUNIT;
    
    if (!actor->info->meleestate)
	dist -= 128*FRACUNIT;	// no melee attack, so fire more

    dist >>= 16;

    // mbf21: MF2_SHORTMRANGE/LONGMELEE/RANGEHALF/HIGHERMPROB generalise the hardcoded
    // arch-vile / revenant / cyberdemon-spider-lostsoul behaviours to any DEHACKED actor.
    // Vanilla types keep their behaviour via the type check; the flag adds it for others.
    if (actor->type == MT_VILE || (actor->flags2 & MF2_SHORTMRANGE))
    {
	if (dist > 14*64)
	    return false;	// too far away
    }


    if (actor->type == MT_UNDEAD || (actor->flags2 & MF2_LONGMELEE))
    {
	if (dist < 196)
	    return false;	// close for fist attack
	dist >>= 1;
    }


    if (actor->type == MT_CYBORG
	|| actor->type == MT_SPIDER
	|| actor->type == MT_SKULL
	|| (actor->flags2 & MF2_RANGEHALF))
    {
	dist >>= 1;
    }

    if (dist > 200)
	dist = 200;

    if ((actor->type == MT_CYBORG || (actor->flags2 & MF2_HIGHERMPROB)) && dist > 160)
	dist = 160;
		
    if (P_Random () < dist)
	return false;
		
    return true;
}


//
// P_Move
// Move in the current direction,
// returns false if the move is blocked.
//
fixed_t	xspeed[8] = {FRACUNIT,47000,0,-47000,-FRACUNIT,-47000,0,47000};
fixed_t yspeed[8] = {0,47000,FRACUNIT,47000,0,-47000,-FRACUNIT,-47000};

extern	line_t**	spechit;	// grows on demand (see p_map.c)
extern	int	numspechit;

boolean P_Move (mobj_t*	actor)
{
    fixed_t	tryx;
    fixed_t	tryy;
    
    line_t*	ld;
    
    // warning: 'catch', 'throw', and 'try'
    // are all C++ reserved words
    boolean	try_ok;
    boolean	good;
		
    if (actor->movedir == DI_NODIR)
	return false;
		
    if ((unsigned)actor->movedir >= 8)
	I_Error ("Weird actor->movedir!");
		
    tryx = actor->x + actor->info->speed*xspeed[actor->movedir];
    tryy = actor->y + actor->info->speed*yspeed[actor->movedir];

    try_ok = P_TryMove (actor, tryx, tryy);

    if (!try_ok)
    {
	// open any specials
	if (actor->flags & MF_FLOAT && floatok)
	{
	    // must adjust height
	    if (actor->z < tmfloorz)
		actor->z += FLOATSPEED;
	    else
		actor->z -= FLOATSPEED;

	    actor->flags |= MF_INFLOAT;
	    return true;
	}
		
	if (!numspechit)
	    return false;
			
	actor->movedir = DI_NODIR;
	good = false;
	while (numspechit--)
	{
	    ld = spechit[numspechit];
	    // if the special is not a door
	    // that can be opened,
	    // return false
	    if (P_UseSpecialLine (actor, ld,0))
		good = true;
	}
	return good;
    }
    else
    {
	actor->flags &= ~MF_INFLOAT;
    }
	
	
    if (! (actor->flags & MF_FLOAT) )	
	actor->z = actor->floorz;
    return true; 
}


//
// TryWalk
// Attempts to move actor on
// in its current (ob->moveangle) direction.
// If blocked by either a wall or an actor
// returns FALSE
// If move is either clear or blocked only by a door,
// returns TRUE and sets...
// If a door is in the way,
// an OpenDoor call is made to start it opening.
//
// (M) MBF P_SmartMove, trimmed: move, then notice if the move put us somewhere
// that hurts.  Setting movedir to DI_NODIR makes the next A_Chase re-pick a
// direction, which is how the monster walks back out of the damage instead of
// standing in it because the player happens to be on the far side.
static boolean P_SmartMove (mobj_t* actor)
{
    int	under_damage = monster_smart ? P_IsUnderDamage (actor) : 0;

    if (!P_Move (actor))
	return false;

    if (monster_smart && !under_damage)
    {
	under_damage = P_IsUnderDamage (actor);
	// -1 is "this really hurts": leave every time.  Otherwise usually, but not
	// always -- a monster that ALWAYS refuses a damaging floor can be walled
	// off by a thin strip of slime, which is its own kind of stupid.
	if (under_damage && (under_damage < 0 || P_Random() < 200))
	    actor->movedir = DI_NODIR;
    }
    return true;
}

boolean P_TryWalk (mobj_t* actor)
{
    if (!P_SmartMove (actor))
    {
	return false;
    }

    actor->movecount = P_Random()&15;
    return true;
}




// ---------------------------------------------------------------------------
// (M) MBF terrain smarts (config monster_smart, Options -> Features).
//
// Vanilla monsters have no idea what they are standing on.  They will happily
// shuffle along the lip of a chasm and stand in a damaging floor while burning,
// because P_NewChaseDir only ever asks "which way is the player".  These give it
// two more things to notice.
// ---------------------------------------------------------------------------

extern fixed_t	tmbbox[4];		// p_map.c -- set up by the caller below

static fixed_t	dropoff_deltax, dropoff_deltay, dropoff_floorz;

// Sum an "away from the ledge" push for every dropoff line the actor overlaps.
// Multiple lines accumulate, so a monster hanging over a corner is pushed out of
// the corner rather than along one edge of it.
static boolean PIT_AvoidDropoff (line_t* line)
{
    if (line->backsector				// one-sided lines are walls
	&& tmbbox[BOXRIGHT]  > line->bbox[BOXLEFT]
	&& tmbbox[BOXLEFT]   < line->bbox[BOXRIGHT]
	&& tmbbox[BOXTOP]    > line->bbox[BOXBOTTOM]
	&& tmbbox[BOXBOTTOM] < line->bbox[BOXTOP]
	&& P_BoxOnLineSide (tmbbox, line) == -1)
    {
	fixed_t	front = line->frontsector->floorheight;
	fixed_t	back  = line->backsector->floorheight;
	angle_t	angle;

	// The actor must be standing on one of the two floors, and the other must
	// be a long way down.
	if (back == dropoff_floorz && front < dropoff_floorz - 24*FRACUNIT)
	    angle = R_PointToAngle2 (0, 0, line->dx, line->dy);		// front is the drop
	else if (front == dropoff_floorz && back < dropoff_floorz - 24*FRACUNIT)
	    angle = R_PointToAngle2 (line->dx, line->dy, 0, 0);		// back is the drop
	else
	    return true;

	dropoff_deltax -= finesine  [angle >> ANGLETOFINESHIFT] * 32;
	dropoff_deltay += finecosine[angle >> ANGLETOFINESHIFT] * 32;
    }
    return true;
}

// Non-zero if the actor is over a ledge and a direction away from it was found.
static fixed_t P_AvoidDropoff (mobj_t* actor)
{
    int	xl, xh, yl, yh, bx, by;

    tmbbox[BOXTOP]    = actor->y + actor->radius;
    tmbbox[BOXBOTTOM] = actor->y - actor->radius;
    tmbbox[BOXRIGHT]  = actor->x + actor->radius;
    tmbbox[BOXLEFT]   = actor->x - actor->radius;

    yh = (tmbbox[BOXTOP]    - bmaporgy) >> MAPBLOCKSHIFT;
    yl = (tmbbox[BOXBOTTOM] - bmaporgy) >> MAPBLOCKSHIFT;
    xh = (tmbbox[BOXRIGHT]  - bmaporgx) >> MAPBLOCKSHIFT;
    xl = (tmbbox[BOXLEFT]   - bmaporgx) >> MAPBLOCKSHIFT;

    dropoff_floorz = actor->z;			// the floor it is standing on
    dropoff_deltax = dropoff_deltay = 0;

    validcount++;
    for (bx = xl; bx <= xh; bx++)
	for (by = yl; by <= yh; by++)
	    P_BlockLinesIterator (bx, by, PIT_AvoidDropoff);

    return dropoff_deltax | dropoff_deltay;
}

// Is the actor standing in something that hurts?  Returns -1 when it is bad
// enough to leave regardless of the dice roll.
static int P_IsUnderDamage (mobj_t* actor)
{
    const sector_t* sec = actor->subsector->sector;

    if (!sec->special)
	return 0;
    switch (sec->special)
    {
      case 4:		// 20% damage + light flicker
      case 7:		// 5% damage
      case 5:		// 10% damage
      case 16:		// 20% damage
      case 11:		// 20% damage, end level at 0%
	return (sec->special == 4 || sec->special == 16 || sec->special == 11) ? -1 : 1;
      default:
	return 0;
    }
}

// Pick a movedir that makes progress along (deltax, deltay), then walk it.
//
// (M) killough 9/8/98 split this out of P_NewChaseDir so the CALLER decides which
// way the monster wants to go.  Vanilla always passed "towards the target"; with
// the split, "away from the target" costs nothing extra and is what backing off
// and sidestepping are built from.  The direction search below is unchanged
// vanilla -- only its input moved.
static void P_DoNewChaseDir (mobj_t* actor, fixed_t deltax, fixed_t deltay)
{
    dirtype_t	d[3];

    int		tdir;
    dirtype_t	olddir;

    dirtype_t	turnaround;

    olddir = actor->movedir;
    turnaround=opposite[olddir];

    if (deltax>10*FRACUNIT)
	d[1]= DI_EAST;
    else if (deltax<-10*FRACUNIT)
	d[1]= DI_WEST;
    else
	d[1]=DI_NODIR;

    if (deltay<-10*FRACUNIT)
	d[2]= DI_SOUTH;
    else if (deltay>10*FRACUNIT)
	d[2]= DI_NORTH;
    else
	d[2]=DI_NODIR;

    // try direct route
    if (d[1] != DI_NODIR
	&& d[2] != DI_NODIR)
    {
	actor->movedir = diags[((deltay<0)<<1)+(deltax>0)];
	if (actor->movedir != turnaround && P_TryWalk(actor))
	    return;
    }

    // try other directions
    if (P_Random() > 200
	||  abs(deltay)>abs(deltax))
    {
	tdir=d[1];
	d[1]=d[2];
	d[2]=tdir;
    }

    if (d[1]==turnaround)
	d[1]=DI_NODIR;
    if (d[2]==turnaround)
	d[2]=DI_NODIR;
	
    if (d[1]!=DI_NODIR)
    {
	actor->movedir = d[1];
	if (P_TryWalk(actor))
	{
	    // either moved forward or attacked
	    return;
	}
    }

    if (d[2]!=DI_NODIR)
    {
	actor->movedir =d[2];

	if (P_TryWalk(actor))
	    return;
    }

    // there is no direct path to the player,
    // so pick another direction.
    if (olddir!=DI_NODIR)
    {
	actor->movedir =olddir;

	if (P_TryWalk(actor))
	    return;
    }

    // randomly determine direction of search
    if (P_Random()&1) 	
    {
	for ( tdir=DI_EAST;
	      tdir<=DI_SOUTHEAST;
	      tdir++ )
	{
	    if (tdir!=turnaround)
	    {
		actor->movedir =tdir;
		
		if ( P_TryWalk(actor) )
		    return;
	    }
	}
    }
    else
    {
	for ( tdir=DI_SOUTHEAST;
	      tdir != (DI_EAST-1);
	      tdir-- )
	{
	    if (tdir!=turnaround)
	    {
		actor->movedir =tdir;
		
		if ( P_TryWalk(actor) )
		    return;
	    }
	}
    }

    if (turnaround !=  DI_NODIR)
    {
	actor->movedir =turnaround;
	if ( P_TryWalk(actor) )
	    return;
    }

    actor->movedir = DI_NODIR;	// can not move
}


//
// P_NewChaseDir
//
// Decide WHICH WAY the monster wants to go, then hand it to P_DoNewChaseDir.
// Vanilla only ever wanted "towards the target".
//
void P_NewChaseDir (mobj_t*	actor)
{
    fixed_t	deltax;
    fixed_t	deltay;

    if (!actor->target)
    {
	// Vanilla fatally I_Error'd here.  This fork drives A_Chase through several extra
	// AI hooks (LLM director, pack-hunt, the co-op buddy, the security drone, revived
	// marines) and one of them can momentarily reach the chase machinery with no target
	// -- crashing the whole game over that is far worse than the monster just wandering
	// for a tic (the next A_Chase re-acquires a target or idles at spawnstate).  Re-roll
	// the walk direction ourselves and carry on; warn ONCE so it stays diagnosable.
	static boolean warned;
	if (!warned)
	{
	    fprintf (stderr, "P_NewChaseDir: called with no target (mobj type %d) -- "
			     "wandering instead of crashing (warned once)\n", actor->type);
	    warned = true;
	}
	actor->movedir   = P_Random () % 8;
	actor->movecount = 15;
	actor->strafecount = 0;
	return;
    }

    deltax = actor->target->x - actor->x;
    deltay = actor->target->y - actor->y;

    actor->strafecount = 0;

    // (M) MBF: get off the ledge first.  Everything else -- chasing, backing off,
    // circling -- is worth less than not falling into the pit, so this is checked
    // before any of it, and movecount is set to 1 so the monster re-evaluates next
    // tic instead of committing to a long walk away from the fight.
    if (monster_smart
	&& actor->floorz - actor->dropoffz > 24*FRACUNIT
	&& actor->z <= actor->floorz
	&& !(actor->flags & (MF_DROPOFF|MF_FLOAT))
	&& P_AvoidDropoff (actor))
    {
	P_DoNewChaseDir (actor, dropoff_deltax, dropoff_deltay);
	actor->movecount = 1;
	return;
    }

    // (M) MBF, killough 8/8/98: back away from a melee threat instead of standing
    // in it.  A monster with a MISSILE attack has no business walking into arm's
    // reach, so when something that can only hurt it up close gets that close --
    // or the player closes in holding a weapon monsters treat as melee -- it gives
    // ground while keeping the target in front of it.
    //
    // strafecount is what makes it read as backing off rather than fleeing: A_Chase
    // keeps the actor facing the target for that many tics, so it retreats looking
    // at you instead of turning its back and running.
    //
    // Lost souls are excluded because they ARE the charge -- a backing-off Lost
    // Soul would never connect.
    if (monster_backing
	&& actor->info->missilestate
	&& actor->type != MT_SKULL
	&& actor->target->health > 0
	&& !(actor->flags & actor->target->flags & MF_FRIEND))
    {
	fixed_t dist = P_AproxDistance (deltax, deltay);
	mobj_t* targ = actor->target;

	fixed_t mrange = P_MeleeRangeOf (targ);

	if ((!targ->info->missilestate && dist < mrange*2)
	 || (targ->player && dist < mrange*3
	     && (weaponinfo[targ->player->readyweapon].flags & WPF_FLEEMELEE)))
	{
	    actor->strafecount = P_Random() & 15;
	    deltax = -deltax;
	    deltay = -deltay;
	}
    }

    // (X) Dodging -- our own extension, not MBF.  MBF only ever reverses the
    // approach vector; nothing in the DOOM lineage ever moves a monster SIDEWAYS.
    //
    // At fighting range a monster with a ranged attack has no reason to walk a
    // straight line into your crosshair.  Rotate the approach vector 90 degrees so
    // it circles instead, and set strafecount so A_Chase keeps it facing you --
    // without that it would simply turn and stroll off sideways, which looks like
    // it lost interest rather than like it is working an angle.
    //
    // Only at range: point blank it should close and bite, and far away the
    // sidestep is invisible and just delays the fight.  A monster that was just
    // hit jinks much more readily -- that is the "dodge" as opposed to the circle.
    else if (monster_dodge
	     && actor->info->missilestate
	     && actor->type != MT_SKULL
	     && actor->target->health > 0
	     && !(actor->flags & actor->target->flags & MF_FRIEND))
    {
	fixed_t dist   = P_AproxDistance (deltax, deltay);
	fixed_t mrange = P_MeleeRangeOf (actor->target);

	if (dist > mrange*3 && dist < MONSTER_DODGE_RANGE
	    && P_Random() < ((actor->flags & MF_JUSTHIT) ? 180 : 70))
	{
	    fixed_t t = deltax;
	    if (P_Random() & 1)
		{ deltax = -deltay; deltay =  t; }	// circle one way...
	    else
		{ deltax =  deltay; deltay = -t; }	// ...or the other
	    actor->strafecount = 4 + (P_Random() & 7);
	}
    }

    P_DoNewChaseDir (actor, deltax, deltay);

    // Keep the old zig-zag timing, but let the manoeuvre run its full length.
    if (actor->strafecount)
	actor->movecount = actor->strafecount;
}



//
// P_LookForPlayers
// If allaround is false, only look 180 degrees in front.
// Returns true if a player is targeted.
//
// BuddyDoom: nearest live enemy monster to a FRIENDLY monster (summonfriend) -- a real
// COUNTKILL monster, shootable, alive, and not itself friendly.  NULL if none.
mobj_t* P_FriendNearestEnemy (mobj_t* actor)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = 0x7fffffff;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	mo = (mobj_t*)th;
	if (mo == actor || mo->health <= 0)		continue;
	if (!(mo->flags & MF_COUNTKILL))		continue;	// real monster only
	if (mo->flags & MF_FRIEND)			continue;	// not another ally
	if (!(mo->flags & MF_SHOOTABLE))		continue;
	d = P_AproxDistance (mo->x - actor->x, mo->y - actor->y);
	if (d < bestd) { bestd = d; best = mo; }
    }
    return best;
}

boolean
P_LookForPlayers
( mobj_t*	actor,
  boolean	allaround )
{
    // A friendly monster hunts the nearest enemy monster instead of a player.
    if (actor->flags & MF_FRIEND)
    {
	mobj_t* e = P_FriendNearestEnemy (actor);
	if (e) { actor->target = e; return true; }
	return false;
    }

    int		c;
    int		stop;
    player_t*	player;
    sector_t*	sector;
    angle_t	an;
    fixed_t	dist;
		
    sector = actor->subsector->sector;
	
    c = 0;
    stop = (actor->lastlook-1)&3;
	
    for ( ; ; actor->lastlook = (actor->lastlook+1)&3 )
    {
	if (!playeringame[actor->lastlook])
	    continue;
			
	if (c++ == 2
	    || actor->lastlook == stop)
	{
	    // done looking
	    return false;	
	}
	
	player = &players[actor->lastlook];

	if (player->health <= 0)
	    continue;		// dead

	if (P_AI_IgnorePlayer (player))
	    continue;		// (mod) true invisibility / notarget -- don't acquire this player

	if (!P_CheckSight (actor, player->mo))
	    continue;		// out of sight
			
	if (!allaround)
	{
	    an = R_PointToAngle2 (actor->x,
				  actor->y, 
				  player->mo->x,
				  player->mo->y)
		- actor->angle;
	    
	    if (an > ANG90 && an < ANG270)
	    {
		dist = P_AproxDistance (player->mo->x - actor->x,
					player->mo->y - actor->y);
		// if real close, react anyway
		if (dist > MELEERANGE)
		    continue;	// behind back
	    }
	}
		
	actor->target = player->mo;
	return true;
    }

    return false;
}


//
// A_KeenDie
// DOOM II special, map 32.
// Uses special tag 666.
//
void A_KeenDie (mobj_t* mo)
{
    thinker_t*	th;
    mobj_t*	mo2;
    line_t	junk;

    A_Fall (mo);
    
    // scan the remaining thinkers
    // to see if all Keens are dead
    for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
    {
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;

	mo2 = (mobj_t *)th;
	if (mo2 != mo
	    && mo2->type == mo->type
	    && mo2->health > 0)
	{
	    // other Keen not dead
	    return;		
	}
    }

    junk.tag = 666;
    EV_DoDoor(&junk,open);
}


//
// ACTION ROUTINES
//

//
// A_Look
// Stay in state until a player is sighted.
//
//
// Pack hunt -- optional aggressive group AI (config: monster_pack).
//  * monsters acquire the player the moment they spawn -- searching even with
//    no line of sight -- so they hunt immediately, and
//  * while still far away they steer partly toward the centre of nearby allies
//    so they bunch up and hit the player in groups.
// Turning it off restores vanilla wake-on-sight/sound behaviour.
//
int	monster_pack       = 0;		// 1 = pack hunt on; default OFF = vanilla 1993 AI
int	monster_pack_range = 2048;	// search / cohesion radius (map units)

// (M) MBF: a ranged monster gives ground when a melee threat closes in, keeping
// the target in front of it (P_NewChaseDir).  Default OFF -- it changes how every
// fight in the game plays, so it stays opt-in like monster_pack.
int	monster_backing    = 0;

// (X) Dodging: at fighting range a ranged monster circles instead of walking
// straight at you, and jinks when hit.  Ours, not MBF's -- see P_NewChaseDir.
// Both are toggled in Options -> Features.
int	monster_dodge      = 0;

// (M) MBF terrain smarts, as one switch: avoid ledges, leave damaging floors, and
// use the relative step rule so a monster can follow you down (and up) tall
// stairs.  Grouped behind one option because individually each is nearly
// invisible.  Default OFF.
int	monster_smart      = 0;

static mobj_t* P_PackNearestPlayer (mobj_t* actor)
{
    int		i, best = -1;
    fixed_t	bd = 0;
    for (i = 0 ; i < MAXPLAYERS ; i++)
    {
	fixed_t	d;
	if (!playeringame[i] || !players[i].mo || players[i].health <= 0)
	    continue;
	d = P_AproxDistance (players[i].mo->x - actor->x,
			     players[i].mo->y - actor->y);
	if (best < 0 || d < bd) { best = i; bd = d; }
    }
    return (best >= 0) ? players[best].mo : NULL;
}

// Average position of nearby live monsters (excluding `actor`).  Needs at least
// a couple of allies for a meaningful "group".
static boolean P_PackCentre (mobj_t* actor, fixed_t* cx, fixed_t* cy)
{
    thinker_t*	th;
    long long	sdx = 0, sdy = 0;	// sum of *relative* positions (small)
    int		n = 0;
    fixed_t	r = (fixed_t)monster_pack_range * FRACUNIT;

    for (th = thinkercap.next ; th != &thinkercap ; th = th->next)
    {
	mobj_t* m;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	m = (mobj_t *)th;
	if (m == actor || m->health <= 0 || !(m->flags & MF_COUNTKILL)) continue;
	if (P_AproxDistance (m->x - actor->x, m->y - actor->y) > r) continue;
	sdx += (m->x - actor->x);
	sdy += (m->y - actor->y);
	n++;
    }
    if (n < 2) return false;
    *cx = actor->x + (fixed_t)(sdx / n);
    *cy = actor->y + (fixed_t)(sdy / n);
    return true;
}

// Chase the target, but while far away bias toward the local pack centre so
// monsters converge into groups en route.  Mirrors vanilla's movecount/P_Move
// flow (only re-steers when committing to a new direction).
void P_PackChase (mobj_t* actor)
{
    if (--actor->movecount < 0 || !P_Move (actor))
    {
	fixed_t	tx = actor->target->x;
	fixed_t	ty = actor->target->y;
	angle_t	ang;

	if (P_AproxDistance (tx - actor->x, ty - actor->y)
		> (fixed_t)monster_pack_range * FRACUNIT)
	{
	    fixed_t cx, cy;
	    if (P_PackCentre (actor, &cx, &cy))
	    {
		tx += (cx - tx) / 5 * 2;	// ~40% toward the group centre
		ty += (cy - ty) / 5 * 2;
	    }
	}

	ang = R_PointToAngle2 (actor->x, actor->y, tx, ty);
	actor->movedir = ((ang + (ANG45/2)) >> 29) & 7;
	actor->movecount = 4 + (P_Random () & 7);	// commit for a few tics
	if (!P_Move (actor))
	    P_NewChaseDir (actor);
    }
}


void A_Look (mobj_t* actor)
{
    mobj_t*	targ;

    actor->threshold = 0;	// any shot will wake up

    // Friendly actors (revived marine, summonfriend) must NEVER acquire the player:
    // not via pack-lock and not via weapon NOISE (sector->soundtarget, which is the
    // human who just fired).  Route straight to the enemy-monster finder so they hunt
    // monsters and leave the player/buddy alone.
    if (actor->flags & MF_FRIEND)
    {
	if (!P_LookForPlayers (actor, false))
	    return;
	goto seeyou;
    }

    // Pack hunt: lock onto the player at once (even with no line of sight) so a
    // freshly-spawned monster starts searching/closing in immediately.
    if (monster_pack)
    {
	mobj_t* pl = P_PackNearestPlayer (actor);
	// (mod) true invisibility: don't pack-lock onto an invisible player either.
	if (pl && !(pl->player && P_AI_IgnorePlayer (pl->player)))
	{ actor->target = pl; goto seeyou; }
    }
    targ = actor->subsector->sector->soundtarget;

    if (targ
	&& (targ->flags & MF_SHOOTABLE)
	// (mod) true invisibility: don't acquire an invisible player through NOISE either.
	// Without this, A_Chase forgets the invisible target -> spawnstate -> A_Look re-acquires
	// it via soundtarget -> seestate -> A_Chase forgets -> ... = infinite P_SetMobjState
	// recursion -> stack overflow (the invis-pickup crash).
	&& !(targ->player && P_AI_IgnorePlayer (targ->player)) )
    {
	actor->target = targ;

	if ( actor->flags & MF_AMBUSH )
	{
	    if (P_CheckSight (actor, actor->target))
		goto seeyou;
	}
	else
	    goto seeyou;
    }
	
	
    if (!P_LookForPlayers (actor, false) )
	return;
		
    // go into chase state
  seeyou:
    if (actor->info->seesound)
    {
	int		sound;
		
	switch (actor->info->seesound)
	{
	  case sfx_posit1:
	  case sfx_posit2:
	  case sfx_posit3:
	    sound = sfx_posit1+P_Random()%3;
	    break;

	  case sfx_bgsit1:
	  case sfx_bgsit2:
	    sound = sfx_bgsit1+P_Random()%2;
	    break;

	  default:
	    sound = actor->info->seesound;
	    break;
	}

	if (actor->type==MT_SPIDER
	    || actor->type == MT_CYBORG
	    || (actor->flags2 & (MF2_BOSS | MF2_FULLVOLSOUNDS)))   // mbf21
	{
	    // full volume
	    S_StartSound (NULL, sound);
	}
	else
	    S_StartSound (actor, sound);
    }

    P_SetMobjState (actor, actor->info->seestate);
}


//
// A_Chase
// Actor has a melee attack,
// so it tries to close as fast as possible
//
void A_Chase (mobj_t*	actor)
{
    int		delta;

    // LLM "AI Director": if this monster has an active high-level order,
    // execute it instead of the vanilla chase logic.
    if (P_AI_Active (actor))
    {
	A_LLMChase (actor);
	return;
    }

    if (actor->reactiontime)
	actor->reactiontime--;
				

    // modify target threshold
    if  (actor->threshold)
    {
	if (!actor->target
	    || actor->target->health <= 0)
	{
	    actor->threshold = 0;
	}
	else
	    actor->threshold--;
    }

    // (mod) True invisibility: forget an INVISIBLE player target this monster isn't actively
    // retaliating against (threshold==0 -> it hasn't been hurt by the player recently) and go
    // back to wandering.  Shooting a monster sets its threshold (P_DamageMobj), so THAT one
    // keeps hunting -- "only once you shoot it does it come after you".
    if (actor->target && actor->target->player
	&& P_AI_IgnorePlayer (actor->target->player)
	&& actor->threshold == 0)
    {
	actor->target = NULL;
	P_SetMobjState (actor, actor->info->spawnstate);
	return;
    }

    // turn towards movement direction if not there yet
    //
    // (M) MBF, killough 9/7/98: ...unless a manoeuvre is running, in which case
    // keep facing the TARGET.  This one line is the whole difference between a
    // monster that turns its back and walks away and one that gives ground with
    // its eyes on you -- movement direction and facing stop being the same thing.
    if (actor->strafecount && actor->target)
	A_FaceTarget (actor);
    else if (actor->movedir < 8)
    {
	actor->angle &= (7<<29);
	delta = actor->angle - (actor->movedir << 29);

	if (delta > 0)
	    actor->angle -= ANG90/2;
	else if (delta < 0)
	    actor->angle += ANG90/2;
    }

    if (!actor->target
	|| !(actor->target->flags&MF_SHOOTABLE))
    {
	// look for a new target
	if (P_LookForPlayers(actor,true))
	    return; 	// got a new target
	
	P_SetMobjState (actor, actor->info->spawnstate);
	return;
    }
    
    // do not attack twice in a row
    if (actor->flags & MF_JUSTATTACKED)
    {
	actor->flags &= ~MF_JUSTATTACKED;
	if (gameskill != sk_nightmare && !fastparm)
	    P_NewChaseDir (actor);
	return;
    }
    
    // check for melee attack
    if (actor->info->meleestate
	&& P_CheckMeleeRange (actor))
    {
	if (actor->info->attacksound)
	    S_StartSound (actor, actor->info->attacksound);

	P_SetMobjState (actor, actor->info->meleestate);
	return;
    }
    
    // check for missile attack
    if (actor->info->missilestate)
    {
	if (gameskill < sk_nightmare
	    && !fastparm && actor->movecount)
	{
	    goto nomissile;
	}
	
	if (!P_CheckMissileRange (actor))
	    goto nomissile;
	
	P_SetMobjState (actor, actor->info->missilestate);
	actor->flags |= MF_JUSTATTACKED;
	return;
    }

    // ?
  nomissile:
    // possibly choose another target
    if (netgame
	&& !actor->threshold
	&& !P_CheckSight (actor, actor->target) )
    {
	if (P_LookForPlayers(actor,true))
	    return;	// got a new target
    }
    
    if (actor->strafecount)
	actor->strafecount--;

    // chase towards player
    if (monster_pack)
    {
	P_PackChase (actor);		// group up + path to the player
    }
    else if (--actor->movecount<0
	|| !P_SmartMove (actor))
    {
	P_NewChaseDir (actor);
    }

    // make active sound
    if (actor->info->activesound
	&& P_Random () < 3)
    {
	S_StartSound (actor, actor->info->activesound);
    }
}


//
// Friendly-actor codepointers (BuddyDoom): turn any MF_FRIEND actor into an escort.
// A modder builds a normal DEHACKED/DECOHack actor but uses A_BuddyLook in Spawn and
// A_BuddyChase in See -- then it hunts enemy monsters like a friend AND follows the
// human when there is nothing to fight.
//
// These are for DEHACKED-authored actors only.  The co-op BUDDY is player 2 and runs
// on the bot in p_ai_coop.c -- it takes no orders from here, and neither does an actor
// using these pointers: a monster has no player_t to be ordered, revived or HUD'd.
//
#define BUDDY_FOLLOW_DIST	(160*FRACUNIT)	// stay within this of the human when idle

// No enemy in reach: pad along after the human (players[consoleplayer]) without ever
// treating them as a target to attack.
static void A_BuddyFollow (mobj_t* self)
{
    mobj_t* h = playeringame[consoleplayer] ? players[consoleplayer].mo : NULL;

    if (!h || h->health <= 0)			// no human -> just wander
    {
	// P_NewChaseDir I_Errors ("called with no target") when the actor has none, and
	// here it never does: we only reach this when the buddy found no enemy AND the
	// human is gone/dead.  Re-roll the walk direction ourselves instead of crashing.
	if (--self->movecount < 0 || !P_Move (self))
	{
	    self->movedir   = P_Random () % 8;
	    self->movecount = 15;
	}
	return;
    }

    if (P_AproxDistance (h->x - self->x, h->y - self->y) <= BUDDY_FOLLOW_DIST)
	return;					// close enough -> idle at the player's side

    // Walk toward the human using the chase-dir machinery, but with the human as a
    // *temporary* target so P_NewChaseDir steers there and P_Move steps -- no attack.
    {
	mobj_t*	saved = self->target;
	self->target = h;
	if (--self->movecount < 0 || !P_Move (self))
	    P_NewChaseDir (self);
	self->target = saved;
    }
}

// See-state action for a buddy: fight the nearest enemy with the stock chase/attack
// logic; if there is none, follow the human.
void A_BuddyChase (mobj_t* self)
{
    if (!self->target
	|| self->target->health <= 0
	|| !(self->target->flags & MF_SHOOTABLE)
	|| (self->target->flags & MF_CORPSE))
	self->target = P_FriendNearestEnemy (self);

    if (self->target)				// enemy present -> full chase + melee/missile
    {
	A_Chase (self);
	return;
    }

    A_BuddyFollow (self);			// nothing to fight -> stay with the human
}

// Spawn-state action for a buddy: acquire an enemy if one is near, then always go
// active (See) so the buddy follows the human even with no enemies around.
void A_BuddyLook (mobj_t* self)
{
    self->target = P_FriendNearestEnemy (self);
    if (self->target && self->info->seesound)
	S_StartSound (self, self->info->seesound);
    if (self->info->seestate)
	P_SetMobjState (self, self->info->seestate);
}


//
// A_FaceTarget
//
void A_FaceTarget (mobj_t* actor)
{	
    if (!actor->target)
	return;
    
    actor->flags &= ~MF_AMBUSH;
	
    actor->angle = R_PointToAngle2 (actor->x,
				    actor->y,
				    actor->target->x,
				    actor->target->y);
    
    if (actor->target->flags & MF_SHADOW)
	actor->angle += (P_Random()-P_Random())<<21;
}


//
// A_PosAttack
//
void A_PosAttack (mobj_t* actor)
{
    int		angle;
    int		damage;
    int		slope;
	
    if (!actor->target)
	return;
		
    A_FaceTarget (actor);
    angle = actor->angle;
    slope = P_AimLineAttack (actor, angle, MISSILERANGE);

    S_StartSound (actor, sfx_pistol);
    angle += (P_Random()-P_Random())<<20;
    damage = ((P_Random()%5)+1)*3;
    P_LineAttack (actor, angle, MISSILERANGE, slope, damage);
}

void A_SPosAttack (mobj_t* actor)
{
    int		i;
    int		angle;
    int		bangle;
    int		damage;
    int		slope;
	
    if (!actor->target)
	return;

    S_StartSound (actor, sfx_shotgn);
    A_FaceTarget (actor);
    bangle = actor->angle;
    slope = P_AimLineAttack (actor, bangle, MISSILERANGE);

    for (i=0 ; i<3 ; i++)
    {
	angle = bangle + ((P_Random()-P_Random())<<20);
	damage = ((P_Random()%5)+1)*3;
	P_LineAttack (actor, angle, MISSILERANGE, slope, damage);
    }
}

void A_CPosAttack (mobj_t* actor)
{
    int		angle;
    int		bangle;
    int		damage;
    int		slope;
	
    if (!actor->target)
	return;

    S_StartSound (actor, sfx_shotgn);
    A_FaceTarget (actor);
    bangle = actor->angle;
    slope = P_AimLineAttack (actor, bangle, MISSILERANGE);

    angle = bangle + ((P_Random()-P_Random())<<20);
    damage = ((P_Random()%5)+1)*3;
    P_LineAttack (actor, angle, MISSILERANGE, slope, damage);
}

void A_CPosRefire (mobj_t* actor)
{	
    // keep firing unless target got out of sight
    A_FaceTarget (actor);

    if (P_Random () < 40)
	return;

    if (!actor->target
	|| actor->target->health <= 0
	|| !P_CheckSight (actor, actor->target) )
    {
	P_SetMobjState (actor, actor->info->seestate);
    }
}


void A_SpidRefire (mobj_t* actor)
{	
    // keep firing unless target got out of sight
    A_FaceTarget (actor);

    if (P_Random () < 10)
	return;

    if (!actor->target
	|| actor->target->health <= 0
	|| !P_CheckSight (actor, actor->target) )
    {
	P_SetMobjState (actor, actor->info->seestate);
    }
}

void A_BspiAttack (mobj_t *actor)
{	
    if (!actor->target)
	return;
		
    A_FaceTarget (actor);

    // launch a missile
    P_SpawnMissile (actor, actor->target, MT_ARACHPLAZ);
}


//
// A_TroopAttack
//
void A_TroopAttack (mobj_t* actor)
{
    int		damage;
	
    if (!actor->target)
	return;
		
    A_FaceTarget (actor);
    if (P_CheckMeleeRange (actor))
    {
	S_StartSound (actor, sfx_claw);
	damage = (P_Random()%8+1)*3;
	P_DamageMobj (actor->target, actor, actor, damage);
	return;
    }

    
    // launch a missile
    P_SpawnMissile (actor, actor->target, MT_TROOPSHOT);
}


void A_SargAttack (mobj_t* actor)
{
    int		damage;

    if (!actor->target)
	return;
		
    A_FaceTarget (actor);
    if (P_CheckMeleeRange (actor))
    {
	damage = ((P_Random()%10)+1)*4;
	P_DamageMobj (actor->target, actor, actor, damage);
    }
}

void A_HeadAttack (mobj_t* actor)
{
    int		damage;
	
    if (!actor->target)
	return;
		
    A_FaceTarget (actor);
    if (P_CheckMeleeRange (actor))
    {
	damage = (P_Random()%6+1)*10;
	P_DamageMobj (actor->target, actor, actor, damage);
	return;
    }
    
    // launch a missile
    P_SpawnMissile (actor, actor->target, MT_HEADSHOT);
}

void A_CyberAttack (mobj_t* actor)
{	
    if (!actor->target)
	return;
		
    A_FaceTarget (actor);
    P_SpawnMissile (actor, actor->target, MT_ROCKET);
}


void A_BruisAttack (mobj_t* actor)
{
    int		damage;
	
    if (!actor->target)
	return;
		
    if (P_CheckMeleeRange (actor))
    {
	S_StartSound (actor, sfx_claw);
	damage = (P_Random()%8+1)*10;
	P_DamageMobj (actor->target, actor, actor, damage);
	return;
    }
    
    // launch a missile
    P_SpawnMissile (actor, actor->target, MT_BRUISERSHOT);
}


//
// A_SkelMissile
//
void A_SkelMissile (mobj_t* actor)
{	
    mobj_t*	mo;
	
    if (!actor->target)
	return;
		
    A_FaceTarget (actor);
    actor->z += 16*FRACUNIT;	// so missile spawns higher
    mo = P_SpawnMissile (actor, actor->target, MT_TRACER);
    actor->z -= 16*FRACUNIT;	// back to normal

    mo->x += mo->momx;
    mo->y += mo->momy;
    mo->tracer = actor->target;
}

int	TRACEANGLE = 0xc000000;

void A_Tracer (mobj_t* actor)
{
    angle_t	exact;
    fixed_t	dist;
    fixed_t	slope;
    mobj_t*	dest;
    mobj_t*	th;
		
    if (gametic & 3)
	return;
    
    // spawn a puff of smoke behind the rocket		
    P_SpawnPuff (actor->x, actor->y, actor->z);
	
    th = P_SpawnMobj (actor->x-actor->momx,
		      actor->y-actor->momy,
		      actor->z, MT_SMOKE);
    
    th->momz = FRACUNIT;
    th->tics -= P_Random()&3;
    if (th->tics < 1)
	th->tics = 1;
    
    // adjust direction
    dest = actor->tracer;
	
    if (!dest || dest->health <= 0)
	return;
    
    // change angle	
    exact = R_PointToAngle2 (actor->x,
			     actor->y,
			     dest->x,
			     dest->y);

    if (exact != actor->angle)
    {
	if (exact - actor->angle > 0x80000000)
	{
	    actor->angle -= TRACEANGLE;
	    if (exact - actor->angle < 0x80000000)
		actor->angle = exact;
	}
	else
	{
	    actor->angle += TRACEANGLE;
	    if (exact - actor->angle > 0x80000000)
		actor->angle = exact;
	}
    }
	
    exact = actor->angle>>ANGLETOFINESHIFT;
    actor->momx = FixedMul (actor->info->speed, finecosine[exact]);
    actor->momy = FixedMul (actor->info->speed, finesine[exact]);
    
    // change slope
    dist = P_AproxDistance (dest->x - actor->x,
			    dest->y - actor->y);
    
    dist = dist / actor->info->speed;

    if (dist < 1)
	dist = 1;
    slope = (dest->z+40*FRACUNIT - actor->z) / dist;

    if (slope < actor->momz)
	actor->momz -= FRACUNIT/8;
    else
	actor->momz += FRACUNIT/8;
}


void A_SkelWhoosh (mobj_t*	actor)
{
    if (!actor->target)
	return;
    A_FaceTarget (actor);
    S_StartSound (actor,sfx_skeswg);
}

void A_SkelFist (mobj_t*	actor)
{
    int		damage;

    if (!actor->target)
	return;
		
    A_FaceTarget (actor);
	
    if (P_CheckMeleeRange (actor))
    {
	damage = ((P_Random()%10)+1)*6;
	S_StartSound (actor, sfx_skepch);
	P_DamageMobj (actor->target, actor, actor, damage);
    }
}



//
// PIT_VileCheck
// Detect a corpse that could be raised.
//
mobj_t*		corpsehit;
mobj_t*		vileobj;
fixed_t		viletryx;
fixed_t		viletryy;

boolean PIT_VileCheck (mobj_t*	thing)
{
    int		maxdist;
    boolean	check;
	
    if (!(thing->flags & MF_CORPSE) )
	return true;	// not a monster
    
    if (thing->tics != -1)
	return true;	// not lying still yet
    
    if (thing->info->raisestate == S_NULL)
	return true;	// monster doesn't have a raise state
    
    maxdist = thing->info->radius + mobjinfo[MT_VILE].radius;
	
    if ( abs(thing->x - viletryx) > maxdist
	 || abs(thing->y - viletryy) > maxdist )
	return true;		// not actually touching
		
    corpsehit = thing;
    corpsehit->momx = corpsehit->momy = 0;
    corpsehit->height <<= 2;
    check = P_CheckPosition (corpsehit, corpsehit->x, corpsehit->y);
    corpsehit->height >>= 2;

    if (!check)
	return true;		// doesn't fit here
		
    return false;		// got one, so stop checking
}



//
// A_VileChase
// Check for ressurecting a body
//
void A_VileChase (mobj_t* actor)
{
    int			xl;
    int			xh;
    int			yl;
    int			yh;
    
    int			bx;
    int			by;

    mobjinfo_t*		info;
    mobj_t*		temp;
	
    if (actor->movedir != DI_NODIR)
    {
	// check for corpses to raise
	viletryx =
	    actor->x + actor->info->speed*xspeed[actor->movedir];
	viletryy =
	    actor->y + actor->info->speed*yspeed[actor->movedir];

	xl = (viletryx - bmaporgx - MAXRADIUS*2)>>MAPBLOCKSHIFT;
	xh = (viletryx - bmaporgx + MAXRADIUS*2)>>MAPBLOCKSHIFT;
	yl = (viletryy - bmaporgy - MAXRADIUS*2)>>MAPBLOCKSHIFT;
	yh = (viletryy - bmaporgy + MAXRADIUS*2)>>MAPBLOCKSHIFT;
	
	vileobj = actor;
	for (bx=xl ; bx<=xh ; bx++)
	{
	    for (by=yl ; by<=yh ; by++)
	    {
		// Call PIT_VileCheck to check
		// whether object is a corpse
		// that canbe raised.
		if (!P_BlockThingsIterator(bx,by,PIT_VileCheck))
		{
		    // got one!
		    temp = actor->target;
		    actor->target = corpsehit;
		    A_FaceTarget (actor);
		    actor->target = temp;
					
		    P_SetMobjState (actor, S_VILE_HEAL1);
		    S_StartSound (corpsehit, sfx_slop);
		    info = corpsehit->info;
		    
		    P_SetMobjState (corpsehit,info->raisestate);
		    // (mod) Restore the LIVING height + radius from mobjinfo instead of `<<= 2`.
		    // A corpse a crusher squashed to height 0 stays 0 (0<<2==0) and comes back a
		    // "ghost monster": immune to hitscans/projectiles and able to drift through walls.
		    // This is crispy-doom's ghost-monster fix (applies to Arch-Viles + the AI director).
		    corpsehit->height = info->height;
		    corpsehit->radius = info->radius;
		    corpsehit->flags = info->flags;
		    corpsehit->health = info->spawnhealth;
		    corpsehit->target = NULL;

		    return;
		}
	    }
	}
    }

    // Return to normal attack.
    A_Chase (actor);
}


//
// A_HealChase (MBF21)
// Like A_VileChase, but a DEHACKED-defined healer picks its own heal animation and
// sound: args[0] = state the healer jumps to on a successful raise (0 = don't change
// state), args[1] = sound played at the corpse (0 = silent).  Falls through to A_Chase
// when there's nothing to raise.
//
void A_HealChase (mobj_t* actor)
{
    int		xl, xh, yl, yh, bx, by;
    mobjinfo_t*	info;
    mobj_t*	temp;
    int		healstate;
    int		healsound;

    if (!actor || !actor->state) return;
    healstate = (int) actor->state->args[0];
    healsound = (int) actor->state->args[1];

    if (actor->movedir != DI_NODIR)
    {
	viletryx = actor->x + actor->info->speed*xspeed[actor->movedir];
	viletryy = actor->y + actor->info->speed*yspeed[actor->movedir];

	xl = (viletryx - bmaporgx - MAXRADIUS*2)>>MAPBLOCKSHIFT;
	xh = (viletryx - bmaporgx + MAXRADIUS*2)>>MAPBLOCKSHIFT;
	yl = (viletryy - bmaporgy - MAXRADIUS*2)>>MAPBLOCKSHIFT;
	yh = (viletryy - bmaporgy + MAXRADIUS*2)>>MAPBLOCKSHIFT;

	vileobj = actor;
	for (bx=xl ; bx<=xh ; bx++)
	  for (by=yl ; by<=yh ; by++)
	    if (!P_BlockThingsIterator(bx,by,PIT_VileCheck))
	    {
		temp = actor->target;
		actor->target = corpsehit;
		A_FaceTarget (actor);
		actor->target = temp;

		if (healstate) P_SetMobjState (actor, (statenum_t)healstate);
		if (healsound)  S_StartSound (corpsehit, healsound);
		info = corpsehit->info;
		P_SetMobjState (corpsehit, info->raisestate);
		corpsehit->height = info->height;	// crispy ghost-monster fix (see A_VileChase)
		corpsehit->radius = info->radius;
		corpsehit->flags  = info->flags;
		corpsehit->health = info->spawnhealth;
		corpsehit->target = NULL;
		return;
	    }
    }

    A_Chase (actor);
}


//
// A_VileStart
//
void A_VileStart (mobj_t* actor)
{
    S_StartSound (actor, sfx_vilatk);
}


//
// A_Fire
// Keep fire in front of player unless out of sight
//
void A_Fire (mobj_t* actor);

void A_StartFire (mobj_t* actor)
{
    S_StartSound(actor,sfx_flamst);
    A_Fire(actor);
}

void A_FireCrackle (mobj_t* actor)
{
    S_StartSound(actor,sfx_flame);
    A_Fire(actor);
}

void A_Fire (mobj_t* actor)
{
    mobj_t*	dest;
    unsigned	an;
		
    dest = actor->tracer;
    if (!dest)
	return;
		
    // don't move it if the vile lost sight
    if (!P_CheckSight (actor->target, dest) )
	return;

    an = dest->angle >> ANGLETOFINESHIFT;

    P_UnsetThingPosition (actor);
    actor->x = dest->x + FixedMul (24*FRACUNIT, finecosine[an]);
    actor->y = dest->y + FixedMul (24*FRACUNIT, finesine[an]);
    actor->z = dest->z;
    P_SetThingPosition (actor);
}



//
// A_VileTarget
// Spawn the hellfire
//
void A_VileTarget (mobj_t*	actor)
{
    mobj_t*	fog;
	
    if (!actor->target)
	return;

    A_FaceTarget (actor);

    fog = P_SpawnMobj (actor->target->x,
		       actor->target->x,
		       actor->target->z, MT_FIRE);
    
    actor->tracer = fog;
    fog->target = actor;
    fog->tracer = actor->target;
    A_Fire (fog);
}




//
// A_VileAttack
//
void A_VileAttack (mobj_t* actor)
{	
    mobj_t*	fire;
    int		an;
	
    if (!actor->target)
	return;
    
    A_FaceTarget (actor);

    if (!P_CheckSight (actor, actor->target) )
	return;

    S_StartSound (actor, sfx_barexp);
    P_DamageMobj (actor->target, actor, actor, 20);
    actor->target->momz = 1000*FRACUNIT/actor->target->info->mass;
	
    an = actor->angle >> ANGLETOFINESHIFT;

    fire = actor->tracer;

    if (!fire)
	return;
		
    // move the fire between the vile and the player
    fire->x = actor->target->x - FixedMul (24*FRACUNIT, finecosine[an]);
    fire->y = actor->target->y - FixedMul (24*FRACUNIT, finesine[an]);	
    P_RadiusAttack (fire, actor, 70 );
}




//
// Mancubus attack,
// firing three missiles (bruisers)
// in three different directions?
// Doesn't look like it. 
//
#define	FATSPREAD	(ANG90/8)

void A_FatRaise (mobj_t *actor)
{
    A_FaceTarget (actor);
    S_StartSound (actor, sfx_manatk);
}


void A_FatAttack1 (mobj_t* actor)
{
    mobj_t*	mo;
    int		an;
	
    A_FaceTarget (actor);
    // Change direction  to ...
    actor->angle += FATSPREAD;
    P_SpawnMissile (actor, actor->target, MT_FATSHOT);

    mo = P_SpawnMissile (actor, actor->target, MT_FATSHOT);
    mo->angle += FATSPREAD;
    an = mo->angle >> ANGLETOFINESHIFT;
    mo->momx = FixedMul (mo->info->speed, finecosine[an]);
    mo->momy = FixedMul (mo->info->speed, finesine[an]);
}

void A_FatAttack2 (mobj_t* actor)
{
    mobj_t*	mo;
    int		an;

    A_FaceTarget (actor);
    // Now here choose opposite deviation.
    actor->angle -= FATSPREAD;
    P_SpawnMissile (actor, actor->target, MT_FATSHOT);

    mo = P_SpawnMissile (actor, actor->target, MT_FATSHOT);
    mo->angle -= FATSPREAD*2;
    an = mo->angle >> ANGLETOFINESHIFT;
    mo->momx = FixedMul (mo->info->speed, finecosine[an]);
    mo->momy = FixedMul (mo->info->speed, finesine[an]);
}

void A_FatAttack3 (mobj_t*	actor)
{
    mobj_t*	mo;
    int		an;

    A_FaceTarget (actor);
    
    mo = P_SpawnMissile (actor, actor->target, MT_FATSHOT);
    mo->angle -= FATSPREAD/2;
    an = mo->angle >> ANGLETOFINESHIFT;
    mo->momx = FixedMul (mo->info->speed, finecosine[an]);
    mo->momy = FixedMul (mo->info->speed, finesine[an]);

    mo = P_SpawnMissile (actor, actor->target, MT_FATSHOT);
    mo->angle += FATSPREAD/2;
    an = mo->angle >> ANGLETOFINESHIFT;
    mo->momx = FixedMul (mo->info->speed, finecosine[an]);
    mo->momy = FixedMul (mo->info->speed, finesine[an]);
}


//
// SkullAttack
// Fly at the player like a missile.
//
#define	SKULLSPEED		(20*FRACUNIT)

void A_SkullAttack (mobj_t* actor)
{
    mobj_t*		dest;
    angle_t		an;
    int			dist;

    if (!actor->target)
	return;
		
    dest = actor->target;	
    actor->flags |= MF_SKULLFLY;

    S_StartSound (actor, actor->info->attacksound);
    A_FaceTarget (actor);
    an = actor->angle >> ANGLETOFINESHIFT;
    actor->momx = FixedMul (SKULLSPEED, finecosine[an]);
    actor->momy = FixedMul (SKULLSPEED, finesine[an]);
    dist = P_AproxDistance (dest->x - actor->x, dest->y - actor->y);
    dist = dist / SKULLSPEED;
    
    if (dist < 1)
	dist = 1;
    actor->momz = (dest->z+(dest->height>>1) - actor->z) / dist;
}


//
// A_PainShootSkull
// Spawn a lost soul and launch it at the target
//
void
A_PainShootSkull
( mobj_t*	actor,
  angle_t	angle )
{
    fixed_t	x;
    fixed_t	y;
    fixed_t	z;
    
    mobj_t*	newmobj;
    angle_t	an;
    int		prestep;
    int		count;
    thinker_t*	currentthinker;

    // count total number of skull currently on the level
    count = 0;

    currentthinker = thinkercap.next;
    while (currentthinker != &thinkercap)
    {
	if (   (currentthinker->function.acp1 == (actionf_p1)P_MobjThinker)
	    && ((mobj_t *)currentthinker)->type == MT_SKULL)
	    count++;
	currentthinker = currentthinker->next;
    }

    // if there are allready 20 skulls on the level,
    // don't spit another one
    if (count > 20)
	return;


    // okay, there's playe for another one
    an = angle >> ANGLETOFINESHIFT;
    
    prestep =
	4*FRACUNIT
	+ 3*(actor->info->radius + mobjinfo[MT_SKULL].radius)/2;
    
    x = actor->x + FixedMul (prestep, finecosine[an]);
    y = actor->y + FixedMul (prestep, finesine[an]);
    z = actor->z + 8*FRACUNIT;
		
    newmobj = P_SpawnMobj (x , y, z, MT_SKULL);

    // Check for movements.
    if (!P_TryMove (newmobj, newmobj->x, newmobj->y))
    {
	// kill it immediately
	P_DamageMobj (newmobj,actor,actor,10000);	
	return;
    }
		
    newmobj->target = actor->target;
    A_SkullAttack (newmobj);
}


//
// A_PainAttack
// Spawn a lost soul and launch it at the target
// 
void A_PainAttack (mobj_t* actor)
{
    if (!actor->target)
	return;

    A_FaceTarget (actor);
    A_PainShootSkull (actor, actor->angle);
}


void A_PainDie (mobj_t* actor)
{
    A_Fall (actor);
    A_PainShootSkull (actor, actor->angle+ANG90);
    A_PainShootSkull (actor, actor->angle+ANG180);
    A_PainShootSkull (actor, actor->angle+ANG270);
}






void A_Scream (mobj_t* actor)
{
    int		sound;
	
    switch (actor->info->deathsound)
    {
      case 0:
	return;
		
      case sfx_podth1:
      case sfx_podth2:
      case sfx_podth3:
	sound = sfx_podth1 + P_Random ()%3;
	break;
		
      case sfx_bgdth1:
      case sfx_bgdth2:
	sound = sfx_bgdth1 + P_Random ()%2;
	break;
	
      default:
	sound = actor->info->deathsound;
	break;
    }

    // Check for bosses.
    if (actor->type==MT_SPIDER
	|| actor->type == MT_CYBORG)
    {
	// full volume
	S_StartSound (NULL, sound);
    }
    else
	S_StartSound (actor, sound);
}


void A_XScream (mobj_t* actor)
{
    S_StartSound (actor, sfx_slop);	
}

void A_Pain (mobj_t* actor)
{
    int	bs = P_Buddy_BodySfx (actor, BUDDYSND_PAIN);	// (buddy) alt-buddy's own pain voice
    if (bs >= 0)
    {
	S_StartSound (actor, bs);
	return;
    }
    if (actor->info->painsound)
	S_StartSound (actor, actor->info->painsound);
}



void A_Fall (mobj_t *actor)
{
    // actor is on ground, it can be walked over
    actor->flags &= ~MF_SOLID;

    // So change this if corpse objects
    // are meant to be obstacles.
}


//
// A_Explode
//
void A_Explode (mobj_t* thingy)
{
    P_RadiusAttack ( thingy, thingy->target, 128 );
}


//
// Run one UMAPINFO boss-action line special (unconditionally -- a dead boss has no
// ->player, so it can't go through the player-gated P_CrossSpecialLine).  Covers the
// canonical bossaction specials from the UMAPINFO spec plus a Boom-generalized
// fallback; other specials are a no-op (documented Stage-2 limitation).
static void U_RunBossActionSpecial (line_t* junk, mobj_t* mo)
{
    switch (junk->special)
    {
      case 11: case 52:  G_ExitLevel ();       break;	// exit
      case 51: case 124: G_SecretExitLevel (); break;	// secret exit
      case 23: case 38:  EV_DoFloor (junk, lowerFloorToLowest); break;	// classic "666"
      case 30:           EV_DoFloor (junk, raiseToTexture);     break;	// classic "667"
      case 19: case 36: case 37: case 45:
			 EV_DoFloor (junk, lowerFloor);            break;
      case 5:  case 24:  EV_DoFloor (junk, raiseFloor);          break;
      case 18: case 69:  EV_DoFloor (junk, raiseFloorToNearest); break;
      default:
	P_DoGenLineSpecial (junk, mo, 0);	// Boom generalized (walk) -- takes a line*
	break;
    }
}

// A_BossDeath
// Possibly trigger special effects
// if on first boss level
//
void A_BossDeath (mobj_t* mo)
{
    thinker_t*	th;
    mobj_t*	mo2;
    line_t	junk;
    int		i;

    // (H) Heretic episode-boss death (crispy heretic A_BossDeath): on MxM8, once the
    // last boss of this episode's type is dead, lower the tag-666 floor to open the
    // exit.  Heretic bosses don't carry DOOM's MBF21 boss flags / UMAPINFO, so handle
    // them here and return before the DOOM logic.
    {
	extern int heretic_mode;
	if (heretic_mode)
	{
	    static const int hbossType[5] =
		{ MT_HIRONLICH, MT_HMINOTAUR, MT_HDSPARIL, MT_HIRONLICH, MT_HMINOTAUR };
	    int ep = gameepisode - 1;
	    if (gamemap != 8 || ep < 0 || ep > 4 || (int) mo->type != hbossType[ep])
		return;
	    for (th = thinkercap.next ; th != &thinkercap ; th = th->next)
	    {
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
		    continue;
		mo2 = (mobj_t *) th;
		if (mo2 != mo && mo2->type == mo->type && mo2->health > 0)
		    return;			// another boss of this type still alive
	    }
	    junk.tag = 666;
	    EV_DoFloor (&junk, lowerFloor);
	    return;
	}
    }

    // UMAPINFO boss actions REPLACE the hardcoded episode-boss behaviour when the
    // current map defines its own (or bossaction=clear to suppress them entirely).
    {
	umap_t*	um = U_LookupMap (gameepisode, gamemap);
	if (um && (um->numbossactions > 0 || um->bossaction_clear))
	{
	    int j; boolean listed = false;
	    for (j = 0; j < um->numbossactions; j++)
		if (um->bossactions[j].type == (int) mo->type) { listed = true; break; }
	    if (!listed)
		return;				// not a boss on this map

	    for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i] && players[i].health > 0) break;
	    if (i == MAXPLAYERS)
		return;				// nobody alive -> don't fire

	    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	    {
		if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
		mo2 = (mobj_t*)th;
		if (mo2 != mo && mo2->type == mo->type && mo2->health > 0)
		    return;			// not all of this type dead yet
	    }

	    memset (&junk, 0, sizeof junk);
	    for (j = 0; j < um->numbossactions; j++)
	    {
		if (um->bossactions[j].type != (int) mo->type) continue;
		junk.special = (short) um->bossactions[j].special;
		junk.tag     = (short) um->bossactions[j].tag;
		U_RunBossActionSpecial (&junk, mo);
	    }
	    return;
	}
    }

    // mbf21: an actor tagged with a boss flag (from DEHACKED) triggers that boss's map
    // special regardless of type/episode/map.  Vanilla actors without these flags fall
    // through to the hardcoded episode logic below.
    if (mo->flags2 & (MF2_MAP07BOSS1|MF2_MAP07BOSS2|MF2_E1M8BOSS|MF2_E2M8BOSS
		      |MF2_E3M8BOSS|MF2_E4M6BOSS|MF2_E4M8BOSS))
    {
	for (i = 0; i < MAXPLAYERS; i++)
	    if (playeringame[i] && players[i].health > 0) break;
	if (i == MAXPLAYERS) return;			// nobody alive -> don't fire

	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
	    if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
	    mo2 = (mobj_t*)th;
	    if (mo2 != mo && mo2->type == mo->type && mo2->health > 0)
		return;					// another of this boss still alive
	}

	memset (&junk, 0, sizeof junk);
	if (mo->flags2 & (MF2_MAP07BOSS1 | MF2_E1M8BOSS | MF2_E4M8BOSS))
	    { junk.tag = 666; EV_DoFloor (&junk, lowerFloorToLowest); }
	if (mo->flags2 & MF2_MAP07BOSS2)
	    { junk.tag = 667; EV_DoFloor (&junk, raiseToTexture); }
	if (mo->flags2 & MF2_E4M6BOSS)
	    { junk.tag = 666; EV_DoDoor (&junk, blazeOpen); }
	if (mo->flags2 & (MF2_E2M8BOSS | MF2_E3M8BOSS))
	    G_ExitLevel ();
	return;
    }

    if ( gamemode == commercial)
    {
	if (gamemap != 7)
	    return;
		
	if ((mo->type != MT_FATSO)
	    && (mo->type != MT_BABY))
	    return;
    }
    else
    {
	switch(gameepisode)
	{
	  case 1:
	    if (gamemap != 8)
		return;

	    if (mo->type != MT_BRUISER)
		return;
	    break;
	    
	  case 2:
	    if (gamemap != 8)
		return;

	    if (mo->type != MT_CYBORG)
		return;
	    break;
	    
	  case 3:
	    if (gamemap != 8)
		return;
	    
	    if (mo->type != MT_SPIDER)
		return;
	    
	    break;
	    
	  case 4:
	    switch(gamemap)
	    {
	      case 6:
		if (mo->type != MT_CYBORG)
		    return;
		break;
		
	      case 8: 
		if (mo->type != MT_SPIDER)
		    return;
		break;
		
	      default:
		return;
		break;
	    }
	    break;
	    
	  default:
	    if (gamemap != 8)
		return;
	    break;
	}
		
    }

    
    // make sure there is a player alive for victory
    for (i=0 ; i<MAXPLAYERS ; i++)
	if (playeringame[i] && players[i].health > 0)
	    break;
    
    if (i==MAXPLAYERS)
	return;	// no one left alive, so do not end game
    
    // scan the remaining thinkers to see
    // if all bosses are dead
    for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
    {
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	
	mo2 = (mobj_t *)th;
	if (mo2 != mo
	    && mo2->type == mo->type
	    && mo2->health > 0)
	{
	    // other boss not dead
	    return;
	}
    }
	
    // victory!
    if ( gamemode == commercial)
    {
	if (gamemap == 7)
	{
	    if (mo->type == MT_FATSO)
	    {
		junk.tag = 666;
		EV_DoFloor(&junk,lowerFloorToLowest);
		return;
	    }
	    
	    if (mo->type == MT_BABY)
	    {
		junk.tag = 667;
		EV_DoFloor(&junk,raiseToTexture);
		return;
	    }
	}
    }
    else
    {
	switch(gameepisode)
	{
	  case 1:
	    junk.tag = 666;
	    EV_DoFloor (&junk, lowerFloorToLowest);
	    return;
	    break;
	    
	  case 4:
	    switch(gamemap)
	    {
	      case 6:
		junk.tag = 666;
		EV_DoDoor (&junk, blazeOpen);
		return;
		break;
		
	      case 8:
		junk.tag = 666;
		EV_DoFloor (&junk, lowerFloorToLowest);
		return;
		break;
	    }
	}
    }
	
    G_ExitLevel ();
}


void A_Hoof (mobj_t* mo)
{
    S_StartSound (mo, sfx_hoof);
    A_Chase (mo);
}

void A_Metal (mobj_t* mo)
{
    S_StartSound (mo, sfx_metal);
    A_Chase (mo);
}

void A_BabyMetal (mobj_t* mo)
{
    S_StartSound (mo, sfx_bspwlk);
    A_Chase (mo);
}

void
A_OpenShotgun2
( player_t*	player,
  pspdef_t*	psp )
{
    S_StartSound (player->mo, sfx_dbopn);
}

void
A_LoadShotgun2
( player_t*	player,
  pspdef_t*	psp )
{
    S_StartSound (player->mo, sfx_dbload);
}

void
A_ReFire
( player_t*	player,
  pspdef_t*	psp );

void
A_CloseShotgun2
( player_t*	player,
  pspdef_t*	psp )
{
    S_StartSound (player->mo, sfx_dbcls);
    A_ReFire(player,psp);
}



mobj_t*		braintargets[32];
int		numbraintargets;
int		braintargeton;

void A_BrainAwake (mobj_t* mo)
{
    thinker_t*	thinker;
    mobj_t*	m;
	
    // find all the target spots
    numbraintargets = 0;
    braintargeton = 0;
	
    thinker = thinkercap.next;
    for (thinker = thinkercap.next ;
	 thinker != &thinkercap ;
	 thinker = thinker->next)
    {
	if (thinker->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;	// not a mobj

	m = (mobj_t *)thinker;

	if (m->type == MT_BOSSTARGET )
	{
	    braintargets[numbraintargets] = m;
	    numbraintargets++;
	}
    }
	
    S_StartSound (NULL,sfx_bossit);
}


void A_BrainPain (mobj_t*	mo)
{
    S_StartSound (NULL,sfx_bospn);
}


void A_BrainScream (mobj_t*	mo)
{
    int		x;
    int		y;
    int		z;
    mobj_t*	th;
	
    for (x=mo->x - 196*FRACUNIT ; x< mo->x + 320*FRACUNIT ; x+= FRACUNIT*8)
    {
	y = mo->y - 320*FRACUNIT;
	z = 128 + P_Random()*2*FRACUNIT;
	th = P_SpawnMobj (x,y,z, MT_ROCKET);
	th->momz = P_Random()*512;

	P_SetMobjState (th, S_BRAINEXPLODE1);

	th->tics -= P_Random()&7;
	if (th->tics < 1)
	    th->tics = 1;
    }
	
    S_StartSound (NULL,sfx_bosdth);
}



void A_BrainExplode (mobj_t* mo)
{
    int		x;
    int		y;
    int		z;
    mobj_t*	th;
	
    x = mo->x + (P_Random () - P_Random ())*2048;
    y = mo->y;
    z = 128 + P_Random()*2*FRACUNIT;
    th = P_SpawnMobj (x,y,z, MT_ROCKET);
    th->momz = P_Random()*512;

    P_SetMobjState (th, S_BRAINEXPLODE1);

    th->tics -= P_Random()&7;
    if (th->tics < 1)
	th->tics = 1;
}


void A_BrainDie (mobj_t*	mo)
{
    G_ExitLevel ();
}

void A_BrainSpit (mobj_t*	mo)
{
    mobj_t*	targ;
    mobj_t*	newmobj;
    
    static int	easy = 0;
	
    easy ^= 1;
    if (gameskill <= sk_easy && (!easy))
	return;
		
    // shoot a cube at current target
    targ = braintargets[braintargeton];
    braintargeton = (braintargeton+1)%numbraintargets;

    // spawn brain missile
    newmobj = P_SpawnMissile (mo, targ, MT_SPAWNSHOT);
    newmobj->target = targ;
    newmobj->reactiontime =
	((targ->y - mo->y)/newmobj->momy) / newmobj->state->tics;

    S_StartSound(NULL, sfx_bospit);
}



void A_SpawnFly (mobj_t* mo);

// travelling cube sound
void A_SpawnSound (mobj_t* mo)	
{
    S_StartSound (mo,sfx_boscub);
    A_SpawnFly(mo);
}

void A_SpawnFly (mobj_t* mo)
{
    mobj_t*	newmobj;
    mobj_t*	fog;
    mobj_t*	targ;
    int		r;
    mobjtype_t	type;
	
    if (--mo->reactiontime)
	return;	// still flying
	
    targ = mo->target;

    // First spawn teleport fog.
    fog = P_SpawnMobj (targ->x, targ->y, targ->z, MT_SPAWNFIRE);
    S_StartSound (fog, sfx_telept);

    // Randomly select monster to spawn.
    r = P_Random ();

    // Probability distribution (kind of :),
    // decreasing likelihood.
    if ( r<50 )
	type = MT_TROOP;
    else if (r<90)
	type = MT_SERGEANT;
    else if (r<120)
	type = MT_SHADOWS;
    else if (r<130)
	type = MT_PAIN;
    else if (r<160)
	type = MT_HEAD;
    else if (r<162)
	type = MT_VILE;
    else if (r<172)
	type = MT_UNDEAD;
    else if (r<192)
	type = MT_BABY;
    else if (r<222)
	type = MT_FATSO;
    else if (r<246)
	type = MT_KNIGHT;
    else
	type = MT_BRUISER;		

    newmobj	= P_SpawnMobj (targ->x, targ->y, targ->z, type);
    if (P_LookForPlayers (newmobj, true) )
	P_SetMobjState (newmobj, newmobj->info->seestate);
	
    // telefrag anything in this spot
    P_TeleportMove (newmobj, newmobj->x, newmobj->y);

    // remove self (i.e., cube).
    P_RemoveMobj (mo);
}



void A_PlayerScream (mobj_t* mo)
{
    // Default death sound.
    int		sound = sfx_pldeth;

    // (buddy) an alternative buddy dies with its own death sound, not the marine's.
    {
	int bs = P_Buddy_BodySfx (mo, BUDDYSND_DEATH);
	if (bs >= 0) { S_StartSound (mo, bs); return; }
    }

    if ( (gamemode == commercial)
	&& 	(mo->health < -50))
    {
	// IF THE PLAYER DIES
	// LESS THAN -50% WITHOUT GIBBING
	sound = sfx_pdiehi;
    }
    
    S_StartSound (mo, sound);
}
