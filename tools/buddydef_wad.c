// buddydef_wad.c -- see buddydef_wad.h.  Plain C, no engine deps.

#include "buddydef_wad.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static int dirent_name_eq(const char a[8], const char* b)
{
    // The on-disk name is exactly 8 bytes; the trailing bytes are 0 (always --
    // see CLAUDE.md WAD lump name rule).  We compare the first min(N,8) bytes.
    int n = 0;
    while (n < 8 && b[n] && !isspace((unsigned char)b[n])) n++;
    if (n == 0) return 0;
    for (int i = 0; i < n; i++)
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i]))
            return 0;
    // Padding bytes in the slot must be 0 (or spaces) for a match.
    for (int i = n; i < 8; i++)
        if (a[i] != 0 && !isspace((unsigned char)a[i]))
            return 0;
    return 1;
}

int buddydef_wad_load(const char* path, buddydef_wad_t* out)
{
    memset(out, 0, sizeof *out);
    FILE* f = fopen(path, "rb");
    if (!f) return -1;

    buddydef_wad_header_t hdr;
    if (fread(&hdr, 1, sizeof hdr, f) != sizeof hdr) { fclose(f); return -1; }
    if (hdr.numlumps == 0 || hdr.numlumps > 0x100000) { fclose(f); return -1; }
    if (memcmp(hdr.identification, "IWAD", 4) != 0 &&
        memcmp(hdr.identification, "PWAD", 4) != 0)        { fclose(f); return -1; }

    out->dir = (buddydef_wad_dirent_t*)calloc(hdr.numlumps, sizeof *out->dir);
    out->data = (unsigned char**)calloc(hdr.numlumps, sizeof *out->data);
    if (!out->dir || !out->data) { fclose(f); buddydef_wad_free(out); return -1; }

    if (fseek(f, hdr.infotableofs, SEEK_SET) != 0)        { fclose(f); buddydef_wad_free(out); return -1; }
    if (fread(out->dir, sizeof(buddydef_wad_dirent_t), hdr.numlumps, f) != hdr.numlumps) {
        fclose(f); buddydef_wad_free(out); return -1;
    }
    out->numlumps = hdr.numlumps;
    memcpy(out->id, hdr.identification, 4);

    // Read each lump's data straight away.  A BUDDYDEF editor will re-save the
    // whole WAD, so we need every byte in memory.  Lump sizes are bounded by the
    // WAD format (max 32-bit unsigned) and in practice nobody ships a WAD with
    // lumps >100 MB; we tolerate up to that.
    for (unsigned i = 0; i < out->numlumps; i++) {
        unsigned sz = out->dir[i].size;
        unsigned pos = out->dir[i].filepos;
        if (sz == 0) { out->data[i] = NULL; continue; }
        if (sz > 256u * 1024u * 1024u) { fclose(f); buddydef_wad_free(out); return -1; }
        if (fseek(f, pos, SEEK_SET) != 0) { fclose(f); buddydef_wad_free(out); return -1; }
        out->data[i] = (unsigned char*)malloc(sz);
        if (!out->data[i]) { fclose(f); buddydef_wad_free(out); return -1; }
        if (fread(out->data[i], 1, sz, f) != sz) { fclose(f); buddydef_wad_free(out); return -1; }
    }
    fclose(f);
    return 0;
}

void buddydef_wad_free(buddydef_wad_t* w)
{
    if (!w) return;
    if (w->data) {
        for (unsigned i = 0; i < w->numlumps; i++) free(w->data[i]);
        free(w->data);
    }
    free(w->dir);
    memset(w, 0, sizeof *w);
}

int buddydef_wad_find(const buddydef_wad_t* w, const char* name)
{
    if (!w || !name || !*name) return -1;
    for (unsigned i = 0; i < w->numlumps; i++)
        if (dirent_name_eq(w->dir[i].name, name))
            return (int)i;
    return -1;
}

