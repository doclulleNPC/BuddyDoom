// extractor -- graphical (SDL3) asset extractor for BuddyDoom.
//
// Replaces the Python asset-extraction scripts (extract_heretic_monsters.py,
// extract_hexen.py, extract_freedoom2.py, extract_doom2.py): reads YOUR OWN
// heretic.wad / hexen.wad / freedoom2.wad / doom2.wad and builds the
// palette-converted, renamed monster asset PWADs the engine loads
// (hereticstuff.wad / hexenstuff.wad / freedoomstuff.wad / doom2stuff.wad in
// run/ID0/).  Nothing is downloaded or redistributed -- it only re-packs assets
// from IWADs you already have.
//
// The DOOM2 source is special: DOOM2 shares DOOM1's palette, so it copies the
// DOOM2-exclusive monsters + super shotgun VERBATIM under their real names into
// doom2stuff.wad, which d_main.c auto-overlays on a DOOM1 launch so the director
// can spawn DOOM2 monsters and the SSG works (see extract_doom2 below).
//
// UI: a dropdown of the source IWADs that are actually present (scanned at
// startup from run/ and run/ID0/), and an Extract button underneath.  Text is
// drawn from the baked font atlas (tools/font_atlas.h), so there are no deps
// beyond SDL3 -- exactly like gpumon_sdl.c / launcher.c.
//
// Build: tools/build_extractor.sh (Linux/macOS) / build_extractor_win.sh (MinGW)
//        or tools/Makefile.msvc / CMakeLists.txt.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

#include "font_atlas.h"
#include "../files/buddydoom_icon.h"
#include "wadpng.h"		// shared Doom patch -> PNG (mirrors tools/wadpng.py)
#include "wadcodes.h"		// shared sprite-name collision policy	// shared 64x64 RGBA window icon (from buddydoom.ico)

#ifdef _WIN32
#define strcasecmp _stricmp	// MSVC has no strcasecmp (POSIX); _stricmp is the equivalent
#endif

#define WINW 560
#define WINH 460

#define PAD    16
#define DD_Y   96
#define DD_H   26
#define BTN_Y  132
#define BTN_H  34
#define LOG_Y  190

static SDL_Window*   win;
static SDL_Renderer* ren;
static SDL_Texture*  font;

// ================================================================= WAD I/O
//
// A WAD is a 12-byte header (magic, lump count, directory offset) followed by
// the lump data and then a directory of 16-byte entries (filepos, size, name8).
// Lump names are up to 8 bytes, NUL-padded (the trailing NUL is the terminator).

typedef struct {
    unsigned char* data;
    long           len;
    int            n;
    struct { char name[9]; int pos, size; } *dir;
} wad_t;

static unsigned int rd32(const unsigned char* p)
{ return (unsigned)p[0] | ((unsigned)p[1]<<8) | ((unsigned)p[2]<<16) | ((unsigned)p[3]<<24); }

static void wad_free(wad_t* w)
{
    if (!w) return;
    free(w->data); free(w->dir); free(w);
}

// Load a WAD file fully into memory + parse its directory.  NULL on any error.
static wad_t* wad_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len < 12) { fclose(f); return NULL; }
    unsigned char* data = malloc(len);
    if (!data || fread(data, 1, len, f) != (size_t)len) { free(data); fclose(f); return NULL; }
    fclose(f);
    if (memcmp(data, "IWAD", 4) && memcmp(data, "PWAD", 4)) { free(data); return NULL; }

    int   n   = (int)rd32(data + 4);
    long  off = (long)rd32(data + 8);
    if (n < 0 || off < 0 || off + (long)n*16 > len) { free(data); return NULL; }

    wad_t* w = calloc(1, sizeof *w);
    w->data = data; w->len = len; w->n = n;
    w->dir = calloc(n ? n : 1, sizeof *w->dir);
    for (int i = 0; i < n; i++) {
        const unsigned char* e = data + off + i*16;
        w->dir[i].pos  = (int)rd32(e);
        w->dir[i].size = (int)rd32(e + 4);
        memcpy(w->dir[i].name, e + 8, 8);
        w->dir[i].name[8] = 0;                 // 8-char names have no in-band NUL
    }
    return w;
}

static int wad_find(wad_t* w, const char* name)
{
    for (int i = 0; i < w->n; i++)
        if (!strcmp(w->dir[i].name, name)) return i;
    return -1;
}

static const unsigned char* wad_lump(wad_t* w, const char* name, int* size)
{
    int i = wad_find(w, name);
    if (i < 0) return NULL;
    if (size) *size = w->dir[i].size;
    return w->data + w->dir[i].pos;
}

// ----------------------------------------------------------------- output WAD
typedef struct { char name[9]; unsigned char* data; int size; } olump_t;
typedef struct { olump_t* v; int n, cap; } obuf_t;

// Append a lump (name capped/padded to 8, data COPIED so the source WAD can go).
static void oadd(obuf_t* o, const char* name, const unsigned char* data, int size)
{
    if (o->n == o->cap) { o->cap = o->cap ? o->cap*2 : 64; o->v = realloc(o->v, o->cap*sizeof *o->v); }
    olump_t* L = &o->v[o->n++];
    memset(L->name, 0, sizeof L->name);
    strncpy(L->name, name, 8);
    L->size = size;
    L->data = size ? malloc(size) : NULL;
    if (size) memcpy(L->data, data, size);
}

static void ofree(obuf_t* o) { for (int i=0;i<o->n;i++) free(o->v[i].data); free(o->v); o->v=NULL; o->n=o->cap=0; }

static void wr32(FILE* f, unsigned int v)
{ unsigned char b[4]={v&255,(v>>8)&255,(v>>16)&255,(v>>24)&255}; fwrite(b,1,4,f); }

// Write `o` as a PWAD.  Returns 1 on success.
static int wad_write(const char* path, obuf_t* o)
{
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    unsigned int diroff = 12;
    for (int i = 0; i < o->n; i++) diroff += o->v[i].size;
    fwrite("PWAD", 1, 4, f);
    wr32(f, o->n);
    wr32(f, diroff);
    for (int i = 0; i < o->n; i++)
        if (o->v[i].size) fwrite(o->v[i].data, 1, o->v[i].size, f);
    unsigned int pos = 12;
    for (int i = 0; i < o->n; i++) {
        wr32(f, pos);
        wr32(f, o->v[i].size);
        char nm[8]; memset(nm, 0, 8); memcpy(nm, o->v[i].name, strlen(o->v[i].name) < 8 ? strlen(o->v[i].name) : 8);
        fwrite(nm, 1, 8, f);
        pos += o->v[i].size;
    }
    fclose(f);
    return 1;
}

// ----------------------------------------------------------------- palette/pixels
static const unsigned char* get_playpal(wad_t* w)
{ int sz; const unsigned char* p = wad_lump(w, "PLAYPAL", &sz); return (p && sz >= 768) ? p : NULL; }

// Emit one sprite lump as PNG in the SOURCE game's palette (tools/wadpng.c).
//
// The old path palette-QUANTISED every borrowed sprite into the DOOM PLAYPAL and
// baked that in permanently.  BuddyDoom reads PNG sprites natively and matches them
// into whatever IWAD is actually running -- keeping a full-colour copy for the
// truecolor path -- so PNG loses nothing, and compresses smaller than the raw
// column format.  Offsets ride along in a grAb chunk.
//
// Returns 1 if the lump was written.  A lump that is not a valid patch (a sound
// whose name happens to share a sprite's 4-char prefix, e.g. hexen.wad's MAGE4 under
// the MAGE code) is REJECTED rather than dumped into the sprite namespace as junk.
static int emit_sprite(obuf_t* o, const char* newname,
                       const unsigned char* raw, int size,
                       const unsigned char* srcpal, int* n_skip)
{
    int            len = 0;
    unsigned char* png = wadpng_from_patch(raw, size, srcpal, &len);
    if (!png) { if (n_skip) (*n_skip)++; return 0; }
    oadd(o, newname, png, len);
    free(png);
    return 1;
}

static int is_dmx(const unsigned char* raw, int size)
{ return size >= 8 && raw[0] == 0x03 && raw[1] == 0x00; }

