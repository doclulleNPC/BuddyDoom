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
  seesound    FRANKN                # DS-lump suffix → dsfrankn (blank = silent)
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
| `special` / `abilities` | Free-text special abilities (shown on the Buddy screen) | *(empty)* |
| `seesound` | Wake-up sound (DS-suffix) | *(none)* |
| `painsound` | Hurt sound | *(none)* |
| `deathsound` | Death sound | *(none)* |
| `activesound` | Idle grunt | *(none)* |
| `ednum` / `doomednum` | Map editor number | `-1` (not map-placed) |

The **Buddy select screen** (Main Menu → Buddy) shows a stats panel with all of these
— HP, Speed, Size (`radius`×`height`), Mass, Pain, Reaction, Attack, and the `special`
abilities text — so every value you set in the `BUDDYDEF` is displayed there.

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

Sound values are the **DS-lump suffix**: `seesound FRANKN` plays the lump
`DSFRANKN`. Ship your `DS…` lumps in the WAD, or point the fields at stock IWAD
sounds so the buddy is audible with no new audio:

```
  seesound    bgsit1     # imp sight
  painsound   dmpain
  deathsound  kntdth
  activesound dmact
```

Leave a field blank/absent for silence.

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
