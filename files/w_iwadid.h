// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Content-based IWAD identification, shared by the engine (d_main.c
//	IdentifyVersion) and the launcher.  Opens a raw WAD file, verifies the
//	"IWAD" magic, scans the lump directory for signature lumps to pin down the
//	game family, and consults an MD5 table to label the exact version.
//	Header-only (all static), so both programs include it without a shared .o.
//
//	IWADs are then identified by CONTENT, not filename -- a renamed or custom
//	IWAD is still recognised.
//
//-----------------------------------------------------------------------------

#ifndef __W_IWADID_H__
#define __W_IWADID_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    IWID_NONE = 0,
    IWID_DOOM_SW, IWID_DOOM_REG, IWID_DOOM_ULTIMATE,
    IWID_DOOM2, IWID_PLUTONIA, IWID_TNT,
    IWID_FREEDOOM1, IWID_FREEDOOM2, IWID_FREEDM,
    IWID_HERETIC, IWID_HEXEN, IWID_STRIFE, IWID_CHEX3,
    IWID_BLASPHEMER		// free Heretic-compatible IWAD (plays as Heretic)
} iwid_t;

// ---------------------------------------------------------------------------
//  MD5 (RFC 1321).  Compact public-domain implementation (Alexander Peslyak /
//  Solar Designer's, released to the public domain).  Streaming so we can hash a
//  multi-megabyte IWAD without loading it whole.
// ---------------------------------------------------------------------------
typedef struct {
    unsigned int	lo, hi;
    unsigned int	a, b, c, d;
    unsigned char	buffer[64];
    unsigned int	block[16];
} iwid_md5_ctx;

#define IWID_F(x, y, z)		((z) ^ ((x) & ((y) ^ (z))))
#define IWID_G(x, y, z)		((y) ^ ((z) & ((x) ^ (y))))
#define IWID_H(x, y, z)		(((x) ^ (y)) ^ (z))
#define IWID_H2(x, y, z)	((x) ^ ((y) ^ (z)))
#define IWID_I(x, y, z)		((y) ^ ((x) | ~(z)))
#define IWID_STEP(f, a, b, c, d, x, t, s) \
	(a) += f((b), (c), (d)) + (x) + (t); \
	(a) = (((a) << (s)) | (((a) & 0xffffffff) >> (32 - (s)))); \
	(a) += (b);

