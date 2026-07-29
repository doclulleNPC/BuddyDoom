// mybuddy -- SDL3 editor for alternative co-op buddies (BUDDYDEF records) and the WADs
// that carry them.
//
// Same look/feel as the in-game Buddy menu (M_Buddy in m_menu.c): the WAD's lump
// directory and the buddy list on the left with a live sprite preview underneath, the
// field editor on the right above a preview of the exact BUDDYDEF text that will be
// written.  Open a WAD (or start a new one), edit, save.
//
// A buddy is player 2 (files/p_ai_coop.c), so a record supplies properties, never an
// actor.  Some keys are parsed but no longer reach the game; the editor colour-codes
// every row by that status instead of letting you tune a number that goes nowhere.
//
// No external deps beyond SDL3 and the DejaVuSansMono atlas baked into font_atlas.h.
// WAD I/O is buddydef_wad.[ch]pp; the BUDDYDEF format is buddydef_parse.[ch]pp, which
// mirrors files/p_buddydef.c.
//
// Build: tools/build_mybuddy.sh (g++ + pkg-config sdl3), tools/Makefile.msvc on Windows.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_dialog.h>	// SDL_ShowOpen/SaveFileDialog -- native, cross-platform

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../files/stb_image.h"	// PNG sprites (GZDoom-authored art), the engine's decoder

#include "font_atlas.h"
#include "buddydef_wad.hpp"
#include "buddydef_parse.hpp"
#include "../files/buddydoom_icon.h"

// OGG decode for the sound preview: the engine's stb_vorbis (files/stb_vorbis.c, added
// to the MyBuddy build), declared the same way i_sound.c declares it.
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len,
                                        int* channels, int* sample_rate, short** output);

using buddy::Buddy;
using buddy::Key;
using buddy::Wad;

// ----- layout -----
// Left column, top to bottom: the WAD's lump directory + its edit buttons, the buddy
// list + its buttons, then the sprite preview.  Right column: the field editor above
// the BUDDYDEF text preview.
// A wide, landscape 1330x800 design (fits comfortably inside 1080p with room for the
// title bar / taskbar).  The window is resizable and the whole UI scales with it: the
// layout is written in this fixed logical space and letterboxed into the real window
// size (SDL_SetRenderLogicalPresentation), so it works on smaller displays too.
enum {
    WINW      = 1330,   // the editor column carries the widest content, so width goes here
    WINH      = 800,
    HEADER_H  = 36,
    FOOTER_H  = 56,     // two rows: a full-width status line above the button row
    LIST_W    = 300,
    PAD       = 14,
    ROWH      = 26,

    WADLIST_Y = HEADER_H + PAD,
    WADLIST_H = 220,
    WADBTN_Y  = WADLIST_Y + WADLIST_H + 6,
    WADBTN_H  = 56,
    BUDLIST_Y = WADBTN_Y + WADBTN_H + PAD,
    BUDLIST_H = 150,
    BUDBTN_Y  = BUDLIST_Y + BUDLIST_H + 6,
    BUDBTN_H  = 28,
    SPRITE_Y  = BUDBTN_Y + BUDBTN_H + PAD,
    SPRITE_H  = WINH - FOOTER_H - PAD - SPRITE_Y,

    EDIT_Y    = HEADER_H + PAD,
    // Tall enough for 30px of chrome + ceil(short/2) grid rows + the full-width rows +
    // the help line.  With 21 fields (19 short, 2 long) that is 30 + 10*26 + 6 + 2*26 +
    // 26 = 374; the assert at the end of layout_fields() catches the next overflow.
    EDIT_H    = 384,
    PREV_Y    = EDIT_Y + EDIT_H + PAD,
    PREV_H    = WINH - FOOTER_H - PAD - PREV_Y,
    LABEL_W   = 150,    // fits the longest label ("Ranged attack") plus a gap
};

// ----- fields -----
enum class Kind {
    Text,      // free-form
    TextLong,  // full-width row (description)
    Int,       // < n > with +/- halves
    Choice,    // < value > cycling a fixed list
    Sprite,    // 4-char, upper-cased as you type
};

// How much of a key reaches the game.  Mirrors the table in docs/BUDDY_MODDING.md --
// update both together.
enum class Status { Live, Pending, Retired };

struct Field {
    const char*             label;
    Kind                    kind;
    Key                     key;                 // which "declared" bit this row owns
    std::string Buddy::*    str  = nullptr;      // exactly one of str/num is set
    int Buddy::*            num  = nullptr;
    int                     vmin = 0, vmax = 0;  // Int only
    const std::vector<std::string>* choices = nullptr;
    size_t                  textmax = 0;         // Text/Sprite: character cap
    Status                  status  = Status::Live;
    const char*             hint    = "";
};

// The value sets the engine actually knows.  Keep in step with p_buddydef.c: ATTACK ->
// Buddy_AttackPtr's table (retired, still parsed), ABILITY -> buddy_ability_name[],
// COLOR -> vp_buddycol_name[] in files/v_png.c.
static const std::vector<std::string> ATTACK_CHOICES = {
    "melee", "none", "baron", "bruiser", "hellknight", "knight",
    "imp", "troop", "poss", "zombie", "pistol", "zombieman",
    "spos", "shotgun", "shotgunguy", "cpos", "chaingun", "chaingunner",
    "sarg", "demon", "bite", "head", "caco", "cacodemon",
    "skel", "revenant", "fatt", "mancubus", "bspi", "arachnotron",
};
// Close-range attacks, by the actor they come from.  Every entry is an attack
// codepointer that actually exists in this engine and does its damage in melee range
// (P_CheckMeleeRange + P_DamageMobj) -- verified against p_enemy.c / heretic.c /
// hexen.c / p_mbf.c.  Entries marked (+r) also fire a missile when out of range; pick
// them when you want the actor's full behaviour, and pair with a ranged choice below.
//
// STRIFE IS NOT LISTED: this engine has no Strife actors.  "Strife" appears only in
// IWAD identification (files/w_iwadid.h) and the UDMF namespace list -- there is no
// Acolyte, Reaver or Templar to borrow an attack from.  (files/hexen.c's Stalker is
// Hexen's swamp Stalker, MT_XSTALKER, not Strife's.)
static const std::vector<std::string> MELEE_CHOICES = {
    "none",
    // --- Doom ---
    "demon",          // A_SargAttack   -- bite 4d10
    "revenant",       // A_SkelFist     -- punch 6d10
    "imp",            // A_TroopAttack  (+r) claw 3d8
    "cacodemon",      // A_HeadAttack   (+r) claw 10d6
    "baron",          // A_BruisAttack  (+r) claw 10d8
    "hellknight",     // A_BruisAttack  (+r) same, weaker body
    "scratch",        // A_Scratch      -- MBF21 generic melee
    // --- Heretic ---
    "golem",          // A_MummyAttack
    "sabreclaw",      // A_ClinkAttack
    "gargoyle",       // A_ImpMeAttack
    "minotaur",       // A_MinotaurAtk1 -- hammer
    "weredragon",     // A_BeastAttack  (+r)
    "undeadwarrior",  // A_KnightAttack (+r) axe
    "disciple",       // A_WizardAttack (+r)
    // --- Hexen ---
    "ettin",          // A_EttinAttack
    "centaur",        // A_CentaurAttack
    "serpent",        // A_DemonAttack1
    "wraith",         // A_WraithMelee
    "stalker",        // A_StalkerMelee
    "bishop",         // A_BishopAttack (+r)
};

// Attacks used at distance -- the missile half of the same actors, plus the pure
// shooters.  Same verification: each one spawns a missile or fires hitscans.
static const std::vector<std::string> RANGED_CHOICES = {
    "none",
    // --- Doom ---
    "zombieman",      // A_PosAttack    -- hitscan
    "shotgunguy",     // A_SPosAttack   -- hitscan spread
    "chaingunner",    // A_CPosAttack   -- hitscan
    "imp",            // A_TroopAttack  -- fireball
    "cacodemon",      // A_HeadAttack   -- fireball
    "baron",          // A_BruisAttack  -- green plasma
    "hellknight",     // A_BruisAttack
    "revenant",       // A_SkelMissile  -- homing
    "mancubus",       // A_FatAttack1
    "arachnotron",    // A_BspiAttack   -- plasma
    "cyberdemon",     // A_CyberAttack  -- rocket
    "lostsoul",       // A_SkullAttack  -- charge
    "painelemental",  // A_PainAttack   -- spawns lost souls
    "archvile",       // A_VileAttack   -- line-of-sight burn
    // --- Heretic ---
    "weredragon",     // A_BeastAttack
    "undeadwarrior",  // A_KnightAttack -- axe volley
    "disciple",       // A_WizardAttack
    "ironlich",       // A_LichAttack
    "ophidian",       // A_SnakeAttack
    "minotaur",       // A_MinotaurAtk2
    "dsparil",        // A_DsparilAttack
    // --- Hexen ---
    "centaurleader",  // A_CentaurAttack2
    "serpent",        // A_DemonAttack2
    "wraith",         // A_WraithMissile
    "bishop",         // A_BishopAttack
    "iceguy",         // A_IceGuyAttack
    "firedemon",      // A_FiredAttack
    "dragon",         // A_DragonAttack
    "stalker",        // A_StalkerMissile
};

static const std::vector<std::string> ABILITY_CHOICES = { "none", "drone", "poisoncloud", "turret" };
static const std::vector<std::string> COLOR_CHOICES   = { "", "Green", "Gray", "Brown", "Red",
                                                          "Blue", "Orange", "Purple", "White" };

// The built-in Marine (roster slot 0, P_Buddy_LoadDefs): the baseline an alternative
// buddy is a deviation from.
struct MarineStats { int health, speed, radius, height, mass, pain, react; };
static const MarineStats MARINE = { 100, 25, 16, 56, 100, 255, 0 };

