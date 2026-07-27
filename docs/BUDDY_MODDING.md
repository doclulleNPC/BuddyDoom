# Modding co-op buddies in BuddyDoom

BuddyDoom ships with an AI co-op **buddy** (a second marine that fights alongside
you). This guide shows modders how to **add** their own buddies or **replace** the
default one — with **no compiler and no toolchain**. You write one text lump, drop
it in a WAD with your sprites and sounds, and it appears in **Main Menu → Buddy**.

- Companion buddies are picked on the **Buddy** select screen (Hexen player-class
  style: a full view of the buddy plus its name and description).
- The built-in **Marine** is always roster slot 0 and stays the default.
- Everything here is read **directly from the WAD at load** — you never run
  `decohack`, DEHACKED, or any build step.

> Worked examples live in the repo's [`modding/`](../modding/) folder:
> [`frank.buddydef`](../modding/frank.buddydef) (native) and
> [`frank.dh`](../modding/frank.dh) (DECOHack, for the advanced route).

---

## ⚠ Status: the buddy is becoming player 2

A modder buddy used to be built as its own **actor** — BuddyDoom generated a 29-state
monster from your record and spawned it beside you. That was a dead end: a monster
cannot open a door, cannot be ordered, cannot be revived and has no HUD, so every one
of those features had to be re-implemented for it, badly.

The rule now is: **a buddy is always player 2.** That is the marine bot's slot, so a
buddy inherits door use, orders, the HUD strip, the automap marker, revive, the
pathfinder, weapons, pickups and savegame support without a line of duplicate code.

**What that means for your `BUDDYDEF` today:** the actor path is gone; applying your
record's properties to player 2 is the work in progress. Right now a record is a
**roster entry** — your buddy is listed on the Buddy screen with its name,
description, preview sprite, colour and stats, and its `ability` runs — but the body
you get in-game is still the Marine's. Every key below is parsed and kept, so a lump
you write today stays valid; the columns marked *(pending)* are the ones that don't
reach the game yet. Design and remaining steps: [`BUDDYDEF.md`](BUDDYDEF.md).

---

## Quick start (3 steps)

> Or skip the text editor: **`run/mybuddy`** is a small SDL3 app that does all of this
> for you — open (or create) a WAD, add/duplicate/delete buddy records, edit every key
> with pickers for the values the engine actually knows, watch the `BUDDYDEF` text it
> will write in a live preview, and save. It colour-codes each key by whether it
> reaches the game today, so you can see at a glance what is worth tuning.
>
> It also edits the **WAD** itself — import a file as a lump, export, rename, delete —
> and puts imported art straight into the sprite namespace, so step 2 below is a button
> rather than a trip through SLADE. A **sprite preview** shows frame A of your buddy's
> art (Doom patch or PNG) as the engine will see it, which catches a wrong `sprite` base
> or a sprite outside `S_START`/`S_END` before you launch the game.
> `mybuddy --check yourbuddy.wad` prints the same report on the console.

1. Make a text lump named **`BUDDYDEF`** describing your buddy (see below).
2. Add your buddy's **sprites** (Doom patch *or* PNG) and **sounds** to the same WAD.
3. Launch and pick it in the menu:

   ```
   buddydoom -iwad DOOM2.WAD -file yourbuddy.wad     # then Main Menu → Buddy
   ```

The fastest possible test needs **no new art at all** — point `sprite` at art
that's already in the IWAD (e.g. `BOSS` = Baron, `TROO` = Imp). See Example 1.

---

## The `BUDDYDEF` lump

Plain text. One `buddy { … }` block per buddy (you can define several in one lump).
Keys are case-insensitive; `#` starts a comment; quotes are optional except where a
value has spaces.

