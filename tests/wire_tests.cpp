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

#include "fields_wire.h"
#include "wire/common.h"
#include "wire/shared_res.h"
#include "wire/space_allies.h"
#include "wire/space_badges.h"
#include "wire/space_contracts.h"
#include "wire/space_core.h"
#include "wire/space_diplomacy.h"
#include "wire/space_finale.h"
#include "wire/space_map.h"
#include "wire/space_planets.h"
#include "wire/space_eco.h"
#include "wire/space_rivalry.h"
#include "wire/space_trade.h"
#include "wire/space_ufo.h"
#include "wire/tribe_anim.h"

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
    // BUG-1151: K_COLONY 14 / K_KEPT_COLONY 15, one part of a colony's cities
    m::ColonyPart cp;
    cp.to = 1; cp.planet = 0x01460035; cp.hash = 0xAABBCCDD; cp.total = 5; cp.offset = 2; cp.bytes = {7, 8, 9};
    golden("map K_COLONY part", cp, "01 35004601 ddccbbaa 0500 0200 0300 070809", nullptr);
    rejects<m::ColonyPart>("map K_COLONY part past its total", {1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 2, 0, 7, 8});
    rejects<m::ColonyPart>("map K_COLONY part cut", {1, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 7, 8});
    // the cities blob: round trip, its ranges
    m::City c1;
    c1.pos[0] = 1; c1.pos[1] = 2; c1.pos[2] = 3; c1.spice = 77; c1.size = 4; c1.max_size = 8; c1.turrets = 3;
    c1.income = 150; c1.happy = 0.5f; c1.specialty = 2; c1.wall = 0x280852F1; c1.name = "Bobton";
    c1.buildings.push_back(std::vector<uint8_t>(0x38, 0x11));
    m::City c2 = c1;
    c2.name = "\xD0\x91";
    c2.buildings.clear();
    std::vector<m::City> cs{c1, c2}, back;
    auto blob = m::put_cities(cs);
    CHECK(m::get_cities(blob, back) && back.size() == 2 && back[0] == c1 && back[1] == c2, "the cities blob round trip");
    CHECK(blob.size() == 2 + (57 + 1 + 6 + 1 + 0x38) + (57 + 1 + 2 + 1), "the cities blob's size (%zu)", blob.size());
    auto bad = blob;
    bad[0] = 2;
    CHECK(!m::get_cities(bad, back), "another blob version is refused");
    CHECK(!m::get_cities(cut(blob, 1), back), "a cut blob is refused");
    m::City nan = c1;
    nan.spice = -1;
    CHECK(!m::get_cities(m::put_cities({nan}), back), "a negative spice is refused");
    std::vector<m::City> many(20, c2);
    CHECK(m::get_cities(m::put_cities(many), back) && back.size() == m::kCityMax, "at most 16 cities");
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
        net::Writer many; // 251 whole items (the writer cuts at 250)
        many.u8(0); many.u32(3); many.u8(1); many.u16(0); many.u16(251);
        for (int i = 0; i < 251; i++) {
            many.u8(2);
            for (uint32_t k : key.v) many.u32(k);
            many.u32(1); many.u32((uint32_t)-1);
        }
        rejects<t::InvMsg>("trade K_INV 251 items", many.data);
        many.data[8] = 250; // the same, 250 items: one item's bytes left over
        accepts<t::InvMsg>("trade K_INV 250 items", many.data);
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
        net::Writer many; // 1001 whole counter entries (the writer cuts at 1000)
        many.u8(0); many.u32(5); many.u8(1);
        many.u16(0); many.u16(0); many.u16(1001);
        for (int i = 0; i < 1001; i++) { many.u32((uint32_t)i); many.u32(1); }
        many.u32(0);
        rejects<b::Table>("badges K_BADGES 1001 counters", many.data);
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

