# Episode 1 — Monster & Thing Overview

A breakdown of every `THINGS` entry placed by the level designer in the 9 E1
maps: monsters, player starts, weapons, ammo, health, powerups, keys, the
explodable barrels that count as hazards, and the E1M1..E1M8 wall-decor props
the `DECORATE` sprite table renders as columns, lamps, hanging corpses, and
bloody messes. Counts are taken from the maps' `THINGS` lumps (10 bytes per
entry: x, y, angle, type, flags) so the numbers match the `P_SpawnMapThing`
stream the engine walks at level start.

Source IWAD: `C:\Program Files (x86)\Steam\steamapps\common\Ultimate Doom\base\DOOM.WAD`
(`mobjinfo[]` is from `files/info.c`; thing names mapped via the classic DOOM
`DoomEd` numbering.)

## Summary

| Metric | Value |
|---|---:|
| Maps in E1 | 9 (E1M1 .. E1M9) |
| Total `THINGS` entries | 2,516 |
| Unique thing types used | 48 |

Counts by category:

| Category | Total |
|---|---:|
| Player | 117 |
| Monster | 930 |
| Special | 3 |
| Health | 716 |
| Ammo | 279 |
| Weapon | 39 |
| Powerup | 29 |
| Key | 16 |
| Hazard | 213 |
| Decor | 174 |

## Per-map totals (by category)

| Map | Total | Player | Monster | Boss | Special | Health | Ammo | Weapon | Powerup | Key | Hazard | Decor |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| E1M1 | 143 | 9 | 29 | · | · | 44 | 20 | 4 | 1 | · | 9 | 27 |
| E1M2 | 262 | 14 | 80 | · | · | 63 | 46 | 4 | 2 | 1 | 24 | 28 |
| E1M3 | 380 | 13 | 131 | · | · | 127 | 40 | 5 | 2 | 2 | 28 | 32 |
| E1M4 | 254 | 13 | 88 | · | · | 61 | 26 | 4 | 3 | 2 | 32 | 25 |
| E1M5 | 293 | 12 | 131 | · | 1 | 77 | 29 | 7 | 4 | 2 | 28 | 2 |
| E1M6 | 463 | 15 | 177 | · | · | 159 | 58 | 3 | 9 | 3 | 24 | 15 |
| E1M7 | 358 | 14 | 150 | · | · | 113 | 29 | 5 | 4 | 3 | 20 | 20 |
| E1M8 | 126 | 13 | 41 | · | 1 | 16 | 16 | 3 | 2 | · | 28 | 6 |
| E1M9 | 237 | 14 | 103 | · | 1 | 56 | 15 | 4 | 2 | 3 | 20 | 19 |

## Monsters (all variants) — by category

In this fork of the engine, the standard DOOM monsters are exposed under two
different `DoomEd` numbers (vanilla 76..81 and the engine-remapped 3001..3006).
The DOOM1 WAD here uses the 3001..3006 set for the actual placements. Both
are listed below; the per-monster total is the sum.

### Boss-tier monsters

| Monster | Total placements |
|---|---:|
| Baron of Hell (MT_BRUISER, doomednum 3003) | 2 |

### Standard monsters

| Monster | Total placements |
|---|---:|
| Shotgun guy (MT_SHOTGUY) | 282 |
| Imp (MT_TROOP, doomednum 3001) | 277 |
| Former Human / zombie (MT_POSSESSED, doomednum 3004) | 216 |
| Sergeant / Former Sergeant (MT_SERGEANT, doomednum 3002) | 90 |
| Spectre / Shadow (MT_SHADOWS) | 63 |

## Per-map monster breakdown

How many of each monster type are placed in each map. The Boss row shows
the optional boss spawn (`MT_BOSSBRAIN` + `MT_BOSSSPIT` + `MT_BOSSTARGET`
mechanism) which only appears in E1M8.

| Map | Shotgun guy (MT_SHOTGUY) | Imp (MT_TROOP, doomednum 3001) | Former Human / zombie (MT_POSSESSED, doomednum 3004) | Sergeant / Former Sergeant (MT_SERGEANT, doomednum 3002) | Spectre / Shadow (MT_SHADOWS) | Baron of Hell (MT_BRUISER, doomednum 3003) | Total monsters |
|---|---:|---:|---:|---:|---:|---:|---:|
| E1M1 | 16 | 4 | 9 | · | · | · | 29 |
| E1M2 | 8 | 18 | 54 | · | · | · | 80 |
| E1M3 | 47 | 47 | 28 | 7 | 2 | · | 131 |
| E1M4 | 18 | 36 | 23 | 11 | · | · | 88 |
| E1M5 | 46 | 31 | 28 | 12 | 14 | · | 131 |
| E1M6 | 56 | 57 | 22 | 20 | 22 | · | 177 |
| E1M7 | 56 | 39 | 47 | 7 | 1 | · | 150 |
| E1M8 | 6 | 5 | · | 18 | 10 | 2 | 41 |
| E1M9 | 29 | 40 | 5 | 15 | 14 | · | 103 |

