# coding=UTF-8

# Dehacked generator for Knee-Deep in KDiZD, for use with DEH9000.
# Patch written by esselfortium

from __future__ import print_function
from deh9000 import *

# Free up a bunch of states in advance.
mobjinfo[MT_VILE].clear() # archvile
mobjinfo[MT_FATSO].clear() # mancubus
mobjinfo[MT_KEEN].clear() # commander keen
mobjinfo[MT_WOLFSS].clear() # wolfenstein ss
mobjinfo[MT_SPIDER].clear() # spider mastermind
mobjinfo[MT_CYBORG].clear() # cyberdemon
# mobjinfo[MT_BABY].clear() # arachnotron
mobjinfo[MT_PAIN].clear() # pain elemental
mobjinfo[MT_BOSSSPIT].clear() # bossbrain spawner
mobjinfo[MT_BOSSTARGET].clear() # bossbrain spawn spot
mobjinfo[MT_SPAWNSHOT].clear() # bossbrain spawn cube
mobjinfo[MT_SPAWNFIRE].clear() # bossbrain spawn fire
mobjinfo[MT_BFG].clear() # bfg projectile
mobjinfo[MT_EXTRABFG].clear() # bfg tracer blasts
weaponinfo[wp_bfg].clear() # bfg weapon
mobjinfo[MT_MISC77].clear() # flaming barrel
mobjinfo[MT_FIRE].clear() # archvile fire

# Remove resurrection states, since we don't have archviles.
for mobj in mobjinfo:
    if (mobj != mobjinfo[MT_POSSESSED]) and (mobj != mobjinfo[MT_HEAD]):
        mobj.raisestate = 0

# Drop a near-identical frame from the Baron/Knight death animations
states[S_BOSS_DIE5].nextstate = S_BOSS_DIE7 # Baron
#states[S_BOS2_DIE5].nextstate = S_BOS2_DIE7 # Hell Knight

# Remove walk-in-place animation from some vanilla monsters' Look cycles:
states[S_TROO_STND].nextstate = S_TROO_STND # Imp
#states[S_SARG_STND].nextstate = S_SARG_STND # Demon/FastDemon
states[S_POSS_STND].nextstate = S_POSS_STND # Zombieman
states[S_SPOS_STND].nextstate = S_SPOS_STND # Shotgunner
states[S_BOSS_STND].nextstate = S_BOSS_STND # Baron/HK

# Fix height of one-legged hanging corpse sprite
mobjinfo[MT_MISC58].height = 84 * FRACUNIT

# -----------------------------------------------------------------------------

"""
Notes on specific monster slots, derived mainly from docs on Eternity's
extended flags at https://eternity.youfailit.net/wiki/Thing_type_flags

Cyberdemon and Spider Mastermind have BOSS, which makes their roars always 
loud and gives them immunity to blast damage.

Revenant has LONGMELEE, which gives it a "restricted non-missile proximity
area".

Revenant, Cyberdemon, and Spider Mastermind have RANGEHALF, which makes them
more likely to use their ranged attack even at longer distances.

Cyberdemon has HIGHERMPROB, which makes it even more likely to use its ranged
attack at a distance.

Arch-Vile has SHORTMRANGE, which reduces its non-melee attack range.

Arch-Vile has DMGIGNORED, which stops other monsters from infighting with it.

Barons and Hell Knights are treated as one species to prevent infighting.

Lost Soul is twice as likely to enter its ranged attack.
"""

# -----------------------------------------------------------------------------

# Monster: RAPID FIRE TROOPER (replaces WolfSS)
m = mobjinfo[MT_WOLFSS]
m.object_name = "Rapid Fire Trooper (WolfSS)"
m.doomednum = 3010
m.spawnhealth = 30
m.speed = 8
m.radius = 20 * FRACUNIT
m.height = 56 * FRACUNIT
m.damage = 0
m.reactiontime = 8
m.painchance = 200
m.mass = 100
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_posit1
m.attacksound = sfx_pistol
m.painsound = sfx_popain
m.deathsound = sfx_podth1
m.activesound = sfx_posact

m.update(states.parse("""
    Spawn: 
      pin(S_SSWV_STND):
      RFTR A 10 A_Look 
      Loop 
    See: 
      RFTR AABBCCDD 4 A_Chase 
      Loop 
    Missile: 
      RFTR E 10 A_FaceTarget 
      RFTR F 5 Bright A_PosAttack 
      RFTR E 5 A_CPosRefire 
      Goto Missile+1
    Pain: 
      RFTR G 3 
      RFTR G 3 A_Pain 
      Goto See 
    Death: 
      RFTR H 5 
      RFTR I 5 A_Scream 
      RFTR J 5 A_Fall 
      RFTR K 5 
      RFTR L -1 
      Stop
    XDeath:
      RFTR M 5
      Goto S_POSS_XDIE2
"""))

# -----------------------------------------------------------------------------

# Monster: KDIZD CHAINGUNNER (replaces Doom2 Chaingunner)
m = mobjinfo[MT_CHAINGUY]
m.clear_states()
m.doomednum = 3011
m.update(states.parse("""
  Spawn:
    CGPS A 10 A_Look
    Loop
  See:
    CGPS AABBCCDD 3 A_Chase
    Loop
  Missile:
    CGPS E 10 A_FaceTarget
    CGPS FE 4 Bright A_CPosAttack
    CGPS F 1 A_CPosRefire
    Goto Missile+1
  Pain:
    CGPS G 3
    CGPS G 3 A_Pain
    Goto See
    Death:
        CGPS H 7 A_Scream
        CGPS I 5 A_Fall
        CGPS JK 4
        CGPS L 4
        CGPS MNO 4
        CGPS P -1
        stop
    XDeath:
        CGPS Q 6 
        CGPS R 5 A_XScream
        CGPS S 5 A_Fall
        CGPS TUVWX 5
        CGPS Y -1
        stop
"""))

# -----------------------------------------------------------------------------

# Monster: HELL KNIGHT COLOR REMAP EXPEDITION - aka "Baronheist 451"
m = mobjinfo[MT_KNIGHT]
m.copy_from(mobjinfo[MT_BRUISER])
m.object_name = "Hell Knight"
m.doomednum = 3012
m.spawnhealth = 500
m.seesound = sfx_kntsit
m.deathsound = sfx_kntdth
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (2 << MF_TRANSSHIFT))
"""
I've performed a Ludicrous Hack(TM) here to combine the Baron and Hell Knight
into one set of sprites, freeing up 29 dehacked states. Since I had already
edited the palette for other reasons, I've edited it some more to rearrange
several color ramps and thus squeeze the Baron and KDiZD's Hell Knight into the
hardcoded color-translation ranges normally used for multiplayer. (So, yes,
player 1 is actually pink now as a side effect.)

Below is some cleanup to make the player corpse decorations appear in their
expected usual green. :)
"""
m = mobjinfo[MT_MISC62] # Player corpse sprite
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (3 << MF_TRANSSHIFT))

m = mobjinfo[MT_MISC68] # Player gibs sprite
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (3 << MF_TRANSSHIFT))

# -----------------------------------------------------------------------------

