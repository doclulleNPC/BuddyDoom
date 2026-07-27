// buddydef_wad.hpp -- tiny WAD reader/writer for the MyBuddy editor.
//
// Just enough of the Doom WAD format to open a WAD, read its directory, edit lumps
// (replace / append / insert / delete / rename) and write it back out.  Format
// reference: files/w_wad.c, files/w_wad.h.  No PK3s -- MyBuddy is a desktop tool and
// the user points it at a real .wad.
//
// Directory ORDER is meaningful (sprites only count as sprites between the
// S_START/S_END markers), so lumps live in a vector and are moved, never sorted.
//
// All multibyte header fields are little-endian on disk and we read them byte-wise,
// so the loader does not care what the host's endianness is.

#ifndef BUDDYDEF_WAD_HPP
#define BUDDYDEF_WAD_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace buddy {

// One lump: its (<=8 char, upper-case) directory name and its bytes.  A marker lump
// such as S_START is a real entry with an empty payload.
struct Lump {
    std::string           name;
    std::vector<uint8_t>  data;

    unsigned size() const { return static_cast<unsigned>(data.size()); }
};

class Wad {
public:
    // Read a WAD from disk.  Returns false and leaves the object empty on failure.
    bool load(const std::string& path);

    // Write every lump back out.  The file's own "IWAD"/"PWAD" tag is preserved (a
    // new WAD starts as PWAD).  Returns false if the file could not be written.
    bool save(const std::string& path) const;

    const std::string& id() const { return id_; }

    bool  empty()  const { return lumps_.empty(); }
    size_t size()  const { return lumps_.size(); }

    const std::vector<Lump>& lumps() const { return lumps_; }
    const Lump& operator[](size_t i) const { return lumps_[i]; }

    // Index of the first lump named `name` (case-insensitive, 8-byte semantics), or -1.
    int find(const std::string& name) const;

    // The bytes of a lump by name, or nullptr when it does not exist.
    const std::vector<uint8_t>* data(const std::string& name) const;

    int  append(const std::string& name, const std::vector<uint8_t>& data);
    int  append(const std::string& name, const uint8_t* data, size_t n);

    // Insert before index `at` (>= size appends).  Returns the new lump's index.
    int  insert(int at, const std::string& name, const std::vector<uint8_t>& data);

    bool replace(int idx, const std::vector<uint8_t>& data);
    bool erase(int idx);
    bool rename(int idx, const std::string& name);

    // Where a new sprite lump belongs: the index of S_END / SS_END, or just past a
    // lone S_START.  -1 when the WAD has no sprite namespace at all.
    int  sprite_end() const;

private:
    std::vector<Lump> lumps_;
    std::string       id_ = "PWAD";
};

// Normalise a name the way a WAD directory stores it: upper-case, <= 8 chars, cut at
// the first space.  Exposed because the editor shows the normalised name back.
std::string wad_name(const std::string& raw);

}  // namespace buddy

#endif
