#!/usr/bin/env python3
"""
wadcodes.py -- shared helper for the *stuff.wad extractors: what 4-char sprite
codes are ALREADY taken, so a borrowed pack never shadows the host game's art.

A *stuff.wad is loaded on top of some other game's IWAD, and BuddyDoom merges every
S_START..S_END region into one sprite namespace (files/r_data.c R_InitSpriteLumps).
A pack lump whose 4-char code matches a host sprite REPLACES it -- ship Strife's
SPID* under its native name and the Spider Mastermind becomes a Stalker.

Checking only the engine's own sprnames_builtin[] is not enough: it lists the codes
the engine *references*, not every code physically present in the IWADs.  Strife's
BLOD/GIBS/SHRD/WATR are unknown to sprnames yet sit in heretic.wad and hexen.wad,
so a pack that kept those names would clobber Heretic/Hexen art the moment someone
loaded it there.  reserved_codes() therefore unions BOTH sources.
"""

import struct
from pathlib import Path

__all__ = ["sprite_codes_in_wad", "reserved_codes"]

# Every IWAD / internal pack a *stuff.wad could plausibly be loaded alongside.
_HOST_WADS = (
    "DOOM.WAD", "doom.wad", "DOOM2.WAD", "doom2.wad", "doom1.wad",
    "PLUTONIA.WAD", "plutonia.wad", "TNT.WAD", "tnt.wad",
    "freedoom1.wad", "freedoom2.wad", "freedm.wad",
    "heretic.wad", "hexen.wad", "strife1.wad",
    "id24res.wad", "buddydoom.wad",
)


def sprite_codes_in_wad(path):
    """The set of 4-char sprite codes inside a WAD's S_START..S_END region(s)."""
    data = Path(path).read_bytes()
    if len(data) < 12:
        return set()
    _magic, n, off = struct.unpack("<4sII", data[:12])
    codes, inside = set(), False
    for i in range(n):
        nm = data[off+i*16+8: off+i*16+16].split(b"\x00")[0].decode("latin1").upper()
        if nm in ("S_START", "SS_START"):
            inside = True
        elif nm in ("S_END", "SS_END"):
            inside = False
        elif inside and len(nm) > 4:
            codes.add(nm[:4])
    return codes


def reserved_codes(search_dirs, exclude=()):
    """Union of the sprite codes present in every host WAD found under `search_dirs`.

    `exclude` names the pack's OWN source WAD (e.g. "hexen.wad" when building
    hexenstuff.wad) -- its codes are the ones we are renaming FROM, so reserving
    them would make every rename collide with itself.
    """
    ex = {e.upper() for e in exclude}
    taken = set()
    for d in search_dirs:
        d = Path(d)
        if not d.is_dir():
            continue
        for name in _HOST_WADS:
            if name.upper() in ex:
                continue
            p = d / name
            if p.exists():
                taken |= sprite_codes_in_wad(p)
    return taken
