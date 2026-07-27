// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Universal Doom Map Format (UDMF) loader -- "doom" namespace.  See p_udmf.h.
//
//	A small self-contained TEXTMAP tokenizer + recursive-descent parser builds
//	intermediate arrays, then copies them into the engine's vertex/sector/side/
//	line arrays exactly as the binary P_Load* loaders do (same field init, so the
//	rest of the engine is none the wiser).  The BSP tree is loaded separately from
//	the ZNODES lump by P_LoadNodes_Extended (extended/GL nodes) in p_setup.c.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "doomdef.h"
#include "i_system.h"		// I_Error
#include "doomstat.h"		// gamemode
#include "doomdata.h"		// ML_*, mapthing_t
#include "m_fixed.h"
#include "m_bbox.h"
#include "tables.h"
#include "w_wad.h"
#include "z_zone.h"
#include "r_defs.h"
#include "r_state.h"		// numvertexes/vertexes/... globals
#include "p_udmf.h"

// engine map globals (defined in p_setup.c / r_state)
extern int		numvertexes, numsectors, numsides, numlines;
extern vertex_t*	vertexes;
extern sector_t*	sectors;
extern side_t*		sides;
extern line_t*		lines;

extern int		R_FlatNumForName (char* name);
extern int		R_TextureNumForName (char* name);
extern void		P_SpawnMapThing (mapthing_t* mthing);
extern int		heretic_mode;

// ---------------------------------------------------------------------------
//  Tokenizer
// ---------------------------------------------------------------------------
enum { TK_EOF, TK_ID, TK_STR, TK_PUNCT };

static const char*	sc_p;		// cursor
static const char*	sc_end;
static char		sc_tok[256];
static int		sc_type;

static void SC_Init (const char* buf, int len)
{
    sc_p = buf; sc_end = buf + len;
}

// Is c part of an unquoted token (identifier / number / keyword)?
static int SC_IsWord (int c)
{
    return isalnum (c) || c == '_' || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
}

static void SC_Next (void)
{
    // skip whitespace and // ... and /* ... */ comments
    for (;;)
    {
	while (sc_p < sc_end && (unsigned char)*sc_p <= ' ') sc_p++;
	if (sc_p + 1 < sc_end && sc_p[0] == '/' && sc_p[1] == '/')
	{
	    sc_p += 2;
	    while (sc_p < sc_end && *sc_p != '\n') sc_p++;
	    continue;
	}
	if (sc_p + 1 < sc_end && sc_p[0] == '/' && sc_p[1] == '*')
	{
	    sc_p += 2;
	    while (sc_p + 1 < sc_end && !(sc_p[0] == '*' && sc_p[1] == '/')) sc_p++;
	    sc_p += 2;
	    continue;
	}
	break;
    }

    if (sc_p >= sc_end) { sc_type = TK_EOF; sc_tok[0] = 0; return; }

    {
	char c = *sc_p;
	if (c == '{' || c == '}' || c == '=' || c == ';')
	{
	    sc_tok[0] = c; sc_tok[1] = 0; sc_type = TK_PUNCT; sc_p++;
	    return;
	}
	if (c == '"')
	{
	    int n = 0;
	    sc_p++;
	    while (sc_p < sc_end && *sc_p != '"' && n < (int)sizeof(sc_tok) - 1)
		sc_tok[n++] = *sc_p++;
	    sc_tok[n] = 0;
	    if (sc_p < sc_end) sc_p++;			// closing quote
	    sc_type = TK_STR;
	    return;
	}
	{
	    int n = 0;
	    while (sc_p < sc_end && SC_IsWord ((unsigned char)*sc_p) && n < (int)sizeof(sc_tok) - 1)
		sc_tok[n++] = *sc_p++;
	    sc_tok[n] = 0;
	    if (!n) { sc_p++; SC_Next (); return; }	// stray punctuation -> skip
	    sc_type = TK_ID;
	}
    }
}

static boolean SC_Bool (const char* v) { return (boolean)(!strcasecmp (v, "true")); }