static const std::vector<Field> FIELDS = {
    { "Name",          Kind::Text,     Key::Name,    &Buddy::name,    nullptr, 0, 0, nullptr, 40, Status::Live,
      "Shown big on the Buddy select screen." },
    { "Sprite base",   Kind::Sprite,   Key::Sprite,  &Buddy::sprite,  nullptr, 0, 0, nullptr, 4, Status::Live,
      "4-char sprite base, e.g. FRAN -> needs FRANA1.. in the WAD. Frame A is the preview." },
    { "Ability",       Kind::Choice,   Key::Ability, &Buddy::ability, nullptr, 0, 0, &ABILITY_CHOICES, 24, Status::Live,
      "The power the buddy really uses in play. Runs on the buddy's body each tic." },
    { "Color",         Kind::Choice,   Key::Color,   &Buddy::color,   nullptr, 0, 0, &COLOR_CHOICES, 24, Status::Live,
      "Default player-colour on the Buddy screen. Empty = no default (menu picks Green)." },
    { "Special blurb", Kind::TextLong, Key::Special, &Buddy::special, nullptr, 0, 0, nullptr, 96, Status::Live,
      "Free text for the SPECIAL: line on the select screen. Not a mechanic." },
    { "Description",   Kind::TextLong, Key::Desc,    &Buddy::desc,    nullptr, 0, 0, nullptr, 160, Status::Live,
      "Flavour text, word-wrapped into the ABOUT panel of the select screen." },

    { "Health",        Kind::Int, Key::Health,       nullptr, &Buddy::health,       1, 99999, nullptr, 0, Status::Pending,
      "Spawn health. Marine: 100. Needs the G_PlayerReborn hook, or a revive resets it." },
    { "Speed",         Kind::Int, Key::Speed,        nullptr, &Buddy::speed,        0, 100, nullptr, 0, Status::Pending,
      "Marine: 25. Players move by ticcmd, so this becomes a forwardmove, not info->speed." },
    { "Radius",        Kind::Int, Key::Radius,       nullptr, &Buddy::radius,       1, 256, nullptr, 0, Status::Pending,
      "Collision radius in map units. Marine: 16." },
    { "Height",        Kind::Int, Key::Height,       nullptr, &Buddy::height,       1, 512, nullptr, 0, Status::Pending,
      "Collision height in map units. Marine: 56." },
    { "Mass",          Kind::Int, Key::Mass,         nullptr, &Buddy::mass,         1, 100000, nullptr, 0, Status::Pending,
      "Knockback resistance. Marine: 100. Higher = harder to shove." },
    { "Pain chance",   Kind::Int, Key::PainChance,   nullptr, &Buddy::painchance,   0, 255, nullptr, 0, Status::Pending,
      "0-255 chance to flinch when hit. Marine: 255." },
    { "Reactiontime",  Kind::Int, Key::ReactionTime, nullptr, &Buddy::reactiontime, 0, 32, nullptr, 0, Status::Pending,
      "Tics before reacting to a target. Marine: 0." },

    { "See sound",     Kind::Text, Key::SeeSound,    &Buddy::seesnd,    nullptr, 0, 0, nullptr, 16, Status::Pending,
      "Lump name, e.g. FRANKN (a DSFRANKN lump works too). Empty = silent." },
    { "Pain sound",    Kind::Text, Key::PainSound,   &Buddy::painsnd,   nullptr, 0, 0, nullptr, 16, Status::Pending,
      "Lump name. The player's pain sound is hardcoded, so this needs a call-site hook." },
    { "Death sound",   Kind::Text, Key::DeathSound,  &Buddy::deathsnd,  nullptr, 0, 0, nullptr, 16, Status::Pending,
      "Lump name. Same hook as the pain sound." },
    { "Active sound",  Kind::Text, Key::ActiveSound, &Buddy::activesnd, nullptr, 0, 0, nullptr, 16, Status::Pending,
      "Idle grunt, lump name." },

    { "Melee attack",  Kind::Choice, Key::MeleeAttack,  &Buddy::melee,  nullptr, 0, 0, &MELEE_CHOICES, 24, Status::Pending,
      "Close-range attack, borrowed from an actor (Doom/Heretic/Hexen). No Strife actors exist." },
    { "Ranged attack", Kind::Choice, Key::RangedAttack, &Buddy::ranged, nullptr, 0, 0, &RANGED_CHOICES, 24, Status::Pending,
      "Attack used at distance. Set both: melee when close, this one otherwise." },

    { "Attack",        Kind::Choice, Key::Attack,    &Buddy::attack, nullptr, 0, 0, &ATTACK_CHOICES, 24, Status::Retired,
      "RETIRED: superseded by Melee/Ranged attack. Parsed so old lumps still load." },
    { "Ednum",         Kind::Int,    Key::Ednum,     nullptr, &Buddy::ednum, -1, 65535, nullptr, 0, Status::Retired,
      "RETIRED: a player is not map-placeable. Parsed, ignored." },
};

// Editor field layout, computed once into LAY[].  Flat and total by construction: short
// fields fill a balanced 2-column grid, full-width fields go underneath it.  (Deriving
// this incrementally is what once drew the rows below Description on top of the rows
// above it.)
struct FieldLay { float lx, vx, ry, vw; };
static std::vector<FieldLay> LAY;

static void layout_fields()
{
    const float x = LIST_W + 2 * PAD, w = WINW - x - PAD;
    const float colw = (w - 24) / 2.0f;
    const float top = EDIT_Y + 30;

    LAY.assign(FIELDS.size(), FieldLay{});

    size_t nshort = 0;
    for (const Field& f : FIELDS)
        if (f.kind != Kind::TextLong) nshort++;
    const int rows = (int)((nshort + 1) / 2);       // left column takes the extra row

    int r = 0;
    for (size_t i = 0; i < FIELDS.size(); i++) {
        if (FIELDS[i].kind == Kind::TextLong) continue;
        const int   col = r / rows, row = r % rows;
        const float rx  = x + 8 + col * colw;
        LAY[i] = { rx, rx + LABEL_W + 2, top + row * ROWH, colw - LABEL_W - 16 };
        r++;
    }

    float wide_y = top + rows * ROWH + 6;
    for (size_t i = 0; i < FIELDS.size(); i++) {
        if (FIELDS[i].kind != Kind::TextLong) continue;
        LAY[i] = { x + 8, x + 8 + LABEL_W + 2, wide_y, w - 16 - LABEL_W - 2 };
        wide_y += ROWH;
    }

    // The panel has to be tall enough for the grid, the full-width rows and the help
    // line, or the last row lands under the help text / outside the panel and reads as
    // "this field does nothing".  Adding fields is what trips this, so assert it here.
    SDL_assert(wide_y <= EDIT_Y + EDIT_H - 24);
}

// ----- app state -----
static SDL_Window*   win;
static SDL_Renderer* ren;
static SDL_Texture*  fonttex;

static std::string   wad_path;
static Wad           wad;
static bool          wad_loaded   = false;
static bool          wad_modified = false;

static std::vector<Buddy> buddies;
static int  sel        = 0;
static int  wad_scroll = 0;     // first visible lump in the directory panel
static int  wad_sel    = -1;    // selected lump (Export / Rename / Delete act on it)

enum class Mode { Normal, EditField, RenameLump };
static Mode        mode   = Mode::Normal;
static int         active = -1;             // FIELDS index being edited, -1 = none
static std::string editbuf;
static size_t      editbuf_max = 0;
static std::string status = "Open a WAD, or click New WAD.";

static void set_status(const char* fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    status = buf;
}

// ----- undo / redo -----
// Snapshot the whole editable state (buddy roster + WAD + selections) before each
// mutating action.  Coarse-grained but simple and always correct; a buddy pack is
// small enough that copying the WAD per edit is cheap.
static void sprite_invalidate();            // defined with the sprite preview below

struct Snapshot {
    std::vector<Buddy> buddies;
    int                sel, wad_sel;
    Wad                wad;
    bool               wad_modified;
};
static std::vector<Snapshot> undo_stack, redo_stack;
enum { UNDO_MAX = 64 };

static Snapshot snapshot_now()
{
    return Snapshot{ buddies, sel, wad_sel, wad, wad_modified };
}

static void restore(const Snapshot& s)
{
    buddies      = s.buddies;
    sel          = s.sel;
    wad_sel      = s.wad_sel;
    wad          = s.wad;
    wad_modified = s.wad_modified;
    if (sel >= (int)buddies.size()) sel = (int)buddies.size() - 1;
    if (sel < 0 && !buddies.empty()) sel = 0;
    sprite_invalidate();
}

// Call at the START of a mutating action, before the change.  Clears the redo
// stack (a new edit forks history) and caps the undo depth.
static void push_undo()
{
    undo_stack.push_back(snapshot_now());
    if (undo_stack.size() > UNDO_MAX) undo_stack.erase(undo_stack.begin());
    redo_stack.clear();
}

static void undo_reset()                    // a fresh WAD -- no history to walk back into
{
    undo_stack.clear();
    redo_stack.clear();
}

static void do_undo()
{
    if (undo_stack.empty()) { set_status("Nothing to undo."); return; }
    redo_stack.push_back(snapshot_now());
    restore(undo_stack.back());
    undo_stack.pop_back();
    set_status("Undo (%d more).", (int)undo_stack.size());
}

static void do_redo()
{
    if (redo_stack.empty()) { set_status("Nothing to redo."); return; }
    undo_stack.push_back(snapshot_now());
    restore(redo_stack.back());
    redo_stack.pop_back();
    set_status("Redo (%d more).", (int)redo_stack.size());
}