// ================================================================= rename tables
//
// These MUST stay in sync with the C ports (files/heretic.c, files/freedoom.c,
// files/hexen.c) that reference the renamed sprite codes -- they are copied
// verbatim from the Python extractors they replace.

// --- Heretic: monster/projectile 4-char code -> DOOM-side code (heretic.c) ---
static const char* HERETIC_RENAME[][2] = {
    {"IMPX","HIMP"},{"MUMM","HMUM"},{"FX15","HMUF"},{"KNIG","HKNI"},{"SPAX","HKAX"},
    {"RAXE","HKRX"},{"BEAS","HBEA"},{"FRB1","HBEB"},{"CLNK","HCLK"},{"WZRD","HWIZ"},
    {"FX11","HWIB"},{"SNKE","HSNK"},{"SNFX","HSNB"},{"HEAD","HIRO"},{"FX05","HIRB"},
    {"FX06","HIRW"},{"FX07","HIRX"},{"MNTR","HMIN"},{"FX12","HMNA"},{"FX13","HMNB"},
    {"FX14","HMNC"},{"SRCR","HSR1"},{"SOR2","HSR2"},{"FX16","HSRB"},{"CHKN","HCHK"},
    // Heretic artifacts: no DOOM collision, kept as-is (same SPR_ codes as crispy-doom).
    {"PTN1","PTN1"},{"PTN2","PTN2"},{"SPHL","SPHL"},{"PWBK","PWBK"},{"TRCH","TRCH"},
    {"FBMB","FBMB"},{"EGGC","EGGC"},{"SOAR","SOAR"},{"INVU","INVU"},{"INVS","INVS"},
    {"ATLP","ATLP"},
    {NULL,NULL}
};
// DMX sound name substrings copied (DS-prefixed) for the Heretic monsters.
static const char* HERETIC_SND[] = {
    "imp","mum","bst","clk","snk","kgt","wiz","hed","minsit","minat","mindth",
    "minact","minpai","sbtsit","sorzap","sorsit","soratk","sorpai","soract", NULL
};

// --- Freedoom2: DOOM2-exclusive 4-char code -> F* code (freedoom.c) ----------
static const char* FREEDOOM_RENAME[][2] = {
    {"SKEL","FSKE"},{"FATT","FFAT"},{"VILE","FVIL"},{"BSPI","FBSP"},{"CPOS","FCPO"},
    {"BOS2","FBO2"},{"PAIN","FPAI"},{"SSWV","FSSW"},{"KEEN","FKEE"},
    {"FATB","FFAB"},{"FBXP","FFBX"},{"MANF","FMAN"},{"FIRE","FFIR"},
    {"APLS","FAPL"},{"APBX","FAPB"},
    {NULL,NULL}
};

// --- Hexen: monster + weapon sprite codes (renamed into an "X.." namespace) --
static const char* HEXEN_MONSTER_SPR[] = {
    "ETTN","ETTB","CENT","CTXD","CTFX","CTDP","DEMN","DEMA","DEMB","DEMC","DEMD",
    "DEME","DMFX","DEM2","DMBA","DMBB","DMBC","DMBD","DMBE","D2FX","WRTH","WRT2",
    "WRBL","MNTR","FX12","FX13","MNSM","SSPT","SSDV","SSXD","SSFX","BISH","BPFX",
    "DRAG","DRFX","FDMN","FDMB","ICEY","ICPR","ICWS","ICEC","SORC","SBMP","SBS1",
    "SBS2","SBS3","SBS4","SBMB","SBMG","SBFX","KORX","ABAT","PIGY","FDTH",
    // APPEND-ONLY below this line: hexen_make_rename() assigns names in list
    // order, so inserting a code EARLIER can steal a name an existing one holds
    // and silently invalidate every SPR_X* in files/info.c.  Diff
    // tools/hexen_sprite_map.txt after every change.  Mirrors
    // MONSTER_SPRITES in tools/extract_hexen.py -- keep the two in step.
    "PLAY","CLER","MAGE",		// fighter/cleric/mage class bosses
    NULL
};
static const char* HEXEN_WEAPON_SPR[] = {
    "FPCH","WFAX","FAXE","FSFX","WFHM","FHMR","FHFX","FSRD","CMCE","WCSS","CSSF",
    "WCFM","CFLM","CFFX","CHLY","SPIR","MWND","WMLG","MLNG","MLFX","MLF2","MSTF",
    "MSP1","MSP2","CONE","SHEX","WFR1","WFR2","WFR3","WCH1","WCH2","WCH3","WMS1",
    "WMS2","WMS3","WPIG","WMCS","AFWP","ACWP","AMWP","AGER","AGR2","AGR3","AGR4", NULL
};
// Wired Hexen monster sounds: sfx short name -> source Hexen DMX lump.  Copied
// as "DS"+short so the engine's "ds%s" lookup finds them (files/hexen.c sfx_x_*).
static const char* HEXEN_SND_WIRED[][2] = {
    {"xetsit","cent2"},{"xetpai","cent1"},{"xetatk","ethit1"},{"xetdth","cntdth1"},
    {"xcesit","taur1"},{"xceact","taur2"},{"xcepai","taur4"},{"xceatk","centhit2"},
    {"xcedth","cntdth1"},{"xslatk","cntshld4"},
    {"xdesit","sbtsit5"},{"xdepai","minact1"},{"xdeatk","dematk2"},{"xdedth","sbtdth3"},
    {"xfdact","fired5"},{"xfdpai","fired2"},{"xfdatk","spit6"},{"xfddth","fired3"},{"xfdhit","firedhit"},
    {"xwrsit","raith5a"},{"xwract","raith3"},{"xwrpai","raith4a"},{"xwratk","raith1b"},{"xwrdth","rathdth2"},
    {"xbisit","syab2d"},{"xbiact","stb1d"},{"xbipai","bshpn1"},{"xbiatk","pop"},{"xbidth","bishdth1"},{"xbihit","bshhit2"},
    {"xicsit","frosty1"},{"xicatk","frosty2"},{"xichit","shards1b"},
    {"xstsit","wtrcrt7"},{"xstact","srfc3"},{"xstpai","serppn1"},{"xstatk","wtrswip"},{"xstdth","srpdth1"},{"xsthit","glbhit4"},
    {"xdrsit","dragsit1"},{"xdrpai","dragpn2"},{"xdratk","mage4"},{"xdrdth","dragdie2"},{"xdrhit","mageball"},
    {NULL,NULL}
};
// Hexen monster/weapon sound name substrings copied verbatim (reference).
static const char* HEXEN_SND_KW[] = {
    "cent","cnt","eth","taur","minact","mindth","minpain","minsit","kor","serp",
    "srp","demat","raith","rath","wrbl","drag","sor","sbt","bish","bsh","fired",
    "fdmn","pig","icedth","icemv","icebrk","frosty","icpr","vamp","bats","glbh",
    "srfc","mumpun","squeal","slurp","shlurp","axe","ham","hmhit","punch","sword",
    "holy","spirt","clhmm","mageball","wand","blastr","mage4","cone3","gnt",
    "wepele","strike1","strike3","fgt","mgpain","mgdth","mggrunt","mgxdth","mgfall",
    "mghmm","mgcdth","clxdth","plrdth","plrpain","plrburn","plrcdth",
    "bite","impact",		// PigAttack (BITE4) + MaulatorMissileHit (IMPACT3)
    NULL
};

#define AISTUFF_NOTE "BuddyDoom internal asset pack -- loaded by the game, not a user PWAD\n"

// ================================================================= sources
enum { K_HERETIC, K_HEXEN, K_FREEDOOM, K_DOOM2, K_STRIFE, K_FREEDOOM2 };

typedef struct {
    const char* src;      // source IWAD basename (lowercase)
    const char* out;      // output PWAD basename (written to run/ID0/)
    const char* label;    // shown in the dropdown
    int         kind;
} source_t;

