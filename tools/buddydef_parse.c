// buddydef_parse.c -- BUDDYDEF text parser/serializer for the editor tool.
//
// Mirrors files/p_buddydef.c exactly: field names, defaults, aliases, brace
// handling, key/value quoting and trailing-comment stripping all match.  The
// engine's parser is forgiving; we keep the same latitude so a file written
// by this tool is byte-for-byte reloadable by the engine.

#include "buddydef_parse.h"
#include "buddydef_wad.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>

// ----------------------------------------------------------------- defaults
// Exactly mirrors Buddy_Defaults() in p_buddydef.c.  Updates the entry's
// "set_*" mask too: defaults clear all bits so that buddydef_serialize()
// emits only fields the user explicitly set.
void buddydef_defaults(buddydef_entry_t* b)
{
    memset(b, 0, sizeof *b);
    strncpy(b->name,  "Buddy", sizeof b->name  - 1);
    strncpy(b->sprite, "PLAY", sizeof b->sprite - 1);
    strncpy(b->attack, "melee", sizeof b->attack - 1);
    strncpy(b->ability, "none", sizeof b->ability - 1);
    b->health = 200; b->speed = 8; b->radius = 20; b->height = 56;
    b->mass = 100;   b->painchance = 120; b->reactiontime = 8; b->ednum = -1;
    b->color[0] = 0;
}

// ----------------------------------------------------------------- value
// Extract the value part of a "key value" line: skip leading ws/':'/'=', strip
// surrounding quotes, drop a trailing comment.  Same logic as Buddy_Value() in
// p_buddydef.c.
static void grab_value(const char* p, char* dst, int cap)
{
    int n = 0;
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=') p++;
    if (*p == '"')
    {
        p++;
        while (*p && *p != '"' && n < cap-1) dst[n++] = *p++;
    }
    else
    {
        while (*p && *p != '#' && *p != '\r' && *p != '\n' && n < cap-1)
            dst[n++] = *p++;
        while (n > 0 && (dst[n-1] == ' ' || dst[n-1] == '\t' || dst[n-1] == ',' || dst[n-1] == '{'))
            n--;
    }
    dst[n] = 0;
}

// ----------------------------------------------------------------- parser
typedef struct {
    buddydef_entry_t* a;
    int n, cap;
} vec_t;

static void vec_push(vec_t* v, const buddydef_entry_t* b)
{
    if (v->n >= v->cap) {
        int nc = v->cap ? v->cap * 2 : 8;
        buddydef_entry_t* na = (buddydef_entry_t*)realloc(v->a, nc * sizeof *na);
        if (!na) return;
        v->a = na; v->cap = nc;
    }
    v->a[v->n++] = *b;
}