# Monster: SUICIDE BOMBER (replaces Dead Lost Soul)
m = mobjinfo[MT_MISC65]
m.clear()
mobjinfo[MT_SPAWNSHOT].clear() # clear this to pin some frames
m.object_name = "Suicide Bomber (DeadLostSoul)"
m.doomednum = 3013
m.spawnhealth = 60
m.speed = 10
m.radius = 25 * FRACUNIT
m.height = 56 * FRACUNIT
m.reactiontime = 8
m.painchance = 5
m.mass = 100
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_posit2
m.painsound = sfx_popain
m.deathsound = sfx_barexp
m.update(states.parse("""
  Spawn: 
    BMBE AB 10 A_Look
    loop
  See:
    BMBE A 2 A_BabyMetal # oh you're a fan? name 3 of their albums
    BMBE AABBBCCCDDD 2 A_Chase
    loop
  Pain:
    BMBE A 6 A_Pain
    goto See
  Melee:
    # fall through into:
  Death:
    BMBE E 0 bright A_Explode
    # fall through into:
  XDeath:
    BMBE E 5 bright A_Scream
    BMBE F 5 bright
    BMBE G 5 A_Fall
    goto S_SPOS_XDIE4
"""))
"""We're using A_BabyMetal to play the suicide zombie's scream. Conveniently,
this pointer also has A_Chase baked into it, so we could shave off a frame.

The XDeath jumps past their explosion, so their melee explosion doesn't give
them a second explosion (insert Xzibit meme here)

They're supposed to play the explosion sound and the gib sound simultaneously,
but things in vanilla can only make one sound at a time, so I'm just using
the explosion sound only. CONSIDER: Making a new sound overlaying the two.
"""

# -----------------------------------------------------------------------------

# Monster: SHADOW (replaces Keen)
m = mobjinfo[MT_KEEN]
m.object_name = "Shadow (Keen)"
m.doomednum = 3014
m.spawnhealth = 80
m.speed = 8
m.radius = 20 * FRACUNIT
m.height = 56 * FRACUNIT
m.damage = 0
m.reactiontime = 3
m.painchance = 200
m.mass = 100
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.attacksound = sfx_claw
m.painsound = sfx_popain
m.deathsound = sfx_keendt
m.activesound = sfx_bgact
m.update(states.parse("""
    Spawn: 
      TROX A 5 A_Look 
      Loop
    See: 
      TROX AAABBBCCCDDD 1 A_Chase 
      Loop 
    Missile: 
        TROX E 30 A_FaceTarget
        TROX F 5
        TROX G 5 BRIGHT A_BspiAttack
        TROX F 5
        TROX E 14
        TROX E 1 A_SpidRefire
        Goto Missile+1
    Pain: 
        TROX H 4
        TROX H 4 A_Pain
        Goto See+1
    Death:
        TROX I 5
        TROX J 4 A_Scream
        TROX K 4 
        TROX L 4 A_Fall
        TROX M 4
        TROX N 4
        TROX O -1
        Stop  
"""))
"""I've used the XDeath only, in place of the Shadow's standard death
which relied on spawning a new actor to fall apart into two halves.
I've also sped up its firing cycle, the frame before SpidRefire having
originally lasted 20 tics."""


# Projectile: Shadow Fireball (replaces arachnotron projectile)
m = mobjinfo[MT_ARACHPLAZ]
m.clear() # clear the arachnotron projectile
m.object_name = "Shadow Fireball (Arach Plasma)"
m.speed = 12 * FRACUNIT
m.radius = 6 * FRACUNIT
m.height = 8 * FRACUNIT
m.damage = 4
m.flags = (MF_NOBLOCKMAP | MF_NOGRAVITY | MF_DROPOFF | MF_MISSILE)
m.seesound = sfx_skeatk
m.deathsound = sfx_firxpl
m.update(states.parse("""
    Spawn:
        SBAL ABC 4 Bright
        Loop
    Death:
        SBAL C 5 Bright
        SBAL DEFGH 4 Bright
        Stop
"""))


# -----------------------------------------------------------------------------

# Monster: SOUL HARVESTER (replaces Revenant)
m = mobjinfo[MT_UNDEAD]
m.clear()
m.object_name = "Soul Harvester (Revenant)"
m.doomednum = 3015
m.spawnhealth = 100
m.speed = 10 # original speed was 8/3. revenants are 10/2.
m.radius = 20 * FRACUNIT
m.height = 56 * FRACUNIT
m.damage = 0
m.reactiontime = 3
m.painchance = 160
m.mass = 120
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_skesit
m.painsound = sfx_dmpain
m.deathsound = sfx_skedth
m.activesound = sfx_dmact
m.update(states.parse("""
    Spawn:
        SLHV AB 10 A_Look
        Loop
    See:
        SLHV AABBCCDD 3 A_Chase
        Loop
    Melee:
    Missile:
        # SLHV EFG 4 A_FaceTarget
        # SLHV HIJK 4 Bright A_FaceTarget

        SLHV E 4 A_FaceTarget
        SLHV F 4 A_FaceTarget
        SLHV G 4 A_FaceTarget
        SLHV H 4 Bright A_FaceTarget
        SLHV I 4 Bright A_FaceTarget
        SLHV J 4 Bright A_FaceTarget
        SLHV K 4 Bright A_FaceTarget
        SLHV L 8 Bright A_SkelMissile
        SLHV M 10
        Goto See
    Pain:
        SLHV N 3
        SLHV N 3 A_Pain
        Goto See
    Death:
        SLHV O 7 A_Scream
        SLHV PQR 7 
        SLHV S 6
        SLHV T 5 A_Fall
        SLHV UV 5
        SLHV W -1
        Stop
    XDeath:
        SLHV X 6 A_XScream
        SLHV Y 6
        SLHV Z 6
        SLHV [ 6 A_Fall
        SLHV \ 6
        SLHV ] -1
        Stop
"""))


"""CONSIDER: Maybe edit the death sprites for these to bake in the ghost
effect. For now, it's just left out.
"""


# Projectile effect: Soul Harvester Fireball Trail (replaces revenant smoke)
states[S_TECH2LAMP].frame = 32772   # this is the nextstate from S_TECH2LAMP4,
                                    # which we can't modify. (see note below)
                                    # (32772 is 4 + 32768 to indicate Bright)
states[S_TECH2LAMP].nextstate = S_NULL

sprnames[SPR_TLP2] = "SHBA"     # likewise, we're going to directly change
                                # this sprite name so that the unmodified
                                # S_TECH2LAMP4 will work.
                               
mobjinfo[MT_MISC30].clear() # clear techlamp2 since we're repurposing some
                            # states from it.

m = mobjinfo[MT_SMOKE]
m.clear() # clear the revenant projectile smoke
m.object_name = "Soul Hv Trail (Rev Smoke)"
m.speed = 0 * FRACUNIT
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_NOBLOCKMAP | MF_NOGRAVITY)
m.update(states.parse("""
    Spawn:
        SHBA A 4 Bright
        SHBA BC 4 Bright
        Goto S_TECH2LAMP4
"""))
"""There's some gross hackery going on in this one. I'll explain:
The original DOS Dehacked executable had a bug in which modifying the final
state in the states table caused an overflow, breaking other things.
I've worked around this by leaving that state unmodified, working it and its
nextstate into a sequence of other states.
"""


# Projectile: Soul Harvester Fireball (replaces revenant fireball)
m = mobjinfo[MT_TRACER]
m.clear() # clear the revenant projectile
m.object_name = "Soul Hv Fireball (Rev Rocket)"
m.speed = 8 * FRACUNIT
m.radius = 6 * FRACUNIT
m.height = 8 * FRACUNIT
m.damage = 2
m.flags = (MF_NOBLOCKMAP | MF_NOGRAVITY | MF_DROPOFF | MF_MISSILE)
m.seesound = sfx_firsht
m.deathsound = sfx_firxpl
m.update(states.parse("""
    Spawn:
        pin(S_TRACER):
        SHBA FF 1 Bright A_Tracer
        SHBA G 2 Bright A_Tracer
        
        Goto S_TRACER
    Death:
        SHBA HIJKLMNOPQ 4 Bright
        Stop
"""))
# Whereas revenant missiles have a 50% chance of homing, these have a 75% chance.

# -----------------------------------------------------------------------------

