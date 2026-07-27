// buddydef_parse.cpp -- see buddydef_parse.hpp.
//
// The parsing rules follow Buddy_ParseText() in files/p_buddydef.c line for line:
// case-insensitive keys, '#' comments, braces that may sit alone or after `buddy`,
// ':' / '=' / whitespace as the separator, optional quotes, a trailing comma or '{'
// trimmed off a bare value, and an unclosed record at EOF still counting.

#include "buddydef_parse.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace buddy {

namespace {

// Value part of a "key value" line.  Same logic as Buddy_Value() in p_buddydef.c.
std::string grab_value(const std::string& s, size_t from)
{
    size_t p = from;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == ':' || s[p] == '=')) p++;

    std::string out;
    if (p < s.size() && s[p] == '"') {
        for (p++; p < s.size() && s[p] != '"'; p++) out.push_back(s[p]);
        return out;
    }
    for (; p < s.size() && s[p] != '#' && s[p] != '\r' && s[p] != '\n'; p++) out.push_back(s[p]);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' ||
                            out.back() == ',' || out.back() == '{'))
        out.pop_back();
    return out;
}

std::string lower(const std::string& s)
{
    std::string o = s;
    for (char& c : o) c = (char)tolower((unsigned char)c);
    return o;
}

// A bare value only needs quoting when it holds something the parser would otherwise
// eat: whitespace, a comment marker, a separator or a brace.
bool needs_quotes(const std::string& v)
{
    if (v.empty()) return false;
    for (char c : v)
        if (c == ' ' || c == '\t' || c == '#' || c == '"' ||
            c == ',' || c == '{' || c == '}' || c == ':' || c == '=')
            return true;
    return false;
}

void emit(std::string& out, const char* key, const std::string& val, bool set,
          const char* skip_when = nullptr)
{
    if (!set) return;
    if (skip_when && val == skip_when) return;              // identical to the default

    char pad[32];
    snprintf(pad, sizeof pad, "  %-12s ", key);
    out += pad;
    if (needs_quotes(val)) {
        out += '"';
        for (char c : val) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += '"';
    } else {
        out += val;
    }
    out += '\n';
}

void emit_int(std::string& out, const char* key, int val, bool set, int skip_when)
{
    if (!set || val == skip_when) return;
    char line[64];
    snprintf(line, sizeof line, "  %-12s %d\n", key, val);
    out += line;
}

}  // namespace

