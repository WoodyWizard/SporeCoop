// BUG-9279: the pure rules of every player's own empire in Space (mods/coop/space_empires_logic.h) and its payloads
// (mods/coop/wire/space_empires.h). Built and run by `make test` (unit-cpp).
#include "space_empires_logic.h"
#include "wire/space_empires.h"

#include <cstdio>

static int g_fail;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

using namespace spaceempires;

static Empire emp(uint64_t player, uint32_t species, int32_t money) { return {true, player, species, money}; }

static void file_format()
{
    Roster r;
    r.e[1] = emp(0x1111, 0x7CA81684, 12345);
    r.e[3] = emp(0x3333, 0x7C000003, 99999999);
    CHECK(roster_valid(r));
    CHECK(r.seats() == 4);
    auto bytes = encode(r);
    CHECK(bytes.size() == 5 + 4 * 17);
    CHECK(bytes[0] == 'E' && bytes[1] == 'M' && bytes[2] == 'P' && bytes[3] == '1');
    Roster back;
    CHECK(decode(bytes, back));
    CHECK(back == r);
    // an empty roster is one empty entry (the host's)
    Roster none;
    CHECK(encode(none).size() == 5 + 17);
    CHECK(decode(encode(none), back) && back == none);
    // cut, longer than it says, a wrong magic, an unknown flag
    auto cut = bytes;
    cut.pop_back();
    CHECK(!decode(cut, back));
    auto longer = bytes;
    longer.push_back(0);
    CHECK(!decode(longer, back));
    auto magic = bytes;
    magic[0] = 'X';
    CHECK(!decode(magic, back));
    auto flag = bytes;
    flag[5 + 17] = 3; // seat 1's flags
    CHECK(!decode(flag, back));
    // too many seats
    std::vector<uint8_t> many = {'E', 'M', 'P', '1', 9};
    many.resize(5 + 9 * 17);
    CHECK(!decode(many, back));
}

static void validity()
{
    Roster r;
    r.e[0] = emp(1, 2, 3); // seat 0 is the game's own player empire: never in the file
    CHECK(!roster_valid(r));
    r = {};
    r.e[1] = emp(1, 0, 3); // no species
    CHECK(!roster_valid(r));
    r.e[1] = emp(1, 5, -1); // money below 0
    CHECK(!roster_valid(r));
    r.e[1] = emp(1, 5, 100000000); // above the game's cap
    CHECK(!roster_valid(r));
    r.e[1] = emp(7, 5, 10);
    r.e[2] = emp(7, 6, 10); // one identity in two seats
    CHECK(!roster_valid(r));
    r.e[2] = emp(0, 6, 10); // an identity not known yet is fine twice
    r.e[3] = emp(0, 8, 10);
    CHECK(roster_valid(r));
    r.e[4] = {false, 0, 9, 0}; // an empty entry carries nothing
    CHECK(!roster_valid(r));
}

static void founding()
{
    const uint32_t host = 0x065CBE70;
    Roster none;
    // seat 1 plays a species of its own: its empire is founded
    CHECK(next_empire(none, {{1, 0x7CA81684, 11}}, host) == 1);
    // a seat on the host's species shares the host's empire
    CHECK(next_empire(none, {{1, host, 11}}, host) == -1);
    // no species of its own (plays the host's creation)
    CHECK(next_empire(none, {{2, 0, 22}}, host) == -1);
    // the lowest seat first
    CHECK(next_empire(none, {{3, 0x7C000003, 33}, {2, 0x7C000002, 22}, {1, host, 11}}, host) == 2);
    // founded once
    Roster one;
    one.e[2] = emp(22, 0x7C000002, 5);
    CHECK(next_empire(one, {{3, 0x7C000003, 33}, {2, 0x7C000002, 22}}, host) == 3);
    // seat 0 and seats past the played ones never
    CHECK(next_empire(none, {{0, 0x7C000001, 1}, {4, 0x7C000004, 44}}, host) == -1);
    // a host without a species of its own known (0): any seat's own species founds
    CHECK(next_empire(none, {{1, 0x7C000001, 11}}, 0) == 1);
}

static void money()
{
    CHECK(start_money(250000) == 250000);
    CHECK(start_money(-5) == 0);
    CHECK(start_money(123456789012LL) == 99999999);
    Wallet w;
    CHECK(!w.report(0, 10)); // nothing sent to it yet
    w.serial = 7;
    CHECK(!w.report(6, 10)); // the answer to an older K_EMPIRE: the host save's money after a load
    CHECK(w.money == 0);
    CHECK(w.report(7, 1500));
    CHECK(w.money == 1500);
    CHECK(!w.report(7, -1));
    CHECK(!w.report(7, 100000000));
    CHECK(w.money == 1500);
}