// spacecoop.cpp: K_DOWN 1 .. K_TERRA_REC 9
static void test_space_core()
{
    namespace c = wire::space_core;
    {
        net::Writer w; // the old down(): disabled for kDownTime (20 s)
        w.u8(1);
        w.u8((uint8_t)20);
        golden("core K_DOWN", c::Down{1, 20}, "01 14", &w);
        rejects<c::Down>("core K_DOWN cut", cut(w.data, 1));
    }
    {
        net::Writer w; // the old raid hook (a raid starts)
        uint32_t place = 0x03000042;
        w.u32(place);
        w.u8(3);
        golden("core K_RAID", c::Raid{place, 3}, "42000003 03", &w);
    }
    {
        c::Mission m;
        m.reward = 1.5f;
        wire::Key3 a, b;
        a.v[0] = 1; a.v[1] = 2; a.v[2] = 3;
        b.v[0] = 0x10; b.v[1] = 0x20; b.v[2] = 0x30;
        m.tools = {a, b};
        net::Writer w; // the old mission hook: f32 reward, u8 n, the keys
        w.f32(1.5f);
        w.u8((uint8_t)m.tools.size());
        for (auto& kk : m.tools)
            for (uint32_t v : kk.v)
                w.u32(v);
        golden("core K_MISSION", m, "0000c03f 02 01000000 02000000 03000000 10000000 20000000 30000000", &w);
        rejects<c::Mission>("core K_MISSION fewer bytes than its keys", cut(w.data, 1));
        rejects<c::Mission>("core K_MISSION no count", {0, 0, 0xc0, 0x3f});
        auto t = accepts<c::Mission>("core K_MISSION no tools", {0, 0, 0xc0, 0x3f, 0});
        CHECK(t.tools.empty(), "no tools");
    }
    {
        c::Terra t{0x01000009, {0.5f, -3.f, 10.f}};
        net::Writer w; // the old terra_tick
        w.u32(t.place);
        for (float v : t.v)
            w.f32(v);
        golden("core K_TERRA", t, "09000001 0000003f 000040c0 00002041", &w);
        golden("core K_TERRA_REC", c::TerraRec{0x01000009, {1.f, 2.f, 10.f}}, "09000001 0000803f 00000040 00002041", nullptr);
        auto nan = enc(t);
        memset(nan.data() + 4, 0xFF, 4); // not finite: read, the handler refuses it (apply_terra, as before)
        accepts<c::Terra>("core K_TERRA NaN read (the handler checks)", nan);
    }
    {
        c::Gift g;
        g.to = 2;
        g.key.v[0] = 0xAA; g.key.v[1] = 0xBB; g.key.v[2] = 0xCC;
        g.n = 7;
        net::Writer w; // the old give_spice
        w.u8(g.to);
        for (uint32_t v : g.key.v)
            w.u32(v);
        w.u32((uint32_t)g.n);
        golden("core K_GIFT", g, "02 aa000000 bb000000 cc000000 07000000", &w);
        golden("core K_GIFT_BACK", g, "02 aa000000 bb000000 cc000000 07000000", &w);
        c::Gift neg = g;
        neg.n = -1;
        accepts<c::Gift>("core K_GIFT count out of range read (the handler checks, space.gift_refund)", enc(neg));
    }
    {
        c::Sculpt sc;
        sc.place = 0x01000001;
        sc.effect = 0xDEADBEEF;
        for (size_t i = 0; i < sizeof sc.xf; i++)
            sc.xf[i] = (uint8_t)i;
        net::Writer w; // the old sculpt hook
        w.u32(sc.place);
        w.u32(sc.effect);
        w.raw(sc.xf, 0x38);
        std::string want = "01000001 efbeadde ";
        for (int i = 0; i < 0x38; i++) {
            char b[4];
            snprintf(b, sizeof b, "%02x", i);
            want += b;
        }
        golden("core K_SCULPT", sc, want.c_str(), &w);
        rejects<c::Sculpt>("core K_SCULPT cut", cut(w.data, 1));
    }
    {
        net::Writer w; // the old SetColors hook
        w.u32(0x01000001);
        w.u32((uint32_t)2);
        w.u32(0xFF112233);
        w.u32(0xFF445566);
        golden("core K_COLOUR", c::Colour{0x01000001, 2, 0xFF112233, 0xFF445566}, "01000001 02000000 332211ff 665544ff", &w);
    }
}