// Parse a UDMF integer.  base 0 -> honours the spec's decimal, octal (0NNN) and
// hex (0xNN) forms (udmf11.txt grammar); plain atoi() would stop at the 'x'.
static int SC_Int (const char* v) { return (int) strtol (v, NULL, 0); }

// Copy a UDMF texture/flat name (<=8 chars) into an upper-cased, NUL-terminated buffer.
static void SC_Name (char* dst, const char* src)
{
    int i;
    for (i = 0; i < 8 && src[i]; i++) dst[i] = toupper ((unsigned char) src[i]);
    dst[i] = 0;
}

// ---------------------------------------------------------------------------
//  Intermediate storage (malloc, freed after the engine arrays are built)
// ---------------------------------------------------------------------------
typedef struct { double x, y; } u_vertex_t;
typedef struct { int hfloor, hceil, light, special, id; char tfloor[9], tceil[9]; } u_sector_t;
typedef struct { int offx, offy, sector; char ttop[9], tmid[9], tbot[9]; } u_side_t;
typedef struct { int v1, v2, sfront, sback, flags, special, id; } u_line_t;
typedef struct { short x, y, angle, type, options; } u_thing_t;

#define GROW(arr, cap, n) do { \
	if ((n) >= (cap)) { (cap) = (cap) ? (cap) * 2 : 128; \
	    (arr) = realloc ((arr), (cap) * sizeof (*(arr))); } } while (0)

static u_vertex_t*	uv;   static int uv_n,  uv_cap;
static u_sector_t*	us;   static int us_n,  us_cap;
static u_side_t*	usd;  static int usd_n, usd_cap;
static u_line_t*	ul;   static int ul_n,  ul_cap;
static u_thing_t*	ut;   static int ut_n,  ut_cap;

static void UDMF_FreeIntermediate (void)
{
    free (uv);  uv  = NULL; uv_n  = uv_cap  = 0;
    free (us);  us  = NULL; us_n  = us_cap  = 0;
    free (usd); usd = NULL; usd_n = usd_cap = 0;
    free (ul);  ul  = NULL; ul_n  = ul_cap  = 0;
    free (ut);  ut  = NULL; ut_n  = ut_cap  = 0;
}

// ---------------------------------------------------------------------------
//  Block parsing.  Each returns after consuming the closing '}'.
// ---------------------------------------------------------------------------
// Read one "key = value ;" inside a block.  Returns 0 at '}' or EOF.
static int UDMF_Field (char* key, char* val, int* vtype)
{
    SC_Next ();
    if (sc_type == TK_EOF || (sc_type == TK_PUNCT && sc_tok[0] == '}')) return 0;
    if (sc_type != TK_ID) return UDMF_Field (key, val, vtype);	// skip strays
    strncpy (key, sc_tok, 63); key[63] = 0;
    SC_Next ();							// '='
    SC_Next ();							// value
    strncpy (val, sc_tok, 255); val[255] = 0; *vtype = sc_type;
    SC_Next ();							// ';'
    return 1;
}

static void UDMF_ParseVertex (void)
{
    u_vertex_t v = { 0, 0 };
    char k[64], val[256]; int vt;
    while (UDMF_Field (k, val, &vt))
    {
	if      (!strcasecmp (k, "x")) v.x = atof (val);
	else if (!strcasecmp (k, "y")) v.y = atof (val);
    }
    GROW (uv, uv_cap, uv_n); uv[uv_n++] = v;
}

static void UDMF_ParseSector (void)
{
    u_sector_t s;
    char k[64], val[256]; int vt;
    memset (&s, 0, sizeof s);
    s.light = 160; strcpy (s.tfloor, "-"); strcpy (s.tceil, "-");
    while (UDMF_Field (k, val, &vt))
    {
	if      (!strcasecmp (k, "heightfloor"))    s.hfloor  = SC_Int (val);
	else if (!strcasecmp (k, "heightceiling"))  s.hceil   = SC_Int (val);
	else if (!strcasecmp (k, "texturefloor"))   SC_Name (s.tfloor, val);
	else if (!strcasecmp (k, "textureceiling")) SC_Name (s.tceil, val);
	else if (!strcasecmp (k, "lightlevel"))     s.light   = SC_Int (val);
	else if (!strcasecmp (k, "special"))        s.special = SC_Int (val);
	else if (!strcasecmp (k, "id"))             s.id      = SC_Int (val);
    }
    GROW (us, us_cap, us_n); us[us_n++] = s;
}