static const void* iwid_md5_body (iwid_md5_ctx* ctx, const void* data, unsigned long size)
{
    const unsigned char* ptr = (const unsigned char*) data;
    unsigned int a, b, c, d, saved_a, saved_b, saved_c, saved_d;
#define IWID_GET(n) \
	(ctx->block[(n)] = \
	 (unsigned int)ptr[(n)*4] | ((unsigned int)ptr[(n)*4+1] << 8) | \
	 ((unsigned int)ptr[(n)*4+2] << 16) | ((unsigned int)ptr[(n)*4+3] << 24))
#define IWID_SET(n) (ctx->block[(n)])
    a = ctx->a; b = ctx->b; c = ctx->c; d = ctx->d;
    do {
	saved_a = a; saved_b = b; saved_c = c; saved_d = d;
	IWID_STEP(IWID_F, a, b, c, d, IWID_GET(0),  0xd76aa478, 7)
	IWID_STEP(IWID_F, d, a, b, c, IWID_GET(1),  0xe8c7b756, 12)
	IWID_STEP(IWID_F, c, d, a, b, IWID_GET(2),  0x242070db, 17)
	IWID_STEP(IWID_F, b, c, d, a, IWID_GET(3),  0xc1bdceee, 22)
	IWID_STEP(IWID_F, a, b, c, d, IWID_GET(4),  0xf57c0faf, 7)
	IWID_STEP(IWID_F, d, a, b, c, IWID_GET(5),  0x4787c62a, 12)
	IWID_STEP(IWID_F, c, d, a, b, IWID_GET(6),  0xa8304613, 17)
	IWID_STEP(IWID_F, b, c, d, a, IWID_GET(7),  0xfd469501, 22)
	IWID_STEP(IWID_F, a, b, c, d, IWID_GET(8),  0x698098d8, 7)
	IWID_STEP(IWID_F, d, a, b, c, IWID_GET(9),  0x8b44f7af, 12)
	IWID_STEP(IWID_F, c, d, a, b, IWID_GET(10), 0xffff5bb1, 17)
	IWID_STEP(IWID_F, b, c, d, a, IWID_GET(11), 0x895cd7be, 22)
	IWID_STEP(IWID_F, a, b, c, d, IWID_GET(12), 0x6b901122, 7)
	IWID_STEP(IWID_F, d, a, b, c, IWID_GET(13), 0xfd987193, 12)
	IWID_STEP(IWID_F, c, d, a, b, IWID_GET(14), 0xa679438e, 17)
	IWID_STEP(IWID_F, b, c, d, a, IWID_GET(15), 0x49b40821, 22)
	IWID_STEP(IWID_G, a, b, c, d, IWID_SET(1),  0xf61e2562, 5)
	IWID_STEP(IWID_G, d, a, b, c, IWID_SET(6),  0xc040b340, 9)
	IWID_STEP(IWID_G, c, d, a, b, IWID_SET(11), 0x265e5a51, 14)
	IWID_STEP(IWID_G, b, c, d, a, IWID_SET(0),  0xe9b6c7aa, 20)
	IWID_STEP(IWID_G, a, b, c, d, IWID_SET(5),  0xd62f105d, 5)
	IWID_STEP(IWID_G, d, a, b, c, IWID_SET(10), 0x02441453, 9)
	IWID_STEP(IWID_G, c, d, a, b, IWID_SET(15), 0xd8a1e681, 14)
	IWID_STEP(IWID_G, b, c, d, a, IWID_SET(4),  0xe7d3fbc8, 20)
	IWID_STEP(IWID_G, a, b, c, d, IWID_SET(9),  0x21e1cde6, 5)
	IWID_STEP(IWID_G, d, a, b, c, IWID_SET(14), 0xc33707d6, 9)
	IWID_STEP(IWID_G, c, d, a, b, IWID_SET(3),  0xf4d50d87, 14)
	IWID_STEP(IWID_G, b, c, d, a, IWID_SET(8),  0x455a14ed, 20)
	IWID_STEP(IWID_G, a, b, c, d, IWID_SET(13), 0xa9e3e905, 5)
	IWID_STEP(IWID_G, d, a, b, c, IWID_SET(2),  0xfcefa3f8, 9)
	IWID_STEP(IWID_G, c, d, a, b, IWID_SET(7),  0x676f02d9, 14)
	IWID_STEP(IWID_G, b, c, d, a, IWID_SET(12), 0x8d2a4c8a, 20)
	IWID_STEP(IWID_H,  a, b, c, d, IWID_SET(5),  0xfffa3942, 4)
	IWID_STEP(IWID_H2, d, a, b, c, IWID_SET(8),  0x8771f681, 11)
	IWID_STEP(IWID_H,  c, d, a, b, IWID_SET(11), 0x6d9d6122, 16)
	IWID_STEP(IWID_H2, b, c, d, a, IWID_SET(14), 0xfde5380c, 23)
	IWID_STEP(IWID_H,  a, b, c, d, IWID_SET(1),  0xa4beea44, 4)
	IWID_STEP(IWID_H2, d, a, b, c, IWID_SET(4),  0x4bdecfa9, 11)
	IWID_STEP(IWID_H,  c, d, a, b, IWID_SET(7),  0xf6bb4b60, 16)
	IWID_STEP(IWID_H2, b, c, d, a, IWID_SET(10), 0xbebfbc70, 23)
	IWID_STEP(IWID_H,  a, b, c, d, IWID_SET(13), 0x289b7ec6, 4)
	IWID_STEP(IWID_H2, d, a, b, c, IWID_SET(0),  0xeaa127fa, 11)
	IWID_STEP(IWID_H,  c, d, a, b, IWID_SET(3),  0xd4ef3085, 16)
	IWID_STEP(IWID_H2, b, c, d, a, IWID_SET(6),  0x04881d05, 23)
	IWID_STEP(IWID_H,  a, b, c, d, IWID_SET(9),  0xd9d4d039, 4)
	IWID_STEP(IWID_H2, d, a, b, c, IWID_SET(12), 0xe6db99e5, 11)
	IWID_STEP(IWID_H,  c, d, a, b, IWID_SET(15), 0x1fa27cf8, 16)
	IWID_STEP(IWID_H2, b, c, d, a, IWID_SET(2),  0xc4ac5665, 23)
	IWID_STEP(IWID_I, a, b, c, d, IWID_SET(0),  0xf4292244, 6)
	IWID_STEP(IWID_I, d, a, b, c, IWID_SET(7),  0x432aff97, 10)
	IWID_STEP(IWID_I, c, d, a, b, IWID_SET(14), 0xab9423a7, 15)
	IWID_STEP(IWID_I, b, c, d, a, IWID_SET(5),  0xfc93a039, 21)
	IWID_STEP(IWID_I, a, b, c, d, IWID_SET(12), 0x655b59c3, 6)
	IWID_STEP(IWID_I, d, a, b, c, IWID_SET(3),  0x8f0ccc92, 10)
	IWID_STEP(IWID_I, c, d, a, b, IWID_SET(10), 0xffeff47d, 15)
	IWID_STEP(IWID_I, b, c, d, a, IWID_SET(1),  0x85845dd1, 21)
	IWID_STEP(IWID_I, a, b, c, d, IWID_SET(8),  0x6fa87e4f, 6)
	IWID_STEP(IWID_I, d, a, b, c, IWID_SET(15), 0xfe2ce6e0, 10)
	IWID_STEP(IWID_I, c, d, a, b, IWID_SET(6),  0xa3014314, 15)
	IWID_STEP(IWID_I, b, c, d, a, IWID_SET(13), 0x4e0811a1, 21)
	IWID_STEP(IWID_I, a, b, c, d, IWID_SET(4),  0xf7537e82, 6)
	IWID_STEP(IWID_I, d, a, b, c, IWID_SET(11), 0xbd3af235, 10)
	IWID_STEP(IWID_I, c, d, a, b, IWID_SET(2),  0x2ad7d2bb, 15)
	IWID_STEP(IWID_I, b, c, d, a, IWID_SET(9),  0xeb86d391, 21)
	a += saved_a; b += saved_b; c += saved_c; d += saved_d;
	ptr += 64;
    } while (size -= 64);
    ctx->a = a; ctx->b = b; ctx->c = c; ctx->d = d;
#undef IWID_GET
#undef IWID_SET
    return ptr;
}

