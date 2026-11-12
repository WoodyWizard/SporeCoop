// Native (Linux) tests for the portable coop netcode: make test
#include "../updater/pack.h"
#include <algorithm>
#include <cassert>
#include <deque>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include "net/dbpf.h"
#include "net/partprops.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <random>
#include <chrono>
#include <thread>
#include <tuple>
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
#include "net/utf8.h"

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
    // BUG-0519: the Cell world scale is appended; an older sender's state (without it) reads as 0
    s.scale = 1.5f;
    Writer w4;
    s.write(w4);
    Reader r4(w4.data.data(), w4.size());
    PlayerState t4;
    CHECK(t4.read(r4) && t4.scale == 1.5f && r4.left() == 0);
    Reader r5(w4.data.data(), w4.size() - 12); // (BUG-0733: the body after the agl)
    PlayerState t5;
    CHECK(t5.read(r5) && t5.scale == 0.f && t5.world == 0x80B16021 && !agl_known(t5.agl));
    // BUG-0425: the sender's agl after the scale; a 0.7.7 sender's state (scale, no agl) reads as kNoAgl
    s.agl = 2.5f;
    Writer w6;
    s.write(w6);
    Reader r6(w6.data.data(), w6.size());
    PlayerState t6;
    CHECK(t6.read(r6) && t6.agl == 2.5f && t6.scale == 1.5f && r6.left() == 0);
    Reader r7(w6.data.data(), w6.size() - 8);
    PlayerState t7;
    CHECK(t7.read(r7) && t7.scale == 1.5f && !agl_known(t7.agl));
    // BUG-0733: the sender's creature size after the agl; an older sender's state reads as 0
    s.body = 1.21f;
    Writer w8;
    s.write(w8);
    Reader r8(w8.data.data(), w8.size());
    PlayerState t8;
    CHECK(t8.read(r8) && t8.body == 1.21f && t8.agl == 2.5f && r8.left() == 0);
    Reader r9(w8.data.data(), w8.size() - 4);
    PlayerState t9;
    CHECK(t9.read(r9) && t9.body == 0.f && t9.agl == 2.5f);
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

// F3 (BUG-0768): the host's player limit (all.max_players) and the late-join rule (all.late_join off once the host
// plays its world): a player over the limit or new after the start is refused with the reason; a player who was in
// the session comes back
static void test_join_rules()
{
    printf("join rules: the player limit and joining after the start (BUG-0768)\n");
    Session host, a, b, c;
    std::string err;
    host.build = a.build = b.build = c.build = 0x922af7c7;
    CHECK(host.host(0, "alice", err));
    std::string port = "127.0.0.1:" + std::to_string(host.port());
    host.max_players = 2;
    CHECK(a.join(port, "bob", err));
    pump(host, a, 300);
    CHECK(a.connected() && host.players().size() == 2);
    std::string why_b;
    b.cb.disconnected = [&](const std::string& w) { why_b = w; };
    CHECK(b.join(port, "carol", err));
    pump(host, b, 300);
    CHECK(!b.connected() && why_b.find("session is full") != std::string::npos && host.players().size() == 2);
    printf("  limit 2: %s\n", why_b.c_str());
    host.max_players = 3;
    CHECK(b.join(port, "carol", err));
    pump(host, b, 300);
    CHECK(b.connected() && host.players().size() == 3);
    printf("  %s", host.status().c_str());
    CHECK(host.status().find("players=3/3") != std::string::npos);
    // the host plays its world, new players not taken
    host.max_players = 4;
    bool closed = true;
    host.join_closed = [&] { return closed; };
    std::string why_c;
    c.cb.disconnected = [&](const std::string& w) { why_c = w; };
    CHECK(c.join(port, "dave", err));
    pump(host, c, 300);
    CHECK(!c.connected() && why_c.find("new players") != std::string::npos && host.players().size() == 3);
    printf("  after the start: %s\n", why_c.c_str());
    a.leave("test"); // bob leaves and comes back: he was in this session
    pump(host, a, 200);
    CHECK(host.players().size() == 2);
    CHECK(a.join(port, "bob", err));
    pump(host, a, 300);
    CHECK(a.connected() && host.players().size() == 3);
    closed = false; // joining after the start allowed again
    CHECK(c.join(port, "dave", err));
    pump(host, c, 300);
    CHECK(c.connected() && host.players().size() == 4);
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
    {   // BUG-0519: a Cell level change: the same point at x=10 (scale 1) and x=5 (scale 2) blends
        // in the newer state's scale (5), not half-way between two scales (7.5)
        SnapshotBuffer c;
        c.delay = 0.1;
        for (int i = 0; i <= 10; i++) {
            PlayerState s;
            s.time_ms = 1000 + i * 50;
            s.scale = i < 5 ? 1.f : 2.f;
            s.pos = {i < 5 ? 10.f : 5.f, 0, 0};
            c.push(s, 5.0 + i * 0.05);
        }
        PlayerState q;
        CHECK(c.sample(5.0 + 0.325, q)); // between the states 4 (scale 1) and 5 (scale 2)
        CHECK(std::fabs(q.pos.x - 5.0f) < 0.01f && q.scale == 2.f);
        SnapshotBuffer::scale_lerp = false;
        CHECK(c.sample(5.0 + 0.325, q) && q.pos.x > 6.f); // the old blend (cell.puppet_scale off)
        SnapshotBuffer::scale_lerp = true;
    }
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

// K4 (BUG-0674, BUG-0675, BUG-0651, BUG-0680): SPORE_FIX_OFF=<switch>[,...] keeps that fix off on the fix's side of
// test_lead_fall too, so the run FAILs (the twin-switch check of CLAUDE.md 3 for the unit test)
static bool k4_on(const char* name)
{
    const char* e = std::getenv("SPORE_FIX_OFF");
    if (!e)
        return true;
    std::string s = std::string(",") + e + ",";
    return s.find(std::string(",") + name + ",") == std::string::npos;
}

// BUG-0650 (drive.lead_late): a Tribe member's walk that starts in a latency spike (its first walking state 0.35 s
// late instead of 0.05 s) moved M1's lead target 3 m in one frame; with the fix the target steps no more than a walk
// and is at the lead again within 1.5 s
static float lead_late_max_step(bool fix, double spike_end, float* end_err, bool proxy = false)
{
    const bool was = g_lead_late_fix, was_fall = g_lead_fall_fix;
    const double was_ow = g_one_way;
    g_lead_late_fix = fix;
    SnapshotBuffer b;
    b.delay = 0.2;
    b.lead = true;
    b.smooth_max = 6.f;
    if (proxy) { // BUG-0674: an NPC proxy (no smoothing, late_lead under drive.lead_fall)
        b.delay = 0.15;
        b.smooth_max = 8.f;
        b.late_lead = true;
        g_lead_late_fix = true;
        g_lead_fall_fix = fix && k4_on("drive.lead_fall");
    }
    g_one_way = 0.05;
    // the sender sends every 1/30 s; the body stands until sender time 3.0, then walks at 9 m/s along x
    auto state = [](double st) {
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.mode = 0x1654C01;
        s.world = 7;
        s.flags = PF_IN_WORLD;
        float x = st > 3.0 ? (float)(st - 3.0) * 9.f : 0.f;
        s.pos = {x, 0.f, 0.f};
        s.vel = {st > 3.0 ? 9.f : 0.f, 0.f, 0.f};
        if (st > 3.0)
            s.flags |= PF_MOVING;
        return s;
    };
    // arrival: 0.05 s late, 0.35 s for the states sent in [2.95, spike_end) (the spike; the states sent after it come
    // before the last of the spike's, which are dropped as old)
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 180; i++) {
        double st = i / 30.0;
        double lat = st >= 2.95 && st < spike_end ? 0.35 : 0.05;
        arr.push_back({10.0 + st + lat, state(st)});
    }
    std::stable_sort(arr.begin(), arr.end(), [](auto& a, auto& c) { return a.first < c.first; });
    size_t k = 0;
    float max_step = 0;
    Vec3 last{};
    bool have = false;
    PlayerState o;
    for (double now = 10.0; now < 15.9; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now)
            b.push(arr[k].second, arr[k].first), k++;
        if (!b.sample(now, o))
            continue;
        if (have && now > 12.5 && now < 14.0)
            max_step = std::max(max_step, dist(last, o.pos));
        last = o.pos;
        have = true;
    }
    // where the sender's body is now (+ one-way): sender time now - 10 + 0.05
    *end_err = std::fabs(o.pos.x - (float)((15.9 - 1.0 / 30 - 10.0 + 0.05 - 3.0) * 9.0));
    g_lead_late_fix = was;
    g_lead_fall_fix = was_fall;
    g_one_way = was_ow;
    return max_step;
}

static void test_lead_late()
{
    printf("lead target of a walk known late (BUG-0650)\n");
    for (double spike_end : {3.3, 3.65}) { // the walk's first states: after the spike / late, in order
        float err_on = 0, err_off = 0;
        float on = lead_late_max_step(true, spike_end, &err_on), off = lead_late_max_step(false, spike_end, &err_off);
        printf("  spike to %.2f s: max target step per 1/30 s frame: fix %.2f m (end error %.2f), M1 %.2f m (%.2f)\n",
               spike_end, on, err_on, off, err_off);
        CHECK(off > 2.f);     // M1: the start known 0.35 s late jumps 2.7-3.3 m
        // under 2 x the walk (9 m/s x 1/30 s; drive.lead_keep catches up at 2 x: + one frame's step back); the test
        // draws a jump at 3 x
        CHECK(on < (g_lead_keep_fix ? 0.7f : 0.6f));
        CHECK(err_on < 0.5f); // and it is at the lead again
    }
}