int buddydef_wad_replace_lump(buddydef_wad_t* w, int idx, const void* data, unsigned size)
{
    if (!w || idx < 0 || (unsigned)idx >= w->numlumps) return -1;
    unsigned char* copy = NULL;
    if (size) {
        copy = (unsigned char*)malloc(size);
        if (!copy) return -1;
        memcpy(copy, data, size);
    }
    free(w->data[idx]);
    w->data[idx] = copy;
    w->dir[idx].size = size;
    return 0;
}

int buddydef_wad_append_lump(buddydef_wad_t* w, const char* name,
                             const void* data, unsigned size)
{
    if (!w || !name || !*name) return -1;
    unsigned nl = w->numlumps;
    buddydef_wad_dirent_t* nd = (buddydef_wad_dirent_t*)realloc(w->dir, (nl + 1) * sizeof *nd);
    if (!nd) return -1;
    w->dir = nd;
    unsigned char** dp = (unsigned char**)realloc(w->data, (nl + 1) * sizeof *dp);
    if (!dp) return -1;
    w->data = dp;

    buddydef_wad_dirent_t* de = &w->dir[nl];
    memset(de, 0, sizeof *de);
    // Limit name to 8 chars; pad with NULs (matches the engine's storage layout).
    int n = 0;
    while (n < 8 && name[n] && !isspace((unsigned char)name[n])) n++;
    memcpy(de->name, name, n);
    for (int i = n; i < 8; i++) de->name[i] = 0;
    de->size = size;
    de->filepos = 0;     // patched by save()

    unsigned char* copy = NULL;
    if (size) {
        copy = (unsigned char*)malloc(size);
        if (!copy) return -1;
        memcpy(copy, data, size);
    }
    w->data[nl] = copy;
    w->numlumps = nl + 1;
    return (int)nl;
}

int buddydef_wad_save(const buddydef_wad_t* w, const char* path)
{
    if (!w || !path) return -1;
    FILE* f = fopen(path, "wb");
    if (!f) return -1;

    // Layout: header, then lump data concatenated, then the directory at the end.
    // Strictly speaking id's WAD format allows data after the directory, but every
    // loader (including this engine) just seeks to infotableofs to read the dir.

    unsigned hdr_sz = (unsigned)sizeof(buddydef_wad_header_t);
    unsigned long long cursor = hdr_sz;

    // Pass 1: lay out lump data, remember each start.
    unsigned* starts = (unsigned*)calloc(w->numlumps, sizeof *starts);
    if (!starts) { fclose(f); return -1; }
    for (unsigned i = 0; i < w->numlumps; i++) {
        starts[i] = (unsigned)cursor;
        cursor += w->dir[i].size;
    }
    unsigned dir_ofs = (unsigned)cursor;
    unsigned dir_sz  = w->numlumps * (unsigned)sizeof(buddydef_wad_dirent_t);

    // Write header.
    buddydef_wad_header_t hdr;
    memcpy(hdr.identification, w->id, 4);
    hdr.numlumps     = w->numlumps;
    hdr.infotableofs = dir_ofs;
    if (fwrite(&hdr, 1, sizeof hdr, f) != sizeof hdr) { free(starts); fclose(f); return -1; }

    // Write each lump's data.
    for (unsigned i = 0; i < w->numlumps; i++) {
        if (w->dir[i].size == 0) continue;
        if (fwrite(w->data[i], 1, w->dir[i].size, f) != w->dir[i].size) {
            free(starts); fclose(f); return -1;
        }
    }

    // Write the directory (with patched filepos).
    for (unsigned i = 0; i < w->numlumps; i++) {
        buddydef_wad_dirent_t de = w->dir[i];
        de.filepos = starts[i];
        if (fwrite(&de, 1, sizeof de, f) != sizeof de) { free(starts); fclose(f); return -1; }
    }

    free(starts);
    fclose(f);
    (void)dir_sz;
    return 0;
}