// ----------------------------------------------------------------- font / primitives
static void font_init()
{
    std::vector<Uint32> px((size_t)FONT_AW * FONT_CH);
    for (size_t i = 0; i < px.size(); i++)
        px[i] = 0x00FFFFFFu | ((Uint32)font_alpha[i] << 24);
    SDL_Surface* s = SDL_CreateSurfaceFrom(FONT_AW, FONT_CH, SDL_PIXELFORMAT_ARGB8888,
                                           px.data(), FONT_AW * 4);
    fonttex = SDL_CreateTextureFromSurface(ren, s);
    SDL_SetTextureBlendMode(fonttex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(fonttex, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(s);
}

// Draw `s` at x/y; `maxw` (when > 0) clips it to that many pixels.
static void text(float x, float y, const std::string& s, Uint8 r, Uint8 g, Uint8 b,
                 float maxw = 0)
{
    SDL_SetTextureColorMod(fonttex, r, g, b);
    const float xe = x + maxw;
    for (char ch : s) {
        if (maxw > 0 && x + FONT_CW > xe) return;
        int c = (unsigned char)ch;
        if (c < FONT_FIRST || c >= FONT_FIRST + FONT_COUNT) c = '?';
        SDL_FRect src = { (float)((c - FONT_FIRST) * FONT_CW), 0, FONT_CW, FONT_CH };
        SDL_FRect dst = { x, y, FONT_CW, FONT_CH };
        SDL_RenderTexture(ren, fonttex, &src, &dst);
        x += FONT_CW;
    }
}

static void rect(float x, float y, float w, float h, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_FRect q = { x, y, w, h };
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    SDL_RenderFillRect(ren, &q);
}

static void rect_outline(float x, float y, float w, float h, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_FRect q = { x, y, w, h };
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    SDL_RenderRect(ren, &q);
}

static std::string ellipsize(const std::string& s, int max_pixels)
{
    int max_chars = (max_pixels / FONT_CW) - 1;
    if (max_chars <= 0) max_chars = 1;
    if ((int)s.size() <= max_chars) return s;
    if (max_chars < 3) return "...";
    return s.substr(0, (size_t)max_chars - 3) + "...";
}

static float textw(const std::string& s) { return (float)s.size() * FONT_CW; }

// ----------------------------------------------------------------- field access
static void record_change() { wad_modified = true; }

static std::string field_value(const Buddy& b, const Field& f)
{
    return f.str ? b.*(f.str) : std::to_string(b.*(f.num));
}

// What the value cell shows: cyclers and numbers get their < > affordance, text is
// shown up to its first newline.
static std::string format_value(const Buddy& b, const Field& f)
{
    if (f.kind == Kind::Int)
        return "< " + std::to_string(b.*(f.num)) + " >";
    if (f.kind == Kind::Choice) {
        const std::string& v = b.*(f.str);
        return "< " + (v.empty() ? std::string("(none)") : v) + " >";
    }
    const std::string& v = b.*(f.str);
    const size_t nl = v.find('\n');
    return nl == std::string::npos ? v : v.substr(0, nl);
}

// ----------------------------------------------------------------- sprite preview
//
// Frame A of the selected buddy's `sprite` base, in both formats the engine accepts:
// the classic 8-bit Doom patch (needs a PLAYPAL) and PNG (as authored for GZDoom).
//
// The palette is NOT baked into this tool -- it comes from the opened WAD when it has
// one, else from an IWAD next to it.  A buddy PWAD usually has neither, in which case
// paletted patches can't be shown and we say so; PNG art still previews.
static uint8_t     pal_rgb[768];
static bool        have_pal = false;
static std::string pal_from;

static bool palette_from_file(const std::string& path)
{
    Wad iw;
    if (!iw.load(path)) return false;
    const std::vector<uint8_t>* p = iw.data("PLAYPAL");
    if (!p || p->size() < 768) return false;
    memcpy(pal_rgb, p->data(), 768);
    pal_from = path;
    return true;
}

static void load_palette()
{
    // Names an IWAD is likely to have, tried in the edited WAD's own folder, then that
    // folder's ID0/ and ../ID0/, then the working directory.
    static const char* const names[] = {
        "DOOM2.WAD", "doom2.wad", "DOOM.WAD", "doom.wad", "freedoom2.wad",
        "freedoom1.wad", "heretic.wad", "DOOM1.WAD", "doom1.wad",
    };
    have_pal = false;
    pal_from.clear();

    if (wad_loaded) {                       // a self-contained pack may ship its own
        const std::vector<uint8_t>* p = wad.data("PLAYPAL");
        if (p && p->size() >= 768) {
            memcpy(pal_rgb, p->data(), 768);
            have_pal = true;
            pal_from = "this WAD";
            return;
        }
    }

    std::string dir;
    if (!wad_path.empty()) {
        const size_t cut = wad_path.find_last_of("/\\");
        if (cut != std::string::npos) dir = wad_path.substr(0, cut);
    }
    for (const char* n : names) {
        if (!dir.empty()) {
            if (palette_from_file(dir + "/" + n))          { have_pal = true; return; }
            if (palette_from_file(dir + "/ID0/" + n))      { have_pal = true; return; }
            if (palette_from_file(dir + "/../ID0/" + n))   { have_pal = true; return; }
        }
        if (palette_from_file(std::string("ID0/") + n))    { have_pal = true; return; }
        if (palette_from_file(n))                          { have_pal = true; return; }
    }
}

// The IWAD the palette came from, cached so sprite scans/decodes (and especially frame
// animation, which re-decodes ~8x/sec) don't reload a 15 MB WAD from disk each time.
// nullptr when there is no separate IWAD (a self-contained pack, or none found).
static Wad         iwad_cache;
static std::string iwad_cache_path;
static bool        iwad_cache_ok = false;

static const Wad* sprite_iwad()
{
    if (!have_pal || pal_from.empty() || pal_from == "this WAD") return nullptr;
    if (iwad_cache_path != pal_from) {
        iwad_cache      = Wad();
        iwad_cache_ok   = iwad_cache.load(pal_from);
        iwad_cache_path = pal_from;
    }
    return iwad_cache_ok ? &iwad_cache : nullptr;
}

// Doom patch -> RGBA.  Layout (files/r_defs.h patch_t): width, height, leftoffset,
// topoffset as int16, then `width` int32 column offsets; a column is a run list of
// {topdelta, length, pad, pixels[length], pad} ended by topdelta 0xFF.
static std::vector<uint8_t> patch_to_rgba(const std::vector<uint8_t>& d, int& ow, int& oh)
{
    if (!have_pal || d.size() < 8) return {};
    const int w = (int16_t)(d[0] | (d[1] << 8));
    const int h = (int16_t)(d[2] | (d[3] << 8));
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return {};
    if (d.size() < (size_t)(8 + 4 * w)) return {};

    std::vector<uint8_t> px((size_t)w * h * 4, 0);
    for (int c = 0; c < w; c++) {
        size_t ofs = (size_t)d[8 + c*4] | ((size_t)d[9 + c*4] << 8)
                   | ((size_t)d[10 + c*4] << 16) | ((size_t)d[11 + c*4] << 24);
        if (ofs + 2 > d.size()) continue;
        const uint8_t* base = d.data();
        const uint8_t* col  = base + ofs;
        while (col + 2 <= base + d.size() && col[0] != 0xFF) {
            const int top = col[0], len = col[1];
            const uint8_t* src = col + 3;                       // skip the pad byte
            if (src + len + 1 > base + d.size()) break;
            for (int y = 0; y < len; y++) {
                const int yy = top + y;
                if (yy < 0 || yy >= h) continue;
                const uint8_t* rgb = pal_rgb + src[y] * 3;
                uint8_t* o = px.data() + ((size_t)yy * w + c) * 4;
                o[0] = rgb[0]; o[1] = rgb[1]; o[2] = rgb[2]; o[3] = 255;
            }
            col = src + len + 1;                                // trailing pad
        }
    }
    ow = w; oh = h;
    return px;
}

static SDL_Texture* sprite_tex;
static int          sprite_w, sprite_h;
static std::string  sprite_key      = "\x01";   // base|frame|rot the texture was built from
static std::string  sprite_base_key = "\x01";   // just the base, to detect buddy switches
static std::string  sprite_note;                // what we found, or why we found nothing
static char         sprite_frame  = 'A';        // animation frame being previewed
static int          sprite_rot    = 1;          // rotation (0 = rotation-less)
static bool         sprite_mirror = false;      // found a mirrored view -> flip on draw
static bool         sprite_anim   = false;      // cycle frames on a timer

static void sprite_invalidate() { sprite_key = "\x01"; }   // force a texture rebuild

static void sprite_free()
{
    if (sprite_tex) { SDL_DestroyTexture(sprite_tex); sprite_tex = nullptr; }
    sprite_w = sprite_h = 0;
}

static std::string cur_sprite_base()
{
    return (sel >= 0 && sel < (int)buddies.size()) ? buddies[sel].sprite : std::string();
}

// The lump for (base, frame, rot).  Sprite lumps are base(4)+frame+rot, with an
// optional second frame+rot pair for the mirrored view (e.g. TROOA2A8 = frame A rot 2,
// and frame A rot 8 drawn mirrored).  Preference: exact rot, then the rotation-less
// '0' frame, then the mirrored pair.  `mirror` tells the caller to flip horizontally.
static int find_sprite_lump(const Wad& w, const std::string& base, char frame, int rot,
                            bool& mirror)
{
    mirror = false;
    if (base.empty()) return -1;
    const std::string b4 = base.substr(0, 4);
    const char fr = (char)toupper((unsigned char)frame);
    const char rc = (char)('0' + rot);

    int  best = -1, bestpri = 99;
    bool bestmir = false;
    for (size_t k = 0; k < w.size(); k++) {
        const buddy::Lump& L = w[k];
        if (L.name.size() < 6 || L.data.size() <= 8) continue;
        if (SDL_strncasecmp(L.name.c_str(), b4.c_str(), 4) != 0) continue;
        if ((char)toupper((unsigned char)L.name[4]) == fr) {
            if      (L.name[5] == rc  && bestpri > 0) { best = (int)k; bestpri = 0; bestmir = false; }
            else if (L.name[5] == '0' && bestpri > 1) { best = (int)k; bestpri = 1; bestmir = false; }
        }
        if (L.name.size() >= 8 && (char)toupper((unsigned char)L.name[6]) == fr) {
            if      (L.name[7] == rc  && bestpri > 2) { best = (int)k; bestpri = 2; bestmir = true; }
            else if (L.name[7] == '0' && bestpri > 3) { best = (int)k; bestpri = 3; bestmir = true; }
        }
    }
    mirror = bestmir;
    return best;
}

// The frame letters (and, for `frame`, the rotation digits) that exist for `base`.
static void scan_sprite(const Wad& w, const std::string& base, std::string& frames,
                        char frame, std::vector<int>& rots)
{
    const std::string b4 = base.substr(0, 4);
    const char fr = (char)toupper((unsigned char)frame);
    auto add_frame = [&](char f) {
        f = (char)toupper((unsigned char)f);
        if (f >= 'A' && f <= 'Z' && frames.find(f) == std::string::npos) frames += f;
    };
    auto add_rot = [&](char f, char r) {
        if ((char)toupper((unsigned char)f) != fr) return;
        const int d = r - '0';
        if (d < 0 || d > 8) return;
        for (int x : rots) if (x == d) return;
        rots.push_back(d);
    };
    for (size_t k = 0; k < w.size(); k++) {
        const buddy::Lump& L = w[k];
        if (L.name.size() < 6 || L.data.size() <= 8) continue;
        if (SDL_strncasecmp(L.name.c_str(), b4.c_str(), 4) != 0) continue;
        add_frame(L.name[4]);
        add_rot(L.name[4], L.name[5]);
        if (L.name.size() >= 8) { add_frame(L.name[6]); add_rot(L.name[6], L.name[7]); }
    }
}

// Available frames/rotations for `base`, from the edited WAD (or the IWAD fallback the
// preview also uses).  Sorted so navigation steps in a stable order.
static void sprite_scan(const std::string& base, std::string& frames, char frame,
                        std::vector<int>& rots)
{
    frames.clear();
    rots.clear();
    if (!wad_loaded || base.empty()) return;
    scan_sprite(wad, base, frames, frame, rots);
    if (frames.empty()) {
        const Wad* iw = sprite_iwad();
        if (iw) scan_sprite(*iw, base, frames, frame, rots);
    }
    std::sort(frames.begin(), frames.end());
    std::sort(rots.begin(), rots.end());
}

static std::vector<uint8_t> decode_lump(const Wad& w, int lump, const char* whence,
                                        int& ow, int& oh, std::string& note)
{
    const buddy::Lump& l = w[(size_t)lump];
    std::vector<uint8_t> rgba;
    int wd = 0, ht = 0;
    char buf[128];

    if (l.data.size() < 8) { note = l.name + " is empty"; return {}; }

    if (l.data[0] == 0x89 && l.data[1] == 'P' && l.data[2] == 'N' && l.data[3] == 'G') {
        int comp = 0;
        uint8_t* p = stbi_load_from_memory(l.data.data(), (int)l.data.size(), &wd, &ht, &comp, 4);
        if (!p) { note = l.name + ": PNG decode failed"; return {}; }
        rgba.assign(p, p + (size_t)wd * ht * 4);
        stbi_image_free(p);
        snprintf(buf, sizeof buf, "%s  %dx%d  PNG%s", l.name.c_str(), wd, ht, whence);
    } else {
        rgba = patch_to_rgba(l.data, wd, ht);
        if (rgba.empty()) {
            note = l.name + (have_pal ? ": not a readable patch"
                                      : ": paletted patch, no PLAYPAL found");
            return {};
        }
        snprintf(buf, sizeof buf, "%s  %dx%d  patch%s", l.name.c_str(), wd, ht, whence);
    }

    // A fully transparent decode is a silent failure (wrong format guess, truncated
    // lump); report the coverage rather than showing an empty box.
    {
        const size_t total = (size_t)wd * ht;
        size_t opaque = 0;
        for (size_t i = 0; i < total; i++) if (rgba[i*4 + 3] > 127) opaque++;
        note = std::string(buf) + "  " + std::to_string(total ? opaque * 100 / total : 0) + "% opaque";
    }
    ow = wd; oh = ht;
    return rgba;
}

// Decode from the edited WAD, or -- for the "point `sprite` at art already in the IWAD"
// shortcut the modding guide recommends -- from the IWAD the palette came from.
static std::vector<uint8_t> decode_sprite(const std::string& base, char frame, int rot,
                                          bool& mirror, int& ow, int& oh, std::string& note)
{
    note.clear();
    mirror = false;
    if (!wad_loaded) { note = "no WAD open";        return {}; }
    if (base.empty()) { note = "no sprite base set"; return {}; }

    int lump = find_sprite_lump(wad, base, frame, rot, mirror);
    if (lump >= 0) return decode_lump(wad, lump, "", ow, oh, note);

    const Wad* iw = sprite_iwad();
    if (iw) {
        const int l = find_sprite_lump(*iw, base, frame, rot, mirror);
        if (l >= 0) return decode_lump(*iw, l, "  (from the IWAD)", ow, oh, note);
    }
    note = "no " + base.substr(0, 4) + std::string(1, (char)toupper((unsigned char)frame))
         + " frame in this WAD";
    return {};
}

static void sprite_refresh(bool force)
{
    const std::string base = cur_sprite_base();
    if (base != sprite_base_key) {              // switched buddy or edited the base
        sprite_base_key = base;
        sprite_frame    = 'A';
        sprite_rot      = 1;
        sprite_key      = "\x01";
    }
    const std::string key = base + "|" + std::string(1, sprite_frame)
                          + std::to_string(sprite_rot);
    if (!force && sprite_key == key) return;
    sprite_key = key;
    sprite_free();

    int  w = 0, h = 0;
    bool mir = false;
    std::vector<uint8_t> rgba = decode_sprite(base, sprite_frame, sprite_rot, mir, w, h,
                                              sprite_note);
    sprite_mirror = mir;
    if (mir && !sprite_note.empty()) sprite_note += "  (mirrored)";
    if (rgba.empty()) return;

    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888,
                                              rgba.data(), w * 4);
    if (surf) {
        sprite_tex = SDL_CreateTextureFromSurface(ren, surf);
        SDL_SetTextureScaleMode(sprite_tex, SDL_SCALEMODE_NEAREST);
        SDL_DestroySurface(surf);
        sprite_w = w; sprite_h = h;
    }
}

// Step to the previous/next existing frame letter (or rotation) for the current buddy.
static void sprite_step_frame(int dir)
{
    std::string frames;
    std::vector<int> rots;
    sprite_scan(cur_sprite_base(), frames, sprite_frame, rots);
    if (frames.empty()) return;
    size_t idx = frames.find(sprite_frame);
    idx = (idx == std::string::npos) ? 0
        : (idx + frames.size() + (dir > 0 ? 1 : -1)) % frames.size();
    sprite_frame = frames[idx];
    sprite_invalidate();
}

static void sprite_step_rot(int dir)
{
    std::string frames;
    std::vector<int> rots;
    sprite_scan(cur_sprite_base(), frames, sprite_frame, rots);
    if (rots.empty()) return;
    size_t idx = 0;
    for (size_t i = 0; i < rots.size(); i++) if (rots[i] == sprite_rot) { idx = i; break; }
    idx = (idx + rots.size() + (dir > 0 ? 1 : -1)) % rots.size();
    sprite_rot = rots[idx];
    sprite_invalidate();
}

// ----------------------------------------------------------------- sound preview
// Play a buddy sound lump (See/Pain/Death/Active) so a modder can check the mapping.
// Mirrors i_sound.c: OGG lumps decode via stb_vorbis; DMX lumps are 8-bit unsigned PCM
// behind an 8-byte header (sample rate at bytes 2-3).  Name resolution mirrors
// I_SfxLumpFor: try "DS"+name first (only when <= 6 chars, or it can't be a lump), then
// the bare lump name.  find() is case-insensitive, so no need to upper-case here.
static SDL_AudioStream* snd_stream = nullptr;

static bool is_sound_field(Key k)
{
    return k == Key::SeeSound || k == Key::PainSound
        || k == Key::DeathSound || k == Key::ActiveSound;
}

static int find_sound_lump(const std::string& name)
{
    if (name.empty()) return -1;
    if (name.size() <= 6) { const int l = wad.find("DS" + name); if (l >= 0) return l; }
    return wad.find(name);
}

static void play_sound(const std::string& name)
{
    const int l = find_sound_lump(name);
    if (l < 0) { set_status("No sound lump for '%s' (tried DS%s / %s).",
                            name.c_str(), name.c_str(), name.c_str()); return; }
    const std::vector<uint8_t>& d = wad[(size_t)l].data;
    if (d.size() < 8) { set_status("Sound lump %s is empty.", wad[(size_t)l].name.c_str()); return; }

    if (snd_stream) { SDL_DestroyAudioStream(snd_stream); snd_stream = nullptr; }

    SDL_AudioSpec spec = {};
    const uint8_t* pcm = nullptr;
    int            bytes = 0;
    std::vector<uint8_t> ogg_pcm;               // keeps the decoded S16 alive until Put

    if (memcmp(d.data(), "OggS", 4) == 0) {
        int chans = 0, rate = 0;
        short* out = nullptr;
        const int frames = stb_vorbis_decode_memory(d.data(), (int)d.size(), &chans, &rate, &out);
        if (frames <= 0 || !out || chans <= 0 || rate <= 0) {
            free(out);
            set_status("Could not decode OGG sound %s.", wad[(size_t)l].name.c_str());
            return;
        }
        spec.format = SDL_AUDIO_S16; spec.channels = chans; spec.freq = rate;
        bytes = frames * chans * (int)sizeof(short);
        ogg_pcm.assign((uint8_t*)out, (uint8_t*)out + bytes);
        free(out);
        pcm = ogg_pcm.data();
    } else {
        const int rate = d[2] | (d[3] << 8);    // DMX header: format, samplerate, count
        spec.format = SDL_AUDIO_U8; spec.channels = 1; spec.freq = rate > 0 ? rate : 11025;
        pcm = d.data() + 8;
        bytes = (int)d.size() - 8;
    }

    snd_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                           nullptr, nullptr);
    if (!snd_stream) { set_status("No audio device: %s", SDL_GetError()); return; }
    SDL_PutAudioStreamData(snd_stream, pcm, bytes);     // SDL copies it -> pcm can go away
    SDL_ResumeAudioStreamDevice(snd_stream);
    set_status("Playing %s (%d bytes).", wad[(size_t)l].name.c_str(), bytes);
}

