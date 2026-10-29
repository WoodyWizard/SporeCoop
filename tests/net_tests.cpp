// Native (Linux) tests for the portable coop netcode: make test
#include "../updater/pack.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include "net/dbpf.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <random>
#include <chrono>
#include <thread>
#include <type_traits>
#include <arpa/inet.h>

#include "net/connection.h"
#include "net/diagmsg.h"
#include "net/discovery.h"
#include "net/fixes.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/lobby.h"
#include "net/shared_ledger.h"
#include "net/session.h"
#include "net/ipmask.h"

using namespace net;

static int g_fail = 0;
static bool g_nofix = false; // --nofix: the code before the wave 2 netcode fixes
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void test_buffer()
{
    printf("buffer roundtrip\n");
    PlayerState s;
    s.id = 3; s.time_ms = 123456; s.pos = {1, 2, 3}; s.rot = {0, 0, 0.7071f, 0.7071f};
    s.vel = {4, 5, 6}; s.flags = PF_IN_WORLD | PF_RUNNING; s.anim = 7; s.health = 50; s.max_health = 100;
    s.species = 0xDEADBEEF;
    Writer w;
    s.write(w);
    Reader r(w.data.data(), w.size());
    PlayerState t;
    CHECK(t.read(r));
    CHECK(t.id == 3 && t.time_ms == 123456 && t.pos.z == 3 && t.rot.w > 0.7f && t.flags == s.flags);
    CHECK(t.species == 0xDEADBEEF && t.max_health == 100);
    Reader bad(w.data.data(), 5);
    PlayerState u;
    CHECK(!u.read(bad));
    // v2: mode + world identify where the player is
    s.mode = 0x1654C05; s.world = 0x80B16021;
    Writer w2;
    s.write(w2);
    Reader r2(w2.data.data(), w2.size());
    PlayerState t2;
    CHECK(t2.read(r2) && t2.mode == 0x1654C05 && t2.world == 0x80B16021);
    // shared resource message payload (u8 id, f64)
    Writer w3;
    w3.u8(3);
    w3.f64(123456.75);
    Reader r3(w3.data.data(), w3.size());
    CHECK(r3.u8() == 3 && r3.f64() == 123456.75 && r3.ok() && r3.left() == 0);
}

// a snapshot in several big unreliable chunks: every chunk goes out (one per packet), none is lost
static void test_unreliable_chunks()
{
    printf("unreliable chunks larger than one packet\n");
    Connection a, b;
    for (int k = 0; k < 5; k++) {
        Writer w;
        w.u8((uint8_t)k);
        for (int i = 0; i < 1000; i++)
            w.u8(0x55);
        a.send_unreliable(9, w);
    }
    std::vector<uint8_t> pkt;
    std::vector<int> got;
    int packets = 0;
    while (a.build_packet(2000, pkt)) {
        packets++;
        b.on_packet(pkt.data(), pkt.size(), 2000, [&](uint8_t type, Reader& r) {
            if (type == 9) got.push_back(r.u8());
        });
    }
    CHECK(packets == 5);
    CHECK(got.size() == 5 && got[0] == 0 && got[4] == 4);
    CHECK(a.unreliable_queued() == 0);
}

static void test_be_keys()
{
    printf("dbpf: species keys inside a save's records\n");
    auto key = [](uint32_t inst) {
        std::vector<uint8_t> v = {0x40, 0x62, 0x62, 0x00, 0x2B, 0x97, 0x8C, 0x46};
        for (int b = 0; b < 4; b++)
            v.push_back((uint8_t)(inst >> (24 - 8 * b)));
        return v;
    };
    std::vector<uint8_t> herd = {7, 7};
    for (uint32_t k : {0x29020CC4u, 0x065CBE70u, 0x29020CC4u}) {
        auto v = key(k);
        herd.insert(herd.end(), v.begin(), v.end());
        herd.push_back(1);
    }
    // the pack list: little-endian (type, instance, group)
    std::vector<uint8_t> pack = {0x10, 0x01, 0, 0, 0x46, 0x8C, 0x97, 0x2B, 0xB9, 0x29, 0x02, 0x29, 0x00, 0x62, 0x62, 0x40, 0};
    std::vector<dbpf::Record> recs = {{{0, 0x01BE418E, 0}, herd}, {{0, 0x02C21781, 0}, key(0x065CBE70)},
                                      {{0, 0x01897C18, 0}, pack}};
    std::vector<uint8_t> f = dbpf::write(recs);
    std::vector<uint32_t> found;
    dbpf::find_keys(f, 0x2B978C46, 0x40626200, found);
    CHECK(found.size() == 3 && found[0] == 0x29020CC4 && found[1] == 0x065CBE70 && found[2] == 0x290229B9);
}

