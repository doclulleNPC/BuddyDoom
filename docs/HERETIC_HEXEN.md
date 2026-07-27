# Heretic / Hexen content in BuddyDoom — current status

**Source audit:** 2026-07-22, updated 2026-07-27 (wave-2 Heretic content + Blasphemer IWAD detection; boot/playability pass — see `docs/HERETIC_SUPPORT_PLAN.md` "Boot/playability pass": `gametype` enum, lump aliases, menu/intermission text fallback, Heretic line specials/teleport/switches/keys, status bar v1). This document describes the additive content pack in the current DOOM engine. It is not a claim that BuddyDoom is a complete Heretic or Hexen game-mode port.

## 1. Architecture

Heretic and Hexen actors are appended to the existing `states[]`, `mobjinfo[]` and sprite tables at runtime:

- `files/heretic.c` / `heretic.h` — Heretic actors and native-sprite remapping;
- `files/hexen.c` / `hexen.h` — Hexen actors;
- `files/p_inv_heretic.c` — Heretic artifact pickups/effects;
- `files/freedoom.c` — FreeDoom DOOM2 actor clones.

The installers run early in `files/d_main.c`, before DeHackEd application and renderer sprite initialization. This keeps the generated DOOM tables intact and lets DEH/DSDHacked edits win when editor-number slots overlap.

## 2. Heretic additive pack

### Implemented

All ten current Heretic monster actors are present in the additive table:

- Golem / mummy;
- Sabreclaw / clink;
- Gargoyle / imp;
- Undead warrior / knight;
- Weredragon / beast;
- Disciple / wizard;
- Ophidian / snake;
- Maulotaur / minotaur;
- Iron Lich;
- D'Sparil (simplified).

They use the ported state/action definitions in `files/heretic.c`, can be spawned through the current summon/director paths, and have the implemented attack/death simplifications documented in the source. Ranged actors use the corresponding appended projectile actors.

The Heretic additive actor set is available when the required parsed sprite assets are present. `P_Director_HereticAvailable` checks the installed sprite frames before the director adds the pool.

### Assets and sprites

The tooling can produce a palette-converted `hereticstuff.wad`, but the current Heretic IWAD path also remaps the appended H* sprite names to native Heretic four-character sprite codes before `R_Init` (`Heretic_RemapNativeSprites` in `files/heretic.c`). Do not describe the current path as “always invisible” or as requiring only the old renamed overlay.

Heretic sounds are resolved through the native Heretic names in `heretic_mode` with a `ds<name>` fallback; missing DOOM-only sounds degrade to silence instead of aborting (`files/i_sound.c`).

### Artifacts

The artifact module currently has ten pickup actors/states, but the original source comment still says “eight”; the runtime enum/initializer includes flask, urn, tome, torch, bomb, ring, shadow, chaos, wings and egg. The effects include healing, berserk/tome behavior, infrared, invulnerability, invisibility, teleport, time bomb, flight support and morph-egg/chicken behavior where the generic subsystem is active.

The additive artifact inventory is usable from the current inventory/console paths. The *artifact* pickup actors (`files/p_inv_heretic.c`) still intentionally use `doomednum=-1`; always-on Heretic artifact UI remains part of the plan. Ordinary map items, however, ARE now placed — see wave-2 below.

### Wave-2 expansion (files/heretic_deco.c, heretic_items.c, heretic_mvar.c)

A larger additive port from crispy-doom's `heretic/info.c` + `heretic/p_enemy.c`, wired in `D_DoomMain` next to `Heretic_Init` (`Heretic_Deco_Init` / `Heretic_MVar_Init` / `Heretic_Items_Init`):

- **`heretic_deco.c`** — Heretic decoration/scenery actors (ported frame tables + mobjinfo). Invented `H*` sprite codes are remapped to native `heretic.wad` 4-char codes by `Heretic_RemapNativeSprites`.
- **`heretic_items.c`** — **map-placeable** Heretic items at their real doomednums: keys, ammo, ground weapons, shields and the health vial. This means these now **appear on Heretic maps with the correct art**, instead of being skipped by `P_HereticThingType`. (Distinct from the `-1` artifacts above.)
- **`heretic_mvar.c`** — monster variants / extra bosses beyond the base 10: the Nitrogolem leader (ednum 45, seeking skull), the ghost mummy variants (`MF_SHADOW`), the mummy soul released on death, and related actors.