static void UDMF_ParseSide (void)
{
    u_side_t s;
    char k[64], val[256]; int vt;
    memset (&s, 0, sizeof s);
    strcpy (s.ttop, "-"); strcpy (s.tmid, "-"); strcpy (s.tbot, "-");
    while (UDMF_Field (k, val, &vt))
    {
	if      (!strcasecmp (k, "offsetx"))        s.offx = SC_Int (val);
	else if (!strcasecmp (k, "offsety"))        s.offy = SC_Int (val);
	else if (!strcasecmp (k, "texturetop"))     SC_Name (s.ttop, val);
	else if (!strcasecmp (k, "texturemiddle"))  SC_Name (s.tmid, val);
	else if (!strcasecmp (k, "texturebottom"))  SC_Name (s.tbot, val);
	else if (!strcasecmp (k, "sector"))         s.sector = SC_Int (val);
    }
    GROW (usd, usd_cap, usd_n); usd[usd_n++] = s;
}

static void UDMF_ParseLine (void)
{
    u_line_t l;
    char k[64], val[256]; int vt;
    memset (&l, 0, sizeof l);
    l.sback = -1;					// one-sided by default
    while (UDMF_Field (k, val, &vt))
    {
	if      (!strcasecmp (k, "v1"))         l.v1     = SC_Int (val);
	else if (!strcasecmp (k, "v2"))         l.v2     = SC_Int (val);
	else if (!strcasecmp (k, "sidefront"))  l.sfront = SC_Int (val);
	else if (!strcasecmp (k, "sideback"))   l.sback  = SC_Int (val);
	else if (!strcasecmp (k, "special"))    l.special = SC_Int (val);
	else if (!strcasecmp (k, "id"))         l.id     = SC_Int (val);
	// flags -> ML_* (same bit values as this engine's doomdata.h)
	else if (SC_Bool (val))
	{
	    if      (!strcasecmp (k, "blocking"))          l.flags |= ML_BLOCKING;
	    else if (!strcasecmp (k, "blockmonsters"))     l.flags |= ML_BLOCKMONSTERS;
	    else if (!strcasecmp (k, "twosided"))          l.flags |= ML_TWOSIDED;
	    else if (!strcasecmp (k, "dontpegtop"))        l.flags |= ML_DONTPEGTOP;
	    else if (!strcasecmp (k, "dontpegbottom"))     l.flags |= ML_DONTPEGBOTTOM;
	    else if (!strcasecmp (k, "secret"))            l.flags |= ML_SECRET;
	    else if (!strcasecmp (k, "blocksound"))        l.flags |= ML_SOUNDBLOCK;
	    else if (!strcasecmp (k, "dontdraw"))          l.flags |= ML_DONTDRAW;
	    else if (!strcasecmp (k, "mapped"))            l.flags |= ML_MAPPED;
	    else if (!strcasecmp (k, "passuse"))           l.flags |= ML_PASSUSE;
	    else if (!strcasecmp (k, "blocklandmonsters")) l.flags |= ML_BLOCKLANDMONSTERS;
	    else if (!strcasecmp (k, "blockplayers"))      l.flags |= ML_BLOCKPLAYERS;
	}
    }
    GROW (ul, ul_cap, ul_n); ul[ul_n++] = l;
}