```
# my buddies
buddy {
  name        "Frank N. Stein"     # shown big on the Buddy screen
  desc        "Gamma bruiser..."    # description (word-wrapped on the screen)
  sprite      FRAN                  # 4-char sprite base (needs FRANA1.. in the WAD)
  health      999
  speed       10                    # map units per move (imp 8, demon 10, cyber 16)
  radius      24                    # map units
  height      64
  mass        1000                  # higher = harder to knock back
  painchance  100                   # 0–255 chance to flinch when hit
  color       green                 # default colour on the Buddy screen
  seesound    FRANKN                # name of the sound lump (blank = silent)
  painsound   FRANKN
  deathsound  FRANKN
  activesound FRANKN
  special     "Tanky bruiser"       # blurb shown on the Buddy screen
  ability     poisoncloud           # NAMED power: none | drone | poisoncloud | turret
}
```

### Fields

| Key | Meaning | Default | Live? |
|-----|---------|---------|-------|
| `name` | Display name (Buddy menu) | `Buddy` | yes |
| `desc` / `about` / `info` | Description (wrapped) | *(empty)* | yes |
| `sprite` | 4-char sprite base name | `PLAY` | select-screen preview; as the in-game skin *(pending)* |
| `color` / `colour` | Default menu colour: `Green Gray Brown Red Blue Orange Purple White`, or 0–7 | *(none)* | yes |
| `special` / `abilities` | Free-text blurb (shown on the Buddy screen) | *(empty)* | yes |
| `ability` | **Named** special ability the buddy actually uses (table below) | `none` | yes |
| `health` / `hp` | Spawn health | `200` | shown on the stats panel; on the body *(pending)* |
| `speed` | Move speed, map units | `8` | as above *(pending)* — becomes a ticcmd speed, players don't use `info->speed` |
| `radius` | Collision radius, map units | `20` | as above *(pending)* |
| `height` | Collision height, map units | `56` | as above *(pending)* |
| `mass` | Knockback resistance | `100` | as above *(pending)* |
| `painchance` | Flinch chance, 0–255 | `120` | as above *(pending)* |
| `reactiontime` / `reaction` | Tics before reacting to a target | `8` | as above *(pending)* |
| `seesound` | Wake-up sound (lump name) | *(none)* | *(pending)* |
| `painsound` | Hurt sound | *(none)* | *(pending)* |
| `deathsound` | Death sound | *(none)* | *(pending)* |
| `activesound` | Idle grunt | *(none)* | *(pending)* |
| `attack` | Attack style (table below) | `melee` | **retired** — player 2 fights with weapons |
| `ednum` / `doomednum` | Map editor number | `-1` | **retired** — a player is not map-placeable |

The two **retired** keys are still accepted so old lumps load without complaint;
they no longer do anything. `attack` is expected to be replaced by a weapon loadout.

The **Buddy select screen** (Options → Buddy) is laid out in quarters over an animated
lava backdrop:

- **upper left** — the animated, recoloured buddy sprite (1x, anchored on its feet; the
  anchor is clamped so an oversized sprite can't draw outside the screen),
- **lower left** — an `ABOUT` panel with the wrapped `desc` text,
- **right** — title, name, the stats panel (HP, Speed, Size = `radius`×`height`, Mass,
  Pain, Reaction, Attack, Ability), the `SPECIAL:` blurb, and the Buddy/Color cyclers.

Anything too long for its slice is cut with `...`, and the Attack/Ability row shrinks to
fit — so a long ability name can't run off the edge.

### Special abilities (`ability`)

`special` is just *text*; **`ability` is the mechanic the buddy actually uses in play**.
It runs once per tic from `P_Buddy_AbilityTicker` (`p_buddydef.c`, called by `P_Ticker`)
for whichever buddy you have selected:

| `ability` value | Behaviour |
|-----------------|-----------|
| `none` (default) | No special power |
| `poisoncloud` | Every 2 s, a cloud of gas around the buddy damages every enemy monster within 160 map units (4 damage, same-ish floor height) and puffs visible smoke. Players, other friendlies and corpses are never touched. |
| `drone` | Deploys a friendly **Security Drone** (`MT_SECDRONE`, `p_secdrone.c`) when an enemy is within 1024 units and none of ours is already out; at most one per 20 s. |
| `turret` | Tosses out a **sentry turret** exactly like the player's `key_turret` deploy (`MT_TURRET`, `p_turret.c`): spawned at the buddy, nudged forward so a wall can't swallow it, then thrown with a little arc. Same gate as the drone — an enemy within 1024 units, none of ours already out, at most one per 30 s. Costs no ammo, so the cap and cooldown are the balance instead. |

An unknown value is refused at load time with a console warning and falls back to
`none`, so a typo can't silently pretend the buddy has a power. All of them wait until
3 seconds into the level before firing, so nothing deploys on tic 0 before you've moved.

The built-in **Marine** (roster slot 0) reports `ability drone`, which names behaviour it
already had: `P_AICoop_MaybeSpawnDrone` deploys drones when it is under heavy fire,
surrounded, or its ammo is capped. That is the bot's own path, so the ticker above stays
out of the way while slot 0 is selected.

The ability runs on **whatever body your buddy currently has**. The code takes a plain
map object and never asks whether it is a player, so it needed no change when the actor
path went away — during the transition that means: pick a modder buddy and you get the
Marine's body with *your* buddy's power.

### Attack styles *(retired)*

`attack` used to pick a monster codepointer for the generated actor. Player 2 fights
with **weapons**, so the key no longer does anything — it is still parsed (old lumps
load fine) and still shown on the stats panel, and is expected to be replaced by a
weapon loadout key. The values that remain accepted: `baron`/`bruiser`/`hellknight`/
`knight`, `imp`/`troop`, `poss`/`zombie`/`pistol`/`zombieman`, `spos`/`shotgun`/
`shotgunguy`, `cpos`/`chaingun`/`chaingunner`, `sarg`/`demon`/`melee`/`bite`,
`head`/`caco`/`cacodemon`, `skel`/`revenant`, `fatt`/`mancubus`,
`bspi`/`arachnotron`, `none`.

---

## Sprites

### Frame naming

Author the sheet in the standard DOOM **monster** convention. For a 4-char base like
`FRAN`:

| Frames | Used for |
|--------|----------|
| `A`, `B` | idle / standing |
| `A`–`D` | walking |
| `E`, `F`, `G` | attacking |
| `H` | pain |
| `I`–`O` | death |

So a full sheet is `FRANA1`, `FRANB1`, … `FRANO1` (plus rotations `FRANA2A8`, etc.,
exactly like an IWAD monster). Put them between `S_START`/`S_END` **or**
`SS_START`/`SS_END` markers. Frame `A` is what the Buddy select screen previews, so
that one is needed even for a buddy you never see in-game yet.

> **Why the convention will matter.** Player frames run `A`–`W` with different
> meanings (`G` = pain, `H`–`N` = death, `O`–`W` = gibs) — a monster sheet dropped
> straight onto player 2 would show the wrong frames when your buddy is hurt or dies.
> Using a monster sheet as a player skin therefore needs a frame-remap table, which is
> part of the pending skin work; keep authoring monster-convention sheets, they are
> what the remap expects.

### Doom patch **or** PNG — both work

- **Doom patch** sprites (the classic 8-bit column format) render directly.
- **PNG** sprites (as authored for GZDoom) are **auto-converted** to the DOOM
  palette at load — you don't convert anything yourself. This is how a GZDoom
  mod's art (e.g. `FRANK.wad`) renders in BuddyDoom's software engine.

  > PNG conversion is **lossy**: images are quantised to the 256-colour palette and
  > transparency becomes on/off (no soft edges, no translucency/additive glow). It
  > looks great for palette-friendly art and rougher for detailed truecolour art.
  > Sprites taller than 254 px are clamped.

### Offsets

Sprite offsets (the point the sprite is drawn from) come from the patch header for
Doom patches, or from the PNG **`grAb`** chunk for PNG sprites — both are honoured
automatically, so your buddy stands on the floor correctly with no extra work.

---

## Sounds

A sound value is just **the name of the sound lump in your WAD** — write it exactly
as the lump is called. `seesound FRANKN` finds a lump named `FRANKN`.

DOOM's own sounds are all stored with a `DS` prefix (`DSPISTOL`, `DSBGSIT1`, …), and
that convention still works: BuddyDoom looks for `DS`+name **first**, then for the
bare name. So `seesound FRANKN` plays `DSFRANKN` **or** `FRANKN`, whichever your WAD
has — you don't have to know or strip the prefix. (`DS`+name wins if both exist, so a
stock sound can never be shadowed by an unrelated lump of the same bare name.)