### Blasphemer (free Heretic IWAD)

`blasphemer.wad` is recognised as a Heretic-family IWAD: added to `known_iwads[]` and the filename gamemode guesser (retail / ExMy episodes) and, like `heretic.wad`, sets `heretic_mode` (`files/d_main.c`). A renamed Blasphemer is still content-identified as Heretic via the `M_HTIC`/`MUS_E1M1` signature (`files/w_iwadid.h`). It is also listed (with `heretic.wad`) in the launcher's IWAD picker (`tools/launcher.c`).

## 3. Hexen additive pack

`files/hexen.c` currently installs ten Hexen actors:

| Actor | Current behavior |
|---|---|
| Ettin (`MT_XETTIN`) | Melee brute |
| Centaur (`MT_XCENTAUR`) | Melee brute |
| Slaughtaur (`MT_XSLAUGHTAUR`) | Melee plus projectile |
| Chaos Serpent (`MT_XDEMON`) | Melee plus fire projectile |
| Afrit (`MT_XFIREDEMON`) | Flying ranged actor |
| Reiver/Wraith (`MT_XWRAITH`) | Floating melee/ranged actor |
| Dark Bishop (`MT_XBISHOP`) | Floating caster |
| Wendigo/Ice Guy (`MT_XICEGUY`) | Floating ice projectile actor |
| Stalker (`MT_XSTALKER`) | Ambush/melee/spit actor |
| Death Wyvern (`MT_XDRAGON`) | Flying boss-style guard actor |

Each current ranged actor has an appended projectile. Multi-stage rituals, teleport/summon behavior and full Hexen `special1/2` semantics are simplified or omitted. The Hexen weapon/player system is not ported.

### Wave-2 expansion (files/hexen_deco.c, hexen_items.c, hexen_mon.c)