// space_contracts.cpp: K_STATE 20, K_ACCEPT 21, K_RESULT 22
static void test_space_contracts()
{
    namespace c = wire::space_contracts;
    c::State st;
    st.id = 5; st.type = 1; st.state = 2; st.reason = 0; st.needed = 2;
    st.money = 10000; st.tool = 0x12345678; st.secs = 90;
    st.crew = {0, 1};
    st.targets = {{0x42, 1, 0x02000042}};
    st.wave = 1; st.waves = 3; st.left = 4; st.up = 4;
    {
        net::Writer w; // the old write_state (secs = the deadline from now)
        w.u32(st.id); w.u8(st.type); w.u8(st.state); w.u8(st.reason); w.u8(st.needed);
        w.u32(st.money); w.u32(st.tool); w.u32(st.secs);
        w.u8((uint8_t)st.crew.size());
        for (uint8_t id : st.crew)
            w.u8(id);
        w.u8((uint8_t)st.targets.size());
        for (auto& tg : st.targets) {
            w.u32(tg.star); w.u8(tg.done); w.u32(tg.planet);
        }
        w.u8(st.wave); w.u8(st.waves); w.u8(st.left); w.u8(st.up);
        golden("contracts K_STATE", st,
               "05000000 01 02 00 02 10270000 78563412 5a000000 02 00 01 01 42000000 01 42000002 01 03 04 04", &w);
        rejects<c::State>("contracts K_STATE cut", cut(w.data, 1));
    }
    c::State bad = st;
    bad.targets.clear();
    rejects<c::State>("contracts K_STATE armada without a target", enc(bad));
    bad = st;
    bad.type = 3;
    rejects<c::State>("contracts K_STATE unknown type", enc(bad));
    bad = st;
    bad.targets.assign(17, {1, 0, 1});
    rejects<c::State>("contracts K_STATE 17 targets", enc(bad));
    c::State none; // the host's "no contract"
    golden("contracts K_STATE none", none, "00000000 00 00 00 00 00000000 00000000 00000000 00 00 00 00 00 00", nullptr);
    {
        net::Writer w; // the old accept()
        w.u32(5);
        golden("contracts K_ACCEPT", c::Accept{5}, "05000000", &w);
    }
    {
        c::Result res{5, 1, 0, 10000, 0x12345678, 1, {0, 1}};
        net::Writer w; // the old finish()
        w.u32(res.id); w.u8(res.ok); w.u8(res.reason); w.u32(res.money); w.u32(res.tool); w.u8(res.type);
        w.u8((uint8_t)res.crew.size());
        for (uint8_t id : res.crew)
            w.u8(id);
        golden("contracts K_RESULT", res, "05000000 01 00 10270000 78563412 01 02 00 01", &w);
        rejects<c::Result>("contracts K_RESULT cut crew", cut(w.data, 1));
    }
}

// space_diplomacy.cpp: K_EVENT 30, K_STATE 31, K_SNAP 32, K_ASK 33, K_CONTACT 34
static void test_space_diplomacy()
{
    namespace d = wire::space_diplomacy;
    {
        d::Event e{0x0A0B0C0D, 1, 0x77, 0.5f, "Grox"};
        net::Writer w; // the old tick (K_EVENT)
        w.u32(e.npc); w.u8(e.first); w.u32(e.ev); w.f32(e.scale); w.str8(e.name);
        golden("diplomacy K_EVENT", e, "0d0c0b0a 01 77000000 0000003f 0447726f78", &w);
        rejects<d::Event>("diplomacy K_EVENT cut name", cut(w.data, 1));
    }
    {
        d::State st{0x0A0B0C0D, 0, 2, "Grox"};
        net::Writer w; // the old tick (K_STATE)
        w.u32(st.npc); w.u8(st.first); w.u8((uint8_t)st.what); w.str8(st.name);
        golden("diplomacy K_STATE", st, "0d0c0b0a 00 02 0447726f78", &w);
    }
    {
        d::Snap sn;
        sn.last = 1;
        sn.reply_to = 2;
        sn.recs = {{0x100, 0x31, {{7, 1.f}, {9, -3.f}}}, {0x200, 0, {}}};
        net::Writer b; // the old send_snapshot flush
        b.u8(sn.last); b.u8(sn.reply_to); b.u16((uint16_t)sn.recs.size());
        for (auto& r : sn.recs) {
            b.u32(r.npc); b.u32(r.flags);
            size_t k = std::min<size_t>(r.ev.size(), 100);
            b.u8((uint8_t)k);
            for (size_t i = 0; i < k; i++) {
                b.u32(r.ev[i].first);
                b.f32(r.ev[i].second);
            }
        }
        golden("diplomacy K_SNAP", sn,
               "01 02 0200 00010000 31000000 02 07000000 0000803f 09000000 000040c0 00020000 00000000 00", &b);
        rejects<d::Snap>("diplomacy K_SNAP cut chunk", cut(b.data, 1));
        d::Snap big;
        big.recs = {{1, 0, std::vector<std::pair<uint32_t, float>>(150, {1, 1.f})}};
        CHECK(enc(big).size() == 4 + 9 + 100 * 8, "K_SNAP is cut at 100 events per empire as before");
    }
    golden("diplomacy K_ASK", d::Ask{}, "", nullptr);
    {
        d::Contact ct{0x0A0B0C0D, 2, "Grox"};
        net::Writer w; // the old tick (K_CONTACT): u32 npc, u8 2 contacted / 1 met, name
        w.u32(ct.npc); w.u8(2); w.str8(ct.name);
        golden("diplomacy K_CONTACT", ct, "0d0c0b0a 02 0447726f78", &w);
    }
}