# Monster: IMP
m = mobjinfo[MT_TROOP]
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (2 << MF_TRANSSHIFT))

# -----------------------------------------------------------------------------

# Monster: DARK IMP (replaces BossBrain)
m = mobjinfo[MT_BOSSBRAIN]
m.object_name = "Dark Imp (BossBrain)"
m.doomednum = 3016
m.spawnhealth = 160 # kdizd's dark imps ranged between 110-130 hp.
                    # these are slightly beefier to help round out the midrange
                    # cast for later maps, and still go down in one SSG shot.
m.speed = 8 # kdizd's dark imp speeds ranged between 7-9. stock imp speed is 8
m.radius = 20 * FRACUNIT
m.height = 56 * FRACUNIT
m.damage = 0
m.reactiontime = 8
m.painchance = 180
m.mass = 200
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (1 << MF_TRANSSHIFT))
m.seesound = sfx_bgsit1
m.attacksound = sfx_claw
m.painsound = sfx_popain
m.deathsound = sfx_bgdth1
m.activesound = sfx_bgact
m.xdeathstate = S_TROO_XDIE1
m.update(states.parse("""
    Spawn:
        DRKV AB 10 A_Look
        Loop
    See:
        DRKV AABBCCDD 2 A_Chase
        Loop
    Melee:
        DRKV EF 8 bright A_FaceTarget
        DRKV G 6 bright A_SargAttack
        Goto See
    Missile:
        DRKV EF 6 bright A_FaceTarget
        DRKV G 6 bright A_HeadAttack
        DRKV EF 6 bright A_CPosRefire # check line of sight before firing again
        DRKV G 6 bright A_HeadAttack
        DRKV EF 6 bright A_CPosRefire # check line of sight before firing again
        DRKV G 6 bright A_HeadAttack
        Goto See
    Pain:
        DRKV H 2
        DRKV H 2 A_Pain
        Goto See
    Death:
        DRKV I 8
        DRKV J 8 A_Scream
        DRKV K 5
        DRKV L 5 A_Fall
        DRKV M -1
        Stop
    # XDeath:
    #     DRKV N 5
    #     DRKV O 5 A_XScream
    #     DRKV P 5
    #     DRKV Q 5 A_Fall
    #     DRKV RST 5
    #     DRKV U -1
    #     Stop
"""))

# -----------------------------------------------------------------------------

# Monster: DARK CACO (replaces Arachnotron)
# For when you need your caco spam to also be arachnotron spam
# Sprite edit by Tormentor667
# Sounds by Esselfortium and BouncyTEM
m = mobjinfo[MT_BABY]
m.clear_states()
m.object_name = "Dark Caco (Arachnotron)"
m.doomednum = 3017
m.spawnhealth = 400
m.speed = 8
m.radius = 31 * FRACUNIT
m.height = 56 * FRACUNIT
m.reactiontime = 8
m.painchance = 128
m.mass = 400
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL | MF_NOGRAVITY | MF_FLOAT)
m.seesound = sfx_spisit
m.painsound = sfx_dmpain
m.deathsound = sfx_pedth
m.activesound = sfx_pepain
m.update(states.parse("""
    Spawn:
        ENCD A 10 A_Look
        Loop
    See:
        ENCD A 2 A_Chase
        Loop
    Missile:
        ENCD A 4 A_FaceTarget
        ENCD A 4 Bright A_FaceTarget

        ENCD B 5 Bright A_FaceTarget
        ENCD C 5 Bright A_BspiAttack

        ENCD B 0 Bright A_SpidRefire
        Goto Missile+2
    Pain:
      ENCD D 3
      ENCD E 3 A_Pain
      Goto See
    Death:
      ENCD E 6 A_Scream
      ENCD F 8
      ENCD G 8 A_Fall
      ENCD HI 8
      ENCD J -1
      Stop
    # Raise:
    #   ENCD I 6
    #   ENCD HGFE 4
    #   Goto See
"""))
"""KDiZD didn't add much in the way of flying monsters, so this seemed like
a sensible addition.

#CONSIDER: These sprite recolors are okay but some unique art would be nice!
(Sprite origin: Tormentor's "Enhanced Cacodemon" recolor from Realm667)
"""

# -----------------------------------------------------------------------------

# BRUISER (replaces solid swaying corpse)
# This version uses recursive A_PainAttack!

m = mobjinfo[MT_MISC51]
m.object_name = "Bruiser (Solid Swaying)"
m.doomednum = 3018
m.spawnhealth = 3000
m.speed = 14
m.radius = 20 * FRACUNIT
m.height = 56 * FRACUNIT
m.damage = 0
m.reactiontime = 8
m.painchance = 10
m.mass = 1000
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_brssit #FIXME: Implement correct sounds!
m.attacksound = sfx_claw
m.painsound = sfx_dmpain
m.deathsound = sfx_brsdth
m.activesound = sfx_dmact
m.update(states.parse("""
    Spawn:
        ZBRU AB 10 bright A_Look
        Loop
    See:
        ZBRU AABBCCDD 3 bright A_Chase
        Loop
    Missile:
        ZBRU E 10 bright A_FaceTarget
        ZBRU F 12 bright A_FaceTarget
        ZBRU G 17 bright A_FatAttack1
        ZBRU H 12 bright A_FatAttack3

        ZBRU I 8 bright A_FaceTarget
        ZBRU I 7 bright A_FaceTarget
        ZBRU J 17 bright A_FatAttack2
        ZBRU E 10 bright A_FatAttack1

        ZBRU F 8 bright A_FaceTarget
        ZBRU F 7 bright A_FaceTarget
        ZBRU G 17 bright A_FatAttack3
        ZBRU K 12 bright A_FatAttack2

        ZBRU K 15 bright A_SpidRefire # check line of sight
        ZBRU L 20 bright A_SkelWhoosh # plays SKESWG sound + FaceTarget
        ZBRU M 35 bright A_PainAttack
        Goto See
    Pain:
        ZBRU N 10 bright A_Pain
        # and fall through into:
    Melee:
        ZBRU K 6 bright A_FaceTarget
        ZBRU L 6 bright A_FaceTarget
        ZBRU MCD 8 bright A_SkelMissile # three homing missiles for you
        Goto Missile + 9 # jump to LOS check for fire attack
    Death:
        ZBRD A 5 bright 
        ZBRD B 5 bright A_Scream
        ZBRD CDEFGHI 5 bright
        ZBRD J 5 bright A_KeenDie
        ZBRD K 5 bright
        ZBRD LMNO 5 bright
        ZBRD PQRS 5
        ZBRD T 6
        ZBRD UV 7
        ZBRD W -1
        Stop
"""))

# Relocate Lost Soul to Tall Lamp 2 slot
"""We'll be using the lost soul's normal slot for a boss attack using the Pain
Elemental pointers, so we'll move the lost soul here. (Note that this needs to
happen *after* the Soul Harvester projectile definition, which clears this lamp
slot in order to use its frames for a specific deh bug workaround.)
"""
m = mobjinfo[MT_MISC30] # former home of tall lamp 2
m.copy_from(mobjinfo[MT_SKULL]) # copy entire lost soul entry
m.object_name = "Lost Soul (Tall Lamp 2)"
m.speed = 4 # see below
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL | MF_NOGRAVITY | MF_FLOAT)
m.update(states.parse("""
See:
    VSKL AABB 3 bright A_Chase # see below
    Loop
"""))
"""The lost soul's original slot is hardcoded to do its long-range attack
twice as often as normal. Because I've relocated the lost soul to enable other
effects, I compensated for this by doubling the resolution on its chase states
(halving the duration and the speed), so it moves at the same effective speed
but is twice as likely to switch to an attack. Using more frequent chase states
does mean they're more likely to change direction when in lurking mode, but
hopefully that won't be a particularly significant change."""

