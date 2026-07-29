// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	(S) Strife inventory-item use-effects.
//
//	The nine Strife inventory items share the generic player_t.inventory[] list
//	(the same array the DOOM overflow inventory in p_invent.c and the Heretic /
//	Hexen artifacts drive).  Their on-floor pickup actors live in
//	files/strife_items.c; their USE effects live here, as a separate additive
//	module (same mechanism as files/p_inv_heretic.c), dispatched from
//	p_invent.c's P_UseArtifact when the selected slot is a s_arti_* item.
//
//	Effect values, ported/approximated from strife-ve src/strife/{p_pspr.c,
//	p_user.c,p_inter.c} for this 1993 id C core (no native Strife item logic):
//	  med patch     +10 HP            med kit    +25 HP
//	  stamina/surgery full heal        targeter  pw_infrared (light-amp aim aid)
//	  shadow armor  pw_invisibility + MF_SHADOW    envirosuit pw_ironfeet
//	  teleporter beacon -> spawn a friendly rebel (MF_FRIEND) beside the player
//	  degnin ore    -> drop a shootable ore that detonates when shot
//	  coin          not usable
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"			// players[], consoleplayer
#include "info.h"
#include "m_fixed.h"
#include "tables.h"			// finecosine/finesine, ANGLETOFINESHIFT
#include "sounds.h"
#include "s_sound.h"
#include "p_local.h"			// P_SpawnMobj, mobj_t, MF_*
#include "p_inv_strife.h"

extern state_t   *states;
extern mobjinfo_t *mobjinfo;

// engine pieces we call (declared by hand, like p_inv_heretic.c)
extern boolean	P_GiveBody (player_t* player, int num);


// ---------------------------------------------------------------------------
// Effects (ApplyStrifeArtifact): return true if the item was consumed.
// ---------------------------------------------------------------------------
boolean ApplyStrifeArtifact (player_t* player, artitype_t a)
{
    mobj_t*	mo = player->mo;

    switch (a)
    {
      case s_arti_medpatch:		// Med Patch: +10 HP, refuse at full.
	if (!P_GiveBody (player, 10))
	{
	    player->message = "CANNOT USE MED PATCH (FULL HEALTH)";
	    return false;
	}
	player->message = "USED MED PATCH";
	return true;

      case s_arti_medkit:		// Medical Kit: +25 HP, refuse at full.
	if (!P_GiveBody (player, 25))
	{
	    player->message = "CANNOT USE MEDICAL KIT (FULL HEALTH)";
	    return false;
	}
	player->message = "USED MEDICAL KIT";
	return true;

      case s_arti_stamina:		// Stamina Implant / Surgery Kit: full heal.
	if (!P_GiveBody (player, MAXHEALTH))	// heals to the 100 HP cap
	{
	    player->message = "CANNOT USE SURGERY KIT (FULL HEALTH)";
	    return false;
	}
	player->message = "USED SURGERY KIT";
	return true;

      case s_arti_targeter:		// Targeter: light-amp / aim aid.
	player->powers[pw_infrared] = INFRATICS;
	player->message = "USED TARGETER";
	return true;

      case s_arti_shadowarmor:		// Shadow Armor: invisibility + MF_SHADOW.
	player->powers[pw_invisibility] = INVISTICS;
	if (mo) mo->flags |= MF_SHADOW;
	player->message = "USED SHADOW ARMOR";
	return true;

      case s_arti_envirosuit:		// Environmental Suit: radiation shielding.
	player->powers[pw_ironfeet] = IRONTICS;
	player->message = "USED ENVIRONMENTAL SUIT";
	return true;

      case s_arti_beacon:
      {
	// Teleporter Beacon: drop a friendly rebel (MT_S_REBEL1 + MF_FRIEND) just
	// in front of the player to fight alongside you.  (Strife spawns several
	// over a timer; one is a fair approximation for this engine.)  // TODO:
	// the full beacon spawns a squad on a fuse.
	mobj_t*	  reb;
	unsigned  an;
	if (!mo) { player->message = "CANNOT USE TELEPORTER BEACON"; return false; }
	an  = mo->angle >> ANGLETOFINESHIFT;
	reb = P_SpawnMobj (mo->x + 32*finecosine[an], mo->y + 32*finesine[an],
			   mo->z, MT_S_REBEL1);
	if (reb)
	{
	    reb->flags |= MF_FRIEND;
	    reb->target = NULL;
	}
	player->message = "USED TELEPORTER BEACON";
	return true;
      }

      case s_arti_degninore:
      {
	// Degnin Ore: drop a live ore just in front of the player.  It is solid +
	// shootable (MF_SHOOTABLE) and detonates into the BNG3 blast (A_Explode)
	// when shot -- the same way you clear a force field in Strife.  Its
	// ->target is the player so the blast attributes to them.
	mobj_t*	  ore;
	unsigned  an;
	if (!mo) { player->message = "CANNOT USE DEGNIN ORE"; return false; }
	an  = mo->angle >> ANGLETOFINESHIFT;
	ore = P_SpawnMobj (mo->x + 32*finecosine[an], mo->y + 32*finesine[an],
			   mo->z, MT_S_DEGNINORE);
	if (ore)
	    ore->target = mo;
	player->message = "DROPPED DEGNIN ORE";
	return true;
      }

      case s_arti_coin:			// Gold: currency counter, not usable.
	return false;

      default:
	return false;
    }
}