// BUG-0651, BUG-0410 (drive.lead_keep): a Tribe member walking at 9 m/s, its states every 1/30 s, `lat` s late (one
// way); a walk that starts at sender time 3.0 is known `start_late` s late (a spike). Returns the mean distance of the
// shown target from the sender's body now (sender time = local - 10 - lat + one-way, where the lead shows it) over
// local 12.5-15.5 s, and in *catch_s how long after the late start's arrival the target is within 0.3 m of it
static float lead_keep_err(bool keep, double lat, double start_late, double* catch_s)
{
    const bool was = g_lead_keep_fix;
    const double was_ow = g_one_way;
    g_lead_keep_fix = keep;
    SnapshotBuffer b;
    b.delay = 0.2;
    b.lead = true;
    b.smooth_max = 6.f;
    g_one_way = lat;
    auto x_at = [](double st) { return st > 3.0 ? (float)(st - 3.0) * 9.f : 0.f; };
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 210; i++) {
        double st = i / 30.0;
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.mode = 0x1654C01;
        s.world = 7;
        s.flags = PF_IN_WORLD | (st > 3.0 ? PF_MOVING : 0);
        s.pos = {x_at(st), 0.f, 0.f};
        s.vel = {st > 3.0 ? 9.f : 0.f, 0.f, 0.f};
        arr.push_back({10.0 + st + lat + (st >= 2.95 && st < 3.3 ? start_late : 0.0), s});
    }
    std::stable_sort(arr.begin(), arr.end(), [](auto& a, auto& c) { return a.first < c.first; });
    size_t k = 0;
    PlayerState o;
    double sum = 0, known = -1;
    int n = 0;
    *catch_s = 99;
    for (double now = 10.0; now < 16.9; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now) {
            if (known < 0 && arr[k].second.flags & PF_MOVING)
                known = now;
            b.push(arr[k].second, arr[k].first), k++;
        }
        if (!b.sample(now, o))
            continue;
        float err = std::fabs(o.pos.x - x_at(now - 10.0));
        if (now > 13.5 && now < 16.5) {
            sum += err;
            n++;
        }
        if (known >= 0 && *catch_s > 90 && err < 0.3f)
            *catch_s = now - known;
    }
    g_lead_keep_fix = was;
    g_one_way = was_ow;
    return n ? (float)(sum / n) : 99.f;
}

// BUG-0410 (drive.lead_keep): a member walking at a speed that changes (5-11 m/s, a 2 s period) along x, its states every
// 0.1 s with 0-30 ms of send and arrival jitter on a 45 ms link, a guest at 30 fps: the mean distance of the shown
// target from the sender's body now and how far the shown time is behind the lead's (p90)
static float lead_ratchet_err(bool keep, double* behind_p90)
{
    const bool was = g_lead_keep_fix;
    const double was_ow = g_one_way;
    g_lead_keep_fix = keep;
    SnapshotBuffer b;
    b.delay = 0.2;
    b.lead = true;
    b.smooth_max = 6.f;
    g_one_way = 0.06;
    auto spd = [](double st) { return 8.0 + 3.0 * std::sin(st * 3.14159); };
    std::vector<double> xs(1); // x at sender time i / 1000
    for (int i = 1; i < 20000; i++)
        xs.push_back(xs.back() + spd(i / 1000.0) / 1000.0);
    auto x_at = [&](double st) { return (float)xs[std::min<size_t>(xs.size() - 1, (size_t)std::max(0.0, st * 1000.0))]; };
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> U(0, 1);
    std::vector<std::pair<double, PlayerState>> arr;
    for (double st = 0; st < 19.0; st += 0.1 + 0.01 * U(rng)) {
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.mode = 0x1654C01;
        s.world = 7;
        s.flags = PF_IN_WORLD | PF_MOVING;
        s.pos = {x_at(st), 0.f, 0.f};
        s.vel = {(float)spd(st), 0.f, 0.f};
        arr.push_back({10.0 + st + 0.045 + 0.03 * U(rng), s});
    }
    std::stable_sort(arr.begin(), arr.end(), [](auto& a, auto& c) { return a.first < c.first; });
    size_t k = 0;
    PlayerState o;
    double sum = 0;
    int n = 0;
    std::vector<double> lb;
    for (double now = 10.0; now < 28.0; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now)
            b.push(arr[k].second, now), k++;
        if (!b.sample(now, o) || now < 13.0)
            continue;
        sum += std::fabs(o.pos.x - x_at(now - 10.0 - 0.045 + 0.06));
        n++;
        lb.push_back(b.late_behind(now));
    }
    std::sort(lb.begin(), lb.end());
    *behind_p90 = lb.empty() ? 99 : lb[lb.size() * 9 / 10];
    g_lead_keep_fix = was;
    g_one_way = was_ow;
    return n ? (float)(sum / n) : 99.f;
}

// BUG-0674 (drive.lead_keep): a member walking +x at 9 m/s turns round at sender time 3.0; the states of [2.95, 3.3) come
// 0.35 s late (a spike), so the lead goes on the old way meanwhile. Returns how long (s) the shown target is over 2 m from
// the sender's body after the first late state arrived
static double lead_turn_over2(bool keep)
{
    const bool was = g_lead_keep_fix;
    const double was_ow = g_one_way;
    g_lead_keep_fix = keep;
    SnapshotBuffer b;
    b.delay = 0.2;
    b.lead = true;
    b.smooth_max = 6.f;
    g_one_way = 0.05;
    auto x_at = [](double st) { return st < 3.0 ? (float)st * 9.f : (float)(6.0 - st) * 9.f; };
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 180; i++) {
        double st = i / 30.0;
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.mode = 0x1654C01;
        s.world = 7;
        s.flags = PF_IN_WORLD | PF_MOVING;
        s.pos = {x_at(st), 0.f, 0.f};
        s.vel = {st < 3.0 ? 9.f : -9.f, 0.f, 0.f};
        arr.push_back({10.0 + st + 0.05 + (st >= 2.95 && st < 3.3 ? 0.35 : 0.0), s});
    }
    std::stable_sort(arr.begin(), arr.end(), [](auto& a, auto& c) { return a.first < c.first; });
    size_t k = 0;
    PlayerState o;
    double over = 0, known = -1;
    for (double now = 10.0; now < 15.9; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now) {
            if (known < 0 && arr[k].second.vel.x < 0)
                known = now;
            b.push(arr[k].second, arr[k].first), k++;
        }
        if (!b.sample(now, o))
            continue;
        if (known >= 0 && std::fabs(o.pos.x - x_at(now - 10.0)) > 2.f)
            over += 1.0 / 30;
    }
    g_lead_keep_fix = was;
    g_one_way = was_ow;
    return over;
}


// BUG-0674 (drive.lead_fall): a member walking +x at 9 m/s turns round (turn = true) or stops (false) at sender time 3.0;
// the states of [2.95, 3.3) come 0.35 s late (a spike), the rest 0.05 s. Returns the worst distance of the shown target
// from the sender's body (its now, where the lead shows it) over the whole run; *straight: the same for a walk that
// goes straight on through the spike (the cost)
static float lead_fall_err(bool fall, int kind)
{
    const bool was = g_lead_fall_fix;
    const double was_ow = g_one_way;
    g_lead_fall_fix = fall && k4_on("drive.lead_fall");
    SnapshotBuffer b;
    b.delay = 0.2;
    b.lead = true;
    b.smooth_max = 6.f;
    g_one_way = 0.05;
    auto x_at = [kind](double st) {
        if (st < 3.0 || kind == 2)
            return (float)st * 9.f;
        return kind == 0 ? (float)(6.0 - st) * 9.f : 27.f;
    };
    auto v_at = [kind](double st) { return st < 3.0 || kind == 2 ? 9.f : kind == 0 ? -9.f : 0.f; };
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 180; i++) {
        double st = i / 30.0;
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.mode = 0x1654C01;
        s.world = 7;
        s.flags = PF_IN_WORLD | (v_at(st) != 0 ? PF_MOVING : 0);
        s.pos = {x_at(st), 0.f, 0.f};
        s.vel = {v_at(st), 0.f, 0.f};
        arr.push_back({10.0 + st + 0.05 + (st >= 2.95 && st < 3.3 ? 0.35 : 0.0), s});
    }
    std::stable_sort(arr.begin(), arr.end(), [](auto& a, auto& c) { return a.first < c.first; });
    size_t k = 0;
    PlayerState o;
    float worst = 0;
    for (double now = 10.0; now < 15.9; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now)
            b.push(arr[k].second, arr[k].first), k++;
        if (!b.sample(now, o) || now < 12.0)
            continue;
        worst = std::max(worst, std::fabs(o.pos.x - x_at(now - 10.0)));
    }
    g_lead_fall_fix = was;
    g_one_way = was_ow;
    return worst;
}

// BUG-0674 (drive.lead_fall) on VelocityEstimate (a Space ship's copy, a Civ vehicle): a ship at 28 m/s, its states every
// 0.1 s, 0.03 s late; it stops at sender time 5.0 while the states of [4.9, 5.4) come 0.4 s late. The worst distance of
// at() from the ship (its sender time now) after the stop
static double ship_fall_err(bool fall)
{
    const bool was = g_lead_fall_fix;
    g_lead_fall_fix = fall && k4_on("drive.lead_fall");
    VelocityEstimate e;
    e.max_ahead = 0.4;
    auto x_at = [](double st) { return st < 5.0 ? st * 28.0 : 140.0; };
    std::vector<std::pair<double, double>> arr; // arrival, sender time
    for (int i = 0; i < 90; i++) {
        double st = i * 0.1;
        arr.push_back({100.0 + st + 0.03 + (st >= 4.9 && st < 5.4 ? 0.4 : 0.0), st});
    }
    std::stable_sort(arr.begin(), arr.end());
    size_t k = 0;
    double worst = 0;
    for (double now = 100.0; now < 108.5; now += 1 / 60.0) {
        while (k < arr.size() && arr[k].first <= now) {
            e.update({(float)x_at(arr[k].second), 0, 0}, arr[k].first, 2000 + arr[k].second);
            k++;
        }
        if (now > 104.0)
            worst = std::max(worst, std::fabs((double)e.at(now, 0.03).x - x_at(now - 100.0)));
    }
    g_lead_fall_fix = was;
    return worst;
}

