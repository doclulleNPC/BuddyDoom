# Episode 1 — Texture & Flat Usage Overview

A breakdown of every wall texture and floor/ceiling flat referenced by the 9
maps of DOOM Episode 1 (E1M1 .. E1M9) in the **Ultimate Doom** IWAD, with usage
counts grouped by category. Cross-references the IWAD's `TEXTURE1` + `TEXTURE2`
composite-texture definitions to confirm every wall name is a real composite
texture (vs. a missing reference).

Source IWAD: `C:\Program Files (x86)\Steam\steamapps\common\Ultimate Doom\base\DOOM.WAD`
(12,408,292 bytes, 2,306 lumps, 1995 release, sig=IWAD).

## Summary

| Metric | Value |
|---|---:|
| Maps in E1 | 9 (E1M1 .. E1M9) |
| Total sectors (all 9 maps) | 1388 |
| Total linedef sides (all 9 maps) | 9785 |
| Unique wall textures used | 113 of 287 composites available in the IWAD |
| Unique flats used | 51 |
| Total wall-texture uses | 9,740 |
| Total flat uses | 2,776 |

## Wall textures — categorized

Wall textures in DOOM are **composite textures** defined in `TEXTURE1` + `TEXTURE2`
(built from one or more patches in `PNAMES`). All 113 textures used in E1 are
present in the IWAD as composite definitions — none are referenced-but-missing.

Counts are summed across **all three slots** of every linedef side (upper, lower, mid),
counting each slot independently. A 2-sided linedef with a `BROWN1` upper, a `BROWN1`
lower and a `-` mid contributes 2 to BROWN1's count.

### Doors  (15 textures, 545 uses)

| Texture | Total uses | As upper | As lower | As mid | Composite? |
|---|---:|---:|---:|---:|:---:|
| `DOORTRAK` | 150 | 0 | 0 | 150 | yes |
| `DOORSTOP` | 144 | 0 | 0 | 144 | yes |
| `BIGDOOR2` | 55 | 48 | 7 | 0 | yes |
| `EXITDOOR` | 43 | 15 | 8 | 20 | yes |
| `BIGDOOR4` | 38 | 30 | 8 | 0 | yes |
| `DOORYEL` | 35 | 0 | 0 | 35 | yes |
| `DOORBLU` | 26 | 0 | 0 | 26 | yes |
| `DOORRED` | 20 | 0 | 0 | 20 | yes |
| `DOOR3` | 13 | 4 | 1 | 8 | yes |
| `doorstop` | 6 | 0 | 0 | 6 | yes |
| `BIGDOOR1` | 6 | 4 | 2 | 0 | yes |
| `doortrak` | 4 | 0 | 0 | 4 | yes |
| `DOOR1` | 2 | 2 | 0 | 0 | yes |
| `Bigdoor2` | 2 | 2 | 0 | 0 | yes |
| `ExitDoor` | 1 | 1 | 0 | 0 | yes |

### Switches  (15 textures, 50 uses)

| Texture | Total uses | As upper | As lower | As mid | Composite? |
|---|---:|---:|---:|---:|:---:|
| `SW1COMP` | 13 | 3 | 3 | 7 | yes |
| `SW1SLAD` | 7 | 3 | 2 | 2 | yes |
| `SW1BRNGN` | 6 | 4 | 1 | 1 | yes |
| `SW1STON1` | 4 | 0 | 1 | 3 | yes |
| `SW1BRCOM` | 4 | 0 | 0 | 4 | yes |
| `SW1DIRT` | 3 | 0 | 0 | 3 | yes |
| `SW1STONE` | 3 | 0 | 2 | 1 | yes |
| `SW2BROWN` | 2 | 0 | 0 | 2 | yes |
| `SW1METAL` | 2 | 0 | 0 | 2 | yes |
| `SW1STRTN` | 1 | 0 | 0 | 1 | yes |
| `SW1PIPE` | 1 | 0 | 1 | 0 | yes |
| `SW1BRN2` | 1 | 1 | 0 | 0 | yes |
| `SW1BRN1` | 1 | 0 | 0 | 1 | yes |
| `SW1COMM` | 1 | 0 | 0 | 1 | yes |
| `SW1STON2` | 1 | 0 | 1 | 0 | yes |