# Bruiser Fire
# Spawned recursively by A_PainAttack.
m = mobjinfo[MT_SKULL]
m.clear() # clear out the original lost soul
m.object_name = "Bruiser Fire (Lost Soul)"
m.doomednum = -1
m.speed = 0
m.radius = 10 * FRACUNIT # this affects how far apart they'll spawn
m.height = 56 * FRACUNIT
m.damage = 2
m.reactiontime = 8
m.painchance = 256
m.mass = 50
m.flags = (MF_NOBLOCKMAP | MF_MISSILE)
m.attacksound = sfx_barexp # FIXME: needs a custom sound that's, uh. quieter
m.deathsound = sfx_barexp
m.update(states.parse("""
    Spawn:
        ZFIR A 3 bright
        ZFIR C 3 bright 
        ZFIR D 3 bright
        # fall through to:
    Death: # goes here when hitting something
        ZFIR F 3 bright
        ZFIR J 3 bright A_PainAttack
        ZFIR N 3 bright 
        ZFIR P 3 bright 
        MISC M 85 bright # invisible frame to finetune LS limit effects
        Stop
"""))

# Projectile: Bruiser Projectile (replaces mancubus projectile)
m = mobjinfo[MT_FATSHOT]
m.clear()
m.object_name = "Bruiser Projectile (Mancubus Projectile)"
m.speed = 18 * FRACUNIT
m.radius = 6 * FRACUNIT
m.height = 8 * FRACUNIT
m.damage = 5
m.flags = (MF_NOBLOCKMAP | MF_NOGRAVITY | MF_DROPOFF | MF_MISSILE)
m.seesound = sfx_firsht
m.deathsound = sfx_firxpl
m.update(states.parse("""
    Spawn:
        ZPRJ AB 4 Bright
        Loop
    Death:
        ZPRJ CDEFGHI 4 Bright
        Stop
"""))

# -----------------------------------------------------------------------------

# Monster: MAULER (replaces PoolBrain)
m = mobjinfo[MT_MISC86]
m.clear()
m.object_name = "Mauler (PoolBrain)"
m.doomednum = 3019
m.spawnhealth = 150
m.speed = 10
m.radius = 30 * FRACUNIT
m.height = 56 * FRACUNIT
m.damage = 2
m.reactiontime = 8
m.painchance = 100 # reduced from 180, though skullfly still gets halted easily
m.mass = 400
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_vilsit
m.attacksound = sfx_sgtatk
m.painsound = sfx_dmpain
m.deathsound = sfx_sgtdth
m.activesound = sfx_dmact

m.update(states.parse("""
    Spawn: 
      MAUD A 1 A_FaceTarget #Avoid falling asleep on the job when SkullFly's
                            #hardcoded behavior sends us back to spawn state.
      MAUD A 1 A_Look 
      Loop 
   See: 
      MAUD AABBCCDD 2 A_Chase 
      Loop 
    Melee:
      MAUD EF 5 A_FaceTarget
      MAUD G 5 A_SargAttack
   Missile: 
      MAUD E 5 A_FaceTarget 
      MAUD F 5 A_SkullAttack 
      MAUD G 5
      Goto Missile
   Pain: 
      MAUD H 2 
      MAUD H 2 A_Pain 
      Goto See 
   Death: 
      MAUD I 8 
      MAUD J 8 A_Scream 
      MAUD K 4 
      MAUD L 4 A_Fall 
      MAUD M 4 
      MAUD N -1 
      Stop 
"""))

# -----------------------------------------------------------------------------

# Monster: WARGRIN (replaces Mancubus)
# Wargrin sprites by Nmn: https://forum.zdoom.org/viewtopic.php?f=37&t=66556
# Sounds by BouncyTEM and Esselfortium
m = mobjinfo[MT_FATSO]
m.object_name = "Wargrin (Mancubus)"
m.doomednum = 3020
m.spawnhealth = 250
m.speed = 7
m.radius = 25 * FRACUNIT
m.height = 56 * FRACUNIT
m.reactiontime = 8
m.painchance = 160
m.mass = 200
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_mansit
m.attacksound = sfx_claw
m.painsound = sfx_popain
m.deathsound = sfx_mandth
m.activesound = sfx_manatk

m.update(states.parse("""
   Spawn: 
      GRIN A 3 A_Look 
      Loop 
   See: 
      GRIN AABBCCDD 2 A_Chase 
      Loop
   Melee:
      GRIN H 8 A_FaceTarget
      GRIN I 8 A_SargAttack
      GRIN J 8
      Goto See
   Missile: 
      GRIN E 18 A_FatRaise # plays DSMANATK and does FaceTarget 
      GRIN F 16 Bright A_FatAttack1
      GRIN E 12 A_FaceTarget 
      GRIN F 16 Bright A_FatAttack2
      GRIN E 12 A_FaceTarget 
      GRIN F 16 Bright A_FatAttack3
      GRIN G 16
      Goto See
   Pain: 
      GRIN K 8 A_Pain 
      Goto See
   Death:
      GRIN L 8 A_Scream
      GRIN M 8
      GRIN N 6 A_Fall
      GRIN O 6
      GRIN P -1
      Stop
"""))

# -----------------------------------------------------------------------------

# Monster: HELL WARRIOR (replaces Arch-Vile)
# Using Arch-Vile slot so monster infighting can't take advantage of its long
# pain state.
m = mobjinfo[MT_VILE]
m.object_name = "Hell Warrior (Arch-Vile)"
m.doomednum = 3021
m.spawnhealth = 600
m.speed = 5
m.radius = 20 * FRACUNIT
m.height = 64 * FRACUNIT
m.reactiontime = 8
m.painchance = 50
m.mass = 1000
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_kntsit
m.deathsound = sfx_kntdth
m.painsound = sfx_dmpain
m.activesound = sfx_dmact
m.update(states.parse("""
    Spawn: 
      HWAR A 1 A_Look
      Loop
    See:
      HWAR AAABBBCCCDDD 2 A_Chase
      Loop
    Missile:
      HWAR E 5 A_FaceTarget
      HWAR F 5 A_FaceTarget
      HWAR G 5 A_BruisAttack
      Goto See
    Pain:
      HWAR J 2 A_Pain
                  ##HWAR J 3 A_CyberAttack # shield up
                  ##HWAR H 20 A_Pain
                  # HWAR H 1 A_CPosRefire # LOS check

                # fall through to
                #Melee:
      HWAR H 30 A_CyberAttack # shield up
      HWAR H 1 A_CPosRefire # LOS check

      HWAR I 0 bright A_BspiAttack
      HWAR I 5 bright A_FatAttack3

      HWAR I 7 bright A_CyberAttack # shield up
      HWAR H 23

      HWAR I 1 bright A_CPosRefire # LOS check
      HWAR I 0 bright A_BspiAttack
      HWAR I 5 bright A_FatAttack3

                  #HWAR I 7 bright A_CyberAttack # shield up
                  #HWAR H 23
      Goto See
    Death:
      HWAR K 6 A_Scream
      HWAR L 6 A_Fall
      HWAR MNOPQRS 6
      HWAR T -1
      Stop
"""))

# Swap player rockets to BFG projectile slot,
# so that we can use rocket as another monster projectile type
m = mobjinfo[MT_BFG]
m.copy_from(mobjinfo[MT_ROCKET])
m.objectname = "Rocket (BFG Shot)"
states[S_MISSILE2].action = A_FireBFG # make the RL fire the relocated rocket
miscdata.bfg_cells_per_shot = 1 # use correct ammo amount for A_FireBFG

