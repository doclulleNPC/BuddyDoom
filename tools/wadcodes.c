// wadcodes.c -- see wadcodes.h.  C mirror of tools/wadcodes.py.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wadcodes.h"

// Every IWAD / internal pack a *stuff.wad could plausibly be loaded alongside.
static const char* const HOST_WADS[] = {
    "DOOM.WAD", "doom.wad", "DOOM2.WAD", "doom2.wad", "doom1.wad",
    "PLUTONIA.WAD", "plutonia.wad", "TNT.WAD", "tnt.wad",
    "freedoom1.wad", "freedoom2.wad", "freedm.wad",
    "heretic.wad", "hexen.wad", "strife1.wad",
    "id24res.wad", "buddydoom.wad",
    NULL
};

int wc_has(const codeset_t* s, const char* code)
{
    int i;
    for (i = 0; i < s->n; i++)
        if (!strncmp(s->v[i], code, 4)) return 1;
    return 0;
}

void wc_add(codeset_t* s, const char* code)
{
    if (s->n >= WC_MAXCODES || wc_has(s, code)) return;
    memcpy(s->v[s->n], code, 4);
    s->v[s->n][4] = 0;
    s->n++;
}

static unsigned rd32(const unsigned char* p)
{ return (unsigned)p[0] | ((unsigned)p[1]<<8) | ((unsigned)p[2]<<16) | ((unsigned)p[3]<<24); }

void wc_codes_in_wad(const char* path, codeset_t* out)
{
    FILE*           f = fopen(path, "rb");
    unsigned char   hdr[12];
    unsigned char*  dir;
    unsigned        n, off;
    unsigned        i;
    int             inside = 0;

    if (!f) return;
    if (fread(hdr, 1, 12, f) != 12) { fclose(f); return; }
    n = rd32(hdr + 4); off = rd32(hdr + 8);
    if (n == 0 || n > 100000u) { fclose(f); return; }
    dir = (unsigned char*) malloc((size_t)n * 16);
    if (!dir) { fclose(f); return; }
    if (fseek(f, (long)off, SEEK_SET) != 0 || fread(dir, 16, n, f) != n) {
        free(dir); fclose(f); return;
    }
    fclose(f);

    for (i = 0; i < n; i++) {
        char nm[9];
        memcpy(nm, dir + i*16 + 8, 8);
        nm[8] = 0;
        if      (!strcmp(nm, "S_START") || !strcmp(nm, "SS_START")) inside = 1;
        else if (!strcmp(nm, "S_END")   || !strcmp(nm, "SS_END"))   inside = 0;
        else if (inside && strlen(nm) > 4) {
            char code[5];
            memcpy(code, nm, 4); code[4] = 0;
            wc_add(out, code);
        }
    }
    free(dir);
}

void wc_reserve_from_dir(const char* dir, const char* exclude, codeset_t* out)
{
    int i;
    for (i = 0; HOST_WADS[i]; i++) {
        char path[1024];
        if (exclude) {
#ifdef _WIN32
            if (!_stricmp(HOST_WADS[i], exclude)) continue;
#else
            if (!strcasecmp(HOST_WADS[i], exclude)) continue;
#endif
        }
        snprintf(path, sizeof path, "%s/%s", dir, HOST_WADS[i]);
        wc_codes_in_wad(path, out);
    }
}

// ---------------------------------------------------------------------------
// GZDoom's RenameSprites() tables, verbatim (../gzdoom/src/d_main.cpp).  Pairs of
// {native, replacement}; NULL-terminated.
// ---------------------------------------------------------------------------
static const char* const HERETIC_RENAMES[] = {
    "HEAD", "LICH",     // Ironlich
    NULL
};

static const char* const HEXEN_RENAMES[] = {
    "BARL", "ZBAR",     // ZBarrel
    "ARM1", "AR_1",     // MeshArmor
    "ARM2", "AR_2",     // FalconShield
    "ARM3", "AR_3",     // PlatinumHelm
    "ARM4", "AR_4",     // AmuletOfWarding
    "SUIT", "ZSUI",     // ZSuitOfArmor / ZArmorChunk
    "TRE1", "ZTRE",     // ZTree / ZTreeDead
    "TRE2", "TRES",     // ZTreeSwamp150
    "CAND", "BCAN",     // ZBlueCandle
    "ROCK", "ROKK",     // rocks and dirt
    "WATR", "HWAT",     // Strife also has WATR
    "GIBS", "POL5",     // RealGibs
    "EGGM", "PRKM",     // PorkFX
    "INVU", "DEFN",     // Icon of the Defender
    NULL
};

static const char* const STRIFE_RENAMES[] = {
    "MISL", "SMIS",     // lots of places
    "ARM1", "ARM3",     // MetalArmor
    "ARM2", "ARM4",     // LeatherArmor
    "PMAP", "SMAP",     // StrifeMap
    "TLMP", "TECH",     // TechLampSilver / TechLampBrass
    "TRE1", "TRET",     // TreeStub
    "BAR1", "BARC",     // BarricadeColumn
    "SHT2", "MPUF",     // MaulerPuff
    "BARL", "BBAR",     // StrifeBurningBarrel
    "TRCH", "TRHL",     // SmallTorchLit
    "SHRD", "SHAR",     // glass shards
    "BLST", "MAUL",     // Mauler
    "LOGG", "LOGW",     // StickInWater
    "VASE", "VAZE",     // Pot / Pitcher
    "CNDL", "KNDL",     // Candle
    "POT1", "MPOT",     // MetalPot
    "SPID", "STLK",     // Stalker
    NULL
};

const char* wc_gzdoom_rename(const char* game, const char* code)
{
    const char* const* t = NULL;
    int i;
    if      (!strcmp(game, "heretic")) t = HERETIC_RENAMES;
    else if (!strcmp(game, "hexen"))   t = HEXEN_RENAMES;
    else if (!strcmp(game, "strife"))  t = STRIFE_RENAMES;
    if (!t) return NULL;
    for (i = 0; t[i]; i += 2)
        if (!strncmp(t[i], code, 4)) return t[i+1];
    return NULL;
}

int wc_pick_rename(const char* game, const char* code, char prefix,
                   codeset_t* used, const codeset_t* shareable, char out[5])
{
    static const char TAIL[] = "23456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char* want = wc_gzdoom_rename(game, code);
    char        cand[5];
    int         k;

    // GZDoom's spelling first -- free, or claimed only by our own pack shipping
    // the identical art under ZDoom's name (buddydoom.wad's STLK for the Stalker).
    if (want && (!wc_has(used, want) || (shareable && wc_has(shareable, want)))) {
        memcpy(out, want, 4); out[4] = 0;
        wc_add(used, out);
        return 1;
    }

    // prefix 0 means "GZDoom's spelling or nothing": the caller would rather OMIT a
    // colliding sprite than invent a name for it, because the host game already owns
    // a lump under that code and its version is a fine stand-in (gibs, fog, puffs).
    if (!prefix) return 0;

    cand[0] = prefix; cand[1] = code[0]; cand[2] = code[1]; cand[4] = 0;
    for (k = 0; k < 2 + (int)(sizeof TAIL - 1); k++) {
        cand[3] = (k == 0) ? code[2] : (k == 1) ? code[3] : TAIL[k-2];
        if (!wc_has(used, cand)) {
            memcpy(out, cand, 5);
            wc_add(used, out);
            return 1;
        }
    }
    return 0;
}
