// Pure logic, index format, and wire codecs for the persistent shared creations library (BUG-0873, creations 1.3).
// Portable (no Windows, no SporeKit): tests/library_tests.cpp and tests/fuzz_decoders.cpp run it natively.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "net/buffer.h"

namespace library {

// creations 1.3 wire kinds over MSG_CREATION (creations.h reserves 10-12 for 0.7.17; 13-14 are WP-Q2)
constexpr uint8_t kLibMinor = 3;
constexpr uint8_t kKindLibList = 13;
constexpr uint8_t kKindLibPng = 14;

constexpr uint8_t kListHave = 0;
constexpr uint8_t kListWant = 1;

// Sporepedia resource IDs for the "Shared Creations" tab (zero collisions with Spore_Game / EP1 / Text)
constexpr uint32_t kLibLocaleGroup = 0x02FABF01u;
constexpr uint32_t kLibLocaleType = 0x02FAC0B6u;
constexpr uint32_t kLibLocaleId = 0xA7C9F2B2u;   // fnv("sporecoop_shared_library_locale")
constexpr uint32_t kLibCatGroup = 0x06B38241u;
constexpr uint32_t kLibCatId = 0x11C77D4Cu;      // fnv("sporecoop_shared_library_category")
constexpr uint32_t kLibItemGroup = 0x4E5892EBu;
constexpr uint32_t kLibItemId = 0x0BA43291u;     // fnv("sporecoop_shared_library_item")
constexpr uint32_t kMyCreationsItemId = 0x146C1D8Cu;

constexpr size_t kMaxPngBytes = 512u * 1024u;
constexpr size_t kMaxEntries = 512u;
constexpr size_t kMaxListItems = 256u;
constexpr int kMaxRetriesPerRun = 3;
constexpr double kRetryIntervalSec = 60.0;
constexpr double kSendPaceSec = 2.0;

// Entry flags
constexpr uint8_t kFlagImported = 1u << 0;
constexpr uint8_t kFlagOriginOwn = 1u << 1; // sender's own creation from EditorSaves.package
constexpr uint8_t kFlagForeign = 1u << 2;   // received from a partner who got it from someone else
constexpr uint8_t kFlagNoShare = 1u << 3;   // never offer back out to other peers

struct AssetKey {
    uint32_t instance = 0;
    uint32_t type = 0;
    uint32_t group = 0;
    bool valid() const { return instance != 0 && instance != 0xFFFFFFFFu && type != 0 && group != 0; }
    bool operator==(const AssetKey& o) const { return instance == o.instance && type == o.type && group == o.group; }
    bool operator<(const AssetKey& o) const
    {
        if (group != o.group)
            return group < o.group;
        if (type != o.type)
            return type < o.type;
        return instance < o.instance;
    }
};

// Model resource groups whose PNG cards are steganographic Spore creations
inline bool is_creation_group(uint32_t g)
{
    return g == 0x40616200u || g == 0x40626200u || g == 0x40636200u || g == 0x40646200u || g == 0x40656200u ||
           g == 0x40666200u;
}

inline bool is_valid_png(const uint8_t* data, size_t len)
{
    static constexpr uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data || len < 33u || len > kMaxPngBytes)
        return false;
    if (std::memcmp(data, kSig, 8) != 0)
        return false;
    // First chunk must be IHDR (length 13 at bytes 8..11, tag "IHDR" at bytes 12..15)
    return data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R';
}

inline uint64_t content_hash64(const uint8_t* data, size_t len)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x100000001B3ull;
    }
    return h ? h : 1ull;
}

inline std::string png_filename(uint64_t hash)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx.png", (unsigned long long)hash);
    return buf;
}

inline bool parse_png_filename(const std::string& name, uint64_t& out_hash)
{
    if (name.size() != 20 || name.compare(16, 4, ".png") != 0)
        return false;
    uint64_t h = 0;
    for (size_t i = 0; i < 16; ++i) {
        char c = name[i];
        uint64_t v = 0;
        if (c >= '0' && c <= '9')
            v = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = (uint64_t)(c - 'A' + 10);
        else
            return false;
        h = (h << 4) | v;
    }
    if (h == 0)
        return false;
    out_hash = h;
    return true;
}