static void iwid_md5_init (iwid_md5_ctx* ctx)
{
    ctx->a = 0x67452301; ctx->b = 0xefcdab89;
    ctx->c = 0x98badcfe; ctx->d = 0x10325476;
    ctx->lo = 0; ctx->hi = 0;
}

static void iwid_md5_update (iwid_md5_ctx* ctx, const void* data, unsigned long size)
{
    unsigned int saved_lo, used, available;
    saved_lo = ctx->lo;
    if ((ctx->lo = (saved_lo + size) & 0x1fffffff) < saved_lo) ctx->hi++;
    ctx->hi += (unsigned int)(size >> 29);
    used = saved_lo & 0x3f;
    if (used) {
	available = 64 - used;
	if (size < available) { memcpy (&ctx->buffer[used], data, size); return; }
	memcpy (&ctx->buffer[used], data, available);
	data = (const unsigned char*)data + available;
	size -= available;
	iwid_md5_body (ctx, ctx->buffer, 64);
    }
    if (size >= 64) { data = iwid_md5_body (ctx, data, size & ~(unsigned long)0x3f); size &= 0x3f; }
    memcpy (ctx->buffer, data, size);
}

static void iwid_md5_final (unsigned char* result, iwid_md5_ctx* ctx)
{
    unsigned int used, available;
    used = ctx->lo & 0x3f;
    ctx->buffer[used++] = 0x80;
    available = 64 - used;
    if (available < 8) { memset (&ctx->buffer[used], 0, available); iwid_md5_body (ctx, ctx->buffer, 64); used = 0; available = 64; }
    memset (&ctx->buffer[used], 0, available - 8);
    ctx->lo <<= 3;
    ctx->buffer[56] = ctx->lo;        ctx->buffer[57] = ctx->lo >> 8;
    ctx->buffer[58] = ctx->lo >> 16;  ctx->buffer[59] = ctx->lo >> 24;
    ctx->buffer[60] = ctx->hi;        ctx->buffer[61] = ctx->hi >> 8;
    ctx->buffer[62] = ctx->hi >> 16;  ctx->buffer[63] = ctx->hi >> 24;
    iwid_md5_body (ctx, ctx->buffer, 64);
    result[0]  = ctx->a;       result[1]  = ctx->a >> 8;  result[2]  = ctx->a >> 16; result[3]  = ctx->a >> 24;
    result[4]  = ctx->b;       result[5]  = ctx->b >> 8;  result[6]  = ctx->b >> 16; result[7]  = ctx->b >> 24;
    result[8]  = ctx->c;       result[9]  = ctx->c >> 8;  result[10] = ctx->c >> 16; result[11] = ctx->c >> 24;
    result[12] = ctx->d;       result[13] = ctx->d >> 8;  result[14] = ctx->d >> 16; result[15] = ctx->d >> 24;
}