static void UDMF_ParseThing (void)
{
    double x = 0, y = 0;
    int angle = 0, type = 0;
    int sk12 = 0, sk3 = 0, sk45 = 0, ambush = 0, single = 0;
    char k[64], val[256]; int vt, opt;
    u_thing_t t;
    while (UDMF_Field (k, val, &vt))
    {
	if      (!strcasecmp (k, "x"))      x = atof (val);
	else if (!strcasecmp (k, "y"))      y = atof (val);
	else if (!strcasecmp (k, "angle"))  angle = SC_Int (val);
	else if (!strcasecmp (k, "type"))   type = SC_Int (val);
	else if (!strcasecmp (k, "skill1") || !strcasecmp (k, "skill2")) { if (SC_Bool (val)) sk12 = 1; }
	else if (!strcasecmp (k, "skill3"))                              { if (SC_Bool (val)) sk3  = 1; }
	else if (!strcasecmp (k, "skill4") || !strcasecmp (k, "skill5")) { if (SC_Bool (val)) sk45 = 1; }
	else if (!strcasecmp (k, "ambush"))  ambush = SC_Bool (val);
	else if (!strcasecmp (k, "single"))  single = SC_Bool (val);
    }
    // Rebuild the vanilla options byte (P_SpawnMapThing decodes these bits).
    opt = 0;
    if (sk12)   opt |= 1;		// ITYTD + HNTR
    if (sk3)    opt |= 2;		// HMP
    if (sk45)   opt |= 4;		// UV + NM
    if (ambush) opt |= 8;		// MTF_AMBUSH (deaf)
    if (!single) opt |= 16;		// "not in single player" (net/co-op only)
    t.x = (short) lround (x); t.y = (short) lround (y);
    t.angle = (short) angle; t.type = (short) type; t.options = (short) opt;
    GROW (ut, ut_cap, ut_n); ut[ut_n++] = t;
}

// ---------------------------------------------------------------------------
//  Top-level parse
// ---------------------------------------------------------------------------
static void UDMF_ParseNamespace (const char* ns)
{
    // Only the vanilla-geometry namespaces are meaningful here: doom/heretic/strife
    // keep id==tag and use classic specials.  hexen/zdoom/eternity imply parameterised
    // specials + args this playsim can't execute, so reject them loudly.
    if (strcasecmp (ns, "doom") && strcasecmp (ns, "heretic") && strcasecmp (ns, "strife"))
	I_Error ("UDMF: unsupported namespace \"%s\" -- only the doom namespace is supported.", ns);
}

static void UDMF_Parse (const char* buf, int len)
{
    char key[64];
    SC_Init (buf, len);
    for (;;)
    {
	SC_Next ();
	if (sc_type == TK_EOF) break;
	if (sc_type != TK_ID) continue;
	strncpy (key, sc_tok, 63); key[63] = 0;
	SC_Next ();					// '{' (block) or '=' (global)
	if (sc_type == TK_PUNCT && sc_tok[0] == '{')
	{
	    if      (!strcasecmp (key, "vertex"))  UDMF_ParseVertex ();
	    else if (!strcasecmp (key, "linedef")) UDMF_ParseLine ();
	    else if (!strcasecmp (key, "sidedef")) UDMF_ParseSide ();
	    else if (!strcasecmp (key, "sector"))  UDMF_ParseSector ();
	    else if (!strcasecmp (key, "thing"))   UDMF_ParseThing ();
	    else						// unknown block -> skip to '}'
		while (!(sc_type == TK_EOF || (sc_type == TK_PUNCT && sc_tok[0] == '}'))) SC_Next ();
	}
	else if (sc_type == TK_PUNCT && sc_tok[0] == '=')
	{
	    SC_Next ();					// value
	    if (!strcasecmp (key, "namespace")) UDMF_ParseNamespace (sc_tok);
	    SC_Next ();					// ';'
	}
    }
}

// ---------------------------------------------------------------------------
//  Build the engine arrays from the parsed intermediates (mirrors P_Load*).
// ---------------------------------------------------------------------------
static int UDMF_FlatNum (const char* n)
{
    return (!n[0] || (n[0] == '-' && !n[1])) ? 0 : R_FlatNumForName ((char*) n);
}