static void test_dbpf()
{
    printf("dbpf write/read, EditorSaves of the test profile\n");
    std::vector<dbpf::Record> recs = {{{0x7C000001, 0x1A99B06B, 0x40626200}, {1, 2, 3, 4}},
                                      {{0x7C000001, 0x030BDEE3, 0x40626200}, std::vector<uint8_t>(300, 9)}};
    dbpf::Package pk;
    CHECK(pk.load(dbpf::write(recs)));
    CHECK(pk.entries().size() == 2);
    std::vector<uint8_t> out;
    const dbpf::Entry* e = pk.find(recs[1].key);
    CHECK(e && pk.read(*e, out) && out == recs[1].data);
    // the real thing (RefPack records, pollen metadata names), when the test profile exists
    const char* home = getenv("HOME");
    const char* user = getenv("USER");
    if (!home || !user)
        return;
    std::string path = std::string(home) + "/.var/app/com.usebottles.bottles/data/bottles/bottles/Games/drive_c/users/" +
                       user + "/AppData/Roaming/SporeKit-test/Spore/EditorSaves.package";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        printf("  (no test profile EditorSaves.package)\n");
        return;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    dbpf::Package es;
    CHECK(es.load(file));
    int bems = 0, names = 0;
    for (auto& en : es.entries()) {
        std::vector<uint8_t> d;
        if (en.key.type == 0x1A99B06B && en.key.group == 0x40626200) {
            CHECK(es.read(en, d) && d.size() > 1000 && d[0] == 0xD3 && d[1] == 0x7E);
            bems++;
        }
        std::string a, n;
        if (en.key.type == 0x030BDEE3 && es.read(en, d) && dbpf::metadata_names(d, a, n) && !n.empty())
            names++;
    }
    printf("  %d creatures, %d names\n", bems, names);
    CHECK(bems > 0 && names >= bems);
    // Pollination.package is a DBBF (64-bit offsets): read piece by piece, as creations.cpp does
    std::ifstream pf(path.substr(0, path.size() - strlen("EditorSaves.package")) + "Pollination.package", std::ios::binary);
    if (!pf) {
        printf("  (no test profile Pollination.package)\n");
        return;
    }
    std::vector<uint8_t> pol((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
    dbpf::IndexInfo info;
    CHECK(dbpf::index_info(pol.data(), pol.size(), info) && info.big && info.offset + info.size <= pol.size());
    std::vector<dbpf::Entry> all;
    CHECK(dbpf::parse_index(pol.data() + info.offset, info.size, info, all) && all.size() == info.count);
    int crts = 0;
    for (auto& en : all) {
        std::vector<uint8_t> d;
        if (en.key.type == 0x2B978C46 && en.offset + en.size <= pol.size() && dbpf::decode(en, pol.data() + en.offset, d) &&
            std::string(d.begin(), d.end()).find("sporemodel") != std::string::npos)
            crts++;
    }
    printf("  Pollination: %zu entries, %d creature models\n", all.size(), crts);
    CHECK(crts > 0);
    dbpf::Package pp;
    CHECK(pp.load(pol) && pp.entries().size() == all.size());
}

static void test_dbbf()
{
    printf("dbpf: a DBBF package (64-bit offsets)\n");
    // the layout of DBBF: magic, the index offset as u64 at 0x38, entries with a u64 offset
    std::vector<uint8_t> f(96, 0);
    memcpy(f.data(), "DBBF", 4);
    std::vector<uint8_t> data = {5, 6, 7};
    size_t doff = f.size();
    f.insert(f.end(), data.begin(), data.end());
    size_t ioff = f.size();
    auto put = [&](uint64_t v, int n) {
        for (int i = 0; i < n; i++)
            f.push_back((uint8_t)(v >> (8 * i)));
    };
    put(0, 4); // flags
    put(0x2B978C46, 4); put(0x40626200, 4); put(0x12345678, 4);
    put(doff, 8); put(3 | 0x80000000u, 4); put(3, 4); put(0, 2); put(1, 2);
    uint32_t isize = (uint32_t)(f.size() - ioff);
    f[0x24] = 1;
    memcpy(&f[0x28], &isize, 4); // u64 index size at 0x28 (a DBPF has it at 0x2C)
    uint64_t io = ioff;
    memcpy(&f[0x38], &io, 8);
    dbpf::Package pk;
    CHECK(pk.load(f) && pk.entries().size() == 1);
    std::vector<uint8_t> out;
    const dbpf::Entry* e = pk.find({0x12345678, 0x2B978C46, 0x40626200});
    CHECK(e && e->offset == doff && pk.read(*e, out) && out == data);
}

static void test_reliability()
{
    printf("reliable channel under 30%% loss + reordering\n");
    Connection a, b;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> U(0, 1);
    std::vector<std::vector<uint8_t>> a2b, b2a;
    std::vector<uint32_t> got;
    const int N = 400;
    uint32_t now = 1000;
    int sent = 0;
    for (int step = 0; step < 4000 && ((int)got.size() < N || a.pending_reliable()); step++) {
        now += 10;
        if (sent < N && step % 2 == 0) {
            Writer w;
            w.u32(sent++);
            a.send_reliable(42, w);
        }
        if (step % 3 == 0) {
            Writer w;
            w.u32(0xAAAA);
            a.send_unreliable(7, w);
        }
        std::vector<uint8_t> pkt;
        while (a.build_packet(now, pkt))
            if (U(rng) > 0.3f) a2b.push_back(pkt);
        while (b.build_packet(now, pkt))
            if (U(rng) > 0.3f) b2a.push_back(pkt);
        // deliver with random reordering
        std::shuffle(a2b.begin(), a2b.end(), rng);
        for (auto& p : a2b)
            b.on_packet(p.data(), p.size(), now, [&](uint8_t type, Reader& r) {
                if (type == 42) got.push_back(r.u32());
            });
        a2b.clear();
        for (auto& p : b2a)
            a.on_packet(p.data(), p.size(), now, [](uint8_t, Reader&) {});
        b2a.clear();
    }
    CHECK((int)got.size() == N);
    bool ordered = true;
    for (size_t i = 0; i < got.size(); i++)
        if (got[i] != i) ordered = false;
    CHECK(ordered);
    CHECK(a.pending_reliable() == 0);
    printf("  delivered %zu/%d, resends=%u rtt=%.0f\n", got.size(), N, a.stats.resends, a.rtt_ms);
}

static void pump(Session& h, Session& c, int ms)
{
    for (int i = 0; i < ms / 5; i++) {
        h.update();
        c.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static void test_session()
{
    printf("session host/join/state/chat/leave over localhost\n");
    Session host, cli;
    int joins = 0, states = 0, chats = 0, leaves = 0;
    bool connected = false;
    host.build = cli.build = 0x922af7c7;
    host.cb.player_join = [&](const PlayerInfo& p) { joins++; CHECK(p.name == "guest-bob"); };
    host.cb.player_state = [&](const PlayerState& s) { states++; CHECK(s.id == 1 && s.pos.x == 10); };
    host.cb.chat = [&](uint8_t id, const std::string& t) { if (id == 1) { chats++; CHECK(t == "hi"); } };
    host.cb.player_leave = [&](uint8_t, uint8_t) { leaves++; };
    cli.cb.connected = [&] { connected = true; };
    std::string err;
    CHECK(host.host(0, "host-alice", err));
    uint16_t port = host.port();
    CHECK(cli.join("127.0.0.1:" + std::to_string(port), "guest-bob", err));
    pump(host, cli, 300);
    CHECK(connected);
    CHECK(joins == 1);
    CHECK(cli.my_id() == 1);
    CHECK(cli.name_of(0) == "host-alice");
    PlayerState s;
    s.pos = {10, 0, 0};
    for (int i = 0; i < 5; i++) {
        cli.send_state(s);
        pump(host, cli, 50);
    }
    CHECK(states >= 3);
    int cstates = 0;
    cli.cb.player_state = [&](const PlayerState& st) { if (st.id == 0) cstates++; };
    host.send_state(s);
    pump(host, cli, 100);
    CHECK(cstates == 1);
    cli.send_chat("hi");
    pump(host, cli, 100);
    CHECK(chats == 1);
    cli.leave("test");
    pump(host, cli, 200);
    CHECK(leaves == 1);
    printf("  %s", host.status().c_str());

    // build mismatch must be rejected
    Session bad;
    bad.build = 0x1234;
    std::string why;
    bad.cb.disconnected = [&](const std::string& w) { why = w; };
    CHECK(bad.join("127.0.0.1:" + std::to_string(port), "x", err));
    pump(host, bad, 300);
    CHECK(why.find("build") != std::string::npos);
    CHECK(bad.role() == Role::None);
}

// BUG-0029: a guest that gets REJECT or BYE must not free its connection while that connection is
// still parsing the packet (heap use-after-free, visible under -fsanitize=address: `make test-asan`)
static void test_session_end_in_delivery()
{
    printf("guest leaves after REJECT / BYE without touching the freed connection\n");
    std::string err;
    Session host, cli;
    host.build = cli.build = 0x922af7c7;
    int disc = 0;
    std::string why;
    cli.cb.disconnected = [&](const std::string& w) { disc++; why = w; };
    CHECK(host.host(0, "h", err));
    CHECK(cli.join("127.0.0.1:" + std::to_string(host.port()), "g", err));
    pump(host, cli, 300);
    CHECK(cli.connected());
    // a reliable message still unacknowledged when the host leaves: it rides in the packet after BYE
    Writer w;
    w.u32(1);
    host.broadcast_reliable(MSG_NOTICE, w);
    host.leave("test");
    pump(host, cli, 100);
    CHECK(disc == 1);
    CHECK(why == "host closed the session");
    CHECK(cli.role() == Role::None);

    // REJECT (build mismatch): disconnected exactly once, with the reason
    Session h2, bad;
    h2.build = 0x922af7c7;
    bad.build = 0x1234;
    int bdisc = 0;
    std::string bwhy;
    bad.cb.disconnected = [&](const std::string& w2) { bdisc++; bwhy = w2; };
    CHECK(h2.host(0, "h", err));
    CHECK(bad.join("127.0.0.1:" + std::to_string(h2.port()), "x", err));
    pump(h2, bad, 300);
    CHECK(bdisc == 1);
    CHECK(bwhy.find("rejected: ") == 0 && bwhy.find("build") != std::string::npos);
    CHECK(bad.role() == Role::None);
}

// a UDP relay between guests and the host (like tools/tests/netem.py or a NAT that keeps its mapping):
// everything from the guests reaches the host from ONE address and port
struct Relay {
    UdpSocket a, b; // a faces the guests, b faces the host
    Endpoint host, guest;
    bool drop = false;
    std::vector<std::vector<uint8_t>> up; // what went to the host (for replays)
    Relay(uint16_t host_port)
    {
        a.open(0);
        b.open(0);
        Endpoint::parse("127.0.0.1", host_port, host);
    }
    void pump()
    {
        uint8_t buf[2048];
        Endpoint from;
        for (int n; (n = a.recv_from(from, buf, sizeof buf)) > 0;) {
            guest = from;
            up.emplace_back(buf, buf + n);
            if (!drop)
                b.send_to(host, buf, n);
        }
        for (int n; (n = b.recv_from(from, buf, sizeof buf)) > 0;)
            if (!drop && guest.port)
                a.send_to(guest, buf, n);
    }
};

// a guest whose goodbye got lost joins again through the same relay: the host sees the same address
// and port; the new HELLO used to land in the old (accepted) peer and was dropped as a duplicate,
// the old peer never timed out (the new connection kept it alive): the guest never got back in
static void test_rejoin_same_endpoint()
{
    printf("rejoin through a relay (same address for the host), goodbye lost\n");
    Session host;
    int joins = 0, leaves = 0, chats = 0;
    host.cb.player_join = [&](const PlayerInfo&) { joins++; };
    host.cb.player_leave = [&](uint8_t, uint8_t) { leaves++; };
    host.cb.chat = [&](uint8_t id, const std::string& t) { chats += id == 1 && t == "back"; };
    std::string err;
    CHECK(host.host(0, "host", err));
    Relay relay(host.port());
    std::string addr = "127.0.0.1:" + std::to_string(relay.a.local_port());
    auto run = [&](Session& c, int ms) {
        for (int i = 0; i < ms / 5; i++) {
            host.update();
            c.update();
            relay.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    Session g1;
    CHECK(g1.join(addr, "bob", err));
    run(g1, 300);
    CHECK(g1.connected() && joins == 1);
    for (int i = 0; i < 5; i++) { // some traffic of the first connection (late copies replayed below)
        g1.send_chat("old");
        run(g1, 20);
    }
    size_t old_pkts = relay.up.size();
    relay.drop = true; // the goodbye is lost
    g1.leave("test");
    run(g1, 50);
    relay.drop = false;
    CHECK(leaves == 0);
    Session g2; // joins again (a new socket, but the relay keeps the host-facing address)
    bool back = false;
    g2.cb.connected = [&] { back = true; };
    CHECK(g2.join(addr, "bob", err));
    run(g2, 600);
    CHECK(back);
    CHECK(leaves == 1 && joins == 2);
    CHECK(host.players().size() == 2);
    // late packets of the first connection (delayed / duplicated by the network) are ignored
    for (size_t i = 0; i < old_pkts; i++)
        relay.b.send_to(relay.host, relay.up[i].data(), relay.up[i].size());
    run(g2, 100);
    g2.send_chat("back");
    run(g2, 200);
    CHECK(chats == 1);
    CHECK(leaves == 1 && host.players().size() == 2);
    CHECK(g2.name_of(g2.my_id()) == "bob"); // a rejoin keeps its name (BUG-0141: not "bob 2")
    printf("  %s", host.status().c_str());
}

// BUG-0141: two guests with the default name were one player to the Space tables keyed by name.
// The host makes names unique; each guest takes its name from the roster. Run with the fix off as
// well: the same checks must see the shared names (the test catches the bug).
static int unique_names_case(bool fix)
{
    Session host, a, b;
    host.unique_names = a.unique_names = b.unique_names = fix;
    std::string err;
    int bad = 0;
    if (!host.host(0, "Player", err) || !a.join("127.0.0.1:" + std::to_string(host.port()), "Player", err))
        return -1;
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 5; i++) {
            host.update();
            a.update();
            b.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    run(300);
    if (!b.join("127.0.0.1:" + std::to_string(host.port()), "Player", err))
        return -1;
    run(400);
    if (!a.connected() || !b.connected())
        return -1;
    auto names = [](const Session& s) {
        std::vector<std::string> v;
        for (auto& p : s.players())
            v.push_back(s.name_of(p.id));
        std::sort(v.begin(), v.end());
        return v;
    };
    std::vector<std::string> want = {"Player", "Player 2", "Player 3"};
    bad += names(host) != want;
    bad += names(a) != want;
    bad += names(b) != want;
    bad += a.name_of(a.my_id()) != host.name_of(a.my_id()); // the guest knows the name the others see
    bad += b.name_of(b.my_id()) != host.name_of(b.my_id());
    return bad;
}

static void test_unique_names()
{
    printf("equal player names are made unique by the host (BUG-0141)\n");
    int with = unique_names_case(true), without = unique_names_case(false);
    printf("  with the fix: %d problem(s); without: %d\n", with, without);
    CHECK(with == 0);
    CHECK(without > 0);
}

// the host's player state and every chunk of a big snapshot (a cell pool of 600 cells = 43
// chunks of ~900 bytes, more than the 32 packets of one flush) reach the guest in the same frame
static void test_big_snapshot()
{
    printf("player state and a 43-chunk snapshot in one frame\n");
    Session host, cli;
    host.build = cli.build = 0x922af7c7;
    std::string err;
    CHECK(host.host(0, "host-alice", err));
    CHECK(cli.join("127.0.0.1:" + std::to_string(host.port()), "guest-bob", err));
    pump(host, cli, 300);
    CHECK(cli.my_id() == 1);
    int chunks = 0, states = 0;
    cli.cb.player_state = [&](const PlayerState& st) { if (st.id == 0) states++; };
    cli.cb.custom = [&](uint8_t type, uint8_t, Reader&) { if (type == 33) chunks++; };
    for (int round = 0; round < 3; round++) {
        for (int k = 0; k < 43; k++) {
            Writer w;
            for (int i = 0; i < 887; i++)
                w.u8((uint8_t)k);
            host.send_unreliable_to(1, 33, w);
        }
        PlayerState s;
        host.send_state(s);
        host.update(); // one frame: one flush
        pump(host, cli, 40);
    }
    CHECK(states == 3);
    CHECK(chunks == 3 * 43);
}

static void test_interp()
{
    printf("snapshot interpolation\n");
    SnapshotBuffer b;
    b.delay = 0.1;
    for (int i = 0; i <= 10; i++) {
        PlayerState s;
        s.time_ms = 1000 + i * 50;
        s.pos = {(float)i, 0, 0};
        s.vel = {20, 0, 0};
        b.push(s, 5.0 + i * 0.05); // local clock offset 4s
    }
    PlayerState o;
    CHECK(b.sample(5.0 + 0.5, o)); // newest local recv time -> render 0.1 s behind -> x=8
    CHECK(std::fabs(o.pos.x - 8.0f) < 0.01f);
    CHECK(b.sample(5.0 + 0.6, o)); // extrapolate 0 s past newest: x=10
    CHECK(std::fabs(o.pos.x - 10.0f) < 0.01f);
    CHECK(b.sample(5.0 + 2.0, o)); // capped at 0.25 s -> x = 10 + 5
    CHECK(std::fabs(o.pos.x - 15.0f) < 0.01f);
}

// a player coming back into the world (from the editor, a load): the snapshots before it have no
// position (0,0,0, PF_IN_WORLD clear) or another mode. Blending from them put the puppet (and the
// home of the herd made for it) inside the planet (crash 0xB88BEA, docs/re/creature.md 10)
static void test_interp_gap()
{
    printf("snapshot interpolation across a gap\n");
    for (int kind = 0; kind < 3; kind++) {
        SnapshotBuffer b;
        b.delay = 0.1;
        for (int i = 0; i <= 10; i++) {
            PlayerState s;
            s.time_ms = 1000 + i * 50;
            s.mode = 0x1654C01;
            s.world = 7;
            bool in = i >= 5;
            if (in) {
                s.flags = PF_IN_WORLD;
                s.pos = {300.f, 300.f, 300.f};
            } else if (kind == 1) { // another mode (the editor) with some position of its own
                s.mode = 0xDBDBA1;
                s.flags = PF_IN_WORLD;
                s.pos = {1.f, 2.f, 3.f};
            } else if (kind == 2) { // another world
                s.world = 8;
                s.flags = PF_IN_WORLD;
                s.pos = {-300.f, 0.f, 0.f};
            }
            b.push(s, 5.0 + i * 0.05);
        }
        PlayerState o;
        // render time between snapshot 4 (out of the world) and 5 (in it)
        CHECK(b.sample(5.0 + 0.5 - 0.27 + 0.1, o));
        CHECK(o.flags & PF_IN_WORLD);
        CHECK(std::fabs(o.pos.x - 300.f) < 0.01f && std::fabs(o.pos.z - 300.f) < 0.01f);
    }
}

static void test_discovery()
{
    Session host;
    std::string err;
    CHECK(host.host(kDefaultPort + kDiscoverPorts - 1, "LanHost", err));
    host.lobby_info = "Creature - Testworld";
    Discovery d;
    CHECK(d.start());
    d.targets = {0x0100007F}; // 127.0.0.1
    d.interval_ms = 100;
    std::vector<Discovery::Game> found;
    for (int i = 0; i < 200 && found.empty(); i++) {
        d.update(now_ms());
        host.update();
        d.update(now_ms());
        found = d.games(now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(found.size() == 1);
    if (!found.empty()) {
        CHECK(found[0].info.name == "LanHost");
        CHECK(found[0].info.info == "Creature - Testworld");
        CHECK(found[0].info.players == 1);
        CHECK(found[0].host.port == kDefaultPort + kDiscoverPorts - 1);
    }
    printf("discovery ok\n");
}

static void test_ackbits()
{
    printf("128-packet ack window\n");
    AckBits b;
    b.set(0);
    b.set(63);
    b.set(64);
    b.set(127);
    CHECK(b.test(0) && b.test(63) && b.test(64) && b.test(127) && !b.test(1) && !b.test(128));
    b.shift(1);
    CHECK(b.test(1) && b.test(64) && b.test(65) && !b.test(0) && !b.test(127));
    b.shift(64);
    CHECK(b.test(65) && !b.test(1));
    b.shift(200);
    CHECK(!b.lo && !b.hi);
}

// a big reliable transfer (the world sent to a guest) in bursts of a whole ack window, with loss
static void test_bulk()
{
    printf("bulk transfer: 4 MB in bursts of %u packets, 5%% loss\n", kAckWindow);
    Connection a, b;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(0, 1);
    const int N = 4000; // 1 KB messages
    uint32_t now = 1000;
    int queued = 0, got = 0, frames = 0;
    bool ordered = true;
    std::vector<uint8_t> pkt;
    a.rtt_ms = b.rtt_ms = 5;
    while (got < N && frames < 3000) {
        frames++;
        now += 33; // one game frame
        while (queued < N && a.pending_reliable() < 600) {
            Writer w;
            w.u32(queued++);
            std::vector<uint8_t> pad(1020, 0x55);
            w.raw(pad.data(), pad.size());
            a.send_reliable(42, w);
        }
        std::vector<std::vector<uint8_t>> a2b, b2a;
        for (unsigned i = 0; i < kAckWindow && a.build_packet(now, pkt); i++)
            if (U(rng) > 0.05f) a2b.push_back(pkt);
        for (auto& p : a2b)
            b.on_packet(p.data(), p.size(), now, [&](uint8_t type, Reader& r) {
                if (type == 42 && r.u32() != (uint32_t)got++) ordered = false;
            });
        while (b.build_packet(now + 1, pkt))
            if (U(rng) > 0.05f) b2a.push_back(pkt);
        for (auto& p : b2a)
            a.on_packet(p.data(), p.size(), now + 2, [](uint8_t, Reader&) {});
    }
    double mbps = got * 1024.0 / (frames * 0.033) / 1e6;
    printf("  %d/%d in %d frames (%.1f s at 30 fps: %.1f MB/s), resends=%u\n", got, N, frames, frames * 0.033, mbps,
           a.stats.resends);
    CHECK(got == N && ordered);
    CHECK(mbps > 2.5); // the old 32-packet window gave ~1.1 MB/s
}

// the world over a very bad link (netem "awful": 250 +- 120 ms each way, 10 % loss, 2 % duplicates)
// a message lost several times in a row let the receiver buffer 1024 later ones; it then dropped new
// messages while still acknowledging their packets, so they were never sent again and the transfer
// stalled for good (a guest rejoin over SPORE_NET=awful never completed)
static void test_bulk_bad_link()
{
    printf("bulk transfer over 250 +- 120 ms, 10%% loss, 2%% duplicates\n");
    struct Pk {
        uint32_t t;
        std::vector<uint8_t> d;
        bool operator<(const Pk& o) const { return t > o.t; }
    };
    for (int run = 0; run < 4; run++) {
        size_t window = run < 2 ? 160 : 600; // world.cpp: 160 on the internet, 600 on a LAN
        Connection a, b;
        std::mt19937 rng(run + 1);
        std::uniform_real_distribution<float> U(0, 1);
        std::vector<Pk> ab, ba; // heaps by arrival time
        // and one message (#100) lost every time for 4 s: the receiver buffers everything after it
        const uint8_t hole[8] = {100, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
        uint32_t hole_until = 0;
        auto send = [&](std::vector<Pk>& q, const std::vector<uint8_t>& p, uint32_t now) {
            if (&q == &ab && std::search(p.begin(), p.end(), hole, hole + 8) != p.end()) {
                if (!hole_until)
                    hole_until = now + 4000;
                if (now < hole_until)
                    return;
            }
            if (U(rng) < 0.10f)
                return;
            for (int k = U(rng) < 0.02f ? 2 : 1; k > 0; k--) {
                q.push_back({now + (uint32_t)(250 + (U(rng) * 2 - 1) * 120), p});
                std::push_heap(q.begin(), q.end());
            }
        };
        const int N = 3300; // a tribe save: ~3.3 MB in 1 KB chunks
        int queued = 0, got = 0;
        bool ordered = true;
        uint32_t now = 1000;
        std::vector<uint8_t> pkt;
        for (; got < N && now < 1000 + 120000; now += 33) {
            while (queued < N && a.pending_reliable() < window) {
                Writer w;
                w.u32(queued++);
                std::vector<uint8_t> pad(1010, 0x55);
                w.raw(pad.data(), pad.size());
                a.send_reliable(42, w);
            }
            for (unsigned i = 0; i < kAckWindow / 3 && a.build_packet(now, pkt); i++)
                send(ab, pkt, now);
            for (; !ab.empty() && ab.front().t <= now; ab.pop_back()) {
                std::pop_heap(ab.begin(), ab.end());
                auto& p = ab.back();
                b.on_packet(p.d.data(), p.d.size(), now, [&](uint8_t type, Reader& r) {
                    if (type == 42 && r.u32() != (uint32_t)got++)
                        ordered = false;
                });
            }
            while (b.build_packet(now, pkt))
                send(ba, pkt, now);
            for (; !ba.empty() && ba.front().t <= now; ba.pop_back()) {
                std::pop_heap(ba.begin(), ba.end());
                auto& p = ba.back();
                a.on_packet(p.d.data(), p.d.size(), now, [](uint8_t, Reader&) {});
            }
        }
        printf("  window %zu, seed %d: %d/%d in %.1f s, resends=%u rtt=%.0f\n", window, run + 1, got, N, (now - 1000) / 1000.0,
               a.stats.resends, a.rtt_ms);
        CHECK(got == N && ordered);
        CHECK(now - 1000 < 45000);
    }
}

// creations arrive from other players: the RefPack decoder must survive any input
static void test_refpack_untrusted()
{
    printf("RefPack decoder on hostile input\n");
    std::mt19937 rng(11);
    std::vector<uint8_t> out;
    int ok = 0;
    for (int k = 0; k < 2000; k++) {
        std::vector<uint8_t> in(5 + rng() % 600);
        for (auto& b : in)
            b = (uint8_t)rng();
        in[0] = 0x10;
        in[1] = 0xFB;
        in[2] = 0;
        in[3] = (uint8_t)(rng() % 8);
        in[4] = (uint8_t)rng(); // declared size < 2 KB
        uint32_t size = (uint32_t)in[3] << 8 | in[4];
        if (dbpf::refpack_decode(in.data(), in.size(), out))
            ok++;
        CHECK(out.size() <= size);
    }
    // a "bomb": a tiny literal then long back-references, far more than the declared 16 bytes
    std::vector<uint8_t> bomb = {0x10, 0xFB, 0, 0, 16, 0xE0 /* 4 literal bytes */, 'a', 'b', 'c', 'd'};
    for (int i = 0; i < 4000; i++) { bomb.push_back(0xDC); bomb.push_back(0); bomb.push_back(0); bomb.push_back(0xFF); }
    CHECK(!dbpf::refpack_decode(bomb.data(), bomb.size(), out));
    CHECK(out.size() <= 16);
    printf("  2000 random inputs (%d decoded), bomb rejected\n", ok);
}

// the updater: packs, paths, versions, the GitHub answer (updater/pack.h)
// BUG-0089: a pack that lists a file twice (also differing only in case: the same file on
// Windows) is rejected; apply_pack's second entry used to delete the first one's backup of the
// original and the rollback lost it. `net_tests --old` runs with the fix off (must fail here).
static void test_updater_dup()
{
    printf("updater: a file listed twice\n");
    upd::Pack dup, q;
    std::string err;
    dup.version = "1";
    dup.files.push_back({"SporebinEP1/mods/coop.dll", 0, {1}});
    dup.files.push_back({"SporebinEP1/mods/coop.dll", 0, {2}});
    CHECK(!upd::parse_pack(upd::write_pack(dup), q, err));
    dup.files[1].path = "SporebinEP1/mods/COOP.dll";
    CHECK(!upd::parse_pack(upd::write_pack(dup), q, err));
    dup.files[1].path = "SporebinEP1/mods/sporekit.dll";
    CHECK(upd::parse_pack(upd::write_pack(dup), q, err) && q.files.size() == 2);
}

// BUG-0242: names Windows treats as one file ("coop.dll" and "coop.dll.") passed the duplicate
// check; device names and the updater's own working names were allowed as pack paths
static void test_updater_paths()
{
    printf("updater: paths that are the same file on Windows, device and working names (BUG-0242)\n");
    upd::Pack p, q;
    std::string err;
    p.version = "9";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'A'}});
    p.files.push_back({"SporebinEP1/mods/coop.dll.", 0, {'B'}});
    p.files.push_back({"SporebinEP1/mods/sporekit/symbols/x.sym", 0, {'C'}});
    p.files.push_back({"SporebinEP1/mods/sporekit./symbols/x.sym", 0, {'D'}});
    bool parsed = upd::parse_pack(upd::write_pack(p), q, err);
    printf("  trailing-dot pack: parse_pack=%d (%s)\n", parsed, err.c_str());
    CHECK(!parsed);
    for (const char* bad : {"SporebinEP1/mods/coop.dll.", "SporebinEP1/mods/sporekit./symbols/x.sym",
                            "SporebinEP1/mods/nul", "SporebinEP1/mods/con.dll", "SporebinEP1/mods/COM1.ini",
                            "SporebinEP1/mods/coop.dll.new", "SporebinEP1/mods/coop.dll.sporecoop-old",
                            "SporebinEP1/mods/coop.dll.old", "SporebinEP1/mods/coop.ini.sporecoop-tmp",
                            "SporebinEP1/mods/sporecoop.pending"}) {
        bool a = upd::allowed_path(bad);
        if (a)
            printf("  allowed: %s\n", bad);
        CHECK(!a);
    }
    // every path of the real release packs (tools/mkpack.py) still passes
    std::vector<std::string> real = {"SporebinEP1/steam_api.dll", "SporebinEP1/sporekit.dll", "SporebinEP1/mods/coop.dll",
                                     "Data/SporeCoop.package", "SporebinEP1/mods/sporecoop.version",
                                     "SporebinEP1/mods/sporekit.ini", "SporebinEP1/mods/coop.ini"};
    for (const char* sym : {"gog2016_922af7c7.sym", "march2017_dc04aee5.sym", "overrides_922af7c7.sym",
                            "overrides_dc04aee5.sym"})
        real.push_back(std::string("SporebinEP1/mods/sporekit/symbols/") + sym);
    upd::Pack rp;
    rp.version = "9";
    for (auto& r : real) {
        CHECK(upd::allowed_path(r));
        rp.files.push_back({r, 0, {'x'}});
    }
    CHECK(upd::parse_pack(upd::write_pack(rp), q, err) && q.files.size() == real.size());
    CHECK(upd::canonical_path("SporebinEP1/mods/COOP.dll") == "sporebinep1/mods/coop.dll");
}

static void test_updater()
{
    printf("updater: packs, paths, versions, release answer\n");
    upd::Pack p;
    p.version = "0.5.1";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {1, 2, 3}});
    p.files.push_back({"SporebinEP1/mods/coop.ini", upd::kOnlyIfMissing, {'a'}});
    auto bytes = upd::write_pack(p);
    upd::Pack q;
    std::string err;
    CHECK(upd::parse_pack(bytes, q, err) && q.version == "0.5.1" && q.files.size() == 2 && q.files[0].data.size() == 3);
    auto bad = bytes;
    bad.back() ^= 1; // a damaged file
    CHECK(!upd::parse_pack(bad, q, err));
    bad = bytes;
    bad.resize(bad.size() - 1); // truncated
    CHECK(!upd::parse_pack(bad, q, err));
    for (const char* evil : {"../steam_api.dll", "SporebinEP1/../../x.dll", "C:/Windows/x.dll", "/etc/x",
                             "SporebinEP1\\mods\\x.dll", "SporebinEP1/SporeApp.exe", "SporebinEP1/mods/../x.dll",
                             "SporebinEP1/mods/a:b.dll", "SporebinEP1/mods//x.dll"})
        CHECK(!upd::allowed_path(evil));
    for (const char* ok : {"SporebinEP1/steam_api.dll", "SporebinEP1/sporekit.dll", "SporebinEP1/mods/coop.dll",
                           "SporebinEP1/mods/sporekit/symbols/gog2016_922af7c7.sym", "Data/SporeCoop.package"})
        CHECK(upd::allowed_path(ok));
    upd::Pack evil;
    evil.version = "9";
    evil.files.push_back({"SporebinEP1/SporeApp.exe", 0, {0}});
    CHECK(!upd::parse_pack(upd::write_pack(evil), q, err));
    CHECK(upd::compare_versions("0.5.1", "0.5.0") == 1 && upd::compare_versions("v0.5.0", "0.5.0") == 0 &&
          upd::compare_versions("0.10.0", "0.9.9") == 1 && upd::compare_versions("1.0", "1.0.1") == -1);
    CHECK(upd::compare_versions("0.7.2.1", "0.7.2") == 1 && upd::compare_versions("0.7.3", "0.7.2.1") == 1 &&
          upd::compare_versions("v0.7.2.1", "0.7.2.1") == 0);
    const char* json = R"({"html_url":"https://github.com/WoodyWizard/SporeCoop/releases/tag/v0.5.1","tag_name":"v0.5.1",
      "assets":[{"name":"SporeCoop-0.5.1.zip","browser_download_url":"https://github.com/WoodyWizard/SporeCoop/releases/download/v0.5.1/SporeCoop-0.5.1.zip"},
      {"name":"SporeCoop-0.5.1.pack","browser_download_url":"https://github.com/WoodyWizard/SporeCoop/releases/download/v0.5.1/SporeCoop-0.5.1.pack"},
      {"name":"evil.pack","browser_download_url":"https://evil.example/x.pack"},
      {"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/WoodyWizard/SporeCoop/releases/download/v0.5.1/SHA256SUMS.txt"}]})";
    upd::Release r;
    CHECK(upd::parse_release(json, r) && r.version == "0.5.1" && r.tag == "v0.5.1");
    CHECK(r.pack_url.find("github.com/WoodyWizard") != std::string::npos && r.sums_url.size() > 0);
    std::string h(64, 'a');
    CHECK(upd::sums_lookup(h + "  SporeCoop-0.5.1.pack\r\n" + std::string(64, 'b') + " *x.zip\n", "SporeCoop-0.5.1.pack") == h);
    CHECK(upd::sums_lookup(h + "  other.pack\n", "SporeCoop-0.5.1.pack").empty());
}

// A Session host for the fakepeer interoperability test (no game required).
static int fakepeer_host()
{
    if (!net_init()) return 1;
    Session host;
    host.build = 0xdc04aee5;
    host.world = 0x12345678;
    std::string err;
    if (!host.host(0, "Host", err)) {
        fprintf(stderr, "host: %s\n", err.c_str());
        return 1;
    }
    bool joined = false, state = false, chat = false, left = false;
    host.cb.player_join = [&](const PlayerInfo& p) { joined = p.id == 1 && p.name == "Bot"; };
    host.cb.player_state = [&](const PlayerState& s) {
        if (s.id == 1 && (s.flags & PF_IN_WORLD) && s.mode == 0x1654c01 && s.world == host.world &&
            s.health == 10 && s.max_health == 10)
            state = true;
    };
    host.cb.chat = [&](uint8_t id, const std::string& text) { chat = id == 1 && text == "fakepeer test"; };
    host.cb.player_leave = [&](uint8_t id, uint8_t reason) { left = id == 1 && reason == 0; };
    printf("%u\n", host.port());
    fflush(stdout);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline && !left) {
        host.update();
        PlayerState s;
        s.pos = {100, 0, 0};
        s.flags = PF_IN_WORLD;
        s.mode = 0x1654c01;
        s.world = host.world;
        host.send_state(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!joined || !state || !chat || !left) {
        fprintf(stderr, "fakepeer: joined=%d state=%d chat=%d left=%d\n", joined, state, chat, left);
        return 1;
    }
    return 0;
}

// `net_tests --nofix` (or a build with -DSPORECOOP_TEST_NOFIX=1) runs the logic before the fixes
#ifndef SPORECOOP_TEST_NOFIX
#define SPORECOOP_TEST_NOFIX 0
#endif

static void test_lobby_roles()
{
    printf("lobby roles keep their ids after an id was reused (BUG-0079)\n");
    const bool kFix = !SPORECOOP_TEST_NOFIX && !g_nofix;
    // guests 1 and 2 joined, 1 left, a newcomer got id 1: the host's list is in join order 0, 2, 1
    std::vector<LobbyEntry> pl(3);
    pl[0].id = 0; pl[0].role = 3;
    pl[1].id = 2; pl[1].role = 1; pl[1].ready = true; pl[1].rtt = 40;
    pl[2].id = 1; pl[2].role = 0; pl[2].rtt = 70;
    auto role_of = [](const std::vector<LobbyEntry>& t, uint8_t id) {
        for (auto& e : t)
            if (e.id == id)
                return (int)e.role;
        return -1;
    };
    for (int wfix = 0; wfix < 2; wfix++)
        for (int rfix = 0; rfix < 2; rfix++) {
            if (!kFix && (wfix || rfix))
                continue;
            if (kFix && !wfix && !rfix)
                continue; // old host and old guest: the bug itself
            Writer w;
            write_lobby(w, pl, wfix);
            Reader r(w.data.data(), w.size());
            std::vector<LobbyEntry> t;
            bool has_roles = false;
            CHECK(read_lobby(r, t, has_roles, rfix));
            CHECK(has_roles && t.size() == 3);
            CHECK(role_of(t, 2) == 1);
            CHECK(role_of(t, 1) == 0);
            CHECK(role_of(t, 0) == 3);
            for (auto& e : t)
                if (e.id == 2)
                    CHECK(e.ready && e.rtt == 40);
        }
    // an older host without roles
    Writer w;
    w.u8(1); w.u8(0); w.u8(1); w.u16(0);
    Reader r(w.data.data(), w.size());
    std::vector<LobbyEntry> t;
    bool has_roles = true;
    CHECK(read_lobby(r, t, has_roles, true) && !has_roles && t.size() == 1 && t[0].ready);
}

static void test_shared_ledger()
{
    printf("shared resources: a crossing host total does not undo a spend (BUG-0106)\n");
    const bool kFix = !SPORECOOP_TEST_NOFIX && !g_nofix;
    SharedLedger g;
    g.pending = -100; // the guest buys for 100 out of 150
    uint32_t s1 = g.send();
    // a host total that left before the host counted the spend (it echoes no count of ours yet)
    double v = g.shown(150, kFix, s1 - 1);
    CHECK(std::fabs(v - 50) < 1e-9); // before the fix: 150, the money jumped back up
    // the host counted it
    v = g.shown(50, kFix, s1);
    CHECK(std::fabs(v - 50) < 1e-9);
    CHECK(g.in_flight.empty());
    // two spends in flight, the host counted the first one only
    g.pending = -10;
    uint32_t a = g.send();
    g.pending = -20;
    g.send();
    g.pending = -5; // not sent yet
    v = g.shown(40, kFix, a);
    CHECK(std::fabs(v - 15) < 1e-9);
    // an older host that echoes nothing: everything sent counts as counted (the old behaviour)
    v = g.shown(20, false, 0);
    CHECK(std::fabs(v - 15) < 1e-9 && g.in_flight.empty());
    // both players spend 100 out of 150 at once: the host floors the total at 0
    CHECK(shared_add(50, -100, kFix) == 0);
    CHECK(shared_add(150, -100, kFix) == 50);
}

// BUG-0220: a guest's spend in the window between another player's join and the next host total
// was undone on its screen and never reached the host
static void test_shared_join_keep()
{
    printf("shared resources: a guest's spend at another player's join counts once (BUG-0220)\n");
    const bool kFix = !SPORECOOP_TEST_NOFIX && !g_nofix;
    CHECK(reset_on_join(true, kFix));                // the host resends its totals to the newcomer
    CHECK(reset_on_join(false, kFix) == !kFix);      // a guest keeps its state
    // a guest that shows the host total 1000 (have, synced 1000); a third player joins
    SharedLedger g;
    bool have = !reset_on_join(false, kFix);
    double synced = 1000, game = 1000;
    game -= 500; // the guest buys for 500 in the window
    // its tick: samples the change only while it has a host total
    if (have)
        g.fold(game, synced);
    // the host's next total (it counted nothing of ours yet) arrives
    double shown = g.shown(1000, true, g.next_seq - 1);
    CHECK(std::fabs(shown - 500) < 1e-9); // before the fix: 1000, the purchase came back
    CHECK(std::fabs(g.pending + 500) < 1e-9); // reported to the host once
    // the fold right at the total does not count a sampled change twice
    SharedLedger h;
    double s2 = 1000;
    h.fold(800, s2);  // tick: -200
    h.fold(800, s2);  // the total in the same frame: nothing new
    CHECK(std::fabs(h.pending + 200) < 1e-9 && s2 == 800);
    // a change made after the tick, folded when the total arrives
    h.fold(750, s2);
    CHECK(std::fabs(h.pending + 250) < 1e-9);
}

// ---- wave 2 netcode fixes (each FAILS with `net_tests --nofix`, the code before the fix) ----

// BUG-0034: a reliable message bigger than one packet (a long `coop say`, the species table of a
// rich planet) was never sent, blocked every reliable message after it and made every flush send a
// full burst of empty packets
static void test_big_reliable()
{
    printf("reliable message bigger than a packet (BUG-0034)\n");
    for (float loss : {0.f, 0.2f}) {
        Connection a, b;
        a.peer_split = b.peer_split = true; // both announced CAP_SPLIT (BUG-0219)
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> U(0, 1);
        uint32_t now = 1000;
        Writer big, huge, small;
        for (int i = 0; i < 1180; i++)
            big.u8((uint8_t)i);
        for (int i = 0; i < 30000; i++) // 26 pieces
            huge.u8((uint8_t)(i * 7));
        small.u32(5);
        a.send_reliable(MSG_CHAT, big);
        a.send_reliable(MSG_SPECIES, huge);
        a.send_reliable(MSG_NOTICE, small);
        int got_small = 0, got_big = 0, got_huge = 0, other = 0, pkts = 0;
        std::vector<uint8_t> pkt;
        for (int step = 0; step < 300; step++) { // 3 s of 10 ms frames
            now += 10;
            for (int i = 0; i < 128 && a.build_packet(now, pkt); i++) {
                pkts++;
                if (U(rng) < loss)
                    continue;
                b.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader& r) {
                    if (t == MSG_NOTICE && r.left() == 4 && r.u32() == 5)
                        got_small++;
                    else if (t == MSG_CHAT && r.left() == 1180) {
                        bool same = true;
                        for (int i = 0; i < 1180; i++)
                            same &= r.u8() == (uint8_t)i;
                        got_big += same;
                    } else if (t == MSG_SPECIES && r.left() == 30000) {
                        bool same = true;
                        for (int i = 0; i < 30000; i++)
                            same &= r.u8() == (uint8_t)(i * 7);
                        got_huge += same;
                    } else
                        other++;
                });
            }
            for (int i = 0; i < 128 && b.build_packet(now, pkt); i++)
                if (U(rng) >= loss)
                    a.on_packet(pkt.data(), pkt.size(), now, [](uint8_t, Reader&) {});
        }
        printf("  loss %.0f%%: small=%d big=%d huge=%d other=%d packets=%d pending=%zu\n", loss * 100, got_small,
               got_big, got_huge, other, pkts, a.pending_reliable());
        CHECK(got_small == 1 && got_big == 1 && got_huge == 1 && other == 0);
        CHECK(a.pending_reliable() == 0);
        CHECK(pkts < 1000); // 300 frames: about one keepalive / ack each, not 128 empty packets
    }
}