static const source_t SOURCES[] = {
    { "doom2.wad",     "doom2stuff.wad",     "DOOM2  \x1a  doom2stuff.wad (monsters + SSG in DOOM1)", K_DOOM2 },
    { "heretic.wad",   "hereticstuff.wad",   "Heretic  \x1a  hereticstuff.wad",   K_HERETIC  },
    { "hexen.wad",     "hexenstuff.wad",     "Hexen  \x1a  hexenstuff.wad",       K_HEXEN    },
    { "freedoom2.wad", "freedoomstuff.wad",  "FreeDoom2  \x1a  freedoomstuff.wad", K_FREEDOOM },
    { "freedoom2.wad", "freedoom2stuff.wad", "FreeDoom2  \x1a  freedoom2stuff.wad (monsters + SSG in FreeDoom1)", K_FREEDOOM2 },
    { "strife1.wad",   "strifestuff.wad",    "Strife  \x1a  strifestuff.wad",     K_STRIFE   },
};
#define NSOURCES ((int)(sizeof SOURCES / sizeof SOURCES[0]))

static int  avail[NSOURCES];          // indices into SOURCES[] that were found
static char avail_path[NSOURCES][600];// resolved absolute path of each found source
static int  navail = 0;
static int  sel = 0;                  // index into avail[] (the selected source)

static char g_rundir[512];            // dir the binary lives in (== run/)
static char g_id0[560];               // g_rundir + "/ID0" (where WADs live)

// Locate `basename` in run/ID0 then run/, trying a few case variants.  1 on hit.
static int find_wad(const char* basename, char* out, int n)
{
    const char* dirs[2] = { g_id0, g_rundir };
    char lo[64], up[64];
    snprintf(lo, sizeof lo, "%s", basename);
    snprintf(up, sizeof up, "%s", basename);
    for (char* p = lo; *p; p++) *p = (char)tolower((unsigned char)*p);
    for (char* p = up; *p; p++) *p = (char)toupper((unsigned char)*p);
    const char* names[3] = { basename, lo, up };
    for (int d = 0; d < 2; d++)
        for (int c = 0; c < 3; c++) {
            char full[600];
            snprintf(full, sizeof full, "%s/%s", dirs[d], names[c]);
            struct stat st;
            if (stat(full, &st) == 0 && (st.st_mode & S_IFREG)) {
                snprintf(out, n, "%s", full);
                return 1;
            }
        }
    return 0;
}

// The DOOM IWAD whose PLAYPAL is the palette-conversion target (+ base sprites).
static int find_base(char* out, int n)
{
    static const char* order[] = { "doom2.wad", "doom.wad", "plutonia.wad", "tnt.wad", NULL };
    for (int i = 0; order[i]; i++)
        if (find_wad(order[i], out, n)) return 1;
    return 0;
}

// The DOOM1 IWAD to diff against when building doom2stuff.wad -- what counts as
// "already present in DOOM1".  Prefer the full registered/retail doom.wad over
// shareware doom1.wad (whose sprite set is only episode 1, so it under-reports
// what DOOM1 has and would pull a few registered-DOOM1 sprites into the pack --
// harmless, just larger).
static int find_doom1(char* out, int n)
{
    static const char* order[] = { "doom.wad", "doomu.wad", "doom1.wad", NULL };
    for (int i = 0; order[i]; i++)
        if (find_wad(order[i], out, n)) return 1;
    return 0;
}

static void scan_sources(void)
{
    navail = 0;
    for (int i = 0; i < NSOURCES; i++) {
        char path[600];
        if (find_wad(SOURCES[i].src, path, sizeof path)) {
            avail[navail] = i;
            snprintf(avail_path[navail], sizeof avail_path[navail], "%s", path);
            navail++;
        }
    }
    if (sel >= navail) sel = 0;
}

// ================================================================= extractors
//
// One function per source, faithfully porting the matching Python script.  Each
// appends a human-readable report into `log` and returns 1 on success.

static void logf_(char* log, int cap, const char* fmt, ...)
{
    int len = (int)strlen(log);
    if (len >= cap-1) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(log + len, cap - len, fmt, ap);
    va_end(ap);
}

static long obuf_bytes(obuf_t* o)
{ long t = 0; for (int i=0;i<o->n;i++) t += o->v[i].size; return t; }

// --- Heretic: build hereticstuff.wad ----------------------------------------
static int extract_heretic(const char* srcpath, const char* outpath, char* log, int lcap)
{
    char basepath[600];
    if (!find_base(basepath, sizeof basepath)) {
        logf_(log, lcap, "ERROR: no DOOM IWAD found in ID0/ for the target palette.\n"
                         "  (need doom2.wad / doom.wad / plutonia.wad / tnt.wad)\n");
        return 0;
    }
    wad_t* h = wad_load(srcpath);
    wad_t* b = wad_load(basepath);
    if (!h) { logf_(log, lcap, "ERROR: cannot read %s\n", srcpath); wad_free(b); return 0; }
    if (!b) { logf_(log, lcap, "ERROR: cannot read base %s\n", basepath); wad_free(h); return 0; }
    // Sprites go out as PNG in the SOURCE game's own palette, so there is no
    // conversion table any more -- files/v_png.c matches them to whatever IWAD is
    // actually running, and keeps the full colour for the truecolor sprite path.
    // The base IWAD is still located above so the "which DOOM do you have" check
    // and its log line stay meaningful.
    const unsigned char* sp = get_playpal(h);
    if (!sp) { logf_(log, lcap, "ERROR: missing PLAYPAL\n"); wad_free(h); wad_free(b); return 0; }

    obuf_t o = {0};
    oadd(&o, "AISTUFF", (const unsigned char*)AISTUFF_NOTE, (int)strlen(AISTUFF_NOTE));
    oadd(&o, "S_START", NULL, 0);
    int n_spr = 0, n_skip = 0;
    for (int i = 0; i < h->n; i++) {
        const char* nm = h->dir[i].name;
        if (strlen(nm) <= 4) continue;
        char code[5]; memcpy(code, nm, 4); code[4] = 0;
        for (int r = 0; HERETIC_RENAME[r][0]; r++) {
            if (!strcmp(code, HERETIC_RENAME[r][0])) {
                char nn[9]; snprintf(nn, sizeof nn, "%s%s", HERETIC_RENAME[r][1], nm+4);
                n_spr += emit_sprite(&o, nn, h->data + h->dir[i].pos, h->dir[i].size,
                                     sp, &n_skip);
                break;
            }
        }
    }
    oadd(&o, "S_END", NULL, 0);

    int n_snd = 0;
    for (int i = 0; i < h->n; i++) {
        const unsigned char* raw = h->data + h->dir[i].pos;
        int sz = h->dir[i].size;
        if (!is_dmx(raw, sz)) continue;
        char low[16]; int j; for (j=0;j<15 && h->dir[i].name[j];j++) low[j]=(char)tolower((unsigned char)h->dir[i].name[j]); low[j]=0;
        int hit = 0;
        for (int k = 0; HERETIC_SND[k]; k++) if (strstr(low, HERETIC_SND[k])) { hit = 1; break; }
        if (!hit) continue;
        char nn[9]; snprintf(nn, sizeof nn, "DS%.6s", h->dir[i].name);
        oadd(&o, nn, raw, sz); n_snd++;
    }

    int ok = wad_write(outpath, &o);
    logf_(log, lcap, ok ? "wrote %s\n" : "ERROR: could not write %s\n", outpath);
    if (ok) {
        logf_(log, lcap, "  base palette : %s\n", strrchr(basepath,'/')?strrchr(basepath,'/')+1:basepath);
        logf_(log, lcap, "  sprites      : %d (converted + renamed)\n", n_spr);
        logf_(log, lcap, "  sounds       : %d (DS* lumps)\n", n_snd);
        logf_(log, lcap, "  total        : %d lumps, %.1f MB\n", o.n, obuf_bytes(&o)/1048576.0);
    }
    ofree(&o); wad_free(h); wad_free(b);
    return ok;
}

