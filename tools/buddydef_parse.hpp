// buddydef_parse.hpp -- BUDDYDEF (de)serialisation for the MyBuddy editor.
//
// Mirrors files/p_buddydef.c, the engine's loader: key names, aliases, defaults, brace
// handling, quoting and trailing-comment stripping all match, so a file written here is
// byte-for-byte reloadable by the engine.  When the engine's parser changes, change
// this with it.
//
// A buddy is player 2 -- a record supplies properties, never an actor.  Some keys are
// therefore parsed but no longer do anything in game (`attack`, `ednum`); they stay in
// the format so existing lumps keep loading.  See docs/BUDDY_MODDING.md.

#ifndef BUDDYDEF_PARSE_HPP
#define BUDDYDEF_PARSE_HPP

#include <bitset>
#include <string>
#include <vector>

#include "buddydef_wad.hpp"

namespace buddy {

// Every editable key, in one enum.  Used both as the "was this declared?" bit and as
// the identity of a row in the editor -- the C version matched on the UI's label
// strings, which silently broke whenever a label was reworded.
enum class Key {
    Name, Desc, Special, Ability, Color, Sprite,
    Health, Speed, Radius, Height, Mass, PainChance, ReactionTime,
    Attack, SeeSound, PainSound, DeathSound, ActiveSound, Ednum,
    COUNT
};

struct Buddy {
    // Identity
    std::string name    = "Buddy";
    std::string desc;
    std::string special;                 // free-text blurb for the select screen
    std::string ability = "none";        // none | drone | poisoncloud | turret
    std::string color;                   // player-colour name, empty = none declared

    // Appearance
    std::string sprite  = "PLAY";        // 4-char base

    // Stats (the engine's Buddy_Defaults seeds these; see the note in the .cpp)
    int health       = 200;
    int speed        = 8;
    int radius       = 20;
    int height       = 56;
    int mass         = 100;
    int painchance   = 120;
    int reactiontime = 8;

    // Retired: parsed, ignored by the game.
    std::string attack = "melee";
    int         ednum  = -1;

    // Sounds (lump names, empty = silent)
    std::string seesnd, painsnd, deathsnd, activesnd;

    // Which keys the source actually declared / the user actually edited.  The
    // serialiser only emits those, which keeps a hand-edited file small, and it is
    // what "not declared" vs "declared as 0" will hang off once the engine's parser
    // grows the same distinction (docs/BUDDYDEF.md section 5).
    std::bitset<static_cast<size_t>(Key::COUNT)> declared;

    bool has(Key k) const  { return declared.test(static_cast<size_t>(k)); }
    void set(Key k)        { declared.set(static_cast<size_t>(k)); }
};

// Parse every `buddy { ... }` block in `text`.
std::vector<Buddy> parse(const std::string& text);

// One record as its BUDDYDEF text block; `serialize_all` concatenates them with a
// blank line between, which is exactly what the BUDDYDEF lump holds.
std::string serialize(const Buddy& b);
std::string serialize_all(const std::vector<Buddy>& all);

// Read and parse the WAD's BUDDYDEF lump.  Empty when the WAD hasn't got one.
std::vector<Buddy> load_from_wad(const Wad& w);

}  // namespace buddy

#endif