static void UDMF_BuildVertexes (void)
{
    int i;
    numvertexes = uv_n;
    vertexes = Z_Malloc (numvertexes * sizeof (vertex_t), PU_LEVEL, 0);
    for (i = 0; i < numvertexes; i++)
    {
	vertexes[i].x = (fixed_t) lround (uv[i].x * FRACUNIT);
	vertexes[i].y = (fixed_t) lround (uv[i].y * FRACUNIT);
    }
}

static void UDMF_BuildSectors (void)
{
    int i;
    numsectors = us_n;
    sectors = Z_Malloc (numsectors * sizeof (sector_t), PU_LEVEL, 0);
    memset (sectors, 0, numsectors * sizeof (sector_t));
    for (i = 0; i < numsectors; i++)
    {
	sector_t* ss = &sectors[i];
	ss->floorheight   = us[i].hfloor << FRACBITS;
	ss->ceilingheight  = us[i].hceil  << FRACBITS;
	ss->floorpic       = UDMF_FlatNum (us[i].tfloor);
	ss->ceilingpic     = UDMF_FlatNum (us[i].tceil);
	ss->lightlevel     = us[i].light;
	ss->special        = us[i].special;
	ss->tag            = us[i].id;
	ss->thinglist      = NULL;
	ss->heightsec      = -1;
	ss->floorlightsec  = ss->ceilinglightsec = -1;
	ss->sky            = 0;
    }
}

static void UDMF_BuildSides (void)
{
    int i;
    numsides = usd_n;
    sides = Z_Malloc (numsides * sizeof (side_t), PU_LEVEL, 0);
    memset (sides, 0, numsides * sizeof (side_t));
    for (i = 0; i < numsides; i++)
    {
	side_t* sd = &sides[i];
	int sec = usd[i].sector;
	if (sec < 0 || sec >= numsectors) sec = 0;
	sd->textureoffset = usd[i].offx << FRACBITS;
	sd->rowoffset     = usd[i].offy << FRACBITS;
	sd->toptexture    = R_TextureNumForName (usd[i].ttop);
	sd->bottomtexture = R_TextureNumForName (usd[i].tbot);
	sd->midtexture    = R_TextureNumForName (usd[i].tmid);
	sd->sector        = &sectors[sec];
    }
}

static void UDMF_BuildLines (void)
{
    int i, j;
    numlines = ul_n;
    lines = Z_Malloc (numlines * sizeof (line_t), PU_LEVEL, 0);
    memset (lines, 0, numlines * sizeof (line_t));
    for (i = 0; i < numlines; i++)
    {
	line_t*   ld = &lines[i];
	vertex_t* v1;
	vertex_t* v2;
	int s0 = ul[i].sfront, s1 = ul[i].sback;

	ld->flags   = ul[i].flags;
	ld->special = ul[i].special;
	ld->tag     = ul[i].id;
	v1 = ld->v1 = &vertexes[(ul[i].v1 >= 0 && ul[i].v1 < numvertexes) ? ul[i].v1 : 0];
	v2 = ld->v2 = &vertexes[(ul[i].v2 >= 0 && ul[i].v2 < numvertexes) ? ul[i].v2 : 0];
	ld->dx = v2->x - v1->x;
	ld->dy = v2->y - v1->y;

	if (!ld->dx)      ld->slopetype = ST_VERTICAL;
	else if (!ld->dy) ld->slopetype = ST_HORIZONTAL;
	else              ld->slopetype = (FixedDiv (ld->dy, ld->dx) > 0) ? ST_POSITIVE : ST_NEGATIVE;

	ld->bbox[BOXLEFT]   = (v1->x < v2->x) ? v1->x : v2->x;
	ld->bbox[BOXRIGHT]  = (v1->x < v2->x) ? v2->x : v1->x;
	ld->bbox[BOXBOTTOM] = (v1->y < v2->y) ? v1->y : v2->y;
	ld->bbox[BOXTOP]    = (v1->y < v2->y) ? v2->y : v1->y;

	if (s0 < 0 || s0 >= numsides) s0 = -1;
	if (s1 < 0 || s1 >= numsides) s1 = -1;
	ld->sidenum[0] = s0;
	ld->sidenum[1] = s1;
	if (ld->sidenum[0] == -1) ld->sidenum[0] = 0;		// dummy right side

	ld->frontsector = (ld->sidenum[0] != -1) ? sides[ld->sidenum[0]].sector : 0;
	ld->backsector  = (ld->sidenum[1] != -1) ? sides[ld->sidenum[1]].sector : 0;

	ld->tranlump   = -1;
	ld->frontmusic = ld->backmusic = -1;
    }

    // Boom 260 translucent 2S middle textures (killough) -- same pass as P_LoadLineDefs.
    for (i = 0; i < numlines; i++)
	if (lines[i].special == 260)
	{
	    if (!lines[i].tag) lines[i].tranlump = 0;
	    else for (j = 0; j < numlines; j++)
		if (lines[j].tag == lines[i].tag) lines[j].tranlump = 0;
	}
}