static void payloads()
{
    namespace ew = wire::space_empires;
    ew::Empire e;
    e.to = 3;
    e.serial = 0x01020304;
    e.have = true;
    e.seat = 2;
    e.money = 250000;
    net::Writer w;
    e.write(w);
    const std::vector<uint8_t> want = {3, 4, 3, 2, 1, 1, 2, 0x90, 0xD0, 0x03, 0x00};
    CHECK(w.data == want);
    ew::Empire back;
    net::Reader r(w.data.data(), w.data.size());
    CHECK(back.read(r) && back.to == 3 && back.serial == 0x01020304 && back.have && back.seat == 2 && back.money == 250000);
    // a money over the game's cap, a seat past the file's
    auto bad = want;
    bad[7] = 0xFF, bad[8] = 0xFF, bad[9] = 0xFF, bad[10] = 0x7F;
    net::Reader r2(bad.data(), bad.size());
    CHECK(!back.read(r2));
    bad = want;
    bad[6] = 8;
    net::Reader r3(bad.data(), bad.size());
    CHECK(!back.read(r3));
    // cut
    net::Reader r4(want.data(), want.size() - 1);
    CHECK(!back.read(r4));
    ew::Report rep;
    rep.serial = 9;
    rep.money = 77;
    net::Writer w2;
    rep.write(w2);
    CHECK(w2.data.size() == 9);
    ew::Report rb;
    net::Reader r5(w2.data.data(), w2.data.size());
    CHECK(rb.read(r5) && rb.to == 0 && rb.serial == 9 && rb.money == 77);
    ew::Ask a;
    net::Writer w3;
    a.write(w3);
    CHECK(w3.data.size() == 5);
}

// K_DIPLO / K_DIPLO_SET: an own empire's diplomacy table in parts, joined again
static void diplomacy_parts()
{
    namespace ew = wire::space_empires;
    std::vector<wire::space_diplomacy::SnapRec> t;
    for (uint32_t i = 0; i < 40; i++) {
        wire::space_diplomacy::SnapRec r;
        r.npc = 0x80000100u + i;
        r.flags = i % 4;
        for (uint32_t k = 0; k < 5; k++)
            r.ev.push_back({0x0526E51Fu + k, 0.5f * k});
        t.push_back(r);
    }
    auto bytes = ew::table_bytes(t);
    CHECK(bytes.size() > ew::kPartMax); // more than one part
    auto parts = ew::parts_of(bytes, 2, 7, 3);
    CHECK(parts.size() == (bytes.size() + ew::kPartMax - 1) / ew::kPartMax);
    // on the wire, out of order: whole only after the last missing part
    ew::Joiner j;
    std::vector<uint8_t> out;
    for (size_t i = parts.size(); i-- > 0;) {
        net::Writer w;
        parts[i].write(w);
        ew::Part back;
        net::Reader r(w.data.data(), w.data.size());
        CHECK(back.read(r) && back.to == 2 && back.serial == 7 && back.version == 3 && back.count == parts.size());
        CHECK(j.add(back, out) == (i == 0));
    }
    CHECK(out == bytes);
    std::vector<wire::space_diplomacy::SnapRec> t2;
    CHECK(ew::table_read(out, t2) && t2.size() == 40 && t2[7].npc == 0x80000107u && t2[7].ev.size() == 5);
    // a new version drops the parts of the old one
    auto p4 = ew::parts_of(bytes, 2, 7, 4);
    CHECK(!j.add(parts[0], out));
    CHECK(!j.add(p4[1], out));
    // no table: one part with count 0
    auto none = ew::parts_of({}, 2, 8, 5);
    CHECK(none.size() == 1 && none[0].count == 0 && none[0].data.empty());
    ew::Joiner j2;
    CHECK(j2.add(none[0], out) && out.empty());
    // a part with an index past its count, or data with count 0, is no message
    net::Writer w;
    ew::Part bad = parts[0];
    bad.idx = bad.count;
    bad.write(w);
    ew::Part back;
    net::Reader r(w.data.data(), w.data.size());
    CHECK(!back.read(r));
    // a table with something after it is not a table
    auto longer = bytes;
    longer.push_back(0);
    CHECK(!ew::table_read(longer, t2));
}

int main()
{
    diplomacy_parts();
    file_format();
    validity();
    founding();
    money();
    payloads();
    if (g_fail)
        std::printf("space_empires_tests: %d FAILED\n", g_fail);
    else
        std::printf("space_empires_tests: ok\n");
    return g_fail ? 1 : 0;
}