// BUG-0675 (drive.lead_throw): an NPC thrown at 20 m/s (its states every 1/30 s report that speed), the states over the
// player network (45 ms, a loss burst: one state in three comes): the longest time (s) the shown target stands still while the NPC flies
static double throw_stand(bool fix)
{
    const bool was = g_lead_throw_fix;
    const double was_ow = g_one_way;
    g_lead_throw_fix = fix && k4_on("drive.lead_throw");
    g_one_way = 0.045;
    SnapshotBuffer b;
    b.delay = 0.15;
    b.lead = true;
    auto x_at = [](double st) { return st < 3.0 ? st * 4.0 : 12.0 + (st - 3.0) * 20.0; };
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 120; i++) {
        double st = i / 30.0;
        if (st > 3.0 && i % 3 != 0)
            continue; // lost (a loss burst: one state in three comes)
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.pos = {(float)x_at(st), 0.f, 0.f};
        s.vel = {st < 3.0 ? 4.f : 20.f, 0.f, 0.f};
        arr.push_back({10.0 + st + 0.045, s});
    }
    size_t k = 0;
    PlayerState o;
    double stand = 0, longest = 0;
    float last = -1e9f;
    for (double now = 10.0; now < 14.0; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now)
            b.push(arr[k].second, arr[k].first), k++;
        if (!b.sample(now, o))
            continue;
        if (now > 13.1 && now < 13.9) {
            stand = std::fabs(o.pos.x - last) < 0.05f ? stand + 1.0 / 30 : 0;
            longest = std::max(longest, stand);
        }
        last = o.pos.x;
    }
    g_lead_throw_fix = was;
    g_one_way = was_ow;
    return longest;
}

// BUG-0651 (drive.lead_low): a member buffer whose mean one-way latency reads 140 ms (spikes, a loaded PC) while the
// link's own latency (the round trip's lower quartile) is 45 ms keeps the lead; the mobile network (100 ms) does not
static bool lead_off_with(bool low_fix, double mean_ow, double low_ow)
{
    const bool was = g_lead_low_fix;
    const double was_ow = g_one_way, was_low = g_one_way_low;
    g_lead_low_fix = low_fix && k4_on("drive.lead_low");
    g_one_way = mean_ow;
    g_one_way_low = low_ow;
    SnapshotBuffer b;
    b.lead = true;
    b.smooth_max = 6.f;
    for (int i = 0; i < 100; i++) {
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + i * 33);
        s.flags = PF_IN_WORLD;
        b.push(s, 10.0 + i * 0.033 + mean_ow);
    }
    bool off = b.lead_off();
    g_lead_low_fix = was;
    g_one_way = was_ow;
    g_one_way_low = was_low;
    return off;
}


// BUG-0680 (drive.puppet_fall): a partner walking +x at 4.75 m/s stops at sender time 3.0; its states of [2.9, 3.3) come
// 0.25 s late (a spike). The puppet's buffer (interpolated, delay 0.1): how far the shown target goes past the stop and
// whether its velocity falls under 0.3 m/s meanwhile (creature.puppet_stop's hold)
static float puppet_stop_past(bool fix, bool* slowed)
{
    const bool was = g_puppet_fall_fix;
    const double was_ow = g_one_way;
    g_puppet_fall_fix = fix && k4_on("drive.puppet_fall");
    g_one_way = 0.05;
    SnapshotBuffer b;
    b.delay = 0.1;
    b.interp_fall = true;
    auto x_at = [](double st) { return st < 3.0 ? (float)st * 4.75f : 14.25f; };
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 150; i++) {
        double st = i / 30.0;
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.flags = PF_IN_WORLD;
        s.pos = {x_at(st), 0.f, 0.f};
        s.vel = {st < 3.0 ? 4.75f : 0.f, 0.f, 0.f};
        arr.push_back({10.0 + st + 0.05 + (st >= 2.9 && st < 3.3 ? 0.25 : 0.0), s});
    }
    std::stable_sort(arr.begin(), arr.end(), [](auto& a, auto& c) { return a.first < c.first; });
    size_t k = 0;
    PlayerState o;
    float past = 0;
    *slowed = false;
    for (double now = 10.0; now < 14.5; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now)
            b.push(arr[k].second, arr[k].first), k++;
        if (!b.sample(now, o) || now < 12.0)
            continue;
        past = std::max(past, o.pos.x - 14.25f);
        if (o.pos.x > 13.f && o.pos.x < 14.25f + 1.f && std::fabs(o.vel.x) < 0.3f && std::fabs(o.vel.x) > 0.f)
            *slowed = true;
    }
    g_puppet_fall_fix = was;
    g_one_way = was_ow;
    return past;
}


// BUG-0675 / BUG-0651 (drive.lead_throw): a member walking at 9.5 m/s whose loaded sender stamps its states off its frames
// (positions every 1/30 s, the times alternately 6 ms early and late: every other pair of states 21 ms apart in its time,
// a 15 m/s path): the mean distance of the shown target from the member (its sender time now)
static double jitter_err(bool fix)
{
    const bool was = g_lead_throw_fix;
    const double was_ow = g_one_way;
    g_lead_throw_fix = fix && k4_on("drive.lead_throw");
    g_one_way = 0.045;
    SnapshotBuffer b;
    b.delay = 0.2;
    b.lead = true;
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 150; i++) {
        double st = i / 30.0, ts = st + (i % 2 ? 0.006 : -0.006);
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + ts * 1000.0 + 0.5);
        s.flags = PF_IN_WORLD | PF_MOVING;
        s.pos = {(float)(st * 9.5), 0.f, 0.f};
        s.vel = {9.5f, 0.f, 0.f};
        arr.push_back({10.0 + st + 0.045, s});
    }
    size_t k = 0;
    PlayerState o;
    int n = 0;
    double sum = 0;
    for (double now = 10.0; now < 14.8; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now)
            b.push(arr[k].second, arr[k].first), k++;
        if (!b.sample(now, o) || now < 11.0)
            continue;
        n++;
        sum += std::fabs(o.pos.x - (now - 10.0) * 9.5);
    }
    g_lead_throw_fix = was;
    g_one_way = was_ow;
    return n ? sum / n : 99.0;
}


// BUG-0674 (drive.lead_fall): an NPC proxy walking +x at 6 m/s turns 90 degrees at sender time 3.0 while the states of
// [2.95, 3.3) come 0.35 s late: the largest step of the shown target in one 1/30 s frame (its driving teleports onto a
// target that jumps over snap / 2)
static float proxy_turn_step(bool fix)
{
    const bool was = g_lead_fall_fix;
    const double was_ow = g_one_way;
    g_lead_fall_fix = fix && k4_on("drive.lead_fall");
    g_one_way = 0.05;
    SnapshotBuffer b;
    b.delay = 0.15;
    b.lead = true;
    b.late_lead = true;
    b.smooth_max = 8.f;
    auto p_at = [](double st) { return st < 3.0 ? Vec3{(float)st * 6.f, 0, 0} : Vec3{18.f, (float)(st - 3.0) * 6.f, 0}; };
    std::vector<std::pair<double, PlayerState>> arr;
    for (int i = 0; i < 180; i++) {
        double st = i / 30.0;
        PlayerState s;
        s.time_ms = (uint32_t)(1000 + st * 1000.0 + 0.5);
        s.flags = PF_IN_WORLD | PF_MOVING;
        s.pos = p_at(st);
        s.vel = st < 3.0 ? Vec3{6.f, 0, 0} : Vec3{0, 6.f, 0};
        arr.push_back({10.0 + st + 0.05 + (st >= 2.95 && st < 3.3 ? 0.35 : 0.0), s});
    }
    std::stable_sort(arr.begin(), arr.end(), [](auto& a, auto& c) { return a.first < c.first; });
    size_t k = 0;
    PlayerState o;
    float mx = 0;
    Vec3 last{};
    bool have = false;
    for (double now = 10.0; now < 15.9; now += 1.0 / 30) {
        while (k < arr.size() && arr[k].first <= now)
            b.push(arr[k].second, arr[k].first), k++;
        if (!b.sample(now, o))
            continue;
        if (have && now > 12.0)
            mx = std::max(mx, dist(last, o.pos));
        last = o.pos;
        have = true;
    }
    g_lead_fall_fix = was;
    g_one_way = was_ow;
    return mx;
}

