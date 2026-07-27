# UDMF map support (BuddyDoom)

BuddyDoom loads **UDMF** (Universal Doom Map Format) maps — the text `TEXTMAP`
map format — in addition to the classic binary format. This is the 1993 playsim,
so support is scoped to the **`doom` namespace**: the geometry, textures, sector
specials and thing flags that the vanilla/Boom engine can actually execute.

Ported from **Woof**'s `p_udmf.c` and its GL/ZDBSP node reader, adapted to this
fork's structures. Reference ports used: `../woof`, `../dsda-doom`, `../gzdoom`.

## What a UDMF map looks like

A UDMF map replaces the eight binary lumps (`THINGS`…`BLOCKMAP`) with:

```
E1M1        (or MAP01 …)   -- the map marker
TEXTMAP     -- the whole map as text (namespace, vertices, linedefs, sidedefs, sectors, things)
ZNODES      -- the BSP tree (extended / GL nodes), since UDMF ships no binary NODES/SEGS/SSECTORS
BLOCKMAP    -- optional (usually absent -> rebuilt from the linedefs)
REJECT      -- optional (absent/short -> a zero-filled "all visible" matrix)
ENDMAP
```

Detection is automatic: if the lump right after the map marker is `TEXTMAP`,
`P_SetupLevel` takes the UDMF path (`p_setup.c`).

## Supported namespaces

`doom`, `dsda` (DSDA-Doom's namespace — the same Boom/MBF21/ID24 feature set this
engine implements), and `heretic` / `strife` (geometry-identical, `id == tag`,
classic specials). **Save UDMF maps from (Ultimate) Doom Builder with the
DSDA-Doom game configuration** — it writes `namespace = "dsda"`. The `hexen` /
`zdoom` / `eternity` namespaces are **rejected** with an error: they imply
parameterised specials, `arg0..arg4`, slopes and 3D floors this engine has no
simulation for, so loading them would silently misbehave.

## Field mapping (doom namespace)

| element  | UDMF keys read | engine field |
|----------|----------------|--------------|
| vertex   | `x`, `y` | `vertex_t.x/y` (fixed) |
| linedef  | `v1`,`v2`,`sidefront`,`sideback`,`special`,`id`, flag booleans | `line_t` v1/v2/sidenum/special/**tag**, `flags` (`ML_*`) |
| sidedef  | `offsetx`,`offsety`,`texturetop`,`texturemiddle`,`texturebottom`,`sector` | `side_t` offsets/textures/sector |
| sector   | `heightfloor`,`heightceiling`,`texturefloor`,`textureceiling`,`lightlevel`,`special`,`id` | `sector_t` heights/pics/light/special/**tag** |
| linedef tag | `id`, falling back to `arg0` when `id` is unset (spec stores the tag in both) | `line_t.tag` |
| thing    | `x`,`y`,`height`,`angle`,`type`,`skill1..5`,`single`,`dm`,`coop`,`ambush`,`friend` | vanilla `mapthing_t` (x/y rounded to int) + options byte; `height` applied as a Z offset after spawn |

Linedef flag booleans map to the matching `ML_*` bit
(`blocking`,`blockmonsters`,`twosided`,`dontpegtop`,`dontpegbottom`,`secret`,
`blocksound`,`dontdraw`,`mapped`,`passuse`,`blocklandmonsters`,`blockplayers`).
Thing skill/mode booleans are folded back into the classic vanilla/Boom options
byte (`skill1|skill2`→easy, `skill3`→medium, `skill4|skill5`→hard, `ambush`→deaf,
`!single`→not-in-SP, `!dm`→not-in-DM `0x20`, `!coop`→not-in-coop `0x40`,
`friend`→MBF friend `0x80`). The DM/co-op bits only take effect in a real netgame,
so single-player + AI buddy is unaffected.

**Dropped** (no engine support): per-texture offsets/scaling, light/tint/colormap,
sector rotation/gravity/friction, linedef `arg1..arg4`/`alpha`/`health`, thing
`tid`/`special`/`args`. Sub-unit vertex/thing coordinates are rounded (things) or
kept in fixed-point (vertices).

## Parameterised (ZDoom/Hexen) specials are NOT executed

BuddyDoom's playsim dispatches **classic tag-based** line/sector specials (a
special number = one fixed action; the target is the line's tag; no `arg1..arg4`,
no Hexen SPAC activation flags). If a map's linedefs carry `arg1..arg4` or SPAC
flags (`playercross`, `playeruse`, `repeatspecial`, …) — the **parameterised**
model used by ZDoom/Hexen/Eternity — those specials cannot run here, and the
loader prints a one-time warning. The map's geometry still renders, but its doors,
switches, lifts and exits will misbehave. **This is why the DoomBuilder "DSDA-Doom"
UDMF config is a poor fit if it emits parameterised specials** (some versions do):
build with a Doom/Boom-format configuration for specials that actually work.

## Nodes (`ZNODES`)

`P_LoadNodes_Extended` (`p_setup.c`) reads the `ZNODES` lump. It handles the whole
ZDBSP family, uncompressed (`X*`) and zlib-compressed (`Z*`, via `W_InflateZlib`):

- **`XNOD` / `ZNOD`** — non-GL extended nodes (explicit seg v1/v2).
- **`XGLN` / `ZGLN`** — GL nodes, 16-bit linedef index.
- **`XGL2` / `ZGL2`** — GL nodes, 32-bit linedef index.
- **`XGL3` / `ZGL3`** — GL nodes with 32-bit fixed-point partition lines.

GL segs store only one vertex; the second is the next seg's first within the
subsector (circular), and `linedef == 0xFFFF/0xFFFFFFFF` marks a **miniseg** (an
internal, wall-less partition edge). Minisegs are skipped by the renderer
(`R_AddLine` returns early when `linedef == NULL`) and `P_GroupLines` scans past
them to find each subsector's real sector.

## Testing

`scratchpad/mkudmf.py` converts any binary map into a UDMF map
(`TEXTMAP` + `XNOD`-in-`ZNODES`):

```
python mkudmf.py e1-arenas.wad E1M1 udmftest.wad
buddydoom -iwad DOOM.WAD -file udmftest.wad -warp 1 1
```

`run/ID0/udmftest.wad` is such a map (E1M1, 704 lines) and loads/renders cleanly.

**Status — verified:**
- Parser, geometry build, thing spawn, blockmap rebuild, reject fallback and the
  **`XNOD`** node path — on a real 704-line map (`udmftest.wad`), 0 texture errors,
  renders without crashing.
- The **`XGL3` GL-node path** (GL segs with implicit second vertex + 32-bit
  fixed-point node records) — on a hand-built two-room map (`run/ID0/udmfgl.wad`,
  built to exercise a GL split without minisegs), renders correctly.

The `XGLN`/`XGL2` seg records share the same reader as `XGL3` (only the linedef
index width / node precision differ), so they follow the same verified path. A map
with **minisegs** (a BSP partition that cuts through a sector — the common case in
real GZDoom/UDB exports) hasn't been exercised end-to-end; the miniseg handling is
implemented (`R_AddLine` skip + `P_GroupLines` scan) but confirm with a real export.

> Note: while testing this, a **pre-existing crash** was found and fixed — a map
> with the co-op buddy enabled (the default) but **no `Player_2_Start`** left the
> buddy slot marked in-game with no body, and `P_Ticker` → `P_PlayerThink`
> dereferenced the NULL mobj. Fixed in `P_AICoop_VerifySpawn` (clears the slot) with
> a defensive `mo` guard in `P_Ticker`. It affected any single-player-only map run
> with the default buddy, not just UDMF ones.