Note lump names are 8 bytes: a name longer than 6 characters can only be found in the
bare form, because `DS`+7 chars no longer fits. Names are truncated to 8 characters.

Ship your own sound lumps (DMX or OGG — both are accepted), or point the fields at
stock IWAD sounds so the buddy is audible with no new audio:

```
  seesound    bgsit1     # imp sight
  painsound   dmpain
  deathsound  kntdth
  activesound dmact
```

Leave a field blank/absent for silence. If neither form of the lump is present when
the `BUDDYDEF` is read, the console prints a `BUDDYDEF: sound "…"` warning at startup
and that sound stays silent — the buddy still loads.

---

## Complete examples

### Example 1 — a buddy using only IWAD art (drop-in, zero new lumps)

Works immediately on any DOOM/DOOM2 IWAD because `BOSS` (Baron) already exists:

```
buddy {
  name        "Baron Ally"
  desc        "A Baron of Hell turned to your side. Heavy, hits hard."
  sprite      BOSS
  health      400
  speed       8
  attack      baron
  seesound    bgsit1
  painsound   dmpain
  deathsound  bgdth1
  activesound dmact
}
```

```
buddydoom -iwad DOOM2.WAD -file baron_ally.wad     # Main Menu → Buddy → Baron Ally
```

### Example 2 — a buddy with custom art (Frank N. Stein)