// Hex-encode a 16-byte digest into a 33-byte buffer (lowercase).
static void iwid_md5_hex (const unsigned char* dig, char* out)
{
    static const char* h = "0123456789abcdef";
    int i;
    for (i = 0; i < 16; i++) { out[i*2] = h[dig[i] >> 4]; out[i*2+1] = h[dig[i] & 15]; }
    out[32] = 0;
}

// ---------------------------------------------------------------------------
//  MD5 -> exact-version table (see docs; more entries can be appended freely).
// ---------------------------------------------------------------------------
typedef struct { const char* md5; iwid_t id; const char* label; } iwid_md5ent_t;

static const iwid_md5ent_t iwid_md5tab[] = {
    { "f0cefca49926d00903cf57551d901abe", IWID_DOOM_SW,       "Doom (shareware v1.9)"                 },
    { "1cd63c5ddff1bf8ce844237f580e9cf3", IWID_DOOM_REG,      "Doom (registered v1.9)"                },
    { "c4fe9fd920207691a9f493668e0a2083", IWID_DOOM_ULTIMATE, "The Ultimate Doom (v1.9)"              },
    { "25e1459ca71d321525f84628f45ca8cd", IWID_DOOM2,         "Doom II (v1.9)"                        },
    { "75c8cf89566741fa9d22447604053bd7", IWID_PLUTONIA,      "Final Doom: Plutonia (v1.9)"           },
    { "3493be7e1e2588bc9c8b31eab2587a04", IWID_PLUTONIA,      "Final Doom: Plutonia (GOG/anthology)"  },
    { "4e158d9953c79ccf97bd0663244cc6b6", IWID_TNT,           "Final Doom: TNT Evilution (v1.9)"      },
    { "1d39e405bf6ee3df69a8d2646c8d5c49", IWID_TNT,           "Final Doom: TNT Evilution (GOG fixed)" },
    { NULL, IWID_NONE, NULL }
};

// ---------------------------------------------------------------------------
//  Identify a WAD file by content.  Returns the game id (IWID_NONE if the file
//  isn't a readable IWAD), and writes a human label into out[cap] when non-NULL.
//  by_md5 (optional) is set to 1 if the exact version came from the MD5 table.
// ---------------------------------------------------------------------------
// IWAD lump names are stored upper-case, NUL-padded to 8 bytes; the signature
// literals below are upper-case too, so a plain strncmp (stops at the literal's
// NUL) matches without needing case-folding.
static int iwid_dir_has (const unsigned char* dir, unsigned n, const char* name)
{
    unsigned i;
    for (i = 0; i < n; i++)
	if (!strncmp ((const char*)dir + i*16 + 8, name, 8))
	    return 1;
    return 0;
}

// Does the file's basename contain this (lowercase) needle, ignoring case?  Used only
// where the lumps can't split two games apart (Blasphemer vs Heretic, plutonia vs tnt).
static int iwid_name_has (const char* path, const char* lowneedle)
{
    const char*	b = path;
    const char*	s;
    char	low[80];
    int		i, c;

    if ((s = strrchr (b, '/')))  b = s+1;
    if ((s = strrchr (b, '\\'))) b = s+1;
    for (i = 0; b[i] && i < 79; i++)
    { c = (unsigned char)b[i]; if (c >= 'A' && c <= 'Z') c += 32; low[i] = (char)c; }
    low[i] = 0;
    return strstr (low, lowneedle) != NULL;
}