inline std::string trim_label(const std::string& s, size_t max_len = 64)
{
    std::string out;
    out.reserve(std::min(s.size(), max_len));
    for (unsigned char c : s) {
        if (out.size() >= max_len)
            break;
        if (c >= 0x20 && c != 0x7F)
            out.push_back((char)c);
    }
    return out;
}

// ---- Persistent index.bin codec ----

struct IndexEntry {
    uint64_t hash = 0;
    AssetKey imported{};
    uint32_t source_group = 0;
    uint8_t flags = 0;
    std::string sender;
    std::string name;
};

constexpr uint8_t kIndexVersion = 1;

inline std::vector<uint8_t> encode_index(const std::vector<IndexEntry>& entries)
{
    net::Writer w;
    w.u8('S');
    w.u8('C');
    w.u8('L');
    w.u8('B');
    w.u8(kIndexVersion);
    uint32_t n = (uint32_t)std::min(entries.size(), kMaxEntries);
    w.u32(n);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& e = entries[i];
        w.u32((uint32_t)(e.hash & 0xFFFFFFFFu));
        w.u32((uint32_t)(e.hash >> 32));
        w.u32(e.imported.instance);
        w.u32(e.imported.type);
        w.u32(e.imported.group);
        w.u32(e.source_group);
        w.u8(e.flags);
        w.str8(trim_label(e.sender));
        w.str8(trim_label(e.name));
    }
    return w.data;
}

inline bool decode_index(const uint8_t* data, size_t len, std::vector<IndexEntry>& out)
{
    out.clear();
    if (!data || len < 9)
        return false;
    net::Reader r{data, len};
    if (r.u8() != 'S' || r.u8() != 'C' || r.u8() != 'L' || r.u8() != 'B')
        return false;
    uint8_t ver = r.u8();
    if (ver != kIndexVersion)
        return false;
    uint32_t n = r.u32();
    if (!r.ok() || n > kMaxEntries)
        return false;
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        IndexEntry e;
        uint32_t lo = r.u32();
        uint32_t hi = r.u32();
        e.hash = (uint64_t)lo | ((uint64_t)hi << 32);
        e.imported.instance = r.u32();
        e.imported.type = r.u32();
        e.imported.group = r.u32();
        e.source_group = r.u32();
        e.flags = r.u8();
        e.sender = trim_label(r.str8());
        e.name = trim_label(r.str8());
        if (!r.ok())
            return false;
        if (e.hash == 0)
            continue;
        out.push_back(std::move(e));
    }
    return true;
}

// ---- Wire codecs (MSG_CREATION kinds 13 & 14, creations 1.3) ----

struct HaveItem {
    uint64_t hash = 0;
    uint32_t group = 0;
    uint8_t flags = 0; // bit 0: origin_own
};

struct ListMsg {
    uint8_t subkind = kListHave;
    std::vector<HaveItem> have;
    std::vector<uint64_t> want;
};

inline std::vector<uint8_t> encode_list(const ListMsg& m)
{
    net::Writer w;
    w.u8(m.subkind);
    if (m.subkind == kListHave) {
        uint16_t n = (uint16_t)std::min(m.have.size(), kMaxListItems);
        w.u16(n);
        for (uint16_t i = 0; i < n; ++i) {
            w.u32((uint32_t)(m.have[i].hash & 0xFFFFFFFFu));
            w.u32((uint32_t)(m.have[i].hash >> 32));
            w.u32(m.have[i].group);
            w.u8(m.have[i].flags);
        }
    } else {
        uint16_t n = (uint16_t)std::min(m.want.size(), kMaxListItems);
        w.u16(n);
        for (uint16_t i = 0; i < n; ++i) {
            w.u32((uint32_t)(m.want[i] & 0xFFFFFFFFu));
            w.u32((uint32_t)(m.want[i] >> 32));
        }
    }
    return w.data;
}

