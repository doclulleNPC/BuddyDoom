// wadpng.c -- see wadpng.h.  C mirror of tools/wadpng.py; the two must stay in
// step (extract_*.py and the extractor GUI are expected to produce the same pack).
//
// Deflate comes from files/miniz.c, which the engine already ships -- the build
// scripts compile it alongside this file.

#include <stdlib.h>
#include <string.h>

#include "miniz.h"
#include "wadpng.h"

// ---------------------------------------------------------------- patch reading

static int rd16s(const unsigned char* p) { return (short)(p[0] | (p[1] << 8)); }
static unsigned rd32u(const unsigned char* p)
{ return (unsigned)p[0] | ((unsigned)p[1]<<8) | ((unsigned)p[2]<<16) | ((unsigned)p[3]<<24); }

int wadpng_is_patch(const unsigned char* raw, int size)
{
    int w, h;
    if (size < 8) return 0;
    w = rd16s(raw); h = rd16s(raw + 2);
    if (w <= 0 || w > 4096 || h <= 0 || h > 4096) return 0;
    return 8 + 4*w <= size;
}

// ---------------------------------------------------------------- PNG writing

static void put32be(unsigned char* p, unsigned int v)
{ p[0] = (unsigned char)(v>>24); p[1] = (unsigned char)(v>>16);
  p[2] = (unsigned char)(v>>8);  p[3] = (unsigned char)v; }

// Append one PNG chunk (length, type, data, CRC) at *pp, advancing it.
static void chunk(unsigned char** pp, const char* type,
                  const unsigned char* data, unsigned int len)
{
    unsigned char* p = *pp;
    unsigned int crc;
    put32be(p, len); p += 4;
    memcpy(p, type, 4);
    crc = (unsigned int) mz_crc32(MZ_CRC32_INIT, p, 4);
    p += 4;
    if (len) {
        memcpy(p, data, len);
        crc = (unsigned int) mz_crc32(crc, p, len);
        p += len;
    }
    put32be(p, crc); p += 4;
    *pp = p;
}

unsigned char* wadpng_from_patch(const unsigned char* raw, int size,
                                 const unsigned char* pal, int* out_len)
{
    int             w, h, loff, toff, x, y;
    unsigned char*  rows;          // RGBA scanlines with a leading filter byte each
    unsigned long   stride, rawlen;
    unsigned char*  comp;
    mz_ulong        complen;
    unsigned char*  png;
    unsigned char*  p;
    unsigned char   ihdr[13], grab[8];

    if (out_len) *out_len = 0;
    if (!wadpng_is_patch(raw, size) || !pal) return NULL;
    w = rd16s(raw); h = rd16s(raw + 2);
    loff = rd16s(raw + 4); toff = rd16s(raw + 6);

    stride = 1 + (unsigned long)w * 4;
    rawlen = stride * (unsigned long)h;
    rows = (unsigned char*) calloc(rawlen ? rawlen : 1, 1);
    if (!rows) return NULL;
    // calloc leaves every pixel 0,0,0,0 -- i.e. transparent, which is exactly a
    // Doom patch's "no post covers this row" state.  v_png.c treats alpha < 128 as
    // a gap in the column posts, so the patch's own transparency round-trips.

    for (x = 0; x < w; x++) {
        unsigned o = rd32u(raw + 8 + x*4);
        if (o == 0 || o >= (unsigned)size) continue;
        // Doom column posts: topdelta, length, pad, pixels..., pad; 0xFF ends.
        while (o < (unsigned)size && raw[o] != 0xFF) {
            int top, n, i;
            if (o + 2 >= (unsigned)size) break;
            top = raw[o]; n = raw[o+1];
            o += 3;                                     // + the leading pad byte
            for (i = 0; i < n; i++) {
                unsigned src = o + i;
                int      yy  = top + i;
                unsigned char c;
                unsigned char* d;
                if (src >= (unsigned)size) break;
                if (yy < 0 || yy >= h) continue;
                c = raw[src];
                d = rows + (unsigned long)yy * stride + 1 + (unsigned long)x * 4;
                d[0] = pal[c*3]; d[1] = pal[c*3+1]; d[2] = pal[c*3+2]; d[3] = 255;
            }
            o += (unsigned)n + 1;                       // + the trailing pad byte
        }
    }
    (void)y;

    complen = mz_compressBound((mz_ulong)rawlen);
    comp = (unsigned char*) malloc(complen ? complen : 1);
    if (!comp) { free(rows); return NULL; }
    if (mz_compress2(comp, &complen, rows, (mz_ulong)rawlen, MZ_BEST_COMPRESSION) != MZ_OK) {
        free(rows); free(comp); return NULL;
    }
    free(rows);

    // 8 sig + (12+13) IHDR + (12+8) grAb + (12+complen) IDAT + 12 IEND
    png = (unsigned char*) malloc(8 + 25 + 20 + 12 + complen + 12);
    if (!png) { free(comp); return NULL; }
    p = png;
    memcpy(p, "\211PNG\r\n\032\n", 8); p += 8;

    put32be(ihdr, (unsigned)w); put32be(ihdr + 4, (unsigned)h);
    ihdr[8]  = 8;      // bit depth
    ihdr[9]  = 6;      // colour type: RGBA
    ihdr[10] = 0;      // deflate
    ihdr[11] = 0;      // adaptive filtering
    ihdr[12] = 0;      // no interlace
    chunk(&p, "IHDR", ihdr, 13);

    put32be(grab,     (unsigned)loff);          // signed, but two's complement
    put32be(grab + 4, (unsigned)toff);
    chunk(&p, "grAb", grab, 8);                 // MUST precede IDAT

    chunk(&p, "IDAT", comp, (unsigned int)complen);
    chunk(&p, "IEND", NULL, 0);
    free(comp);

    if (out_len) *out_len = (int)(p - png);
    return png;
}