# Hell Warrior Shield (replaces Rocket)
m = mobjinfo[MT_ROCKET]
m.clear()
mobjinfo[MT_MISC4].clear_states() # blue keycard, since we're going to pin a state from it.
m.object_name = "Hell Warrior Shield (Rocket)"
m.flags = (MF_SHOOTABLE | MF_NOBLOOD)
m.mass = 999999
m.spawnhealth = 0
m.damage = 5
m.speed = 1 # note that this speed is *not* multiplied by FRACUNIT.
            # when doom spawns a projectile, it immediately moves it by half its speed,
            # which in this case amounts to a minuscule 0.000007629394531 of a map unit.
            # this is the lowest speed i can set without hitting divide-by-0 errors,
            # and should realistically eliminate the blockmap corruption crash caused
            # by doom moving it without updating the blockmap linking.

m.radius = 32 * FRACUNIT # wider than the Hell Warrior so less slips through it.
m.height = 66 * FRACUNIT
m.update(states.parse("""
    Spawn: 
      pin(S_BKEY):
      TNT1 A 1
      TNT1 A 29
      Stop
"""))
m.deathstate = S_BKEY # in vanilla, the shield instantly spawns into its death state
                      # due to collision checks with the hell warrior itself.
m.xdeathstate = S_BKEY

# -----------------------------------------------------------------------------

# Monster: NIGHTMARE DEMON (replaces HangingCorpseTop)
m = mobjinfo[MT_MISC80]
m.copy_from(mobjinfo[MT_SERGEANT])
m.object_name = "Nightmare Demon (HangingCorpseTop)"
m.doomednum = 3022
m.spawnhealth = 200 # 150 is default demon health
m.painchance = 112 # 180 is default demon painchance
m.speed = 14 # 10 is default demon speed
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (1 << MF_TRANSSHIFT))
m.seesound = sfx_bspsit
m.activesound = sfx_bspact
m.deathsound = sfx_bspdth
m.update(states.parse("""
    Melee:
      GSRG AB 6 A_FaceTarget # custom sprites for these two frames, since
                             # i'm using up deh states to speed them up,
                             # anyway.
      Goto S_SARG_ATK3
"""))
"""Grayscale demon, using the translation flags.
Inspired by KDiZD's mechanized "blood demon", but mine's a bit more
aggressive and doesn't have such high HP."""

# -----------------------------------------------------------------------------

# Monster: SOUL REAPER (replaces VileFire)
m = mobjinfo[MT_FIRE]
m.clear()
m.object_name = "Soul Reaper (VileFire)"
m.doomednum = 3023
m.spawnhealth = 400
m.speed = 7
m.radius = 24 * FRACUNIT
m.height = 64 * FRACUNIT
m.reactiontime = 3
m.painchance = 35
m.mass = 750
m.flags = (MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL)
m.seesound = sfx_skesit
# melee attack sound is DSSKEPCH, currently using a duplicate of DSCLAW
m.painsound = sfx_dmpain
m.deathsound = sfx_skedth
m.activesound = sfx_dmact
m.update(states.parse("""
    Spawn:
        SLRP A 5 A_Look
        Loop
    See:
        SLRP AAABBBCCCDDD 2 A_Chase
        Loop
    Melee:
        SLRP E 6 A_FaceTarget
        SLRP F 6 A_FaceTarget
        SLRP G 10 A_SkelFist
        Goto See
    Missile:
        SLRP K 6 Bright A_FaceTarget
        SLRP LM 6 Bright A_FaceTarget
        SLRP N 7 Bright A_SkelMissile
        SLRP M 6 Bright A_FaceTarget
        SLRP N 7 Bright A_SkelMissile
        SLRP M 6 Bright
        SLRP O 6
        Goto See
    Pain:
        SLRP P 6 A_Pain
        Goto See
    Death:
        SLRP Q 7 A_Scream
        SLRP RSTU 7 
        SLRP V 6
        SLRP W 6 A_Fall
        SLRP XY 6
        SLRP Z -1
        Stop
"""))

# -----------------------------------------------------------------------------

# Monster: ZOMBIEMAN NO AMMO DROP (replaces misc81)
m = mobjinfo[MT_MISC81]
m.copy_from(mobjinfo[MT_POSSESSED])
m.object_name = "Zombieman No Ammo Drop (Misc81)"
m.doomednum = 3025

# -----------------------------------------------------------------------------

# ANIMATED SPINNY KEYS

# Sprite names matter for pickups, so make sure the right ones are replaced:
strings["BKEY"] = "KEYB"
strings["RKEY"] = "KEYR"
strings["YKEY"] = "KEYY"
strings["BSKU"] = "KEYS"
strings["RSKU"] = "KEYO"
strings["YSKU"] = "KEYG"

# Blue keycard
m = mobjinfo[MT_MISC4]
m.clear()
m.doomednum = 5
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SPECIAL)
m.update(states.parse("""
    Spawn: 
      KEYB ABCDEFGHIJKL 2 BRIGHT
      Loop
"""))

# Red keycard
m = mobjinfo[MT_MISC5]
m.clear()
m.doomednum = 13
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SPECIAL)
m.update(states.parse("""
    Spawn: 
      KEYR ABCDEFGHIJKL 2 BRIGHT
      Loop
"""))

# Yellow keycard
m = mobjinfo[MT_MISC6]
m.clear()
m.doomednum = 6
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SPECIAL)
m.update(states.parse("""
    Spawn: 
      KEYY A 24 BRIGHT #     :-)
      KEYY ABCDEFGHIJKL 2 BRIGHT
      Goto Spawn+1
"""))

# Green keycard (replaces yellow skull)
m = mobjinfo[MT_MISC7]
m.clear()
m.object_name = "Green Key (YSkull)"
m.doomednum = 39
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SPECIAL)
m.update(states.parse("""
    Spawn: 
      KEYG ABCDEFGHIJKL 2 BRIGHT
      Loop
"""))

# Orange keycard (replaces red skull)
m = mobjinfo[MT_MISC8]
m.clear()
m.object_name = "Orange Key (RSkull)"
m.doomednum = 38
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SPECIAL)
m.update(states.parse("""
    Spawn: 
      KEYO ABCDEFGHIJKL 2 BRIGHT
      Loop
"""))

# Silver keycard (replaces blue skull)
m = mobjinfo[MT_MISC9]
m.clear()
m.object_name = "Silver Key (BSkull)"
m.doomednum = 40
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SPECIAL)
m.update(states.parse("""
    Spawn: 
      KEYS ABCDEFGHIJKL 2 BRIGHT
      Loop
"""))

# Update message strings for the new keycard colors.
strings.GOTYELWSKUL = "Picked up a green key."
strings.GOTREDSKULL = "Picked up an orange key."
strings.GOTBLUESKUL = "Picked up a silver key."

# Shorten "keycard" to "key" for consistency.
strings.GOTYELWCARD = "Picked up a yellow key."
strings.GOTREDCARD = "Picked up a red key."
strings.GOTBLUECARD = "Picked up a blue key."

# -----------------------------------------------------------------------------

# DECORATIONS

# Dead Miner (replaces PainElemental)
m = mobjinfo[MT_PAIN]
m.clear()
m.object_name = "Dead Miner (PainElem)"
m.doomednum = 81
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.update(states.parse("""
    Spawn: 
      DECO A -1
      Loop
"""))

# Ceiling Light (replaces HangTNoBrain) - deh#134
m = mobjinfo[MT_MISC83]
m.clear()
m.object_name = "Ceiling Light (HangTNoBrain)"
m.doomednum = 78
m.radius = 16 * FRACUNIT
m.height = 8 * FRACUNIT
m.flags = (MF_SPAWNCEILING|MF_NOGRAVITY)
m.update(states.parse("""
    Spawn: 
      DECO B -1 bright
      Loop
"""))

