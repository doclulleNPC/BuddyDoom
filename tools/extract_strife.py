#!/usr/bin/env python3
"""
extract_strife.py -- build run/ID0/strifestuff.wad: the Strife MONSTER sprites (as
PNG, Strife's own palette) + its sounds, so the ported Strife actors (files/strife*.c,
MT_S_*) can be summoned while playing DOOM -- the same trick hereticstuff.wad /
hexenstuff.wad play for their games.

Scope: MONSTERS ONLY.  The sprite list is derived from files/strife_mon.c -- every
SPR_S_* the monster/boss installer actually touches, and nothing else -- so it can
never drift from the port.  Strife's items, weapons and scenery are deliberately
left out: they are not summonable content, and they were most of the sprite-name
collisions.

Collisions (docs/BUDDY_SPRITE_COLLISIONS.md).  BuddyDoom merges every S_START..S_END
region into ONE sprite namespace, so a pack lump whose 4-char code matches a host
sprite REPLACES it.  Two ways out, both taken from GZDoom's RenameSprites()
(../gzdoom/src/d_main.cpp):

  * RENAME, when GZDoom's StrifeRenames[] has a spelling for it and that spelling is
    free -- SPID -> STLK (the Stalker), MISL -> SMIS.  The engine points its SPR_S_*
    slot at the placeholder outside strife_mode, via the generated files/strife_ph.inc.
  * OMIT, when it does not.  GIBS/PLAY/POW1/SHT1/TFOG are generic effects (gibs, the
    player, a zap, a puff, teleport fog) that every host game already owns a lump for,
    so simply not shipping them leaves the actor drawing the host's equivalent --
    which is what you want anyway, and cannot shadow anything.

Sounds are Strife's own DS* lumps, copied verbatim -- EXCEPT the ones that share a
name with a DOOM sound (DSOOF, DSBAREXP, ...).  Those are skipped rather than
renamed: copying them would silently replace DOOM's own audio, and the DOOM lump of
the same name is a fine stand-in for the Strife actor.

Usage:
    python3 tools/extract_strife.py                    # src ID0/strife1.wad
    python3 tools/extract_strife.py --src /path/strife1.wad --out ID0/strifestuff.wad
"""

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wadpng import patch_to_png		# noqa: E402  (needs the path tweak above)
from wadcodes import reserved_codes, sprite_codes_in_wad	# noqa: E402


# ---------------------------------------------------------------------------
# WAD I/O (same shape as extract_hexen.py)
# ---------------------------------------------------------------------------
def read_wad(path):
    data = path.read_bytes()
    _magic, n, off = struct.unpack("<4sII", data[:12])
    ent = []
    for i in range(n):
        fp, sz = struct.unpack("<II", data[off+i*16: off+i*16+8])
        nm = data[off+i*16+8: off+i*16+16].split(b"\x00")[0].decode("latin1")
        ent.append((nm, fp, sz))
    return data, ent


def lump(data, ent, name):
    for nm, fp, sz in ent:
        if nm == name:
            return data[fp:fp+sz]
    return None


def pal(data, ent):
    p = lump(data, ent, "PLAYPAL")
    return [(p[i*3], p[i*3+1], p[i*3+2]) for i in range(256)]


def write_wad(out, lumps):
    cur = 12
    de = []
    for nm, d in lumps:
        de.append((cur, len(d), nm)); cur += len(d)
    with open(out, "wb") as f:
        f.write(b"PWAD"); f.write(struct.pack("<II", len(lumps), cur))
        for _n, d in lumps:
            f.write(d)
        for fp, sz, nm in de:
            f.write(struct.pack("<II", fp, sz))
            f.write(nm.encode("ascii", "replace")[:8].ljust(8, b"\x00"))


def is_dmx(raw):
    return len(raw) >= 8 and raw[0] == 3 and raw[1] == 0


