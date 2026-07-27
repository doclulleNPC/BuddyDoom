// mybuddy -- tiny SDL3 editor for BUDDYDEF records in WADs.
//
// Same look/feel as the in-game Buddy menu (M_Buddy in m_menu.c): a list of
// buddies on the left, a stats/identity panel on the right, and a live preview
// of the BUDDYDEF text at the bottom-right.  Open a WAD, edit the BUDDYDEF
// lumps (or append a new one), save.
//
// No external deps beyond SDL3 and the DejaVuSansMono font atlas already baked
// into tools/font_atlas.h.  WAD I/O lives in buddydef_wad.[ch]; BUDDYDEF format
// in buddydef_parse.[ch] (mirrors files/p_buddydef.c).
//
// Build: see tools/build_mybuddy.sh  (gcc + pkg-config sdl3)

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_dialog.h>	// SDL_ShowOpen/SaveFileDialog -- native, cross-platform
#include <stdint.h>		// intptr_t (dialog userdata tag)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _MSC_VER
#include <io.h>
#define access _access
#ifndef F_OK
#define F_OK 0
#endif
#else
#include <unistd.h>
#endif

#include "font_atlas.h"
#include "buddydef_wad.h"
#include "buddydef_parse.h"
#include "../files/buddydoom_icon.h"

// ----- layout -----
#define WINW          980
#define WINH          770
#define HEADER_H      36
#define FOOTER_H      44
#define LIST_W        280
#define PAD            14
#define ROWH          26
// Left column: a WAD-contents (lump directory) panel on top, the buddies list below.
#define WADLIST_Y     (HEADER_H + PAD)
#define WADLIST_H     200
#define BUDLIST_Y     (WADLIST_Y + WADLIST_H + PAD)
#define BUDLIST_H     ((WINH - FOOTER_H - PAD - 32) - PAD - BUDLIST_Y)
#define PREV_H        200
#define PREV_Y        (WINH - FOOTER_H - PREV_H - PAD)
#define EDIT_Y        (HEADER_H + PAD)
#define EDIT_H        (PREV_Y - EDIT_Y - PAD)
#define LABEL_W       110

// ----- field kinds -----
enum {
    F_TEXT,        // free-form text
    F_TEXT_LONG,   // multi-line (description)
    F_INT,         // +/- inc/dec on a min..max range
    F_CHOICE,      // cycle through `choices[]`
    F_SPRITE,      // 4-char uppercase-only input (rendered identical to F_TEXT)
};

typedef struct {
    const char* label;
    int         kind;
    int         vmin, vmax;
    const char* const* choices;
    int         nchoices;
    int         text_off;        // byte offset into buddydef_entry_t
    int         text_max;        // buffer size at .text_off
} field_t;

#define OFFS(field) ((int)offsetof(buddydef_entry_t, field))

#define B_NAME_MAX      40
#define B_DESC_MAX      160
#define B_SPECIAL_MAX    96
#define B_ABILITY_MAX    24
#define B_COLOR_MAX      24
#define B_ATTACK_MAX     24
#define B_SND_MAX        16
#define B_SPRITE_MAX      8

#define T_STR(field, lab, max)      { lab, F_TEXT, 0, 0, NULL, 0, OFFS(field), (max) }
#define T_LONG(field, lab, max)     { lab, F_TEXT_LONG, 0, 0, NULL, 0, OFFS(field), (max) }
#define T_INT(field, lab, lo, hi)   { lab, F_INT, (lo), (hi), NULL, 0, OFFS(field), 0 }
#define T_CH(field, lab, arr)       { lab, F_CHOICE, 0, 0, (arr), (int)(sizeof(arr)/sizeof(arr[0])), OFFS(field), 0 }

static const char* const ATTACK_CHOICES[] = {
    "melee", "none", "baron", "bruiser", "hellknight", "knight",
    "imp", "troop",
    "poss", "zombie", "pistol", "zombieman",
    "spos", "shotgun", "shotgunguy",
    "cpos", "chaingun", "chaingunner",
    "sarg", "demon", "bite",
    "head", "caco", "cacodemon",
    "skel", "revenant",
    "fatt", "mancubus",
    "bspi", "arachnotron",
};
static const char* const ABILITY_CHOICES[] = { "none", "drone", "poisoncloud", "turret" };

