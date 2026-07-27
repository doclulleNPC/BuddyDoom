// buddydef_wad.h -- tiny WAD reader/writer for the BUDDYDEF editor tool.
//
// Just enough of the Doom WAD format to:
//   - open a WAD, read the header + directory
//   - find + extract a BUDDYDEF lump (or any lump by name)
//   - rebuild a WAD with a new/edited BUDDYDEF lump, preserving everything else
//
// Format reference: files/w_wad.c, files/w_wad.h.  We do NOT support PWADs-with-
// non-WAD-files or PK3s (the BUDDYDEF tool is offline/desktop -- the user points
// at a real .wad).  32-bit on-disk layout only (the only Doom has ever shipped).
//
// All multibyte header fields are little-endian on disk; we read with a plain
// fread and no endian swap -- this engine runs everywhere x86/ARM little-endian.

#ifndef BUDDYDEF_WAD_H
#define BUDDYDEF_WAD_H

#include <stdio.h>
#include <stddef.h>

#define BUDDYDEF_WAD_IDLEN 4
#define BUDDYDEF_WAD_LUMPNAMELEN 8

typedef struct {
    char     identification[4];   // "IWAD" or "PWAD"
    unsigned numlumps;
    unsigned infotableofs;
} buddydef_wad_header_t;

typedef struct {
    unsigned filepos;
    unsigned size;
    char     name[8];             // null-padded
} buddydef_wad_dirent_t;

// An entire WAD in memory.  Built by buddydef_wad_load, freed by buddydef_wad_free.
typedef struct {
    char                  id[4];          // "IWAD" or "PWAD"
    buddydef_wad_dirent_t* dir;           // numlumps entries
    unsigned              numlumps;
    unsigned char**       data;          // numlumps pointer slots, each `size` bytes (or NULL)
} buddydef_wad_t;

// Load a WAD from disk (read-only so far).  Returns 0 on success, -1 on failure.
// On success, *out is malloc'd and must be freed with buddydef_wad_free.
int buddydef_wad_load(const char* path, buddydef_wad_t* out);

// Free everything in a WAD loaded with buddydef_wad_load.
void buddydef_wad_free(buddydef_wad_t* w);

// Find the first lump named `name` (8-byte, case-insensitive, fixed-prefix).
// Returns -1 if not found.
int buddydef_wad_find(const buddydef_wad_t* w, const char* name);

// Replace a lump's data with a new malloc'd copy.  The directory entry is updated
// in place.  On failure (returns -1) the WAD is untouched.
int buddydef_wad_replace_lump(buddydef_wad_t* w, int idx, const void* data, unsigned size);

// Append a new lump to the directory (caller-supplied name + malloc'd data).
// Returns the new index, or -1 on failure.
int buddydef_wad_append_lump(buddydef_wad_t* w, const char* name,
                             const void* data, unsigned size);

// Write a WAD to disk.  Always writes PWAD regardless of input id (we only ever
// edit or create BUDDYDEF PWADs, never re-emit a full IWAD).
int buddydef_wad_save(const buddydef_wad_t* w, const char* path);

#endif