# ---------------------------------------------------------------------------
# The engine's own tables are the source of truth for BOTH lists below -- the
# Strife codes we must ship, and the other games' codes we must not collide with.
# ---------------------------------------------------------------------------
def read_engine_codes(files):
    """-> (monster codes in enum order, their SPR_S_* names, all other games' codes).

    The Strife half is narrowed to what files/strife_mon.c installs: that file fills
    every MT_S_* monster/boss and their projectiles, and each of its ST() rows names
    the sprite slot it uses, so grepping SPR_S_* out of it yields exactly the monster
    art -- no hand-kept list to fall out of date.
    """
    src = (files / "info.c").read_text()
    body = re.search(r"char \*sprnames_builtin\[NUMSPRITES\] = \{(.*?)\n\};", src, re.S).group(1)
    strife, other = [], []
    for line in body.split("\n"):
        if "#include" in line:
            inc = line.split('"')[1]
            codes = re.findall(r'"([A-Z0-9]{4})"', (files / inc).read_text())
            (strife if "strife_sprnames" in inc else other).extend(codes)
            continue
        other += re.findall(r'"([A-Z0-9]{4})"', line.split("//")[0])
    enums = re.findall(r"\b(SPR_S_[A-Z0-9_]+)\b", (files / "strife_spr.inc").read_text())
    if len(enums) != len(strife):
        raise SystemExit(f"strife_spr.inc ({len(enums)}) and strife_sprnames.inc "
                         f"({len(strife)}) are out of step -- regenerate them together")

    used_by_monsters = set(re.findall(r"\b(SPR_S_[A-Z0-9_]+)\b",
                                      (files / "strife_mon.c").read_text()))
    keep = [(e, c) for e, c in zip(enums, strife) if e in used_by_monsters]
    if not keep:
        raise SystemExit("no SPR_S_* found in strife_mon.c -- has the port moved?")
    return [c for _e, c in keep], [e for e, _c in keep], other


# GZDoom hit this exact problem and solved it the same way: RenameSprites() in
# ../gzdoom/src/d_main.cpp renames the colliding sprite lumps of the IWAD at load,
# per game, from a fixed table ("Renames sprites in IWADs so that unique actors can
# have unique sprites, making it possible to import any actor from any game into any
# other game").  Its StrifeRenames[] table is reproduced verbatim below, so a pack
# built here uses the SAME spellings the rest of the ecosystem already knows --
# notably SPID -> STLK for the Stalker, which tools/mybuddy.cpp and
# docs/BUDDY_SPRITE_COLLISIONS.md already assume.
#
# Anything colliding that GZDoom's table does NOT cover falls through to the
# deterministic Y* scheme in make_rename() below.
GZDOOM_STRIFE_RENAMES = {
    "MISL": "SMIS",	# lots of places
    "ARM1": "ARM3",	# MetalArmor
    "ARM2": "ARM4",	# LeatherArmor
    "PMAP": "SMAP",	# StrifeMap
    "TLMP": "TECH",	# TechLampSilver / TechLampBrass
    "TRE1": "TRET",	# TreeStub
    "BAR1": "BARC",	# BarricadeColumn
    "SHT2": "MPUF",	# MaulerPuff
    "BARL": "BBAR",	# StrifeBurningBarrel
    "TRCH": "TRHL",	# SmallTorchLit
    "SHRD": "SHAR",	# glass shards
    "BLST": "MAUL",	# Mauler
    "LOGG": "LOGW",	# StickInWater
    "VASE": "VAZE",	# Pot / Pitcher
    "CNDL": "KNDL",	# Candle
    "POT1": "MPOT",	# MetalPot
    "SPID": "STLK",	# Stalker
}
FIXED_RENAME = GZDOOM_STRIFE_RENAMES


