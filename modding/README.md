# BuddyDoom modding: custom co-op buddies

BuddyDoom lets modders add selectable co-op **buddies** with no external toolchain.
Drop a `BUDDYDEF` text lump into a WAD (plus the buddy's sprites and sounds) and it
shows up in **Main Menu → Buddy** (a Hexen-style select screen) and can be chosen as
your companion. The engine builds the whole friendly actor for you.

There are two formats here; pick one:

| File | Format | Needs a compiler? |
|------|--------|-------------------|
| `frank.buddydef` | **native BUDDYDEF** — read straight from the WAD | **No** |
| `frank.dh` | DECOHack source (→ DEHACKED) | Yes (DoomTools `decohack`) |

The native `BUDDYDEF` route is the recommended one — no `decohack`, and it carries the
name/description the select screen shows. Use `frank.dh` only if you want a full
DECOHack actor with custom states.

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
  attack      baron                 # see list below
  seesound    FRANKN                # DS-lump suffix -> dsfrankn (blank = silent)
  painsound   FRANKN
  deathsound  FRANKN
  activesound FRANKN
  ednum       30001                 # optional DoomEd number (also map-placeable)
}
```

**`attack`** picks the fire behaviour (all reuse stock projectiles, so nothing else is
needed): `baron`, `imp`, `poss`, `spos`, `cpos`, `sarg`/`melee`, `head`, `skel`,
`fatt`, `bspi`, or `none` (pacifist follower).

**Sprite frames** follow the standard DOOM monster convention — the engine builds all
states from them: `A`–`B` idle, `A`–`D` walk, `E`–`G` attack, `H` pain, `I`–`O` death.
So a full sheet is `SPRTA1..SPRTO1` (+ rotations). BuddyDoom injects `A_BuddyLook`
(spawn) and `A_BuddyChase` (see) — a friendly actor that fights the nearest enemy and
otherwise follows you.

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
reference, attack-style table, and packaging guide.

## How selection works

- Roster slot **0** is always the built-in **Marine** (the hard-coded player-2 buddy) —
  unchanged default.
- Slots **1..N** are your `BUDDYDEF` buddies.
- Choosing a `BUDDYDEF` buddy replaces the marine: it spawns beside you at the start of
  each level and follows/fights via the friendly AI. Persisted as `buddy_select` in
  `run/buddydoom.cfg`; takes effect from the next level.

```
buddydoom -iwad DOOM2.WAD -file yourbuddy.wad   # then Main Menu -> Buddy
```
