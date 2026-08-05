// wadcodes.h -- shared *stuff.wad sprite-name policy (C mirror of tools/wadcodes.py).
//
// A *stuff.wad is loaded on top of another game's IWAD and BuddyDoom merges every
// S_START..S_END region into ONE sprite namespace (files/r_data.c), so a pack lump
// whose 4-char code matches a host sprite REPLACES it -- ship Strife's SPID* under
// its native name and the Spider Mastermind becomes a Stalker.
//
// GZDoom solves the same problem in RenameSprites() (../gzdoom/src/d_main.cpp):
// it renames the colliding lumps of the IWAD at load, per game, from a fixed table.
// We do the same, except our namespace holds all four games at once, so a name
// GZDoom reuses freely may still be taken here (Hexen owns ARM3/ARM4, which
// GZDoom's Strife table wants) -- those fall through to a deterministic scheme.
#ifndef WADCODES_H
#define WADCODES_H

#define WC_MAXCODES 2048	// every host IWAD's codes at once (DOOM+Heretic+Hexen+Strife+packs)

typedef struct {
    char v[WC_MAXCODES][5];
    int  n;
} codeset_t;

int  wc_has(const codeset_t* s, const char* code);
void wc_add(codeset_t* s, const char* code);

// Collect the 4-char sprite codes inside a WAD's S_START..S_END region(s).
void wc_codes_in_wad(const char* path, codeset_t* out);

// Union the sprite codes of every host IWAD/internal pack found in `dir`, skipping
// `exclude` (the pack's OWN source wad -- reserving its codes would make every
// rename collide with itself).  Call once per directory you want scanned.
void wc_reserve_from_dir(const char* dir, const char* exclude, codeset_t* out);

// GZDoom's per-game rename tables (verbatim from RenameSprites()).  Returns the
// preferred new code for `code`, or NULL if that game has no opinion.
const char* wc_gzdoom_rename(const char* game, const char* code);

// Pick a collision-free replacement for `code`:
//   * GZDoom's spelling when it is free (or only claimed by `shareable`, our own
//     internal pack shipping the SAME art under ZDoom's name),
//   * else deterministic: stem `prefix`+code[0..1], 4th char tries code[2],
//     code[3], then 2..9/A..Z -- UNLESS `prefix` is 0, which means "GZDoom's
//     spelling or nothing" and returns 0 so the caller can omit the sprite instead.
// The chosen name is added to `used`.  Returns 1 on success, 0 if nothing is free.
int wc_pick_rename(const char* game, const char* code, char prefix,
                   codeset_t* used, const codeset_t* shareable, char out[5]);

#endif
