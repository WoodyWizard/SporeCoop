#include "library_sync.h"
#include "../../../updater/sha256.h"
#include <algorithm>
#include <cstring>

namespace library_sync {
bool valid_hash(const std::string &h) {
    return h.size() == 64 &&
           std::all_of(h.begin(), h.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
static uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
static uint32_t png_crc(const uint8_t *data, size_t n) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
bool valid_png(const std::vector<uint8_t> &b) {
    const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (b.size() < 45 || b.size() > kMaxFile || memcmp(b.data(), signature, 8))
        return false;
    bool header = false, pixels = false;
    for (size_t pos = 8; pos + 12 <= b.size();) {
        uint32_t n = be32(b.data() + pos);
        if (n > b.size() - pos - 12)
            return false;
        const uint8_t *type = b.data() + pos + 4;
        if (png_crc(type, size_t(n) + 4) != be32(type + 4 + n))
            return false;
        if (!header) {
            if (memcmp(type, "IHDR", 4) || n != 13)
                return false;
            uint32_t w = be32(type + 4), h = be32(type + 8);
            if (!w || !h || w > 1024 || h > 1024 || type[12] != 8 || (type[13] != 2 && type[13] != 6) || type[14] ||
                type[15] || type[16] > 1)
                return false;
            header = true;
        } else if (!memcmp(type, "IHDR", 4))
            return false;
        pixels |= !memcmp(type, "IDAT", 4);
        if (!memcmp(type, "IEND", 4))
            return pixels && n == 0 && pos + 12 == b.size();
        pos += size_t(n) + 12;
    }
    return false;
}
static bool text_ok(const std::string &s) {
    return s.size() <= 255 && utf8::valid(s) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void write_item(net::Writer &w, const Item &i) {
    w.str8(i.hash);
    w.u32(i.size);
    w.str8(i.author);
    w.str8(i.name);
    w.str8(i.source);
    w.str8(i.sharer);
}
bool read_item(net::Reader &r, Item &i) {
    i.hash = r.str8();
    i.size = r.u32();
    i.author = r.str8();
    i.name = r.str8();
    i.source = r.str8();
    i.sharer = r.str8();
    return r.ok() && valid_hash(i.hash) && i.size >= 45 && i.size <= kMaxFile && text_ok(i.author) && text_ok(i.name) &&
           text_ok(i.source) && text_ok(i.sharer);
}
void Exchange::join(uint8_t id) { peers.try_emplace(id); }
void Exchange::leave(uint8_t id) { peers.erase(id); }
void Exchange::reset() { peers.clear(); }
size_t Exchange::incoming_bytes() const {
    size_t total = 0;
    for (auto &[id, p] : peers)
        total += p.receiving.size();
    return total;
}
void Exchange::receive(uint8_t id, net::Reader &r) {
    auto it = peers.find(id);
    if (it == peers.end())
        return;
    auto &p = it->second;
    auto kind = r.u8();
    if (kind == 0) {
        Item i;
        if (!read_item(r, i) || r.left() || !p.in.hash.empty())
            return;
        bool known = have(i);
        if (known) {
            if (seen)
                seen(id, i);
        } else {
            p.in = i;
            p.receiving.clear();
        }
        net::Writer w;
        w.u8(1);
        w.str8(i.hash);
        w.u8(!known);
        send(id, w);
        return;
    }
    std::string hash = r.str8();
    if (!valid_hash(hash))
        return;
    if (kind == 1) {
        uint8_t need = r.u8();
        if (!r.ok() || r.left() || need > 1 || !p.waiting || p.out.hash != hash || p.sending_data)
            return;
        if (!need) {
            p.waiting = false;
            p.out = {};
            return;
        }
        if (!load(hash, p.sending) || p.sending.size() != p.out.size || upd::sha256_hex(p.sending) != hash) {
            net::Writer w;
            w.u8(4);
            w.str8(hash);
            send(id, w);
            p.waiting = false;
            p.out = {};
            p.sending.clear();
            return;
        }
        p.offset = 0;
        p.sending_data = true;
    } else if (kind == 2) {
        uint32_t offset = r.u32();
        size_t n = r.u16();
        if (!r.ok() || hash != p.in.hash || n == 0 || n > kChunk || n != r.left() || offset != p.receiving.size() ||
            offset > p.in.size || n > p.in.size - offset)
            return;
        p.receiving.insert(p.receiving.end(), r.cur(), r.cur() + n);
        if (p.receiving.size() == p.in.size) {
            bool ok = upd::sha256_hex(p.receiving) == hash && valid_png(p.receiving) && store(id, p.in, p.receiving);
            if (ok && seen)
                seen(id, p.in);
            net::Writer w;
            w.u8(3);
            w.str8(hash);
            w.u8(ok);
            send(id, w);
            p.in = {};
            p.receiving.clear();
        }
    } else if (kind == 3) {
        uint8_t ok = r.u8();
        if (!r.ok() || r.left() || ok > 1 || p.out.hash != hash || !p.waiting || p.offset != p.out.size)
            return;
        p.out = {};
        p.waiting = false;
        p.sending_data = false;
        p.sending.clear();
    } else if (kind == 4) {
        if (r.ok() && !r.left() && p.in.hash == hash) {
            p.in = {};
            p.receiving.clear();
        }
    }
}
void Exchange::tick(uint8_t id) {
    auto it = peers.find(id);
    if (it == peers.end())
        return;
    auto &p = it->second;
    if (p.sending_data && p.offset < p.sending.size()) {
        size_t n = std::min(kChunk, p.sending.size() - p.offset);
        net::Writer w;
        w.u8(2);
        w.str8(p.out.hash);
        w.u32(uint32_t(p.offset));
        w.u16(uint16_t(n));
        w.raw(p.sending.data() + p.offset, n);
        if (send(id, w))
            p.offset += n;
    } else if (!p.waiting) {
        const auto &items = inventory();
        while (p.cursor < items.size()) {
            const auto &i = items[p.cursor];
            if (p.offered.count(i.hash) || (eligible && !eligible(i)) || (eligible_to && !eligible_to(id, i))) {
                ++p.cursor;
                continue;
            }
            net::Writer w;
            w.u8(0);
            write_item(w, i);
            if (send(id, w)) {
                ++p.cursor;
                p.offered.insert(i.hash);
                p.out = i;
                p.waiting = true;
                p.offset = 0;
            }
            break;
        }
    }
}
} // namespace library_sync