static void test_lead_fall()
{
    printf("remote bodies moved on through a latency spike (BUG-0674, BUG-0675, BUG-0651)\n");
    const char* what[] = {"turn round", "stop", "straight on"};
    for (int kind = 0; kind < 3; kind++) {
        float on = lead_fall_err(true, kind), off = lead_fall_err(false, kind);
        printf("  member %s in a 0.35 s spike: worst target error fall %.2f m, 0.7.10 %.2f m\n", what[kind], on, off);
        if (kind == 0) {
            CHECK(off > 5.5f); // ~2 x 9 m/s x the gap
            CHECK(on < 4.5f);
        } else if (kind == 1) {
            CHECK(off > 2.5f); // 9 m/s x the gap
            CHECK(on < 2.0f);
        } else
            CHECK(on < 2.0f); // the cost: behind for the rest of the gap, caught up
    }
    for (double spike_end : {3.3, 3.65}) {
        float e_on = 0, e_off = 0;
        float on = lead_late_max_step(true, spike_end, &e_on, true), off = lead_late_max_step(false, spike_end, &e_off, true);
        printf("  an NPC proxy's walk known late (spike to %.2f s): max target step %.2f m (end error %.2f), 0.7.10 %.2f m\n",
               spike_end, on, e_on, off);
        CHECK(off > 2.f);
        CHECK(on < 0.7f);
        CHECK(e_on < 0.5f);
    }
    float pt_on = proxy_turn_step(true), pt_off = proxy_turn_step(false);
    printf("  an NPC proxy turning inside a 0.35 s spike: max target step %.2f m per frame (0.7.10 %.2f m)\n", pt_on, pt_off);
    CHECK(pt_off > 1.25f); // over snap / 2 of the proxies' driving (2.5 m)
    CHECK(pt_on < 0.8f);
    double s_on = ship_fall_err(true), s_off = ship_fall_err(false);
    printf("  a 28 m/s ship that stops in a 0.4 s spike: worst copy error fall %.1f m, 0.7.10 %.1f m\n", s_on, s_off);
    CHECK(s_off > 7.5);
    CHECK(s_on < 6.0);
    double t_on = throw_stand(true), t_off = throw_stand(false);
    printf("  a thrown NPC (20 m/s, two of three states lost): the target stands up to %.2f s (0.7.10 %.2f s)\n", t_on, t_off);
    CHECK(t_off >= 0.06);
    CHECK(t_on < 0.04);
    double j_on = jitter_err(true), j_off = jitter_err(false);
    printf("  a 9.5 m/s member stamped 6 ms off its frames: mean target error %.2f m (0.7.10 %.2f m)\n", j_on, j_off);
    CHECK(j_off > 0.18);
    CHECK(j_on < 0.1);
    CHECK(lead_off_with(false, 0.14, 0.045));  // 0.7.10: the mean reads the spikes and frame waits: no lead
    CHECK(!lead_off_with(true, 0.14, 0.045));  // the player network under load keeps it
    CHECK(lead_off_with(true, 0.14, 0.10));    // the mobile network does not
    CHECK(!lead_off_with(true, 0.06, -1));     // no samples yet: the mean as before
    bool sl_on = false, sl_off = false;
    float p_on = puppet_stop_past(true, &sl_on), p_off = puppet_stop_past(false, &sl_off);
    printf("  a puppet whose partner stops in a 0.25 s spike: target past the stop %.2f m (0.7.10 %.2f m), slowed %d\n", p_on,
           p_off, (int)sl_on);
    CHECK(p_off > 0.5f);
    CHECK(p_on < 0.3f);
    CHECK(sl_on && !sl_off);
}

static void test_lead_keep()
{
    {
        double on = lead_turn_over2(true), off = lead_turn_over2(false);
        printf("  a turn round inside a spike: the target over 2 m off for %.2f s after the news (0.7.9 %.2f s)\n", on, off);
        CHECK(off > 0.4);
        CHECK(on < 0.1);
    }
    {
        double b_on, b_off;
        float on = lead_ratchet_err(true, &b_on), off = lead_ratchet_err(false, &b_off);
        printf("  speed changes on a jittery 45 ms link: mean target error keep %.2f m (behind the lead p90 %.3f s), "
               "0.7.9 %.2f m (%.3f s)\n", on, b_on, off, b_off);
    }
    printf("member lead over a 110 ms link and after a start known late (BUG-0651, BUG-0410)\n");
    double c_on, c_off;
    // a 110 ms one-way link (the player network under load reads 100-115 ms): 0.7.9 showed the members interpolated
    float on = lead_keep_err(true, 0.11, 0.0, &c_on), off = lead_keep_err(false, 0.11, 0.0, &c_off);
    printf("  one-way 110 ms: mean target error keep %.2f m, 0.7.9 %.2f m\n", on, off);
    CHECK(off > 1.5f); // 0.2 s + 0.11 s late at 9 m/s
    CHECK(on < 0.3f);
    // over 130 ms (the mobile network) no lead with the fix either
    on = lead_keep_err(true, 0.14, 0.0, &c_on);
    CHECK(on > 1.5f);
    // a 45 ms link, the walk's start known 0.3 s late (a spike of the player network): caught up at up to 2x, not 1.33x
    on = lead_keep_err(true, 0.045, 0.3, &c_on);
    off = lead_keep_err(false, 0.045, 0.3, &c_off);
    printf("  start 0.3 s late: within 0.3 m of the body %.2f s after it is known (0.7.9 %.2f s); mean error %.2f / %.2f m\n",
           c_on, c_off, on, off);
    CHECK(c_off > 0.8);
    CHECK(c_on < 0.7);
    CHECK(on < 0.15f);
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

// BUG-0405: the world sent to a guest that loads. The guest reads its socket once per frame (frames of
// 200-950 ms while it loads), up to 256 packets per update, and acknowledges at the end of the frame:
// the ack bits covered the last 128 packets only and the sender's rto (from a 5-80 ms rtt) fired long
// before the guest read the rest, so 50-90 % of the packets went out twice on a clean loopback.
struct BusyResult {
    int got = 0;
    double secs = 0;
    uint32_t sent = 0, resends = 0;
};
static BusyResult bulk_busy_case(bool fix, bool busy, uint32_t delay, float loss, int seed)
{
    bool was = g_fix.bulk_acks;
    g_fix.bulk_acks = fix;
    struct Pk {
        uint32_t t;
        std::vector<uint8_t> d;
    };
    Connection a, b;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0, 1);
    std::deque<Pk> ab, ba; // in flight (constant delay: in order)
    auto send = [&](std::deque<Pk>& q, const std::vector<uint8_t>& p, uint32_t now) {
        if (U(rng) >= loss && q.size() < 3600) // udp.cpp: a 4 MB receive buffer, the rest is dropped
            q.push_back({now + delay, p});
    };
    const int N = 3400; // a tribe world: ~3.4 MB in 1 KB chunks
    int queued = 0, got = 0;
    bool ordered = true;
    uint32_t now = 1000, guest_next = 1000;
    std::vector<uint8_t> pkt;
    for (; got < N && now < 1000 + 120000; now += 1) {
        if (now % 33 == 0) { // the host's frame: world.cpp's window, Session::flush's burst
            size_t window = a.rtt_ms < 40 ? 600 : 160;
            while (queued < N && a.pending_reliable() < window) {
                Writer w;
                w.u32(queued++);
                std::vector<uint8_t> pad(1010, 0x55);
                w.raw(pad.data(), pad.size());
                a.send_reliable(42, w);
            }
            int burst = a.rtt_ms < 40.f ? (int)kAckWindow : (int)kAckWindow / 3;
            for (int i = 0; i < burst && a.build_packet(now, pkt); i++)
                send(ab, pkt, now);
        }
        while (!ba.empty() && ba.front().t <= now) { // the host reads in its frame too, but often
            a.on_packet(ba.front().d.data(), ba.front().d.size(), now, [](uint8_t, Reader&) {});
            ba.pop_front();
        }
        if (now >= guest_next) { // the guest's frame: Session::update, then Session::flush
            for (int i = 0; i < 256 && !ab.empty() && ab.front().t <= now; i++) {
                b.on_packet(ab.front().d.data(), ab.front().d.size(), now, [&](uint8_t type, Reader& r) {
                    if (type == 42 && r.u32() != (uint32_t)got++)
                        ordered = false;
                });
                ab.pop_front();
                if (b.ack_urgent() && b.build_packet(now, pkt))
                    send(ba, pkt, now);
            }
            while (b.build_packet(now, pkt))
                send(ba, pkt, now);
            guest_next = now + (busy ? 200 + (uint32_t)(U(rng) * 750) : 33);
        }
    }
    g_fix.bulk_acks = was;
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
    // BUG-0392: the release page when the API refuses; asset addresses only on github.com (or the test server)
    const std::string api = "https://api.github.com/repos/WoodyWizard/SporeCoop/releases/latest",
                      web = "https://github.com/WoodyWizard/SporeCoop/releases/latest";
    CHECK(upd::web_latest_url(api) == web);
    CHECK(upd::web_latest_url("http://127.0.0.1:47999/repos/o/r/releases/latest") == "http://127.0.0.1:47999/o/r/releases/latest");
    CHECK(upd::web_latest_url("https://evil.example/repos/o/r/releases/latest").empty());
    CHECK(upd::web_latest_url("http://127.0.0.1:9/").empty() && upd::web_latest_url("http://127.0.0.1:47999/latest").empty());
    CHECK(upd::web_latest_url("https://api.github.com/repos/o/r/x/releases/latest").empty());
    CHECK(upd::release_from_redirect(web, "https://github.com/WoodyWizard/SporeCoop/releases/tag/v0.7.3.3", r) &&
          r.version == "0.7.3.3" && r.from_web &&
          r.pack_url == "https://github.com/WoodyWizard/SporeCoop/releases/download/v0.7.3.3/SporeCoop-0.7.3.3.pack" &&
          r.sums_url == "https://github.com/WoodyWizard/SporeCoop/releases/download/v0.7.3.3/SHA256SUMS.txt");
    // a relative target; the assets stay on github.com whatever host the redirect names
    CHECK(upd::release_from_redirect(web, "/WoodyWizard/SporeCoop/releases/tag/v0.8.0?x=1", r) && r.version == "0.8.0");
    CHECK(upd::release_from_redirect(web, "https://evil.example/WoodyWizard/SporeCoop/releases/tag/v0.8.0", r) &&
          r.pack_url.rfind("https://github.com/WoodyWizard/SporeCoop/releases/download/v0.8.0/", 0) == 0);
    for (const char* bad : {"https://github.com/WoodyWizard/SporeCoop/releases", "/releases/tag/", "/releases/tag/v1/../x",
                            "/releases/tag/v1%2F..", "/releases/tag/vX", "/releases/tag/v..1"})
        CHECK(!upd::release_from_redirect(web, bad, r));
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

// BUG-0612: an unreliable message that can never fit a packet is counted (with its type and size), and the
// messages queued after it still go out
static void test_unreliable_too_big()
{
    printf("an unreliable message too big for any packet is counted, the rest sent (BUG-0612)\n");
    Connection a, b;
    Writer big, small;
    for (int i = 0; i < 1300; i++)
        big.u8((uint8_t)i);
    small.u32(7);
    a.send_unreliable(MSG_PLAYER_STATE, big);
    a.send_unreliable(MSG_CHAT, small);
    std::vector<uint8_t> pkt;
    int got_small = 0, got_big = 0;
    uint32_t now = 1000;
    for (int i = 0; i < 4 && a.build_packet(now, pkt); i++)
        b.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader& m) {
            got_small += t == MSG_CHAT && m.left() == 4;
            got_big += t == MSG_PLAYER_STATE;
        });
    CHECK(got_small == 1 && got_big == 0);
    CHECK(a.stats.unrel_too_big == 1 && a.stats.unrel_too_big_type == MSG_PLAYER_STATE && a.stats.unrel_too_big_size == 1300);
    // the largest message that fits is sent
    Writer edge;
    for (int i = 0; i < (int)kMaxPacket - 31 - 4; i++)
        edge.u8(1);
    a.send_unreliable(MSG_CHAT, edge);
    int got_edge = 0;
    for (int i = 0; i < 4 && a.build_packet(now + 10, pkt); i++)
        b.on_packet(pkt.data(), pkt.size(), now + 10, [&](uint8_t t, Reader& m) { got_edge += t == MSG_CHAT && m.left() == edge.size(); });
    CHECK(got_edge == 1 && a.stats.unrel_too_big == 1);
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