// ----------------------------------------------------------------- file ops
static void new_wad_session()
{
    wad = Wad();
    wad_loaded = false;
    wad_path.clear();
    sprite_invalidate();
    buddies.assign(1, Buddy());
    buddies[0].set(Key::Name);
    sel = 0;
    wad_sel = -1;
    wad_modified = false;
    undo_reset();
    set_status("New WAD -- add a buddy, then Save As.");
}

static bool load_wad(const std::string& path)
{
    Wad next;
    if (!next.load(path)) {
        set_status("Could not open '%s' as a WAD.", path.c_str());
        return false;
    }
    wad = std::move(next);
    wad_loaded = true;
    wad_path = path;

    buddies = buddy::load_from_wad(wad);
    if (buddies.empty()) {
        buddies.assign(1, Buddy());
        buddies[0].set(Key::Name);
    }
    sel = 0;
    wad_scroll = 0;
    wad_sel = -1;
    wad_modified = false;
    undo_reset();
    load_palette();                     // PLAYPAL for paletted patch previews
    sprite_invalidate();
    set_status("Loaded %s -- %u lump(s), %d buddy(ies).", path.c_str(),
               (unsigned)wad.size(), (int)buddies.size());
    return true;
}

static bool save_wad(const std::string& path)
{
    if (!path.empty()) wad_path = path;
    if (wad_path.empty()) { set_status("Need a path -- File -> Save As."); return false; }

    const std::string lump = buddy::serialize_all(buddies);
    const std::vector<uint8_t> bytes(lump.begin(), lump.end());

    const int idx = wad.find("BUDDYDEF");
    if (idx >= 0) wad.replace(idx, bytes);
    else          wad.append("BUDDYDEF", bytes);

    if (!wad.save(wad_path)) {
        set_status("Save to %s failed.", wad_path.c_str());
        return false;
    }
    wad_loaded = true;                  // a "New WAD" session now has a real file
    wad_modified = false;
    set_status("Saved %s (%d buddy(ies)).", wad_path.c_str(), (int)buddies.size());
    return true;
}

// ----------------------------------------------------------------- native dialogs
// SDL3's dialogs are native on all three platforms and ASYNCHRONOUS: the callback may
// fire on another thread, so it only stashes the result and wakes the event loop.
static const SDL_DialogFileFilter WAD_FILTERS[] = {
    { "Doom WAD", "wad" },
    { "All files", "*" },
};
static const SDL_DialogFileFilter ANY_FILTERS[] = {
    { "All files", "*" },
};

enum class Pending { None, Open, Save, Import, Export, Cancel };
static std::string pending_path;
static Pending     pending_action = Pending::None;

static void SDLCALL dialog_cb(void* userdata, const char* const* filelist, int filter)
{
    (void)filter;
    const Pending action = (Pending)(intptr_t)userdata;
    if (filelist && filelist[0]) {
        pending_path   = filelist[0];
        pending_action = action;
    } else {
        pending_action = Pending::Cancel;       // cancelled, or error (filelist == NULL)
    }
    SDL_Event wake = {};
    wake.type = SDL_EVENT_USER;                 // make SDL_WaitEvent return
    SDL_PushEvent(&wake);
}

static void open_dialog()
{
    SDL_ShowOpenFileDialog(dialog_cb, (void*)(intptr_t)Pending::Open, win,
                           WAD_FILTERS, 2, nullptr, false);
    set_status("Choose a WAD to open...");
}

static void save_dialog()
{
    SDL_ShowSaveFileDialog(dialog_cb, (void*)(intptr_t)Pending::Save, win,
                           WAD_FILTERS, 2, wad_path.empty() ? nullptr : wad_path.c_str());
    set_status("Choose where to save...");
}