def make_rename(colliding, reserved, shareable=()):
    """Collision-free placeholders for `colliding`, avoiding every code in `reserved`.

    GZDoom's spelling (FIXED_RENAME) wins whenever it is free.  `shareable` names
    codes claimed only by our OWN internal pack (run/buddydoom.wad): buddydoom.wad
    already ships the Stalker under ZDoom's STLK, so re-using STLK here is the same
    art, not a clash -- but a GZDoom name that some real IWAD owns (Hexen has ARM3
    and ARM4, which GZDoom happily reuses because it only ever loads one game) must
    still be rejected, and falls through to the Y* scheme.

    Deterministic: stem 'Y'+code[:2], 4th char tries code[2], code[3], then 2..9/A..Z.
    Order-dependent, so the caller must keep the input order stable (it comes from
    the engine's own enum order, which only ever grows).
    """
    used, ren = set(reserved), {}
    soft = set(shareable)
    for code in colliding:
        want = FIXED_RENAME.get(code)
        if want and (want not in used or want in soft):
            used.add(want); ren[code] = want
            continue
        # No GZDoom spelling (or it is taken by a real IWAD): leave `code` out of the
        # map entirely.  The caller drops it from the pack -- see the note there.
    return ren


def main():
    here = Path(__file__).resolve().parent.parent
    id0, files = here / "run" / "ID0", here / "files"
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=str(id0/"strife1.wad"))
    ap.add_argument("--out", default=str(id0/"strifestuff.wad"))
    a = ap.parse_args()
    sp, op = Path(a.src), Path(a.out)
    if not sp.exists():
        print(f"ERROR: {sp} not found", file=sys.stderr); return 2

    codes, enums, other = read_engine_codes(files)
    sdata, sent = read_wad(sp)
    spal = pal(sdata, sent)

    present = set(nm[:4] for nm, fp, sz in sent if len(nm) > 4)
    wanted = [c for c in codes if c in present]
    missing = [c for c in codes if c not in present]

    # A code must be renamed if ANY other game claims it -- both the codes the
    # engine references (sprnames_builtin[]) and the codes physically present in
    # the IWADs on disk.  The second set matters: BLOD/GIBS/SHRD/WATR are unknown
    # to sprnames yet live in heretic.wad and hexen.wad, so keeping Strife's native
    # spelling would clobber their art whenever this pack is loaded there.
    foreign = reserved_codes([id0, here / "run"], exclude=("strife1.wad",))
    taken = set(other) | foreign
    colliding = [c for c in wanted if c in taken]
    # reserve everything anybody owns, so a placeholder can never shadow a
    # DOOM/Heretic/Hexen sprite, an internal pack's, or another Strife one.
    own = set()
    for cand in (id0 / "buddydoom.wad", here / "run" / "buddydoom.wad"):
        if cand.exists():
            own = sprite_codes_in_wad(cand); break
    ren = make_rename(colliding, taken | set(codes), shareable=own)
    # Anything that collides and has NO GZDoom spelling is dropped rather than given
    # an invented name: these are generic effects (gibs, fog, puffs, the player) the
    # host game already provides under the same code, so falling through to its lump
    # is both correct-looking and collision-proof.
    omitted = [c for c in colliding if c not in ren]
    wanted = [c for c in wanted if c not in omitted]
    code_out = {c: ren.get(c, c) for c in wanted}

    # ---- sprites: PNG in Strife's own palette (see tools/wadpng.py) ----
    out = [("S_START", b"")]
    n_spr, n_skip, n_ren = 0, 0, 0
    for nm, fp, sz in sent:
        code = nm[:4]
        if len(nm) <= 4 or code not in code_out:
            continue
        png = patch_to_png(sdata[fp:fp+sz], spal)
        if png is None:			# a same-prefixed non-patch lump (sound, etc.)
            n_skip += 1
            continue
        new = (code_out[code] + nm[4:])[:8]
        out.append((new, png))
        n_spr += 1
        n_ren += (code_out[code] != code)
    out.append(("S_END", b""))

    # ---- sounds: Strife's DS* lumps, minus the ones DOOM already owns ----
    doom_ds = set()
    for cand in ("DOOM2.WAD", "doom2.wad", "DOOM.WAD", "doom.wad",
                 "doom1.wad", "freedoom2.wad"):
        p = id0 / cand
        if p.exists():
            _d, e = read_wad(p)
            doom_ds |= {nm.upper() for nm, _f, _s in e if nm.upper().startswith("DS")}
            break
    n_snd, skipped_snd = 0, []
    seen = set()
    for nm, fp, sz in sent:
        if not nm.upper().startswith("DS") or nm in seen:
            continue
        raw = sdata[fp:fp+sz]
        if not is_dmx(raw):
            continue
        seen.add(nm)
        if nm.upper() in doom_ds:	# never replace a DOOM sound
            skipped_snd.append(nm); continue
        out.append((nm[:8], raw)); n_snd += 1

    # tag as a BuddyDoom-internal asset pack (launcher hides it from the PWAD list)
    out.insert(0, ("AISTUFF", b"BuddyDoom internal asset pack -- loaded by the game, not a user PWAD\n"))
    op.parent.mkdir(parents=True, exist_ok=True)
    write_wad(op, out)

    # ---- sidecars: human-readable map + the generated engine remap table ----
    mapfile = here / "tools" / "strife_sprite_map.txt"
    with open(mapfile, "w") as f:
        f.write("# Strife sprite code -> strifestuff.wad code.  Only codes that COLLIDE\n"
                "# with a DOOM/Heretic/Hexen sprite are renamed; the rest keep their\n"
                "# native name.  See docs/BUDDY_SPRITE_COLLISIONS.md.\n")
        for c in wanted:
            if code_out[c] != c:
                f.write(f"{c} -> {code_out[c]}\n")
        if missing:
            f.write("\n# not present in this strife1.wad (skipped): " + ", ".join(missing) + "\n")

    phfile = files / "strife_ph.inc"
    by_code = dict(zip(codes, enums))
    with open(phfile, "w") as f:
        f.write("// Auto-generated by tools/extract_strife.py -- do not edit by hand.\n"
                "// Strife sprite slots whose NATIVE 4-char code collides with a\n"
                "// DOOM/Heretic/Hexen sprite, and the collision-free placeholder that\n"
                "// strifestuff.wad ships them under.  files/strife.c applies these when\n"
                "// the game is NOT in strife_mode, so the pack never shadows the host\n"
                "// game's own art.  See docs/BUDDY_SPRITE_COLLISIONS.md.\n")
        for c in wanted:
            if code_out[c] != c:
                f.write(f'    {{ {by_code[c]}, "{code_out[c]}" }},\n')

    total = sum(len(d) for _n, d in out)
    print(f"extract_strife: wrote {op}")
    print(f"  sprite format: PNG (Strife palette preserved; grAb offsets)")
    print(f"  sprites: {n_spr}  ({len(wanted)} monster codes, {n_ren} lumps under a placeholder)")
    print(f"  renamed codes ({len(ren)}): " + ", ".join(f"{k}->{v}" for k, v in sorted(ren.items())))
    if omitted:
        print(f"  omitted, host game already owns the code ({len(omitted)}): "
              + ", ".join(sorted(omitted)))
    if n_skip:
        print(f"  non-patch lumps sharing a sprite prefix, skipped: {n_skip}")
    print(f"  sounds (DS*, verbatim): {n_snd}")
    if skipped_snd:
        print(f"  sounds skipped (name already owned by DOOM): {len(skipped_snd)} "
              f"-- {', '.join(sorted(skipped_snd))}")
    print(f"  total: {len(out)} lumps, {total/1024/1024:.1f} MB")
    print(f"  sprite map   -> {mapfile.relative_to(here)}")
    print(f"  engine table -> {phfile.relative_to(here)}")
    if missing:
        print(f"  NOTE: {len(missing)} codes absent in this IWAD: {', '.join(missing)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