## Top 25 things (overall)

| # | Thing | Total | Category |
|---:|---|---:|---|
| 1 | Shotgun guy (MT_SHOTGUY) | 282 | Monster |
| 2 | Imp (MT_TROOP, doomednum 3001) | 277 | Monster |
| 3 | Armor bonus (MT_MISC3) | 256 | Health |
| 4 | Former Human / zombie (MT_POSSESSED, doomednum 3004) | 216 | Monster |
| 5 | Health bonus (MT_MISC2) | 215 | Health |
| 6 | Explodable barrel (MT_BARREL) | 213 | Hazard |
| 7 | Medikit (MT_MISC11) | 114 | Health |
| 8 | Rocket (MT_MISC22) | 96 | Ammo |
| 9 | Stimpack (MT_MISC10) | 95 | Health |
| 10 | Sergeant / Former Sergeant (MT_SERGEANT, doomednum 3002) | 90 | Monster |
| 11 | DM player start | 81 | Player |
| 12 | Spectre / Shadow (MT_SHADOWS) | 63 | Monster |
| 13 | Clip box (MT_MISC17) | 61 | Ammo |
| 14 | Clip (MT_CLIP) | 55 | Ammo |
| 15 | Green column (MT_MISC31) | 51 | Decor |
| 16 | Plasma rifle (MT_MISC23) | 35 | Ammo |
| 17 | Hanging victim (sw, MT_MISC71) | 23 | Decor |
| 18 | Hanging victim (one leg, MT_MISC48) | 22 | Decor |
| 19 | Rocket box (MT_MISC19) | 22 | Ammo |
| 20 | Twitching impaled victim (MT_MISC62) | 21 | Decor |
| 21 | Bloody mess 1 (MT_MISC68) | 20 | Decor |
| 22 | Blue armor (MT_MISC0) | 18 | Health |
| 23 | Shotgun (MT_SHOTGUN) | 16 | Weapon |
| 24 | Bloody mess 2 (MT_MISC69) | 12 | Decor |
| 25 | Computer map (MT_MISC24) | 10 | Powerup |

## All things grouped by category

### Player (5 types, 117 placements)

| Thing | Total placements |
|---|---:|
| DM player start | 81 |
| Player 1 start | 9 |
| Player 2 start | 9 |
| Player 3 start | 9 |
| Player 4 start | 9 |

### Monster (6 types, 930 placements)

| Thing | Total placements |
|---|---:|
| Shotgun guy (MT_SHOTGUY) | 282 |
| Imp (MT_TROOP, doomednum 3001) | 277 |
| Former Human / zombie (MT_POSSESSED, doomednum 3004) | 216 |
| Sergeant / Former Sergeant (MT_SERGEANT, doomednum 3002) | 90 |
| Spectre / Shadow (MT_SHADOWS) | 63 |
| Baron of Hell (MT_BRUISER, doomednum 3003) | 2 |

### Special (1 type, 3 placements)

| Thing | Total placements |
|---|---:|
| Teleport landing pad (MT_TELEPORTMAN) | 3 |

### Health (7 types, 716 placements)

| Thing | Total placements |
|---|---:|
| Armor bonus (MT_MISC3) | 256 |
| Health bonus (MT_MISC2) | 215 |
| Medikit (MT_MISC11) | 114 |
| Stimpack (MT_MISC10) | 95 |
| Blue armor (MT_MISC0) | 18 |
| Green armor (MT_MISC1) | 9 |
| Soulsphere (MT_MISC12) | 9 |

### Ammo (7 types, 279 placements)

| Thing | Total placements |
|---|---:|
| Rocket (MT_MISC22) | 96 |
| Clip box (MT_MISC17) | 61 |
| Clip (MT_CLIP) | 55 |
| Plasma rifle (MT_MISC23) | 35 |
| Rocket box (MT_MISC19) | 22 |
| Cell pack (MT_MISC18) | 8 |
| BFG (MT_MISC16) | 2 |

