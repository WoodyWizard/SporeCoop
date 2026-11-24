#include "galaxy_crt.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

#include "creations.h"
#include "galaxy_client.h"
#include "net/buffer.h"
#include "net/galaxy.h"
#include "net/validate.h"

namespace galaxy_crt {

static const SkHost* H;
static SkMod* M;
using namespace net::galaxy;

struct Key4 {
    uint64_t owner = 0;
    uint32_t k[3]{};
    bool operator<(const Key4& o) const
    {
        return std::tie(owner, k[0], k[1], k[2]) < std::tie(o.owner, o.k[0], o.k[1], o.k[2]);
    }
};

struct Put { // a creation of this game on its way into the store
    std::vector<uint8_t> data;
    uint32_t crc = 0;
    bool built = false, stored = false;
    int tries = 0;
    double next = 0;
};
static std::map<std::tuple<uint32_t, uint32_t, uint32_t>, Put> g_puts; // {instance, type, group}
struct Want { // another player's creation asked from the store
    double next = 0;
    net::ChunkRx rx;
    enum { kAsking, kHere, kNone } state = kAsking;
    uint32_t local[3]{};
};
static std::map<Key4, Want> g_want;
static bool g_was_welcomed;
static uint64_t g_put_sends, g_puts_stored, g_no_bundle, g_gets, g_got, g_none, g_bad, g_not_installed;
constexpr int kPutTries = 8;
constexpr double kPutEvery = 3.0, kGetEvery = 5.0, kNoneFor = 30.0;
constexpr int kBuildsPerTick = 2;

static void send_put(uint32_t inst, uint32_t type, uint32_t group, const Put& p)
{
    for (uint32_t off = 0; off < p.data.size(); off += kCrtChunk) {
        net::Writer w;
        w.u32(inst);
        w.u32(type);
        w.u32(group);
        w.u32((uint32_t)p.data.size());
        w.u32(p.crc);
        w.u32(off);
        const uint32_t n = std::min<uint32_t>(kCrtChunk, (uint32_t)p.data.size() - off);
        w.data.insert(w.data.end(), p.data.begin() + off, p.data.begin() + off + n);
        galaxy::send_message(CRTPUT, w);
    }
    g_put_sends++;
}

void put(uint32_t inst, uint32_t type, uint32_t group)
{
    if (inst)
        g_puts.try_emplace({inst, type, group});
}

bool local_key(uint64_t owner, const uint32_t key[3], uint32_t out[3])
{
    Want& w = g_want[{owner, {key[0], key[1], key[2]}}];
    if (w.state == Want::kHere) {
        std::copy(w.local, w.local + 3, out);
        return true;
    }
    const double now = H->time_seconds();
    if (w.state == Want::kNone) { // its owner never put it: the game's own content, or not yet
        std::copy(key, key + 3, out);
        if (now >= w.next)
            w.state = Want::kAsking, w.next = 0; // (asked again now and then)
        return true;
    }
    if (now >= w.next) {
        w.next = now + kGetEvery;
        net::Writer m;
        m.u64(owner);
        for (int i = 0; i < 3; i++)
            m.u32(key[i]);
        if (galaxy::send_message(CRTGET, m))
            g_gets++;
    }
    return false;
}

bool here(uint64_t owner, const uint32_t key[3])
{
    auto it = g_want.find({owner, {key[0], key[1], key[2]}});
    return it != g_want.end() && it->second.state == Want::kHere;
}

static Key4 read_key(net::Reader& r)
{
    Key4 k;
    k.owner = r.u64();
    for (auto& v : k.k)
        v = r.u32();
    return k;
}

static void on_have(net::Reader& r)
{
    const Key4 k = read_key(r);
    const uint8_t have = r.u8();
    if (!r.ok())
        return;
    if (have) { // our put is kept
        auto it = g_puts.find({k.k[0], k.k[1], k.k[2]});
        if (it != g_puts.end() && !it->second.stored) {
            it->second.stored = true;
            g_puts_stored++;
        }
        return;
    }
    auto it = g_want.find(k);
    if (it != g_want.end() && it->second.state == Want::kAsking) {
        it->second.state = Want::kNone;
        it->second.next = H->time_seconds() + kNoneFor;
        g_none++;
    }
}

static void on_part(net::Reader& r)
{
    const Key4 k = read_key(r);
    const uint32_t total = r.u32(), crc = r.u32(), off = r.u32();
    if (!r.ok())
        return;
    auto it = g_want.find(k);
    if (it == g_want.end() || it->second.state == Want::kHere)
        return;
    Want& w = it->second;
    if (net::chunk_add(w.rx, crc, total, off, r.cur(), r.left(), kCrtChunk, kCrtMaxBytes) != net::Chunk::Complete)
        return;
    std::vector<uint8_t> data = std::move(w.rx.data);
    w.rx = net::ChunkRx{};
    if (crc32(0, data.data(), data.size()) != crc) {
        g_bad++;
        return;
    }
    const creations::Key ours = creations::creation_from_bundle(data);
    if (!ours.instance) {
        g_not_installed++;
        w.state = Want::kNone; // (built from its own key meanwhile)
        w.next = H->time_seconds() + kNoneFor;
        return;
    }
    w.state = Want::kHere;
    w.local[0] = ours.instance, w.local[1] = ours.type, w.local[2] = ours.group;
    g_got++;
    H->log(M, SK_INFO, "galaxy creations: %08X/%08X of player %016llX is %08X here (%u KB) (BUG-9290)", k.k[0], k.k[1],
           (unsigned long long)k.owner, ours.instance, total >> 10);
}

void tick(double now)
{
    const bool welcomed = galaxy::welcomed();
    if (welcomed != g_was_welcomed) { // a new connection (maybe another server): put and ask again
        g_was_welcomed = welcomed;
        for (auto& [k, p] : g_puts)
            p.stored = false, p.tries = 0, p.next = 0;
        for (auto& [k, w] : g_want)
            if (w.state != Want::kHere)
                w.state = Want::kAsking, w.next = 0;
    }
    if (!welcomed)
        return;
    int builds = 0;
    for (auto& [k, p] : g_puts) {
        if (p.stored || p.tries >= kPutTries || now < p.next)
            continue;
        if (!p.built) {
            if (builds++ >= kBuildsPerTick)
                break;
            p.built = true;
            if (!creations::creation_bundle({std::get<0>(k), std::get<1>(k), std::get<2>(k)}, p.data) ||
                p.data.size() > kCrtMaxBytes) {
                g_no_bundle++;
                p.stored = true; // (nothing to put: its game has no records of it)
                continue;
            }
            p.crc = crc32(0, p.data.data(), p.data.size());
        }
        p.tries++;
        p.next = now + kPutEvery;
        send_put(std::get<0>(k), std::get<1>(k), std::get<2>(k), p);
        break; // (one creation a frame)
    }
}

std::string status()
{
    unsigned kept = 0, here = 0;
    for (auto& [k, p] : g_puts)
        kept += p.stored;
    for (auto& [k, w] : g_want)
        here += w.state == Want::kHere;
    char b[300];
    snprintf(b, sizeof b,
             "galaxy creations (BUG-9287/9290): put %u (kept %u, sends %llu, no bundle %llu); asked %u (here %u, gets %llu, "
             "got %llu, none %llu, damaged %llu, not installed %llu)\n",
             (unsigned)g_puts.size(), kept, (unsigned long long)g_put_sends, (unsigned long long)g_no_bundle,
             (unsigned)g_want.size(), here, (unsigned long long)g_gets, (unsigned long long)g_got,
             (unsigned long long)g_none, (unsigned long long)g_bad, (unsigned long long)g_not_installed);
    return b;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    galaxy::on_message(CRTHAVE, on_have);
    galaxy::on_message(CRTPART, on_part);
}

} // namespace galaxy_crt