# Sitting Corpse (replaces HangTLookingUp) - deh#133
m = mobjinfo[MT_MISC82]
m.clear()
m.object_name = "Sitting Corpse (HangTLookingUp)"
m.doomednum = 77
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SOLID)
m.update(states.parse("""
    Spawn: 
      DECO D -1
      Loop
"""))

# Jackhammer (replaces ColonGibs) - deh#135
m = mobjinfo[MT_MISC84]
m.clear()
m.object_name = "Jackhammer (ColonGibs)"
m.doomednum = 79
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.update(states.parse("""
    Spawn: 
      DECO C -1
      Loop
"""))

# Yellow Ceiling Light (replaces HangBNoBrain) - deh#130
m = mobjinfo[MT_MISC79]
m.clear()
m.object_name = "Yellow Ceiling Light (HangBNoBrain)"
m.doomednum = 74
m.radius = 16 * FRACUNIT
m.height = 8 * FRACUNIT
m.flags = (MF_SPAWNCEILING|MF_NOGRAVITY)
m.update(states.parse("""
    Spawn: 
      DECO G -1 bright
      Loop
"""))

# Nmn Stalagmite (replaces Doom stalagmite, we don't need both)
m = mobjinfo[MT_MISC47]
m.object_name = "Stalagmite (BrownStub)"
m.update(states.parse("""
    Spawn: 
      DECO E -1
      Loop
"""))

# Stalactite (replaces TorchBlueShort)
m = mobjinfo[MT_MISC44]
m.object_name = "Stalactite (TorchBlueShort)"
m.doomednum = 55
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SPAWNCEILING | MF_NOGRAVITY)
m.update(states.parse("""
    Spawn: 
      DECO F -1
      Stop
"""))

# Stage Light (replaces TorchGreenShort)
m = mobjinfo[MT_MISC45]
m.object_name = "Stage Light (TorchGreenShort)"
m.doomednum = 56
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SOLID)
m.update(states.parse("""
    Spawn: 
      TLGH A -1
      Stop
"""))


# I Can't Believe They Really Implemented This And I Can't Believe I Did Either
# (replaces Meat4)
m = mobjinfo[MT_MISC54]
m.clear()
m.object_name = "An Extremely Long Sigh From Everyone In The Room (Meat4)"
m.doomednum = 52
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.update(states.parse("""
    Spawn: 
      DILD AB 8 bright # the reason you're all here, really
      Loop
"""))


# Floor lamp is no longer bright, because we use palette fullbrights instead
states[S_COLU].frame = states[S_COLU].frame - 32768

# Non-solid Short Lamp (replaces PoolBlood) dehnum 136
m = mobjinfo[MT_MISC85]
m.copy_from(mobjinfo[MT_MISC31]) # copy from Short Lamp
m.doomednum = 2029
m.flags = (m.flags - MF_SOLID)
m.radius = 1 * FRACUNIT
m.object_name = "Short Lamp Nonsolid (PoolBlood)"

# Dead Marine Gray (replaces BossEye)
m = mobjinfo[MT_BOSSSPIT]
m.object_name = "Dead Marine Gray (BossEye)"
m.doomednum = 2050
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (1 << MF_TRANSSHIFT))
m.spawnstate = S_PLAY_DIE7 # dead player state

# Dead Marine Pink (replaces BossTarget)
m = mobjinfo[MT_BOSSTARGET]
m.object_name = "Dead Marine Pink (BossTarget)"
m.doomednum = 2051
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.spawnstate = S_PLAY_DIE7 # dead player state


# Bronze Lamp (replaces TechLamp1)
# Sprite edit by Captain Toenail
m = mobjinfo[MT_MISC29]
m.object_name = "Bronze Lamp (TechLamp1)"
m.doomednum = 2052
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SOLID)
m.update(states.parse("""
    Spawn: 
      BLMP A -1
      Stop
"""))

# Reflected Torch (replaces TorchGreenTall)
m = mobjinfo[MT_MISC42]
m.clear()
m.object_name = "Reflected Torch (TorchBlueTall)"
m.doomednum = 45
m.radius = 16 * FRACUNIT
m.height = 8 * FRACUNIT
# m.flags = ()
m.update(states.parse("""
    Spawn: 
      RTOR ABCD 4 bright
      Loop
"""))

# Hissy Plush (replaces Hanging Leg Blocking)
m = mobjinfo[MT_MISC55]
m.clear()
m.object_name = "Hissy Plush (Meat5)"
m.doomednum = 53
m.radius = 8 * FRACUNIT
m.height = 8 * FRACUNIT
m.update(states.parse("""
    Spawn: 
      HISS A -1
      Loop
"""))

# Rolling Chair (replaces Redundant Gibs)
# Sprites by Nmn
m = mobjinfo[MT_MISC69]
m.clear()
m.object_name = "Rolling Chair (Gibs)"
m.doomednum = 12
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SOLID)
m.update(states.parse("""
    Spawn: 
      CHAI A -1
      Loop
"""))

# Green Tree (replaces Cyberdemon)
# Sprites by Nmn
m = mobjinfo[MT_CYBORG]
m.clear()
m.object_name = "Green Tree (Cyberdemon)"
m.doomednum = 2030
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_SOLID)
m.update(states.parse("""
    Spawn: 
      GREN A -1
      Loop
"""))

# Teleporting Berserk (replaces Lite Amp) - deh#62
states[S_PSTR].nextstate = S_PSTR # First we make the standard berserk state loop,
states[S_PSTR].length = 2         # to prevent it from disappearing on rocket impact.
m = mobjinfo[MT_MISC16]
m.copy_from(mobjinfo[MT_MISC13])  # Copy from standard berserk pickup
m.object_name = "Pushable Berserk (LiteAmp)"
m.doomednum = 2027
m.flags = (MF_SPECIAL | MF_SHOOTABLE | MF_NOBLOOD | MF_COUNTITEM)
m.mass = 100
m.health = 1000
m.deathstate = S_PSTR

# Resurrector (replaces MT_SPIDER)
m = mobjinfo[MT_SPIDER]
m.objectname = "Resurrector (Spiderdemon)"
m.doomednum = 7
m.speed = 40
m.radius = 16 * FRACUNIT
m.height = 16 * FRACUNIT
m.damage = 0
m.reactiontime = 1
m.painchance = 1
m.mass = 100
m.flags = (MF_DROPOFF | MF_NOGRAVITY | MF_FLOAT | MF_NOSECTOR)
states[S_VILE_HEAL1].nextstate = 0   # Resurrect one monster and then stop

m.update(states.parse("""
    Spawn:
        HISS A 1 A_Look
        Goto Spawn
    See:
        HISS A 1 A_VileChase
        Loop
"""))


# Unused: MT_SPAWNFIRE (translucent in ports) 


# -----------------------------------------------------------------------------

# MISC EFFECTS

# Rain 128x256 (replaces Meat3)
m = mobjinfo[MT_MISC53]
m.clear()
m.object_name = "Rain 128x256 (Meat3)"
m.doomednum = 51
m.radius = 20 * FRACUNIT
m.height = 16 * FRACUNIT
m.flags = (MF_NOBLOCKMAP)
m.update(states.parse("""
    Spawn: 
      MISC ABCDEFGH 5 BRIGHT
      Loop
"""))


# Wall Torch (replaces TorchBlueTall)
# Sprites by Nmn
m = mobjinfo[MT_MISC41]
m.clear()
m.object_name = "Wall Torch (TorchBlueTall)"
m.doomednum = 44
m.radius = 1 * FRACUNIT
m.height = 1 * FRACUNIT
m.flags = (MF_NOBLOCKMAP)
m.update(states.parse("""
    Spawn: 
      FIRS ABC 6 BRIGHT
      Loop
"""))