### Tech / industrial  (39 textures, 2,591 uses)

| Texture | Total uses | As upper | As lower | As mid | Composite? |
|---|---:|---:|---:|---:|:---:|
| `STARTAN3` | 509 | 110 | 56 | 343 | yes |
| `COMPTALL` | 351 | 50 | 64 | 237 | yes |
| `STEP2` | 246 | 41 | 205 | 0 | yes |
| `STARTAN2` | 218 | 64 | 16 | 138 | yes |
| `TEKWALL1` | 173 | 92 | 31 | 50 | yes |
| `STEP6` | 141 | 1 | 140 | 0 | yes |
| `GRAY5` | 109 | 74 | 29 | 6 | yes |
| `GRAYTALL` | 107 | 73 | 32 | 2 | yes |
| `TEKWALL5` | 86 | 13 | 11 | 62 | yes |
| `COMPUTE2` | 84 | 53 | 21 | 10 | yes |
| `COMPTILE` | 59 | 21 | 11 | 27 | yes |
| `STEP4` | 59 | 0 | 59 | 0 | yes |
| `TEKWALL4` | 58 | 21 | 13 | 24 | yes |
| `LITE3` | 46 | 0 | 0 | 46 | yes |
| `STEP1` | 46 | 0 | 46 | 0 | yes |
| `STEP5` | 46 | 0 | 46 | 0 | yes |
| `LITE4` | 38 | 0 | 0 | 38 | yes |
| `GRAY4` | 31 | 29 | 2 | 0 | yes |
| `COMPSPAN` | 25 | 2 | 11 | 12 | yes |
| `LITEBLU3` | 22 | 0 | 0 | 22 | yes |
| `STEP3` | 20 | 0 | 20 | 0 | yes |
| `PIPE2` | 20 | 8 | 2 | 10 | yes |
| `TEKWALL2` | 20 | 11 | 0 | 9 | yes |
| `LITE5` | 14 | 0 | 0 | 14 | yes |
| `LITE2` | 9 | 2 | 2 | 5 | yes |
| `LITEBLU4` | 8 | 0 | 0 | 8 | yes |
| `GRAY7` | 7 | 4 | 3 | 0 | yes |
| `COMPUTE3` | 6 | 2 | 2 | 2 | yes |
| `TEKWALL3` | 6 | 0 | 0 | 6 | yes |
| `COMPSTA1` | 4 | 0 | 0 | 4 | yes |
| `COMPUTE1` | 4 | 0 | 0 | 4 | yes |
| `compspan` | 4 | 0 | 0 | 4 | yes |
| `LITEBLU2` | 4 | 0 | 0 | 4 | yes |
| `COMPSTA2` | 3 | 0 | 0 | 3 | yes |
| `COMP2` | 2 | 0 | 0 | 2 | yes |
| `Compute2` | 2 | 0 | 0 | 2 | yes |
| `LITEBLU1` | 2 | 0 | 0 | 2 | yes |
| `compute1` | 1 | 1 | 0 | 0 | yes |
| `step4` | 1 | 0 | 1 | 0 | yes |

### Stone / metal  (19 textures, 4,686 uses)

