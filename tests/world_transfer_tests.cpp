#include "net/protocol.h"
#include "net/connection.h"
#include "net/fixes.h"
#include <algorithm>
#include <cstdio>
#include <random>
#include <string>
using namespace net;
static int failures=0;
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
// Adapted from upstream v0.7.9 BUG-0405 regression, retained as a focused gate.
struct BusyResult {
    int got = 0;
    double secs = 0;
    uint32_t sent = 0, resends = 0;
};
static BusyResult bulk_busy_case(bool fix, bool busy, uint32_t delay, float loss, int seed, bool constrained = false, bool paced = false)
{
    #ifdef LINEAGES_BULK_ACKS
    bool was = g_fix.bulk_acks;
    g_fix.bulk_acks = fix;
#endif
    struct Pk {
        uint32_t t;
        std::vector<uint8_t> d;
    };
    Connection a, b;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0, 1);
    std::deque<Pk> ab, ba; // in flight (constant delay: in order)
    double credit = 4200;
    uint32_t last_bulk = 1000;
    auto send = [&](std::deque<Pk>& q, const std::vector<uint8_t>& p, uint32_t now) {
        if (constrained && &q == &ab && p.size() > 64) {
            credit = std::min(4200.0, credit + (now - last_bulk) * 8.5);
            last_bulk = now;
            if (credit < p.size()) return;
            credit -= p.size();
        }
        if (U(rng) >= loss && q.size() < 3600) // udp.cpp: a 4 MB receive buffer, the rest is dropped
            q.push_back({now + delay, p});
    };
    const int chunk = paced ? (int)kPacedWorldChunkBytes : 1000;
    const int N = (3400000 + chunk - 1) / chunk; // a tribe world: ~3.4 MB in 1 KB chunks
    int queued = 0, got = 0;
    bool ordered = true;
    uint32_t now = 1000, guest_next = 1000;
    std::vector<uint8_t> pkt;
    for (; got < N && now < 1000 + (constrained ? 660000u : 120000u); now += 1) {
        if (now % 33 == 0) { // the host's frame: world.cpp's window, Session::flush's burst
            size_t window = paced ? kPacedWorldWindow : (a.rtt_ms < 40 ? 600 : 160);
            while (queued < N && a.pending_reliable() < window) {
                Writer w;
                w.u32(queued++);
                std::vector<uint8_t> pad(chunk - 4, 0x55);
                w.raw(pad.data(), pad.size());
                a.send_reliable(MSG_WORLD_CHUNK, w);
            }
            int burst = a.rtt_ms < 40.f ? (int)kAckWindow : (int)kAckWindow / 3;
            for (int i = 0; i < burst && a.build_packet(now, pkt, paced); i++)
                send(ab, pkt, now);
        }
        while (!ba.empty() && ba.front().t <= now) { // the host reads in its frame too, but often
            a.on_packet(ba.front().d.data(), ba.front().d.size(), now, [](uint8_t, Reader&) {});
            ba.pop_front();
        }
        if (now >= guest_next) { // the guest's frame: Session::update, then Session::flush
            for (int i = 0; i < 256 && !ab.empty() && ab.front().t <= now; i++) {
                b.on_packet(ab.front().d.data(), ab.front().d.size(), now, [&](uint8_t type, Reader& r) {
                    if (type == MSG_WORLD_CHUNK && r.u32() != (uint32_t)got++)
                        ordered = false;
                });
                ab.pop_front();
                #ifdef LINEAGES_BULK_ACKS
                if (b.ack_urgent() && b.build_packet(now, pkt))
                    send(ba, pkt, now);
#endif
            }
            while (b.build_packet(now, pkt))
                send(ba, pkt, now);
            guest_next = now + (busy ? 200 + (uint32_t)(U(rng) * 750) : 33);
        }
    }
    #ifdef LINEAGES_BULK_ACKS
    g_fix.bulk_acks = was;
#endif
    CHECK(ordered);
    return {got, (now - 1000) / 1000.0, a.stats.packets_sent, a.stats.resends};
}

static void test_bulk_busy_receiver()
{
    printf("bulk transfer to a guest that loads (BUG-0405)\n");
    struct Case {
        const char* name;
        bool busy;
        uint32_t delay;
        float loss;
    } cases[] = {{"clean, guest loads", true, 1, 0}, {"clean, guest plays", false, 1, 0},
                 {"40 ms, 3% loss, guest loads", true, 40, 0.03f}, {"40 ms, 3% loss, guest plays", false, 40, 0.03f},
                 {"150 ms, 10% loss, guest loads", true, 150, 0.10f}};
    for (auto& c : cases) {
        BusyResult r[2];
        for (int fix = 0; fix < 2; fix++) {
            r[fix] = bulk_busy_case(fix, c.busy, c.delay, c.loss, 3);
            printf("  %-30s fix %s: %d/3400 in %.1f s, sent %u, resends %u (%.0f %%)\n", c.name, fix ? "on " : "off",
                   r[fix].got, r[fix].secs, r[fix].sent, r[fix].resends, 100.0 * r[fix].resends / std::max(1u, r[fix].sent));
        }
        CHECK(r[1].got == 3400);
        CHECK(r[1].secs <= r[0].secs * 1.1 + 0.5); // not slower
        if (c.loss == 0)
            CHECK(r[1].resends * 10 <= r[1].sent); // a clean link resends little (was 50-90 %)
        else
            CHECK(r[1].resends <= r[0].resends);
    }
}

static void test_paced_transfer()
{
    auto normal = bulk_busy_case(true, false, 110, 0, 3, true);
    auto paced = bulk_busy_case(true, false, 110, 0, 3, true, true);
    printf("Constrained 8.5 KB/s, 220 ms RTT: normal %d/3400 in %.1f s (%u retries); paced %d in %.1f s (%u retries)\n",
           normal.got, normal.secs, normal.resends, paced.got, paced.secs, paced.resends);
    CHECK(paced.got == (3400000 + kPacedWorldChunkBytes - 1) / kPacedWorldChunkBytes);
    CHECK(paced.secs < 600); // finish within the test budget even with a slow link
    CHECK(paced.resends < normal.resends / 4);
}
static void test_pacing_leaves_live_state_unblocked()
{
    Connection host, guest;
    Writer chunk, state;
    chunk.u32(1);
    state.u32(2);
    host.send_reliable(MSG_WORLD_CHUNK, chunk);
    host.send_reliable(MSG_WORLD_CHUNK, chunk);
    std::vector<uint8_t> packet;
    unsigned chunks = 0, states = 0;
    auto receive = [&](uint32_t now) {
        CHECK(guest.on_packet(packet.data(), packet.size(), now, [&](uint8_t type, Reader&) {
            if (type == MSG_WORLD_CHUNK) ++chunks;
            if (type == MSG_PLAYER_STATE) ++states;
        }));
    };
    CHECK(host.build_packet(1000, packet, true));
    receive(1000);
    CHECK(chunks == 1);
    CHECK(!host.build_packet(1010, packet, true));
    host.send_unreliable(MSG_PLAYER_STATE, state);
    CHECK(host.build_packet(1010, packet, true));
    receive(1010);
    CHECK(states == 1 && chunks == 1);
    CHECK(host.build_packet(1000 + kPacedWorldIntervalMs, packet, true));
    receive(1000 + kPacedWorldIntervalMs);
    CHECK(chunks == 2);
}

int main()
{
    test_bulk_busy_receiver();
    test_paced_transfer();
    test_pacing_leaves_live_state_unblocked();
    return failures ? 1 : 0;
}