// --- Freedoom2: build freedoomstuff.wad -------------------------------------
static int extract_freedoom(const char* srcpath, const char* outpath, char* log, int lcap)
{
    wad_t* fw = wad_load(srcpath);
    if (!fw) { logf_(log, lcap, "ERROR: cannot read %s\n", srcpath); return 0; }
    int s0 = wad_find(fw, "S_START"), s1 = wad_find(fw, "S_END");
    if (s0 < 0 || s1 < 0 || s1 < s0) {
        logf_(log, lcap, "ERROR: freedoom2 has no S_START/S_END sprite range\n");
        wad_free(fw); return 0;
    }
    // DOOM1 sound diff (optional): keep only DOOM2-exclusive sounds.
    char d1path[600]; wad_t* d1 = NULL;
    if (find_wad("doom.wad", d1path, sizeof d1path)) d1 = wad_load(d1path);

    // Sprites go out as PNG in FreeDoom's own palette -- one pack format across
    // every *stuff.wad, and the engine matches them to whatever IWAD is running.
    const unsigned char* fpal = get_playpal(fw);
    if (!fpal) { logf_(log, lcap, "ERROR: missing PLAYPAL\n"); wad_free(fw); wad_free(d1); return 0; }

    obuf_t o = {0};
    oadd(&o, "AISTUFF", (const unsigned char*)AISTUFF_NOTE, (int)strlen(AISTUFF_NOTE));
    oadd(&o, "S_START", NULL, 0);
    int n_spr = 0, n_skip = 0;
    for (int i = s0+1; i < s1; i++) {
        const char* nm = fw->dir[i].name;
        if (strlen(nm) < 4) continue;
        char code[5]; memcpy(code, nm, 4); code[4] = 0;
        for (int r = 0; FREEDOOM_RENAME[r][0]; r++) {
            if (!strcmp(code, FREEDOOM_RENAME[r][0])) {
                char nn[9]; snprintf(nn, sizeof nn, "%s%s", FREEDOOM_RENAME[r][1], nm+4);
                n_spr += emit_sprite(&o, nn, fw->data + fw->dir[i].pos,
                                     fw->dir[i].size, fpal, &n_skip);
                break;
            }
        }
    }
    oadd(&o, "S_END", NULL, 0);

    int n_snd = 0;
    for (int i = 0; i < fw->n; i++) {
        const char* nm = fw->dir[i].name;
        const unsigned char* raw = fw->data + fw->dir[i].pos;
        int sz = fw->dir[i].size;
        if (!(nm[0]=='D' && (nm[1]=='S' || nm[1]=='P'))) continue;
        if (!is_dmx(raw, sz)) continue;
        if (d1 && wad_find(d1, nm) >= 0) continue;   // present in DOOM1 -> not exclusive
        oadd(&o, nm, raw, sz); n_snd++;
    }

    int ok = wad_write(outpath, &o);
    logf_(log, lcap, ok ? "wrote %s\n" : "ERROR: could not write %s\n", outpath);
    if (ok) {
        logf_(log, lcap, "  sound diff   : %s\n", d1 ? "doom.wad (exclusive only)" : "(none: all DS*/DP*)");
        logf_(log, lcap, "  sprites      : %d (renamed F*, PNG)\n", n_spr);
        logf_(log, lcap, "  sounds       : %d (orig names)\n", n_snd);
        logf_(log, lcap, "  total        : %d lumps, %.1f MB\n", o.n, obuf_bytes(&o)/1048576.0);
    }
    ofree(&o); wad_free(fw); wad_free(d1);
    return ok;
}

// --- DOOM2: build doom2stuff.wad (DOOM2 monsters + super shotgun for DOOM1) --
//
// DOOM2 uses the SAME palette as DOOM1, so its assets copy VERBATIM (no remap)
// under their REAL sprite/sound names.  We diff doom2.wad against a DOOM1 IWAD
// and keep only what DOOM1 lacks: the extra monsters (revenant, arch-vile,
// mancubus, arachnotron, chaingunner, hell knight, pain elemental, SS, Keen,
// boss brain + their projectiles) and the super shotgun (SHT2 first-person
// weapon + SGN2 pickup, and the DBOPN/DBLOAD/DBCLS/DSHTGN sounds).  Because the
// names are the genuine DOOM2 codes, the engine's existing MT_*/SPR_*/sfx_*
// definitions light up with NO info.c changes -- d_main.c auto-overlays this
// PWAD on a DOOM1 launch (doom2_overlay), the director's P_Director_Doom2Available
// probe (sprites[SPR_SKEL].numframes) sees the monsters, and g_game.c re-enables
// wp_supershotgun.  Unlike freedoomstuff.wad (which renames to an F* namespace so
// it can coexist with a real DOOM2 IWAD), this is only overlaid when the IWAD is
// DOOM1, so no rename is needed.
// Build a "phase 2 minus phase 1" overlay pack: the sprites/sounds a Doom-1-format
// IWAD does NOT have, under their NATIVE names, so the engine can overlay them onto
// that game (files/d_main.c, doom2_overlay) and the DOOM2 monsters + super shotgun
// work there.  Native names are deliberate -- unlike the *stuff.wad packs, this one
// is only ever loaded on top of the game it belongs to.
//
// `d1names` is the phase-1 IWAD to diff against, NULL for the id Software default
// (doom.wad / doomu.wad / doom1.wad).  Freedoom passes freedoom1.wad, which is why
// the two never mix: id art stays with Doom, Freedoom art stays with Freedoom.
static int extract_phase2(const char* srcpath, const char* outpath,
                          const char* const* d1names, char* log, int lcap)
{
    char d1path[600];
    int  gotd1 = 0;
    if (d1names) {
        for (int i = 0; d1names[i] && !gotd1; i++)
            gotd1 = find_wad(d1names[i], d1path, sizeof d1path);
    } else {
        gotd1 = find_doom1(d1path, sizeof d1path);
    }
    if (!gotd1) {
        logf_(log, lcap, "ERROR: no phase-1 IWAD found in ID0/ to diff against.\n"
                         "  (need %s -- cannot compute the missing-in-phase-1 set)\n",
              d1names ? d1names[0] : "doom.wad / doomu.wad / doom1.wad");
        return 0;
    }
    wad_t* d2 = wad_load(srcpath);
    wad_t* d1 = wad_load(d1path);
    if (!d2) { logf_(log, lcap, "ERROR: cannot read %s\n", srcpath); wad_free(d1); return 0; }
    if (!d1) { logf_(log, lcap, "ERROR: cannot read DOOM1 %s\n", d1path); wad_free(d2); return 0; }

    int s0 = wad_find(d2, "S_START"), s1 = wad_find(d2, "S_END");
    if (s0 < 0 || s1 < 0 || s1 < s0) {
        logf_(log, lcap, "ERROR: %s has no S_START/S_END sprite range\n", srcpath);
        wad_free(d2); wad_free(d1); return 0;
    }
    // Scan DOOM1's sprite range for the 4-char codes it already has.  Fall back
    // to the whole directory if its S_START/S_END markers are absent/odd.
    int d1s0 = wad_find(d1, "S_START"), d1s1 = wad_find(d1, "S_END");
    int lo = (d1s0 >= 0 && d1s1 > d1s0) ? d1s0 + 1 : 0;
    int hi = (d1s0 >= 0 && d1s1 > d1s0) ? d1s1     : d1->n;

    // PNG in the source game's palette, like every other pack.
    const unsigned char* p2pal = get_playpal(d2);
    if (!p2pal) { logf_(log, lcap, "ERROR: missing PLAYPAL\n"); wad_free(d2); wad_free(d1); return 0; }

    obuf_t o = {0};
    oadd(&o, "AISTUFF", (const unsigned char*)AISTUFF_NOTE, (int)strlen(AISTUFF_NOTE));
    oadd(&o, "S_START", NULL, 0);
    int n_spr = 0, n_skip = 0;
    for (int i = s0+1; i < s1; i++) {
        const char* nm = d2->dir[i].name;
        if (strlen(nm) < 4) continue;                 // markers / stray lumps
        int in_d1 = 0;                                // same 4-char code already in DOOM1?
        for (int j = lo; j < hi && !in_d1; j++)
            if (strlen(d1->dir[j].name) >= 4 && !strncmp(d1->dir[j].name, nm, 4)) in_d1 = 1;
        if (in_d1) continue;
        n_spr += emit_sprite(&o, nm, d2->data + d2->dir[i].pos, d2->dir[i].size,
                             p2pal, &n_skip);
    }
    oadd(&o, "S_END", NULL, 0);

    // DOOM2-exclusive sounds (DS*/DP* in doom2 but not doom1): the new monster
    // barks plus the super shotgun's DBOPN/DBLOAD/DBCLS/DSHTGN.
    int n_snd = 0;
    for (int i = 0; i < d2->n; i++) {
        const char* nm = d2->dir[i].name;
        const unsigned char* raw = d2->data + d2->dir[i].pos;
        int sz = d2->dir[i].size;
        if (!(nm[0]=='D' && (nm[1]=='S' || nm[1]=='P'))) continue;
        if (!is_dmx(raw, sz)) continue;
        if (wad_find(d1, nm) >= 0) continue;          // already in DOOM1 -> not exclusive
        oadd(&o, nm, raw, sz); n_snd++;
    }

    int ok = wad_write(outpath, &o);
    logf_(log, lcap, ok ? "wrote %s\n" : "ERROR: could not write %s\n", outpath);
    if (ok) {
        logf_(log, lcap, "  diff base    : %s\n", strrchr(d1path,'/')?strrchr(d1path,'/')+1:d1path);
        logf_(log, lcap, "  sprites      : %d (phase-2 only, PNG -- monsters + SSG)\n", n_spr);
        logf_(log, lcap, "  sounds       : %d (DOOM2-only DS*/DP*)\n", n_snd);
        logf_(log, lcap, "  total        : %d lumps, %.1f MB\n", o.n, obuf_bytes(&o)/1048576.0);
        logf_(log, lcap, "  -> auto-overlaid on a DOOM1 launch (d_main.c doom2_overlay).\n");
    }
    ofree(&o); wad_free(d2); wad_free(d1);
    return ok;
}

