// Golden-byte tests of the game modules' message payloads (mods/coop/wire/*.h, C6 of docs/plan-0.7.15.md;
// docs/04-netcode.md#netcode-wire-structs). make test; built with AddressSanitizer.
//
// Per message kind:
//   * golden(): a fixed value written by the struct is exactly the bytes in the test (hex), the bytes the feature's
//     hand-written writer produced before the struct existed (its Writer calls are copied here verbatim as `legacy`),
//     and they read back to the same value with nothing left over;
//   * the C6 behaviour changes (a malformed payload the old hand parse used and the struct refuses) as rejects().
// A changed byte here is a wire change: 0.7.14 and 0.7.15 must still play together.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "wire/common.h"
#include "wire/space_badges.h"
#include "wire/space_map.h"
#include "wire/space_rivalry.h"
#include "wire/space_trade.h"

static int g_fail = 0;
static int g_golden = 0;
#define CHECK(c, ...)                                                    \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s ", __FILE__, __LINE__, #c);         \
            printf(__VA_ARGS__);                                         \
            printf("\n");                                                \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static std::string hex(const std::vector<uint8_t>& v)
{
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}

template <class T>
static std::vector<uint8_t> enc(const T& x)
{
    net::Writer w;
    x.write(w);
    return w.data;
}

// `want`: the bytes as hex (spaces allowed between fields); `legacy`: the old hand-written writer's output for the
// same value (nullptr: the test only pins the hex)
template <class T>
static void golden(const char* name, const T& x, const char* want, const net::Writer* legacy)
{
    std::string w;
    for (const char* p = want; *p; p++)
        if (*p != ' ')
            w += *p;
    auto b = enc(x);
    CHECK(hex(b) == w, "%s: bytes %s, golden %s", name, hex(b).c_str(), w.c_str());
    if (legacy)
        CHECK(legacy->data == b, "%s: the old writer gave %s, the struct %s", name, hex(legacy->data).c_str(), hex(b).c_str());
    net::Reader r(b.data(), b.size());
    T y;
    CHECK(y.read(r), "%s: the golden bytes are rejected", name);
    CHECK(r.left() == 0, "%s: %zu bytes left", name, r.left());
    CHECK(enc(y) == b, "%s: read back and written again differs", name);
    g_golden++;
}

// a payload the struct refuses (a C6 behaviour change or an old check moved into read)
template <class T>
static void rejects(const char* name, const std::vector<uint8_t>& b)
{
    net::Reader r(b.data(), b.size());
    T y;
    CHECK(!y.read(r), "%s: accepted %s", name, hex(b).c_str());
}

// a payload the struct still takes (an older peer's shorter message, trailing bytes of a newer one)
template <class T>
static T accepts(const char* name, const std::vector<uint8_t>& b)
{
    net::Reader r(b.data(), b.size());
    T y;
    CHECK(y.read(r), "%s: rejected %s", name, hex(b).c_str());
    return y;
}

static std::vector<uint8_t> cut(std::vector<uint8_t> v, size_t n)
{
    v.resize(v.size() > n ? v.size() - n : 0);
    return v;
}

// ---------------------------------------------------------------- per feature
// (one function per wire header, in kind order)