// ---- wave 5 (each FAILS with its switch off: `net_tests --off <name>`) ----

// what a 0.7.2 receiver makes of a packet: it tests only flag bit 1 and delivers every reliable
// message as it is, pieces (flag bit 2) included
struct OldReceiver {
    std::vector<std::pair<uint8_t, size_t>> got; // type, length of each delivered message
    int pieces = 0;
    void on_packet(const std::vector<uint8_t>& pkt)
    {
        Reader r(pkt.data(), pkt.size());
        r.skip(4 + 2 + 4 + 2 + 2 + 16);
        uint8_t count = r.u8();
        for (int i = 0; i < count && r.ok(); i++) {
            uint8_t type = r.u8(), flags = r.u8();
            uint16_t len = r.u16();
            if (flags & 1)
                r.u16();
            r.skip(len);
            if (!r.ok())
                break;
            if (flags & 2)
                pieces++;
            if (flags & 1)
                got.push_back({type, len});
        }
    }
};

// BUG-0219: 0.7.3.0 split a reliable message above one packet for any peer; a 0.7.2 peer read every
// piece as a whole message (a long chat line lost, a species table of 42-83 records read as empty)
static void test_old_peer_split()
{
    printf("no split reliable messages to a peer without CAP_SPLIT (BUG-0219)\n");
    Connection a; // peer_split stays false: the peer announced nothing
    uint32_t now = 1000;
    Writer chat, species, small;
    for (int i = 0; i < 3000; i++)
        chat.u8('x');
    species.u16(50);
    for (int i = 0; i < 50 * 28; i++)
        species.u8(i % 28 == 12 ? 1 : 0); // relation 1: the bytes 0.7.2 read as the count n = 0
    small.u32(5);
    a.send_reliable(MSG_CHAT, chat);
    a.send_reliable(MSG_SPECIES, species);
    a.send_reliable(MSG_NOTICE, small);
    OldReceiver old;
    std::vector<uint8_t> pkt;
    for (int step = 0; step < 20; step++) {
        now += 10;
        for (int i = 0; i < 128 && a.build_packet(now, pkt); i++)
            old.on_packet(pkt);
    }
    int species_n = 0, chat_n = 0;
    for (auto& g : old.got) {
        species_n += g.first == MSG_SPECIES;
        chat_n += g.first == MSG_CHAT;
    }
    printf("  old receiver: %zu messages, %d pieces (chat %d, species %d), too_big_old=%u\n", old.got.size(),
           old.pieces, chat_n, species_n, a.stats.too_big_old);
    CHECK(old.pieces == 0);
    CHECK(chat_n == 0 && species_n == 0); // not sent at all (Session cuts chat lines itself)
    CHECK(!old.got.empty() && old.got.back().first == MSG_NOTICE);
    CHECK(a.stats.too_big_old == 2);

    // the receiving side: pieces from a peer that never announced splitting are dropped, not joined
    Connection s, r; // s splits (peer_split set by mistake), r was told nothing
    s.peer_split = true;
    s.send_reliable(MSG_CHAT, chat);
    s.send_reliable(MSG_NOTICE, small);
    int whole = 0, notice = 0;
    for (int step = 0; step < 20; step++) {
        now += 10;
        for (int i = 0; i < 128 && s.build_packet(now, pkt); i++)
            r.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader& m) {
                whole += t == MSG_CHAT;
                notice += t == MSG_NOTICE && m.left() == 4;
            });
        for (int i = 0; i < 128 && r.build_packet(now, pkt); i++)
            s.on_packet(pkt.data(), pkt.size(), now, [](uint8_t, Reader&) {});
    }
    CHECK(whole == 0 && notice == 1 && r.stats.frag_lost == 1);
}