// space_finale.cpp: K_STATE 40, K_WAIT 41, K_REACHED 42
static void test_space_finale()
{
    namespace f = wire::space_finale;
    f::State st{2, 5, 0x123, 0x456, 10.f};
    net::Writer w; // the old send_state
    w.u8(st.zone); w.u8(st.flags); w.u32(st.star); w.u32(st.prev); w.f32(st.dist);
    golden("finale K_STATE", st, "02 05 23010000 56040000 00002041", &w);
    rejects<f::State>("finale K_STATE cut", cut(w.data, 1));
    golden("finale K_WAIT", f::Wait{}, "", nullptr);
    golden("finale K_REACHED", f::Reached{}, "", nullptr);
}

// space_allies.cpp: K_RAIDS 60, K_DEFENDED 61
static void test_space_allies()
{
    namespace a = wire::space_allies;
    a::Raids rd;
    rd.raids = {{0x01000005, 3}, {0x02000006, 30}};
    {
        net::Writer w; // the old raids_tick
        w.u8((uint8_t)std::min<size_t>(rd.raids.size(), 50));
        for (size_t i = 0; i < rd.raids.size() && i < 50; i++) {
            w.u32(rd.raids[i].first);
            w.u8(rd.raids[i].second);
        }
        golden("allies K_RAIDS", rd, "02 05000001 03 06000002 1e", &w);
        rejects<a::Raids>("allies K_RAIDS cut list", cut(w.data, 1));
    }
    a::Raids many;
    many.raids.assign(60, {1, 1});
    CHECK(enc(many).size() == 1 + 50 * 5, "K_RAIDS is cut at 50 raids as before");
    {
        net::Writer w; // the old watch_raids win
        w.u8(1);
        w.u32(0x01000005);
        golden("allies K_DEFENDED", a::Defended{1, 0x01000005}, "01 05000001", &w);
    }
}

// space_planets.cpp: K_SPICE 80, K_SPICE_TAKEN 81
static void test_space_planets()
{
    namespace p = wire::space_planets;
    p::Spice sp;
    sp.pol = 0x77;
    sp.colonies = {{0x01000005, {1.f, 2.f}}, {0x02000006, {}}};
    {
        net::Writer w; // the old host_tick part
        w.u32(sp.pol);
        w.u8((uint8_t)sp.colonies.size());
        for (auto& [id, v] : sp.colonies) {
            w.u32(id);
            w.u8((uint8_t)v.size());
            for (float x : v)
                w.f32(x);
        }
        golden("planets K_SPICE", sp, "77000000 02 05000001 02 0000803f 00000040 06000002 00", &w);
        rejects<p::Spice>("planets K_SPICE cut", cut(w.data, 1));
    }
    // as the old parse: a colony with more than 16 values is not skipped: the next one starts after its 16th
    net::Writer w17;
    w17.u32(0x77); w17.u8(2); w17.u32(0x01000005); w17.u8(17);
    for (int i = 0; i < 16; i++)
        w17.f32(1.f);
    w17.u32(0x0000803f); // read as the 17th value by a correct parser, as the next planet id here
    w17.u8(0);
    auto s17 = accepts<p::Spice>("planets K_SPICE 17 values (old misparse kept)", w17.data);
    CHECK(s17.colonies.size() == 2 && s17.colonies[0].second.size() == 16 && s17.colonies[1].first == 0x0000803f,
          "the old parse of an over-long colony");
    {
        p::SpiceTaken t{0x77, 0x01000005, {0.5f}};
        net::Writer w; // the old guest_check
        w.u32(t.pol); w.u32(t.id); w.u8((uint8_t)t.left.size());
        for (float x : t.left)
            w.f32(x);
        golden("planets K_SPICE_TAKEN", t, "77000000 05000001 01 0000003f", &w);
        rejects<p::SpiceTaken>("planets K_SPICE_TAKEN cut", cut(w.data, 1));
    }
}

