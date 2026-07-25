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

## Sprite format: Doom patches, not PNG  ⚠️

BuddyDoom's renderer is the **1993 software renderer** — sprites must be **Doom patch**
lumps (in an `S_START/S_END` **or** `SS_START/SS_END` namespace). It **cannot** draw
**PNG** sprites the way GZDoom does.

The bundled **`FRANK.wad`** (the GZDoom *Frank N. Stein* mod) stores its `FRAN` sprites
as PNG, so Frank shows up in the roster but with a **"no preview"** placeholder and is
invisible in-game. BuddyDoom detects the PNG lumps and **skips them instead of crashing**.
To use Frank's real art you must convert those PNGs to Doom-patch format (quantised to
the game palette, with the right sprite offsets) — ask and a converter can be provided.

To try the buddy system immediately with **stock IWAD art**, point `sprite` at a sprite
already in your IWAD, e.g. `sprite BOSS` (Baron of Hell) or `sprite TROO` (Imp).

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