// space_map.cpp: K_ROSTER 10, K_ROSTER_REQ 11, K_KEPT 12, K_KEPT_DONE 13
static void test_space_map()
{
    namespace m = wire::space_map;
    spacemap::Colony c;
    c.planet = 0x02001234;
    c.name = "Ab";
    c.star_name = "Sol";
    c.spice[0] = 0x11; c.spice[1] = 0x22; c.spice[2] = 0x33;
    c.spice_name = "Red";
    c.cities = 3;
    c.home = true;
    m::Roster ro;
    ro.version = 7; ro.part = 0; ro.parts = 2;
    ro.head.home_star = 0x1234; ro.head.home_planet = 0x01001234; ro.head.place = 0x80001234;
    ro.head.galaxy_pos = {1.f, 2.f, -3.f};
    ro.head.empire_name = "Emp"; ro.head.star_now_name = "S"; ro.head.color = 0xFF00FF00; ro.head.political_id = 0x99;
    ro.head.money = 5000;
    ro.colonies = {c};
    {
        // the old send_roster: body = put_head(with_pos) + u8 n + colonies; then u32 version, u8 part, u8 parts, body
        net::Writer body;
        body.u32(ro.head.home_star); body.u32(ro.head.home_planet); body.u32(ro.head.place);
        body.f32(ro.head.galaxy_pos.x); body.f32(ro.head.galaxy_pos.y); body.f32(ro.head.galaxy_pos.z);
        body.str8(ro.head.empire_name.substr(0, 64)); body.str8(ro.head.star_now_name.substr(0, 64));
        body.u32(ro.head.color); body.u32(ro.head.political_id); body.u32((uint32_t)ro.head.money);
        net::Writer cols;
        cols.u32(c.planet); cols.str8(c.name.substr(0, 64)); cols.str8(c.star_name.substr(0, 64));
        for (uint32_t k : c.spice) cols.u32(k);
        cols.str8(c.spice_name.substr(0, 32)); cols.u16(c.cities); cols.u8(c.home ? 1 : 0);
        body.u8(1);
        body.raw(cols.data.data(), cols.size());
        net::Writer w;
        w.u32(ro.version); w.u8(0); w.u8(2); w.raw(body.data.data(), body.size());
        golden("map K_ROSTER part 0", ro,
               "07000000 00 02 34120000 34120001 34120080 0000803f 00000040 000040c0 03456d70 0153 00ff00ff 99000000 88130000"
               " 01 34120002 024162 03536f6c 11000000 22000000 33000000 03526564 0300 01",
               &w);
        auto back = accepts<m::Roster>("map K_ROSTER star", w.data);
        CHECK(back.colonies.size() == 1 && back.colonies[0].star == 0x1234, "the colony's star from its planet id");
        // a cut part: not used, but its header is (the parts bookkeeping)
        net::Reader r(w.data.data(), w.data.size() - 1);
        m::Roster x;
        CHECK(!x.read(r) && x.head_ok && x.version == 7 && x.parts == 2, "a cut part keeps its header");
        rejects<m::Roster>("map K_ROSTER part >= parts", {7, 0, 0, 0, 2, 2, 0});
        rejects<m::Roster>("map K_ROSTER no parts", {7, 0, 0, 0, 0, 0, 0});
    }
    m::Roster r1;
    r1.version = 8; r1.part = 1; r1.parts = 2;
    {
        net::Writer w;
        w.u32(8); w.u8(1); w.u8(2); w.u8(0);
        golden("map K_ROSTER part 1", r1, "08000000 01 02 00", &w);
    }
    golden("map K_ROSTER_REQ", m::RosterReq{}, "", nullptr);
    m::Kept k;
    k.to = 2;
    k.planets = {0x01000005, 0x02000006};
    {
        net::Writer w; // the old send_kept
        w.u8(k.to);
        uint8_t n = (uint8_t)std::min<size_t>(k.planets.size(), 200);
        w.u8(n);
        for (uint8_t i = 0; i < n; i++)
            w.u32(k.planets[i]);
        golden("map K_KEPT", k, "02 02 05000001 06000002", &w);
    }
    m::Kept big;
    big.planets.assign(300, 1);
    CHECK(enc(big).size() == 2 + 200 * 4, "K_KEPT is cut at 200 colonies as before");
    m::KeptDone d;
    d.failed = {0x01000005};
    {
        net::Writer w; // the old kept_tick
        w.u8((uint8_t)d.failed.size());
        for (uint32_t p : d.failed)
            w.u32(p);
        golden("map K_KEPT_DONE", d, "01 05000001", &w);
        rejects<m::KeptDone>("map K_KEPT_DONE cut (C6)", cut(w.data, 1));
    }
}