inline bool decode_list(const uint8_t* data, size_t len, ListMsg& out)
{
    out = ListMsg{};
    if (!data || len < 3)
        return false;
    net::Reader r{data, len};
    out.subkind = r.u8();
    uint16_t n = r.u16();
    if (!r.ok() || n > kMaxListItems)
        return false;
    if (out.subkind == kListHave) {
        out.have.reserve(n);
        for (uint16_t i = 0; i < n; ++i) {
            HaveItem it;
            uint32_t lo = r.u32();
            uint32_t hi = r.u32();
            it.hash = (uint64_t)lo | ((uint64_t)hi << 32);
            it.group = r.u32();
            it.flags = r.u8();
            if (!r.ok())
                return false;
            if (it.hash != 0 && is_creation_group(it.group))
                out.have.push_back(it);
        }
        return true;
    }
    if (out.subkind == kListWant) {
        out.want.reserve(n);
        for (uint16_t i = 0; i < n; ++i) {
            uint32_t lo = r.u32();
            uint32_t hi = r.u32();
            uint64_t h = (uint64_t)lo | ((uint64_t)hi << 32);
            if (!r.ok())
                return false;
            if (h != 0)
                out.want.push_back(h);
        }
        return true;
    }
    return false;
}

struct PngMsg {
    uint64_t hash = 0;
    uint32_t group = 0;
    uint8_t flags = 0; // bit 0: origin_own
    std::string sender;
    std::string name;
    std::vector<uint8_t> png;
};

inline std::vector<uint8_t> encode_png_msg(const PngMsg& m)
{
    net::Writer w;
    w.u32((uint32_t)(m.hash & 0xFFFFFFFFu));
    w.u32((uint32_t)(m.hash >> 32));
    w.u32(m.group);
    w.u8(m.flags);
    w.str8(trim_label(m.sender));
    w.str8(trim_label(m.name));
    uint32_t n = (uint32_t)std::min(m.png.size(), kMaxPngBytes);
    w.u32(n);
    if (n)
        w.raw(m.png.data(), n);
    return w.data;
}

inline bool decode_png_msg(const uint8_t* data, size_t len, PngMsg& out)
{
    out = PngMsg{};
    if (!data || len < 19)
        return false;
    net::Reader r{data, len};
    uint32_t lo = r.u32();
    uint32_t hi = r.u32();
    out.hash = (uint64_t)lo | ((uint64_t)hi << 32);
    out.group = r.u32();
    out.flags = r.u8();
    out.sender = trim_label(r.str8());
    out.name = trim_label(r.str8());
    uint32_t plen = r.u32();
    if (!r.ok() || plen == 0 || plen > kMaxPngBytes || r.left() < plen)
        return false;
    if (!is_creation_group(out.group))
        return false;
    if (!is_valid_png(r.cur(), plen))
        return false;
    if (content_hash64(r.cur(), plen) != out.hash)
        return false;
    out.png.assign(r.cur(), r.cur() + plen);
    return true;
}

// ---- Pure library state & policy ----

struct Entry {
    uint64_t hash = 0;
    AssetKey imported{};
    uint32_t source_group = 0;
    uint8_t flags = 0;
    std::string sender;
    std::string name;
    // Runtime retry tracking (reset per process start)
    int attempts = 0;
    double next_retry = 0.0;
    bool gave_up = false;

    bool is_imported() const { return (flags & kFlagImported) != 0 && imported.valid(); }
    bool is_foreign() const { return (flags & kFlagForeign) != 0; }
};

class LibraryState {
  public:
    std::set<uint64_t> own_hashes;
    std::set<AssetKey> own_keys;
    std::map<uint64_t, Entry> entries;
    std::set<AssetKey> imported_keys;

    void set_own(std::set<uint64_t> hashes, std::set<AssetKey> keys)
    {
        own_hashes = std::move(hashes);
        own_keys = std::move(keys);
    }

    void load_index(const std::vector<IndexEntry>& idx)
    {
        entries.clear();
        imported_keys.clear();
        for (const auto& ie : idx) {
            if (ie.hash == 0 || entries.size() >= kMaxEntries)
                continue;
            Entry e;
            e.hash = ie.hash;
            e.imported = ie.imported;
            e.source_group = ie.source_group;
            e.flags = ie.flags;
            e.sender = ie.sender;
            e.name = ie.name;
            if (e.is_imported())
                imported_keys.insert(e.imported);
            entries[e.hash] = std::move(e);
        }
    }

