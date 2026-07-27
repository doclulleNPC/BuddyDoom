# UDMF Support — As-Built Audit

**Status: IMPLEMENTED.** UDMF (`doom` namespace) map loading shipped in
`files/p_udmf.c` / `files/p_udmf.h`, wired into `P_SetupLevel`
(`files/p_setup.c`), with GL/ZDBSP node support in `P_LoadNodes_Extended`. The
user-facing feature doc is `docs/UDMF.md`. This file was originally a forward
plan; it has been rewritten as an **audit of the code that actually landed** —
what matches the design, what deviates, and what's still missing.

**Spec:** `docs/udmf11.txt` (UDMF v1.1, Quasar 2009).
**Ported from:** Woof's `p_udmf.c` + its GL-node reader (per `docs/UDMF.md`).

---

## 0. Verdict

The implementation is **more complete than the original plan anticipated** and is
verified working on real maps. The single biggest risk the plan flagged — GL nodes
(old §4) — is fully solved: `XGLN`/`XGL2`/`XGL3` plus their zlib `Z*` variants are
all parsed, including the hard miniseg case.

What's here and correct:

- **Detection guard** — `UDMF_IsMap` tests `lumpnum+1 == "TEXTMAP"` (8-byte,
  case-insensitive compare), inert for binary maps (`p_setup.c:1115`).
- **Self-contained tokenizer + recursive-descent parser** for the UDMF grammar,
  with `//` and `/* */` comments, quoted strings, and unknown-block/field skipping
  (`p_udmf.c:67–329`).
- **Geometry build mirrors the binary `P_Load*` field-for-field** — vertices,
  sectors, sides, lines (incl. `slopetype`/`bbox`/dummy-right-side/`ML_*` flags and
  even the Boom-260 translucent-midtex pass) (`p_udmf.c:339–445`).
- **GL/ZDBSP nodes** — `XNOD`/`ZNOD` + `XGLN`/`XGL2`/`XGL3` and zlib variants, with
  correct GL-seg implicit-v2 back-patching and miniseg handling
  (`p_setup.c:295–468`).
- **Blockmap rebuilt from linedefs** when absent (`P_LoadBlockMap(-1)`), **reject
  synthesized** all-visible when absent/short (`P_LoadReject_UDMF`,
  `p_setup.c:1030`).
- **Determinism preserved** — float→fixed conversion via `lround(x*FRACUNIT)`
  happens only at load; the playsim never sees a double (`p_udmf.c:346`).
- **Build wiring complete** — `p_udmf.obj` is in `files/Makefile.msvc` OBJS
  (line 41), CMake globs `files/*.c`, `build.sh` globs `*.c`. No LNK2019 this time.
- **Bonus:** the work uncovered and fixed a *pre-existing* NULL-buddy crash on
  single-player-only maps (`P_AICoop_VerifySpawn` + `P_Ticker` guard) — see
  `docs/UDMF.md` note.

**Verified** (per `docs/UDMF.md`): parser/geometry/things/blockmap/reject + `XNOD`
on a real 704-line map (`run/ID0/udmftest.wad`); `XGL3` on a hand-built GL map
(`run/ID0/udmfgl.wad`).

---

## 1. Findings (audit of the shipped code)

Ranked most to least significant. None are showstoppers for the maps BuddyDoom
targets; all are worth recording.

### F1. Integer literals: hex/octal not parsed — spec deviation — ✅ FIXED
`p_udmf.c` originally read all integers with `atoi`, which stops at the `x` in a
hex literal (`atoi("0x1f") == 0`); the UDMF grammar (`udmf11.txt:64`) permits
**hex `0x…`** and **octal `0NNN`** integers. Fixed by adding `SC_Int` (`strtol
(v, NULL, 0)`, base 0 → auto-detects dec/oct/hex) and routing every integer field
through it (`p_udmf.c` SC_Int helper + sector/side/line/thing parsers). Float
coordinates still use `atof`. Impact was low (editors emit decimal) but this is
now spec-correct.

### F2. Thing `dm` / `coop` / `friend` flags dropped
`UDMF_ParseThing` (`p_udmf.c:258–287`) honors `skill1..5`, `ambush`, and `single`,
but **ignores `dm`, `coop`, and MBF `friend`**. Only the "not in single-player"
bit (0x10) is emitted; the Boom `MTF_NOTDM` (0x20) / `MTF_NOTCOOP` (0x40) bits are
never set. **Impact:** things meant for one multiplayer mode only can't be
distinguished — a DM-only or coop-only actor is treated as "present in all net
modes." Minor for default SP+buddy play; a fidelity gap for MP maps. `friend`
being dropped is fine (BuddyDoom has its own buddy system).

### F3. Unsupported namespaces hard-error (intentional, but note the hole)
`UDMF_ParseNamespace` (`p_udmf.c:292–299`) calls `I_Error` for anything other than
`doom`/`heretic`/`strife`. This is a deliberate, documented choice (reject rather
than silently mis-simulate Hexen/ZDoom parameterized specials) and differs from the
plan's softer "warn and load geometry." Defensible. **The gap:** the namespace
statement is *optional* in the parser — a map that omits `namespace` gets **no
validation** and loads as `doom`. A malformed Hexen-namespace map missing its
namespace line would load and misbehave rather than erroring. Low probability.