| Texture | Total uses | As upper | As lower | As mid | Composite? |
|---|---:|---:|---:|---:|:---:|
| `BROWN1` | 1,162 | 255 | 273 | 634 | yes |
| `BROWN96` | 896 | 270 | 212 | 414 | yes |
| `BROWNGRN` | 888 | 184 | 137 | 567 | yes |
| `STONE` | 489 | 137 | 156 | 196 | yes |
| `SLADWALL` | 459 | 108 | 76 | 275 | yes |
| `STONE2` | 318 | 56 | 24 | 238 | yes |
| `BROWN144` | 153 | 28 | 31 | 94 | yes |
| `STONE3` | 137 | 19 | 87 | 31 | yes |
| `BROWNPIP` | 59 | 14 | 14 | 31 | yes |
| `BROWNHUG` | 49 | 11 | 19 | 19 | yes |
| `REDWALL1` | 25 | 3 | 22 | 0 | yes |
| `SHAWN2` | 15 | 3 | 12 | 0 | yes |
| `shawn2` | 13 | 1 | 12 | 0 | yes |
| `stone` | 8 | 0 | 0 | 8 | yes |
| `STONPOIS` | 4 | 0 | 0 | 4 | yes |
| `SLADRIP2` | 3 | 0 | 0 | 3 | yes |
| `SLADPOIS` | 3 | 0 | 0 | 3 | yes |
| `sladwall` | 3 | 2 | 0 | 1 | yes |
| `SLADRIP3` | 2 | 0 | 2 | 0 | yes |

### Other  (25 textures, 1,868 uses)

| Texture | Total uses | As upper | As lower | As mid | Composite? |
|---|---:|---:|---:|---:|:---:|
| `STARG3` | 492 | 114 | 42 | 336 | yes |
| `SUPPORT2` | 409 | 1 | 29 | 379 | yes |
| `METAL1` | 217 | 73 | 34 | 110 | yes |
| `STARG1` | 142 | 15 | 8 | 119 | yes |
| `STARTAN1` | 129 | 32 | 20 | 77 | yes |
| `STARGR1` | 121 | 6 | 17 | 98 | yes |
| `EXITSIGN` | 60 | 48 | 12 | 0 | yes |
| `NUKE24` | 53 | 0 | 53 | 0 | yes |
| `AASTINKY` | 39 | 18 | 21 | 0 | yes |
| `NUKESLAD` | 39 | 0 | 34 | 5 | yes |
| `BRNBIGC` | 30 | 0 | 0 | 30 | yes |
| `PLAT1` | 24 | 2 | 22 | 0 | yes |
| `PLANET1` | 16 | 16 | 0 | 0 | yes |
| `NUKEDGE1` | 16 | 0 | 7 | 9 | yes |
| `support2` | 16 | 0 | 0 | 16 | yes |
| `BRNBIGL` | 12 | 0 | 0 | 12 | yes |
| `BRNBIGR` | 12 | 0 | 0 | 12 | yes |
| `BRNSMALC` | 12 | 0 | 0 | 12 | yes |
| `BRNSMAL1` | 10 | 0 | 0 | 10 | yes |
| `BRNSMAL2` | 10 | 0 | 0 | 10 | yes |
| `BRNSMALL` | 2 | 0 | 0 | 2 | yes |
| `BRNSMALR` | 2 | 0 | 0 | 2 | yes |
| `BRNPOIS2` | 2 | 0 | 0 | 2 | yes |
| `BRNPOIS` | 2 | 0 | 0 | 2 | yes |
| `plat1` | 1 | 0 | 1 | 0 | yes |

## Top wall textures (overall)

| # | Texture | Uses | Category |
|---:|---|---:|---|
| 1 | `BROWN1` | 1,162 | Stone / metal |
| 2 | `BROWN96` | 896 | Stone / metal |
| 3 | `BROWNGRN` | 888 | Stone / metal |
| 4 | `STARTAN3` | 509 | Tech / industrial |
| 5 | `STARG3` | 492 | Other |
| 6 | `STONE` | 489 | Stone / metal |
| 7 | `SLADWALL` | 459 | Stone / metal |
| 8 | `SUPPORT2` | 409 | Other |
| 9 | `COMPTALL` | 351 | Tech / industrial |
| 10 | `STONE2` | 318 | Stone / metal |
| 11 | `STEP2` | 246 | Tech / industrial |
| 12 | `STARTAN2` | 218 | Tech / industrial |
| 13 | `METAL1` | 217 | Other |
| 14 | `TEKWALL1` | 173 | Tech / industrial |
| 15 | `BROWN144` | 153 | Stone / metal |
| 16 | `DOORTRAK` | 150 | Doors |
| 17 | `DOORSTOP` | 144 | Doors |
| 18 | `STARG1` | 142 | Other |
| 19 | `STEP6` | 141 | Tech / industrial |
| 20 | `STONE3` | 137 | Stone / metal |
| 21 | `STARTAN1` | 129 | Other |
| 22 | `STARGR1` | 121 | Other |
| 23 | `GRAY5` | 109 | Tech / industrial |
| 24 | `GRAYTALL` | 107 | Tech / industrial |
| 25 | `TEKWALL5` | 86 | Tech / industrial |
| 26 | `COMPUTE2` | 84 | Tech / industrial |
| 27 | `EXITSIGN` | 60 | Other |
| 28 | `COMPTILE` | 59 | Tech / industrial |
| 29 | `STEP4` | 59 | Tech / industrial |
| 30 | `BROWNPIP` | 59 | Stone / metal |