    std::vector<IndexEntry> save_index() const
    {
        std::vector<IndexEntry> out;
        out.reserve(entries.size());
        for (const auto& [h, e] : entries) {
            IndexEntry ie;
            ie.hash = e.hash;
            ie.imported = e.imported;
            ie.source_group = e.source_group;
            ie.flags = e.flags;
            ie.sender = e.sender;
            ie.name = e.name;
            out.push_back(std::move(ie));
        }
        return out;
    }

    // Receiver policy: decide whether to request/accept an offered creation.
    bool should_want(uint64_t hash, bool origin_own, bool allow_foreign) const
    {
        if (hash == 0)
            return false;
        if (own_hashes.count(hash) || entries.count(hash))
            return false;
        if (!origin_own && !allow_foreign)
            return false;
        return entries.size() < kMaxEntries;
    }

    // Add a received creation PNG (deduped by hash). Returns true if newly inserted.
    bool ingest(uint64_t hash, uint32_t group, bool origin_own, const std::string& sender, const std::string& name,
                bool skip_if_own = true)
    {
        if (hash == 0)
            return false;
        if (skip_if_own && own_hashes.count(hash))
            return false;
        auto it = entries.find(hash);
        if (it != entries.end()) {
            if (it->second.sender.empty() && !sender.empty())
                it->second.sender = trim_label(sender);
            if (it->second.name.empty() && !name.empty())
                it->second.name = trim_label(name);
            return false;
        }
        if (entries.size() >= kMaxEntries)
            return false;
        Entry e;
        e.hash = hash;
        e.source_group = group;
        e.flags = origin_own ? kFlagOriginOwn : kFlagForeign;
        e.sender = trim_label(sender);
        e.name = trim_label(name);
        entries[hash] = std::move(e);
        return true;
    }

    bool mark_imported(uint64_t hash, AssetKey k)
    {
        auto it = entries.find(hash);
        if (it == entries.end() || !k.valid())
            return false;
        if (it->second.imported.valid() && !(it->second.imported == k))
            imported_keys.erase(it->second.imported);
        it->second.imported = k;
        it->second.flags |= kFlagImported;
        it->second.attempts = 0;
        it->second.next_retry = 0.0;
        it->second.gave_up = false;
        imported_keys.insert(k);
        return true;
    }

    // Record a failed ImportPNG attempt. With retry_enabled: retry 60 s later up to 3 times per run.
    void record_import_failure(uint64_t hash, double now, bool retry_enabled)
    {
        auto it = entries.find(hash);
        if (it == entries.end())
            return;
        it->second.attempts++;
        if (!retry_enabled || it->second.attempts >= kMaxRetriesPerRun) {
            it->second.gave_up = true;
            it->second.next_retry = 0.0;
        } else {
            it->second.next_retry = now + kRetryIntervalSec;
        }
    }

    // Re-queue entries whose imported key is no longer present in Pollination/RM.
    void mark_needs_import(uint64_t hash)
    {
        auto it = entries.find(hash);
        if (it == entries.end())
            return;
        if (it->second.imported.valid())
            imported_keys.erase(it->second.imported);
        it->second.flags &= (uint8_t)~kFlagImported;
        it->second.imported = {};
        it->second.attempts = 0;
        it->second.next_retry = 0.0;
        it->second.gave_up = false;
    }

    std::vector<uint64_t> due_imports(double now) const
    {
        std::vector<uint64_t> out;
        for (const auto& [h, e] : entries) {
            if (!e.is_imported() && !e.gave_up && now >= e.next_retry)
                out.push_back(h);
        }
        return out;
    }

    // Force immediate retry of failed-but-not-exhausted entries (testing / user trigger).
    void reset_retry_timers()
    {
        for (auto& [h, e] : entries) {
            if (!e.is_imported() && !e.gave_up)
                e.next_retry = 0.0;
        }
    }

    // My Creations filter: hide imported partner creations, never hide local player's own EditorSaves creations.
    bool should_hide_from_my_creations(AssetKey k) const
    {
        return k.valid() && imported_keys.count(k) != 0 && own_keys.count(k) == 0;
    }

    std::vector<AssetKey> feed_keys() const
    {
        std::vector<AssetKey> out;
        out.reserve(imported_keys.size());
        for (const auto& k : imported_keys) {
            if (own_keys.count(k) == 0)
                out.push_back(k);
        }
        return out;
    }
};

} // namespace library