// space_eco.cpp: K_ECO 90 (new in 0.7.15, BUG-1148: no older writer)
static void test_space_eco()
{
    namespace e = wire::space_eco;
    e::Eco x;
    x.planet = 0x02460035;
    x.plants = {{{{0x8C9D0727, 0x438F6347, 0x40666200}}, {1.f, 2.f, -3.f}}};
    x.animals = {{{{0x0654E276, 0x2B978C46, 0x40626200}}, {0.f, 0.f, 0.f}},
                 {{{0x067C9377, 0x2B978C46, 0x40626200}}, {0.5f, 0.f, 0.f}}};
    golden("eco K_ECO", x,
           "35004602 01 27079d8c 47638f43 00626640 0000803f 00000040 000040c0 "
           "02 76e25406 468c972b 00626240 00000000 00000000 00000000 77937c06 468c972b 00626240 0000003f 00000000 00000000",
           nullptr);
    net::Writer w;
    x.write(w);
    rejects<e::Eco>("eco K_ECO cut", cut(w.data, 1));
    // a list over the game's limit, a non-finite position: no peer's
    net::Writer big;
    big.u32(1); big.u8((uint8_t)e::kMaxSpecies + 1);
    for (size_t i = 0; i < (e::kMaxSpecies + 1) * 6; i++) big.u32(0);
    big.u8(0);
    rejects<e::Eco>("eco K_ECO too many", big.data);
    e::Eco nan = x;
    nan.plants[0].pos[1] = NAN;
    rejects<e::Eco>("eco K_ECO NaN position", enc(nan));
    e::Eco empty;
    empty.planet = 0x00460035;
    golden("eco K_ECO empty", empty, "35004600 00 00", nullptr);
}

// shared.cpp: MSG_RES 25, MSG_RES_ADD 26 (the body after the type)
// C7 (BUG-1160): the Tribe member kinds, the same bytes under MSG_TRIBE_ANIM as under MSG_NPC_ANIM before (the old
// writers of tribe.cpp copied as `legacy`)
static void test_tribe_anim()
{
    namespace t = wire::tribe_anim;
    {
        net::Writer w; // gameover_host_tick's send(kind, v)
        w.u8(20);
        w.u8(3);
        golden("tribe K_GAMEOVER", t::Wait{t::K_GAMEOVER, 3}, "14 03", &w);
        net::Writer w2;
        w2.u8(21);
        w2.u8(1);
        golden("tribe K_HOST_PAUSE", t::Wait{t::K_HOST_PAUSE, 1}, "15 01", &w2);
        rejects<t::Wait>("tribe wait of another kind", {0x09, 0x01});
        rejects<t::Wait>("tribe wait cut", {0x14});
    }
    {
        std::vector<std::pair<uint16_t, float>> v = {{5, 3.0f}, {0x1234, 0.f}};
        net::Writer w; // send_member_air
        w.u8(9);
        w.u8((uint8_t)v.size());
        for (size_t i = 0; i < v.size(); i++) {
            w.u16(v[i].first);
            w.f32(v[i].second);
        }
        t::Air a;
        a.v = v;
        golden("tribe K_AIR", a, "09 02 0500 00004040 3412 00000000", &w);
        rejects<t::Air>("tribe air cut", {0x09, 0x02, 0x05, 0x00, 0x00, 0x00, 0x40, 0x40, 0x34});
    }
    {
        net::Writer w; // send_member_holds, a start with its item (BUG-0971)
        w.u8(12);
        w.u8(1);
        w.u16(7);
        w.u32(0x04C6CB02u);
        w.u8(3);
        t::Hold h;
        h.with_item = true;
        h.v.push_back({7, 0x04C6CB02u, 3});
        golden("tribe K_HOLD_ITEM", h, "0c 01 0700 02cbc604 03", &w);
        net::Writer w2; // ... and an end (reliable, no item)
        w2.u8(11);
        w2.u8(1);
        w2.u16(7);
        w2.u32(0);
        t::Hold e;
        e.v.push_back({7, 0, 0});
        golden("tribe K_HOLD end", e, "0b 01 0700 00000000", &w2);
        rejects<t::Hold>("tribe hold of another kind", {0x02, 0x00});
    }
}