static const field_t FIELDS[] = {
    T_STR(name,        "Name",          B_NAME_MAX),
    T_STR(sprite,      "Sprite base",   B_SPRITE_MAX),
    T_STR(attack,      "Attack",        B_ATTACK_MAX),
    T_STR(ability,     "Ability",       B_ABILITY_MAX),
    T_STR(color,       "Color",         B_COLOR_MAX),
    T_STR(seesnd,      "See sound",     B_SND_MAX),
    T_STR(painsnd,     "Pain sound",    B_SND_MAX),
    T_STR(deathsnd,    "Death sound",   B_SND_MAX),
    T_STR(activesnd,   "Active sound",  B_SND_MAX),
    T_STR(special,     "Special blurb", B_SPECIAL_MAX),
    T_LONG(desc,       "Description",   B_DESC_MAX),
    T_INT(health,      "Health",        1, 99999),
    T_INT(speed,       "Speed",         0, 100),
    T_INT(radius,      "Radius",        1, 256),
    T_INT(height,      "Height",        1, 512),
    T_INT(mass,        "Mass",          1, 100000),
    T_INT(painchance,  "Pain chance",   0, 255),
    T_INT(reactiontime,"Reactiontime",  0, 32),
    T_INT(ednum,       "Ednum",         -1, 65535),
};
#define NFIELDS (int)(sizeof(FIELDS)/sizeof(FIELDS[0]))

// Compute the layout of the editor panel and return the y-position of row
// `row_in_col` in column `col` of the 2-column layout.  Special-cases F_TEXT_LONG
// to span both columns.
static void editor_row_pos(int idx, int* out_col, int* out_row_in_col, float* out_ry)
{
    float x = LIST_W + 2*PAD, w = WINW - x - PAD, h = EDIT_H;
    float colw = (w - 24) / 2.0f;
    float fy = EDIT_Y + 30;
    static int col = 0, row_in_col = 0;
    static float last_fy = 0;
    (void)last_fy;

    // We redo the layout for each field (it's < 20 iterations, cheap).
    col = 0; row_in_col = 0;
    for (int i = 0; i < NFIELDS; i++) {
        const field_t* f = &FIELDS[i];
        if (f->kind == F_TEXT_LONG) {
            if (i == idx) {
                *out_col = 0;
                *out_row_in_col = row_in_col;
                *out_ry = fy + row_in_col * ROWH;
                return;
            }
            fy += ROWH;
            col = 0; row_in_col = 0;
            continue;
        }
        if (i == idx) {
            *out_col = col;
            *out_row_in_col = row_in_col;
            *out_ry = fy + row_in_col * ROWH;
            return;
        }
        row_in_col++;
        if (row_in_col * (float)ROWH > (h - 60)) {
            col++;
            row_in_col = 0;
        }
    }
    *out_col = 0; *out_row_in_col = 0; *out_ry = 0;
}

// ----- engine state -----
static SDL_Window*   win;
static SDL_Renderer* ren;
static SDL_Texture*  fonttex;

static char          wad_path[1024] = "";
static buddydef_wad_t wad;
static int            wad_loaded = 0;
static int            wad_modified = 0;

static buddydef_entry_t* buddies = NULL;
static int                n_buddies = 0;
static int                sel = 0;
static int                wad_scroll = 0;        // first visible lump in the WAD-contents panel

static int            active = -1;            // FIELDS index of cell being edited, -1 = none
static int            mode = 0;               // 0 normal, 1 text edit
static char           editbuf[256];
static int            editbuf_max = 0;
static char           status[160] = "Open a WAD, or click New WAD.";