static void import_dialog()
{
    SDL_ShowOpenFileDialog(dialog_cb, (void*)(intptr_t)Pending::Import, win,
                           ANY_FILTERS, 1, nullptr, false);
    set_status("Choose a file to add as a lump...");
}

static void export_dialog()
{
    SDL_ShowSaveFileDialog(dialog_cb, (void*)(intptr_t)Pending::Export, win,
                           ANY_FILTERS, 1, nullptr);
    set_status("Choose where to write the lump...");
}

// ----------------------------------------------------------------- lump editing
// A legal 8-byte lump name from a file path: basename, extension dropped, upper-cased.
// ".../FRANA1.png" -> "FRANA1".
static std::string lump_name_from_path(const std::string& path)
{
    size_t b = path.find_last_of("/\\");
    std::string base = (b == std::string::npos) ? path : path.substr(b + 1);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.resize(dot);
    const std::string n = buddy::wad_name(base);
    return n.empty() ? "LUMP" : n;
}

static std::vector<uint8_t> read_file(const std::string& path, bool& ok)
{
    ok = false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return {}; }
    std::vector<uint8_t> buf((size_t)sz);
    if (sz && fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return {}; }
    fclose(f);
    ok = true;
    return buf;
}

// Add a file to the WAD.  Art belonging to the selected buddy's sprite base lands
// INSIDE the sprite namespace -- a sprite outside S_START/S_END is invisible to the
// engine, the single most common reason a modder buddy ends up with no art.
static void import_file(const std::string& path)
{
    if (!wad_loaded && wad.empty() && wad_path.empty()) {
        // A brand-new session has no file yet, but the in-memory WAD is perfectly
        // usable -- importing before the first save is fine.
        wad_loaded = true;
    }
    bool ok = false;
    const std::vector<uint8_t> data = read_file(path, ok);
    if (!ok) { set_status("Could not read '%s'.", path.c_str()); return; }

    push_undo();
    const std::string name = lump_name_from_path(path);
    const bool is_sprite = sel >= 0 && sel < (int)buddies.size()
                        && !buddies[sel].sprite.empty()
                        && SDL_strncasecmp(name.c_str(), buddies[sel].sprite.c_str(), 4) == 0;
    int idx;

    if (is_sprite) {
        const int at = wad.sprite_end();
        if (at < 0) {                   // no namespace yet -- create one around the import
            wad.append("SS_START", {});
            idx = wad.append(name, data);
            wad.append("SS_END", {});
            set_status("Imported %s (%u bytes) in a new SS_START/SS_END namespace.",
                       name.c_str(), (unsigned)data.size());
        } else {
            idx = wad.insert(at, name, data);
            set_status("Imported %s (%u bytes) into the sprite namespace.",
                       name.c_str(), (unsigned)data.size());
        }
    } else {
        idx = wad.append(name, data);
        set_status("Imported %s (%u bytes).", name.c_str(), (unsigned)data.size());
    }

    if (idx < 0) { set_status("Import of '%s' failed.", name.c_str()); return; }
    wad_sel = idx;
    wad_modified = true;
    load_palette();                     // the import may have been a PLAYPAL
    sprite_invalidate();
}

static void export_lump(const std::string& path)
{
    if (wad_sel < 0 || (size_t)wad_sel >= wad.size()) { set_status("Select a lump first."); return; }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { set_status("Could not write '%s'.", path.c_str()); return; }
    const buddy::Lump& l = wad[(size_t)wad_sel];
    if (!l.data.empty()) fwrite(l.data.data(), 1, l.data.size(), f);
    fclose(f);
    set_status("Exported %s (%u bytes).", l.name.c_str(), l.size());
}

static void delete_lump()
{
    if (wad_sel < 0 || (size_t)wad_sel >= wad.size()) { set_status("Select a lump first."); return; }
    const std::string nm = wad[(size_t)wad_sel].name;
    push_undo();
    if (!wad.erase(wad_sel)) return;
    if ((size_t)wad_sel >= wad.size()) wad_sel = (int)wad.size() - 1;
    wad_modified = true;
    load_palette();
    sprite_invalidate();
    set_status("Deleted lump %s.", nm.c_str());
}

// ----------------------------------------------------------------- UI layout
struct Buttons {
    SDL_FRect file, quit;                            // File v dropdown (header) + Quit (footer)
    SDL_FRect mi_open, mi_new, mi_save, mi_saveas;   // File-menu items (drawn when open)
    SDL_FRect add, del, dup;
    SDL_FRect marine;                   // seed the stat rows from the built-in Marine
    SDL_FRect imp, exp, ren, dell;      // lump-level WAD editing
    SDL_FRect frame_prev, frame_next;   // sprite preview: step animation frame
    SDL_FRect rot_prev, rot_next;       // sprite preview: step rotation
    SDL_FRect play;                     // sprite preview: animate on/off
};
static Buttons btn;
static bool     file_menu_open = false;              // the File v dropdown is showing

static void recompute_layout()
{
    // File sits in the HEADER, top-left (where the title text used to be -- the window
    // caption already says what this is), so its menu pops DOWN over the panels.
    btn.file = { PAD, 5, 110, 26 };
    const float mw = 150, ih = 26;
    const float my = btn.file.y + btn.file.h + 1;
    btn.mi_open   = { PAD, my + 0 * ih, mw, ih };
    btn.mi_new    = { PAD, my + 1 * ih, mw, ih };
    btn.mi_save   = { PAD, my + 2 * ih, mw, ih };
    btn.mi_saveas = { PAD, my + 3 * ih, mw, ih };

    // Footer: the status line gets its own full-width row (WINH-FOOTER_H+6); Quit sits
    // on the row below it so the status text is never drawn under a button.
    btn.quit = { WINW - PAD - 90, WINH - 30, 90, 26 };

    btn.add = { PAD,       BUDBTN_Y, 60,  BUDBTN_H };
    btn.del = { PAD + 70,  BUDBTN_Y, 80,  BUDBTN_H };
    btn.dup = { PAD + 160, BUDBTN_Y, 100, BUDBTN_H };

    btn.marine = { WINW - PAD - 160, EDIT_Y + 4, 160, 22 };

    const float bw = (LIST_W - 6) / 2.0f;
    btn.imp  = { PAD,          WADBTN_Y,      bw, 26 };
    btn.exp  = { PAD + bw + 6, WADBTN_Y,      bw, 26 };
    btn.ren  = { PAD,          WADBTN_Y + 30, bw, 26 };
    btn.dell = { PAD + bw + 6, WADBTN_Y + 30, bw, 26 };

    // Sprite-preview control row, along the bottom of the preview panel.
    // Layout: "Fr" [<] X [>]   "Rot" [<] N [>]   [Play].
    const float scy = SPRITE_Y + SPRITE_H - 46;
    btn.frame_prev = { PAD + 30,  scy, 20, 22 };
    btn.frame_next = { PAD + 66,  scy, 20, 22 };
    btn.rot_prev   = { PAD + 124, scy, 20, 22 };
    btn.rot_next   = { PAD + 160, scy, 20, 22 };
    btn.play       = { PAD + 186, scy, 94, 22 };

    layout_fields();
}

// ----------------------------------------------------------------- drawing
// The header carries the File button (drawn later, with its dropdown) on the left and
// the open-WAD state on the right.  No title text: the window caption already has it.
static void draw_header()
{
    rect(0, 0, WINW, HEADER_H, 32, 32, 40);
    const std::string right = (wad_path.empty() ? "(unsaved)" : wad_path)
                            + (wad_modified ? " *" : "")
                            + "   " + std::to_string(buddies.size()) + " buddy(ies)";
    text(WINW - PAD - textw(right), 10, right, 200, 200, 210);
    rect(0, HEADER_H, WINW, 1, 60, 60, 80);
}

// Top-left panel: the WAD's lump directory.  BUDDYDEF is highlighted, the selected
// lump wins over it, and the wheel scrolls.
static void draw_wad_contents()
{
    const float x = PAD, y = WADLIST_Y, w = LIST_W, h = WADLIST_H;
    rect(x, y, w, h, 22, 22, 30);
    rect_outline(x, y, w, h, 60, 60, 80);

    if (!wad_loaded && wad.empty()) {
        text(x + 8, y + 6, "WAD Contents", 160, 180, 220);
        text(x + 8, y + 34, "(no WAD open)", 120, 120, 140);
        return;
    }

    text(x + 8, y + 6, "WAD Contents (" + std::to_string(wad.size()) + ")", 160, 180, 220);

    const int maxrows = (int)((h - 34) / ROWH);
    float ry = y + 30;
    for (int i = wad_scroll; i < (int)wad.size() && (i - wad_scroll) < maxrows; i++) {
        const buddy::Lump& l = wad[(size_t)i];
        const bool isbud = (l.name == "BUDDYDEF");
        char row[64];
        snprintf(row, sizeof row, "%-8.8s %6u", l.name.c_str(), l.size());
        if (i == wad_sel)  rect(x + 4, ry, w - 8, ROWH, 60, 90, 130);
        else if (isbud)    rect(x + 4, ry, w - 8, ROWH, 70, 60, 40);
        text(x + 8, ry + 4, row, isbud ? 255 : 200, isbud ? 235 : 205, isbud ? 150 : 215, w - 16);
        ry += ROWH;
    }
    if ((int)wad.size() > maxrows) {
        const int last = std::min(wad_scroll + maxrows, (int)wad.size());
        const std::string sc = std::to_string(wad_scroll + 1) + "-" + std::to_string(last)
                             + "/" + std::to_string(wad.size());
        text(x + w - textw(sc) - 8, y + 6, sc, 120, 140, 160);
    }
}

static int wadrow_at(float mx, float my)
{
    const float x = PAD, y = WADLIST_Y, w = LIST_W, h = WADLIST_H;
    if (wad.empty()) return -1;
    if (mx < x || mx >= x + w || my < y + 30) return -1;
    const int r = (int)((my - (y + 30)) / ROWH);
    const int i = wad_scroll + r;
    if (r < 0 || r >= (int)((h - 34) / ROWH)) return -1;
    if (i < 0 || (size_t)i >= wad.size()) return -1;
    return i;
}

static void draw_lump_buttons()
{
    struct Row { SDL_FRect r; const char* label; Uint8 cr, cg, cb; bool needs_sel; };
    const Row rows[4] = {
        { btn.imp,  "Import file", 50, 90, 60,   false },
        { btn.exp,  "Export lump", 50, 70, 110,  true  },
        { btn.ren,  "Rename lump", 60, 60, 100,  true  },
        { btn.dell, "Delete lump", 110, 50, 50,  true  },
    };
    for (const Row& b : rows) {
        const bool live = (!wad.empty() || !b.needs_sel) && (!b.needs_sel || wad_sel >= 0);
        rect(b.r.x, b.r.y, b.r.w, b.r.h, live ? b.cr : 40, live ? b.cg : 40, live ? b.cb : 48);
        text(b.r.x + (b.r.w - textw(b.label)) / 2.0f, b.r.y + 4, b.label,
             live ? 235 : 110, live ? 245 : 110, live ? 245 : 125);
    }
}

