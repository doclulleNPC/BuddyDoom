// buddydef_wad.cpp -- see buddydef_wad.hpp.

#include "buddydef_wad.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace buddy {

namespace {

uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v);        p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);  p[3] = (uint8_t)(v >> 24);
}

}  // namespace

std::string wad_name(const std::string& raw)
{
    std::string out;
    for (char c : raw) {
        if (out.size() >= 8) break;
        if (isspace((unsigned char)c)) break;
        out.push_back((char)toupper((unsigned char)c));
    }
    return out;
}

bool Wad::load(const std::string& path)
{
    lumps_.clear();
    id_ = "PWAD";

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 ||
        (memcmp(hdr, "IWAD", 4) != 0 && memcmp(hdr, "PWAD", 4) != 0)) {
        fclose(f);
        return false;
    }

    id_.assign((const char*)hdr, 4);
    const uint32_t numlumps = rd32(hdr + 4);
    const uint32_t diroff   = rd32(hdr + 8);
    if (numlumps > 100000u) { fclose(f); return false; }   // not a WAD we want to touch

    std::vector<uint8_t> dir(numlumps * 16u);
    if (numlumps && (fseek(f, (long)diroff, SEEK_SET) != 0 ||
                     fread(dir.data(), 16, numlumps, f) != numlumps)) {
        fclose(f);
        return false;
    }

    lumps_.reserve(numlumps);
    for (uint32_t i = 0; i < numlumps; i++) {
        const uint8_t* e = dir.data() + i * 16u;
        Lump l;
        // The on-disk name is exactly 8 bytes, NUL-padded (CLAUDE.md's lump-name rule).
        {
            const char* n = (const char*)e + 8;
            size_t len = 0;
            while (len < 8 && n[len]) len++;
            l.name.assign(n, len);
        }
        const uint32_t pos = rd32(e), sz = rd32(e + 4);
        if (sz) {
            l.data.resize(sz);
            if (fseek(f, (long)pos, SEEK_SET) != 0 || fread(l.data.data(), 1, sz, f) != sz)
                l.data.clear();                     // unreadable lump -> keep the entry, drop the bytes
        }
        lumps_.push_back(std::move(l));
    }
    fclose(f);
    return true;
}

bool Wad::save(const std::string& path) const
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    // Layout: header, lump data back to back, directory last.  Every loader (this
    // engine included) just seeks to infotableofs, so the order is free.
    uint8_t hdr[12] = { 'P', 'W', 'A', 'D' };
    memcpy(hdr, id_.data(), id_.size() == 4 ? 4 : 0);
    wr32(hdr + 4, (uint32_t)lumps_.size());
    wr32(hdr + 8, 0);                               // patched below
    fwrite(hdr, 1, 12, f);

    std::vector<uint32_t> pos(lumps_.size());
    uint32_t cursor = 12;
    for (size_t i = 0; i < lumps_.size(); i++) {
        pos[i] = cursor;                            // empty lumps keep the running offset
        if (!lumps_[i].data.empty()) {
            fwrite(lumps_[i].data.data(), 1, lumps_[i].data.size(), f);
            cursor += (uint32_t)lumps_[i].data.size();
        }
    }

    const uint32_t diroff = cursor;
    for (size_t i = 0; i < lumps_.size(); i++) {
        uint8_t e[16] = { 0 };
        wr32(e,     pos[i]);
        wr32(e + 4, (uint32_t)lumps_[i].data.size());
        const std::string n = wad_name(lumps_[i].name);
        memcpy(e + 8, n.data(), n.size());          // rest stays NUL
        fwrite(e, 1, 16, f);
    }

    fseek(f, 8, SEEK_SET);
    uint8_t off[4];
    wr32(off, diroff);
    fwrite(off, 1, 4, f);
    fclose(f);
    return true;
}

int Wad::find(const std::string& name) const
{
    const std::string want = wad_name(name);
    if (want.empty()) return -1;
    for (size_t i = 0; i < lumps_.size(); i++)
        if (wad_name(lumps_[i].name) == want)
            return (int)i;
    return -1;
}

const std::vector<uint8_t>* Wad::data(const std::string& name) const
{
    const int i = find(name);
    return i < 0 ? nullptr : &lumps_[(size_t)i].data;
}

int Wad::append(const std::string& name, const std::vector<uint8_t>& data)
{
    if (wad_name(name).empty()) return -1;
    Lump l;
    l.name = wad_name(name);
    l.data = data;
    lumps_.push_back(std::move(l));
    return (int)lumps_.size() - 1;
}

int Wad::append(const std::string& name, const uint8_t* data, size_t n)
{
    return append(name, std::vector<uint8_t>(data, data + n));
}

int Wad::insert(int at, const std::string& name, const std::vector<uint8_t>& data)
{
    if (wad_name(name).empty()) return -1;
    if (at < 0) at = 0;
    if ((size_t)at >= lumps_.size()) return append(name, data);
    Lump l;
    l.name = wad_name(name);
    l.data = data;
    lumps_.insert(lumps_.begin() + at, std::move(l));
    return at;
}

bool Wad::replace(int idx, const std::vector<uint8_t>& data)
{
    if (idx < 0 || (size_t)idx >= lumps_.size()) return false;
    lumps_[(size_t)idx].data = data;
    return true;
}

bool Wad::erase(int idx)
{
    if (idx < 0 || (size_t)idx >= lumps_.size()) return false;
    lumps_.erase(lumps_.begin() + idx);
    return true;
}

bool Wad::rename(int idx, const std::string& name)
{
    if (idx < 0 || (size_t)idx >= lumps_.size()) return false;
    const std::string n = wad_name(name);
    if (n.empty()) return false;
    lumps_[(size_t)idx].name = n;
    return true;
}

int Wad::sprite_end() const
{
    int start = -1;
    for (size_t i = 0; i < lumps_.size(); i++) {
        const std::string n = wad_name(lumps_[i].name);
        if (n == "S_END" || n == "SS_END")     return (int)i;
        if (n == "S_START" || n == "SS_START") start = (int)i;
    }
    return start >= 0 ? start + 1 : -1;
}

}  // namespace buddy