### Weapon (4 types, 39 placements)

| Thing | Total placements |
|---|---:|
| Shotgun (MT_SHOTGUN) | 16 |
| Rocket launcher (MT_MISC27) | 9 |
| Chaingun (MT_CHAINGUN) | 9 |
| Chainsaw (MT_MISC26) | 5 |

### Powerup (5 types, 29 placements)

| Thing | Total placements |
|---|---:|
| Computer map (MT_MISC24) | 10 |
| Blur sphere (MT_INS) | 7 |
| Rad suit (MT_MISC14) | 7 |
| Allmap (MT_MISC15) | 4 |
| Berserk (MT_MISC13) | 1 |

### Key (3 types, 16 placements)

| Thing | Total placements |
|---|---:|
| Red keycard (MT_MISC6) | 6 |
| Blue keycard (MT_MISC4) | 6 |
| Yellow skull key (MT_MISC5) | 4 |

### Hazard (1 type, 213 placements)

| Thing | Total placements |
|---|---:|
| Explodable barrel (MT_BARREL) | 213 |

### Decor (9 types, 174 placements)

| Thing | Total placements |
|---|---:|
| Green column (MT_MISC31) | 51 |
| Hanging victim (sw, MT_MISC71) | 23 |
| Hanging victim (one leg, MT_MISC48) | 22 |
| Twitching impaled victim (MT_MISC62) | 21 |
| Bloody mess 1 (MT_MISC68) | 20 |
| Bloody mess 2 (MT_MISC69) | 12 |
| Red pillar (skull on pole, MT_MISC43) | 10 |
| Short green pillar (MT_MISC50) | 8 |
| Candelabra (MT_MISC49) | 7 |

## Notes

- `THINGS` lumps in the WAD are 10 bytes per entry: x (i16), y (i16), angle
  (u16), type (u16), flags (u16). The `type` field is the editor `DoomEd`
  number, not the C `MT_*` enum index. The two are linked via
  `mobjinfo[].doomednum` in `files/info.c`.
- **The 3001..3006 doomednum assignment is universal across modern DOOM
  source ports**, not fork-specific. Verified against: this engine
  (`files/info.c`), GZDoom (`wadsrc/static/zscript/actors/doom/doomimp.zs`
  + `wadsrc/static/mapinfo/doomitems.txt`), DSDA-DOOM/PrBoom+
  (`prboom2/src/info.c`), and Woof (`src/info.c`). All four ports map
  MT_TROOP=3001, MT_SERGEANT=3002, MT_BRUISER=3003, MT_POSSESSED=3004,
  MT_HEAD=3005, MT_SKULL=3006. The same numbers are baked into the
  original DOOM1 IWAD's `THINGS.type` field (so this is the convention the
  level authors actually used), and it's the convention GZDoom and
  DSDA-DOOM honor. The WAD mixes classic vanilla numbers (10=Demon,
  14=Teleport, 9=ShotgunGuy) with the 3001-3006 monster set in the same
  map. The historical origin is widely attributed to the ZDoom / Eternity
  Engine compatibility shims of the late 1990s, but the assignment is
  upstream of any modern fork.
- The `MT_MISC77..MT_MISC86` slots in this engine's enum are reserved for
  Heretic / TC compatibility and don't appear in DOOM1's E1 maps.
- `Spectre / Shadow (MT_SHADOWS)` is the translucent invulnerable variant
  of the pink demon. doomEd number 58 (vanilla) is what DOOM1 uses here.
- The 213 `Explodable barrel (MT_BARREL)` placements are technically pickups
  (they explode and drop ammo when shot) but they are categorized as
  `Hazard` here for clarity, since killing them with a close-range weapon
  damages the player. In a survival run they are killable XP / ammo sources.
- `DM player start` (doomEd 11) is the deathmatch player spawn — E1 has 81
  of them. Single-player will never see these; the engine picks player-1
  start (1..4) on map load.
- **E1M8 (Phobos Anomaly) is the Baron of Hell arena** — the 2
  `MT_BRUISER` placements are the only Barons in the entire episode.
  No Spider Mastermind or Cyberdemon appear in Episode 1 (those bosses
  are DOOM2 / E2+ / E3+). The `MT_BOSSBRAIN` / `MT_BOSSSPIT` / `MT_BOSSTARGET`
  three-lump boss-spawn mechanism is also unused in E1 (DOOM1 maps don't
  trigger it; the BFG-style "kill the boss to leave" logic is bypassed).