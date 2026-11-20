#include "galaxy_crt.h"

#include <cstdio>
#include <map>
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

static uint64_t key_of(uint32_t inst, uint32_t group) { return (uint64_t)group << 32 | inst; }

struct Own { // this player's creature on its way into the store
    uint32_t inst = 0, type = 0, group = 0, crc = 0;
    std::vector<uint8_t> data;
    bool stored = false;
    int tries = 0;
    double next = 0;
};
static Own g_own;
struct Want { // a creature asked from the store
    uint32_t type = 0;
    double next = 0;
    net::ChunkRx rx;
    bool done = false;
};
static std::map<uint64_t, Want> g_want;
static bool g_was_welcomed;
static uint64_t g_put_sends, g_puts_stored, g_gets, g_got, g_none, g_bad, g_no_bundle;
constexpr int kPutTries = 8;
constexpr double kPutEvery = 3.0, kGetEvery = 5.0, kGetAfterNone = 15.0;

static void send_put()
{
    for (uint32_t off = 0; off < g_own.data.size(); off += kCrtChunk) {
        net::Writer w;
        w.u32(g_own.inst);
        w.u32(g_own.group);
        w.u32((uint32_t)g_own.data.size());
        w.u32(g_own.crc);
        w.u32(off);
        const uint32_t n = std::min<uint32_t>(kCrtChunk, (uint32_t)g_own.data.size() - off);
        w.data.insert(w.data.end(), g_own.data.begin() + off, g_own.data.begin() + off + n);
        galaxy::send_message(CRTPUT, w);
    }
    g_put_sends++;
}

void put_own(uint32_t inst, uint32_t type, uint32_t group)
{
    if (!inst || (inst == g_own.inst && group == g_own.group))
        return;
    g_own = Own{};
    g_own.inst = inst, g_own.type = type, g_own.group = group;
    if (!creations::species_bundle({inst, type, group}, g_own.data) || g_own.data.size() > kCrtMaxBytes) {
        g_no_bundle++;
        g_own.stored = true; // (nothing to put: not asked again for this key)
        H->log(M, SK_WARN, "galaxy creations: no bundle of this player's creature %08X (BUG-9287)", inst);
        return;
    }
    g_own.crc = crc32(0, g_own.data.data(), g_own.data.size());
}

bool want(uint32_t inst, uint32_t type, uint32_t group)
{
    Want& w = g_want[key_of(inst, group)];
    w.type = type;
    const double now = H->time_seconds();
    if (w.done || now < w.next)
        return false;
    w.next = now + kGetEvery;
    net::Writer m;
    m.u32(inst);
    m.u32(group);
    if (galaxy::send_message(CRTGET, m))
        g_gets++;
    return true;
}

static void on_have(net::Reader& r)
{
    const uint32_t inst = r.u32(), group = r.u32();
    const uint8_t have = r.u8();
    if (!r.ok())
        return;
    if (have && inst == g_own.inst && group == g_own.group && !g_own.stored) {
        g_own.stored = true;
        g_puts_stored++;
        H->log(M, SK_INFO, "galaxy creations: the galaxy keeps this player's creature %08X (BUG-9287)", inst);
    }
    if (!have) {
        auto it = g_want.find(key_of(inst, group));
        if (it != g_want.end()) {
            it->second.next = H->time_seconds() + kGetAfterNone; // (its player may put it soon)
            g_none++;
        }
    }
}

static void on_part(net::Reader& r)
{
    const uint32_t inst = r.u32(), group = r.u32(), total = r.u32(), crc = r.u32(), off = r.u32();
    if (!r.ok())
        return;
    auto it = g_want.find(key_of(inst, group));
    if (it == g_want.end() || it->second.done)
        return;
    Want& w = it->second;
    const net::Chunk c = net::chunk_add(w.rx, crc, total, off, r.cur(), r.left(), kCrtChunk, kCrtMaxBytes);
    if (c != net::Chunk::Complete)
        return;
    std::vector<uint8_t> data = std::move(w.rx.data);
    w.rx = net::ChunkRx{};
    if (crc32(0, data.data(), data.size()) != crc) {
        g_bad++;
        return;
    }
    w.done = true;
    g_got++;
    creations::species_from_bundle(data);
    H->log(M, SK_INFO, "galaxy creations: got creature %08X from the galaxy (%u KB) (BUG-9287)", inst, total >> 10);
}

void tick(double now)
{
    const bool welcomed = galaxy::welcomed();
    if (welcomed != g_was_welcomed) { // a new connection (maybe another server): put and ask again
        g_was_welcomed = welcomed;
        g_own.stored = g_own.data.empty();
        g_own.tries = 0;
        g_own.next = 0;
        for (auto& [k, w] : g_want)
            if (!w.done)
                w.next = 0;
    }
    if (!welcomed || g_own.stored || g_own.data.empty() || g_own.tries >= kPutTries || now < g_own.next)
        return;
    g_own.tries++;
    g_own.next = now + kPutEvery;
    send_put();
}

std::string status()
{
    char b[300];
    snprintf(b, sizeof b,
             "galaxy creations (BUG-9287): own %08X %u bytes stored=%d tries=%d puts=%llu kept=%llu no_bundle=%llu; asked %u "
             "(gets %llu, got %llu, none %llu, damaged %llu)\n",
             g_own.inst, (unsigned)g_own.data.size(), (int)g_own.stored, g_own.tries, (unsigned long long)g_put_sends,
             (unsigned long long)g_puts_stored, (unsigned long long)g_no_bundle, (unsigned)g_want.size(),
             (unsigned long long)g_gets, (unsigned long long)g_got, (unsigned long long)g_none, (unsigned long long)g_bad);
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
