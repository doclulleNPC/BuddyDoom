# BUDDYDEF — the buddy template

**Status: design document, being executed.** This describes what a `BUDDYDEF` record
*must* be able to express so that a modder buddy is a first-class companion.
`docs/BUDDY_MODDING.md` documents the format as it exists **today**.

**Decided:** a buddy is **always player 2**, so it inherits the whole co-op bot
(`p_ai_coop.c`) instead of re-implementing it. One buddy at a time. That answers §6's
"what happens to the standalone-actor path?": it is **gone**. Removed with it —

- the generated 29-state actor + mobjtype per record (`Buddy_Register`), the `attack`
  codepointer table and the BUDDYDEF sound registration (`p_buddydef.c`);
- `P_Buddy_SpawnSelected`, the thinker-list lookup `P_Buddy_Mobj` and the duplicate
  order commands (`come`/`wait`/`attack`/`report`/`where`/`buddyhome`/mode toggle) that
  existed only because an mobj buddy had no `player_t`;
- the mobj branches in the HUD (`hu_buddy.c`), the automap (`am_map.c`), the console
  (`c_console.c`) and the key binds (`g_game.c`) — the marine bot's implementations are
  now the only ones;
- `summon <buddyname>` and map placement by `ednum` (both needed a real mobjtype).

Kept: the parser and the roster (the select screen reads it), the preview sprite, and
the three `ability` mechanics — that code takes a plain `mobj_t*` and never asks whether
it is a player, so it runs on the buddy's player body unchanged.

**Remaining steps**, in order: (1) parser declared-mask + correct Marine defaults (§5),
(2) spawn player 2 when the map has no `Player_2_Start`, (3) skin + frame remap (§4.3,
§6), (4) stats incl. the `G_PlayerReborn` hook (§3), (5) per-buddy behaviour values
(§4.5), (6) sounds (§3), (7) voice + faces (§4.4).

---

## 1. The model: two inheritance axes

A buddy inherits along two axes that cover disjoint sets of fields, so they never
collide:

| Axis | Written as | Supplies | In one phrase |
|------|-----------|----------|---------------|
| **Buddy base** | implicit in `buddy` | the whole Marine bot: door use, orders, HUD, automap marker, revive, pathfinder, item grabbing, voice | *how it behaves* |
| **Actor base** | `: <Actor>` | `mobjinfo` properties: health, radius, height, mass, painchance, reaction time, sounds, attack | *what it is* |

The Marine is not a special case under this model — it is simply `buddy "Marine" :
Player`.

> **Properties are inherited, states are not.**
> A buddy derived from `HellKnight` takes the Hell Knight's *numbers and sounds*, but
> **not** its `S_BOS2_*` states. States come from being a buddy (the player state
> machine). This is exactly where the current design goes wrong: it builds a standalone
> actor with its own 29 states, which makes the buddy a monster — and a monster cannot
> open doors, cannot be ordered, has no HUD, and cannot be revived. See §3.

---

## 2. Evidence: Frank is a hand-copied Hell Knight

Frank's current `BUDDYDEF` against `MT_KNIGHT` (`files/info.c`):

| Field | Hell Knight | Frank | |
|-------|------------|-------|---|
| `spawnhealth` | 500 | 500 | identical |
| `radius` | 24 | 24 | identical |
| `height` | 64 | 64 | identical |
| `mass` | 1000 | 1000 | identical |
| `reactiontime` | 8 | 8 | identical (not declared → default) |
| `speed` | 8 | 12 | override |
| `painchance` | 50 | 100 | override |
| `seesound` | `kntsit` | `frankn` | override |
| `painsound` | `dmpain` | `fpain` | override |
| `deathsound` | `kntdth` | `fjaul` | override |
| `activesound` | `dmact` | `frankn` | override |
| sprite | `BOS2` | `FRAN` | override |
| attack | `A_BruisAttack` / missile | `hellknight` | *names the inherited one* |

Five fields are the Hell Knight character for character, and `attack hellknight` merely
re-states what inheritance would have given for free. With `: HellKnight`, Frank's
declaration collapses to what actually makes him Frank:

```
buddy "Frank N. Stein" : HellKnight
{
  sprite      FRAN
  speed       12          // HK has 8
  painchance  100         // HK has 50
  seesound    frankn
  painsound   fpain
  deathsound  fjaul
  activesound frankn
  special     poisoncloud
  color       green
}
```

`attack` becomes optional — needed only when a buddy wants an attack *other* than its
base actor's (`buddy "X" : HellKnight { attack Revenant }`).

---

## 3. Why a naive value swap fails: the Marine's values live in four places

This is the core finding. Overriding "the Marine's health" is not one assignment:

| What | Where it lives | Trap |
|------|---------------|------|
| health | `MT_PLAYER.spawnhealth` **and** `G_PlayerReborn` (`p->health = MAXHEALTH`) | the override must also run on **reborn**, or the buddy is back to 100 HP every time it gets up |
| radius / height | `MT_PLAYER`, copied into `mobj_t` at spawn | both the `info` copy and `mo->radius`/`mo->height` must be set |
| speed | **`MT_PLAYER.speed == 0`** — players move by ticcmd, not `info->speed`. The real number is `COOP_RUN` (`0x32`) in `p_ai_coop.c` | different unit: a monster `speed 12` (map units/tic) has to be converted to a ticcmd `forwardmove` |
| pain / death sound | `MT_PLAYER.painsound` / `.deathsound`, but `p_inter.c` also calls `sfx_plpain` **hardcoded**, and `sfx_pdiehi` for heavy damage | an `info` override alone is not enough; the call sites need a buddy hook |
| sprite | `S_PLAY*` states | player sheets run A–W (G = pain, H–N death, O–W gibs), monster sheets A–O (H = pain, I–O death) → a **frame remap** is required, or existing monster art shows the wrong frames |
| attack | not a field at all — `AICoop_BestRanged` + `readyweapon` + `BT_ATTACK` + ammo | replacing weapon fire with an actor attack is the largest single change |
| behaviour | ~20 `#define`s in `p_ai_coop.c` | not overridable at all today (§4.6) |

---

## 4. The complete template

Everything below must be expressible. Values shown are the Marine's, i.e. what a buddy
inherits when it declares nothing.

### 4.1 Identity

| Field | Marine | Notes |
|-------|--------|-------|
| `name` | `"Marine"` | shown in the Buddy menu |
| `desc` | *(see roster[0])* | flavour text, lower-left panel of the select screen |
| `special` *(text)* | *(see roster[0])* | the abilities blurb on the select screen — **distinct** from the `special` ability below; these two want separate keys |
| `ednum` | — | optional DoomEd number for map placement |
| `color` | menu selection | player-colour translation (`R_SetBuddyColor`) |

### 4.2 Actor properties (inherited via `: <Actor>`)

`health`, `speed`, `radius`, `height`, `mass`, `painchance`, `reactiontime`,
`seesound`, `painsound`, `deathsound`, `activesound`, `attack`.

Any actor from Doom, Heretic or Hexen may be the base.

### 4.3 Appearance

| Field | Marine | Notes |
|-------|--------|-------|
| `sprite` | `PLAY` | 4-char sprite base; needs the frame remap of §3 |
| `scale` | 1.0 | Frank is 64 units tall against the Marine's 56 |
| `faces` | `BUF*` | **42 mugshot lumps** for the buddy HUD: `BUFST{p}{0-2}`, `BUFTR{p}0`, `BUFTL{p}0`, `BUFOUCH{p}`, `BUFEVL{p}`, `BUFKILL{p}` for pain levels p=0..4, plus `BUFGOD0`, `BUFDEAD0`. A buddy needs its own set, or must be able to say "no face" (the HUD already falls back to a text label) |
| `blood` | default | blood colour / `MF_NOBLOOD` for non-fleshy buddies |
| `gibs` | `S_PLAY_XDIE*` | monster sheets have no xdeath frames — a buddy must be able to opt out of gibbing |

### 4.4 Voice — the phrase set

This is the largest single block and the one most easily overlooked. The buddy persona
currently ships **~168 individual phrases across 83 tag families** (`VOICE_MAP` in
`files/i_voice.c`, baked by `tools/bake_buddy_voice.py`). Every one of them is a slot a
new buddy either fills, inherits, or silences.

