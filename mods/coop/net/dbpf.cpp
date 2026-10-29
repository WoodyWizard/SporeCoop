#include "dbpf.h"

#include <cstring>

#include "../fix_switch.h"

namespace dbpf {

// unit-test only switches (BUG-0214 R6: the off side reads out of bounds; net_tests --old-dbbf / --old-refpack)
FIX_SWITCH(g_bounds_fix);  // net.dbbf_bounds, BUG-0208
FIX_SWITCH(g_refpack_fix); // net.refpack_bound, BUG-0047

static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
{
    v[at] = x & 0xFF;
    v[at + 1] = x >> 8 & 0xFF;
    v[at + 2] = x >> 16 & 0xFF;
    v[at + 3] = x >> 24;
}

static uint64_t le64(const uint8_t* p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

bool index_info(const uint8_t* f, size_t n, IndexInfo& out)
{
    if (n < 96)
        return false;
    out.count = le32(f + 0x24);
    out.size = le32(f + 0x2C);
    if (memcmp(f, "DBPF", 4) == 0) {
        out.big = false;
        out.offset = le32(f + 0x40);
        if (!out.offset)
            out.offset = le32(f + 0x28);
    } else if (memcmp(f, "DBBF", 4) == 0) { // 64-bit offsets (Pollination.package, docs/re/creations.md 1.2)
        out.big = true;
        out.size = (uint32_t)le64(f + 0x28); // u64 index size at 0x28, u64 index offset at 0x38
        out.offset = le64(f + 0x38);
    } else
        return false;
    return out.size >= 4;
}

bool parse_index(const uint8_t* p, size_t n, const IndexInfo& info, std::vector<Entry>& out)
{
    const uint8_t* end = p + n;
    if (n < 4)
        return false;
    uint32_t flags = le32(p);
    p += 4;
    uint32_t ft = 0, fg = 0;
    if (flags & 1) { if (p + 4 > end) return false; ft = le32(p); p += 4; }
    if (flags & 2) { if (p + 4 > end) return false; fg = le32(p); p += 4; }
    if (flags & 4) p += 4;
    size_t off_size = info.big ? 8 : 4;
    for (uint32_t i = 0; i < info.count; i++) {
        size_t need = (flags & 1 ? 0 : 4) + (flags & 2 ? 0 : 4) + 4 + off_size + 12;
        if (p + need > end)
            return false;
        Entry e;
        if (flags & 1) e.key.type = ft; else { e.key.type = le32(p); p += 4; }
        if (flags & 2) e.key.group = fg; else { e.key.group = le32(p); p += 4; }
        e.key.instance = le32(p); p += 4;
        e.offset = info.big ? le64(p) : le32(p);
        p += off_size;
        e.size = le32(p) & 0x7FFFFFFF;
        e.usize = le32(p + 4);
        e.refpack = (p[8] | p[9] << 8) == 0xFFFF;
        p += 12;
        out.push_back(e);
    }
    return true;
}

bool decode(const Entry& e, const uint8_t* raw, std::vector<uint8_t>& out)
{
    if (!e.refpack) {
        out.assign(raw, raw + e.size);
        return true;
    }
    return refpack_decode(raw, e.size, out) && (e.usize == 0 || out.size() == e.usize);
}

bool Package::load(const std::vector<uint8_t>& file)
{
    file_ = file;
    entries_.clear();
    size_t n = file_.size();
    IndexInfo info;
    // BUG-0208: a DBBF offset is 64-bit: offset + size wrapped past 2^64 and passed the check
    if (!index_info(file_.data(), n, info) || (g_bounds_fix ? info.offset > n || info.size > n - info.offset
                                                            : info.offset + info.size > n))
        return false;
    std::vector<Entry> all;
    if (!parse_index(file_.data() + info.offset, info.size, info, all))
        return false;
    for (auto& e : all)
        if (g_bounds_fix ? e.offset <= n && e.size <= n - e.offset : e.offset + e.size <= n)
            entries_.push_back(e);
    return true;
}

const Entry* Package::find(const Key& k) const
{
    for (auto& e : entries_)
        if (e.key == k)
            return &e;
    return nullptr;
}

bool Package::read(const Entry& e, std::vector<uint8_t>& out) const { return decode(e, file_.data() + e.offset, out); }

// EA RefPack (QFS): 2 flag bytes, 3 or 4 byte big-endian size, then control codes
bool refpack_decode(const uint8_t* src, size_t n, std::vector<uint8_t>& out)
{
    out.clear();
    if (n < 5)
        return false;
    size_t o = 2;
    uint32_t size;
    if (src[0] & 0x80) {
        if (n < 6 && g_refpack_fix) // BUG-0047: the 4-byte size needs 6 header bytes (be32 read one byte past a 5-byte record)
            return false;
        size = be32(src + 2);
        o = 6;
    } else {
        size = (uint32_t)src[2] << 16 | src[3] << 8 | src[4];
        o = 5;
    }
    if (size > (64u << 20))
        return false;
    out.reserve(size);
    // untrusted input (creations come over the network): never grow past the declared size
    auto lit = [&](size_t k) {
        if (o + k > n || out.size() + k > size)
            return false;
        out.insert(out.end(), src + o, src + o + k);
        o += k;
        return true;
    };
    auto copy = [&](size_t off, size_t len) {
        if (off == 0 || off > out.size() || out.size() + len > size)
            return false;
        size_t from = out.size() - off;
        for (size_t i = 0; i < len; i++)
            out.push_back(out[from + i]);
        return true;
    };
    while (o < n) {
        uint8_t c0 = src[o];
        if (c0 < 0x80) {
            if (o + 2 > n) return false;
            uint8_t c1 = src[o + 1];
            o += 2;
            if (!lit(c0 & 3) || !copy(((c0 & 0x60) << 3) + c1 + 1, ((c0 & 0x1C) >> 2) + 3)) return false;
        } else if (c0 < 0xC0) {
            if (o + 3 > n) return false;
            uint8_t c1 = src[o + 1], c2 = src[o + 2];
            o += 3;
            if (!lit((c1 & 0xC0) >> 6) || !copy(((c1 & 0x3F) << 8) + c2 + 1, (c0 & 0x3F) + 4)) return false;
        } else if (c0 < 0xE0) {
            if (o + 4 > n) return false;
            uint8_t c1 = src[o + 1], c2 = src[o + 2], c3 = src[o + 3];
            o += 4;
            if (!lit(c0 & 3) || !copy(((c0 & 0x10) << 12) + (c1 << 8) + c2 + 1, ((c0 & 0x0C) << 6) + c3 + 5))
                return false;
        } else if (c0 < 0xFC) {
            o++;
            if (!lit(((c0 & 0x1F) << 2) + 4)) return false;
        } else {
            o++;
            if (!lit(c0 & 3)) return false;
            break;
        }
    }
    return out.size() == size;
}

std::vector<uint8_t> write(const std::vector<Record>& records)
{
    std::vector<uint8_t> f(96, 0);
    memcpy(f.data(), "DBPF", 4);
    put32(f, 4, 3);
    std::vector<uint32_t> offs;
    for (auto& r : records) {
        offs.push_back((uint32_t)f.size());
        f.insert(f.end(), r.data.begin(), r.data.end());
    }
    size_t ioff = f.size();
    std::vector<uint8_t> idx(8 + records.size() * 28, 0);
    put32(idx, 0, 4); // flags: constant "unknown" field (0)
    for (size_t i = 0; i < records.size(); i++) {
        size_t at = 8 + i * 28;
        const Record& r = records[i];
        put32(idx, at, r.key.type);
        put32(idx, at + 4, r.key.group);
        put32(idx, at + 8, r.key.instance);
        put32(idx, at + 12, offs[i]);
        put32(idx, at + 16, (uint32_t)r.data.size() | 0x80000000u);
        put32(idx, at + 20, (uint32_t)r.data.size());
        idx[at + 24] = 0; idx[at + 25] = 0; // not compressed
        idx[at + 26] = 1; idx[at + 27] = 0;
    }
    f.insert(f.end(), idx.begin(), idx.end());
    put32(f, 0x24, (uint32_t)records.size());
    put32(f, 0x2C, (uint32_t)idx.size());
    put32(f, 0x3C, 3);
    put32(f, 0x40, (uint32_t)ioff);
    return f;
}

// every record of a DBPF 3.0 file with the file offset of its index entry
struct Located {
    Entry e;
    size_t at; // index entry: offset field
};
static bool locate(const std::vector<uint8_t>& f, std::vector<Located>& out)
{
    IndexInfo info;
    if (f.size() < 96 || memcmp(f.data(), "DBPF", 4) != 0 || !index_info(f.data(), f.size(), info) ||
        info.offset + info.size > f.size())
        return false;
    const uint8_t* base = f.data() + info.offset;
    std::vector<Entry> all;
    if (!parse_index(base, info.size, info, all))
        return false;
    uint32_t flags = le32(base);
    size_t p = 4 + (flags & 1 ? 4 : 0) + (flags & 2 ? 4 : 0) + (flags & 4 ? 4 : 0);
    size_t per = (flags & 1 ? 0 : 4) + (flags & 2 ? 0 : 4) + 4 + 4 + 12;
    for (size_t i = 0; i < all.size(); i++) {
        size_t at = info.offset + p + i * per + per - 16; // the offset field: offset, size, usize, flags
        if (all[i].offset + all[i].size <= f.size())
            out.push_back({all[i], at});
    }
    return true;
}

// a key of `type` and `group` at d[i]: big-endian (group, type, instance) or little-endian (type,
// instance, group); its instance's 4 bytes are at d[i + inst_at]
struct KeyAt {
    size_t pos;
    int inst_at; // 8 (big-endian) or 4 (little-endian)
};
static void key_hits(const std::vector<uint8_t>& d, uint32_t type, uint32_t group, std::vector<KeyAt>& out)
{
    uint8_t be[8], lt[4], lg[4];
    for (int b = 0; b < 4; b++) {
        be[b] = (uint8_t)(group >> (24 - 8 * b));
        be[4 + b] = (uint8_t)(type >> (24 - 8 * b));
        lt[b] = (uint8_t)(type >> (8 * b));
        lg[b] = (uint8_t)(group >> (8 * b));
    }
    for (size_t i = 0; i + 12 <= d.size(); i++) {
        if (d[i] == be[0] && memcmp(d.data() + i, be, 8) == 0) {
            out.push_back({i, 8});
            i += 11;
        } else if (d[i] == lt[0] && memcmp(d.data() + i, lt, 4) == 0 && memcmp(d.data() + i + 8, lg, 4) == 0) {
            out.push_back({i, 4});
            i += 11;
        }
    }
}

static uint32_t inst_of(const uint8_t* p, int inst_at) { return inst_at == 8 ? be32(p + 8) : le32(p + 4); }

void find_keys(const std::vector<uint8_t>& file, uint32_t type, uint32_t group, std::vector<uint32_t>& instances)
{
    std::vector<Located> recs;
    if (!locate(file, recs))
        return;
    std::vector<uint8_t> d;
    std::vector<KeyAt> hits;
    for (auto& r : recs) {
        if (!decode(r.e, file.data() + r.e.offset, d))
            continue;
        hits.clear();
        key_hits(d, type, group, hits);
        for (auto& h : hits) {
            uint32_t inst = inst_of(d.data() + h.pos, h.inst_at);
            bool have = false;
            for (uint32_t x : instances)
                have |= x == inst;
            if (!have)
                instances.push_back(inst);
        }
    }
}

static std::string utf16le_to_utf8(const uint8_t* p, size_t chars)
{
    std::string s;
    for (size_t i = 0; i < chars; i++) {
        uint32_t c = p[2 * i] | p[2 * i + 1] << 8;
        if (c < 0x80)
            s += (char)c;
        else if (c < 0x800) {
            s += (char)(0xC0 | c >> 6);
            s += (char)(0x80 | (c & 0x3F));
        } else {
            s += (char)(0xE0 | c >> 12);
            s += (char)(0x80 | (c >> 6 & 0x3F));
            s += (char)(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// version (BE u32), ..., FF FF FF FF FF FF FF FE, then author and name: BE u32 length + UTF-16LE
bool metadata_names(const std::vector<uint8_t>& m, std::string& author, std::string& name)
{
    static const uint8_t mark[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE};
    for (size_t i = 0; i + 8 <= m.size(); i++) {
        if (memcmp(m.data() + i, mark, 8) != 0)
            continue;
        size_t o = i + 8;
        std::string out[2];
        for (auto& s : out) {
            if (o + 4 > m.size())
                return false;
            uint32_t len = be32(m.data() + o);
            o += 4;
            if (len > 256 || o + len * 2 > m.size())
                return false;
            s = utf16le_to_utf8(m.data() + o, len);
            o += len * 2;
        }
        author = out[0];
        name = out[1];
        return true;
    }
    return false;
}

} // namespace dbpf
