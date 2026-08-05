#!/usr/bin/env python3
"""
extract_hexen.py -- build run/ID0/hexenstuff.wad: the Hexen MONSTERS and WEAPONS
(sprites palette-converted to the DOOM palette + their sounds) so they can later be
ported into DOOM by a files/hexen.c, exactly like extract_heretic_monsters.py +
files/heretic.c did for Heretic.

The engine MERGES sprite namespaces (R_InitSpriteLumps), so this WAD carries ONLY its
own Hexen sprites in one S_START..S_END -- they get ADDED to the IWAD's sprites.

Collisions: Hexen 4-char sprite codes (e.g. PLAY, FX12) would clash with DOOM / the
Heretic stuff, so every extracted code is RENAMED into an "X.." (heXen) namespace.
The rename is deterministic and collision-free; the full map is printed and written to
tools/hexen_sprite_map.txt so the C port (hexen.c) can use the SAME codes.

Sounds are copied VERBATIM (Hexen lump names are descriptive and up to 8 chars, so they
can't take a "DS" prefix like Heretic's did); naming them for the engine's `ds%s` lookup
is a porting-step decision.  The selected names are printed for reference.

Usage:
    python3 tools/extract_hexen.py                       # src ID0/hexen.wad, base = a DOOM IWAD in ID0
    python3 tools/extract_hexen.py --base ID0/DOOM.WAD --out ID0/hexenstuff.wad
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wadpng import patch_to_png		# noqa: E402  (needs the path tweak above)
from wadcodes import reserved_codes	# noqa: E402

# ---------------------------------------------------------------------------
# Hexen MONSTER sprite codes (body + gibs/variants + their projectiles).
# ---------------------------------------------------------------------------
MONSTER_SPRITES = [
    "ETTN", "ETTB",                                         # ettin (+ mace ball)
    "CENT", "CTXD", "CTFX", "CTDP",                         # centaur / slaughtaur (+ shield fx)
    "DEMN", "DEMA", "DEMB", "DEMC", "DEMD", "DEME", "DMFX", # chaos serpent (green) + fx
    "DEM2", "DMBA", "DMBB", "DMBC", "DMBD", "DMBE", "D2FX", # chaos serpent (brown) + fx
    "WRTH", "WRT2", "WRBL",                                 # reiver (wraith) + bone shot
    "MNTR", "FX12", "FX13", "MNSM",                         # minotaur (dark servant) + mace/floor-fire/smoke
    "SSPT", "SSDV", "SSXD", "SSFX",                         # stalker (+ dive/death/fx)
    "BISH", "BPFX",                                         # dark bishop + crozier shot
    "DRAG", "DRFX",                                         # death wyvern (dragon) + fx
    "FDMN", "FDMB",                                         # afrit (fire demon) + fireball
    "ICEY", "ICPR", "ICWS", "ICEC",                         # wendigo + ice shards + frozen-corpse shatter
    "SORC", "SBMP", "SBS1", "SBS2", "SBS3", "SBS4",         # heresiarch (sorcerer) + spell balls
    "SBMB", "SBMG", "SBFX",
    "KORX", "ABAT",                                         # korax (final boss) + bats
    "PIGY",                                                 # pig (morph)
    "FDTH",                                                 # generic burning death
    # NOTE: APPEND-ONLY below this line.  make_rename() assigns names greedily in
    # list order, so inserting a code EARLIER can steal a name an existing code
    # already holds and silently invalidate every SPR_X* in files/info.c.  New
    # codes go at the end; diff tools/hexen_sprite_map.txt after every re-run.
    "PLAY", "CLER", "MAGE",                                 # fighter/cleric/mage class bosses
]

# ---------------------------------------------------------------------------
# Hexen WEAPON sprite codes: per-class HUD weapons (W*), pickups/pieces (A*), projectiles.
# ---------------------------------------------------------------------------
WEAPON_SPRITES = [
    # Fighter: gauntlets, Timon's axe, hammer (+thrown), Quietus
    "FPCH", "WFAX", "FAXE", "FSFX", "WFHM", "FHMR", "FHFX", "FSRD",
    # Cleric: mace, serpent staff, firestorm, wraithverge (holy spirits)
    "CMCE", "WCSS", "CSSF", "WCFM", "CFLM", "CFFX", "CHLY", "SPIR",
    # Mage: wand, arc of death (lightning), bloodscourge, frost shards
    "MWND", "WMLG", "MLNG", "MLFX", "MLF2", "MSTF", "MSP1", "MSP2", "CONE", "SHEX",
    # Mage HUD weapon sprites
    "WFR1", "WFR2", "WFR3", "WCH1", "WCH2", "WCH3", "WMS1", "WMS2", "WMS3", "WPIG", "WMCS",
    # 4th-weapon assembly pieces (the Guardian artifacts)
    "AFWP", "ACWP", "AMWP", "AGER", "AGR2", "AGR3", "AGR4",
]

# ---------------------------------------------------------------------------
# Hexen MONSTER sounds wired into files/hexen.c.  The engine looks a sound up as
# "ds<sfxname>" (i_sound.c), so each chosen Hexen DMX lump is copied under a NEW
# name "DS"+<short>, where <short> (<=6 chars, so DS+short <= the 8-byte lump cap)
# is the sfx tag in files/sounds.c / sounds.h (sfx_x_*).  Mapping below is:
#     short-sfx-name -> source Hexen lump (from hexen.wad SNDINFO / crispy-doom).
# Mirror these names exactly in files/sounds.{c,h} and files/hexen.c.
MONSTER_SOUNDS = {
    # Ettin (EttinSight/Active=cent2, Pain=cent1, Attack=ethit1, Death=cntdth1)
    "xetsit": "cent2",   "xetpai": "cent1",   "xetatk": "ethit1",  "xetdth": "cntdth1",
    # Centaur (taur1/taur2/taur4/centhit2/cntdth1)
    "xcesit": "taur1",   "xceact": "taur2",   "xcepai": "taur4",
    "xceatk": "centhit2","xcedth": "cntdth1",
    # Slaughtaur (centaur leader): leader attack = cntshld4
    "xslatk": "cntshld4",
    # Chaos Serpent / Demon (sbtsit5 sight+active, minact1 pain, dematk2 atk, sbtdth3 death)
    "xdesit": "sbtsit5", "xdepai": "minact1", "xdeatk": "dematk2", "xdedth": "sbtdth3",
    # Fire Demon / Afrit (active=fired5, pain=fired2, attack=spit6, death=fired3; FX hit=firedhit)
    "xfdact": "fired5",  "xfdpai": "fired2",  "xfdatk": "spit6",
    "xfddth": "fired3",  "xfdhit": "firedhit",
    # Wraith / Reiver (raith5a/raith3/raith4a/raith1b/rathdth2)
    "xwrsit": "raith5a", "xwract": "raith3",  "xwrpai": "raith4a",
    "xwratk": "raith1b", "xwrdth": "rathdth2",
    # Dark Bishop (sight=syab2d, active=stb1d, pain=bshpn1, attack=pop, death=bishdth1; FX=bshhit2)
    "xbisit": "syab2d",  "xbiact": "stb1d",   "xbipai": "bshpn1",
    "xbiatk": "pop",     "xbidth": "bishdth1","xbihit": "bshhit2",
    # Ice Guy / Wendigo (sight+active=frosty1, attack=frosty2; FX explode=shards1b; no pain/death sfx)
    "xicsit": "frosty1", "xicatk": "frosty2", "xichit": "shards1b",
    # Stalker / Serpent (sight=wtrcrt7, active=srfc3, pain=serppn1, attack=wtrswip, death=srpdth1; FX hit=glbhit4)
    "xstsit": "wtrcrt7", "xstact": "srfc3",   "xstpai": "serppn1",
    "xstatk": "wtrswip", "xstdth": "srpdth1", "xsthit": "glbhit4",
    # Death Wyvern / Dragon (sight+active=dragsit1, pain=dragpn2, attack=mage4, death=dragdie2; FX=mageball)
    "xdrsit": "dragsit1","xdrpai": "dragpn2", "xdratk": "mage4",
    "xdrdth": "dragdie2","xdrhit": "mageball",
}

# ---------------------------------------------------------------------------
# Hexen monster/weapon SOUND lump-name keywords (Hexen SFX names are descriptive,
# not <thing><action>).  A DMX lump whose name contains any of these is copied
# VERBATIM (kept for reference / future weapon work; the wired-up monster sounds
# go through MONSTER_SOUNDS above as DS* lumps).
# ---------------------------------------------------------------------------
SOUND_KEYWORDS = (
    # monsters
    "cent", "cnt", "eth", "taur", "minact", "mindth", "minpain", "minsit",
    "kor", "serp", "srp", "demat", "raith", "rath", "wrbl", "drag", "sor", "sbt",
    "bish", "bsh", "fired", "fdmn", "pig", "icedth", "icemv", "icebrk", "frosty",
    "icpr", "vamp", "bats", "glbh", "srfc", "mumpun", "squeal", "slurp", "shlurp",
    "bite", "impact",		# PigAttack (BITE4) + MaulatorMissileHit (IMPACT3)
    # weapons
    "axe", "ham", "hmhit", "punch", "sword", "holy", "spirt", "clhmm", "mageball",
    "wand", "blastr", "mage4", "cone3", "gnt", "wepele", "strike1", "strike3",
    # player-class pain / death / effort
    "fgt", "mgpain", "mgdth", "mggrunt", "mgxdth", "mgfall", "mghmm", "mgcdth",
    "clxdth", "plrdth", "plrpain", "plrburn", "plrcdth",
)


def read_wad(path):
    data = path.read_bytes()
    magic, n, off = struct.unpack("<4sII", data[:12])
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


def is_dmx(raw):
    return len(raw) >= 8 and raw[0] == 3 and raw[1] == 0


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


def make_rename(codes, reserved=()):
    """Deterministic, collision-free 4-char rename into the 'X' (heXen) namespace.
    XYZ stem = 'X'+code[:2]; 4th char tries code[2], code[3], then 2..9/A..Z.

    `reserved` seeds the used-set with codes other games already own, so a
    placeholder can never shadow their art (tools/wadcodes.py).  Seeding only ever
    removes candidates -- it cannot change an assignment unless that assignment was
    already colliding, which is exactly the case we want changed.  Diff
    tools/hexen_sprite_map.txt after any re-run to confirm nothing moved."""
    used, ren = set(reserved), {}
    for code in codes:
        stem = "X" + code[:2]
        for c in [code[2], code[3]] + list("23456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
            cand = (stem + c)[:4]
            if cand not in used:
                used.add(cand); ren[code] = cand; break
    return ren


def main():
    here = Path(__file__).resolve().parent.parent
    id0 = here / "run" / "ID0"
    ap = argparse.ArgumentParser()
    ap.add_argument("--src",  default=str(id0/"hexen.wad"))
    ap.add_argument("--base", default=None,
                    help="(deprecated, ignored) sprites are PNG now -- no palette conversion")
    ap.add_argument("--out",  default=str(id0/"hexenstuff.wad"))
    a = ap.parse_args()
    sp, op = Path(a.src), Path(a.out)
    if not sp.exists():
        print(f"ERROR: {sp} not found", file=sys.stderr); return 2

    hdata, hent = read_wad(sp)
    hexpal = pal(hdata, hent)			# Hexen's OWN palette -- see below

    # which selected sprite codes actually exist in hexen.wad's sprite namespace?
    present = set(nm[:4] for nm, fp, sz in hent if len(nm) > 4)
    wanted = [c for c in (MONSTER_SPRITES + WEAPON_SPRITES) if c in present]
    missing = [c for c in (MONSTER_SPRITES + WEAPON_SPRITES) if c not in present]
    ren = make_rename(wanted, reserved_codes([id0, here / "run"], exclude=("hexen.wad",)))

    # Sprites go in as PNG in HEXEN's palette -- NOT palette-converted to DOOM's.
    # The engine decodes each PNG at load (files/v_png.c V_PNGLumpDecode via
    # r_data.c R_InitSpriteLumps), nearest-matching into whatever IWAD is actually
    # running AND keeping a full-colour copy for the truecolor sprite path
    # (r_things.c R_BlitHDSprite).  Baking a DOOM-palette conversion in here threw
    # that colour away permanently; PNG also compresses smaller than the raw
    # column format, so the pack shrinks.  Offsets ride along in a grAb chunk.
    out = [("S_START", b"")]
    n_spr, n_skip = 0, 0
    for nm, fp, sz in hent:
        code = nm[:4]
        if code in ren and len(nm) > 4:
            png = patch_to_png(hdata[fp:fp+sz], hexpal)
            if png is None:			# not a patch (a same-prefixed sound etc.)
                n_skip += 1
                continue
            new = ren[code] + nm[4:]
            out.append((new[:8], png))
            n_spr += 1
    out.append(("S_END", b""))

    # WIRED-UP monster sounds: copy each chosen Hexen DMX lump under "DS"+<short>
    # so the engine's "ds%s" lookup (i_sound.c) finds it for sfx_x_* (files/sounds.c).
    by_name = {nm.upper(): (fp, sz) for nm, fp, sz in hent}
    n_ds = 0
    ds_missing = []
    for short, srclump in MONSTER_SOUNDS.items():
        key = srclump.upper()
        if key not in by_name:
            ds_missing.append(srclump); continue
        fp, sz = by_name[key]
        raw = hdata[fp:fp+sz]
        if not is_dmx(raw):
            ds_missing.append(srclump + "(not-dmx)"); continue
        out.append(("DS" + short.upper()[:6], raw)); n_ds += 1

    # monster / weapon sounds (DMX), copied verbatim (reference, not wired up).
    n_snd = 0
    snd_names = []
    seen = set()
    for nm, fp, sz in hent:
        raw = hdata[fp:fp+sz]
        if nm not in seen and is_dmx(raw) and any(k in nm.lower() for k in SOUND_KEYWORDS):
            out.append((nm[:8], raw)); n_snd += 1; snd_names.append(nm); seen.add(nm)

    # tag as an BuddyDoom-internal asset pack so the launcher hides it from the user PWAD list
    out.insert(0, ("AISTUFF", b"BuddyDoom internal asset pack -- loaded by the game, not a user PWAD\n"))
    op.parent.mkdir(parents=True, exist_ok=True)
    write_wad(op, out)

    # sidecar rename map for the future hexen.c port
    mapfile = here / "tools" / "hexen_sprite_map.txt"
    with open(mapfile, "w") as f:
        f.write("# Hexen sprite code -> hexenstuff.wad code (use these in files/hexen.c)\n")
        cat = {c: "monster" for c in MONSTER_SPRITES}
        cat.update({c: "weapon" for c in WEAPON_SPRITES})
        for c in wanted:
            f.write(f"{c} -> {ren[c]}   ({cat.get(c,'?')})\n")
        if missing:
            f.write("\n# not present in this hexen.wad (skipped): " + ", ".join(missing) + "\n")

    total = sum(len(d) for _n, d in out)
    print(f"extract_hexen: wrote {op}")
    print(f"  sprite format: PNG (Hexen palette preserved; grAb offsets)")
    print(f"  monster/weapon sprites (renamed): {n_spr}  ({len(wanted)} codes)")
    if n_skip:
        print(f"  non-patch lumps sharing a sprite prefix, skipped: {n_skip}")
    print(f"  WIRED monster sounds (DS* lumps for sfx_x_*): {n_ds}")
    if ds_missing:
        print(f"    WARNING: missing source lumps for DS sounds: {', '.join(ds_missing)}")
    print(f"  monster/weapon sounds (verbatim, reference): {n_snd}")
    print(f"  total: {len(out)} lumps, {total/1024/1024:.1f} MB")
    print(f"  sprite rename map -> {mapfile.relative_to(here)}")
    if missing:
        print(f"  NOTE: {len(missing)} selected codes absent in this IWAD: {', '.join(missing)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
