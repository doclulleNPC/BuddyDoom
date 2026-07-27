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

## Quick start (3 steps)

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
  attack      baron                 # attack style, see table below
  seesound    FRANKN                # name of the sound lump (blank = silent)
  painsound   FRANKN
  deathsound  FRANKN
  activesound FRANKN
  ednum       30001                 # OPTIONAL DoomEd number (also map-placeable)
}
```

### Fields

| Key | Meaning | Default |
|-----|---------|---------|
| `name` | Display name (Buddy menu) | `Buddy` |
| `desc` / `about` / `info` | Description (wrapped) | *(empty)* |
| `sprite` | 4-char sprite base name | `PLAY` |
| `health` / `hp` | Spawn health | `200` |
| `speed` | Move speed, map units | `8` |
| `radius` | Collision radius, map units | `20` |
| `height` | Collision height, map units | `56` |
| `mass` | Knockback resistance | `100` |
| `painchance` | Flinch chance, 0–255 | `120` |
| `reactiontime` / `reaction` | Tics before reacting to a target | `8` |
| `attack` | Attack style (table below) | `melee` |
| `special` / `abilities` | Free-text blurb (shown on the Buddy screen) | *(empty)* |
| `ability` | **Named** special ability the buddy actually uses (table below) | `none` |
| `seesound` | Wake-up sound (lump name) | *(none)* |
| `painsound` | Hurt sound | *(none)* |
| `deathsound` | Death sound | *(none)* |
| `activesound` | Idle grunt | *(none)* |
| `ednum` / `doomednum` | Map editor number | `-1` (not map-placed) |

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
| `turret` | Tosses out a **sentry turret** exactly like the player's `key_turret` deploy (`MT_TURRET`, `p_turret.c`): spawned at the buddy, nudged forward so a wall can't swallow it, then thrown with a little arc. Same gate as the drone — an enemy within 1024 units, none of ours already out, at most one per 30 s. Costs no ammo (an mobj buddy has no inventory to spend, unlike the player's 50 bullets / 25 shells), so the cap and cooldown are the balance instead. |

An unknown value is refused at load time with a console warning and falls back to
`none`, so a typo can't silently pretend the buddy has a power. All of them wait until
3 seconds into the level before firing, so nothing deploys on tic 0 before you've moved.

The built-in **Marine** (roster slot 0) reports `ability drone`, which names behaviour it
already had: `P_AICoop_MaybeSpawnDrone` deploys drones when it is under heavy fire,
surrounded, or its ammo is capped. That path is the marine's own (it needs a `player_t`),
so it is not driven by the ticker above.

### Attack styles

Each `attack` maps to a stock DOOM attack (all reuse built-in projectiles, so you
need nothing extra):

| `attack` value(s) | Behaviour |
|-------------------|-----------|
| `baron`, `bruiser`, `hellknight`, `knight` | Claw up close, green plasma ball at range |
| `imp`, `troop` | Claw or fireball |
| `poss`, `zombie`, `pistol`, `zombieman` | Hitscan pistol |
| `spos`, `shotgun`, `shotgunguy` | Hitscan shotgun spread |
| `cpos`, `chaingun`, `chaingunner` | Hitscan chaingun |
| `sarg`, `demon`, `melee`, `bite` | Melee bite (no projectile) |
| `head`, `caco`, `cacodemon` | Cacodemon fireball |
| `skel`, `revenant` | Homing missile |
| `fatt`, `mancubus` | Mancubus fireballs |
| `bspi`, `arachnotron` | Plasma |
| `none` | Pacifist — follows but never attacks |

Behaviour of every buddy: it hunts the **nearest enemy** and fights it; when no
enemy is in reach it **follows you**. (Implemented by the `A_BuddyLook` /
`A_BuddyChase` code pointers — see *Advanced* below.)

---

## Sprites

### Frame naming

Sprites follow the standard DOOM monster convention. For a 4-char base like `FRAN`,
BuddyDoom builds all states from these frame letters:

| Frames | Used for |
|--------|----------|
| `A`, `B` | idle / standing |
| `A`–`D` | walking |
| `E`, `F`, `G` | attacking |
| `H` | pain |
| `I`–`O` | death |
| `O`…`I` (reverse) | resurrection (Arch-Vile / director revive) |

So a full sheet is `FRANA1`, `FRANB1`, … `FRANO1` (plus rotations `FRANA2A8`, etc.,
exactly like an IWAD monster). Put them between `S_START`/`S_END` **or**
`SS_START`/`SS_END` markers.

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

## How selection & spawning works

- The **Buddy** menu lists roster slot **0 = Marine** (built-in) followed by every
  `BUDDYDEF` buddy found in the loaded WADs.
- Picking a buddy stores its index as **`buddy_select`** in `run/buddydoom.cfg`.
- Choosing a `BUDDYDEF` buddy **replaces the marine**: at the start of every level it
  spawns beside you and follows/fights via the friendly AI. Choosing **Marine**
  (slot 0) restores the built-in companion. The change takes effect from the next
  level.
- If you gave the buddy an `ednum`, mappers can also place it directly in a map with
  a DoomEd editor.

### What the game shows and how you order it around

A `BUDDYDEF` buddy is an **mobj**, not player 2 — `P_Buddy_SpawnSelected` switches the
marine's co-op slot off. Since the HUD, the automap marker and the console orders were
all keyed on that slot, they ask `P_Buddy_Mobj()` (`p_buddydef.c`) instead when it is
empty, so a modder buddy is recognised just like the marine:

- **HUD strip** (top-right, config `show_buddy_hud`) — the buddy's **name**, `HP n/max`
  coloured by percentage, and its state: `FOLLOWING` / `FIGHTING` / `HOLDING` /
  `COMING` / `DOWN`. No armor/weapon/ammo/mugshot line: an mobj has none of those.
- **Automap** — a yellow arrow (green when it is down), same as the marine's.
- **Console orders** — `where` (aliases `buddy`, `comp`), `report`/`status`,
  `come`/`follow`, `wait`/`stay`, `attack`, and `buddyhome`/`buddytp` (warps it back to
  your side; a mobj buddy has no map spawn point to return to). `come` makes it break
  off and pad back to you for 8 seconds; `wait` toggles hold-position. `A_BuddyChase`
  checks both each time it runs. `buddygod`/`buddyheal`/`buddyarm` are marine-player
  powers and reply that they only work for the Marine.

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

## Advanced: DECOHack / DEHACKED buddies

If you'd rather author a full actor in **DECOHack** (or hand-written DEHACKED),
BuddyDoom exposes two code pointers that turn any friendly (`+FRIEND`) actor into a
companion:

- **`A_BuddyLook`** (spawn state) — acquire the nearest enemy, then go active.
- **`A_BuddyChase`** (see state) — fight that enemy; when none, follow the human.

See [`modding/frank.dh`](../modding/frank.dh) for a complete DECOHack buddy. If your
DECOHack build doesn't know these pointer names, emit the frames with `A_NULL` and
assign them in a BEX `[CODEPTR]` block — BuddyDoom parses `[CODEPTR]` and knows
`BuddyLook` / `BuddyChase`. Note: DECOHack requires the `decohack` compiler; the
native `BUDDYDEF` route above does not, and also carries the name/description the
menu shows (DEHACKED does not), so `BUDDYDEF` is recommended for most modders.

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
- **Buddy doesn't appear in-game after selecting.** Selection applies from the next
  level — start a new game or change level. Buddies are disabled in `-vanilla`, in
  netgames, and during demo playback.
- **Attack does nothing.** `attack none` never fires; otherwise verify the value is
  in the attack table.

See also: [`modding/README.md`](../modding/README.md), and for the renderer's PNG
handling, `docs/LEGACY_FIXES.md` (§17).