// Deterministic, collision-free 4-char rename into the "X" (heXen) namespace.
// stem = 'X' + code[:2]; 4th char tries code[2], code[3], then 2..9/A..Z.
// Hexen sprite rename, via the shared policy (tools/wadcodes.c).  `used` must be
// pre-seeded with every code the host IWADs already own -- checking only the codes
// we ourselves assign is not enough: Strife's BLOD/GIBS/SHRD/WATR are unknown to the
// engine's tables yet live in heretic.wad and hexen.wad, and a pack that kept such a
// name would clobber that game's art whenever it is loaded there.
static void hexen_make_rename(codeset_t* used, const char* code, char out[5])
{
    if (!wc_pick_rename("hexen", code, 'X', used, NULL, out))
        { memcpy(out, code, 4); out[4] = 0; }   // unreachable in practice
}

static int extract_doom2(const char* srcpath, const char* outpath, char* log, int lcap)
{ return extract_phase2(srcpath, outpath, NULL, log, lcap); }

// Freedoom Phase 2 minus Phase 1 -- the same overlay, built from Freedoom's own art.
static int extract_freedoom2(const char* srcpath, const char* outpath, char* log, int lcap)
{
    static const char* const fd1[] = { "freedoom1.wad", NULL };
    return extract_phase2(srcpath, outpath, fd1, log, lcap);
}

// --- Strife: build strifestuff.wad ------------------------------------------
//
// MONSTERS ONLY.  The sprite list is whatever files/strife_mon.c actually installs
// (every SPR_S_* it names), intersected with the engine's generated code/enum
// tables -- so it cannot drift from the port, and Strife's items/weapons/scenery,
// which were most of the collisions, never enter the pack.
//
// Strife was ported with its NATIVE 4-char codes, which is right in strife_mode but
// collides with DOOM/Heretic/Hexen everywhere else.  Both cures come from GZDoom's
// RenameSprites() (../gzdoom/src/d_main.cpp):
//   * RENAME when its StrifeRenames[] has a free spelling -- SPID -> STLK (Stalker),
//     MISL -> SMIS.  files/strife.c repoints the SPR_S_* slot outside strife_mode,
//     driven by the files/strife_ph.inc this job writes.
//   * OMIT when it does not.  GIBS/PLAY/POW1/SHT1/TFOG are generic effects the host
//     game already owns a lump for, so not shipping them leaves the actor drawing
//     the host's equivalent -- correct-looking, and it cannot shadow anything.
// See docs/BUDDY_SPRITE_COLLISIONS.md.

// Read "CODE" strings (want_quoted) or SPR_S_* identifiers out of a generated .inc.
// Returns the count, or -1 if the file is unreadable.
static int read_inc_list(const char* path, char (*out)[16], int cap, int want_quoted)
{
    FILE* f = fopen(path, "rb");
    char  line[512];
    int   n = 0;
    if (!f) return -1;
    while (n < cap && fgets(line, sizeof line, f)) {
        if (line[0] == '/' && line[1] == '/') continue;
        if (want_quoted) {
            char* q = strchr(line, '"');
            char* e;
            if (!q) continue;
            e = strchr(q + 1, '"');
            if (!e || e - q - 1 > 15) continue;
            memcpy(out[n], q + 1, (size_t)(e - q - 1));
            out[n][e - q - 1] = 0;
            n++;
        } else {
            char* q = strstr(line, "SPR_S_");
            char* e;
            if (!q) continue;
            for (e = q; *e && (isalnum((unsigned char)*e) || *e == '_'); e++) {}
            if (e - q > 15) continue;
            memcpy(out[n], q, (size_t)(e - q));
            out[n][e - q] = 0;
            n++;
        }
    }
    fclose(f);
    return n;
}

#define STRIFE_MAXSPR 400