std::vector<Buddy> parse(const std::string& text)
{
    std::vector<Buddy> out;
    Buddy cur;
    bool  inrec = false;

    size_t i = 0;
    while (i <= text.size()) {
        // one line
        size_t eol = text.find('\n', i);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(i, eol - i);
        if (eol >= text.size()) i = text.size() + 1; else i = eol + 1;

        {   // strip comment + leading whitespace
            const size_t h = line.find('#');
            if (h != std::string::npos) line.resize(h);
        }
        size_t p = 0;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
        if (p >= line.size()) continue;

        if (line[p] == '{') { cur = Buddy(); inrec = true; continue; }
        if (line[p] == '}') { if (inrec) out.push_back(cur); inrec = false; continue; }

        std::string key;
        for (; p < line.size() && line[p] != ' ' && line[p] != '\t' &&
               line[p] != ':' && line[p] != '=' && line[p] != '{'; p++)
            key.push_back((char)tolower((unsigned char)line[p]));

        if (key == "buddy") {                       // "buddy {" or "buddy" then "{"
            if (line.find('{', p) != std::string::npos) { cur = Buddy(); inrec = true; }
            continue;
        }
        if (!inrec) continue;                       // stray line outside a record

        const std::string v = grab_value(line, p);

        auto str = [&](std::string& dst, Key k) { dst = v; cur.set(k); };
        auto num = [&](int& dst, Key k) { dst = atoi(v.c_str()); cur.set(k); };

        if      (key == "name")                              str(cur.name, Key::Name);
        else if (key == "desc" || key == "about" || key == "info")
                                                             str(cur.desc, Key::Desc);
        else if (key == "sprite")                            str(cur.sprite, Key::Sprite);
        else if (key == "attack")                            str(cur.attack, Key::Attack);
        else if (key == "seesound")                          str(cur.seesnd, Key::SeeSound);
        else if (key == "painsound")                         str(cur.painsnd, Key::PainSound);
        else if (key == "deathsound")                        str(cur.deathsnd, Key::DeathSound);
        else if (key == "activesound")                       str(cur.activesnd, Key::ActiveSound);
        else if (key == "special" || key == "abilities")     str(cur.special, Key::Special);
        else if (key == "ability")                           str(cur.ability, Key::Ability);
        else if (key == "color" || key == "colour")          str(cur.color, Key::Color);
        else if (key == "health" || key == "hp")             num(cur.health, Key::Health);
        else if (key == "speed")                             num(cur.speed, Key::Speed);
        else if (key == "radius")                            num(cur.radius, Key::Radius);
        else if (key == "height")                            num(cur.height, Key::Height);
        else if (key == "mass")                              num(cur.mass, Key::Mass);
        else if (key == "painchance")                        num(cur.painchance, Key::PainChance);
        else if (key == "reactiontime" || key == "reaction") num(cur.reactiontime, Key::ReactionTime);
        else if (key == "ednum" || key == "doomednum")       num(cur.ednum, Key::Ednum);
        // anything else: silently ignored, exactly like the engine
    }
    if (inrec) out.push_back(cur);                  // unclosed record still counts

    return out;
}

std::string serialize(const Buddy& b)
{
    std::string out = "buddy {\n";
    emit    (out, "name",        b.name,      b.has(Key::Name));
    emit    (out, "desc",        b.desc,      b.has(Key::Desc));
    emit    (out, "sprite",      b.sprite,    b.has(Key::Sprite),       "PLAY");
    emit_int(out, "health",      b.health,    b.has(Key::Health),        200);
    emit_int(out, "speed",       b.speed,     b.has(Key::Speed),           8);
    emit_int(out, "radius",      b.radius,    b.has(Key::Radius),         20);
    emit_int(out, "height",      b.height,    b.has(Key::Height),         56);
    emit_int(out, "mass",        b.mass,      b.has(Key::Mass),          100);
    emit_int(out, "painchance",  b.painchance, b.has(Key::PainChance),   120);
    emit_int(out, "reactiontime", b.reactiontime, b.has(Key::ReactionTime), 8);
    emit    (out, "attack",      b.attack,    b.has(Key::Attack),      "melee");
    emit    (out, "special",     b.special,   b.has(Key::Special));
    emit    (out, "ability",     b.ability,   b.has(Key::Ability),      "none");
    emit    (out, "color",       b.color,     b.has(Key::Color),            "");
    emit    (out, "seesound",    b.seesnd,    b.has(Key::SeeSound),         "");
    emit    (out, "painsound",   b.painsnd,   b.has(Key::PainSound),        "");
    emit    (out, "deathsound",  b.deathsnd,  b.has(Key::DeathSound),       "");
    emit    (out, "activesound", b.activesnd, b.has(Key::ActiveSound),      "");
    emit_int(out, "ednum",       b.ednum,     b.has(Key::Ednum),            -1);
    out += "}\n";
    return out;
}

std::string serialize_all(const std::vector<Buddy>& all)
{
    std::string out;
    for (size_t i = 0; i < all.size(); i++) {
        out += serialize(all[i]);
        if (i + 1 < all.size()) out += '\n';        // blank line between records
    }
    return out;
}

std::vector<Buddy> load_from_wad(const Wad& w)
{
    const std::vector<uint8_t>* d = w.data("BUDDYDEF");
    if (!d || d->empty()) return {};
    return parse(std::string((const char*)d->data(), d->size()));
}

}  // namespace buddy