A large additive expansion ports most of the remaining Hexen bestiary/scenery/items
from crispy-doom (verified to build MSVC x64 and boot; all `doomednum = -1` **summon-only**
— forced in `D_DoomMain` — because there is no `hexen_mode`/`P_HexenThingType` map path yet,
so real Hexen ednums can't shadow DOOM/Heretic map things):

- **`hexen_deco.c`** — ~136 decoration/scenery actors: statues, rocks, trees/stumps,
  mushrooms, stalagmites/stalactites (stone + ice), moss, tombstones, 13 gargoyle statues,
  torches (wall/twined/brass/fire-bull, lit + unlit), cauldron, chandelier, candles, bell
  (full swing), banner, log, chains, table clutter, and **destructibles** (pottery, suit of
  armor, xmas tree, shrubs, destructible tree) that shatter into chunk sub-actors.
- **`hexen_items.c`** — ~56 pickups: 3 mana, 4 armor, 11 keys, artifacts, 17 puzzle pieces,
  9 class weapon pieces. They appear with correct art; effects are minimal (health→`P_GiveBody`,
  armor→`P_GiveArmor`, the rest cosmetic) since there's no Hexen class/mana/key subsystem.
  Touch handled by `P_TouchHexenItem` (dispatched in `p_inter.c`).
- **`hexen_mon.c`** — 11 combatants: Chaos Serpent 2, buried Wraith, and the bosses
  **Korax** (`MT_XKORAX`, bone-pop death scattering spirits, lightning column, 6-shot missile
  fan) and the **Heresiarch** (`MT_XHERESIARCH`) + their projectiles. The bosses are
  faithful-as-feasible: the ACS ritual, Korax's TID teleports, and the Heresiarch's three
  orbiting Sorcerer Balls are dropped (this engine has no `mobj_t.special1/args`/ACS) — they
  chase and cast direct spell volleys with the real death sequences. MASH/Disc-of-Repulsion
  chunk duplicates and the Fighter/Cleric/Mage class-bosses are skipped (need weapon FX).

All summonable by name (`summon korax`, `summon heresiarch`, `summon barrel`, …) via
`Hexen_TypeByName` chaining `Hexen_Mon_TypeByName`/`Hexen_ItemTypeByName`. Native Hexen sprite
codes are registered directly (5 collide with DOOM and are aliased, like the `X*` monster
sprites); the art renders when a Hexen sprite WAD is loaded, else the actors spawn invisibly.

### Poison cloud + poison damage-over-time

`MT_XPOISONCLOUD` (`files/hexen.c`, ported from crispy `hexen/a_action.c`/`p_inter.c`) is the
lingering gas the Cleric's Flechette leaves behind. Unlike a normal hit it deals **delayed
poison damage**: a short-radius attack (`P_PoisonRadiusAttack`, `files/p_map.c`) feeds the
victim's poison counter, and `P_PlayerThink` then bleeds 1 HP every 16 tics until it decays
(`P_PoisonPlayer`/`P_PoisonDamage`, `files/p_inter.c`; poison ignores armor and can kill).
This adds two `player_t` fields (`poisoncount`, `poisoner`) — appended at the end, nulled on
save load like `attacker`. The cloud reuses `mobj_t.reactiontime`/`movecount` as its lifetime
and bob phase, so `mobj_t` is unchanged. Summon the cloud directly for testing via the console:
`summon poisoncloud`.

The **Flechette artifact** is wired up as a Heretic-style inventory item
(`h_arti_flechette`): using it throws a poison bag (`MT_XPOISONBAG`) that settles briefly, then
`A_PoisonBagInit` pops the cloud above it (crispy's Cleric behavior). Effect in
`ApplyHereticArtifact` (`files/p_inv_heretic.c`), bag actor/states + `A_PoisonBagInit` in
`files/hexen.c`; get one with `givearti flechette`.

The **poison shroom** (`MT_XPOISONSHROOM`, crispy `MT_ZPOISONSHROOM`) is also wired: a
shootable/solid plant (30 HP) that idles with a slow pulse and, when destroyed, bursts into a
poison cloud through the same `A_PoisonBagInit`. Summon it with `summon poisonshroom` (or
`shroom`).

**Art + summon gating:** the poison art (`PSBG` bag/cloud, `SHRM` shroom) ships separately from
the hexenstuff monster sprites — e.g. dropped into a buddy WAD like `FRANK.wad` (autoloaded via
`BUDDYDEF`). So the poison spawnables resolve on their *own* sprites via
`Hexen_PoisonTypeByName` (`files/hexen.c`), not the `SPR_XETT` gate in `Hexen_Available`; that's
why `summon poisoncloud`/`poisonbag`/`poisonshroom` work even without the monster pack. The
cloud is intentionally `MF_SHADOW` (fuzzy, faithful to Hexen); the bag, shroom and Flechette
inventory icon render with their real `PSBG`/`SHRM` pixels (remap them to the DOOM `PLAYPAL` if
the greens look off).

The current rule director selects Hexen trash from `MT_XETTIN` through `MT_XSTALKERBOSS` when `SPR_XETT` is available; it can select `MT_XDRAGON` as a rare objective guard. The asset probe is sprite-based, not a launcher-state API.

## 4. Asset extraction

The repository tools can extract/rename palette-converted assets for the additive pack:

```sh
python3 tools/extract_heretic_monsters.py
python3 tools/extract_hexen.py
```

The Hexen extractor writes the collision-free X* sprite namespace and the map used by the C table. Sounds are reused/loaded according to the current source path; the Hexen actor file explicitly notes that DOOM SFX are still reused for Hexen.

These tools are asset preparation, not a replacement for the behavior/player/game-mode work.

## 5. Still not a full Heretic/Hexen mode

The following remain incomplete:

- Corvus player and Heretic weapon set/tome modes;
- complete Heretic/Hexen status bars, menus, intermissions and finales;
- complete Heretic/Hexen line/sector specials and level progression;
- Hexen classes, weapons, artifacts and ACS/polyobject behavior;
- full Heresiarch/Korax and D'Sparil multi-phase behavior;
- all map-placeable content and native mission-specific UI.

See `docs/HERETIC_SUPPORT_PLAN.md` for the full game-mode plan and `docs/INVENTORY.md` for the current artifact boundary.

## Source map

- Heretic actors/remap: `files/heretic.c`, `files/heretic.h`.
- Hexen actors: `files/hexen.c`, `files/hexen.h`.
- Artifacts/morph: `files/p_inv_heretic.c`, `files/p_morph.c`.
- Map thing resolution: `files/p_mobj.c`, `files/heretic.c`.
- Sound fallback/native lookup: `files/i_sound.c`.
- Director pools/availability: `files/p_ai_director.c`.
- Startup ordering: `files/d_main.c`.