# Explosion Generator (replaces Meat2)
m = mobjinfo[MT_MISC52]
m.clear()
m.object_name = "Explosion Generator (Meat2)"
m.doomednum = 50
m.radius = 1 * FRACUNIT
m.height = 1 * FRACUNIT
m.flags = (MF_NOBLOOD|MF_NOSECTOR)
m.update(states.parse("""
    Spawn: 
      MISC I 1 A_Explode
      Loop
"""))


# Crushable Script Helper (replaces HangNoGuts)
m = mobjinfo[MT_MISC78]
m.clear()
m.object_name = "Crushable Script Helper (HangNoGuts)"
m.doomednum = 73
m.spawnhealth = 1
m.mass = 1000000
m.radius = 16 * FRACUNIT
m.height = 10 * FRACUNIT
m.flags = (MF_SOLID|MF_SHOOTABLE)
m.spawnstate = S_COLU
m.update(states.parse("""
    Death:
      MISC I 1 A_Fall
      Stop
""")) 


# Seismic Bomb FX - Timed Explosion
m = mobjinfo[MT_MEGA]
m.clear()
m.object_name = "Seismic Timed Explosion (Megasphere)"
m.doomednum = 6001
m.spawnhealth = 60
m.mass = 1000000
m.radius = 1 * FRACUNIT
m.height = 64 * FRACUNIT
m.flags = (MF_SOLID|MF_SHOOTABLE|MF_NOBLOOD)
m.painsound = sfx_tink
m.deathsound = sfx_barexp
m.update(states.parse("""
    Spawn: 
      MISC M 10
      Loop
    Death:
      BOMX AAA 38 A_Pain #beeps leading up to the explosion (CONSIDER: merge to one state)
      BOMX A 0 A_Scream
      BOMX A 8 Bright A_Explode #CONSIDER: custom sprites for bigger explosion?
      BOMX BC 8 Bright
      Stop
"""))

# Seismic Bomb faux-pickup (replaces Flaming Barrel)
m = mobjinfo[MT_MISC77]
m.object_name = "Seismic Bomb faux-pickup (FlamingBarrel)"
m.doomednum = 6002
m.radius = 1 * FRACUNIT
m.height = 16 * FRACUNIT
m.update(states.parse("""
    Spawn: 
      BOMB A 8 BRIGHT
      BOMB A 8
      Loop
"""))

# Monster: QUIET IMP (replaces ExtraBFG)
m = mobjinfo[MT_EXTRABFG]
m.object_name = "Quiet Imp (ExtraBFG)"
m.doomednum = 667
m.spawnhealth = 160
m.speed = -6
m.radius = 20 * FRACUNIT
m.height = 56 * FRACUNIT
m.damage = 0
m.reactiontime = 8
m.painchance = 180
m.mass = 200
m.activesound = sfx_bgact
m.flags = (MF_SOLID)
m.flags = ( # Apply color translation
            (m.flags & ~MF_TRANSLATION) |
            (2 << MF_TRANSSHIFT))
m.update(states.parse("""
    Spawn:
        BTRO A 1 A_Look
        Loop
    See:
        BTRO AABBCCDD 3 A_Chase
        Loop
"""))

# -----------------------------------------------------------------------------

# Weapons

# PISTOL
# KDiZD's pistol fires slightly faster than vanilla's:
states[S_PISTOL1].tics = states[S_PISTOL1].tics - 1

# SUPER SHOTGUN
w = weaponinfo[wp_supershotgun]
w.clear_states() # ssg weapon
w.update(states.parse("""
  Ready:
    SSG2 A 1 A_WeaponReady
    Loop
  Deselect:
    SSG2 A 1 A_Lower
    Loop
  Select:
    SSG2 A 1 A_Raise
    Loop
  Fire:
    pin(S_DSGUN1):
    SSG2 A 3
    SSG2 A 7 A_FireShotgun2
    SSG2 B 7 A_CheckReload
    SSG2 C 6
    SSG2 D 6 A_OpenShotgun2
    SSG2 E 5
    SSG2 K 4
    SSG2 F 4 #A_LoadShotgun2 isn't used, its sound is combined w/ previous one
    SSG2 G 5
    SSG2 H 5 
    SSG2 C 5 A_CloseShotgun2 #this codepointer includes refire. frame is only visible during single shot.
    Goto Ready
  Flash:
    SSG2 I 4 Bright A_Light1
    SSG2 J 3 Bright A_Light2
    Goto S_LIGHTDONE
"""))

# Pickup sprite name
strings["SGN2"] = "SSGN"

# FLARE RIFLE (replaces Plasma Rifle)
# Based on the Flare Gun from MAPGAME.WAD by Fletcher, used with permission.
# (Mapgame is fantastic and everyone should play it!)
w = weaponinfo[wp_plasma]
w.clear_states() # plasma weapon
w.update(states.parse("""
  Ready:
    FLRE A 1 A_WeaponReady
    Loop
  Deselect:
    FLRE A 1 A_Lower
    Loop
  Select:
    FLRE A 1 A_Raise
    Loop
  Fire:
    FLRE B 0 A_LoadShotgun2 # play firing sound from here, not from projectile
    FLRE B 3 Bright A_FirePlasma 
    FLRE C 6
    FLRE B 0 A_ReFire
    Goto Ready
  Flash:
    pin(S_PLASMAFLASH1):
    FLRF A 2 Bright A_Light2
    Goto S_LIGHTDONE
    pin(S_PLASMAFLASH2): #FirePlasma has a random jump between these two flash states
    FLRF B 2 Bright A_Light1
    Goto S_LIGHTDONE
"""))

# Flare rifle projectile
m = mobjinfo[MT_PLASMA]
m.speed = 35 * FRACUNIT
m.radius = 8 * FRACUNIT
m.height = 4 * FRACUNIT
m.damage = 14
m.seesound = sfx_None   # projectile sounds can be muted at close range,
                        # so this plays from the weapon itself instead

# Flare rifle ammo
ammodata[am_cell].clipammo = 10 # multiplied by 5 for cellpack
ammodata[am_cell].maxammo = 200 # doubled when carrying backpack

# We're not using the BFG slot, but there needs to be something there, so:
w = weaponinfo[wp_bfg]
w.copy_from(weaponinfo[wp_pistol])

# Center guns while firing them, like ZDoom does:
centerguns = [
    states[S_PISTOL1], states[S_SGUN1], states[S_CHAIN1], 
    states[S_MISSILE1], states[S_PLASMA1], states[S_DSGUN1]
]
for state in centerguns:
    state.misc1 = 1; state.misc2 = 32;
# Thanks to TDRR for a post pointing out this trick.

# -----------------------------------------------------------------------------

# Level names for automap
strings.HUSTR_1  =  "KDiKDiZD Start"
strings.HUSTR_13 =  "M1: Hangar"
strings.HUSTR_14 =  "M2: Nuclear Plant"
strings.HUSTR_15 =  "M3: Toxin Refinery"
strings.HUSTR_16 =  "M4: Command Control"
strings.HUSTR_17 =  "M5: Phobos Lab"
strings.HUSTR_18 =  "M6: Central Processing"
strings.HUSTR_19 =  "M7: Computer Station"
strings.HUSTR_20 =  "M8: Phobos Anomaly"
strings.HUSTR_31 =  "M9: Military Base"
strings.HUSTR_21 =  "Where Am I?"
strings.HUSTR_22 =  "The Party"
strings.HUSTR_23 =  "The Rest of Your Life"
# Dehacked string length limits :pensive:

# Flaregun
strings.GOTPLASMA =  "You got a flare rifle!"
strings.GOTCELL = "Picked up some rifle ammo."
strings.GOTCELLBOX = "Picked up a box of rifle ammo."