static int extract_strife(const char* srcpath, const char* outpath, char* log, int lcap)
{
    static char codes[STRIFE_MAXSPR][16];       // native 4-char codes, enum order
    static char enums[STRIFE_MAXSPR][16];       // matching SPR_S_* names
    static char newc [STRIFE_MAXSPR][5];        // what each one ships as
    char        incpath[700], enumpath[700];
    int         ncodes, nenums, i, c;
    int         n_spr = 0, n_skip = 0, n_snd = 0, n_ren = 0, n_dropsnd = 0, n_omit = 0;
    static codeset_t used, own;
    wad_t*      sw;
    const unsigned char* spal;
    obuf_t      o = {0};
    FILE*       mf;

    snprintf(incpath,  sizeof incpath,  "%s/../files/strife_sprnames.inc", g_rundir);
    snprintf(enumpath, sizeof enumpath, "%s/../files/strife_spr.inc",      g_rundir);
    ncodes = read_inc_list(incpath,  codes, STRIFE_MAXSPR, 1);
    nenums = read_inc_list(enumpath, enums, STRIFE_MAXSPR, 0);
    if (ncodes > 0 && nenums == ncodes) {
        // Keep only the slots files/strife_mon.c installs -- the monster/boss art.
        char  monpath[700];
        char* mon;
        long  mlen;
        FILE* mfp;
        snprintf(monpath, sizeof monpath, "%s/../files/strife_mon.c", g_rundir);
        mfp = fopen(monpath, "rb");
        if (!mfp) {
            logf_(log, lcap, "ERROR: need files/strife_mon.c to pick the monster sprites\n");
            return 0;
        }
        fseek(mfp, 0, SEEK_END); mlen = ftell(mfp); fseek(mfp, 0, SEEK_SET);
        mon = (char*) malloc((size_t)mlen + 1);
        if (!mon || fread(mon, 1, (size_t)mlen, mfp) != (size_t)mlen) {
            fclose(mfp); free(mon);
            logf_(log, lcap, "ERROR: cannot read files/strife_mon.c\n");
            return 0;
        }
        mon[mlen] = 0;
        fclose(mfp);
        {   int keep = 0, k;
            for (k = 0; k < ncodes; k++) {
                // whole-identifier match, so SPR_S_ROB1 never matches SPR_S_ROB10
                const char* q = mon;
                int         hit = 0;
                size_t      el = strlen(enums[k]);
                while ((q = strstr(q, enums[k])) != NULL) {
                    char after = q[el];
                    if (!(isalnum((unsigned char)after) || after == '_')) { hit = 1; break; }
                    q += el;
                }
                if (!hit) continue;
                if (keep != k) {
                    memcpy(codes[keep], codes[k], sizeof codes[0]);
                    memcpy(enums[keep], enums[k], sizeof enums[0]);
                }
                keep++;
            }
            ncodes = nenums = keep;
        }
        free(mon);
    }
    if (ncodes <= 0 || nenums != ncodes) {
        logf_(log, lcap, "ERROR: need files/strife_sprnames.inc + strife_spr.inc\n"
                         "  (run the extractor from the BuddyDoom tree; got %d / %d)\n",
              ncodes, nenums);
        return 0;
    }

    sw = wad_load(srcpath);
    if (!sw) { logf_(log, lcap, "ERROR: cannot read %s\n", srcpath); return 0; }
    spal = get_playpal(sw);
    if (!spal) { logf_(log, lcap, "ERROR: missing PLAYPAL\n"); wad_free(sw); return 0; }

    // Reserve every code the other games own.  The engine's tables alone are not
    // enough: BLOD/GIBS/SHRD/WATR are unknown to sprnames yet live in heretic.wad
    // and hexen.wad, so keeping Strife's spelling would clobber their art there.
    memset(&used, 0, sizeof used);
    memset(&own,  0, sizeof own);
    wc_reserve_from_dir(g_id0,    "strife1.wad", &used);
    wc_reserve_from_dir(g_rundir, "strife1.wad", &used);
    {   // buddydoom.wad already ships the Stalker under ZDoom's STLK -- same art,
        // so re-using STLK here is sharing, not a clash.
        char bd[700];
        snprintf(bd, sizeof bd, "%s/buddydoom.wad", g_id0);    wc_codes_in_wad(bd, &own);
        snprintf(bd, sizeof bd, "%s/buddydoom.wad", g_rundir); wc_codes_in_wad(bd, &own);
    }

    for (c = 0; c < ncodes; c++) {
        if (!wc_has(&used, codes[c])) {                 // unique already -- keep it
            memcpy(newc[c], codes[c], 4); newc[c][4] = 0;
            wc_add(&used, newc[c]);
        } else if (wc_pick_rename("strife", codes[c], 0, &used, &own, newc[c])) {
            n_ren++;                                    // GZDoom has a free spelling
        } else {
            newc[c][0] = 0;                             // omit: host game owns it
            n_omit++;
        }
    }

    oadd(&o, "AISTUFF", (const unsigned char*)AISTUFF_NOTE, (int)strlen(AISTUFF_NOTE));
    oadd(&o, "S_START", NULL, 0);
    for (i = 0; i < sw->n; i++) {
        const char* nm = sw->dir[i].name;
        if (strlen(nm) <= 4) continue;
        for (c = 0; c < ncodes; c++) {
            char nn[9];
            if (strncmp(nm, codes[c], 4)) continue;
            if (!newc[c][0]) break;                     // omitted -- host game's lump wins
            snprintf(nn, sizeof nn, "%s%s", newc[c], nm + 4);
            n_spr += emit_sprite(&o, nn, sw->data + sw->dir[i].pos, sw->dir[i].size,
                                 spal, &n_skip);
            break;
        }
    }
    oadd(&o, "S_END", NULL, 0);

    // Strife's own DS* sounds, verbatim -- minus any name DOOM already owns, which
    // would silently replace DOOM's audio (DSOOF, DSBAREXP, ...).  The DOOM lump of
    // the same name is a fine stand-in for the Strife actor.
    {
        char   bp[600];
        wad_t* dm = find_base(bp, sizeof bp) ? wad_load(bp) : NULL;
        for (i = 0; i < sw->n; i++) {
            const char* nm = sw->dir[i].name;
            if (strncmp(nm, "DS", 2)) continue;
            if (!is_dmx(sw->data + sw->dir[i].pos, sw->dir[i].size)) continue;
            if (dm && wad_find(dm, (char*)nm) >= 0) { n_dropsnd++; continue; }
            oadd(&o, nm, sw->data + sw->dir[i].pos, sw->dir[i].size); n_snd++;
        }
        wad_free(dm);
    }

    if (!wad_write(outpath, &o)) {
        logf_(log, lcap, "ERROR: cannot write %s\n", outpath);
        ofree(&o); wad_free(sw); return 0;
    }

    // The engine-side remap table (files/strife.c #includes it).
    {
        char phpath[700];
        snprintf(phpath, sizeof phpath, "%s/../files/strife_ph.inc", g_rundir);
        mf = fopen(phpath, "w");
        if (mf) {
            fprintf(mf, "// Auto-generated by tools/extractor -- do not edit by hand.\n"
                        "// Strife sprite slots whose NATIVE 4-char code collides with a\n"
                        "// DOOM/Heretic/Hexen sprite, and the collision-free placeholder\n"
                        "// that strifestuff.wad ships them under.  files/strife.c applies\n"
                        "// these when the game is NOT in strife_mode, so the pack never\n"
                        "// shadows the host game's art.  docs/BUDDY_SPRITE_COLLISIONS.md.\n");
            for (c = 0; c < ncodes; c++)
                if (newc[c][0] && strncmp(newc[c], codes[c], 4))
                    fprintf(mf, "    { %s, \"%s\" },\n", enums[c], newc[c]);
            fclose(mf);
            logf_(log, lcap, "  engine table -> files/strife_ph.inc\n");
        }
    }
    {
        char mappath[700];
        snprintf(mappath, sizeof mappath, "%s/../tools/strife_sprite_map.txt", g_rundir);
        mf = fopen(mappath, "w");
        if (mf) {
            fprintf(mf, "# Strife sprite code -> strifestuff.wad code.  Only codes that\n"
                        "# COLLIDE with a DOOM/Heretic/Hexen sprite are renamed; the rest\n"
                        "# keep their native name.  docs/BUDDY_SPRITE_COLLISIONS.md.\n");
            for (c = 0; c < ncodes; c++)
                if (newc[c][0] && strncmp(newc[c], codes[c], 4))
                    fprintf(mf, "%s -> %s\n", codes[c], newc[c]);
            fclose(mf);
        }
    }

    logf_(log, lcap, "wrote %s\n", outpath);
    logf_(log, lcap, "  sprites: %d (PNG, Strife palette)  %d monster codes\n", n_spr, ncodes);
    logf_(log, lcap, "  renamed: %d   omitted (host game owns the code): %d\n", n_ren, n_omit);
    if (n_skip) logf_(log, lcap, "  non-patch lumps skipped: %d\n", n_skip);
    logf_(log, lcap, "  sounds: %d  (%d skipped, name already owned by DOOM)\n", n_snd, n_dropsnd);
    ofree(&o); wad_free(sw);
    return 1;
}