static void test_shared_res()
{
    namespace s = wire::shared_res;
    s::Res r;
    r.id = 1;
    r.v = 100.0;
    r.has_acks = true;
    r.acks = {{1, 7}, {2, 9}};
    r.has_causes = true;
    r.causes = {{0, 3.0}, {2, -25.0}};
    {
        net::Writer w; // the old shared tick (host, floor fix and cause fix on)
        w.u8(r.id);
        w.f64(r.v);
        w.u8((uint8_t)r.acks.size());
        for (auto& [id, seq] : r.acks) {
            w.u8(id);
            w.u32(seq);
        }
        w.u8((uint8_t)std::min<size_t>(r.causes.size(), 16));
        size_t i = 0;
        for (auto& [id, c] : r.causes) {
            if (i++ >= 16)
                break;
            w.u8(id);
            w.f64(c);
        }
        golden("shared MSG_RES", r,
               "01 0000000000005940 02 01 07000000 02 09000000 02 00 0000000000000840 02 00000000000039c0", &w);
        // a cut ack list: refused, the head was fine (the handler rejects it after its ledger steps, as before)
        net::Writer acks;
        acks.u8(1); acks.f64(100.0); acks.u8(2); acks.u8(1); acks.u32(7); acks.u8(2);
        net::Reader rr(acks.data.data(), acks.data.size());
        s::Res x;
        CHECK(!x.read(rr) && x.head_ok && x.acks.size() == 1, "a cut ack list");
        // a cut cause list: the causes before the cut, the total taken (as before)
        auto c = accepts<s::Res>("shared MSG_RES cut cause list", cut(w.data, 1));
        CHECK(c.has_causes && c.causes.size() == 1 && c.v == 100.0, "the first cause of a cut list");
    }
    s::Res old; // an older host: no acks, no causes
    old.id = 2;
    old.v = 3.0;
    {
        net::Writer w;
        w.u8(2);
        w.f64(3.0);
        golden("shared MSG_RES (older host)", old, "02 0000000000000840", &w);
    }
    s::Res nocause = r; // floor fix on, no causes yet (the first total)
    nocause.has_causes = false;
    golden("shared MSG_RES (no causes)", nocause, "01 0000000000005940 02 01 07000000 02 09000000", nullptr);
    s::Res inf = old;
    inf.v = INFINITY;
    rejects<s::Res>("shared MSG_RES not finite", enc(inf));
    s::ResAdd a{1, -25.0, true, 42};
    {
        net::Writer w; // the old guest tick (floor fix on)
        w.u8(a.id);
        w.f64(a.v);
        w.u32(a.seq);
        golden("shared MSG_RES_ADD", a, "01 00000000000039c0 2a000000", &w);
        auto shortseq = accepts<s::ResAdd>("shared MSG_RES_ADD 3 bytes of a sequence: none", cut(w.data, 1));
        CHECK(!shortseq.has_seq, "no sequence number in 3 bytes");
    }
    golden("shared MSG_RES_ADD (older guest)", s::ResAdd{1, -25.0, false, 0}, "01 00000000000039c0", nullptr);
    s::ResAdd nan{1, NAN, false, 0};
    rejects<s::ResAdd>("shared MSG_RES_ADD not finite", enc(nan));
}

// fields_wire.h (MSG_FIELDS): one packet of encode() is what decode() reads
static void test_fields_wire()
{
    fields_wire::Batch b;
    b.kind = 3;
    b.fids = {1, 4};
    b.ids = {0x10, 0x20};
    b.vals = {1.f, 2.f, -3.f, 10.f};
    auto out = fields_wire::encode(b);
    std::string want = "03 02 0104 0200 1000 0000803f 00000040 2000 000040c0 00002041";
    want.erase(std::remove(want.begin(), want.end(), ' '), want.end());
    CHECK(out.size() == 1 && hex(out[0].data) == want,
          "MSG_FIELDS bytes %s", out.empty() ? "" : hex(out[0].data).c_str());
    net::Reader r(out[0].data.data(), out[0].size());
    fields_wire::Batch y;
    CHECK(fields_wire::decode(r, y) && y.ids == b.ids && y.vals == b.vals && y.fids == b.fids, "MSG_FIELDS read back");
    g_golden++;
}