// space_trade.cpp: K_TOOL 70 .. K_INV_DONE 79
static void test_space_trade()
{
    namespace t = wire::space_trade;
    wire::Key3 key;
    key.v[0] = 0xA; key.v[1] = 0xB; key.v[2] = 0xC;
    t::Tool tool;
    tool.to = 1; tool.id = 0x100; tool.key = key; tool.n = 5; tool.name = "Bomb";
    {
        net::Writer w; // the old give()
        w.u8(tool.to); w.u32(tool.id);
        for (uint32_t x : key.v) w.u32(x);
        w.u32((uint32_t)tool.n); w.str8(tool.name);
        golden("trade K_TOOL", tool, "01 00010000 0a000000 0b000000 0c000000 05000000 04426f6d62", &w);
        t::Tool z = tool;
        z.n = 0;
        rejects<t::Tool>("trade K_TOOL 0 charges", enc(z));
        z.n = 1000;
        rejects<t::Tool>("trade K_TOOL 1000 charges", enc(z));
    }
    t::ToolAck ta;
    ta.to = 2; ta.id = 0x100; ta.got = 3; ta.back = 2;
    {
        net::Writer w; // the old K_TOOL handler's answer
        w.u8(ta.to); w.u32(ta.id); w.u32((uint32_t)ta.got); w.u32((uint32_t)(ta.back));
        golden("trade K_TOOL_ACK", ta, "02 00010000 03000000 02000000", &w);
    }
    t::Sell sell;
    sell.to = 1; sell.id = 0x101; sell.planet = 0x01000007; sell.key = key; sell.n = 10; sell.price = 400; sell.name = "Red";
    {
        net::Writer w; // the old sell()
        w.u8(sell.to); w.u32(sell.id); w.u32(sell.planet);
        for (uint32_t x : key.v) w.u32(x);
        w.u32((uint32_t)sell.n); w.u32((uint32_t)sell.price); w.str8(sell.name);
        golden("trade K_SELL", sell, "01 01010000 07000001 0a000000 0b000000 0c000000 0a000000 90010000 03526564", &w);
        t::Offer of;
        static_cast<t::Sell&>(of) = sell;
        golden("trade K_OFFER", of, "01 01010000 07000001 0a000000 0b000000 0c000000 0a000000 90010000 03526564", &w);
        of.price = 0x7FFFFFFF;
        rejects<t::Offer>("trade K_OFFER n x price over an int", enc(of));
        t::Sell s2 = sell;
        s2.price = 0x7FFFFFFF;
        accepts<t::Sell>("trade K_SELL any price (as before)", enc(s2));
    }
    t::SellAck sa;
    sa.to = 2; sa.id = 0x101; sa.got = 10;
    {
        net::Writer w;
        w.u8(sa.to); w.u32(sa.id); w.u32((uint32_t)sa.got);
        golden("trade K_SELL_ACK", sa, "02 01010000 0a000000", &w);
    }
    t::OfferAck oa;
    oa.to = 2; oa.id = 0x102; oa.got = 0; oa.reason = 1;
    {
        net::Writer w; // the old answer_offer
        w.u8(oa.to); w.u32(oa.id); w.u32((uint32_t)oa.got); w.u8(oa.reason);
        golden("trade K_OFFER_ACK", oa, "02 02010000 00000000 01", &w);
    }
    t::OfferCancel oc;
    oc.to = 1; oc.id = 0x102;
    {
        net::Writer w;
        w.u8(oc.to); w.u32(oc.id);
        golden("trade K_OFFER_CANCEL", oc, "01 02010000", &w);
    }
    t::Inv inv;
    inv.slots = 6;
    inv.items = {{2, key, 1, 9}, {4, key, 7, -1}};
    t::InvMsg im;
    im.to = 0; im.id = 3; im.fresh = true; im.inv = inv;
    const char* hold = "0600 0200 02 0a000000 0b000000 0c000000 01000000 09000000 04 0a000000 0b000000 0c000000 07000000 ffffffff";
    auto put_inv_old = [](net::Writer& w, const t::Inv& v) { // the old put_inv
        w.u16((uint16_t)std::clamp(v.slots, 0, 0xFFFF));
        uint16_t n = (uint16_t)std::min<size_t>(v.items.size(), 250);
        w.u16(n);
        for (uint16_t i = 0; i < n; i++) {
            const t::InvItem& x = v.items[i];
            w.u8(x.type);
            for (uint32_t k : x.key.v) w.u32(k);
            w.u32((uint32_t)x.count);
            w.u32((uint32_t)x.ammo);
        }
    };
    {
        net::Writer w; // the old inv_tick
        w.u8(0); w.u32(3); w.u8(1); put_inv_old(w, inv);
        golden("trade K_INV", im, (std::string("00 03000000 01 ") + hold).c_str(), &w);
        t::Inv bad = inv;
        bad.items[0].count = 100001;
        t::InvMsg b = im;
        b.inv = bad;
        rejects<t::InvMsg>("trade K_INV count over 100000", enc(b));
        b.inv = inv;
        b.inv.items[0].ammo = -2;
        rejects<t::InvMsg>("trade K_INV ammo below -1", enc(b));
        net::Writer many;
        many.u8(0); many.u32(3); many.u8(1); many.u16(0); many.u16(251);
        rejects<t::InvMsg>("trade K_INV 251 items", many.data);
    }
    t::InvKept ik;
    ik.to = 1; ik.id = 4; ik.have = true; ik.inv = inv;
    {
        net::Writer w; // the old send_kept_inv
        w.u8(1); w.u32(4); w.u8(1); put_inv_old(w, inv);
        golden("trade K_INV_KEPT", ik, (std::string("01 04000000 01 ") + hold).c_str(), &w);
    }
    t::InvKept none;
    none.to = 1; none.id = 0; none.have = false;
    {
        net::Writer w; // the old "nothing kept" answer
        w.u8(1); w.u32(0); w.u8(0);
        golden("trade K_INV_KEPT none", none, "01 00000000 00", &w);
    }
    t::InvDone dn;
    dn.to = 0; dn.id = 4; dn.restored = 0; dn.failed = 2;
    {
        net::Writer w; // the old K_INV_KEPT handler's answer
        w.u8(0); w.u32(4); w.u16(0); w.u16((uint16_t)2);
        golden("trade K_INV_DONE", dn, "00 04000000 0000 0200", &w);
        auto sh = accepts<t::InvDone>("trade K_INV_DONE without the counts (read as before)", {0, 4, 0, 0, 0});
        CHECK(!sh.has_counts && sh.id == 4, "short K_INV_DONE");
    }
}

