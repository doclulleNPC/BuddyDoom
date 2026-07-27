# BuddyDoom modding: custom co-op buddies

BuddyDoom lets modders add selectable co-op **buddies** with no external toolchain.
Drop a `BUDDYDEF` text lump into a WAD (plus the buddy's sprites and sounds) and it
shows up in **Main Menu → Buddy** (a Hexen-style select screen) and can be chosen as
your companion.

> **Status:** a buddy is **always player 2** — that is how it inherits door use,
> orders, the HUD, revive, the pathfinder and weapons from the co-op bot. The old path
> (generating a standalone monster actor from your record) is gone. Applying a record
> to player 2 is in progress: your buddy is listed, previewed and its `ability` runs,
> but the body is still the Marine's for now. See
> [`../docs/BUDDY_MODDING.md`](../docs/BUDDY_MODDING.md).

There are two formats here; pick one:

| File | Format | Needs a compiler? |
|------|--------|-------------------|
| `frank.buddydef` | **native BUDDYDEF** — read straight from the WAD | **No** |
| `frank.dh` | DECOHack source (→ DEHACKED) | Yes (DoomTools `decohack`) |

`BUDDYDEF` is the buddy format. `frank.dh` is something else: a DECOHack **friendly
actor** using the `A_BuddyLook`/`A_BuddyChase` code pointers — a monster that walks
with you and shoots things, with no orders, no HUD and no menu entry.

## The BUDDYDEF format

A `BUDDYDEF` lump is plain text: one `buddy { ... }` block per buddy, `key value`
pairs, `#` comments. See `frank.buddydef` for a complete example.

```
buddy {
  name        "Frank N. Stein"     # shown in the Buddy menu
  desc        "Gamma bruiser..."    # menu description (wrapped)
  sprite      FRAN                  # 4-char sprite base (needs FRANA1.. in the WAD)
  health      999
  speed       10                    # map units per move (baron 8, demon 10)
  radius      24                    # map units
  height      64
  mass        1000
  painchance  100                   # 0-255
  color       green                 # default colour on the Buddy screen
  seesound    FRANKN                # DS-lump suffix -> dsfrankn (blank = silent)
  painsound   FRANKN
  deathsound  FRANKN
  activesound FRANKN
  special     "Tanky bruiser"       # blurb on the Buddy screen
  ability     poisoncloud           # none | drone | poisoncloud | turret
}
```

`attack` and `ednum` are still accepted but no longer do anything: player 2 fights with
weapons and cannot be placed in a map.

**Sprite frames** follow the standard DOOM monster convention: `A`–`B` idle, `A`–`D`
walk, `E`–`G` attack, `H` pain, `I`–`O` death — a full sheet is `SPRTA1..SPRTO1`
(+ rotations). Frame `A` is what the select screen previews; the rest is what the
pending player-skin remap expects.

## Sprites: Doom patch **or** PNG

BuddyDoom's renderer is the **1993 software renderer**, but it accepts both sprite
formats (in an `S_START/S_END` **or** `SS_START/SS_END` namespace):

- **Doom patch** sprites render directly.
- **PNG** sprites (as authored for GZDoom) are **auto-converted** to the DOOM palette
  at load — no manual step. This is how the bundled **`FRANK.wad`** (whose `FRAN`
  sprites are PNG) renders in BuddyDoom. Conversion is **lossy** (256-colour quantise,
  on/off transparency, no translucency; sprites >254 px are clamped), and sprite
  offsets are taken from the PNG `grAb` chunk automatically.

To try the buddy system immediately with **stock IWAD art**, point `sprite` at a sprite
already in your IWAD, e.g. `sprite BOSS` (Baron of Hell) or `sprite TROO` (Imp).

See **[`../docs/BUDDY_MODDING.md`](../docs/BUDDY_MODDING.md)** for the full field
reference and packaging guide.

## How selection works

- Roster slot **0** is always the built-in **Marine** (the hard-coded player-2 buddy) —
  unchanged default.
- Slots **1..N** are your `BUDDYDEF` buddies.
- Slots **1..N** are your `BUDDYDEF` buddies. The choice is persisted as
  `buddy_select` in `run/buddydoom.cfg`.
- Whichever is selected, the companion is **player 2**, spawned by the normal co-op
  path. During the transition a modder selection prints a notice at startup and you
  play with the Marine's body; the selected buddy's `ability` still runs.

```
buddydoom -iwad DOOM2.WAD -file yourbuddy.wad   # then Main Menu -> Buddy
```