// BUG-0186: a Civ vehicle standing still that the sender's game moves 20 units at once (one step):
// without a cap the step became a 120 u/s speed and the mirror was put up to ~30 units past it
static double jump_overshoot(float max_speed)
{
    VelocityEstimate e;
    e.max_speed = max_speed;
    double worst = 0;
    for (int k = 0; k < 30; k++) {
        double sent = k * 0.1, recv = 1000 + sent + 0.03;
        float x = k >= 10 ? 20.f : 0.f; // put 20 units further between snapshots 9 and 10, then still
        e.update({x, 0, 0}, recv, 5000 + sent);
        for (double t = recv; t < recv + 0.1; t += 1 / 60.0)
            worst = std::max(worst, (double)std::fabs(e.at(t).x - x));
    }
    return worst;
}

static void test_velocity_jump_cap()
{
    printf("vehicle velocity after a jump of the sender's game (BUG-0186)\n");
    double old = jump_overshoot(0), capped = jump_overshoot(2 * 8.f + 10.f);
    printf("  max overshoot: %.1f units without a cap, %.1f with it\n", old, capped);
    CHECK(old > 10.0);  // the bug: the step became a speed
    CHECK(capped < 1.0);
    // a vehicle at its top speed is not capped
    VelocityEstimate e;
    e.max_speed = 2 * 8.f + 10.f;
    for (int k = 0; k < 20; k++)
        e.update({8.f * k * 0.1f, 0, 0}, 1000 + k * 0.1 + 0.03, 5000 + k * 0.1);
    CHECK(e.capped == 0 && std::fabs(e.vel.x - 8.f) < 0.5f);
}

// BUG-0186 (b): a fast vehicle (25 u/s) whose sender hitches for 0.5 s (no frames: the vehicle stands
// still on the host meanwhile), then goes on; and the same vehicle with one packet 150 ms late
static double hitch_error(double ahead, bool hitch)
{
    const float S = 25.f;
    VelocityEstimate e;
    e.max_ahead = ahead;
    double worst = 0, sent = 0, x = 0;
    for (int k = 0; k < 40; k++) {
        double gap = hitch && k == 20 ? 0.5 : 0.1; // the sender's time between snapshots
        double move_dt = hitch && k == 20 ? 0.1 : gap;  // in a hitch the vehicle moved one frame's worth
        sent += gap;
        x += S * move_dt;
        double recv = 1000 + sent + 0.03 + (!hitch && k == 20 ? 0.15 : 0);
        e.update({(float)x, 0, 0}, recv, 5000 + sent);
        double next_recv = 1000 + sent + 0.03 + (hitch && k == 19 ? 0.5 : 0.1);
        for (double t = recv; t < next_recv && k >= 5; t += 1 / 60.0) {
            // the truth at local time t: standing during the hitch, else moving on
            double truth = x + (hitch && k == 19 ? 0 : S * std::max(0.0, t - recv));
            worst = std::max(worst, std::fabs((double)e.at(t).x - truth));
        }
    }
    return worst;
}