// BUG-0219 in a session: a guest whose HELLO has no extension (0.7.2) gets long chat lines cut
// between two characters (from the host and relayed), no species table, and is announced once
static void test_old_peer_session()
{
    printf("a 0.7.2-shaped guest: long chat cut, big messages not sent, one notice (BUG-0219)\n");
    Session host, oldg, newg;
    host.build = oldg.build = newg.build = 0x922af7c7;
    oldg.send_caps = false;
    std::string err;
    CHECK(host.host(0, "Host", err));
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    int notices = 0;
    host.cb.old_peer = [&](uint8_t id, const std::string&) { notices += id == 1; };
    std::vector<size_t> old_chat, new_chat;
    bool old_valid = true;
    int old_species = 0, new_species = 0;
    oldg.cb.chat = [&](uint8_t id, const std::string& t) {
        if (id != oldg.my_id()) {
            old_chat.push_back(t.size());
            old_valid &= utf8::valid(t);
        }
    };
    newg.cb.chat = [&](uint8_t id, const std::string& t) { if (id != newg.my_id()) new_chat.push_back(t.size()); };
    oldg.cb.custom = [&](uint8_t t, uint8_t, Reader&) { old_species += t == MSG_SPECIES; };
    newg.cb.custom = [&](uint8_t t, uint8_t, Reader&) { new_species += t == MSG_SPECIES; };
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 5; i++) {
            host.update();
            oldg.update();
            newg.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    CHECK(oldg.join(addr, "Old", err));
    run(300);
    CHECK(newg.join(addr, "New", err));
    run(300);
    CHECK(oldg.connected() && newg.connected());
    std::string longline;
    while (longline.size() < 3000)
        longline += "\xd0\x96"; // Cyrillic, two bytes: an odd cut would land inside a character
    host.send_chat(longline);
    Writer sp;
    sp.u16(50);
    for (int i = 0; i < 50 * 28; i++)
        sp.u8(0);
    host.broadcast_reliable(MSG_SPECIES, sp);
    newg.send_chat(longline); // relayed by the host
    run(400);
    host.send_chat("marker");
    run(300);
    printf("  old guest: chats");
    for (auto n : old_chat)
        printf(" %zu", n);
    printf(", species %d; new guest: chats", old_species);
    for (auto n : new_chat)
        printf(" %zu", n);
    printf(", species %d; notices %d\n", new_species, notices);
    CHECK(old_chat.size() == 3 && old_valid);
    if (old_chat.size() == 3) {
        CHECK(old_chat[0] <= Connection::kMaxSingle - 3 && old_chat[0] >= Connection::kMaxSingle - 5);
        CHECK(old_chat[1] <= Connection::kMaxSingle - 3 && old_chat[1] >= Connection::kMaxSingle - 5);
        CHECK(old_chat[2] == 6);
    }
    CHECK(old_species == 0);
    CHECK(new_chat.size() == 2 && !new_chat.empty() && new_chat[0] == longline.size());
    CHECK(new_species == 1);
    CHECK(notices == 1);
    CHECK(host.status().find("older peer 1: too_big_old=1") != std::string::npos);
}