// --- Hexen: build hexenstuff.wad --------------------------------------------
static int extract_hexen(const char* srcpath, const char* outpath, char* log, int lcap)
{
    char basepath[600];
    if (!find_base(basepath, sizeof basepath)) {
        logf_(log, lcap, "ERROR: no DOOM IWAD found in ID0/ for the target palette.\n");
        return 0;
    }
    wad_t* h = wad_load(srcpath);
    wad_t* b = wad_load(basepath);
    if (!h) { logf_(log, lcap, "ERROR: cannot read %s\n", srcpath); wad_free(b); return 0; }
    if (!b) { logf_(log, lcap, "ERROR: cannot read base %s\n", basepath); wad_free(h); return 0; }
    // Sprites go out as PNG in the SOURCE game's own palette, so there is no
    // conversion table any more -- files/v_png.c matches them to whatever IWAD is
    // actually running, and keeps the full colour for the truecolor sprite path.
    // The base IWAD is still located above so the "which DOOM do you have" check
    // and its log line stay meaningful.
    const unsigned char* sp = get_playpal(h);
    if (!sp) { logf_(log, lcap, "ERROR: missing PLAYPAL\n"); wad_free(h); wad_free(b); return 0; }

    // Which wanted sprite codes actually exist in this hexen.wad?
    static const char* wanted[128]; int nwanted = 0;
    static char ren[128][5];
    static codeset_t used;                       // seeded with the host games' codes
    memset(&used, 0, sizeof used);
    wc_reserve_from_dir(g_id0,    "hexen.wad", &used);
    wc_reserve_from_dir(g_rundir, "hexen.wad", &used);
    for (int t = 0; t < 2; t++) {
        const char** list = t ? HEXEN_WEAPON_SPR : HEXEN_MONSTER_SPR;
        for (int c = 0; list[c]; c++) {
            int present = 0;
            for (int i = 0; i < h->n && !present; i++)
                if (strlen(h->dir[i].name) > 4 && !strncmp(h->dir[i].name, list[c], 4)) present = 1;
            if (present && nwanted < 128) {
                wanted[nwanted] = list[c];
                hexen_make_rename(&used, list[c], ren[nwanted]);
                nwanted++;
            }
        }
    }

    obuf_t o = {0};
    oadd(&o, "AISTUFF", (const unsigned char*)AISTUFF_NOTE, (int)strlen(AISTUFF_NOTE));
    oadd(&o, "S_START", NULL, 0);
    int n_spr = 0, n_skip = 0;
    for (int i = 0; i < h->n; i++) {
        const char* nm = h->dir[i].name;
        if (strlen(nm) <= 4) continue;
        for (int c = 0; c < nwanted; c++) {
            if (!strncmp(nm, wanted[c], 4)) {
                char nn[9]; snprintf(nn, sizeof nn, "%s%s", ren[c], nm+4);
                n_spr += emit_sprite(&o, nn, h->data + h->dir[i].pos, h->dir[i].size,
                                     sp, &n_skip);
                break;
            }
        }
    }
    oadd(&o, "S_END", NULL, 0);

    // Wired monster sounds: copy each chosen Hexen DMX lump as "DS"+short.
    int n_ds = 0;
    for (int r = 0; HEXEN_SND_WIRED[r][0]; r++) {
        const char* shortnm = HEXEN_SND_WIRED[r][0];
        const char* srclump = HEXEN_SND_WIRED[r][1];
        for (int i = 0; i < h->n; i++) {
            if (strcasecmp(h->dir[i].name, srclump)) continue;
            const unsigned char* raw = h->data + h->dir[i].pos; int sz = h->dir[i].size;
            if (!is_dmx(raw, sz)) break;
            char nn[9]; snprintf(nn, sizeof nn, "DS%.6s", shortnm);
            for (char* p = nn; *p; p++) *p = (char)toupper((unsigned char)*p);
            oadd(&o, nn, raw, sz); n_ds++;
            break;
        }
    }

    // Verbatim keyword sounds (reference), de-duplicated by name.
    int n_snd = 0;
    for (int i = 0; i < h->n; i++) {
        const char* nm = h->dir[i].name;
        const unsigned char* raw = h->data + h->dir[i].pos; int sz = h->dir[i].size;
        if (!is_dmx(raw, sz)) continue;
        char low[16]; int j; for (j=0;j<15 && nm[j];j++) low[j]=(char)tolower((unsigned char)nm[j]); low[j]=0;
        int hit = 0;
        for (int k = 0; HEXEN_SND_KW[k]; k++) if (strstr(low, HEXEN_SND_KW[k])) { hit = 1; break; }
        if (!hit) continue;
        // de-dupe: skip if already emitted verbatim
        int dup = 0;
        for (int e = 0; e < o.n; e++) if (!strcmp(o.v[e].name, nm)) { dup = 1; break; }
        if (dup) continue;
        oadd(&o, nm, raw, sz); n_snd++;
    }

    int ok = wad_write(outpath, &o);
    logf_(log, lcap, ok ? "wrote %s\n" : "ERROR: could not write %s\n", outpath);
    if (ok) {
        logf_(log, lcap, "  base palette : %s\n", strrchr(basepath,'/')?strrchr(basepath,'/')+1:basepath);
        logf_(log, lcap, "  sprites      : %d  (%d codes -> X* namespace)\n", n_spr, nwanted);
        logf_(log, lcap, "  wired sounds : %d (DS* for sfx_x_*)\n", n_ds);
        logf_(log, lcap, "  ref sounds   : %d (verbatim)\n", n_snd);
        logf_(log, lcap, "  total        : %d lumps, %.1f MB\n", o.n, obuf_bytes(&o)/1048576.0);

        // Sidecar rename map for the future files/hexen.c port (best-effort).
        char mappath[600];
        snprintf(mappath, sizeof mappath, "%s/../tools/hexen_sprite_map.txt", g_rundir);
        FILE* mf = fopen(mappath, "w");
        if (mf) {
            fprintf(mf, "# Hexen sprite code -> hexenstuff.wad code (use these in files/hexen.c)\n");
            for (int c = 0; c < nwanted; c++) {
                int weapon = 0;
                for (int w = 0; HEXEN_WEAPON_SPR[w]; w++) if (!strcmp(wanted[c], HEXEN_WEAPON_SPR[w])) { weapon = 1; break; }
                fprintf(mf, "%s -> %s   (%s)\n", wanted[c], ren[c], weapon ? "weapon" : "monster");
            }
            fclose(mf);
            logf_(log, lcap, "  rename map   -> tools/hexen_sprite_map.txt\n");
        }
    }
    ofree(&o); wad_free(h); wad_free(b);
    return ok;
}

// ================================================================= worker
enum { ST_IDLE, ST_RUNNING, ST_DONE };
static SDL_AtomicInt g_status;
static SDL_Mutex*    g_lock;
static char          g_log[4096];    // protected by g_lock
static int           g_job = -1;     // avail[] index to run (set by main thread)

static int worker(void* unused)
{
    (void)unused;
    int job = g_job;
    int si = avail[job];
    const source_t* S = &SOURCES[si];

    char out[600];
    snprintf(out, sizeof out, "%s/%s", g_id0, S->out);
    SDL_CreateDirectory(g_id0);      // ensure run/ID0 exists

    char local[4096]; local[0] = 0;
    snprintf(local, sizeof local, "Extracting from %s ...\n\n",
             strrchr(avail_path[job],'/') ? strrchr(avail_path[job],'/')+1 : avail_path[job]);

    switch (S->kind) {
        case K_HERETIC:  extract_heretic (avail_path[job], out, local, sizeof local); break;
        case K_HEXEN:    extract_hexen   (avail_path[job], out, local, sizeof local); break;
        case K_FREEDOOM: extract_freedoom(avail_path[job], out, local, sizeof local); break;
        case K_DOOM2:    extract_doom2   (avail_path[job], out, local, sizeof local); break;
        case K_STRIFE:   extract_strife  (avail_path[job], out, local, sizeof local); break;
        case K_FREEDOOM2: extract_freedoom2(avail_path[job], out, local, sizeof local); break;
    }

    SDL_LockMutex(g_lock);
    snprintf(g_log, sizeof g_log, "%s", local);
    SDL_UnlockMutex(g_lock);
    SDL_SetAtomicInt(&g_status, ST_DONE);
    return 0;
}

// ================================================================= UI draw
static int   dd_open = 0;
static float mouse_x, mouse_y;
static const SDL_FRect btn = { PAD, BTN_Y, WINW-2*PAD, BTN_H };