static void draw_list()
{
    const float x = PAD, y = BUDLIST_Y, w = LIST_W, h = BUDLIST_H;
    rect(x, y, w, h, 22, 22, 30);
    rect_outline(x, y, w, h, 60, 60, 80);
    text(x + 8, y + 6, "Buddies", 160, 180, 220);

    const int maxrows = (int)((h - 34) / ROWH);   // never draw past the panel edge
    float ry = y + 30;
    for (int i = 0; i < (int)buddies.size() && i < maxrows; i++) {
        if (i == sel) rect(x + 4, ry, w - 8, ROWH, 60, 90, 130);
        std::string head = buddies[i].name.empty() ? "(untitled)" : buddies[i].name;
        if (!buddies[i].sprite.empty()) head += "  [" + buddies[i].sprite.substr(0, 4) + "]";
        text(x + 12, ry + 4, ellipsize(head, (int)w - 24), i == sel ? 255 : 220,
             i == sel ? 255 : 220, 220);
        ry += ROWH;
    }
    if ((int)buddies.size() > maxrows)
        text(x + 8, y + h - 20,
             "+" + std::to_string((int)buddies.size() - maxrows) + " more (wheel to select)",
             150, 160, 180);

    rect(btn.add.x, btn.add.y, btn.add.w, btn.add.h, 50, 90, 50);
    text(btn.add.x + 10, btn.add.y + 6, "+ New", 230, 255, 230);
    rect(btn.del.x, btn.del.y, btn.del.w, btn.del.h, 110, 50, 50);
    text(btn.del.x + 16, btn.del.y + 6, "Delete", 255, 230, 230);
    rect(btn.dup.x, btn.dup.y, btn.dup.w, btn.dup.h, 50, 70, 110);
    text(btn.dup.x + 16, btn.dup.y + 6, "Duplicate", 230, 240, 255);
}

static void draw_sprite_preview()
{
    const float x = PAD, y = SPRITE_Y, w = LIST_W, h = SPRITE_H;
    rect(x, y, w, h, 18, 18, 26);
    rect_outline(x, y, w, h, 60, 60, 80);
    text(x + 8, y + 6, "Sprite preview", 160, 180, 220);

    sprite_refresh(false);

    if (sprite_tex && sprite_w > 0 && sprite_h > 0) {
        const float bw = w - 16, bh = h - 78;   // leave room for the control row + note
        float sc = bw / (float)sprite_w;
        if (sprite_h * sc > bh) sc = bh / (float)sprite_h;
        if (sc > 6.0f) sc = 6.0f;               // don't blow a 20px sprite up to mush
        const float dw = sprite_w * sc, dh = sprite_h * sc;
        SDL_FRect dst = { x + (w - dw) / 2.0f, y + 28 + (bh - dh) / 2.0f, dw, dh };
        if (sprite_mirror)
            SDL_RenderTextureRotated(ren, sprite_tex, nullptr, &dst, 0.0, nullptr,
                                     SDL_FLIP_HORIZONTAL);
        else
            SDL_RenderTexture(ren, sprite_tex, nullptr, &dst);
    }

    // Frame / rotation stepper + animation toggle.
    auto ctl = [&](const SDL_FRect& b, const char* s, bool on) {
        rect(b.x, b.y, b.w, b.h, on ? 70 : 50, on ? 100 : 60, on ? 70 : 90);
        text(b.x + (b.w - textw(s)) / 2.0f, b.y + 4, s, 230, 240, 245);
    };
    const float scy = btn.frame_prev.y;
    text(x + 8, scy + 4, "Fr", 200, 210, 220);
    ctl(btn.frame_prev, "<", false);
    text(btn.frame_prev.x + 24, scy + 4, std::string(1, sprite_frame), 255, 235, 160);
    ctl(btn.frame_next, ">", false);
    text(btn.rot_prev.x - 36, scy + 4, "Rot", 200, 210, 220);
    ctl(btn.rot_prev, "<", false);
    text(btn.rot_prev.x + 24, scy + 4, std::to_string(sprite_rot), 255, 235, 160);
    ctl(btn.rot_next, ">", false);
    ctl(btn.play, sprite_anim ? "Stop" : "Play", sprite_anim);

    text(x + 8, y + h - 20, sprite_note.empty() ? "(nothing to show)" : sprite_note,
         150, 160, 180, w - 16);
}

static int field_at(float mx, float my);
static void mouse_logical(float* mx, float* my);
static bool hit(float mx, float my, const SDL_FRect& r);

static void draw_editor()
{
    const float x = LIST_W + 2 * PAD, y = EDIT_Y, w = WINW - x - PAD, h = EDIT_H;
    rect(x, y, w, h, 22, 22, 30);
    rect_outline(x, y, w, h, 60, 60, 80);
    text(x + 8, y + 6, "Editor", 160, 180, 220);
    // Legend for the label colours: which keys actually reach the game today.
    text(x + 8 + 8 * FONT_CW,  y + 6, "   live",    210, 210, 230);
    text(x + 8 + 15 * FONT_CW, y + 6, "  pending",  205, 170, 110);
    text(x + 8 + 24 * FONT_CW, y + 6, "  retired",  115, 115, 130);
    rect(btn.marine.x, btn.marine.y, btn.marine.w, btn.marine.h, 50, 70, 110);
    text(btn.marine.x + 8, btn.marine.y + 4, "Marine baseline", 230, 240, 255);

    if (sel < 0 || sel >= (int)buddies.size()) return;
    const Buddy& b = buddies[sel];

    float mxf = 0, myf = 0;
    mouse_logical(&mxf, &myf);
    const int hover = field_at(mxf, myf);

    for (size_t i = 0; i < FIELDS.size(); i++) {
        const Field& f = FIELDS[i];
        const FieldLay& L = LAY[i];
        Uint8 lr = 200, lg = 200, lb = 220;     // label, colour-coded by status
        Uint8 vr = 255, vg = 235, vb = 150;     // value

        if (f.status == Status::Pending) { lr = 205; lg = 170; lb = 110; vr = 205; vg = 185; vb = 130; }
        else if (f.status == Status::Retired) { lr = 115; lg = 115; lb = 130; vr = 130; vg = 130; vb = 140; }

        text(L.lx, L.ry + 4, f.label, lr, lg, lb);

        const bool hot = (active == (int)i);
        if (hot || hover == (int)i) {
            const SDL_FRect bg = { L.vx - 4, L.ry + 1, L.vw + 4, ROWH - 4 };
            if (mode == Mode::EditField && hot) rect(bg.x, bg.y, bg.w, bg.h, 60, 60, 90);
            else if (hot)                       rect(bg.x, bg.y, bg.w, bg.h, 50, 50, 70);
            else                                rect(bg.x, bg.y, bg.w, bg.h, 34, 34, 46);
        }
        if (mode == Mode::EditField && hot) {
            text(L.vx, L.ry + 4, editbuf, 255, 235, 160);
            text(L.vx + textw(editbuf), L.ry + 4, "_", 255, 255, 160);
        } else {
            const float vmaxw = is_sound_field(f.key) ? L.vw - 20 : L.vw;
            text(L.vx, L.ry + 4, ellipsize(format_value(b, f), (int)vmaxw), vr, vg, vb);
        }
        // Sound rows get a small ">" play button at the right of the value cell.
        if (is_sound_field(f.key)) {
            const float px = L.vx + L.vw - 16;
            rect(px, L.ry + 3, 14, ROWH - 8, 55, 85, 55);
            text(px + 3, L.ry + 4, ">", 220, 245, 220);
        }
    }

    // Help for the row under the cursor (or the one being edited): what the key does,
    // the Marine's value where there is one, and why it may not be live yet.
    const int which = (mode == Mode::EditField && active >= 0) ? active : hover;
    const float hy = y + h - 18;
    if (which >= 0) {
        const Field& f = FIELDS[(size_t)which];
        std::string line = f.hint;
        if (f.status == Status::Pending) line = "[pending] " + line;
        else if (f.status == Status::Retired) line = "[retired] " + line;
        text(x + 8, hy, line, f.status == Status::Retired ? 150 : 190,
             f.status == Status::Retired ? 150 : 200, 210, w - 16);
    } else {
        text(x + 8, hy, "Hover for help. Click </> to nudge a number or the number to type it; a < choice > cycles.",
             130, 140, 160, w - 16);
    }
}

// Everything the engine would reject or silently ignore about this record, as one line.
// Mirrors what p_buddydef.c does at load time: a sprite base is 4 chars and must exist
// as <base>A1/<base>A0 (only the length is checkable here), and an unknown ability
// falls back to none.
static std::string issues_line(const Buddy& b)
{
    std::vector<std::string> out;
    auto known = [](const std::vector<std::string>& set, const std::string& v, size_t from) {
        for (size_t i = from; i < set.size(); i++)
            if (SDL_strcasecmp(set[i].c_str(), v.c_str()) == 0) return true;
        return false;
    };

    if (b.name.empty()) out.push_back("no name");
    if (b.sprite.size() != 4)
        out.push_back("sprite base is " + std::to_string(b.sprite.size()) + " chars, needs 4");
    if (!b.ability.empty() && !known(ABILITY_CHOICES, b.ability, 0))
        out.push_back("unknown ability '" + b.ability + "' (engine falls back to none)");
    if (!b.color.empty() && !known(COLOR_CHOICES, b.color, 1))
        out.push_back("unknown color '" + b.color + "'");

    std::string s;
    for (size_t i = 0; i < out.size(); i++) s += (i ? "; " : "") + out[i];
    return s;
}

static void draw_preview()
{
    if (sel < 0 || sel >= (int)buddies.size()) return;
    const float x = LIST_W + 2 * PAD, y = PREV_Y, w = WINW - x - PAD, h = PREV_H;
    rect(x, y, w, h, 14, 14, 24);
    rect_outline(x, y, w, h, 60, 60, 80);
    text(x + 8, y + 6, "Preview (what gets written to the WAD)", 160, 180, 220);

    const std::string probs = issues_line(buddies[sel]);
    if (!probs.empty())
        text(x + 8 + 39 * FONT_CW, y + 6, "! " + probs, 235, 170, 90, w - 16 - 39 * FONT_CW);

    const std::string txt = buddy::serialize(buddies[sel]);
    const int max_lines = (int)((h - 36) / FONT_CH);
    float cy = y + 30;
    size_t at = 0;
    for (int n = 0; n < max_lines && at <= txt.size(); n++) {
        size_t nl = txt.find('\n', at);
        if (nl == std::string::npos) nl = txt.size();
        text(x + 8, cy, txt.substr(at, nl - at), 180, 200, 220, w - 20);
        cy += FONT_CH;
        if (nl >= txt.size()) break;
        at = nl + 1;
    }
}