## Floor & ceiling flats — categorized

Flats in DOOM are stored as raw 4096-byte lumps (64x64 indexed graphics). The
map's `SECTORS` reference a floor flat and a ceiling flat per sector. Below,
`uses` counts each reference (a sector with `FLOOR0_1` floor and `CEIL5_1`
ceiling contributes 1 to each).

### Floor  (28 flats, 1,706 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `FLOOR5_1` | 310 | yes |
| `FLOOR4_8` | 244 | yes |
| `FLAT5_4` | 233 | yes |
| `FLAT20` | 154 | yes |
| `FLOOR7_2` | 145 | yes |
| `FLOOR7_1` | 139 | yes |
| `FLAT14` | 81 | yes |
| `FLAT5_5` | 64 | yes |
| `FLOOR5_3` | 55 | yes |
| `FLOOR3_3` | 51 | yes |
| `FLOOR5_2` | 32 | yes |
| `FLOOR4_5` | 23 | yes |
| `FLOOR4_1` | 22 | yes |
| `FLOOR5_4` | 22 | yes |
| `FLAT5` | 21 | yes |
| `FLOOR6_2` | 20 | yes |
| `FLOOR0_3` | 19 | yes |
| `FLAT23` | 18 | yes |
| `FLAT22` | 9 | yes |
| `FLOOR0_6` | 9 | yes |
| `FLOOR1_1` | 8 | yes |
| `FLAT10` | 7 | yes |
| `FLOOR4_6` | 6 | yes |
| `FLOOR6_1` | 5 | yes |
| `FLAT18` | 4 | yes |
| `FLOOR0_1` | 2 | yes |
| `FLOOR1_7` | 2 | yes |
| `FLAT2` | 1 | yes |

### Ceiling  (7 flats, 637 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `CEIL3_5` | 414 | yes |
| `CEIL5_1` | 93 | yes |
| `CEIL5_2` | 86 | yes |
| `CEIL3_1` | 37 | yes |
| `CEIL4_3` | 4 | yes |
| `CEIL4_2` | 2 | yes |
| `CEIL3_2` | 1 | yes |

### Sky  (1 flat, 124 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `F_SKY1` | 124 | yes |

### Liquid (nukage/slime)  (1 flat, 88 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `NUKAGE3` | 88 | yes |

### Floor (demon)  (4 flats, 4 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `DEM1_3` | 1 | yes |
| `DEM1_4` | 1 | yes |
| `DEM1_1` | 1 | yes |
| `DEM1_2` | 1 | yes |

### Floor (metal)  (1 flat, 50 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `MFLR8_1` | 50 | yes |

### Ceiling (light)  (4 flats, 121 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `TLITE6_5` | 52 | yes |
| `TLITE6_6` | 38 | yes |
| `TLITE6_1` | 27 | yes |
| `TLITE6_4` | 4 | yes |

### Other  (5 flats, 46 uses)

| Flat | Uses | Lump present? |
|---|---:|:---:|
| `STEP2` | 26 | yes |
| `STEP1` | 9 | yes |
| `CONS1_1` | 8 | yes |
| `CONS1_7` | 2 | yes |
| `CONS1_5` | 1 | yes |