// space_badges.cpp: K_BADGES 85, K_BADGES_KEPT 86, K_BADGES_DONE 87
static void test_space_badges()
{
    namespace b = wire::space_badges;
    b::Badges t;
    t.badge = {{0x58B78701, 1}, {0x1234, 2}};
    t.stage = {{0x58B78701, 1}};
    t.event = {{2, 7}};
    t.card = 0xC0DE;
    auto put_list_old = [](net::Writer& w, const std::vector<std::pair<uint32_t, int32_t>>& v) {
        uint16_t n = (uint16_t)std::min<size_t>(v.size(), 1000);
        w.u16(n);
        for (uint16_t i = 0; i < n; i++) {
            w.u32(v[i].first);
            w.u32((uint32_t)v[i].second);
        }
    };
    auto put_old = [&](net::Writer& w, const b::Badges& x) {
        put_list_old(w, x.badge); put_list_old(w, x.stage); put_list_old(w, x.event); w.u32(x.card);
    };
    const char* tables = "0200 0187b758 01000000 34120000 02000000 0100 0187b758 01000000 0100 02000000 07000000 dec00000";
    b::Table tb;
    tb.to = 0; tb.serial = 5; tb.fresh = true; tb.b = t;
    {
        net::Writer w; // the old tick
        w.u8(0); w.u32(5); w.u8(1); put_old(w, t);
        golden("badges K_BADGES", tb, (std::string("00 05000000 01 ") + tables).c_str(), &w);
        b::Table bad = tb;
        bad.b.badge[0].second = 17;
        rejects<b::Table>("badges K_BADGES state 17", enc(bad));
    }
    b::Kept kp;
    kp.to = 1; kp.serial = 6; kp.have = true; kp.b = t;
    {
        net::Writer w; // the old send_kept
        w.u8(1); w.u32(6); w.u8(1); put_old(w, t);
        golden("badges K_BADGES_KEPT", kp, (std::string("01 06000000 01 ") + tables).c_str(), &w);
    }
    b::Kept none;
    none.to = 1; none.serial = 0;
    {
        net::Writer w; // the old "nothing kept"
        w.u8(1); w.u32(0); w.u8(0);
        golden("badges K_BADGES_KEPT none", none, "01 00000000 00", &w);
    }
    b::Done dn;
    dn.to = 0; dn.serial = 6;
    {
        net::Writer w;
        w.u8(0); w.u32(6);
        golden("badges K_BADGES_DONE", dn, "00 06000000", &w);
    }
}