// ---------------------------------------------------------------------------
//  Sub-lump scan + public entry points
// ---------------------------------------------------------------------------
static int udmf_znodes = -1, udmf_blockmap = -1, udmf_reject = -1;

int UDMF_ZnodesLump   (void) { return udmf_znodes; }
int UDMF_BlockmapLump (void) { return udmf_blockmap; }
int UDMF_RejectLump   (void) { return udmf_reject; }

// Compare a lump-directory name (8 bytes, NUL-padded) to a C string.
static boolean UDMF_LumpIs (int lump, const char* name)
{
    return (lump >= 0 && lump < numlumps
	    && !strncasecmp (lumpinfo[lump].name, name, 8));
}

boolean UDMF_IsMap (int lumpnum)
{
    return UDMF_LumpIs (lumpnum + 1, "TEXTMAP");
}

void UDMF_LoadMap (int lumpnum)
{
    int   textmap = lumpnum + 1;
    int   len = W_LumpLength (textmap);
    char* raw = Z_Malloc (len + 1, PU_STATIC, 0);
    int   j;

    memcpy (raw, W_CacheLumpNum (textmap, PU_CACHE), len);
    raw[len] = 0;

    // find the map's sub-lumps: everything between TEXTMAP and ENDMAP.
    udmf_znodes = udmf_blockmap = udmf_reject = -1;
    for (j = textmap + 1; j < numlumps; j++)
    {
	if (UDMF_LumpIs (j, "ENDMAP")) break;
	if      (UDMF_LumpIs (j, "ZNODES"))   udmf_znodes   = j;
	else if (UDMF_LumpIs (j, "BLOCKMAP")) udmf_blockmap = j;
	else if (UDMF_LumpIs (j, "REJECT"))   udmf_reject   = j;
    }

    UDMF_FreeIntermediate ();
    UDMF_Parse (raw, len);
    Z_Free (raw);

    // order matters: sides reference sectors, lines reference vertexes+sides.
    UDMF_BuildVertexes ();
    UDMF_BuildSectors ();
    UDMF_BuildSides ();
    UDMF_BuildLines ();

    printf ("UDMF: %d verts, %d sectors, %d sides, %d lines, %d things\n",
	    numvertexes, numsectors, numsides, numlines, ut_n);
}

void UDMF_LoadThings (void)
{
    int i;
    for (i = 0; i < ut_n; i++)
    {
	mapthing_t mt = { ut[i].x, ut[i].y, ut[i].angle, ut[i].type, ut[i].options };

	// Do not spawn DOOM II monsters when running a non-commercial IWAD (same
	// list as P_LoadThings; skipped for Heretic where those ednums differ).
	if (gamemode != commercial && !heretic_mode)
	{
	    switch (mt.type)
	    {
	      case 64: case 65: case 66: case 67: case 68: case 69:
	      case 71: case 84: case 88: case 89:
		continue;
	    }
	}
	P_SpawnMapThing (&mt);
    }
    UDMF_FreeIntermediate ();
}