This is the shipped [`modding/frank.buddydef`](../modding/frank.buddydef). It expects
the `FRAN` sprites in the WAD (Frank's GZDoom PNGs convert automatically):

```
buddy {
  name        "Frank N. Stein"
  desc        "Gamma-powered bruiser reanimated to fight at your side. Tons of HP, a bit slow, hits like a Hell Knight. Sticks close and tears into anything hostile."
  sprite      FRAN
  health      999
  speed       10
  radius      24
  height      64
  mass        1000
  painchance  100
  attack      baron
  seesound    bgsit1
  painsound   dmpain
  deathsound  kntdth
  activesound dmact
  ednum       30001
}
```

```
buddydoom -iwad DOOM2.WAD -file FRANK.wad my_frank_buddydef.wad
```

---

## How selection works

- The **Buddy** menu lists roster slot **0 = Marine** (built-in) followed by every
  `BUDDYDEF` buddy found in the loaded WADs.
- Picking a buddy stores its index as **`buddy_select`** in `run/buddydoom.cfg`.
- A buddy is **player 2** — the co-op slot the marine bot drives (`p_ai_coop.c`). It is
  spawned by the normal co-op path, not by anything in `BUDDYDEF`.
- **During the transition**: selecting a modder buddy prints a one-line notice at
  startup and you play with the Marine's body. Its `ability` does run. Everything else
  the record declares is shown on the select screen and applied once the skin/stats
  work lands.

### What the game shows and how you order it around

Because the buddy *is* player 2, there is exactly one implementation of all of this —
the marine bot's — and your buddy gets it:

- **HUD strip** (top of screen, config `show_buddy_hud`) — name, HP, armor, current
  weapon and ammo, artifact inventory and the `BUF*` mugshot.
- **Automap** — a yellow arrow, a green medkit cross while down.
- **Console orders** — `where` (aliases `buddy`, `comp`), `report`/`status`,
  `come`/`follow`, `wait`/`stay`, `attack`, `buddyhome`/`buddytp`, plus the
  player-only powers `buddygod`/`buddyheal`/`buddyarm`/`buddygive`.
- **Key binds** — `key_buddy_come` / `key_buddy_stay` / `key_buddy_attack` and the
  `key_buddy_mode` toggle (right mouse button by default).
- **Revive** — stand next to your downed buddy and press USE.

---

## Packaging the WAD

One WAD, containing:

```
BUDDYDEF                     ← your text lump (this file)
S_START  (or SS_START)
FRANA1, FRANA2A8, … FRANO1   ← your sprites (Doom patch or PNG)
S_END    (or SS_END)
DSFRANKN, …                  ← your sounds (optional; or reuse IWAD sounds)
```

Any WAD/PWAD tool (SLADE, DeuTex, …) can build this. `BUDDYDEF` is just a text lump;
sprites go in the sprite namespace; sounds are `DS…` lumps.

---

## Advanced: DECOHack / DEHACKED friendly actors

Separately from `BUDDYDEF`, BuddyDoom exposes two code pointers that turn any friendly
(`+FRIEND`) DECOHack/DEHACKED actor into an **escort**:

- **`A_BuddyLook`** (spawn state) — acquire the nearest enemy, then go active.
- **`A_BuddyChase`** (see state) — fight that enemy; when none, follow the human.

See [`modding/frank.dh`](../modding/frank.dh) for a complete example. If your DECOHack
build doesn't know these pointer names, emit the frames with `A_NULL` and assign them
in a BEX `[CODEPTR]` block — BuddyDoom parses `[CODEPTR]` and knows `BuddyLook` /
`BuddyChase`.

> This is **not** the buddy path. Such an actor is a friendly monster: it walks with
> you and shoots things, and that is all. It takes no orders, has no HUD strip or
> automap marker, cannot open a door, cannot be revived and does not appear in the
> Buddy menu — because those all belong to a `player_t`, and it hasn't got one. If you
> want a companion, write a `BUDDYDEF` record.

---

## Limitations & troubleshooting

- **Sprite shows a "no preview" placeholder / buddy is invisible.** The sprite lumps
  aren't found. Check the 4-char `sprite` base matches your lump names (`FRAN` →
  `FRANA1`…), and that they're inside `S_START/S_END` or `SS_START/SS_END`.
- **Colours look off on a PNG buddy.** That's palette quantisation — inherent to the
  8-bit software renderer. Author with palette-friendly colours, or ship Doom-patch
  sprites for exact control.
- **No sound.** Sound fields take the DS-suffix (`FRANKN`, not `DSFRANKN`), and the
  `DS…` lump (or a stock IWAD sound) must be present.
- **I selected my buddy and got the Marine.** Expected during the transition — see
  the status box at the top. The startup console says so explicitly:
  `P_Buddy: '<name>' selected -- BUDDYDEF buddies are being ported to the player-2
  path; the Marine is your companion for now.`
- **No buddy at all on this map.** The co-op slot needs a `Player_2_Start` thing; a
  map without one disables the buddy and says so on the console. (Spawning player 2
  beside player 1 when the map has no co-op start is part of the pending work.)
  Buddies are also disabled in `-vanilla`, in netgames and during demo playback.
- **My buddy isn't in the menu.** The record is skipped when its `sprite` base has no
  art: `Buddy: '<name>' skipped -- sprite XXXX not found in WADs.`

See also: [`modding/README.md`](../modding/README.md), and for the renderer's PNG
handling, `docs/LEGACY_FIXES.md` (§17).
