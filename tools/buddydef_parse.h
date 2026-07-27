// buddydef_parse.h -- BUDDYDEF text parser/serializer for the editor tool.
//
// Mirrors files/p_buddydef.c (the engine's loader).  Field names, defaults and
// aliases MUST stay in sync so files round-trip cleanly.  The engine's parser is
// more forgiving (no quotes, commas, optional braces); we keep the same latitude
// so an EDL text authored in this tool is byte-for-byte reloadable by the engine.

#ifndef BUDDYDEF_PARSE_H
#define BUDDYDEF_PARSE_H

#include <stddef.h>
#include "buddydef_wad.h"

// A single edit-able buddy definition.  All fields are stored as their raw
// strings (so the editor can show "999" / "Frank N. Stein" verbatim), except for
// the few integer fields we want inc/dec UI for.  The editor updates .set_* to
// mark fields the user edited; the serializer emits the unset fields as their
// defaults so the file is small and human-readable.
typedef struct {
    // Identity
    char    name[40];
    char    desc[160];
    char    special[96];          // free-text "special abilities" blurb
    char    ability[24];          // named: none | drone | poisoncloud | turret
    char    color[24];            // player-colour name, "" = none

    // Sprite
    char    sprite[8];            // 4-char base, padded to 8

    // Stats
    int     health;
    int     speed;
    int     radius;
    int     height;
    int     mass;
    int     painchance;
    int     reactiontime;

    // Combat
    char    attack[24];           // name from the engine's table

    // Sounds
    char    seesnd[16];
    char    painsnd[16];
    char    deathsnd[16];
    char    activesnd[16];

    // Map placement
    int     ednum;                // -1 = not placable

    // "Was this set?" mask so the serializer can omit defaults (the engine's
    // parser looks at the string, so emission with the default value is fine
    // even when the user didn't change it -- but keeping the file short is
    // nicer for hand-editing).
    unsigned set_name      : 1;
    unsigned set_desc      : 1;
    unsigned set_special   : 1;
    unsigned set_ability   : 1;
    unsigned set_color     : 1;
    unsigned set_sprite    : 1;
    unsigned set_health    : 1;
    unsigned set_speed     : 1;
    unsigned set_radius    : 1;
    unsigned set_height    : 1;
    unsigned set_mass      : 1;
    unsigned set_painchance: 1;
    unsigned set_reactiontime : 1;
    unsigned set_attack    : 1;
    unsigned set_seesnd    : 1;
    unsigned set_painsnd   : 1;
    unsigned set_deathsnd  : 1;
    unsigned set_activesnd : 1;
    unsigned set_ednum     : 1;
} buddydef_entry_t;

// Reset a single entry to the engine's `Buddy_Defaults` (p_buddydef.c).  Used
// for "New buddy" and to seed any field that gets explicitly cleared.
void buddydef_defaults(buddydef_entry_t* b);

// Parse one or more buddy { ... } blocks out of `text` (length `len`) into a
// freshly allocated, NUL-terminated array at *out.  Returns the count.
//
// *out is heap-allocated; free with free().  Each entry also has its "set_*"
// bits set for the keys actually present in the source -- if you call
// buddydef_serialize() on the returned array, you'll get the original text back
// (modulo whitespace, comment placement, and quote-vs-bare decisions).
int buddydef_parse(const char* text, int len, buddydef_entry_t** out);

// Serialize one entry back to its BUDDYDEF text block (a single `buddy { ... }`).
// Caller frees the returned string.  Behaves like the engine's parser: case-
// insensitive keys, optional quotes, optional commas, NoComments.  Matches the
// field set & defaults in p_buddydef.c.
char* buddydef_serialize(const buddydef_entry_t* b);

// Serialize an array of entries as a single BUDDYDEF lump (one `buddy { ... }`
// after another, with blank lines between).  Caller frees the returned string.
char* buddydef_serialize_all(const buddydef_entry_t* b, int n);

// Convenience: read the BUDDYDEF lump from a loaded WAD and parse it.  Returns
// 0 on success, -1 if no BUDDYDEF lump exists.  Implementation in
// buddydef_parse.c, which `#include`s "buddydef_wad.h" for the typedef.
int buddydef_load_from_wad(buddydef_wad_t* w, buddydef_entry_t** out, int* n);

#endif