// BUG-0235: the BUG-0209 rule (same name, silent 2 s) dropped a busy player (loading, saving) when
// another player of the same name joined, and a crash rejoin into a full session was refused
static void tick(std::vector<Session*> v, int ms)
{
    for (int i = 0; i < ms / 5; i++) {
        for (auto* x : v)
            if (x->role() != Role::None)
                x->update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// CAP_VERSION (0.7.4): every peer learns the mod / SporeKit / game versions of the others; an
// older peer (no extension) reads as "" and still joins; the old message shapes still read
static void test_peer_versions()
{
    printf("peer versions in HELLO / WELCOME / PLAYER_JOIN, an old-shape peer reads as unknown\n");
    // message level: a 0.7.3-shaped HELLO (caps without CAP_VERSION) and PLAYER_JOIN (id + name)
    {
        Hello h;
        h.caps = CAP_SPLIT;
        h.version = "coop 9.9";
        Writer w;
        h.write(w);
        Reader r(w.data.data(), w.size());
        Hello g;
        CHECK(g.read(r) && g.version.empty() && g.caps == CAP_SPLIT);
        Hello h2;
        h2.has_token = true;
        h2.token = 77;
        h2.version = "coop 1.2.3, SporeKit 0.3.4, GOG 2024 dc04aee5";
        Writer w2;
        h2.write(w2);
        Reader r2(w2.data.data(), w2.size());
        Hello g2;
        CHECK(g2.read(r2) && g2.token == 77 && g2.version == h2.version);
        Writer wj;
        wj.u8(3);
        wj.str8("Old");
        Reader rj(wj.data.data(), wj.size());
        PlayerJoin pj;
        CHECK(pj.read(rj) && pj.p.id == 3 && pj.p.name == "Old" && pj.p.version.empty());
        Welcome wl;
        wl.your_id = 2;
        wl.players = {{0, "H", "vh"}, {1, "A", "va"}, {2, "B", ""}};
        wl.host_version = "vh";
        Writer ww;
        wl.write(ww);
        Reader rw(ww.data.data(), ww.size());
        Welcome got;
        CHECK(got.read(rw) && got.host_version == "vh" && got.players.size() == 3 && got.players[0].version == "vh" &&
              got.players[1].version == "va" && got.players[2].version.empty());
    }
    Session host, oldg, newg;
    host.build = oldg.build = newg.build = 0x922af7c7;
    host.version = "coop H";
    oldg.version = "coop O";
    newg.version = "coop N";
    oldg.send_caps = false;
    std::string err;
    CHECK(host.host(0, "Host", err));
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 5; i++) {
            host.update();
            oldg.update();
            newg.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    CHECK(oldg.join(addr, "Old", err));
    run(300);
    CHECK(newg.join(addr, "New", err));
    run(300);
    CHECK(oldg.connected() && newg.connected());
    auto ver = [](const Session& s, uint8_t id) {
        for (auto& p : s.players())
            if (p.id == id)
                return p.version;
        return std::string("?");
    };
    printf("  host sees old='%s' new='%s'; new sees host='%s' old='%s'; old sees host='%s'\n", ver(host, 1).c_str(),
           ver(host, 2).c_str(), ver(newg, 0).c_str(), ver(newg, 1).c_str(), ver(oldg, 0).c_str());
    CHECK(ver(host, 1).empty() && ver(host, 2) == "coop N");
    CHECK(ver(newg, 0) == "coop H" && ver(newg, 1).empty() && ver(newg, 2) == "coop N");
    CHECK(ver(oldg, 0) == "coop H"); // an old guest ignores the trailing bytes (here: reads them, it is this build)
    host.leave();
    oldg.leave();
    newg.leave();
}

// logs and reports never carry a full public address (net/ipmask.h)
static void test_ip_mask()
{
    printf("addresses in logs: loopback / LAN class kept, public hashed\n");
    auto ip = [](const char* s) { return (uint32_t)inet_addr(s); };
    CHECK(mask_ip(ip("127.0.0.1"), 47810) == "127.0.0.1:47810");
    CHECK(mask_ip(ip("192.168.1.20"), 47810) == "192.168.x.x:47810");
    CHECK(mask_ip(ip("10.1.2.3"), 0) == "10.x.x.x");
    std::string pub = mask_ip(ip("93.184.216.34"), 47810);
    CHECK(pub.rfind("public #", 0) == 0 && pub.find("93") == std::string::npos);
    CHECK(mask_ip(ip("93.184.216.34"), 1) != mask_ip(ip("93.184.216.35"), 1));
}

static void test_rejoin_token()
{
    printf("a rejoin is the same game by its token, not by name and silence (BUG-0235)\n");
    std::string err;
    { // a busy player keeps its entry; a different player of the same name is "Bob 2"
        Session h, b, x;
        b.rejoin_token = 0x1111;
        x.rejoin_token = 0x2222;
        int leaves = 0;
        h.cb.player_leave = [&](uint8_t, uint8_t) { leaves++; };
        int hchat = 0;
        h.cb.chat = [&](uint8_t, const std::string& t) { hchat += t == "back"; };
        CHECK(h.host(0, "Alice", err));
        std::string a = "127.0.0.1:" + std::to_string(h.port());
        CHECK(b.join(a, "Bob", err));
        tick({&h, &b}, 400);
        tick({&h}, 2500); // b's game is blocked: no updates, no packets
        CHECK(x.join(a, "Bob", err));
        tick({&h, &x}, 400);
        tick({&h, &b, &x}, 600); // b runs again
        b.send_chat("back");
        tick({&h, &b, &x}, 400);
        printf("  busy: leaves=%d host players=%zu x='%s' b connected=%d chat=%d\n", leaves, h.players().size(),
               x.name_of(x.my_id()).c_str(), b.connected(), hchat);
        CHECK(leaves == 0 && h.players().size() == 3);
        CHECK(x.name_of(x.my_id()) == "Bob 2" && b.name_of(b.my_id()) == "Bob");
        CHECK(b.connected() && hchat == 1);
    }
    { // a crash rejoin into 4/4 with the same token: accepted at once, same name, LEAVE before JOIN
        Session h, g1, g2, g3, g3b;
        g1.rejoin_token = 1;
        g2.rejoin_token = 2;
        g3.rejoin_token = g3b.rejoin_token = 3;
        CHECK(h.host(0, "Alice", err));
        std::string a = "127.0.0.1:" + std::to_string(h.port());
        CHECK(g1.join(a, "Bob", err));
        CHECK(g2.join(a, "Dave", err));
        CHECK(g3.join(a, "Carol", err));
        tick({&h, &g1, &g2, &g3}, 500);
        uint8_t old_id = g3.my_id();
        std::vector<std::string> ev;
        g1.cb.player_leave = [&](uint8_t id, uint8_t) { ev.push_back("L" + std::to_string(id)); };
        g1.cb.player_join = [&](const PlayerInfo& p) { ev.push_back("J" + std::to_string(p.id)); };
        std::vector<uint8_t> self_join;
        g3b.cb.player_join = [&](const PlayerInfo& p) { if (p.name == "Carol") self_join.push_back(p.id); };
        std::string why;
        g3b.cb.disconnected = [&](const std::string& w) { why = w; };
        tick({&h, &g1, &g2}, 2500); // g3's game died (no BYE)
        CHECK(g3b.join(a, "Carol", err));
        tick({&h, &g1, &g2, &g3b}, 600);
        std::string order;
        for (auto& e : ev)
            order += e + " ";
        printf("  full: rejoin connected=%d why='%s' name='%s' players=%zu g1 events: %s\n", g3b.connected(),
               why.c_str(), g3b.name_of(g3b.my_id()).c_str(), h.players().size(), order.c_str());
        CHECK(g3b.connected() && g3b.name_of(g3b.my_id()) == "Carol");
        CHECK(h.players().size() == 4 && g3b.players().size() == 4);
        CHECK(self_join.empty()); // its own old entry is not in its roster
        CHECK(ev.size() == 2 && ev[0] == "L" + std::to_string(old_id) && ev[1][0] == 'J');
    }
    { // edge cases: no extension (0.7.2), a live peer with the same token, core.rejoin_name off
        for (int kind = 0; kind < 3; kind++) {
            Session h, b, x;
            b.rejoin_token = x.rejoin_token = 7;
            if (kind == 0)
                b.send_caps = x.send_caps = false;
            if (kind == 2)
                h.rejoin_by_name = false;
            CHECK(h.host(0, "Alice", err));
            std::string a = "127.0.0.1:" + std::to_string(h.port());
            CHECK(b.join(a, "Bob", err));
            tick({&h, &b}, 400);
            if (kind != 1)
                tick({&h}, 2500); // silent (kind 1: b stays live)
            CHECK(x.join(a, "Bob", err));
            tick(kind == 1 ? std::vector<Session*>{&h, &b, &x} : std::vector<Session*>{&h, &x}, 400);
            printf("  edge %d: x='%s' host players=%zu\n", kind, x.name_of(x.my_id()).c_str(), h.players().size());
            CHECK(x.name_of(x.my_id()) == "Bob 2" && h.players().size() == 3);
        }
    }
}

// BUG-0236: the co-op window's own hint line, pasted whole, starts with a label
static void test_address_label()
{
    printf("a pasted \"Friends connect to: ...\" line joins its first address (BUG-0236)\n");
    struct Case {
        const char* in;
        uint16_t port;
    } cases[] = {{"Friends connect to: 192.168.1.5:47810,  10.0.0.2:47810", 47810},
                 {"\xd0\x94\xd1\x80\xd1\x83\xd0\xb7\xd1\x8c\xd1\x8f \xd0\xbf\xd0\xbe\xd0\xb4\xd0\xba\xd0\xbb\xd1\x8e"
                  "\xd1\x87\xd0\xb0\xd1\x8e\xd1\x82\xd1\x81\xd1\x8f \xd0\xba: 192.168.1.5:47810,  10.0.0.2:47810",
                  47810},
                 {"Bob: Friends connect to: 192.168.1.5:47811", 47811},
                 {"Friends connect to: 192.168.1.5:47810, ask me: ok", 47810}};
    for (auto& c : cases) {
        Endpoint e;
        std::string err, name;
        bool ok = Endpoint::parse_numeric(c.in, kDefaultPort, e, err, &name);
        bool good = ok && e.port == c.port && e.str().rfind("192.168.1.5:", 0) == 0;
        if (!good)
            printf("  '%s': ok=%d port=%u name='%s' err='%s'\n", c.in, ok, e.port, name.c_str(), err.c_str());
        CHECK(good);
    }
    // no host:port token: the first word goes to the name lookup as before
    Endpoint e;
    std::string err, name;
    CHECK(!Endpoint::parse_numeric("myhost.lan", kDefaultPort, e, err, &name) && name == "myhost.lan");
    CHECK(Endpoint::clean("  myhost.lan  ") == "myhost.lan");
}

// BUG-0109: with 3 players the host relays a leaving guest's last state unreliably, behind the
// frame's snapshot chunks: it reached the third player after the reliable PLAYER_LEAVE and made a
// ghost partner there for the rest of the session
static void test_state_after_leave()
{
    printf("a departed player's last state after PLAYER_LEAVE (BUG-0109)\n");
    Session host, b, c;
    host.build = b.build = c.build = 0x922af7c7;
    std::string err;
    CHECK(host.host(0, "host", err));
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 5; i++) {
            host.update();
            if (b.role() != Role::None)
                b.update();
            c.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    CHECK(b.join(addr, "bob", err));
    run(300);
    CHECK(c.join(addr, "carol", err));
    run(300);
    CHECK(b.my_id() == 1 && c.my_id() == 2);
    bool left = false;
    int ghost = 0;
    c.cb.player_leave = [&](uint8_t id, uint8_t) { left |= id == 1; };
    c.cb.player_state = [&](const PlayerState& st) { ghost += left && st.id == 1; };
    // the modules queued their snapshot chunks for c in the frame before (two ~990-byte chunks)
    Writer chunk;
    for (int i = 0; i < 990; i++)
        chunk.u8(0);
    host.send_unreliable_to(2, MSG_CELL_STATE, chunk);
    host.send_unreliable_to(2, MSG_CELL_STATE, chunk);
    PlayerState st;
    st.pos = {1, 2, 3};
    b.send_state(st); // b's last frame, then Leave: state and BYE reach the host in one update()
    b.leave("test");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    run(300);
    printf("  c: left=%d, states of the departed player after the leave=%d\n", (int)left, ghost);
    CHECK(left);
    CHECK(ghost == 0);
}

// BUG-0122: velocity of ship / vehicle mirrors over the sender's time, not the arrival time
static double velocity_error(int scenario, bool sender_time)
{
    const float S = 100.f; // u/s along x; truth at local time t is S*(t-1000)
    VelocityEstimate e;
    double maxerr = 0, recv[40];
    for (int k = 0; k < 40; k++)
        recv[k] = 1000 + k * 0.1 + 0.03;
    if (scenario == 0)
        recv[20] += 0.075; // one packet 75 ms late
    else {
        // two packets in one guest frame (20 is 100 ms late), the next one 60 ms late. (The
        // ticket's case put 22 25 ms after 21, i.e. 45 ms before it was sent on the truth clock:
        // no network does that, every packet here takes at least the 30 ms path.)
        recv[20] = recv[21];
        recv[22] += 0.06;
    }
    for (int k = 0; k < 40; k++) {
        double sent = k * 0.1;
        e.update({(float)(S * sent), 0, 0}, recv[k], sender_time ? 5000 + sent : -1); // own clock
        double nxt = k < 39 ? recv[k + 1] : recv[k] + 0.1;
        for (double t = recv[k]; t < nxt; t += 1 / 60.0) {
            double err = std::fabs(e.at(t).x - S * (t - 1000));
            if (k >= 5 && err > maxerr)
                maxerr = err;
        }
    }
    return maxerr;
}

static void test_velocity_jitter()
{
    printf("ship / vehicle velocity under jitter (BUG-0122)\n");
    double late_old = velocity_error(0, false), bunch_old = velocity_error(1, false);
    double late = velocity_error(0, true), bunch = velocity_error(1, true);
    printf("  max error: late packet %.1f (arrival time %.1f), bunched %.1f (arrival time %.1f)\n", late, late_old,
           bunch, bunch_old);
    CHECK(late < 5.0);
    CHECK(bunch < 5.0);
}

// BUG-0133: after the one-way latency rose by 300 ms the buffer rendered past its newest snapshot
// (puppets and NPCs in 20 Hz steps) for about 50 s
static void test_interp_latency_rise()
{
    printf("interpolation after the latency rises (BUG-0133)\n");
    SnapshotBuffer b; // delay 0.10 s
    double lat = 0.03, first_bad = -1, recovered = -1, worst_jump = 0;
    PlayerState prev_out;
    bool have_prev = false;
    for (int k = 0; k < 20 * 80; k++) { // 80 s of 20 Hz states
        double sent = 1000 + k * 0.05;
        if (sent > 1010)
            lat = 0.33;
        PlayerState s;
        s.time_ms = (uint32_t)(sent * 1000);
        s.pos = {(float)(k * 0.25), 0, 0};
        s.vel = {5, 0, 0};
        s.flags = PF_IN_WORLD;
        double now = sent + lat;
        b.push(s, now);
        bool extrap = b.age(now) > b.delay + 1e-9;
        if (sent > 1010 && extrap && first_bad < 0)
            first_bad = sent;
        if (first_bad > 0 && !extrap && recovered < 0)
            recovered = sent;
        // the drawn path never goes backwards (the render clock slows down, it does not jump back)
        for (int f = 0; f < 3; f++) {
            PlayerState o;
            b.sample(now + f / 60.0, o);
            if (have_prev && prev_out.pos.x - o.pos.x > worst_jump)
                worst_jump = prev_out.pos.x - o.pos.x;
            prev_out = o;
            have_prev = true;
        }
    }
    double took = recovered > 0 ? recovered - first_bad : 1e9;
    printf("  extrapolating from %.1f s, interpolating again %.1f s later, largest step back %.2f\n",
           first_bad - 1000, took, worst_jump);
    CHECK(took <= 5.0);
    CHECK(worst_jump < 0.5);
    // steady jitter keeps it interpolating (no drift past the newest snapshot)
    SnapshotBuffer j;
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> J(0, 0.04);
    int extr = 0, n = 0;
    for (int k = 0; k < 20 * 60; k++) {
        double sent = 2000 + k * 0.05, now = sent + 0.05 + J(rng);
        PlayerState s;
        s.time_ms = (uint32_t)(sent * 1000);
        j.push(s, now);
        if (k > 100) {
            n++;
            extr += j.age(now) > j.delay + 1e-9;
        }
    }
    printf("  jitter 0-40 ms: %d of %d samples past the newest snapshot\n", extr, n);
    CHECK(extr == 0);
}

// BUG-0154: net ids of host objects never repeat while in use and are never 0
static void test_alloc_id()
{
    printf("net id allocator (BUG-0154)\n");
    // --nofix: the inline `id = next++; if (!next) next = 1;` of squadron / civ / tribe before
    auto alloc = [](uint16_t& next, const std::map<uint16_t, int>& in_use) -> uint16_t {
        if (!g_nofix)
            return alloc_id(next, in_use);
        uint16_t id = next++;
        if (!next)
            next = 1;
        return id;
    };
    std::map<uint16_t, int> live{{1, 0}, {2, 0}};
    uint16_t next = 0xFFFF;
    uint16_t a = alloc(next, live);
    live[a] = 0;
    uint16_t b = alloc(next, live);
    printf("  after 0xFFFF with 1 and 2 alive: %u, then %u\n", a, b);
    CHECK(a == 0xFFFF);
    CHECK(b == 3);
    std::map<uint16_t, int> full;
    for (uint32_t i = 1; i <= 0xFFFF; i++)
        full[(uint16_t)i] = 0;
    CHECK(alloc(next, full) == 0);
}

// ---- wave 3 netcode fixes (each FAILS with `net_tests --nofix`) ----

// BUG-0030: a peer that has received nothing writes ack 0; the host's first packet (seq 0, the
// WELCOME) counted as acknowledged when it was lost, and the join timed out after 10 s
static void test_lost_first_packet()
{
    printf("the host's first packet is lost (BUG-0030)\n");
    Connection host, guest;
    host.conn_id = guest.conn_id = 77;
    uint32_t now = 1000;
    std::vector<uint8_t> pkt;
    Writer hello;
    hello.u32(1);
    guest.send_reliable(MSG_HELLO, hello);
    int got_welcome = 0;
    guest.build_packet(now, pkt);
    host.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader&) {
        if (t == MSG_HELLO) {
            Writer w;
            w.u8(1);
            host.send_reliable(MSG_WELCOME, w);
        }
    });
    CHECK(host.build_packet(now, pkt)); // the WELCOME packet: lost
    for (int step = 0; step < 300; step++) { // 3 s without any loss
        now += 10;
        while (guest.build_packet(now, pkt))
            host.on_packet(pkt.data(), pkt.size(), now, [](uint8_t, Reader&) {});
        while (host.build_packet(now, pkt))
            guest.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader&) { got_welcome += t == MSG_WELCOME; });
    }
    printf("  welcome delivered=%d host pending=%zu\n", got_welcome, host.pending_reliable());
    CHECK(got_welcome == 1);
    CHECK(host.pending_reliable() == 0);
}