static void test_velocity_hitch()
{
    printf("vehicle position after a hitch of the sender (BUG-0186)\n");
    const bool was_fall = g_lead_fall_fix;
    g_lead_fall_fix = false; // (the bug as it was: BUG-0674's fall bounds it too)
    double old_h = hitch_error(0.4, true);
    g_lead_fall_fix = was_fall;
    double new_h = hitch_error(0.15, true);
    double old_l = hitch_error(0.4, false), new_l = hitch_error(0.15, false);
    printf("  sender hitch 0.5 s: max error %.1f units with 0.4 s ahead, %.1f with 0.15 s; one packet 150 ms late: %.1f / %.1f\n",
           old_h, new_h, old_l, new_l);
    CHECK(old_h > 8.0);  // the bug: thrown 10 units past the standing vehicle
    CHECK(new_h < 5.0);
    CHECK(new_l < 3.0);  // a late packet costs little
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

// BUG-0210: a host on the network twice (cable and Wi-Fi) answers a broadcast query from both
// addresses: one game in the list, not two; two hosts that look alike stay two
static void test_discovery_dedup()
{
    printf("LAN game list: one host with two addresses is one game (BUG-0210)\n");
    Discovery d;
    CHECK(d.start());
    d.targets = {};
    auto rep = [](uint32_t id, bool has_id = true) {
        DiscoverReply r;
        r.port = kDefaultPort;
        r.name = "Alice";
        r.players = 2;
        r.max_players = 4;
        r.info = "Space - Madas";
        r.has_id = has_id;
        r.host_id = id;
        return r;
    };
    // what goes over the wire: the id survives a write and a read
    {
        Writer w;
        rep(0xA11CE).write(w);
        Reader r(w.data.data(), w.size());
        DiscoverReply x;
        CHECK(x.read(r) && x.has_id && x.host_id == 0xA11CE && r.left() == 0);
        Writer w0; // an older host's answer (no id)
        DiscoverReply o = rep(0, false);
        o.write(w0);
        Reader r0(w0.data.data(), w0.size());
        CHECK(x.read(r0) && !x.has_id && x.name == "Alice");
    }
    const Endpoint wired{0x7101A8C0, 0}, wifi{0x8A01A8C0, 0}; // 192.168.1.113 / .138
    uint32_t now = 50000;
    for (int i = 0; i < 6; i++) { // a few query rounds, both addresses answer each
        d.on_reply(wired, rep(0xA11CE), now + i * 1500);
        d.on_reply(wifi, rep(0xA11CE), now + i * 1500 + 1);
    }
    now += 6 * 1500;
    auto g = d.games(now);
    printf("  one host, two addresses: listed %zu\n", g.size());
    CHECK(g.size() == 1);
    CHECK(!g.empty() && g[0].host.ip == wired.ip && g[0].host.port == kDefaultPort); // stays on the first
    // the wired cable is pulled: after a few seconds the entry shows the address that answers
    for (int i = 0; i < 4; i++)
        d.on_reply(wifi, rep(0xA11CE), now + i * 1500);
    now += 4 * 1500;
    g = d.games(now);
    CHECK(g.size() == 1 && g[0].host.ip == wifi.ip);
    // another host of the same name, port and stage (another game id): its own row
    d.on_reply(Endpoint{0x0A01A8C0, 0}, rep(0xB0B), now);
    CHECK(d.games(now).size() == 2);
    // older hosts (no id) on two addresses: as before, two rows (nothing tells them apart)
    Discovery o;
    CHECK(o.start());
    o.targets = {};
    o.on_reply(wired, rep(0, false), now);
    o.on_reply(wifi, rep(0, false), now);
    CHECK(o.games(now).size() == 2);
    // the loopback rule stays: loopback then LAN of one host is one row with the LAN address
    Discovery l;
    CHECK(l.start());
    l.targets = {};
    l.on_reply(Endpoint{0x0100007F, 0}, rep(7), now);
    l.on_reply(wired, rep(7), now);
    l.on_reply(wifi, rep(7), now);
    g = l.games(now);
    CHECK(g.size() == 1 && g[0].host.ip == wired.ip);
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

// ---- support diagnostics (net/diagmsg.h, docs/REPORTING.md#reporting-support) ----
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
    CHECK(d2.scale == 0); // BUG-0606: not in Cell
    d.scale = 1.5f;
    Writer ws;
    d.write(ws);
    Reader rs(ws.data.data(), ws.size());
    dg::Digest d4;
    CHECK(rs.u8() == dg::kDigest && d4.read(rs) && d4.scale == 1.5f && d4.out.size() == 2);
    Writer wo; // an older sender's digest (no scale at the end) reads scale 0
    d.write(wo);
    wo.data.resize(wo.data.size() - 4);
    Reader ro(wo.data.data(), wo.size());
    dg::Digest d5;
    CHECK(ro.u8() == dg::kDigest && d5.read(ro) && d5.scale == 0 && d5.out.size() == 2);
    d.scale = 0;
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
    h.entries.push_back({dg::make_key('c', 6), 300, 0, 0}); // around another player, 250 m away (BUG-0298)
    g.entries[2].x = 40;                                   // n1 30 m off
    g.totals[0].second = 80;
    g.vote = 1;
    c = dg::compare(h, g);
    std::set<std::string> ids;
    for (auto& i : c.issues)
        ids.insert(i.id);
    CHECK(ids.count("extra:n9") && ids.count("missing:n4") && ids.count("missing:c5") && ids.count("pos:n1"));
    CHECK(ids.count("total:1") && ids.count("vote") && !ids.count("missing:n2") && ids.size() == 6);
    {   // a guest is sent only its nearest lim.cell_sent cells (BUG-0295): the others are not missing
        dg::Limits few;
        few.cell_sent = 0;
        bool c5 = false;
        for (auto& i : dg::compare(h, g, few).issues)
            c5 |= i.id == "missing:c5";
        CHECK(!c5);
    }
    {   // BUG-0379: in a crowded pool the cells near rank 900 depend on the centre the host used
        dg::Digest hc, gc;
        hc.mode = gc.mode = 0x1654C00;
        hc.world = gc.world = 7;
        gc.self = 1;
        hc.entries.push_back({dg::make_key('p', 1), 0, 0, 0});
        gc.entries.push_back({dg::make_key('p', 1), 0, 0, 0});
        for (uint32_t i = 0; i < 1000; i++)
            hc.entries.push_back({dg::make_key('c', 100 + i), 0.05f * (float)i, 0, 0});
        for (uint32_t i = 0; i < 906; i++) // the host's 900 picked a little off: 897-899 not sent, 900-905 sent
            if (i < 897 || i >= 900)
                gc.entries.push_back({dg::make_key('c', 100 + i), 0.05f * (float)i, 0, 0});
        auto missing = [&](const dg::Limits& l) {
            int n = 0;
            for (auto& i : dg::compare(hc, gc, l).issues)
                n += i.cat == "missing";
            return n;
        };
        dg::Limits old;
        old.cell_sure = 1.f;
        CHECK(missing(dg::Limits{}) == 0 && missing(old) == 3);
        gc.entries.erase(gc.entries.begin() + 10); // a near cell the guest lacks: missing either way
        CHECK(missing(dg::Limits{}) == 1 && missing(old) == 4);
    }
    {   // BUG-0379: a guest total the host had a moment before it asked is no difference (sent late)
        std::vector<std::pair<uint8_t, float>> recent = {{dg::kTotalDna, 90}, {dg::kTotalDna, 80}};
        bool dna = false;
        for (auto& i : dg::compare(h, g, {}, &recent).issues)
            dna |= i.id == "total:1";
        CHECK(!dna);
        recent = {{dg::kTotalDna, 90}, {dg::kTotalCellFood, 80}}; // another total's value does not count
        dna = false;
        for (auto& i : dg::compare(h, g, {}, &recent).issues)
            dna |= i.id == "total:1";
        CHECK(dna);
    }
    {   // BUG-0430: what the windows let through is late only when the host left that value a while ago, or the
        // missing cells stood in the band at the check before; the normal lag of a changing value is not
        dg::Digest gl = g;
        gl.entries.erase(std::remove_if(gl.entries.begin(), gl.entries.end(),
                                        [](const dg::Entry& e) { return e.key == dg::make_key('n', 9); }),
                         gl.entries.end());
        // a steadily rising total (host 100, guest 80 = one update back, still among the last moment's values)
        std::vector<std::pair<uint8_t, float>> recent = {{dg::kTotalDna, 70}, {dg::kTotalDna, 80}, {dg::kTotalDna, 90}};
        std::vector<std::pair<uint8_t, float>> fresh = {{dg::kTotalDna, 80}, {dg::kTotalDna, 90}};
        auto no_total = [](const dg::Compared& x) {
            return std::none_of(x.issues.begin(), x.issues.end(), [](const dg::Issue& i) { return i.cat == "total"; });
        };
        dg::Compared rising = dg::compare(h, gl, {}, &recent, {&fresh, nullptr});
        CHECK(no_total(rising) && rising.late_totals == 0 && !rising.late());
        // the host went 80 -> 100 2 s ago and stood still; the guest still shows 80: late
        std::vector<std::pair<uint8_t, float>> stood = {{dg::kTotalDna, 100}};
        dg::Compared lc = dg::compare(h, gl, {}, &recent, {&stood, nullptr});
        CHECK(no_total(lc) && lc.late_totals == 1 && lc.late());
        CHECK(dg::compare(h, gl, {}, &recent).late_totals == 0); // no Late: never late (the switch off)
        dg::Compared on_time = dg::compare(h, h);
        CHECK(on_time.late_totals == 0 && !on_time.late());
        int run = 0;
        CHECK(!dg::late_streak(run, lc) && !dg::late_streak(run, lc) && dg::late_streak(run, lc) && run == 3);
        CHECK(!dg::late_streak(run, on_time) && run == 0); // one check in time ends the streak
        CHECK(!dg::late_streak(run, lc) && run == 1);
        // cells: the band between the sure 80 % and the sent part
        dg::Digest hc, gc;
        hc.mode = gc.mode = 0x1654C00;
        hc.world = gc.world = 7;
        gc.self = 1;
        hc.entries.push_back({dg::make_key('p', 1), 0, 0, 0});
        gc.entries.push_back({dg::make_key('p', 1), 0, 0, 0});
        for (uint32_t i = 0; i < 1000; i++)
            hc.entries.push_back({dg::make_key('c', 100 + i), 0.05f * (float)i, 0, 0});
        auto band = [&](uint32_t lacking, const std::set<uint32_t>* steady) {
            dg::Digest gx = gc;
            for (uint32_t i = 0; i < 900 - lacking; i++)
                gx.entries.push_back({dg::make_key('c', 100 + i), 0.05f * (float)i, 0, 0});
            return dg::compare(hc, gx, {}, nullptr, {nullptr, steady});
        };
        dg::Compared first = band(40, nullptr); // the first check: nothing to compare the band with
        CHECK(first.late_band == 180 && first.band.size() == 180 && first.late_cells == 0 && !first.late() && first.issues.empty());
        std::set<uint32_t> changed; // the host's set changed: none of the missing ones stood in the band
        dg::Compared moving = band(40, &changed);
        CHECK(moving.late_cells == 0 && !moving.late());
        dg::Compared few = band(3, &first.band), many = band(40, &first.band);
        CHECK(few.late_cells == 3 && !few.late() && few.issues.empty());
        CHECK(many.late_cells == 40 && many.late() && many.issues.empty());
    }
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
    // no place on both (world 0, Space's galaxy view, BUG-0378): each game has only its own ship and
    // the host's ships are not mirrored there: players and ships are not compared, the rest is
    {
        dg::Digest h4, g4;
        h4.mode = g4.mode = 0x1654C05;
        h4.self = 0;
        g4.self = 1;
        h4.entries = {{dg::make_key('p', 0), 0, 0, 0}, {dg::make_key('s', 1), 9, 0, 0}};
        g4.entries = {{dg::make_key('p', 1), 300, 0, 0}};
        dg::Limits none;
        none.no_place = true;
        CHECK(dg::compare(h4, g4).issues.size() == 3); // the old check: p0 missing, s1 missing, p1 extra
        CHECK(dg::compare(h4, g4, none).issues.empty());
        h4.totals = {{dg::kTotalDna, 100}};
        g4.totals = {{dg::kTotalDna, 50}};
        CHECK(dg::compare(h4, g4, none).issues.size() == 1); // totals still are
    }
    {   // a fast ship's lag allowance (BUG-0378): 22 m apart, flying 90 m/s with ~0.25 s between the reads
        dg::Digest h5, g5;
        h5.mode = g5.mode = 0x1654C05;
        h5.world = g5.world = 0x40AE3044;
        g5.self = 1;
        h5.entries = {{dg::make_key('p', 0), 0, 0, 700}, {dg::make_key('p', 1), 100, 0, 700}};
        g5.entries = {{dg::make_key('p', 0), 22, 0, 700}, {dg::make_key('p', 1), 100, 0, 700}};
        CHECK(dg::compare(h5, g5).issues.size() == 1); // the fixed 10 m
        dg::Limits fast;
        fast.slack[dg::make_key('p', 0)] = 90 * 0.25f;
        CHECK(dg::compare(h5, g5, fast).issues.empty());
        g5.entries[0].x = 60; // far beyond its lag: still a difference
        CHECK(dg::compare(h5, g5, fast).issues.size() == 1);
    }
    {   // BUG-0547: the host's own path: a ship that flew 200 m/s and stopped 0.2 s ago
        std::deque<dg::PathPoint> path;
        for (int i = 0; i <= 60; i++) // 1 s at 60 fps: 0.8 s at 200 m/s along x, then standing at x = 160
            path.push_back({i / 60.0, (float)std::min(i / 60.0 * 200.0, 160.0), 0, 700});
        dg::PathHit h = dg::path_distance(path, 110, 3, 700, 0.0); // the copy 50 m behind, 3 m to the side
        CHECK(std::fabs(h.d - 3) < 0.01f && std::fabs(h.lag - (1.0f - 0.55f)) < 0.02f);
        h = dg::path_distance(path, 110, 3, 700, 0.9); // only the last 0.1 s (standing): 50 m off
        CHECK(h.d > 49 && h.d < 51);
        h = dg::path_distance(path, 160, 40, 700, 0.0); // beside the path: still far
        CHECK(h.d > 39);
        CHECK(dg::path_distance({}, 0, 0, 0, 0).d > 1e20f);
    }
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


// BUG-0559: a part's description and the most similar part (net/partprops)
static std::vector<uint8_t> part_prop(const std::vector<std::tuple<uint32_t, uint16_t, std::vector<uint8_t>>>& props)
{
    std::vector<uint8_t> d;
    auto be = [&](uint32_t v, int n) {
        for (int i = n - 1; i >= 0; i--)
            d.push_back((uint8_t)(v >> (8 * i)));
    };
    be((uint32_t)props.size(), 4);
    for (auto& [id, t, val] : props) {
        be(id, 4);
        be(t, 2);
        be(t == 0x39 ? 0x1C : t == 0x13 ? 0x0D : 0, 2);
        if (t == 0x39) { // an array of one bounding box
            be(1, 4);
            be(24, 4);
        } else if (t == 0x13) {
            be((uint32_t)val.size() / 2, 4);
        }
        d.insert(d.end(), val.begin(), val.end());
    }
    return d;
}

static std::vector<uint8_t> be_int(int v)
{
    return {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
}

static std::vector<uint8_t> box(float s)
{
    std::vector<uint8_t> b(24);
    float f[6] = {-s, -s, -s, s, s, s};
    memcpy(b.data(), f, 24);
    return b;
}

static void test_partprops()
{
    using namespace partprops;
    std::vector<uint8_t> name = {'a', 0, 'b', 0};
    // a carnivore mouth of the pack (bite 3), a small one; a string with flags 0x0D before the ints
    auto want_d = part_prop({{0x04EBB27E, 0x13, name}, {0xB00F0FEC, 0x01, {1}}, {0xB1C3E5B6, 0x09, be_int(3)},
                             {0x02166464, 0x09, be_int(75)}, {0x1CBB2F62, 0x39, box(0.1f)}});
    Part want;
    CHECK(parse(want_d.data(), want_d.size(), 0xF6DE5C63, want));
    CHECK(want.roles.size() == 1 && want.roles[0] == 0xB00F0FEC && want.abil.size() == 1 && want.abil[0].second == 3);
    CHECK(want.cost == 75 && want.has_box && std::fabs(want.size() - 0.3464f) < 0.01f);
    std::vector<Part> have(5);
    auto mk = [&](int i, uint32_t id, std::vector<std::tuple<uint32_t, uint16_t, std::vector<uint8_t>>> pr) {
        auto d = part_prop(pr);
        CHECK(parse(d.data(), d.size(), id, have[i]));
    };
    mk(0, 0x10000001, {{0x02166464, 0x09, be_int(75)}, {0x1CBB2F62, 0x39, box(0.1f)}});                    // a detail
    mk(1, 0x10000002, {{0xB00F0FE9, 0x01, {1}}, {0xB1C3E5B6, 0x09, be_int(3)}, {0x1CBB2F62, 0x39, box(0.1f)}}); // an eye
    mk(2, 0x10000003, {{0xB00F0FEC, 0x01, {1}}, {0xB1C3E5B6, 0x09, be_int(3)}, {0x02166464, 0x09, be_int(75)},
                       {0x1CBB2F62, 0x39, box(1.0f)}});                                                           // a big mouth
    mk(3, 0x10000004, {{0xB00F0FEC, 0x01, {1}}, {0xB1C3E5B6, 0x09, be_int(2)}, {0x02166464, 0x09, be_int(75)},
                       {0x1CBB2F62, 0x39, box(0.12f)}});                                                          // a mouth like it
    mk(4, 0x10FE0005, {{0xB00F0FEC, 0x01, {1}}, {0xB1C3E5B6, 0x09, be_int(3)}, {0x02166464, 0x09, be_int(75)},
                       {0x1CBB2F62, 0x39, box(0.1f)}});                                                           // the same, but an 0xFE byte
    CHECK(nearest(want, have) == 0x10000004); // a mouth stays a mouth, of a similar size, and fits in place
    CHECK(std::isinf(distance(want, have[0])) && std::isinf(distance(want, have[1])));
    have.resize(2);
    CHECK(nearest(want, have) == 0); // no mouth here: none (the caller falls back)
    // untrusted bytes: truncations and a count past the end are refused, never read past the end
    for (size_t n = 0; n < want_d.size(); n++) {
        Part p;
        CHECK(!parse(want_d.data(), n, 1, p));
    }
    std::vector<uint8_t> bad = want_d;
    bad[3] = 0xFF;
    Part p;
    CHECK(!parse(bad.data(), bad.size(), 1, p));
    printf("partprops: ok\n");
}

// ---- 0.7.13 N1: net and session (each FAILS with its switch off: `net_tests --off <name>`, and with --nofix) ----

// a network for one Session: what it sends (with the address) and a queue of datagrams to read (from the address)
struct FakeNet : Transport {
    std::deque<std::pair<Endpoint, std::vector<uint8_t>>> in;
    std::vector<std::pair<Endpoint, std::vector<uint8_t>>> out;
    uint16_t port = 0;
    bool open(uint16_t p, std::string*) override { port = p ? p : 50000; return true; }
    void close() override {}
    bool send_to(const Endpoint& to, const void* d, size_t n) override
    {
        out.push_back({to, std::vector<uint8_t>((const uint8_t*)d, (const uint8_t*)d + n)});
        return true;
    }
    int recv_from(Endpoint& from, void* buf, size_t cap) override
    {
        if (in.empty())
            return 0;
        auto [ep, d] = std::move(in.front());
        in.pop_front();
        size_t n = std::min(cap, d.size());
        if (n)
            memcpy(buf, d.data(), n);
        from = ep;
        return (int)n;
    }
    uint16_t local_port() const override { return port; }
    size_t sent_to(const Endpoint& e) const
    {
        size_t k = 0;
        for (auto& [to, d] : out)
            k += to == e;
        return k;
    }
};
static uint32_t g_fake_ms = 5000;
static uint32_t fake_ms() { return g_fake_ms; }
static Endpoint ep4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    Endpoint e;
    uint8_t* p = reinterpret_cast<uint8_t*>(&e.ip);
    p[0] = a; p[1] = b; p[2] = c; p[3] = d;
    e.port = port;
    return e;
}
// the first datagram of a guest that joins: a real Connection sending its HELLO
static std::vector<uint8_t> hello_datagram(uint32_t conn, const std::string& name, uint32_t build)
{
    Connection c;
    c.conn_id = conn;
    Hello h;
    h.build = build;
    h.name = name;
    Writer w;
    h.write(w);
    c.send_reliable(MSG_HELLO, w);
    std::vector<uint8_t> pkt;
    c.build_packet(g_fake_ms, pkt);
    return pkt;
}
// a datagram with a valid header and no message (what any sender of 31 bytes could make)
static std::vector<uint8_t> header_datagram(uint32_t conn)
{
    Connection c;
    c.conn_id = conn;
    Writer w;
    w.u8(0);
    c.send_unreliable(MSG_PING, w);
    std::vector<uint8_t> pkt;
    c.build_packet(g_fake_ms, pkt);
    return pkt;
}

static bool one_clean_line(const std::string& s)
{
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F)
            return false;
    return utf8::valid(s);
}

// BUG-0774: a guest's HELLO name, the host's name in WELCOME and a newcomer's in PLAYER_JOIN come as sent (control
// characters, a newline into the logs and the co-op slot's marker file, broken UTF-8, 255 bytes)
static void test_name_clean()
{
    printf("names from the network are one clean line of at most 24 characters (BUG-0774)\n");
    CHECK(utf8::clean_text("A\nB\x01\x1b[1m\xC2\x85\xE2\x80\xA8" "C", 24) == "AB[1mC");
    CHECK(utf8::clean_text("\x80\xC3", 24).empty());              // a stray continuation byte, a cut sequence
    CHECK(utf8::clean_text("\xC0\xAF" "x", 24) == "x");             // an overlong '/'
    CHECK(utf8::clean_text("\xED\xA0\x80" "y", 24) == "y");         // a surrogate
    CHECK(utf8::clean_text("Zoë 🦖 \xEF\xBF\xBD", 24) == "Zoë 🦖 \xEF\xBF\xBD"); // letters, emoji, a real U+FFFD stay
    CHECK(utf8::count(utf8::clean_text(std::string(300, 'x'), 24)) == 24);
    Session host, a, b;
    std::string err;
    host.build = a.build = b.build = 0x922af7c7;
    std::vector<std::string> host_log, a_log;
    host.cb.log = [&](const std::string& s) { host_log.push_back(s); };
    a.cb.log = [&](const std::string& s) { a_log.push_back(s); };
    CHECK(host.host(0, "Host\nplanet Evil\x07", err));
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    CHECK(a.join(addr, "Bob\nplanet X\x01", err));
    pump(host, a, 300);
    CHECK(b.join(addr, std::string(60, 'w') + "\r\n", err));
    pump(host, b, 300);
    pump(host, a, 200);
    std::string an = host.name_of(1), bn = host.name_of(2), hn = a.name_of(0), bn_a = a.name_of(2);
    printf("  host sees '%s', '%s'; guest A sees the host '%s', B '%s'\n", an.c_str(), bn.c_str(), hn.c_str(), bn_a.c_str());
    CHECK(an == "Bobplanet X");
    CHECK(utf8::count(bn) == 24 && one_clean_line(bn));
    CHECK(hn == "Hostplanet Evil");
    CHECK(bn_a == bn);
    CHECK(a.name_of(1) == an); // the guest takes its name from the host's roster
    for (auto* log : {&host_log, &a_log})
        for (auto& l : *log)
            CHECK(one_clean_line(l));
    // the LAN game list: a host's name and info line as any machine of the network sends them
    Session lan;
    CHECK(lan.host(kDefaultPort + kDiscoverPorts - 1, "Lan\nHost\x01", err));
    lan.lobby_info = "Creature\r\n - World";
    Discovery d;
    CHECK(d.start());
    d.targets = {0x0100007F};
    d.interval_ms = 100;
    std::vector<Discovery::Game> found;
    for (int i = 0; i < 200 && found.empty(); i++) {
        d.update(now_ms());
        lan.update();
        d.update(now_ms());
        found = d.games(now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(found.size() == 1);
    if (!found.empty()) {
        printf("  the LAN list shows '%s' (%s)\n", found[0].info.name.c_str(), found[0].info.info.c_str());
        CHECK(found[0].info.name == "LanHost" && found[0].info.info == "Creature - World");
    }
}

// BUG-0776: a host with its port open to the internet answered a 6-byte discovery query from any address (up to
// 520 bytes back: a reflector, and the host's name, players and world for any scanner)
static void test_discovery_source()
{
    printf("LAN discovery is answered from local networks only, 20 a second at most (BUG-0776)\n");
    std::vector<uint32_t> radmin = {ep4(26, 255, 255, 255, 0).ip};
    CHECK(local_source(ep4(192, 168, 1, 5, 0).ip, {}) && local_source(ep4(10, 1, 2, 3, 0).ip, {}));
    CHECK(local_source(ep4(127, 0, 0, 1, 0).ip, {}) && local_source(ep4(172, 20, 0, 9, 0).ip, {}));
    CHECK(local_source(ep4(169, 254, 3, 4, 0).ip, {}) && local_source(ep4(100, 101, 1, 1, 0).ip, {}));
    CHECK(!local_source(ep4(8, 8, 8, 8, 0).ip, {}) && !local_source(ep4(172, 32, 0, 1, 0).ip, {}));
    CHECK(!local_source(ep4(26, 1, 2, 3, 0).ip, {}) && local_source(ep4(26, 1, 2, 3, 0).ip, radmin));
    CHECK(!local_source(ep4(27, 1, 2, 3, 0).ip, radmin));
    CHECK(!local_source(ep4(8, 8, 8, 8, 0).ip, {ep4(255, 255, 255, 255, 0).ip}));
    FakeNet net;
    Session host;
    host.set_transport(&net);
    host.clock = fake_ms;
    std::string err;
    CHECK(host.host(kDefaultPort, "LanHost", err));
    host.lobby_info = "Creature - Testworld";
    std::vector<uint8_t> q(6, 0);
    uint32_t magic = kDiscoverQuery;
    memcpy(q.data(), &magic, 4);
    Endpoint pub = ep4(8, 8, 8, 8, 40001), lan = ep4(192, 168, 1, 5, 40002), vpn = ep4(26, 1, 2, 3, 40003);
    net.in.push_back({pub, q});
    net.in.push_back({lan, q});
    net.in.push_back({vpn, q});
    host.update();
    size_t to_pub = net.sent_to(pub), to_lan = net.sent_to(lan), to_vpn_before = net.sent_to(vpn);
    host.lan_broadcasts = radmin;
    g_fake_ms += 1000;
    net.in.push_back({vpn, q});
    host.update();
    size_t to_vpn = net.sent_to(vpn) - to_vpn_before;
    g_fake_ms += 1000;
    for (int i = 0; i < 200; i++)
        net.in.push_back({ep4(10, 0, (uint8_t)(i / 250), (uint8_t)(i % 250 + 1), 40100), q});
    size_t before = net.out.size();
    host.update(); // reads 200 of them in one frame
    size_t burst = net.out.size() - before;
    printf("  answers: internet %zu, LAN %zu, VPN subnet %zu without / %zu with its adapter; 200 queries in a second: %zu\n",
           to_pub, to_lan, to_vpn_before, to_vpn, burst);
    CHECK(to_pub == 0);
    CHECK(to_lan == 1);
    CHECK(to_vpn_before == 0 && to_vpn == 1);
    CHECK(burst <= Session::kDiscoverPerSecond);
    CHECK(host.status().find("refused: discovery=") != std::string::npos);
}

// BUG-0778: any datagram with a valid header from a new address or port took one of 16 peer slots for 10 s; with
// 16 of them no player could join or rejoin ("no answer from host")
static void test_halfopen()
{
    printf("junk datagrams and refused HELLOs do not block a join (BUG-0778)\n");
    std::string err;
    for (int round = 0; round < 2; round++) {
        FakeNet net;
        Session host;
        host.set_transport(&net);
        host.clock = fake_ms;
        host.build = 0x922af7c7;
        CHECK(host.host(kDefaultPort, "Host", err));
        for (int i = 0; i < 16; i++) {
            if (round == 0) // 16 header-only datagrams from 16 ports of one address
                net.in.push_back({ep4(203, 0, 113, 9, (uint16_t)(30000 + i)), header_datagram(0x1000u + i)});
            else // 16 HELLOs of a wrong build (refused: half-open until their REJECT is through) from 16 addresses
                net.in.push_back({ep4(198, 51, 100, (uint8_t)(i + 1), 30000), hello_datagram(0x2000u + i, "x", 0x1234)});
        }
        host.update();
        size_t held = host.peer_count();
        g_fake_ms += 500;
        Endpoint guest = ep4(192, 168, 1, 7, 41000);
        net.in.push_back({guest, hello_datagram(0x77, "Bob", 0x922af7c7)});
        host.update();
        bool in = host.players().size() == 2 && host.name_of(1) == "Bob";
        printf("  %s: %zu peer(s) held, then a real guest: %s\n", round == 0 ? "16 header-only datagrams" : "16 refused HELLOs", held,
               in ? "joined" : "NOT joined");
        CHECK(in);
        CHECK(net.sent_to(guest) >= 1);
        CHECK(host.peer_count() <= Session::kMaxPeers);
        if (round == 0)
            CHECK(held == 0);
    }
    // a datagram without a HELLO from a joined guest's address and port with another connection id (spoofed, or late
    // from an older connection) does not replace the guest
    FakeNet net;
    Session host;
    host.set_transport(&net);
    host.clock = fake_ms;
    host.build = 0x922af7c7;
    CHECK(host.host(kDefaultPort, "Host", err));
    Endpoint guest = ep4(192, 168, 1, 7, 41000);
    net.in.push_back({guest, hello_datagram(0x77, "Bob", 0x922af7c7)});
    host.update();
    net.in.push_back({guest, header_datagram(0x99)});
    host.update();
    CHECK(host.players().size() == 2);
}

// BUG-0861: a callback that starts a new session while a packet is delivered (here the guest's cb.connected:
// leave, then host) had the new session closed at the end of the delivery by the old session's deferred leave
static void test_restart_in_callback()
{
    printf("host() / join() inside a delivery callback keep the new session (BUG-0861)\n");
    std::string err;
    for (int variant = 0; variant < 3; variant++) {
        Session host, cli, other;
        host.build = cli.build = other.build = 0x922af7c7;
        CHECK(host.host(0, "h", err));
        CHECK(other.host(0, "o", err));
        std::string addr = "127.0.0.1:" + std::to_string(host.port()), other_addr = "127.0.0.1:" + std::to_string(other.port());
        int connected = 0, disc = 0;
        bool restarted = false;
        cli.cb.disconnected = [&](const std::string&) { disc++; };
        cli.cb.connected = [&] {
            connected++;
            if (restarted)
                return;
            restarted = true;
            std::string e;
            if (variant == 0) {
                cli.leave("callback");
                CHECK(cli.host(0, "rehosted", e));
            } else if (variant == 1) {
                CHECK(cli.host(0, "rehosted", e)); // without a leave first
            } else {
                cli.leave("callback");
                CHECK(cli.join(other_addr, "g", e)); // to another host
            }
        };
        CHECK(cli.join(addr, "g", err));
        for (int i = 0; i < 80; i++) {
            host.update();
            cli.update();
            other.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        bool ok = variant < 2 ? cli.role() == Role::Host && cli.players().size() == 1
                              : cli.role() == Role::Client && cli.connected() && other.players().size() == 2;
        printf("  %s: role %s, connected %d, disconnected %d\n",
               variant == 0 ? "leave + host" : variant == 1 ? "host" : "leave + join elsewhere",
               cli.role() == Role::Host ? "host" : cli.role() == Role::Client ? "client" : "none", connected, disc);
        CHECK(restarted);
        CHECK(ok);
        CHECK(disc == 0);
        // the first host saw the guest go (BYE) and keeps running
        CHECK(host.players().size() == 1 && host.role() == Role::Host);
    }
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
                                               {"lan_dedup", &g_fix.lan_dedup},
                                               {"name_clean", &g_fix.name_clean},
                                               {"lan_only_disc", &g_fix.lan_only_disc},
                                               {"halfopen_cap", &g_fix.halfopen_cap},
                                               {"restart_gen", &g_fix.restart_gen},
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
    test_unreliable_too_big();
    test_unreliable_chunks();
    test_dbpf();
    test_partprops();
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
    test_bulk_busy_receiver();
    test_session();
    test_session_end_in_delivery();
    test_rejoin_same_endpoint();
    test_unique_names();
    test_join_rules();
    test_big_snapshot();
    test_interp();
    test_interp_gap();
    test_lead_late();
    test_lead_keep();
    test_lead_fall();
    test_discovery();
    test_lobby_roles();
    test_shared_ledger();
    test_shared_join_keep();
    test_big_reliable();
    test_state_after_leave();
    test_velocity_jitter();
    test_velocity_jump_cap();
    test_velocity_hitch();
    test_interp_latency_rise();
    test_alloc_id();
    test_lost_first_packet();
    test_interp_wrap();
    test_refpack_short();
    test_dbbf_wrap();
    test_discovery_prune();
    test_discovery_dedup();
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
    test_name_clean();
    test_discovery_source();
    test_halfopen();
    test_restart_in_callback();
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