### F4. Intermediate arrays use `malloc`/`realloc`, not the zone allocator
The parser's scratch arrays (`uv/us/usd/ul/ut`) grow via `realloc` and are `free`d
(`p_udmf.c:139–156`). CLAUDE.md says "no `malloc`/`free` for game data — use the
zone allocator." **Mitigating:** these are transient *load-time* scratch buffers
freed before the tic loop (mirroring `w_inflate.c`, which also mallocs), and the
**final engine arrays are correctly `Z_Malloc(PU_LEVEL)`** — so no game data lives
outside the zone. Acceptable, but a deviation from the stated convention; could be
moved to a `PU_STATIC` zone buffer if strict adherence is wanted.

### F5. Robustness — no bounds check on seg→linedef index — ✅ FIXED
Both the XNOD and GL seg readers did `&lines[ld]` / `&vertexes[v]` with no range
check; a corrupt/hostile `ZNODES` lump with an out-of-range index → OOB read →
crash. Both readers now guard the linedef and vertex indices against
`numlines`/`numvertexes` and `I_Error` cleanly (spec §II.A: refuse a map that
exceeds engine limits rather than destabilize) instead of reading out of bounds.
The GL path guards the per-seg `v1` always and the linedef index only on non-
miniseg segs.

### F6. `UDMF_Field` recurses on stray tokens
On a non-ID token it does `return UDMF_Field(...)` (`p_udmf.c:166`). C doesn't
guarantee tail-call elimination, so a pathologically long run of stray punctuation
inside a block could overflow the stack. Malformed-input-only; convert to a loop if
hardening.

### F7. Minisegs implemented but not verified end-to-end
The miniseg path (`R_AddLine` early-return on `linedef==NULL`; `P_GroupLines`
scan-past; the GL 2nd-pass sector adoption at `p_setup.c:426–437`) is present and
looks correct, but per `docs/UDMF.md` it has **not** been exercised on a real
GZDoom/UDB export whose BSP cuts a sector (the common real-world case). Recommend
loading one such map before calling GL support battle-tested.

### F8. Dropped fields (correctly out of scope, documented)
Per-texture offsets/scale, slopes, 3D floors, colormap/tint, sector
rotation/gravity/friction, linedef `arg0..arg4`/`alpha`/`health`, thing
`height`(Z)/`tid`/`special`/`args`. All beyond the 1993 playsim and honestly listed
under "Dropped" in `docs/UDMF.md`. No action — this is the intended scope boundary.

---

## 2. Recommended follow-ups (in priority order)

- ✅ **F1** — done (`strtol` hex/octal).
- ✅ **F5** — done (seg linedef/vertex bounds guards + `I_Error`).

Remaining:

1. **F7** — obtain/build a real GZDoom UDMF export with minisegs and confirm it
   renders; this is the last unverified corner of the node reader.
2. **F2** — emit `MTF_NOTDM`/`MTF_NOTCOOP` from `dm`/`coop` for MP-map fidelity.
3. **F3/F6** — hardening only; do if UDMF maps from untrusted sources become a
   concern.

---

## 3. Phase-2 (Hexen/ZDoom namespace) — still not started, still a big lift

The original plan's phase 2 stands unchanged and remains **out of scope**: the
`hexen`/`zdoom`/`eternity` namespaces need `line_t`/`sector_t` `id` distinct from
`tag`, `args[5]` on lines and things, thing `special` + Z-height, and a scripting
(ACS) runtime — none of which the deterministic 1993 playsim has. Adding thing
specials/args to `mobj_t` would also re-fingerprint `files/buddydoom_saveg.sig` and
auto-bump `VERSION_NUM` (CLAUDE.md auto-versioning). The current code correctly
**refuses** these namespaces (F3) rather than pretending to support them.

---

## Appendix: field → struct mapping (as implemented)

Matches `docs/UDMF.md`; reproduced here as the audit reference.

| element | UDMF keys read | engine field | code |
|---|---|---|---|
| vertex | `x`,`y` | `vertex_t.x/y` = `lround(v*FRACUNIT)` | `p_udmf.c:339` |
| sector | `heightfloor`,`heightceiling`,`texturefloor`,`textureceiling`,`lightlevel`(def 160),`special`,`id` | `sector_t` heights/pics/light/special/**tag=id** | `p_udmf.c:351` |
| sidedef | `offsetx`,`offsety`,`texturetop`,`texturemiddle`,`texturebottom`,`sector` | `side_t` offsets/textures/sector | `p_udmf.c:374` |
| linedef | `v1`,`v2`,`sidefront`,`sideback`,`special`,`id`, flag bools | `line_t` v1/v2/sidenum/special/**tag=id**/`flags` | `p_udmf.c:394` |
| thing | `x`,`y`,`angle`,`type`,`skill1..5`,`ambush`,`single` | vanilla `mapthing_t` + options byte | `p_udmf.c:258` |

Linedef flag booleans → `ML_*` bits: `blocking`,`blockmonsters`,`twosided`,
`dontpegtop`,`dontpegbottom`,`secret`,`blocksound`,`dontdraw`,`mapped`,`passuse`,
`blocklandmonsters`,`blockplayers` (`p_udmf.c:239–253`). Thing skill/mode folding:
`skill1|skill2`→easy, `skill3`→medium, `skill4|skill5`→hard, `ambush`→deaf,
`!single`→multiplayer-only (`p_udmf.c:278–283`).