// BUG-0053: the sender's 32-bit millisecond clock wraps after 49.7 days: every later snapshot was
// "older" than the last one and the puppet froze
static void test_interp_wrap()
{
    printf("snapshot interpolation across the sender's clock wrap (BUG-0053)\n");
    SnapshotBuffer b;
    PlayerState s;
    s.time_ms = 0xFFFFFF00u;
    s.pos = {1, 0, 0};
    b.push(s, 100.0);
    s.time_ms = 0x00000100u; // 512 ms later, after the wrap
    s.pos = {5, 0, 0};
    b.push(s, 100.5);
    PlayerState out;
    CHECK(b.sample(101.0, out));
    printf("  x=%g (5 expected)\n", out.pos.x);
    CHECK(out.pos.x > 4);
    CHECK(b.age(101.0) < 1.0);
    // an older snapshot after the wrap is still dropped, the NPC spawn's time 0 is older than any
    s.time_ms = 0xFFFFFF80u;
    s.pos = {-50, 0, 0};
    b.push(s, 100.6);
    CHECK(b.sample(101.0, out) && out.pos.x > 4);
    SnapshotBuffer n;
    s.time_ms = 0;
    s.pos = {0, 0, 0};
    n.push(s, 10.0);
    s.time_ms = 0x90000000u; // a host up for 28 days
    s.pos = {7, 0, 0};
    n.push(s, 10.05);
    CHECK(n.sample(11.0, out) && out.pos.x > 6);
}

