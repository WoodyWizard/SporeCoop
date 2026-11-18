#include "../updater/sha256.h"
#include "net/library_sync.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <map>
using namespace library_sync;
// Valid 1x1 RGBA PNG (structural fixture; not passed to Spore's importer).
static const uint8_t png_bytes[] = {137, 80,  78, 71,  13, 10, 26, 10,  0,   0,  0,  13, 73, 72,  68,  82,  0,
                                    0,   0,   1,  0,   0,  0,  1,  8,   6,   0,  0,  0,  31, 21,  196, 137, 0,
                                    0,   0,   11, 73,  68, 65, 84, 120, 156, 99, 96, 0,  2,  0,   0,   5,   0,
                                    1,   122, 94, 171, 63, 0,  0,  0,   0,   73, 69, 78, 68, 174, 66,  96,  130};
struct Node {
    Exchange exchange;
    std::map<std::string, std::vector<uint8_t>> files;
    std::map<std::string, Item> items;
    std::vector<Item> offers;
    int writes = 0;
    Node() {
        exchange.inventory = [this]() -> const std::vector<Item> & { return offers; };
        exchange.have = [this](const Item &i) { return files.count(i.hash) > 0; };
        exchange.load = [this](const std::string &h, std::vector<uint8_t> &b) {
            if (!files.count(h))
                return false;
            b = files.at(h);
            return true;
        };
        exchange.store = [this](uint8_t, const Item &i, const std::vector<uint8_t> &b) {
            ++writes;
            files[i.hash] = b;
            items[i.hash] = i;
            offers.push_back(i);
            return true;
        };
    }
};
int main() {
    std::vector<uint8_t> png(std::begin(png_bytes), std::end(png_bytes));
    assert(valid_png(png));
    // A valid ancillary text chunk exercises multiple data packets per PNG.
    std::vector<uint8_t> chunk{'t', 'E', 'X', 't', 'n', 0};
    chunk.resize(2004, 'a');
    uint32_t crc = ~0u;
    for (uint8_t b : chunk) {
        crc ^= b;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    crc = ~crc;
    const uint32_t text_size = uint32_t(chunk.size() - 4);
    std::vector<uint8_t> framed{uint8_t(text_size >> 24), uint8_t(text_size >> 16), uint8_t(text_size >> 8),
                                uint8_t(text_size)};
    framed.insert(framed.end(), chunk.begin(), chunk.end());
    for (int shift = 24; shift >= 0; shift -= 8)
        framed.push_back(uint8_t(crc >> shift));
    png.insert(png.end() - 12, framed.begin(), framed.end());
    assert(valid_png(png) && png.size() > 2 * kChunk);
    auto bad = png;
    bad[40] ^= 1;
    assert(!valid_png(bad));
    bad = png;
    bad.push_back(0);
    assert(!valid_png(bad));
    bad = png;
    bad[8] = 255;
    assert(!valid_png(bad));
    assert(!valid_hash("../../bad") && !valid_hash(std::string(64, 'G')));
    Item item{upd::sha256_hex(png), "Original Author", "Creature", "stable-source", "Co-op Name", uint32_t(png.size())};
    net::Writer encoded;
    write_item(encoded, item);
    for (size_t n = 0; n < encoded.size(); ++n) {
        net::Reader r(encoded.data.data(), n);
        Item partial;
        assert(!read_item(r, partial));
    }
    Node a, b;
    a.files[item.hash] = png;
    a.items[item.hash] = item;
    a.offers.push_back(item);
    struct Packet {
        int to;
        std::vector<uint8_t> bytes;
    };
    std::deque<Packet> queue;
    a.exchange.send = [&](uint8_t, const net::Writer &w) {
        queue.push_back({1, w.data});
        return true;
    };
    b.exchange.send = [&](uint8_t, const net::Writer &w) {
        queue.push_back({0, w.data});
        return true;
    };
    auto connect = [&] {
        a.exchange.join(1);
        b.exchange.join(0);
    };
    auto drive = [&] {
        for (int frame = 0; frame < 100; ++frame) {
            a.exchange.tick(1);
            b.exchange.tick(0);
            while (!queue.empty()) {
                auto p = std::move(queue.front());
                queue.pop_front();
                net::Reader r(p.bytes.data(), p.bytes.size());
                if (p.to)
                    b.exchange.receive(0, r);
                else
                    a.exchange.receive(1, r);
            }
        }
    };
    connect();
    drive();
    assert(b.writes == 1 && b.files == a.files);
    assert(b.items.at(item.hash).author == "Original Author" && b.items.at(item.hash).sharer == "Co-op Name");
    a.exchange.reset();
    b.exchange.reset();
    connect();
    drive();
    assert(b.writes == 1);
    // A bogus chunk cannot allocate its advertised offset or poison the next transfer.
    net::Writer attack;
    attack.u8(2);
    attack.str8(item.hash);
    attack.u32(0xFFFFFFFF);
    attack.u16(800);
    net::Reader r(attack.data.data(), attack.size());
    b.exchange.receive(0, r);
    assert(b.exchange.incoming_bytes() == 0);
    // Disconnect during an offered transfer drops partial state; reconnect resumes by digest.
    b.files.clear();
    b.items.clear();
    b.offers.clear();
    a.exchange.reset();
    b.exchange.reset();
    connect();
    a.exchange.tick(1);
    auto offer = std::move(queue.front());
    queue.pop_front();
    net::Reader offer_reader(offer.bytes.data(), offer.bytes.size());
    b.exchange.receive(0, offer_reader);
    auto need = std::move(queue.front());
    queue.pop_front();
    net::Reader need_reader(need.bytes.data(), need.bytes.size());
    a.exchange.receive(1, need_reader);
    a.exchange.tick(1);
    auto partial = std::move(queue.front());
    queue.pop_front();
    net::Reader partial_reader(partial.bytes.data(), partial.bytes.size());
    b.exchange.receive(0, partial_reader);
    assert(b.exchange.incoming_bytes() == kChunk);
    a.exchange.leave(1);
    b.exchange.leave(0);
    queue.clear();
    connect();
    drive();
    assert(b.writes == 2);
    // Four seats, host-only links: a guest upload reaches the other two guests.
    Node host, guest1, guest2, guest3;
    Node *nodes[] = {&host, &guest1, &guest2, &guest3};
    guest1.files[item.hash] = png;
    guest1.items[item.hash] = item;
    guest1.offers.push_back(item);
    struct Relay {
        int from, to;
        std::vector<uint8_t> bytes;
    };
    std::deque<Relay> relays;
    for (int i = 0; i < 4; ++i) {
        nodes[i]->exchange.send = [&, i](uint8_t to, const net::Writer &w) {
            relays.push_back({i, to, w.data});
            return true;
        };
        if (i) {
            host.exchange.join(uint8_t(i));
            nodes[i]->exchange.join(0);
        }
    }
    for (int frame = 0; frame < 300; ++frame) {
        for (int i = 1; i < 4; ++i) {
            host.exchange.tick(uint8_t(i));
            nodes[i]->exchange.tick(0);
        }
        while (!relays.empty()) {
            auto p = std::move(relays.front());
            relays.pop_front();
            assert(p.from == 0 || p.to == 0);
            net::Reader payload(p.bytes.data(), p.bytes.size());
            nodes[p.to]->exchange.receive(uint8_t(p.from), payload);
        }
    }
    for (int i = 0; i < 4; ++i)
        assert(nodes[i]->files.at(item.hash) == png);
    assert(host.writes == 1 && guest1.writes == 0 && guest2.writes == 1 && guest3.writes == 1);
    // BUG-9112: the host offers each guest by its choice: a foreign creation reaches only the guest
    // that wants foreign creations (eligible_to), the connected player's own goes to everyone
    {
        std::vector<uint8_t> chunk2{'t', 'E', 'X', 't', 'n', 0};
        chunk2.resize(64, 'b');
        uint32_t c2 = ~0u;
        for (uint8_t v : chunk2) {
            c2 ^= v;
            for (int bit = 0; bit < 8; ++bit)
                c2 = (c2 >> 1) ^ (0xEDB88320u & (0u - (c2 & 1)));
        }
        c2 = ~c2;
        const uint32_t n2 = uint32_t(chunk2.size() - 4);
        std::vector<uint8_t> png2(std::begin(png_bytes), std::end(png_bytes));
        std::vector<uint8_t> f2{uint8_t(n2 >> 24), uint8_t(n2 >> 16), uint8_t(n2 >> 8), uint8_t(n2)};
        f2.insert(f2.end(), chunk2.begin(), chunk2.end());
        for (int shift = 24; shift >= 0; shift -= 8)
            f2.push_back(uint8_t(c2 >> shift));
        png2.insert(png2.end() - 12, f2.begin(), f2.end());
        assert(valid_png(png2));
        Item foreign{upd::sha256_hex(png2), "Stranger", "Cell", "foreign-source", "Host", uint32_t(png2.size())};
        host.files[foreign.hash] = png2;
        host.items[foreign.hash] = foreign;
        host.offers.push_back(foreign);
        host.exchange.eligible_to = [&](uint8_t peer, const Item &i) { return i.source != "foreign-source" || peer == 2; };
        for (int i = 1; i < 4; ++i) {
            host.exchange.leave(uint8_t(i));
            host.exchange.join(uint8_t(i));
        }
        for (int frame = 0; frame < 300; ++frame) {
            for (int i = 1; i < 4; ++i) {
                host.exchange.tick(uint8_t(i));
                nodes[i]->exchange.tick(0);
            }
            while (!relays.empty()) {
                auto p = std::move(relays.front());
                relays.pop_front();
                net::Reader payload(p.bytes.data(), p.bytes.size());
                nodes[p.to]->exchange.receive(uint8_t(p.from), payload);
            }
        }
        assert(guest2.files.count(foreign.hash) && !guest3.files.count(foreign.hash) && !guest1.files.count(foreign.hash));
        std::cout << "PASS foreign creations only to the guest that wants them (BUG-9112)\n";
    }
    std::cout << "PASS library integrity, truncation, authorship, transfer, dedup, disconnect/reconnect\n";
    std::cout << "PASS four-seat host relay without echo imports\n";
}
