// Spore resource packages (DBPF 3.0, the format of EditorSaves.package and the game packages):
// read the index and records (uncompressed or RefPack), write a small uncompressed package.
// Plain C++ (no game code) so the host build can unit test it (tests/net_tests.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dbpf {

struct Key {
    uint32_t instance = 0, type = 0, group = 0;
    bool operator==(const Key& o) const { return instance == o.instance && type == o.type && group == o.group; }
};

struct Entry {
    Key key;
    uint64_t offset = 0; // 64-bit in DBBF packages
    uint32_t size = 0, usize = 0;
    bool refpack = false;
};

struct Record {
    Key key;
    std::vector<uint8_t> data; // uncompressed
};

// BUG-0208: Package::load checks offset and size without an overflow (false: the old sum, tests)
extern bool g_bounds_fix;
// BUG-0047: a 4-byte RefPack size needs 6 header bytes (false: the old 5-byte check, net_tests --old-refpack)
extern bool g_refpack_fix;

class Package {
public:
    bool load(const std::vector<uint8_t>& file); // whole file in memory
    const std::vector<Entry>& entries() const { return entries_; }
    bool read(const Entry& e, std::vector<uint8_t>& out) const;
    const Entry* find(const Key& k) const;

private:
    std::vector<uint8_t> file_;
    std::vector<Entry> entries_;
};

// reading a package piece by piece (large files such as Pollination.package): the header (its first
// 96 bytes) gives the index, the index the entries, an entry's raw bytes its data
struct IndexInfo {
    uint64_t offset = 0;
    uint32_t size = 0, count = 0;
    bool big = false; // DBBF: 64-bit offsets
};
bool index_info(const uint8_t* header, size_t n, IndexInfo& out); // DBPF 3.0 or DBBF
bool parse_index(const uint8_t* index, size_t n, const IndexInfo& info, std::vector<Entry>& out);
bool decode(const Entry& e, const uint8_t* raw, std::vector<uint8_t>& out); // raw: e.size bytes

bool refpack_decode(const uint8_t* src, size_t n, std::vector<uint8_t>& out);
std::vector<uint8_t> write(const std::vector<Record>& records);

// Keys of one type and group inside the records of a DBPF 3.0 package, big-endian (group, type,
// instance) or little-endian (type, instance, group): a planet save names its species both ways
// (herds and creatures big-endian, the pack list little-endian; docs/re/creature.md 15). Appends the
// instances not in the list yet.
void find_keys(const std::vector<uint8_t>& file, uint32_t type, uint32_t group, std::vector<uint32_t>& instances);

// pollen_metadata (0x030BDEE3): author and creation name (UTF-8), false if not understood
bool metadata_names(const std::vector<uint8_t>& meta, std::string& author, std::string& name);

} // namespace dbpf