// BUG-0047: a 5-byte record with the 4-byte-size flag read one byte past the buffer (ASan in
// test-asan); it is rejected now
static void test_refpack_short()
{
    printf("RefPack: a 5-byte record with a 4-byte size (BUG-0047)\n");
    uint8_t* p = new uint8_t[5]{0x90, 0xFB, 0, 0, 0};
    std::vector<uint8_t> out;
    CHECK(!dbpf::refpack_decode(p, 5, out));
    delete[] p;
}

// BUG-0039: answers with a new port each time grew the LAN game list for ever
static void test_dbbf_wrap()
{
    printf("DBPF: a DBBF index offset near 2^64 is rejected, not read before the file (BUG-0208)\n");
    std::vector<uint8_t> f(96, 0);
    memcpy(f.data(), "DBBF", 4);
    f[0x24] = 1;                         // one entry
    f[0x28] = 4;                         // index size 4
    memset(f.data() + 0x38, 0xFF, 8);    // index offset 2^64 - 1: offset + size wraps to 3
    dbpf::Package p;
    CHECK(!p.load(f));
    // an entry whose offset wraps the same way is dropped
    std::vector<uint8_t> g(96, 0);
    memcpy(g.data(), "DBBF", 4);
    g[0x24] = 1;
    g[0x28] = 4 + 4 + 4 + 4 + 8 + 12;    // flags, type, group, instance, u64 offset, size/usize/flags
    g[0x38] = 96;
    std::vector<uint8_t> idx(g[0x28], 0);
    memset(idx.data() + 16, 0xFF, 8);    // the entry's offset: 2^64 - 1
    idx[24] = 8;                         // size 8
    g.insert(g.end(), idx.begin(), idx.end());
    CHECK(p.load(g) && p.entries().empty());
}

static void test_discovery_prune()
{
    printf("LAN game list: pruned and capped (BUG-0039)\n");
    Discovery d;
    CHECK(d.start());
    // no query targets: a real game of a lab lane on this machine would answer a loopback query
    // and stay in the list past the prune (seen in make test while lanes were running)
    d.targets = {};
    // what update() does with each answer datagram (64 per frame at most)
    uint32_t now = 100000;
    for (int i = 0; i < 500; i++) {
        DiscoverReply rep;
        rep.port = (uint16_t)(1000 + i);
        rep.name = std::string(255, 'x');
        rep.info = "fake";
        d.on_reply(Endpoint{0x0100000A, 0}, rep, now + i / 64);
    }
    d.update(now + 10);
    printf("  after 500 fake answers: kept %zu, listed %zu\n", d.stored(), d.games(now + 10).size());
    CHECK(d.stored() <= Discovery::kMaxGames);
    d.update(now + 6000);
    CHECK(d.stored() == 0);
    CHECK(d.games(now + 6000).empty());
}

// BUG-0138: a pasted address with spaces around it, the whole hint line, a wrong port
static void test_address_parse()
{
    printf("host address parsing (BUG-0138)\n");
    struct Case {
        const char* in;
        bool ok;
        uint16_t port;
    } cases[] = {{" 192.168.1.5:47810", true, 47810},
                 {"\t192.168.1.5:47811\r\n", true, 47811},
                 {"192.168.1.5:47810,  10.0.0.2:47810", true, 47810},
                 {"192.168.1.5", true, kDefaultPort},
                 {"192.168.1.5:", false, 0},
                 {"192.168.1.5:abc", false, 0},
                 {"192.168.1.5:99999", false, 0},
                 {"192.168.1.5:0", false, 0},
                 {"192.168.1.5:-1", false, 0},
                 {":47810", false, 0}};
    for (auto& c : cases) {
        Endpoint e;
        std::string err;
        bool ok = Endpoint::parse(c.in, kDefaultPort, e, err);
        bool good = ok == c.ok && (!ok || (e.port == c.port && e.str().rfind("192.168.1.5:", 0) == 0));
        if (!good)
            printf("  '%s': ok=%d port=%u err='%s'\n", c.in, ok, e.port, err.c_str());
        CHECK(good);
        if (!ok && strchr(c.in, ':') && c.in[0] != ':')
            CHECK(err.find("port") != std::string::npos);
    }
}

// BUG-0204: joining by host name looked the name up inside the click (the game's main thread): a
// slow name server stopped the frames. The fake lookup takes 1.5 s like a name server that hangs.
static bool slow_lookup(const std::string& host, uint32_t& ip)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    if (host != "slowhost")
        return false;
    ip = htonl(0x7F000001);
    return true;
}

static void test_join_by_name()
{
    printf("join by host name does not wait for the name server (BUG-0204)\n");
    LookupFn saved = g_lookup;
    g_lookup = slow_lookup;
    Session host, cli;
    host.build = cli.build = 0x922af7c7;
    bool connected = false;
    std::string disc;
    cli.cb.connected = [&] { connected = true; };
    cli.cb.disconnected = [&](const std::string& w) { disc = w; };
    std::string err;
    CHECK(host.host(0, "host-alice", err));
    auto t0 = std::chrono::steady_clock::now();
    CHECK(cli.join("slowhost:" + std::to_string(host.port()), "guest-bob", err));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    printf("  join() returned after %lld ms\n", (long long)ms);
    CHECK(ms < 300); // the frame is not held
    for (int i = 0; i < 80 && !connected; i++)
        pump(host, cli, 50);
    CHECK(connected);
    CHECK(cli.host_address() == "127.0.0.1:" + std::to_string(host.port()));
    // a name that does not resolve: the error arrives through disconnected, the frame never waits
    Session bad;
    t0 = std::chrono::steady_clock::now();
    bool started = bad.join("nohost:47810", "x", err);
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 300);
    if (started) {
        bad.cb.disconnected = [&](const std::string& w) { disc = w; };
        for (int i = 0; i < 80 && bad.role() != Role::None; i++) {
            bad.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    } else
        disc = err;
    CHECK(bad.role() == Role::None);
    CHECK(disc.find("cannot resolve nohost") != std::string::npos);
    // leaving while the lookup still runs: the worker finishes alone
    Session gone;
    CHECK(gone.join("slowhost:47810", "y", err));
    gone.leave("user");
    CHECK(!gone.resolving());
    std::this_thread::sleep_for(std::chrono::milliseconds(1700));
    g_lookup = saved;
}

// BUG-0094: the setup used the first SporeCoop-*.pack the file system listed (NTFS: by name, so
// 0.10.0 before 0.11.0 and 0.9.0)
static void test_pick_pack()
{
    printf("setup: the newest local pack (BUG-0094)\n");
    CHECK(upd::pack_version_of("SporeCoop-0.11.0.pack") == "0.11.0");
    CHECK(upd::pack_version_of("C:\\x\\sporecoop-0.7.2.4.PACK") == "0.7.2.4");
    CHECK(upd::pack_version_of("SporeCoop-.pack").empty());
    CHECK(upd::pack_version_of("SporeCoop-beta.pack").empty());
    CHECK(upd::pick_pack({"SporeCoop-0.10.0.pack", "SporeCoop-0.11.0.pack", "SporeCoop-0.9.0.pack"}) ==
          "SporeCoop-0.11.0.pack");
    CHECK(upd::pick_pack({"SporeCoop-x.pack", "SporeCoop-0.8.1.pack", "SporeCoop-0.8.0.10.pack"}) ==
          "SporeCoop-0.8.1.pack");
    CHECK(upd::pick_pack({}).empty());
}

// ---- support diagnostics (net/diagmsg.h, docs/SUPPORT.md) ----
static void test_diag_messages()
{
    printf("diag: clock, mark and digest messages round trip; a bad digest is refused\n");
    namespace dg = net::diag;
    Writer w;
    dg::ClockReq q{7, 123.25};
    q.write(w);
    Reader r(w.data.data(), w.size());
    CHECK(r.u8() == dg::kClockReq);
    dg::ClockReq q2;
    CHECK(q2.read(r) && q2.seq == 7 && q2.t_local == 123.25);
    Writer wa;
    dg::ClockAns a{7, 123.25, 4567.5};
    a.write(wa);
    Reader ra(wa.data.data(), wa.size());
    dg::ClockAns a2;
    CHECK(ra.u8() == dg::kClockAns && a2.read(ra) && a2.t_echo == 123.25 && a2.t_session == 4567.5);
    // a mark: the text is cut at 200 bytes between two characters
    dg::Mark m;
    m.origin = 2;
    m.t_session = 99.125;
    m.name = "Bob";
    while (m.text.size() < 400)
        m.text += "\xd0\x96";
    Writer wm;
    m.write(wm);
    Reader rm(wm.data.data(), wm.size());
    dg::Mark m2;
    CHECK(rm.u8() == dg::kMark && m2.read(rm) && m2.origin == 2 && m2.t_session == 99.125 && m2.name == "Bob");
    CHECK(m2.text.size() <= dg::kMarkTextMax && m2.text.size() >= dg::kMarkTextMax - 1 && utf8::valid(m2.text));
    // a digest
    dg::Digest d;
    d.seq = 5;
    d.self = 1;
    d.mode = 0x1654C00;
    d.world = 0xABCD;
    d.vote = 1;
    d.vote_editor = 0x1234;
    d.totals = {{dg::kTotalCellFood, 42}};
    d.entries = {{dg::make_key('p', 1), 1, 2, 3}, {dg::make_key('c', 77), 4, 5, 6}};
    d.out = {2, 3};
    Writer wd;
    d.write(wd);
    Reader rd(wd.data.data(), wd.size());
    dg::Digest d2;
    CHECK(rd.u8() == dg::kDigest && d2.read(rd) && d2.seq == 5 && d2.self == 1 && d2.world == 0xABCD && d2.vote == 1);
    CHECK(d2.totals.size() == 1 && d2.totals[0].second == 42 && d2.entries.size() == 2 && d2.entries[1].z == 6 && d2.out.size() == 2 && d2.out[1] == 3);
    CHECK(dg::parse_key("c77") == dg::make_key('c', 77) && dg::parse_key("p") == 0 && dg::parse_key("x1y") == 0);
    CHECK(dg::key_text(dg::make_key('n', 12)) == "n12");
    // a digest that claims more entries than it has
    Writer wb;
    d.write(wb);
    wb.data.resize(wb.data.size() - 11);
    Reader rb(wb.data.data(), wb.size());
    rb.u8();
    dg::Digest d3;
    CHECK(!d3.read(rb));
}

static void test_diag_clock()
{
    printf("diag: the session clock takes the sample with the shortest round trip\n");
    namespace dg = net::diag;
    dg::ClockSync c;
    CHECK(!c.valid());
    // the host's session clock is local + 1000; one way delays differ per sample
    double true_off = 1000;
    struct S {
        double up, down;
    } s[] = {{0.050, 0.010}, {0.004, 0.004}, {0.030, 0.090}, {0.002, 0.003}, {0.200, 0.010}};
    double t = 10;
    for (auto& x : s) {
        double host = t + x.up + true_off;
        c.add(t, t + x.up + x.down, host);
        t += 1;
    }
    CHECK(c.valid() && c.samples() == 5);
    CHECK(std::fabs(c.offset() - true_off) < 0.001); // the 5 ms sample: error <= 0.5 ms
    CHECK(std::fabs(c.rtt() - 0.005) < 1e-9);
    c.add(20, 19, 5); // an answer before the request: refused
    c.add(20, 30, 5); // 10 s round trip: stale
    CHECK(c.samples() == 5);
    for (int i = 0; i < 20; i++)
        c.add(100 + i, 100.5 + i, 100.25 + i + 7);
    CHECK(c.samples() == dg::ClockSync::kKeep && std::fabs(c.offset() - 7) < 1e-9); // old samples aged out
}