static void font_init(void)
{
    Uint32* px = malloc(FONT_AW*FONT_CH*4);
    for (int i=0;i<FONT_AW*FONT_CH;i++)
        px[i] = 0x00FFFFFFu | ((Uint32)font_alpha[i] << 24);
    SDL_Surface* s = SDL_CreateSurfaceFrom(FONT_AW, FONT_CH, SDL_PIXELFORMAT_ARGB8888, px, FONT_AW*4);
    font = SDL_CreateTextureFromSurface(ren, s);
    SDL_SetTextureBlendMode(font, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(font, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(s); free(px);
}

static void text(float x, float y, const char* str, Uint8 r, Uint8 g, Uint8 b)
{
    if (!str) return;
    SDL_SetTextureColorMod(font, r, g, b);
    for (const char* p=str; *p; p++) {
        int c = (unsigned char)*p;
        if (c < FONT_FIRST || c >= FONT_FIRST+FONT_COUNT) c = (c==0x1a) ? '>' : '?';
        SDL_FRect src = { (float)((c-FONT_FIRST)*FONT_CW), 0, FONT_CW, FONT_CH };
        SDL_FRect dst = { x, y, FONT_CW, FONT_CH };
        SDL_RenderTexture(ren, font, &src, &dst);
        x += FONT_CW;
    }
}

static void fillrect(float x,float y,float w,float h, Uint8 r,Uint8 g,Uint8 b)
{ SDL_FRect q={x,y,w,h}; SDL_SetRenderDrawColor(ren,r,g,b,255); SDL_RenderFillRect(ren,&q); }

static void outline(float x,float y,float w,float h, Uint8 r,Uint8 g,Uint8 b)
{ SDL_FRect q={x,y,w,h}; SDL_SetRenderDrawColor(ren,r,g,b,255); SDL_RenderRect(ren,&q); }

static void draw(void)
{
    int st = SDL_GetAtomicInt(&g_status);
    fillrect(0,0,WINW,WINH, 22,22,28);

    text(PAD, 16, "BuddyDoom Asset Extractor", 120,200,255);
    text(PAD, 40, "Re-pack monster assets from your own IWADs into run/ID0/.", 150,150,160);
    fillrect(PAD, 64, WINW-2*PAD, 1, 60,60,72);

    if (navail == 0) {
        text(PAD, DD_Y, "No source IWADs found in run/ or run/ID0/.", 220,120,90);
        text(PAD, DD_Y+24, "Place heretic.wad / hexen.wad / freedoom2.wad there,", 170,170,180);
        text(PAD, DD_Y+44, "then reopen this tool.", 170,170,180);
        SDL_RenderPresent(ren);
        return;
    }

    // --- source dropdown (closed header) ---
    text(PAD, DD_Y-22, "Source IWAD:", 170,170,180);
    fillrect(PAD, DD_Y, WINW-2*PAD, DD_H, 34,34,44);
    outline(PAD, DD_Y, WINW-2*PAD, DD_H, 70,70,88);
    text(PAD+8, DD_Y+(DD_H-FONT_CH)/2, SOURCES[avail[sel]].label, 235,235,240);
    text(WINW-PAD-FONT_CW-6, DD_Y+(DD_H-FONT_CH)/2, dd_open ? "^" : "v", 160,160,175);

    // --- Extract button ---
    int busy = (st == ST_RUNNING);
    fillrect(btn.x, btn.y, btn.w, btn.h, busy ? 60:40, busy ? 60:90, busy ? 70:130);
    outline(btn.x, btn.y, btn.w, btn.h, 90,120,160);
    {
        const char* bl = busy ? "Extracting..." : "Extract Assets";
        float tw = (float)strlen(bl)*FONT_CW;
        text(btn.x + (btn.w-tw)/2, btn.y + (btn.h-FONT_CH)/2, bl, 220,235,255);
    }

    // --- result log ---
    fillrect(PAD, LOG_Y, WINW-2*PAD, WINH-LOG_Y-PAD, 16,16,20);
    outline(PAD, LOG_Y, WINW-2*PAD, WINH-LOG_Y-PAD, 50,50,62);
    if (st != ST_IDLE) {
        char buf[4096];
        SDL_LockMutex(g_lock); snprintf(buf, sizeof buf, "%s", g_log); SDL_UnlockMutex(g_lock);
        float ly = LOG_Y + 8;
        char* line = buf;
        while (line && *line && ly < WINH-PAD-FONT_CH) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = 0;
            Uint8 cr=200,cg=205,cb=210;
            if (strstr(line, "ERROR")) { cr=230; cg=110; cb=90; }
            else if (strstr(line, "wrote")) { cr=130; cg=220; cb=140; }
            text(PAD+8, ly, line, cr,cg,cb);
            ly += FONT_CH;
            line = nl ? nl+1 : NULL;
        }
    }

    // --- open dropdown list (drawn LAST so it overlays the log) ---
    if (dd_open) {
        float dy = DD_Y + DD_H;
        for (int i = 0; i < navail; i++) {
            float oy = dy + i*DD_H;
            int hover = (mouse_x >= PAD && mouse_x <= WINW-PAD && mouse_y >= oy && mouse_y <= oy+DD_H);
            fillrect(PAD, oy, WINW-2*PAD, DD_H, hover ? 55:38, hover ? 65:38, hover ? 85:48);
            outline(PAD, oy, WINW-2*PAD, DD_H, 70,70,88);
            text(PAD+8, oy+(DD_H-FONT_CH)/2, SOURCES[avail[i]].label, 225,225,235);
        }
    }

    SDL_RenderPresent(ren);
}

static void on_click(float mx, float my)
{
    if (navail == 0) return;

    // dropdown open: a click selects an option or closes it
    if (dd_open) {
        float dy = DD_Y + DD_H;
        for (int i = 0; i < navail; i++) {
            float oy = dy + i*DD_H;
            if (mx>=PAD && mx<=WINW-PAD && my>=oy && my<=oy+DD_H) { sel = i; dd_open = 0; return; }
        }
        dd_open = 0;
        return;
    }

    // toggle dropdown
    if (mx>=PAD && mx<=WINW-PAD && my>=DD_Y && my<=DD_Y+DD_H) { dd_open = 1; return; }

    // Extract button
    if (mx>=btn.x && mx<=btn.x+btn.w && my>=btn.y && my<=btn.y+btn.h) {
        if (SDL_GetAtomicInt(&g_status) == ST_RUNNING) return;
        g_job = sel;
        SDL_SetAtomicInt(&g_status, ST_RUNNING);
        SDL_LockMutex(g_lock); snprintf(g_log, sizeof g_log, "Working...\n"); SDL_UnlockMutex(g_lock);
        SDL_Thread* th = SDL_CreateThread(worker, "extract", NULL);
        SDL_DetachThread(th);
    }
}

// Run one source headlessly (no window): extract to `outdir`, print the log.
// Returns 0 on success.  Used by --cli so the tool can also fully replace the
// Python scripts in scripts/CI, and so its output can be diffed in tests.
static int run_one_cli(int si, const char* srcpath, const char* outdir)
{
    const source_t* S = &SOURCES[si];
    char out[600]; snprintf(out, sizeof out, "%s/%s", outdir, S->out);
    SDL_CreateDirectory(outdir);
    char log[4096]; log[0] = 0;
    int ok = 0;
    switch (S->kind) {
        case K_HERETIC:  ok = extract_heretic (srcpath, out, log, sizeof log); break;
        case K_HEXEN:    ok = extract_hexen   (srcpath, out, log, sizeof log); break;
        case K_FREEDOOM: ok = extract_freedoom(srcpath, out, log, sizeof log); break;
        case K_DOOM2:    ok = extract_doom2   (srcpath, out, log, sizeof log); break;
        case K_STRIFE:   ok = extract_strife  (srcpath, out, log, sizeof log); break;
        case K_FREEDOOM2: ok = extract_freedoom2(srcpath, out, log, sizeof log); break;
    }
    fputs(log, stdout);
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();

    // run/ is where the binary lives; game WADs live in run/ID0/.
    const char* bp = SDL_GetBasePath();
    snprintf(g_rundir, sizeof g_rundir, "%s", bp ? bp : "./");
    // strip a trailing slash for clean joins
    size_t rl = strlen(g_rundir);
    while (rl > 1 && (g_rundir[rl-1]=='/' || g_rundir[rl-1]=='\\')) g_rundir[--rl] = 0;
    snprintf(g_id0, sizeof g_id0, "%s/ID0", g_rundir);

    // --- headless batch mode: extractor --cli [outdir] ---------------------
    // Extracts every source IWAD that's present, no window (for scripts/tests).
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cli")) {
            const char* outdir = (i+1 < argc && argv[i+1][0] != '-') ? argv[i+1] : g_id0;
            scan_sources();
            if (navail == 0) { fprintf(stderr, "no source IWADs found in %s or %s\n", g_id0, g_rundir); return 1; }
            int rc = 0;
            for (int a = 0; a < navail; a++) {
                printf("== %s ==\n", SOURCES[avail[a]].src);
                rc |= run_one_cli(avail[a], avail_path[a], outdir);
                putchar('\n');
            }
            return rc;
        }
    }

    scan_sources();

    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr,"SDL_Init: %s\n", SDL_GetError()); return 1; }
    win = SDL_CreateWindow("BuddyDoom Asset Extractor", WINW, WINH, 0);
    {
        SDL_Surface* icon = SDL_CreateSurfaceFrom(
            BUDDYDOOM_ICON_W, BUDDYDOOM_ICON_H, SDL_PIXELFORMAT_RGBA32,
            (void*)buddydoom_icon_rgba, BUDDYDOOM_ICON_W*4);
        if (icon) { SDL_SetWindowIcon(win, icon); SDL_DestroySurface(icon); }
    }
    ren = SDL_CreateRenderer(win, NULL);
    font_init();

    g_lock = SDL_CreateMutex();
    SDL_SetAtomicInt(&g_status, ST_IDLE);

    int run = 1;
    while (run) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 33)) {
            do {
                if (e.type == SDL_EVENT_QUIT) run = 0;
                else if (e.type == SDL_EVENT_MOUSE_MOTION) { mouse_x = e.motion.x; mouse_y = e.motion.y; }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    on_click(e.button.x, e.button.y);
            } while (SDL_PollEvent(&e));
        }
        draw();
    }

    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