// squadron.cpp: MSG_UFO_LEAD (id 78, space 1.3 of 0.7.17, BUG-1288; ported from the agy branch's id 74): K_STATE 0,
// K_HIT 1, K_DAMAGE 2, K_LEADS 3
static void test_space_ufo()
{
    namespace u = wire::space_ufo;
    {
        u::LeadState st;
        st.leader = 1;
        st.mode = 0x01654C05u;
        st.world = 0x80000042u;
        st.body = {0x07, 0x00, 0x00, 0x01, 0x00};
        golden("ufo K_STATE", st, "00 01 054c6501 42000080 0500 0700000100", nullptr);
        auto b = enc(st);
        uint8_t k = 0xFF;
        net::Reader rp(b.data(), b.size());
        CHECK(u::peek_kind(rp, k) && k == u::K_STATE && rp.left() == b.size(), "ufo peek_kind K_STATE");
        rejects<u::LeadState>("ufo K_STATE empty", {});
        rejects<u::LeadState>("ufo K_STATE cut body", cut(b, 1));
        rejects<u::LeadState>("ufo K_STATE cut header", cut(b, 6));
        auto bad = b;
        bad[0] = u::K_HIT;
        rejects<u::LeadState>("ufo K_STATE wrong kind", bad);
        u::LeadState z = st;
        z.leader = 0;
        rejects<u::LeadState>("ufo K_STATE host leader", enc(z));
        z = st;
        z.world = 0;
        rejects<u::LeadState>("ufo K_STATE world 0", enc(z));
        z = st;
        z.body.clear();
        rejects<u::LeadState>("ufo K_STATE empty body", enc(z));
        net::Writer big;
        big.u8(u::K_STATE); big.u8(1); big.u32(st.mode); big.u32(st.world);
        big.u16((uint16_t)(u::kMaxStateBody + 1));
        for (size_t i = 0; i <= u::kMaxStateBody; i++)
            big.u8(0);
        rejects<u::LeadState>("ufo K_STATE oversize body", big.data);
    }
    {
        u::LeadHit h;
        h.leader = 1;
        h.mode = 0x01654C05u;
        h.world = 0x80000042u;
        h.shooter = 2;
        h.ship_id = 5;
        h.dmg = 25.f;
        h.type = 3;
        h.dir[0] = 0.f; h.dir[1] = 0.f; h.dir[2] = 1.f;
        golden("ufo K_HIT", h, "01 01 054c6501 42000080 02 0500 0000c841 03000000 00000000 00000000 0000803f", nullptr);
        auto b = enc(h);
        rejects<u::LeadHit>("ufo K_HIT cut", cut(b, 1));
        auto bad = b;
        bad[0] = u::K_DAMAGE;
        rejects<u::LeadHit>("ufo K_HIT wrong kind", bad);
        u::LeadHit z = h;
        z.leader = 0;
        rejects<u::LeadHit>("ufo K_HIT host leader", enc(z));
        z = h;
        z.world = 0;
        rejects<u::LeadHit>("ufo K_HIT world 0", enc(z));
        z = h;
        z.shooter = 0;
        rejects<u::LeadHit>("ufo K_HIT host shooter", enc(z));
        z = h;
        z.shooter = z.leader;
        rejects<u::LeadHit>("ufo K_HIT self shooter", enc(z));
        z = h;
        z.dmg = -1.f;
        rejects<u::LeadHit>("ufo K_HIT negative damage", enc(z));
        z = h;
        z.dmg = u::kMaxDamage + 1.f;
        rejects<u::LeadHit>("ufo K_HIT huge damage", enc(z));
        z = h;
        z.dmg = NAN;
        rejects<u::LeadHit>("ufo K_HIT NaN damage", enc(z));
        z = h;
        z.dir[2] = INFINITY;
        rejects<u::LeadHit>("ufo K_HIT Inf dir", enc(z));
    }
    {
        u::LeadDamage d;
        d.leader = 1;
        d.mode = 0x01654C05u;
        d.world = 0x41000009u;
        d.victim = 3;
        d.dmg = 25.f;
        d.type = 0;
        d.dir[0] = 0.f; d.dir[1] = 0.f; d.dir[2] = 1.f;
        d.attacker = 7;
        golden("ufo K_DAMAGE", d, "02 01 054c6501 09000041 03 0000c841 00000000 00000000 00000000 0000803f 0700", nullptr);
        auto b = enc(d);
        rejects<u::LeadDamage>("ufo K_DAMAGE cut", cut(b, 1));
        u::LeadDamage z = d;
        z.leader = 0;
        rejects<u::LeadDamage>("ufo K_DAMAGE host leader", enc(z));
        z = d;
        z.world = 0;
        rejects<u::LeadDamage>("ufo K_DAMAGE world 0", enc(z));
        z = d;
        z.victim = 0;
        rejects<u::LeadDamage>("ufo K_DAMAGE host victim", enc(z));
        z = d;
        z.victim = z.leader;
        rejects<u::LeadDamage>("ufo K_DAMAGE self victim", enc(z));
        z = d;
        z.dmg = -0.5f;
        rejects<u::LeadDamage>("ufo K_DAMAGE negative damage", enc(z));
        z = d;
        z.dmg = NAN;
        rejects<u::LeadDamage>("ufo K_DAMAGE NaN damage", enc(z));
        z = d;
        z.dir[0] = NAN;
        rejects<u::LeadDamage>("ufo K_DAMAGE NaN dir", enc(z));
    }
    {
        u::Leads l;
        l.rows = {{0x01654C05u, 0x80000042u, 2}, {0x01654C05u, 0x41000009u, 3}};
        golden("ufo K_LEADS", l, "03 02 054c6501 42000080 02 054c6501 09000041 03", nullptr);
        auto b = enc(l);
        u::Leads back;
        net::Reader rb(b.data(), b.size());
        CHECK(back.read(rb) && back.rows.size() == 2 && back.rows[1].leader == 3 && back.rows[1].world == 0x41000009u,
              "ufo K_LEADS round trip");
        golden("ufo K_LEADS empty", u::Leads{}, "03 00", nullptr);
        rejects<u::Leads>("ufo K_LEADS cut", cut(b, 1));
        auto bad = b;
        bad[0] = u::K_STATE;
        rejects<u::Leads>("ufo K_LEADS wrong kind", bad);
        u::Leads z = l;
        z.rows[0].leader = 0;
        rejects<u::Leads>("ufo K_LEADS host leader", enc(z));
        z = l;
        z.rows[1].leader = 0xFF;
        rejects<u::Leads>("ufo K_LEADS no leader", enc(z));
        z = l;
        z.rows[0].world = 0;
        rejects<u::Leads>("ufo K_LEADS world 0", enc(z));
        net::Writer many;
        many.u8(u::K_LEADS);
        many.u8((uint8_t)(u::kMaxLeads + 1));
        for (int i = 0; i <= u::kMaxLeads; i++) {
            many.u32(0x01654C05u); many.u32(0x80000001u + i); many.u8(1);
        }
        rejects<u::Leads>("ufo K_LEADS too many rows", many.data);
    }
    // the families: a 0.7.16.1 peer (space 1.2, or no CAP_FAMILIES) is never sent MSG_UFO_LEAD
    std::vector<net::FamilyRev> old_fam = {{1, 1, 2}, {u::kFamily, 1, 2}};
    std::vector<net::FamilyRev> new_fam = {{1, 1, 2}, {u::kFamily, 1, u::kMinor}};
    std::vector<net::FamilyRev> future_minor = {{u::kFamily, 1, (uint16_t)(u::kMinor + 1)}};
    std::vector<net::FamilyRev> wrong_rev = {{u::kFamily, 2, u::kMinor}};
    CHECK(!u::supports(false, new_fam), "ufo supports requires CAP_FAMILIES");
    CHECK(!u::supports(true, old_fam), "ufo supports rejects space 1.2");
    CHECK(u::supports(true, new_fam), "ufo supports accepts space 1.3");
    CHECK(u::supports(true, future_minor), "ufo supports accepts space 1.4+");
    CHECK(!u::supports(true, wrong_rev), "ufo supports rejects space 2.x");
    uint8_t k = 0;
    net::Reader empty(nullptr, 0);
    CHECK(!u::peek_kind(empty, k), "ufo peek_kind rejects empty reader");
}

int main()
{
    printf("wire golden bytes\n");
    test_space_core();
    test_space_contracts();
    test_space_diplomacy();
    test_space_finale();
    test_space_allies();
    test_space_planets();
    test_space_eco();
    test_space_ufo();
    test_shared_res();
    test_fields_wire();
    test_space_map();
    test_space_trade();
    test_space_badges();
    test_space_rivalry();
    test_tribe_anim();
    printf(g_fail ? "wire_tests: FAILED (%d)\n" : "wire_tests: %d message kinds, ALL OK\n", g_fail ? g_fail : g_golden);
    return g_fail ? 1 : 0;
}