static void draw_footer()
{
    rect(0, WINH - FOOTER_H, WINW, FOOTER_H, 32, 32, 40);
    rect(0, WINH - FOOTER_H, WINW, 1, 60, 60, 80);
    // Status / rename prompt on its own full-width row, above the button row.
    if (mode == Mode::RenameLump)
        text(PAD, WINH - FOOTER_H + 6, "Rename lump: " + editbuf + "_", 255, 235, 160,
             WINW - 2 * PAD);
    else if (!status.empty())
        text(PAD, WINH - FOOTER_H + 6, status, 200, 220, 200, WINW - 2 * PAD);

    rect(btn.quit.x, btn.quit.y, btn.quit.w, btn.quit.h, 110, 50, 50);
    text(btn.quit.x + 28, btn.quit.y + 6, "Quit", 255, 230, 230);
}

// The File button and its dropdown.  Drawn LAST, after every panel, so the open menu
// overlays them instead of being painted over.
static void draw_file_menu()
{
    rect(btn.file.x, btn.file.y, btn.file.w, btn.file.h, file_menu_open ? 70 : 50, 80, 110);
    text(btn.file.x + 18, btn.file.y + 6, "File  v", 230, 240, 255);

    if (file_menu_open) {
        float hx = 0, hy = 0;
        mouse_logical(&hx, &hy);
        struct Item { const SDL_FRect& r; const char* label; };
        const Item items[4] = {
            { btn.mi_open,   "Open WAD..." },
            { btn.mi_new,    "New WAD" },
            { btn.mi_save,   "Save" },
            { btn.mi_saveas, "Save As..." },
        };
        const SDL_FRect& top = btn.mi_open;
        rect(top.x, top.y, top.w, 4 * top.h, 30, 34, 44);              // menu background
        rect_outline(top.x, top.y, top.w, 4 * top.h, 90, 110, 150);
        for (const Item& it : items) {
            if (hit(hx, hy, it.r)) rect(it.r.x, it.r.y, it.r.w, it.r.h, 55, 75, 110);
            text(it.r.x + 10, it.r.y + 6, it.label, 225, 235, 245);
        }
    }
}

static void draw()
{
    rect(0, 0, WINW, WINH, 16, 16, 22);
    draw_header();
    draw_wad_contents();
    draw_lump_buttons();
    draw_list();
    draw_sprite_preview();
    draw_editor();
    draw_preview();
    draw_footer();
    draw_file_menu();		// last: the open dropdown must overlay the panels
    SDL_RenderPresent(ren);
}