# Post-Z1M8 text:
strings.C3TEXT = \
"Once you wrap the last day of filming\n"\
"and pay off your debt, you're supposed to\n"\
"be free, aren't you? Why are you being\n"\
"shaken down by mobsters in imp costumes?\n"\
"It's not supposed to end this way..."

# Z1M9 secret access text
strings.C5TEXT = \
"You've unlocked the secret level: a\n"\
"military base where it's always raining\n"\
"inside."

# -----------------------------------------------------------------------------

# Story text backdrops
# #strings["SLIME16"] = "FLOOR4_8"
# #strings["RROCK14"] = "FLOOR4_8"
strings["RROCK07"] = "NFFL48W" # can't use FLOOR4_8 due to length limits, lol
# #strings["RROCK17"] = "FLOOR4_8"
strings["RROCK13"] = "DIAM21"
# #strings["RROCK19"] = "FLOOR4_8"

# -----------------------------------------------------------------------------

# Music tracks
strings["doom2"] = "e1m1"
strings["ddtbl2"] = "e1m2"
strings["runni2"] = "e1m3"
strings["dead2"] = "e1m4"
strings["stlks3"] = "e1m5"
strings["romero"] = "e1m6"
strings["shawn2"] = "e1m7"
strings["messag"] = "e1m8"
#strings["evil"] = "e1m9" # remapping this string doesn't work in boom...

# -----------------------------------------------------------------------------

# Various sprite names not covered in other sections

# Pickups with rotations from KDiZD
strings["CLIP"] = "RCLP"; strings["MEDI"] = "RMED"; strings["PMAP"] = "RMAP";
strings["PSTR"] = "RSTR"; strings["SBOX"] = "RSBX"; strings["SHEL"] = "RSHL";
strings["STIM"] = "RSTM"; strings["SUIT"] = "RSUT"; strings["ARM1"] = "RARM";
strings["ARM2"] = "RAR2"; strings["BON2"] = "RBN2"; strings["BPAK"] = "RBPK";
strings["BROK"] = "RBRK"; strings["AMMO"] = "RAMO";

# Palette-remapped vanilla sprites
strings["CHGG"] = "VCHG"; strings["CHGF"] = "VCHF"; strings["SAWG"] = "VSAW";
strings["PISG"] = "VPSG"; strings["PISF"] = "VPSF"; strings["BAL1"] = "VBL1";
strings["PUFF"] = "VPUF"; strings["BLUD"] = "VBLD"; strings["BAL2"] = "VBL2";
strings["MISL"] = "VMSL"; strings["TFOG"] = "VTFG"; strings["IFOG"] = "VIFG";
strings["PUNG"] = "VPNG"; strings["MISG"] = "VMSG"; strings["MISF"] = "VMSF";
strings["MISL"] = "VMSL"; strings["SHTG"] = "VSHG"; strings["SHTF"] = "VSHF";
strings["SARG"] = "VSRG"; strings["TROO"] = "VTRO"; strings["BOSS"] = "VBOS"; 
strings["BAL7"] = "VBL7"; strings["PLAY"] = "VPLY"; strings["POSS"] = "VPOS"; 
strings["SPOS"] = "VSPS"; strings["SKUL"] = "VSKL"; strings["HEAD"] = "VHED";
strings["POL5"] = "VPL5"; strings["CAND"] = "VCND"; strings["CBRA"] = "VCBR";
strings["SHOT"] = "VSHT"; strings["MGUN"] = "VMGN"; strings["LAUN"] = "VLAU";
strings["CSAW"] = "VCSW"; strings["ROCK"] = "VROK"; strings["BAR1"] = "VBRL";
strings["COLU"] = "VCOL"; strings["ELEC"] = "VELE"; strings["BEXP"] = "VBXP";
strings["PINS"] = "VINS"; strings["BON1"] = "VBN1"; strings["SOUL"] = "VSOL";
strings["TRED"] = "VTOR"; strings["POL1"] = "VPL1"; strings["POL2"] = "VPL2";
strings["POL3"] = "VPL3"; strings["POL4"] = "VPL4"; strings["POL6"] = "VPL6";
strings["COL1"] = "VCO1"; strings["COL2"] = "VCO2"; strings["COL3"] = "VCO3";
strings["COL4"] = "VCO4"; strings["COL5"] = "VCO5"; strings["FSKU"] = "VFSK";
strings["COL6"] = "VCO6"; strings["CEYE"] = "VEYE"; strings["TRE1"] = "VTR1";
strings["GOR1"] = "VGR1"; strings["GOR2"] = "VGR2"; strings["GOR3"] = "VGR3";
strings["GOR4"] = "VGR4"; strings["GOR5"] = "VGR5"; strings["TGRN"] = "VTOG";
strings["SMRT"] = "VSTR"; strings["SMBT"] = "VSTB"; strings["SMGT"] = "VSTG";
strings["PINV"] = "VINV"; strings["TRE2"] = "VTR2";

# Plasma rifle and associated pickups, replaced with the flare rifle
strings["PLSG"] = "FLRE"; strings["PLSF"] = "FLRF"; strings["PLSS"] = "FLRP"; 
strings["PLSE"] = "FLRX"; strings["PLAS"] = "FLRS"; strings["CELL"] = "VCEL";
strings["CELP"] = "RIFP";

# -----------------------------------------------------------------------------

"""
Sounds index:

DSHTGN, DBOPN, DBLOAD, DBCLS: Double shotgun sounds

DBLOAD: Flaregun attack, played by A_LoadShotgun2

BSPWLK: Suicide Bomber scream via A_BabyMetal (incl A_Chase)

SKESIT: Soul Harvester sight
SKEDTH: Soul Harvester death

SKESWG: Bruiser firethrow warning sound via A_SkelWhoosh (incl A_FaceTarget)

SKEPCH: Soul Reaver melee claw, called by A_SkelFist (incl. melee atk)

KNTSIT: Hell Knight sight
KNTDTH: Hell Knight death

SKEATK: Shadow attack
KEENDT: Shadow death

SPISIT: Dark Caco sight
PEPAIN: Dark Caco active
PEDTH: Dark Caco death

MANSIT: Wargrin sight
MNPAIN: Wargrin active
MANDTH: Wargrin death

BSPSIT: Nightmare Demon sight
BSPACT: Nightmare Demon active
BSPDTH: Nightmare Demon death

VILSIT: Mauler sight

Should be free as of this writing:
BOSSIT: Romero sight, played loud by A_BrainAwake (incl. spawnspot init)
BOSPN: Romero pain, played loud by A_BrainPain
BOSDTH: Romero death, played loud by A_BrainScream (incl. explosions)
VILATK: played by A_VileStart
HOOF: played by A_Hoof (incl. A_Chase)
METAL: played by A_Metal (incl. A_Chase)
MANATK: played by A_FatRaise (incl. FaceTarget)

VILACT
VIPAIN
VILDTH
CYBSIT
CYBDTH
SSSIT
SSDTH
BOSPIT
RXPLOD
SKEACT
SPISIT
KEENPN
SPIDTH
PLASMA
"""

# -----------------------------------------------------------------------------

# Generate dehacked patch
dehfile.save("kdikdizd.deh")

# Print stats to console

actionstates = 0
for s in dehfile.free_states():
    if states[s].action is not None:
        actionstates = actionstates + 1
print("%d action states remaining" % actionstates)

state_ids = dehfile.free_states()
if state_ids:
    print("%d other states remaining" % (len(state_ids) - actionstates))
    
sprite_ids = dehfile.free_sprites()
if sprite_ids:
    print("%d sprites remaining" % len(sprite_ids))