int buddydef_parse(const char* text, int len, buddydef_entry_t** out)
{
    *out = NULL;
    vec_t v = { 0, 0, 0 };

    char line[512];
    buddydef_entry_t cur;
    int inrec = 0;
    int i = 0;

    buddydef_defaults(&cur);

    while (i < len)
    {
        int n = 0;
        while (i < len && text[i] != '\n' && n < (int)sizeof(line)-1) line[n++] = (char)text[i++];
        if (i < len && text[i] == '\n') i++;
        line[n] = 0;

        char* c = strchr(line, '#'); if (c) *c = 0;
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;

        // Brace handling (may sit alone or after "buddy")
        if (*p == '{') { buddydef_defaults(&cur); inrec = 1; continue; }
        if (*p == '}') { if (inrec) vec_push(&v, &cur); inrec = 0; continue; }

        // First token = key (lowercased)
        char key[32]; int k = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ':' && *p != '=' && *p != '{' && k < 31)
            key[k++] = (char)tolower((unsigned char)*p++);
        key[k] = 0;

        if (!strcmp(key, "buddy")) {
            if (strchr(p, '{')) { buddydef_defaults(&cur); inrec = 1; }
            continue;
        }
        if (!inrec) continue;

        // ---- string fields
        if (!strcmp(key, "name")) {
            grab_value(p, cur.name, sizeof cur.name); cur.set_name = 1;
        }
        else if (!strcmp(key, "desc") || !strcmp(key, "about") || !strcmp(key, "info")) {
            grab_value(p, cur.desc, sizeof cur.desc); cur.set_desc = 1;
        }
        else if (!strcmp(key, "sprite")) {
            grab_value(p, cur.sprite, sizeof cur.sprite); cur.set_sprite = 1;
        }
        else if (!strcmp(key, "attack")) {
            grab_value(p, cur.attack, sizeof cur.attack); cur.set_attack = 1;
        }
        else if (!strcmp(key, "seesound")) {
            grab_value(p, cur.seesnd, sizeof cur.seesnd); cur.set_seesnd = 1;
        }
        else if (!strcmp(key, "painsound")) {
            grab_value(p, cur.painsnd, sizeof cur.painsnd); cur.set_painsnd = 1;
        }
        else if (!strcmp(key, "deathsound")) {
            grab_value(p, cur.deathsnd, sizeof cur.deathsnd); cur.set_deathsnd = 1;
        }
        else if (!strcmp(key, "activesound")) {
            grab_value(p, cur.activesnd, sizeof cur.activesnd); cur.set_activesnd = 1;
        }
        else if (!strcmp(key, "special") || !strcmp(key, "abilities")) {
            grab_value(p, cur.special, sizeof cur.special); cur.set_special = 1;
        }
        else if (!strcmp(key, "ability")) {
            grab_value(p, cur.ability, sizeof cur.ability); cur.set_ability = 1;
        }
        else if (!strcmp(key, "color") || !strcmp(key, "colour")) {
            grab_value(p, cur.color, sizeof cur.color); cur.set_color = 1;
        }
        // ---- integer fields
        else
        {
            char vbuf[32]; grab_value(p, vbuf, sizeof vbuf);
            int iv = atoi(vbuf);
            if      (!strcmp(key, "health") || !strcmp(key, "hp"))                   { cur.health = iv; cur.set_health = 1; }
            else if (!strcmp(key, "speed"))                                          { cur.speed = iv; cur.set_speed = 1; }
            else if (!strcmp(key, "radius"))                                         { cur.radius = iv; cur.set_radius = 1; }
            else if (!strcmp(key, "height"))                                         { cur.height = iv; cur.set_height = 1; }
            else if (!strcmp(key, "mass"))                                           { cur.mass = iv; cur.set_mass = 1; }
            else if (!strcmp(key, "painchance"))                                     { cur.painchance = iv; cur.set_painchance = 1; }
            else if (!strcmp(key, "reactiontime") || !strcmp(key, "reaction"))       { cur.reactiontime = iv; cur.set_reactiontime = 1; }
            else if (!strcmp(key, "ednum") || !strcmp(key, "doomednum"))             { cur.ednum = iv; cur.set_ednum = 1; }
        }
    }
    if (inrec) vec_push(&v, &cur);   // unclosed record still counts (engine does this)

    *out = v.a;
    int rcount = v.n;
    return rcount;
}

// ----------------------------------------------------------------- serialize
// Tagged growable buffer.
typedef struct { char* s; int n; int cap; } sbuf_t;
static void sb_putc(sbuf_t* b, char c)
{
    if (b->n + 1 >= b->cap) {
        int nc = b->cap ? b->cap * 2 : 256;
        char* ns = (char*)realloc(b->s, nc);
        if (!ns) return;
        b->s = ns; b->cap = nc;
    }
    b->s[b->n++] = c;
}
static void sb_puts(sbuf_t* b, const char* s)
{
    while (*s) sb_putc(b, *s++);
}
static void sb_printf(sbuf_t* b, const char* fmt, ...)
{
    char tmp[64];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n < (int)sizeof tmp) { for (int i = 0; i < n; i++) sb_putc(b, tmp[i]); return; }
    char* big = (char*)malloc(n + 1);
    if (!big) return;
    va_start(ap, fmt); vsnprintf(big, n + 1, fmt, ap); va_end(ap);
    for (int i = 0; i < n; i++) sb_putc(b, big[i]);
    free(big);
}

// Wrap a string in quotes if it contains whitespace, '#', or non-ASCII chars.
// Same heuristic doom builders use; the engine's parser handles both.
static int needs_quotes(const char* s)
{
    for (const char* p = s; *p; p++)
        if (*p <= ' ' || *p == '#' || *p == '"' || (unsigned char)*p > 127) return 1;
    return 0;
}

// Emit `key   value\n` -- for free-text fields that always want quotes-on-content.
static void emit_field(sbuf_t* b, const char* key, const char* val, int is_set)
{
    if (!is_set) return;
    if (!val) val = "";
    sb_printf(b, "  %-12s ", key);
    if (needs_quotes(val) || strchr(val, '\n')) {
        sb_putc(b, '"');
        for (const char* p = val; *p; p++) {
            if (*p == '"' || *p == '\\') sb_putc(b, '\\');
            sb_putc(b, *p);
        }
        sb_putc(b, '"');
    } else sb_puts(b, val);
    sb_putc(b, '\n');
}