static void test_diag_compare()
{
    printf("diag: the sync-check compares stage, players, totals and objects; lasting differences only\n");
    namespace dg = net::diag;
    dg::Digest h, g;
    h.mode = g.mode = 0x1654C01;
    h.world = g.world = 0x42;
    h.self = 0;
    g.self = 1;
    h.totals = {{dg::kTotalDna, 100}};
    g.totals = {{dg::kTotalDna, 100.2f}};
    h.entries = {{dg::make_key('p', 0), 0, 0, 0}, {dg::make_key('p', 1), 50, 0, 0}, {dg::make_key('n', 1), 10, 0, 0},
                 {dg::make_key('n', 2), 500, 0, 0}, {dg::make_key('c', 3), 1, 1, 1}};
    g.entries = {{dg::make_key('p', 0), 2, 0, 0}, {dg::make_key('p', 1), 51, 0, 0}, {dg::make_key('n', 1), 11, 0, 0},
                 {dg::make_key('c', 3), 1, 1, 2}};
    dg::Compared c = dg::compare(h, g);
    // n2 is 450 m from the guest: never sent to it, not missing
    CHECK(!c.other_place && c.issues.empty() && c.objects == 4);
    // differences
    g.entries.push_back({dg::make_key('n', 9), 0, 0, 0});  // a ghost on the guest
    h.entries.push_back({dg::make_key('n', 4), 60, 0, 0}); // near the guest, missing there
    h.entries.push_back({dg::make_key('c', 5), 0, 0, 0});  // a cell the guest lacks
    g.entries[2].x = 40;                                   // n1 30 m off
    g.totals[0].second = 80;
    g.vote = 1;
    c = dg::compare(h, g);
    std::set<std::string> ids;
    for (auto& i : c.issues)
        ids.insert(i.id);
    CHECK(ids.count("extra:n9") && ids.count("missing:n4") && ids.count("missing:c5") && ids.count("pos:n1"));
    CHECK(ids.count("total:1") && ids.count("vote") && !ids.count("missing:n2") && ids.size() == 6);
    std::string t = dg::describe(c.issues);
    printf("  %s\n", t.c_str());
    CHECK(t.find("n: 1 missing on the guest (n4)") != std::string::npos && t.find("n: 1 apart (n1 30.0)") != std::string::npos);
    CHECK(t.find("dna: host 100.0, guest 80.0") != std::string::npos);
    // a recheck: only what is still there counts
    dg::Digest g2 = g;
    g2.totals[0].second = 100;
    g2.vote = 0;
    g2.entries.pop_back(); // the ghost is gone
    auto lasting = dg::persistent(c.issues, dg::compare(h, g2).issues);
    CHECK(lasting.size() == 3); // missing n4, missing c5, pos n1
    // a player out of the world on one side (dead, respawning): its creature is not compared
    dg::Digest h3 = h, g3 = g2;
    h3.entries.erase(h3.entries.begin()); // the host has no own creature now (dead)...
    g3.entries.erase(g3.entries.begin()); // ...and the guest no puppet of it
    g3.entries.erase(g3.entries.begin()); // the guest's own creature is gone too, the host still shows it
    h3.out = {0};
    g3.out = {1};
    for (auto& i : dg::compare(h3, g3).issues)
        CHECK(i.key.empty() || i.key[0] != 'p');
    // RTS stages: NPCs are not sent per guest
    dg::Limits rts;
    rts.npc_missing = false;
    for (auto& i : dg::compare(h, g2, rts).issues)
        CHECK(i.id != "missing:n4");
    // another stage: nothing compared
    g2.mode = 0x1654C00;
    c = dg::compare(h, g2);
    CHECK(c.other_place && c.issues.size() == 1 && c.objects == 0);
}

// MSG_DIAG between new and old games: an older SporeCoop gets an unknown message type (its handler
// ignores it) and the session goes on; a mark of a guest is relayed by the host to the other guest
static void test_diag_session()
{
    printf("diag: MSG_DIAG relayed by the host, ignored by an older guest\n");
    namespace dg = net::diag;
    Session host, oldg, newg;
    host.build = oldg.build = newg.build = 0x922af7c7;
    oldg.send_caps = false;
    std::string err;
    CHECK(host.host(0, "Host", err));
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    int host_marks = 0, new_marks = 0, old_unknown = 0, old_chats = 0;
    // the host relays a mark like coop.cpp does (support::on_message)
    host.cb.custom = [&](uint8_t type, uint8_t from, Reader& r) {
        if (type != MSG_DIAG || r.u8() != dg::kMark)
            return;
        dg::Mark m;
        if (!m.read(r))
            return;
        host_marks++;
        m.origin = from;
        Writer w;
        m.write(w);
        host.broadcast_reliable(MSG_DIAG, w, from);
    };
    newg.cb.custom = [&](uint8_t type, uint8_t from, Reader& r) {
        dg::Mark m;
        if (type == MSG_DIAG && from == 0 && r.u8() == dg::kMark && m.read(r) && m.text == "only one can eat")
            new_marks++;
    };
    // an older SporeCoop: every type it does not know falls through (world::on_message ignores it)
    oldg.cb.custom = [&](uint8_t type, uint8_t, Reader&) { old_unknown += type == MSG_DIAG; };
    oldg.cb.chat = [&](uint8_t, const std::string& t) { old_chats += t == "still here"; };
    auto run = [&](int ms) {
        for (int i = 0; i < ms / 5; i++) {
            host.update();
            oldg.update();
            newg.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    CHECK(oldg.join(addr, "Old", err));
    run(300);
    CHECK(newg.join(addr, "New", err));
    run(300);
    CHECK(oldg.connected() && newg.connected());
    // a guest's mark reaches the other guest through the host; one of the host reaches both
    dg::Mark m;
    m.name = "Old";
    m.text = "only one can eat";
    Writer w;
    m.write(w);
    oldg.send_reliable_to(0, MSG_DIAG, w); // a guest's mark: the host relays it to the other guest
    run(300);
    CHECK(host_marks == 1 && new_marks == 1);
    Writer wh;
    m.write(wh);
    host.broadcast_reliable(MSG_DIAG, wh);
    Writer wq;
    dg::ClockReq{1, 2.0}.write(wq);
    host.send_unreliable_to(oldg.my_id(), MSG_DIAG, wq);
    run(300);
    CHECK(old_unknown >= 1 && new_marks == 2);
    host.send_chat("still here"); // the old guest's session goes on
    run(300);
    CHECK(old_chats == 1 && oldg.connected() && newg.connected());
}

// the support statistics of a connection: bytes both ways, round trip samples, lost packets
static void test_diag_conn_stats()
{
    printf("diag: connection statistics (bytes, round trips, lost packets)\n");
    Connection a, b;
    std::vector<uint8_t> pkt;
    uint32_t now = 1000;
    auto deliver = [](uint8_t, Reader&) {};
    int dropped = 0;
    for (int i = 0; i < 200; i++) {
        Writer w;
        w.u32(i);
        a.send_unreliable(MSG_PLAYER_STATE, w);
        if (a.build_packet(now, pkt)) {
            if (i % 10 == 3) // 10 % of a's packets are lost on the way
                dropped++;
            else
                b.on_packet(pkt.data(), pkt.size(), now + 5, deliver);
        }
        if (b.build_packet(now + 5, pkt))
            a.on_packet(pkt.data(), pkt.size(), now + 10, deliver);
        now += 20;
    }
    printf("  sent %u bytes %llu, b got %llu, lost %u (dropped %d), rtt n=%u min %.0f max %.0f\n", a.stats.packets_sent,
           (unsigned long long)a.stats.bytes_sent, (unsigned long long)b.stats.bytes_recv, a.stats.lost, dropped,
           a.stats.rtt_n, a.stats.rtt_min, a.stats.rtt_max);
    CHECK(a.stats.bytes_sent > 200 * 30 && b.stats.bytes_recv < a.stats.bytes_sent && b.stats.bytes_recv > 0);
    CHECK(a.stats.lost + 4 >= (uint32_t)dropped && a.stats.lost <= (uint32_t)dropped); // the last kLostAfter packets are still open
    CHECK(a.stats.rtt_n > 100 && a.stats.rtt_min >= 10 && a.stats.rtt_max <= 30);
    a.stats.take_window();
    CHECK(a.stats.rtt_n == 0);
    // a bulk transfer outruns the acks (one ack per 200 packets, all received): nothing counts as lost
    Connection c, d;
    for (int i = 0; i < 2000; i++) {
        Writer w;
        w.u32(i);
        c.send_unreliable(MSG_PLAYER_STATE, w);
        if (c.build_packet(now, pkt))
            d.on_packet(pkt.data(), pkt.size(), now, deliver);
        if (i % 200 == 199 && d.build_packet(now + 300, pkt))
            c.on_packet(pkt.data(), pkt.size(), now + 1, deliver);
        now += 1;
    }
    printf("  bulk: sent %u, lost %u\n", c.stats.packets_sent, c.stats.lost);
    CHECK(c.stats.packets_sent > 1500 && c.stats.lost == 0);
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--fakepeer-host")
        return fakepeer_host();
    // `--off <name>[,<name>]`: one wave 5 switch off (its test must fail), the rest on
    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--off") {
            std::string list = argv[i + 1];
            std::map<std::string, bool*> sw = {{"peer_caps", &g_fix.peer_caps},
                                               {"rejoin_token", &g_fix.rejoin_token},
                                               {"addr_label", &g_fix.addr_label},
                                               {"update_paths", &upd::g_path_fix}};
            for (size_t a = 0; a <= list.size();) {
                size_t b = list.find(',', a);
                if (b == std::string::npos)
                    b = list.size();
                auto it = sw.find(list.substr(a, b - a));
                if (it == sw.end()) {
                    fprintf(stderr, "unknown switch '%s'\n", list.substr(a, b - a).c_str());
                    return 2;
                }
                *it->second = false;
                printf("net fix %s OFF\n", it->first.c_str());
                a = b + 1;
            }
        }
    if (argc == 2 && (std::string(argv[1]) == "--nofix" || std::string(argv[1]) == "--old")) {
        // the code before the fixes (tests must fail): every bool of Fixes off
        static_assert(std::is_trivially_copyable_v<Fixes>, "Fixes holds only its bools");
        memset((void*)&g_fix, 0, sizeof g_fix);
        g_nofix = true;
        upd::g_dup_fix = false;
        upd::g_path_fix = false;
        upd::g_pick_fix = false;
        // BUG-0214 R6: the decoder bounds fixes stay on (their off side is an out-of-bounds read
        // that aborts the ASan build); --old-dbbf / --old-refpack switch them off one at a time
        printf("netcode fixes OFF\n");
    }
    if (argc == 2 && std::string(argv[1]) == "--old-dbbf") { // must die under ASan
        dbpf::g_bounds_fix = false;
        test_dbbf_wrap();
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--old-refpack") { // must die under ASan
        dbpf::g_refpack_fix = false;
        test_refpack_short();
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    test_buffer();
    test_unreliable_chunks();
    test_dbpf();
    test_be_keys();
    test_dbbf();
    test_refpack_untrusted();
    test_updater();
    test_updater_dup();
    test_updater_paths();
    test_reliability();
    test_ackbits();
    test_bulk();
    test_bulk_bad_link();
    test_session();
    test_session_end_in_delivery();
    test_rejoin_same_endpoint();
    test_unique_names();
    test_big_snapshot();
    test_interp();
    test_interp_gap();
    test_discovery();
    test_lobby_roles();
    test_shared_ledger();
    test_shared_join_keep();
    test_big_reliable();
    test_state_after_leave();
    test_velocity_jitter();
    test_interp_latency_rise();
    test_alloc_id();
    test_lost_first_packet();
    test_interp_wrap();
    test_refpack_short();
    test_dbbf_wrap();
    test_discovery_prune();
    test_address_parse();
    test_address_label();
    test_old_peer_split();
    test_old_peer_session();
    test_rejoin_token();
    test_peer_versions();
    test_ip_mask();
    test_join_by_name();
    test_pick_pack();
    test_diag_messages();
    test_diag_clock();
    test_diag_compare();
    test_diag_session();
    test_diag_conn_stats();
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