// ----------------------------------------------------------------- input
static bool hit(float mx, float my, const SDL_FRect& r)
{
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

// SDL_GetMouseState returns window pixels; with logical presentation the UI lives in
// 1020x1000 logical space, so convert before hit-testing against layout rects.
static void mouse_logical(float* mx, float* my)
{
    float wx = 0, wy = 0;
    SDL_GetMouseState(&wx, &wy);
    SDL_RenderCoordinatesFromWindow(ren, wx, wy, mx, my);
}

static int field_at(float mx, float my)
{
    if (sel < 0 || sel >= (int)buddies.size()) return -1;
    for (size_t i = 0; i < FIELDS.size(); i++)
        if (my >= LAY[i].ry && my < LAY[i].ry + ROWH
            && mx >= LAY[i].lx && mx < LAY[i].vx + LAY[i].vw + 4)
            return (int)i;
    return -1;
}

static int list_at(float mx, float my)
{
    const float x = PAD, y = BUDLIST_Y;
    if (mx < x || mx >= x + LIST_W || my < y + 30 || my >= y + BUDLIST_H) return -1;
    const int idx = (int)((my - (y + 30)) / ROWH);
    return (idx < 0 || idx >= (int)buddies.size()) ? -1 : idx;
}

// Left half of a value cell decrements, right half increments.
static int value_dir(float mx, float vx, float value_px)
{
    return mx < vx + value_px / 2.0f ? -1 : 1;
}

static void begin_edit_text(int idx)
{
    if (sel < 0 || sel >= (int)buddies.size()) return;
    const Field& f = FIELDS[(size_t)idx];
    active = idx;
    mode = Mode::EditField;
    editbuf = f.num ? std::string() : field_value(buddies[sel], f);   // numbers: type fresh
    editbuf_max = f.textmax ? f.textmax : (f.num ? 7 : 64);   // ints: up to 7 digits
    SDL_StartTextInput(win);
    set_status("Editing '%s' -- type a value, Enter to confirm, Esc to cancel.", f.label);
}

static void commit_edit_text()
{
    if (active < 0) return;
    const Field& f = FIELDS[(size_t)active];
    Buddy& b = buddies[sel];
    if (editbuf.size() > editbuf_max) editbuf.resize(editbuf_max);
    if (f.num) {                                 // numeric field: type fresh, parse + clamp
        if (!editbuf.empty()) {                  // empty commit -> leave the value as-is
            const int v = std::max(f.vmin, std::min(f.vmax, atoi(editbuf.c_str())));
            if (b.*(f.num) != v) push_undo();
            b.*(f.num) = v;
        }
    } else {
        if (b.*(f.str) != editbuf) push_undo();  // skip a no-op commit (click-away)
        b.*(f.str) = editbuf;
    }
    b.set(f.key);
    record_change();
    if (f.key == Key::Sprite) sprite_invalidate();
    SDL_StopTextInput(win);
    mode = Mode::Normal;
    active = -1;
    set_status("Saved %s.", f.label);
}

static void cancel_edit()
{
    if (mode != Mode::Normal) SDL_StopTextInput(win);
    mode = Mode::Normal;
    active = -1;
    set_status("Cancelled.");
}

static void begin_rename_lump()
{
    if (wad_sel < 0 || (size_t)wad_sel >= wad.size()) { set_status("Select a lump first."); return; }
    editbuf = wad[(size_t)wad_sel].name;
    editbuf_max = 8;
    mode = Mode::RenameLump;
    active = -1;
    SDL_StartTextInput(win);
    set_status("New lump name (8 chars max) -- Enter to confirm, Esc to cancel.");
}

static void commit_rename_lump()
{
    if (mode != Mode::RenameLump) return;
    if (wad_sel >= 0 && (size_t)wad_sel < wad.size() && wad[(size_t)wad_sel].name != editbuf)
        push_undo();
    if (wad.rename(wad_sel, editbuf)) {
        wad_modified = true;
        sprite_invalidate();
        set_status("Renamed lump to %s.", wad[(size_t)wad_sel].name.c_str());
    } else {
        set_status("Rename failed (empty name?).");
    }
    SDL_StopTextInput(win);
    mode = Mode::Normal;
}

static void cycle_choice(int idx, int dir)
{
    const Field& f = FIELDS[(size_t)idx];
    Buddy& b = buddies[sel];
    push_undo();
    const std::vector<std::string>& set = *f.choices;
    size_t cur = 0;
    // Case-insensitive, like the engine's own lookups (Buddy_ColorIndex /
    // Buddy_AbilityId): a hand-written "green" must still find its slot, or the first
    // click would jump to an unrelated value instead of stepping on.
    for (size_t i = 0; i < set.size(); i++)
        if (SDL_strcasecmp((b.*(f.str)).c_str(), set[i].c_str()) == 0) { cur = i; break; }
    cur = (cur + set.size() + (size_t)((dir > 0) ? 1 : -1)) % set.size();
    b.*(f.str) = set[cur];
    b.set(f.key);
    record_change();
}

static void inc_int(int idx, int dir)
{
    const Field& f = FIELDS[(size_t)idx];
    Buddy& b = buddies[sel];
    int& v = b.*(f.num);
    const int nv = std::max(f.vmin, std::min(f.vmax, v + dir));
    if (nv == v) return;                        // clamped at min/max -- no change
    push_undo();
    v = nv;
    b.set(f.key);
    record_change();
}

static void click_value(float mx, float my)
{
    const int i = field_at(mx, my);
    if (i < 0) return;
    const Field& f = FIELDS[(size_t)i];
    if (f.kind == Kind::Choice) {
        const int dir = value_dir(mx, LAY[i].vx, textw(format_value(buddies[sel], f)));
        cycle_choice(i, dir);
        return;
    }
    if (f.kind == Kind::Int) {
        // "< n >": the leftmost "<" nudges down, the rightmost ">" nudges up, and
        // clicking the number itself opens a text edit -- so 500 -> 200 is one type,
        // not 300 clicks.
        const std::string s = format_value(buddies[sel], f);
        const float x0 = LAY[i].vx, wpx = textw(s);
        if (mx < x0 + 2 * FONT_CW)              inc_int(i, -1);
        else if (mx >= x0 + wpx - 2 * FONT_CW)  inc_int(i, +1);
        else                                    begin_edit_text(i);
        return;
    }
    if (is_sound_field(f.key) && mx >= LAY[i].vx + LAY[i].vw - 18) {
        play_sound(field_value(buddies[sel], f));       // clicked the ">" play button
        return;
    }
    begin_edit_text(i);
}

static void click_main(float mx, float my)
{
    // File dropdown: while open it captures the next click (an item runs its action,
    // anything else just closes it).
    if (file_menu_open) {
        if      (hit(mx, my, btn.mi_open))   { file_menu_open = false; open_dialog(); }
        else if (hit(mx, my, btn.mi_new))    { file_menu_open = false; new_wad_session(); }
        else if (hit(mx, my, btn.mi_save))   { file_menu_open = false; save_wad(""); }
        else if (hit(mx, my, btn.mi_saveas)) { file_menu_open = false; save_dialog(); }
        else                                 { file_menu_open = false; }
        return;
    }
    if (hit(mx, my, btn.file)) { file_menu_open = true; return; }
    if (hit(mx, my, btn.quit)) { SDL_Event q = {}; q.type = SDL_EVENT_QUIT; SDL_PushEvent(&q); return; }

    if (hit(mx, my, btn.add)) {
        push_undo();
        buddies.emplace_back();
        buddies.back().set(Key::Name);
        sel = (int)buddies.size() - 1;
        sprite_invalidate();
        record_change();
        set_status("Added new buddy.");
        return;
    }
    if (hit(mx, my, btn.del)) {
        if (buddies.size() <= 1) { set_status("Need at least one buddy."); return; }
        push_undo();
        buddies.erase(buddies.begin() + sel);
        if (sel >= (int)buddies.size()) sel = (int)buddies.size() - 1;
        sprite_invalidate();
        record_change();
        set_status("Deleted buddy.");
        return;
    }
    if (hit(mx, my, btn.dup)) {
        push_undo();
        buddies.push_back(buddies[sel]);
        sel = (int)buddies.size() - 1;
        record_change();
        set_status("Duplicated buddy.");
        return;
    }
    if (hit(mx, my, btn.marine)) {
        // An alternative co-op buddy is a deviation from the Marine, so that is the
        // honest starting point.  (The parser's defaults are a different, older
        // baseline -- see docs/BUDDYDEF.md section 5.)
        if (sel < 0 || sel >= (int)buddies.size()) return;
        push_undo();
        Buddy& b = buddies[sel];
        b.health = MARINE.health;  b.set(Key::Health);
        b.speed  = MARINE.speed;   b.set(Key::Speed);
        b.radius = MARINE.radius;  b.set(Key::Radius);
        b.height = MARINE.height;  b.set(Key::Height);
        b.mass   = MARINE.mass;    b.set(Key::Mass);
        b.painchance   = MARINE.pain;   b.set(Key::PainChance);
        b.reactiontime = MARINE.react;  b.set(Key::ReactionTime);
        record_change();
        set_status("Stats set to the Marine's baseline.");
        return;
    }

    if (hit(mx, my, btn.imp))  { import_dialog(); return; }
    if (hit(mx, my, btn.exp))  { export_dialog(); return; }
    if (hit(mx, my, btn.ren))  { begin_rename_lump(); return; }
    if (hit(mx, my, btn.dell)) { delete_lump(); return; }

    // Sprite-preview controls (view only -- no record change, no undo entry).
    if (hit(mx, my, btn.frame_prev)) { sprite_step_frame(-1); return; }
    if (hit(mx, my, btn.frame_next)) { sprite_step_frame(+1); return; }
    if (hit(mx, my, btn.rot_prev))   { sprite_step_rot(-1);   return; }
    if (hit(mx, my, btn.rot_next))   { sprite_step_rot(+1);   return; }
    if (hit(mx, my, btn.play)) {
        sprite_anim = !sprite_anim;
        set_status(sprite_anim ? "Animating sprite frames." : "Animation stopped.");
        return;
    }

    {   // WAD directory: pick the lump the lump buttons act on
        const int wi = wadrow_at(mx, my);
        if (wi >= 0) {
            wad_sel = wi;
            set_status("Lump %s -- %u bytes.", wad[(size_t)wi].name.c_str(), wad[(size_t)wi].size());
            return;
        }
    }

    const int li = list_at(mx, my);
    if (li >= 0) {
        sel = li;
        active = -1;
        mode = Mode::Normal;
        SDL_StopTextInput(win);
        sprite_invalidate();
        return;
    }
    click_value(mx, my);
}

// ----------------------------------------------------------------- headless check
// Everything the editor would show about a WAD, on stdout.  Useful in a build script
// ("does my buddy pack still parse and is its art findable?") and the only way to
// exercise the parser + sprite decoder without a display.
static int check_mode(const std::string& path)
{
    if (!load_wad(path)) { printf("mybuddy: cannot open '%s'\n", path.c_str()); return 1; }
    load_palette();

    printf("WAD      %s -- %u lump(s)\n", path.c_str(), (unsigned)wad.size());
    printf("PLAYPAL  %s\n", have_pal ? pal_from.c_str()
                                     : "(none found -- paletted art can't be previewed)");
    printf("buddies  %d\n", (int)buddies.size());

    for (size_t i = 0; i < buddies.size(); i++) {
        const Buddy& b = buddies[i];
        sel = (int)i;
        printf("\n  [%d] %s\n", (int)i, b.name.empty() ? "(untitled)" : b.name.c_str());
        printf("      sprite %-4.4s  ability %-12s color %-8s\n",
               b.sprite.c_str(), b.ability.empty() ? "none" : b.ability.c_str(),
               b.color.empty() ? "-" : b.color.c_str());
        printf("      hp %d  speed %d  size %dx%d  mass %d  pain %d  react %d\n",
               b.health, b.speed, b.radius, b.height, b.mass, b.painchance, b.reactiontime);

        int  w = 0, h = 0;
        bool mir = false;
        std::string note;
        decode_sprite(b.sprite, 'A', 1, mir, w, h, note);
        printf("      art    %s\n", note.c_str());

        const std::string probs = issues_line(b);
        if (!probs.empty()) printf("      ISSUES %s\n", probs.c_str());
    }
    return 0;
}

// ----------------------------------------------------------------- main
int main(int argc, char** argv)
{
    SDL_SetMainReady();
    recompute_layout();

    if (argc >= 3 && std::string(argv[1]) == "--check") return check_mode(argv[2]);

    if (argc >= 2) load_wad(argv[1]);
    else           new_wad_session();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_InitSubSystem(SDL_INIT_AUDIO);      // sound preview -- optional, ignore failure
    win = SDL_CreateWindow("MyBuddy - Buddy Editor", WINW, WINH, SDL_WINDOW_RESIZABLE);
    if (!win) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    {
        SDL_Surface* icon = SDL_CreateSurfaceFrom(BUDDYDOOM_ICON_W, BUDDYDOOM_ICON_H,
                                                  SDL_PIXELFORMAT_ABGR8888,
                                                  (void*)buddydoom_icon_rgba,
                                                  BUDDYDOOM_ICON_W * 4);
        if (icon) { SDL_SetWindowIcon(win, icon); SDL_DestroySurface(icon); }
    }
    // Below this the text stops being legible, and the layout is a fixed grid that
    // cannot reflow -- so put a floor under it rather than allow a useless window.
    SDL_SetWindowMinimumSize(win, WINW / 2, WINH / 2);
    ren = SDL_CreateRenderer(win, nullptr);
    // The whole UI is laid out in a fixed WINW x WINH logical space and scaled into
    // whatever size the window is dragged to, aspect preserved (bars on the odd side).
    // Mouse/event coords arrive in window pixels and are converted back into this space
    // (SDL_ConvertEventToRenderCoordinates below, mouse_logical() for hover), so nothing
    // in the layout has to know the window was resized.
    SDL_SetRenderLogicalPresentation(ren, WINW, WINH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    font_init();

    bool run = true;
    while (run) {
        SDL_Event e;
        // While animating, wake on a timer to advance the frame; otherwise block.
        const bool got = sprite_anim ? SDL_WaitEventTimeout(&e, 120) : SDL_WaitEvent(&e);
        if (!got) {
            if (sprite_anim) { sprite_step_frame(+1); draw(); continue; }   // timeout tick
            break;                                                          // real failure
        }

        // Map mouse/wheel event coords from window pixels into the 1020x1000 logical
        // space the UI is laid out in (see SDL_SetRenderLogicalPresentation above).
        SDL_ConvertEventToRenderCoordinates(ren, &e);

        switch (e.type) {
        case SDL_EVENT_QUIT:
            run = false;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                // An open edit commits, and the click still reaches its target -- so
                // moving from one field to the next is one click, not two.  (Swallowing
                // it made the long Special blurb / Description rows look uneditable:
                // click one, click the other, nothing appears to happen.)
                if (mode == Mode::EditField)       { commit_edit_text(); click_main(e.button.x, e.button.y); }
                else if (mode == Mode::RenameLump) commit_rename_lump();
                else                               click_main(e.button.x, e.button.y);
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (mode == Mode::Normal) {
                float mmx = 0, mmy = 0;
                mouse_logical(&mmx, &mmy);
                const bool over_wad = !wad.empty() && mmx >= PAD && mmx < PAD + LIST_W
                                   && mmy >= WADLIST_Y && mmy < WADLIST_Y + WADLIST_H;
                if (over_wad) {
                    const int maxrows = (int)((WADLIST_H - 34) / ROWH);
                    if (e.wheel.y > 0) { if (wad_scroll > 0) wad_scroll--; }
                    else if (wad_scroll < (int)wad.size() - maxrows) wad_scroll++;
                } else {
                    const int before = sel;
                    if (e.wheel.y > 0) { if (sel > 0) sel--; }
                    else if (sel < (int)buddies.size() - 1) sel++;
                    if (sel != before) sprite_invalidate();
                }
            }
            break;

        case SDL_EVENT_TEXT_INPUT:
            if (mode != Mode::Normal) {
                const std::string in = e.text.text;
                // Sprite bases and lump names are stored upper-case in the WAD directory;
                // fold as the user types so "fran" and "FRAN" can't make two lumps.
                const bool is_int   = (mode == Mode::EditField && active >= 0
                                       && FIELDS[(size_t)active].num);
                const bool is_upper = (mode == Mode::RenameLump)
                                    || (mode == Mode::EditField && active >= 0
                                        && FIELDS[(size_t)active].kind == Kind::Sprite);
                for (char c : in) {
                    if (editbuf.size() >= editbuf_max) break;
                    if (is_int && !((c >= '0' && c <= '9') || (c == '-' && editbuf.empty())))
                        continue;                       // numeric field: digits (and a leading -)
                    if (is_upper) c = (char)toupper((unsigned char)c);
                    editbuf += c;
                }
            }
            break;

        case SDL_EVENT_KEY_DOWN:
            if (mode == Mode::RenameLump) {
                if (e.key.key == SDLK_BACKSPACE)   { if (!editbuf.empty()) editbuf.pop_back(); }
                else if (e.key.key == SDLK_RETURN) commit_rename_lump();
                else if (e.key.key == SDLK_ESCAPE) cancel_edit();
            } else if (mode == Mode::EditField) {
                if (e.key.key == SDLK_BACKSPACE)   { if (!editbuf.empty()) editbuf.pop_back(); }
                else if (e.key.key == SDLK_RETURN) commit_edit_text();
                else if (e.key.key == SDLK_ESCAPE) cancel_edit();
            } else {
                if (e.key.key == SDLK_ESCAPE) { if (file_menu_open) file_menu_open = false; else run = false; }
                else if (e.key.key == SDLK_S && (e.key.mod & SDL_KMOD_CTRL)) save_wad("");
                else if (e.key.key == SDLK_O && (e.key.mod & SDL_KMOD_CTRL)) open_dialog();
                else if (e.key.key == SDLK_Z && (e.key.mod & SDL_KMOD_CTRL) && (e.key.mod & SDL_KMOD_SHIFT)) do_redo();
                else if (e.key.key == SDLK_Z && (e.key.mod & SDL_KMOD_CTRL)) do_undo();
                else if (e.key.key == SDLK_Y && (e.key.mod & SDL_KMOD_CTRL)) do_redo();
                else if (e.key.key == SDLK_UP)   { if (sel > 0) { sel--; sprite_invalidate(); } }
                else if (e.key.key == SDLK_DOWN) { if (sel < (int)buddies.size() - 1) { sel++; sprite_invalidate(); } }
            }
            break;

        default:
            break;
        }

        // Apply an async file-dialog result (stashed by dialog_cb).
        switch (pending_action) {
        case Pending::Open:   load_wad(pending_path);     pending_action = Pending::None; break;
        case Pending::Save:   save_wad(pending_path);     pending_action = Pending::None; break;
        case Pending::Import: import_file(pending_path);  pending_action = Pending::None; break;
        case Pending::Export: export_lump(pending_path);  pending_action = Pending::None; break;
        case Pending::Cancel: set_status("Dialog cancelled."); pending_action = Pending::None; break;
        default: break;
        }

        draw();
    }

    sprite_free();
    if (snd_stream) SDL_DestroyAudioStream(snd_stream);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