// space_rivalry.cpp: K_STATE 50 .. K_SET 57
static void test_space_rivalry()
{
    namespace v = wire::space_rivalry;
    v::State st;
    st.rivals = true; st.split = 3; st.total = 10000; st.share = 5000; st.merge = 1;
    st.credits = {{1, 7000}};
    st.rels = {{0, 1, 2}};
    st.props = {{1, 0, 2}};
    {
        net::Writer w; // the old host_broadcast
        w.u8(true); w.u16(3); w.u32((uint32_t)10000); w.u32((uint32_t)5000); w.u16(1);
        w.u8(1); w.u8(1); w.u32((uint32_t)7000);
        w.u8(1); w.u8(0); w.u8(1); w.u8((uint8_t)2);
        w.u8(1); w.u8(1); w.u8(0); w.u8(2);
        golden("rivalry K_STATE", st, "01 0300 10270000 88130000 0100 01 01 581b0000 01 00 01 02 01 01 00 02", &w);
        rejects<v::State>("rivalry K_STATE cut", cut(w.data, 1));
    }
    v::Req rq;
    rq.action = 5; rq.target = 0;
    {
        net::Writer w;
        w.u8(5); w.u8(0);
        golden("rivalry K_REQ", rq, "05 00", &w);
    }
    v::Event ev;
    ev.what = 5; ev.a = 1; ev.b = 0;
    {
        net::Writer w; // the old host_event
        w.u8(5); w.u8(1); w.u8(0);
        golden("rivalry K_EVENT", ev, "05 01 00", &w);
    }
    v::Hit h;
    h.victim = 1; h.dmg = 25.f; h.type = 3; h.dir[0] = 0; h.dir[1] = 0; h.dir[2] = 1;
    {
        net::Writer w; // the old on_hit
        w.u8(1); w.f32(25.f); w.u32((uint32_t)3); w.f32(0); w.f32(0); w.f32(1);
        golden("rivalry K_HIT", h, "01 0000c841 03000000 00000000 00000000 0000803f", &w);
        v::Hit z = h;
        z.dmg = 0;
        rejects<v::Hit>("rivalry K_HIT no damage", enc(z));
        z.dmg = 2e6f;
        rejects<v::Hit>("rivalry K_HIT damage over 1e6", enc(z));
        z.dmg = NAN;
        rejects<v::Hit>("rivalry K_HIT NaN damage", enc(z));
        z.dmg = 5;
        z.dir[1] = NAN;
        accepts<v::Hit>("rivalry K_HIT NaN direction (the receiver's to replace)", enc(z));
    }
    v::Money mo;
    mo.v = 7000;
    {
        net::Writer w;
        w.u32((uint32_t)7000);
        golden("rivalry K_MONEY", mo, "581b0000", &w);
        mo.v = -5;
        accepts<v::Money>("rivalry K_MONEY out of range (the receiver's check, BUG-1110)", enc(mo));
    }
    v::MergeFix mf;
    mf.d = -300;
    {
        net::Writer w;
        w.u32((uint32_t)-300);
        golden("rivalry K_MERGE_FIX", mf, "d4feffff", &w);
        mf.d = 100000000;
        rejects<v::MergeFix>("rivalry K_MERGE_FIX 1e8", enc(mf));
        mf.d = (int)0x80000000u;
        rejects<v::MergeFix>("rivalry K_MERGE_FIX -2^31 (C6)", enc(mf));
    }
    golden("rivalry K_ASK", v::Ask{}, "", nullptr);
    v::Set se;
    se.to = 1; se.have = 2; se.v = 0;
    {
        net::Writer w; // the old K_ASK answer
        w.u8(1); w.u8(2); w.u32((uint32_t)0);
        golden("rivalry K_SET", se, "01 02 00000000", &w);
    }
}

int main()
{
    printf("wire golden bytes\n");
    test_space_map();
    test_space_trade();
    test_space_badges();
    test_space_rivalry();
    printf(g_fail ? "wire_tests: FAILED (%d)\n" : "wire_tests: %d message kinds, ALL OK\n", g_fail ? g_fail : g_golden);
    return g_fail ? 1 : 0;
}