*(The `dir:*` tags in the same table — 52 more phrases mapping to `DD*` lumps — belong
to the AI Director persona, not to the buddy. See `docs/BUDDY_VOICE.md`.)*

**Combat callouts** — rotated variants, chosen by `AICoop_Callout(prefix, n)`:

| Tag | Variants | Fires when |
|-----|---------|-----------|
| `contact` | 4 | a monster is acquired |
| `kill` | 4 | generic kill |
| `kill<mon>` | 22 | per-monster taunts: `killimp`(3), `killzm`, `killsg`, `killcg`, `killpk`, `killsc`, `killsl`, `killcd`, `killpe`, `killhk`, `killbn`, `killrv`, `killmc`, `killar`, `killmm`, `killcy`, `killav`, `killns`, `killkn` |
| `spree` | 4 | kill streak |
| `clear` | 3 | area cleared |
| `bigmon` | 3 | a boss-class monster appears |
| `crit` | 3 | critical hit taken |
| `hurt` | 3 | buddy takes damage |
| `dry` | 3 | out of ammo |
| `dodge` | 3 | dodging a missile |
| `barrel` | 3 | barrel nearby |
| `gib` | 3 | gibbed a monster |
| `flank` | 3 | flanking |
| `infight` | 2 | monsters infighting |
| `crush` | 2 | crusher |
| `fists` | 2 | down to fists |
| `berserk` | 2 | berserk pack |
| `taunt` | 4 | idle taunt |

**Squad / status callouts:**

| Tag | Variants | Fires when |
|-----|---------|-----------|
| `help` | 8 | buddy needs help |
| `thanks` | 6 | player helped the buddy |
| `ff` | 6 | friendly fire |
| `idle` | 4 | nothing happening |
| `plhurt` | 3 | player is hurt |
| `pldown` | 2 | player is down |
| `revived` | 3 | buddy was revived |
| `healed` | 2 | buddy healed itself |
| `lost` | 3 | lost the player |
| `stuck` | 3 | stuck on geometry |
| `edge` | 3 | at a ledge |
| `jump` | 2 | jumping |
| `door` | 2 | at a door |
| `locked` | 2 | locked door |
| `secret` | 2 | secret found |
| `pickup` | 3 | picked something up |
| `nice` | 2 | approval |
| `god` | 2 | god mode |
| `arm` | 2 | fully armed |
| `home` | 3 | teleported home |
| `lvlstart` | 3 | level start |
| `lvlclear` | 3 | level finished |

**Order acknowledgements** (single phrases): `summon_ok`, `wait_hold`, `wait_move`,
`attack_ok`, `attack_none`.

**State reports** (single phrases, spoken by the `where` command): `state:following`,
`state:fighting`, `state:healing`, `state:holding`, `state:coming`, `state:grabbing`.

**Weapon status reports** (16, spoken by the `report` command): `status:<weapon>` and
`status:<weapon>:ammo` for fists, pistol, shotgun, chaingun, rocketlauncher, plasma,
bfg, chainsaw, supershotgun.

**What the template must allow per buddy:**

- **inherit** — say nothing, use the Marine's voice (the default)
- **silence** — `voice none`; Frank is a monster, the Joker-marine one-liners do not fit
- **replace wholesale** — `voice FRANK`, a lump-name prefix so the buddy ships its own
  set and unspecified tags fall back to silence rather than to the Marine
- **override single tags** — e.g. only `contact` and `kill`, inheriting the rest

The weapon-status family is a special case: a buddy that carries no weapons (§4.7) has
nothing to report, so those 16 slots should silently drop out rather than needing 16
`none` declarations.

### 4.5 Bot behaviour

Today these are `#define`s in `p_ai_coop.c` — identical for every buddy. They are the
buddy's *personality* and belong in the template:

| Key | Marine | Meaning |
|-----|--------|---------|
| `sight` | 1280 | monster acquisition range |
| `near` | 256 | follow distance to the human |
| `keep` | 192 | advance toward a monster until this close |
| `run` | 0x32 | forwardmove magnitude ("speed" for a player-based buddy) |
| `turn` | 1300 | max angleturn per tic |
| `facing` | 1500 | aim tolerance before opening fire |
| `heal_hp` | 50 | seek a med-pack below this health |
| `caution_hp` | 50 | below this, stay near the player and play safe |
| `safe_hp` | 40 | below this, route home the low-danger way |
| `leash` | 640 | max stray from the player while cautious / on "come" |
| `engage_near` | 448 | while staying close, only fight threats this near |
| `revive_range` | 96 | how close the human must stand to revive |
| `heal_range` | 1024 | how far to look for health |
| `item_range` | 128 | idle pickup radius |
| `grab_near` | 512 | only grab items while this close to the player |
| `summon_tics` | 7s | how long "come" runs |
| `attack_tics` | 10s | how long "attack" charges |
| `blast_safe` | 176 | minimum range for rocket/BFG (splash safety) |
| `dodge_range` | 256 | react to incoming missiles within this |

Frank is a 500 HP tank; with the Marine's values he retreats and hunts med-packs below
50 HP like a 100 HP marine. He wants roughly `keep 64, caution_hp 0, heal_hp 0`.

Not exposed (algorithm internals, not personality): `PF_MAXPOP`, `PF_PATHMAX`,
`PF_MAXADJ`, `COOP_BL_MAX`, `PF_INF`.

### 4.6 Abilities

All of these are unconditionally on for the Marine and must become switchable:

| Key | Marine | Notes |
|-----|--------|-------|
| `revive` | on | should a monster buddy be able to revive downed marines? |
| `drone` | on | Security Drone deployment (`p_secdrone.c`) |
| `autoheal` | on | auto-uses health items |
| `item_grab` | on | detours to pick things up |
| `weapons` | player set | `G_PlayerReborn` grants fist + pistol + 50 bullets; a buddy fighting only with its actor attack should carry nothing |
| `special` | *(none)* | the buddy's individual power: `poisoncloud`, `turret`, … |

Note the naming collision to resolve: `special` is currently the *blurb text* in
`BUDDYDEF`, while the ability is `ability`. Under the new model the ability is what a
modder will call "special". Suggested: `special` = the ability, `about`/`desc` = text.

### 4.7 Combat

| Key | Marine | Notes |
|-----|--------|-------|
| `attack` | player weapons | under the new model: an actor from Doom/Heretic/Hexen, whose attack codepointer the buddy uses |
| `melee` | — | optional separate close-range attack |
| `attack_range` | `COOP_SIGHT` | |
| `attack_cooldown` | weapon-derived | a monster attack needs its own cadence |

---

## 5. Parser changes required

1. **`: Parent`** — inheritance from an actor (and later from another buddy).
2. **A declared-mask.** The parser currently cannot distinguish "not declared" from
   "declared as 0", so *inheritance is not expressible at all*. Every field needs a
   "was this set?" bit.
3. **Fix the default seed.** `Buddy_Defaults()` (`files/p_buddydef.c`) seeds a
   monster-ish set — health 200, speed 8, radius 20, height 56, painchance 120 — which
   matches neither the Marine (100/25/16/56/255) nor any actor. This is the bug at the
   root: a buddy inherits *nothing* today, it starts from a fourth, invented baseline.
4. **Actor lookup by name** across the Doom, Heretic and Hexen tables.
5. **Voice-set resolution** — a lump-name prefix plus per-tag overrides (§4.4).

---

## 6. Open questions

- **Frame remap or player-convention sheets?** A remap table (player frame → monster
  sheet frame) lets existing monster art like `FRAN` work unchanged; requiring
  player-convention sheets (A–W) is simpler in code but means re-cutting the art.
- **Voice fallback.** When a buddy declares its own voice set, should unspecified tags
  fall back to the Marine's phrases (wrong persona) or to silence (recommended)?
- **Does a monster buddy revive?** Reviving downed marines is a base-class ability; it
  may want to be off by default for non-marine buddies.
- **What happens to the standalone-actor path?** BUDDYDEF currently also registers a
  real mobjtype so mappers can place a buddy by `ednum` and the console can
  `summon frank`. Under the new model the menu no longer uses it. Keep it for map
  placement, or drop it?