// Emit a string-keyed field, suppressing the line when the value equals the
// engine's default.  Keeps the file tidy and small.
static void emit_str(sbuf_t* b, const char* key, const char* val,
                     int is_set, const char* default_to)
{
    if (!is_set) return;
    if (default_to && !strcmp(val, default_to)) return;
    sb_printf(b, "  %-12s ", key);
    if (needs_quotes(val) || strchr(val, '\n')) {
        sb_putc(b, '"');
        for (const char* p = val; *p; p++) {
            if (*p == '"' || *p == '\\') sb_putc(b, '\\');
            sb_putc(b, *p);
        }
        sb_putc(b, '"');
    } else sb_puts(b, val);
    sb_putc(b, '\n');
}

// Emit an int-keyed field, suppressing the line when the value equals the
// engine's default for the same field.
static void emit_int(sbuf_t* b, const char* key, int v, int is_set, int defaults_to)
{
    if (!is_set) return;
    if (v == defaults_to) return;
    sb_printf(b, "  %-12s %d\n", key, v);
}

char* buddydef_serialize(const buddydef_entry_t* b)
{
    sbuf_t buf = { 0, 0, 0 };
    sb_puts(&buf, "buddy {\n");
    emit_field(&buf, "name",        b->name,        b->set_name);
    emit_field(&buf, "desc",        b->desc,        b->set_desc);
    emit_str  (&buf, "sprite",      b->sprite,      b->set_sprite,    "PLAY");
    emit_int  (&buf, "health",      b->health,      b->set_health,    200);
    emit_int  (&buf, "speed",       b->speed,       b->set_speed,     8);
    emit_int  (&buf, "radius",      b->radius,      b->set_radius,    20);
    emit_int  (&buf, "height",      b->height,      b->set_height,    56);
    emit_int  (&buf, "mass",        b->mass,        b->set_mass,      100);
    emit_int  (&buf, "painchance",  b->painchance,  b->set_painchance, 120);
    emit_int  (&buf, "reactiontime", b->reactiontime, b->set_reactiontime, 8);
    emit_str  (&buf, "attack",      b->attack,      b->set_attack, "melee");
    emit_field(&buf, "special",     b->special,     b->set_special);
    emit_str  (&buf, "ability",     b->ability,     b->set_ability, "none");
    emit_str  (&buf, "color",       b->color,       b->set_color,    "");
    emit_str  (&buf, "seesound",    b->seesnd,      b->set_seesnd,  "");
    emit_str  (&buf, "painsound",   b->painsnd,     b->set_painsnd, "");
    emit_str  (&buf, "deathsound",  b->deathsnd,    b->set_deathsnd, "");
    emit_str  (&buf, "activesound", b->activesnd,   b->set_activesnd, "");
    if (b->set_ednum && b->ednum != -1) sb_printf(&buf, "  ednum       %d\n", b->ednum);
    sb_puts(&buf, "}\n");
    if (!buf.s) return strdup("");
    sb_putc(&buf, 0);
    return buf.s;
}

char* buddydef_serialize_all(const buddydef_entry_t* b, int n)
{
    sbuf_t buf = { 0, 0, 0 };
    for (int i = 0; i < n; i++) {
        char* one = buddydef_serialize(&b[i]);
        if (!one) continue;
        for (const char* p = one; *p; p++) sb_putc(&buf, *p);
        if (i + 1 < n) sb_putc(&buf, '\n');          // blank line between records
        free(one);
    }
    if (!buf.s) return strdup("");
    sb_putc(&buf, 0);
    return buf.s;
}

// ----------------------------------------------------------------- from WAD
int buddydef_load_from_wad(buddydef_wad_t* w, buddydef_entry_t** out, int* n)
{
    int idx = buddydef_wad_find(w, "BUDDYDEF");
    if (idx < 0) { *out = NULL; *n = 0; return -1; }
    const char* data = (const char*)w->data[idx];
    unsigned size = w->dir[idx].size;
    if (!data || size == 0) { *out = NULL; *n = 0; return -1; }
    *n = buddydef_parse(data, (int)size, out);
    return 0;
}