// ----------------------------------------------------------------- font
static void font_init(void)
{
    Uint32* px = (Uint32*)malloc(FONT_AW*FONT_CH*4);
    for (int i = 0; i < FONT_AW*FONT_CH; i++)
        px[i] = 0x00FFFFFFu | ((Uint32)font_alpha[i] << 24);
    SDL_Surface* s = SDL_CreateSurfaceFrom(FONT_AW, FONT_CH, SDL_PIXELFORMAT_ARGB8888, px, FONT_AW*4);
    fonttex = SDL_CreateTextureFromSurface(ren, s);
    SDL_SetTextureBlendMode(fonttex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(fonttex, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(s); free(px);
}

static void text(float x, float y, const char* s, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_SetTextureColorMod(fonttex, r, g, b);
    for (const char* p = s; *p; p++) {
        int c = (unsigned char)*p;
        if (c < FONT_FIRST || c >= FONT_FIRST+FONT_COUNT) c = '?';
        SDL_FRect src = { (float)((c-FONT_FIRST)*FONT_CW), 0, FONT_CW, FONT_CH };
        SDL_FRect dst = { x, y, FONT_CW, FONT_CH };
        SDL_RenderTexture(ren, fonttex, &src, &dst);
        x += FONT_CW;
    }
}

static void text_clip(float x, float y, float xmax, const char* s, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_SetTextureColorMod(fonttex, r, g, b);
    float xe = x + xmax;
    for (const char* p = s; *p; p++) {
        if (x + FONT_CW > xe) return;
        int c = (unsigned char)*p;
        if (c < FONT_FIRST || c >= FONT_FIRST+FONT_COUNT) c = '?';
        SDL_FRect src = { (float)((c-FONT_FIRST)*FONT_CW), 0, FONT_CW, FONT_CH };
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

static const char* ellipsize(const char* s, int max_pixels)
{
    static char buf[256];
    int max_chars = (max_pixels / FONT_CW) - 1;
    if (max_chars <= 0) max_chars = 1;
    int n = (int)strlen(s);
    if (n <= max_chars) { snprintf(buf, sizeof buf, "%s", s); return buf; }
    if (max_chars < 3) { snprintf(buf, sizeof buf, "..."); return buf; }
    snprintf(buf, sizeof buf, "%.*s...", max_chars - 3, s);
    return buf;
}

// ----------------------------------------------------------------- field helpers
static void record_change(void) { wad_modified = 1; }

static char* field_ptr(buddydef_entry_t* b, const field_t* f) { return (char*)b + f->text_off; }
static const char* field_value(const buddydef_entry_t* b, const field_t* f) { return (const char*)b + f->text_off; }

static void format_value(const buddydef_entry_t* b, const field_t* f, char* out, int outsz)
{
    if (f->kind == F_INT) {
        int v = *(int*)((const char*)b + f->text_off);
        snprintf(out, outsz, "< %d >", v);
    } else if (f->kind == F_CHOICE) {
        const char* cur = (const char*)b + f->text_off;
        snprintf(out, outsz, "< %s >", cur[0] ? cur : "(none)");
    } else {
        const char* cur = (const char*)b + f->text_off;
        const char* nl = strchr(cur, '\n');
        if (nl) {
            int n = (int)(nl - cur);
            if (n >= outsz) n = outsz - 1;
            memcpy(out, cur, n); out[n] = 0;
        } else snprintf(out, outsz, "%s", cur);
    }
}

// Set the matching "set_*" bit.  Done by label string mapping (the layout
// above is the source of truth for which fields exist, so we don't need a
// table of bit offsets).
static void set_dirty(buddydef_entry_t* b, const field_t* f)
{
    const char* l = f->label;
    if      (!strcmp(l, "Name"))          b->set_name = 1;
    else if (!strcmp(l, "Sprite base"))   b->set_sprite = 1;
    else if (!strcmp(l, "Attack"))        b->set_attack = 1;
    else if (!strcmp(l, "Ability"))       b->set_ability = 1;
    else if (!strcmp(l, "Color"))         b->set_color = 1;
    else if (!strcmp(l, "See sound"))     b->set_seesnd = 1;
    else if (!strcmp(l, "Pain sound"))    b->set_painsnd = 1;
    else if (!strcmp(l, "Death sound"))   b->set_deathsnd = 1;
    else if (!strcmp(l, "Active sound"))  b->set_activesnd = 1;
    else if (!strcmp(l, "Special blurb")) b->set_special = 1;
    else if (!strcmp(l, "Description"))   b->set_desc = 1;
    else if (!strcmp(l, "Health"))        b->set_health = 1;
    else if (!strcmp(l, "Speed"))         b->set_speed = 1;
    else if (!strcmp(l, "Radius"))        b->set_radius = 1;
    else if (!strcmp(l, "Height"))        b->set_height = 1;
    else if (!strcmp(l, "Mass"))          b->set_mass = 1;
    else if (!strcmp(l, "Pain chance"))   b->set_painchance = 1;
    else if (!strcmp(l, "Reactiontime"))  b->set_reactiontime = 1;
    else if (!strcmp(l, "Ednum"))         b->set_ednum = 1;
}

// ----------------------------------------------------------------- file ops
static void new_wad_session(void)
{
    if (wad_loaded) buddydef_wad_free(&wad);
    wad_loaded = 0;
    wad_path[0] = 0;
    free(buddies);
    buddies = (buddydef_entry_t*)calloc(1, sizeof *buddies);
    buddydef_defaults(buddies);
    buddies[0].set_name = 1;
    n_buddies = 1;
    sel = 0;
    wad_modified = 0;
    snprintf(status, sizeof status, "New WAD -- add a buddy, then Save As.");
}

static int load_wad(const char* path)
{
    if (wad_loaded) buddydef_wad_free(&wad);
    if (buddydef_wad_load(path, &wad) != 0) {
        wad_loaded = 0;
        snprintf(status, sizeof status, "Could not open '%s' as a WAD.", path);
        return -1;
    }
    wad_loaded = 1;
    snprintf(wad_path, sizeof wad_path, "%s", path);
    free(buddies);
    int n = 0;
    if (buddydef_load_from_wad(&wad, &buddies, &n) != 0) {
        buddies = (buddydef_entry_t*)calloc(1, sizeof *buddies);
        buddydef_defaults(buddies);
        buddies[0].set_name = 1;
        n = 1;
    }
    n_buddies = n;
    sel = 0;
    wad_scroll = 0;
    wad_modified = 0;
    snprintf(status, sizeof status, "Loaded %s -- %u lump(s), %d buddy(ies).", path, wad.numlumps, n);
    return 0;
}

static int save_wad(const char* path)
{
    if (path && path[0]) snprintf(wad_path, sizeof wad_path, "%s", path);
    if (!wad_path[0]) { snprintf(status, sizeof status, "Need a path -- File -> Save As."); return -1; }

    // Build the full BUDDYDEF lump text.
    char* lump = buddydef_serialize_all(buddies, n_buddies);
    if (!lump) { snprintf(status, sizeof status, "Serialize failed."); return -1; }
    unsigned sz = (unsigned)strlen(lump);

    if (!wad_loaded) {
        // First save from a "New WAD" session: write a tiny empty PWAD shell,
        // then reload it -- the WAD writer insists on a real lump table.
        buddydef_wad_t fresh; memset(&fresh, 0, sizeof fresh);
        memcpy(fresh.id, "PWAD", 4);
        fresh.dir = (buddydef_wad_dirent_t*)calloc(0, 1);
        fresh.data = (unsigned char**)calloc(0, sizeof *fresh.data);
        if (buddydef_wad_save(&fresh, wad_path) != 0) {
            free(lump); buddydef_wad_free(&fresh);
            snprintf(status, sizeof status, "Save to %s failed.", wad_path);
            return -1;
        }
        buddydef_wad_free(&fresh);
        if (buddydef_wad_load(wad_path, &wad) != 0) {
            free(lump);
            snprintf(status, sizeof status, "Reload after save failed.");
            return -1;
        }
        wad_loaded = 1;
    }

    int idx = buddydef_wad_find(&wad, "BUDDYDEF");
    if (idx >= 0) {
        buddydef_wad_replace_lump(&wad, idx, lump, sz);
    } else {
        buddydef_wad_append_lump(&wad, "BUDDYDEF", lump, sz);
    }
    free(lump);

    if (buddydef_wad_save(&wad, wad_path) != 0) {
        snprintf(status, sizeof status, "Save to %s failed.", wad_path);
        return -1;
    }
    wad_modified = 0;
    snprintf(status, sizeof status, "Saved %s (%d buddy(ies)).", wad_path, n_buddies);
    return 0;
}

// ----------------------------------------------------------------- native dialogs
// SDL3's built-in file dialogs are native on Windows/macOS/Linux -- one API, no
// comdlg32 / zenity / kdialog.  They are ASYNCHRONOUS: the callback fires when the
// user is done (may be a background thread), so it only stashes the result in
// pending_* and wakes the event loop; the actual load/save runs on the main thread.
static const SDL_DialogFileFilter WAD_FILTERS[] = {
    { "Doom WAD", "wad" },
    { "All files", "*"   },
};
enum { PENDING_NONE = 0, PENDING_OPEN = 1, PENDING_SAVE = 2, PENDING_CANCEL = -1 };
static char pending_path[1024];
static int  pending_action = PENDING_NONE;

static void SDLCALL dialog_cb(void* userdata, const char* const* filelist, int filter)
{
    (void)filter;
    int action = (int)(intptr_t)userdata;          // PENDING_OPEN or PENDING_SAVE
    if (filelist && filelist[0])
    {
        snprintf(pending_path, sizeof pending_path, "%s", filelist[0]);
        pending_action = action;
    }
    else
        pending_action = PENDING_CANCEL;           // cancelled, or error (filelist == NULL)

    SDL_Event wake = { .type = SDL_EVENT_USER };    // make SDL_WaitEvent return
    SDL_PushEvent(&wake);
}

static void open_dialog(void)
{
    SDL_ShowOpenFileDialog(dialog_cb, (void*)(intptr_t)PENDING_OPEN, win,
                           WAD_FILTERS, (int)(sizeof WAD_FILTERS / sizeof WAD_FILTERS[0]),
                           NULL, false);
    snprintf(status, sizeof status, "Choose a WAD to open...");
}

static void save_dialog(void)
{
    SDL_ShowSaveFileDialog(dialog_cb, (void*)(intptr_t)PENDING_SAVE, win,
                           WAD_FILTERS, (int)(sizeof WAD_FILTERS / sizeof WAD_FILTERS[0]),
                           wad_path[0] ? wad_path : NULL);
    snprintf(status, sizeof status, "Choose where to save...");
}

// ----------------------------------------------------------------- UI layout
typedef struct {
    SDL_FRect open, newwad, save, saveas, quit;
    SDL_FRect add, del, dup;
} btns_t;
static btns_t btn;

static void recompute_layout(void)
{
    float fy = WINH - FOOTER_H + 8;
    btn.open   = (SDL_FRect){ PAD,              fy, 110, 28 };
    btn.newwad = (SDL_FRect){ PAD + 120,        fy, 110, 28 };
    btn.save   = (SDL_FRect){ WINW - PAD - 320, fy, 100, 28 };
    btn.saveas = (SDL_FRect){ WINW - PAD - 210, fy, 110, 28 };
    btn.quit   = (SDL_FRect){ WINW - PAD - 90,  fy,  90, 28 };

    float ly = WINH - FOOTER_H - PAD - 32;
    btn.add = (SDL_FRect){ PAD,       ly, 60,  28 };
    btn.del = (SDL_FRect){ PAD + 70,  ly, 80,  28 };
    btn.dup = (SDL_FRect){ PAD + 160, ly, 100, 28 };
}

static void draw_header(void)
{
    rect(0, 0, WINW, HEADER_H, 32, 32, 40);
    text(PAD, 10, "MyBuddy - BUDDYDEF Editor", 220, 220, 240);
    const char* fn = wad_path[0] ? wad_path : "(unsaved)";
    char right[256];
    snprintf(right, sizeof right, "%s%s   %d buddy(ies)",
             fn, wad_modified ? " *" : "", n_buddies);
    float w = (float)strlen(right) * FONT_CW;
    text(WINW - PAD - w, 10, right, 200, 200, 210);
    rect(0, HEADER_H, WINW, 1, 60, 60, 80);
}

// Top-left panel: the opened WAD's lump directory (what's actually in the file),
// so you can see the WAD's contents.  BUDDYDEF is highlighted.  Wheel-scrolls.
static void draw_wad_contents(void)
{
    float x = PAD, y = WADLIST_Y, w = LIST_W, h = WADLIST_H;
    rect(x, y, w, h, 22, 22, 30);
    rect_outline(x, y, w, h, 60, 60, 80);

    if (!wad_loaded) {
        text(x + 8, y + 6, "WAD Contents", 160, 180, 220);
        text(x + 8, y + 34, "(no WAD open)", 120, 120, 140);
        return;
    }

    char title[48];
    snprintf(title, sizeof title, "WAD Contents (%u)", wad.numlumps);
    text(x + 8, y + 6, title, 160, 180, 220);

    int maxrows = (int)((h - 34) / ROWH);
    float ry = y + 30;
    for (int i = wad_scroll; i < (int)wad.numlumps && (i - wad_scroll) < maxrows; i++) {
        char nm[9]; memcpy(nm, wad.dir[i].name, 8); nm[8] = 0;
        int isbud = !strncmp(nm, "BUDDYDEF", 8);
        char row[64];
        snprintf(row, sizeof row, "%-8.8s %6u", nm, wad.dir[i].size);
        if (isbud) rect(x + 4, ry, w - 8, ROWH, 70, 60, 40);
        text_clip(x + 8, ry + 4, w - 16, row,
                  isbud ? 255 : 200, isbud ? 235 : 205, isbud ? 150 : 215);
        ry += ROWH;
    }
    if ((int)wad.numlumps > maxrows) {
        int last = wad_scroll + maxrows; if (last > (int)wad.numlumps) last = (int)wad.numlumps;
        char sc[40]; snprintf(sc, sizeof sc, "%d-%d/%u", wad_scroll + 1, last, wad.numlumps);
        text(x + w - (float)strlen(sc)*FONT_CW - 8, y + 6, sc, 120, 140, 160);
    }
}

static void draw_list(void)
{
    float x = PAD, y = BUDLIST_Y;
    float w = LIST_W;
    float h = BUDLIST_H;
    rect(x, y, w, h, 22, 22, 30);
    rect_outline(x, y, w, h, 60, 60, 80);
    text(x + 8, y + 6, "Buddies", 160, 180, 220);

    float ry = y + 30;
    for (int i = 0; i < n_buddies; i++) {
        SDL_FRect row = { x + 4, ry, w - 8, ROWH };
        if (i == sel) rect(row.x, row.y, row.w, row.h, 60, 90, 130);
        const char* nm = buddies[i].name[0] ? buddies[i].name : "(untitled)";
        char head[128];
        if (buddies[i].sprite[0])
            snprintf(head, sizeof head, "%.32s  [%.4s]", nm, buddies[i].sprite);
        else
            snprintf(head, sizeof head, "%.32s", nm);
        text(row.x + 8, row.y + 4, ellipsize(head, (int)row.w - 16),
             i == sel ? 255 : 220, i == sel ? 255 : 220, i == sel ? 220 : 220);
        ry += ROWH;
    }

    rect(btn.add.x, btn.add.y, btn.add.w, btn.add.h, 50, 90, 50);
    text(btn.add.x + 10, btn.add.y + 6, "+ New", 230, 255, 230);
    rect(btn.del.x, btn.del.y, btn.del.w, btn.del.h, 110, 50, 50);
    text(btn.del.x + 16, btn.del.y + 6, "Delete", 255, 230, 230);
    rect(btn.dup.x, btn.dup.y, btn.dup.w, btn.dup.h, 50, 70, 110);
    text(btn.dup.x + 16, btn.dup.y + 6, "Duplicate", 230, 240, 255);
}

static void draw_editor(void)
{
    float x = LIST_W + 2*PAD, y = EDIT_Y, w = WINW - x - PAD, h = EDIT_H;
    rect(x, y, w, h, 22, 22, 30);
    rect_outline(x, y, w, h, 60, 60, 80);
    text(x + 8, y + 6, "Editor", 160, 180, 220);

    if (sel < 0 || sel >= n_buddies) return;
    buddydef_entry_t* b = &buddies[sel];

    for (int i = 0; i < NFIELDS; i++) {
        const field_t* f = &FIELDS[i];
        int col, ric; float ry;
        editor_row_pos(i, &col, &ric, &ry);
        float colw = (w - 24) / 2.0f;
        float rx = x + 8 + col * colw;
        float vx = rx + LABEL_W + 2;
        float value_w = (col == 0 ? colw : (w - 8 - colw)) - LABEL_W - 16;

        text(rx, ry + 4, f->label, 200, 200, 220);
        char val[64];
        format_value(b, f, val, sizeof val);
        int hot = (active == i);
        if (hot) {
            SDL_FRect bg = { vx - 4, ry + 1, value_w + 4, ROWH - 4 };
            if (mode == 1) rect(bg.x, bg.y, bg.w, bg.h, 60, 60, 90);
            else           rect(bg.x, bg.y, bg.w, bg.h, 50, 50, 70);
        }
        if (mode == 1 && active == i) {
            text(vx, ry + 4, editbuf, 255, 235, 160);
            float cw = (float)strlen(editbuf) * FONT_CW;
            text(vx + cw, ry + 4, "_", 255, 255, 160);
        } else {
            text(vx, ry + 4, ellipsize(val, (int)value_w), 255, 235, 150);
        }
    }
}

static void draw_preview(void)
{
    if (sel < 0 || sel >= n_buddies) return;
    float x = LIST_W + 2*PAD, y = PREV_Y, w = WINW - x - PAD, h = PREV_H;
    rect(x, y, w, h, 14, 14, 24);
    rect_outline(x, y, w, h, 60, 60, 80);
    text(x + 8, y + 6, "Preview (what gets written to the WAD)", 160, 180, 220);

    char* txt = buddydef_serialize(&buddies[sel]);
    if (!txt) return;
    float cy = y + 30;
    float maxx = (x + w - 12) - (x + 8);
    char* line = txt;
    int line_count = 0, max_lines = (int)((h - 36) / FONT_CH);
    while (*line && line_count < max_lines) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        text_clip(x + 8, cy, maxx, line, 180, 200, 220);
        cy += FONT_CH;
        line_count++;
        if (!nl) break;
        *nl = '\n';
        line = nl + 1;
    }
    free(txt);
}

static void draw_footer(void)
{
    rect(0, WINH - FOOTER_H, WINW, 20, 32, 32, 40);
    rect(0, WINH - FOOTER_H + 20, WINW, 1, 60, 60, 80);
    if (status[0]) text(PAD, WINH - FOOTER_H + 4, status, 200, 220, 200);

    rect(btn.open.x,   btn.open.y,   btn.open.w,   btn.open.h,   50, 80, 110);
    text(btn.open.x + 18, btn.open.y + 6, "Open WAD", 230, 240, 255);
    rect(btn.newwad.x, btn.newwad.y, btn.newwad.w, btn.newwad.h, 80, 80, 110);
    text(btn.newwad.x + 12, btn.newwad.y + 6, "New WAD", 230, 240, 255);
    rect(btn.save.x,   btn.save.y,   btn.save.w,   btn.save.h,   50, 110, 50);
    text(btn.save.x + 28, btn.save.y + 6, "Save", 230, 255, 230);
    rect(btn.saveas.x, btn.saveas.y, btn.saveas.w, btn.saveas.h, 70, 110, 50);
    text(btn.saveas.x + 14, btn.saveas.y + 6, "Save As", 230, 255, 230);
    rect(btn.quit.x,   btn.quit.y,   btn.quit.w,   btn.quit.h,   110, 50, 50);
    text(btn.quit.x + 28, btn.quit.y + 6, "Quit", 255, 230, 230);
}

static void draw(void)
{
    rect(0, 0, WINW, WINH, 16, 16, 22);
    draw_header();
    draw_wad_contents();
    draw_list();
    draw_editor();
    draw_preview();
    draw_footer();
    SDL_RenderPresent(ren);
}

// ----------------------------------------------------------------- input
static int hit(float mx, float my, SDL_FRect r) { return mx >= r.x && mx < r.x+r.w && my >= r.y && my < r.y+r.h; }

static int field_at(float mx, float my)
{
    if (sel < 0 || sel >= n_buddies) return -1;
    float x = LIST_W + 2*PAD, w = WINW - x - PAD;
    if (mx < x || mx >= x + w) return -1;
    for (int i = 0; i < NFIELDS; i++) {
        int col, ric; float ry;
        editor_row_pos(i, &col, &ric, &ry);
        float colw = (w - 24) / 2.0f;
        float rx = x + 8 + col * colw;
        if (my >= ry && my < ry + ROWH) {
            if (mx >= rx && mx < rx + colw - 4) return i;
        }
    }
    return -1;
}

static int list_at(float mx, float my)
{
    float x = PAD, y = BUDLIST_Y;
    float w = LIST_W;
    float list_h = BUDLIST_H;
    if (mx < x || mx >= x + w || my < y + 30 || my >= y + list_h) return -1;
    int idx = (int)((my - (y + 30)) / ROWH);
    if (idx < 0 || idx >= n_buddies) return -1;
    return idx;
}

// Hit-test inside a value cell so we can split it into "<" (decrement) and ">"
// (increment) halves.  mx is in window coordinates; vx/val_size describe the
// value cell.
static int value_x(float mx, float vx, int est_value_px)
{
    return mx < vx + est_value_px / 2.0f ? -1 : 1;
}

static void begin_edit_text(int field_idx)
{
    if (sel < 0 || sel >= n_buddies) return;
    active = field_idx;
    mode = 1;
    const field_t* f = &FIELDS[field_idx];
    const char* cur = field_value(&buddies[sel], f);
    int cap = f->text_max;
    if (cap > (int)sizeof editbuf - 1) cap = (int)sizeof editbuf - 1;
    editbuf_max = cap;
    snprintf(editbuf, sizeof editbuf, "%s", cur);
    SDL_StartTextInput(win);
    snprintf(status, sizeof status, "Editing '%s' -- Enter to confirm, Esc to cancel.", f->label);
}

static void commit_edit_text(void)
{
    if (active < 0) return;
    const field_t* f = &FIELDS[active];
    buddydef_entry_t* b = &buddies[sel];
    char* dst = field_ptr(b, f);
    snprintf(dst, editbuf_max + 1, "%s", editbuf);
    set_dirty(b, f);
    record_change();
    SDL_StopTextInput(win);
    mode = 0;
    active = -1;
    snprintf(status, sizeof status, "Saved %s.", f->label);
}

static void cancel_edit(void)
{
    if (mode == 1) SDL_StopTextInput(win);
    mode = 0;
    active = -1;
    snprintf(status, sizeof status, "Cancelled.");
}

static void cycle_choice(int field_idx, int dir)
{
    buddydef_entry_t* b = &buddies[sel];
    const field_t* f = &FIELDS[field_idx];
    char* dst = field_ptr(b, f);
    int cur = 0;
    for (int i = 0; i < f->nchoices; i++)
        if (!strcmp(dst, f->choices[i])) { cur = i; break; }
    cur = (cur + dir + f->nchoices) % f->nchoices;
    snprintf(dst, f->text_max + 1, "%s", f->choices[cur]);
    set_dirty(b, f);
    record_change();
}

static void inc_int(int field_idx, int dir)
{
    buddydef_entry_t* b = &buddies[sel];
    const field_t* f = &FIELDS[field_idx];
    int* v = (int*)((char*)b + f->text_off);
    *v += dir;
    if (*v < f->vmin) *v = f->vmin;
    if (*v > f->vmax) *v = f->vmax;
    set_dirty(b, f);
    record_change();
}

static void click_value(float mx, float my)
{
    int f = field_at(mx, my);
    if (f < 0) return;
    const field_t* fd = &FIELDS[f];
    if (fd->kind == F_CHOICE) {
        int col, ric; float ry;
        editor_row_pos(f, &col, &ric, &ry);
        float x = LIST_W + 2*PAD, w = WINW - x - PAD;
        float colw = (w - 24) / 2.0f;
        float rx = x + 8 + col * colw;
        float vx = rx + LABEL_W + 2;
        char val[64];
        format_value(&buddies[sel], fd, val, sizeof val);
        float vw = (float)strlen(val) * FONT_CW;
        cycle_choice(f, value_x(mx, vx, (int)vw));
        return;
    }
    if (fd->kind == F_INT) {
        int col, ric; float ry;
        editor_row_pos(f, &col, &ric, &ry);
        float x = LIST_W + 2*PAD, w = WINW - x - PAD;
        float colw = (w - 24) / 2.0f;
        float rx = x + 8 + col * colw;
        float vx = rx + LABEL_W + 2;
        char val[64];
        format_value(&buddies[sel], fd, val, sizeof val);
        float vw = (float)strlen(val) * FONT_CW;
        inc_int(f, value_x(mx, vx, (int)vw));
        return;
    }
    begin_edit_text(f);
}

static void click_main(float mx, float my)
{
    if (hit(mx, my, btn.open))   { open_dialog(); return; }
    if (hit(mx, my, btn.newwad)) { new_wad_session(); return; }
    if (hit(mx, my, btn.save))   { save_wad(NULL); return; }
    if (hit(mx, my, btn.saveas)) { save_dialog(); return; }
    if (hit(mx, my, btn.quit)) { SDL_Event q = {.type = SDL_EVENT_QUIT}; SDL_PushEvent(&q); return; }
    if (hit(mx, my, btn.add)) {
        buddies = (buddydef_entry_t*)realloc(buddies, (n_buddies + 1) * sizeof *buddies);
        buddydef_defaults(&buddies[n_buddies]);
        buddies[n_buddies].set_name = 1;
        sel = n_buddies++;
        record_change();
        snprintf(status, sizeof status, "Added new buddy.");
        return;
    }
    if (hit(mx, my, btn.del)) {
        if (n_buddies <= 1) { snprintf(status, sizeof status, "Need at least one buddy."); return; }
        for (int i = sel; i < n_buddies - 1; i++) buddies[i] = buddies[i+1];
        n_buddies--;
        if (sel >= n_buddies) sel = n_buddies - 1;
        record_change();
        snprintf(status, sizeof status, "Deleted buddy.");
        return;
    }
    if (hit(mx, my, btn.dup)) {
        buddies = (buddydef_entry_t*)realloc(buddies, (n_buddies + 1) * sizeof *buddies);
        buddies[n_buddies] = buddies[sel];
        sel = n_buddies++;
        record_change();
        snprintf(status, sizeof status, "Duplicated buddy.");
        return;
    }
    int li = list_at(mx, my);
    if (li >= 0) { sel = li; active = -1; mode = 0; SDL_StopTextInput(win); return; }
    click_value(mx, my);
}

// ----------------------------------------------------------------- main
int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    SDL_SetMainReady();

    recompute_layout();

    if (argc >= 2) load_wad(argv[1]);
    else           new_wad_session();

    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    win = SDL_CreateWindow("MyBuddy - BUDDYDEF Editor", WINW, WINH, 0);
    {
        SDL_Surface* icon = SDL_CreateSurfaceFrom(
            BUDDYDOOM_ICON_W, BUDDYDOOM_ICON_H, SDL_PIXELFORMAT_RGBA32,
            (void*)buddydoom_icon_rgba, BUDDYDOOM_ICON_W*4);
        if (icon) { SDL_SetWindowIcon(win, icon); SDL_DestroySurface(icon); }
    }
    ren = SDL_CreateRenderer(win, NULL);
    font_init();

    int run = 1;
    while (run) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) break;
        switch (e.type) {
        case SDL_EVENT_QUIT:
            run = 0;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (mode == 1) commit_edit_text();
                else           click_main(e.button.x, e.button.y);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (mode == 0) {
                float mmx = 0, mmy = 0; SDL_GetMouseState(&mmx, &mmy);
                int over_wad = wad_loaded && mmx >= PAD && mmx < PAD + LIST_W
                             && mmy >= WADLIST_Y && mmy < WADLIST_Y + WADLIST_H;
                if (over_wad) {
                    int maxrows = (int)((WADLIST_H - 34) / ROWH);
                    if (e.wheel.y > 0) { if (wad_scroll > 0) wad_scroll--; }
                    else if (wad_scroll < (int)wad.numlumps - maxrows) wad_scroll++;
                } else {
                    if (e.wheel.y > 0) { if (sel > 0) sel--; }
                    else               { if (sel < n_buddies - 1) sel++; }
                }
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (mode == 1) {
                int avail = editbuf_max - (int)strlen(editbuf);
                if ((int)strlen(e.text.text) <= avail) strcat(editbuf, e.text.text);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (mode == 1) {
                if (e.key.key == SDLK_BACKSPACE) {
                    int n = (int)strlen(editbuf); if (n) editbuf[n-1] = 0;
                } else if (e.key.key == SDLK_RETURN) {
                    commit_edit_text();
                } else if (e.key.key == SDLK_ESCAPE) {
                    cancel_edit();
                }
            } else {
                if (e.key.key == SDLK_ESCAPE) { run = 0; }
                else if (e.key.key == SDLK_S && (e.key.mod & SDL_KMOD_CTRL)) { save_wad(NULL); }
                else if (e.key.key == SDLK_UP) { if (sel > 0) sel--; }
                else if (e.key.key == SDLK_DOWN) { if (sel < n_buddies - 1) sel++; }
                else if (e.key.key == SDLK_N && (e.key.mod & SDL_KMOD_CTRL)) {
                    buddies = (buddydef_entry_t*)realloc(buddies, (n_buddies + 1) * sizeof *buddies);
                    buddydef_defaults(&buddies[n_buddies]);
                    buddies[n_buddies].set_name = 1;
                    sel = n_buddies++;
                    record_change();
                }
                else if (e.key.key == SDLK_DELETE) {
                    if (n_buddies > 1) {
                        for (int i = sel; i < n_buddies - 1; i++) buddies[i] = buddies[i+1];
                        n_buddies--;
                        if (sel >= n_buddies) sel = n_buddies - 1;
                        record_change();
                    }
                }
            }
            break;
        }

        // Apply an async file-dialog result (stashed by dialog_cb).
        if (pending_action == PENDING_OPEN)        { load_wad(pending_path); pending_action = PENDING_NONE; }
        else if (pending_action == PENDING_SAVE)   { save_wad(pending_path); pending_action = PENDING_NONE; }
        else if (pending_action == PENDING_CANCEL) { snprintf(status, sizeof status, "Dialog cancelled."); pending_action = PENDING_NONE; }

        draw();
    }
    free(buddies);
    if (wad_loaded) buddydef_wad_free(&wad);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