## Top flats (overall)

| # | Flat | Uses | Category |
|---:|---|---:|---|
| 1 | `CEIL3_5` | 414 | Ceiling |
| 2 | `FLOOR5_1` | 310 | Floor |
| 3 | `FLOOR4_8` | 244 | Floor |
| 4 | `FLAT5_4` | 233 | Floor |
| 5 | `FLAT20` | 154 | Floor |
| 6 | `FLOOR7_2` | 145 | Floor |
| 7 | `FLOOR7_1` | 139 | Floor |
| 8 | `F_SKY1` | 124 | Sky |
| 9 | `CEIL5_1` | 93 | Ceiling |
| 10 | `NUKAGE3` | 88 | Liquid (nukage/slime) |
| 11 | `CEIL5_2` | 86 | Ceiling |
| 12 | `FLAT14` | 81 | Floor |
| 13 | `FLAT5_5` | 64 | Floor |
| 14 | `FLOOR5_3` | 55 | Floor |
| 15 | `TLITE6_5` | 52 | Ceiling (light) |
| 16 | `FLOOR3_3` | 51 | Floor |
| 17 | `MFLR8_1` | 50 | Floor (metal) |
| 18 | `TLITE6_6` | 38 | Ceiling (light) |
| 19 | `CEIL3_1` | 37 | Ceiling |
| 20 | `FLOOR5_2` | 32 | Floor |
| 21 | `TLITE6_1` | 27 | Ceiling (light) |
| 22 | `STEP2` | 26 | Other |
| 23 | `FLOOR4_5` | 23 | Floor |
| 24 | `FLOOR4_1` | 22 | Floor |
| 25 | `FLOOR5_4` | 22 | Floor |
| 26 | `FLAT5` | 21 | Floor |
| 27 | `FLOOR6_2` | 20 | Floor |
| 28 | `FLOOR0_3` | 19 | Floor |
| 29 | `FLAT23` | 18 | Floor |
| 30 | `FLAT22` | 9 | Floor |

## Per-map stats

| Map | Sectors | Sides | Upper used | Upper `-` | Lower used | Lower `-` | Mid used | Mid `-` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| E1M1 | 88 | 666 | 156 | 510 | 158 | 508 | 319 | 347 |
| E1M2 | 200 | 1323 | 310 | 1013 | 288 | 1035 | 743 | 580 |
| E1M3 | 177 | 1326 | 317 | 1009 | 289 | 1037 | 754 | 572 |
| E1M4 | 139 | 1054 | 235 | 819 | 265 | 789 | 612 | 442 |
| E1M5 | 143 | 1053 | 241 | 812 | 206 | 847 | 597 | 456 |
| E1M6 | 250 | 1727 | 371 | 1356 | 357 | 1370 | 1011 | 716 |
| E1M7 | 170 | 1223 | 219 | 1004 | 226 | 997 | 693 | 530 |
| E1M8 | 74 | 511 | 118 | 393 | 258 | 253 | 155 | 356 |
| E1M9 | 147 | 902 | 238 | 664 | 190 | 712 | 414 | 488 |

## Notes

- "`-`" is the standard DOOM "no texture" marker (single dash). A 1-sided linedef
  (no back side) always has `-` in its mid-texture slot; 2-sided door/window
  linedefs use the three slots to define upper, lower, and middle wall regions.
- Wall uses total = sum of all upper, lower, mid references across every linedef
  side in every E1 map. A single linedef side can use the same texture in 0..3
  slots, so the grand total exceeds the number of linedef sides.
- The IWAD contains **287 composite wall textures** (125 in TEXTURE1 + 162 in
  TEXTURE2). E1 uses 113 of them — a much wider variety than I first reported
  (a previous version of this analysis had a map-segmentation bug that double-
  counted the same data into every map and missed textures). The brown-and-bronze
  palette dominates but the level designers reached into nearly 40% of the
  IWAD's available composite textures to keep E1 visually interesting.