static iwid_t IWID_Identify (const char* path, char* out, int cap, int* by_md5)
{
    FILE*		f;
    unsigned char	hdr[12];
    unsigned		numl, diroff;
    unsigned char*	dir = NULL;
    iwid_t		id = IWID_NONE;
    const char*		label = "Unknown IWAD";
    int			md5ok = 0;

    if (by_md5) *by_md5 = 0;
    if (!(f = fopen (path, "rb"))) return IWID_NONE;
    if (fread (hdr, 1, 12, f) != 12 || memcmp (hdr, "IWAD", 4)) { fclose (f); return IWID_NONE; }
    numl   = hdr[4] | hdr[5]<<8 | hdr[6]<<16 | ((unsigned)hdr[7]<<24);
    diroff = hdr[8] | hdr[9]<<8 | hdr[10]<<16 | ((unsigned)hdr[11]<<24);

    // --- MD5 the whole file (exact version) ---
    {
	iwid_md5_ctx	c;
	unsigned char	digest[16], buf[16384];
	char		hex[33];
	size_t		got;
	int		k;
	iwid_md5_init (&c);
	fseek (f, 0, SEEK_SET);
	while ((got = fread (buf, 1, sizeof buf, f)) > 0) iwid_md5_update (&c, buf, (unsigned long)got);
	iwid_md5_final (digest, &c);
	iwid_md5_hex (digest, hex);
	for (k = 0; iwid_md5tab[k].md5; k++)
	    if (!strcmp (hex, iwid_md5tab[k].md5))
	    { id = iwid_md5tab[k].id; label = iwid_md5tab[k].label; md5ok = 1; break; }
    }

    // --- lump-signature fallback (renamed / patched / unlisted IWADs) ---
    if (!md5ok && numl && numl <= 100000)
    {
	dir = (unsigned char*) malloc ((size_t)numl * 16);
	if (dir && !fseek (f, (long)diroff, SEEK_SET) && fread (dir, 16, numl, f) == numl)
	{
	    // The Heretic family is tested from the most specific down: hexen.wad carries
	    // Heretic's M_HTIC menu art, and Blasphemer carries Heretic's M_HTIC *and*
	    // MUS_E1M1 -- test either of them after the plain Heretic sig and they both read
	    // as "Heretic" (which, since the engine derives heretic_mode from this id, would
	    // boot Hexen as Heretic).  Hexen is the only one with MAP01; Blasphemer's own
	    // marker lump is BLASPHEM, with the name check as the fallback for a build that
	    // lacks it.
	    if      (iwid_dir_has (dir, numl, "MAP01") && iwid_dir_has (dir, numl, "INVBACK"))
		{ id = IWID_STRIFE;   label = "Strife"; }	// MAP01 + Strife inventory-HUD lump
	    else if (iwid_dir_has (dir, numl, "MAP01") && iwid_dir_has (dir, numl, "SNDCURVE"))
		{ id = IWID_HEXEN;    label = "Hexen"; }
	    else if (iwid_dir_has (dir, numl, "BLASPHEM")
		     || ((iwid_dir_has (dir, numl, "M_HTIC") || iwid_dir_has (dir, numl, "MUS_E1M1"))
			 && iwid_name_has (path, "blasphem")))
		{ id = IWID_BLASPHEMER; label = "Blasphemer"; }
	    else if (iwid_dir_has (dir, numl, "M_HTIC")   || iwid_dir_has (dir, numl, "MUS_E1M1"))
		{ id = IWID_HERETIC;  label = "Heretic"; }
	    else if (iwid_dir_has (dir, numl, "MAP01"))
	    {
		// Doom II family -- lumps can't split doom2/plutonia/tnt (only MD5 can),
		// so fall back on the filename for the label; game behaviour is identical.
		const char* b = path, * s;
		if ((s = strrchr (b, '/')))  b = s+1;
		if ((s = strrchr (b, '\\'))) b = s+1;
		if      (strstr (b, "plut") || strstr (b, "Plut")) { id = IWID_PLUTONIA; label = "Final Doom: Plutonia"; }
		else if (strstr (b, "tnt")  || strstr (b, "TNT"))  { id = IWID_TNT;      label = "Final Doom: TNT Evilution"; }
		else if (strstr (b, "freedm"))                     { id = IWID_FREEDM;   label = "FreeDM"; }
		else if (strstr (b, "freedoom2") || strstr(b,"freedoom"))
							           { id = IWID_FREEDOOM2; label = "Freedoom: Phase 2"; }
		else                                               { id = IWID_DOOM2;   label = "Doom II"; }
	    }
	    else if (iwid_dir_has (dir, numl, "E4M1")) { id = IWID_DOOM_ULTIMATE; label = "The Ultimate Doom"; }
	    else if (iwid_dir_has (dir, numl, "E2M1"))
	    {
		const char* b = path, * s;
		if ((s = strrchr (b, '/')))  b = s+1;
		if ((s = strrchr (b, '\\'))) b = s+1;
		if (strstr (b, "chex") || strstr (b, "Chex")) { id = IWID_CHEX3;      label = "Chex Quest 3"; }
		else if (strstr (b, "freedoom1"))             { id = IWID_FREEDOOM1;  label = "Freedoom: Phase 1"; }
		else                                          { id = IWID_DOOM_REG;   label = "Doom (registered)"; }
	    }
	    else if (iwid_dir_has (dir, numl, "E1M1")) { id = IWID_DOOM_SW; label = "Doom (shareware)"; }
	}
	if (dir) free (dir);
    }

    fclose (f);
    if (out && cap > 0) { strncpy (out, label, cap-1); out[cap-1] = 0; }
    if (by_md5) *by_md5 = md5ok;
    return id;
}

#endif // __W_IWADID_H__
