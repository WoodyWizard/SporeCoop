// Native tests for the species line's portable part (species_line_core.h, WP-0 of docs/plan-0.7.16.md, BUG-1270): the
// rules of each stage, what the host sends of a row, the wire / store format and the store record.
// make test (built with AddressSanitizer).
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "player_store.h"
#include "species_line_core.h"

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

using namespace species;

constexpr uint32_t kCrt = 0x2B978C46, kCrtG = 0x40626200, kVeh = 0x24682294, kVehG = 0x40646200;

static auto all = [](Source) { return true; };

static void test_rules()
{
    printf("rules\n");
    Row r;
    // nothing made: every stage the world's own
    for (int s = 0; s < (int)Stage::Count; s++)
        CHECK(pick(r, (Stage)s, all) == Source::None);
    // an own creature: Creature, Tribe and Civ are the creature; Cell is never the Row's (creations keeps the cell)
    r.creature = 0x1111;
    CHECK(pick(r, Stage::Cell, all) == Source::None);
    CHECK(pick(r, Stage::Creature, all) == Source::Creature);
    CHECK(pick(r, Stage::Tribe, all) == Source::Creature);
    CHECK(pick(r, Stage::Civ, all) == Source::Creature);
    // a tribe outfit made for this creature: Tribe and Civ wear it
    r.tribe = tribe_ref(r, 0x2222, kCrt, kCrtG, shared_instance(0xABCDEF));
    CHECK(r.tribe.made_for == 0x1111);
    CHECK(tribe_counts(r) && tribe_line_own(r) == 0x2222);
    CHECK(pick(r, Stage::Creature, all) == Source::Creature);
    CHECK(pick(r, Stage::Tribe, all) == Source::Tribe);
    CHECK(pick(r, Stage::Civ, all) == Source::Tribe);
    // a Civ outfit made for that tribe line: Civ wears it, Tribe still the tribe outfit
    r.civ = civ_ref(r, 0x3333, kCrt, kCrtG, shared_instance(0x123456));
    CHECK(r.civ.made_for == 0x2222);
    CHECK(pick(r, Stage::Tribe, all) == Source::Tribe);
    CHECK(pick(r, Stage::Civ, all) == Source::Civ);
    // another creature: the tribe outfit was not made for it, the Civ outfit not for its tribe line
    Row o = r;
    o.creature = 0x4444;
    CHECK(!tribe_counts(o) && tribe_line_own(o) == 0x4444 && !civ_counts(o));
    CHECK(pick(o, Stage::Tribe, all) == Source::Creature);
    CHECK(pick(o, Stage::Civ, all) == Source::Creature);
    // back to the first creature: both outfits count again (they are kept)
    o.creature = 0x1111;
    CHECK(pick(o, Stage::Civ, all) == Source::Civ);
    // a Civ outfit made for the creature (no tribe outfit then) counts while the tribe line is the creature
    Row c;
    c.creature = 0x1111;
    c.civ = civ_ref(c, 0x5555, kCrt, kCrtG, shared_instance(7));
    CHECK(c.civ.made_for == 0x1111 && pick(c, Stage::Civ, all) == Source::Civ);
    c.tribe = tribe_ref(c, 0x2222, kCrt, kCrtG, shared_instance(8)); // a tribe outfit made later: a new tribe line
    CHECK(pick(c, Stage::Civ, all) == Source::Tribe);
    // the world's own creature (0): an outfit made for it counts
    Row w;
    w.tribe = tribe_ref(w, 0x2222, kCrt, kCrtG, shared_instance(9));
    CHECK(w.tribe.made_for == 0 && pick(w, Stage::Tribe, all) == Source::Tribe);
    CHECK(pick(w, Stage::Creature, all) == Source::None);
    // an outfit not sent yet (no shared key) does not count
    Row u = r;
    u.tribe.shared = 0;
    CHECK(pick(u, Stage::Tribe, all) == Source::Creature);
    CHECK(!civ_counts(u) || u.civ.made_for == tribe_line_own(u));
    // a source this game cannot use falls back: Civ -> Tribe -> Creature -> None
    CHECK(pick(r, Stage::Civ, [](Source s) { return s != Source::Civ; }) == Source::Tribe);
    CHECK(pick(r, Stage::Civ, [](Source s) { return s == Source::Creature; }) == Source::Creature);
    CHECK(pick(r, Stage::Tribe, [](Source s) { return s != Source::Tribe; }) == Source::Creature);
    CHECK(pick(r, Stage::Civ, [](Source) { return false; }) == Source::None);
}

static void test_shared()
{
    printf("shared keys\n");
    CHECK(shared_instance(0xFFABCDEF) == 0x7BABCDEF);
    CHECK(is_shared(0x7B000001) && !is_shared(0x7B000000) && !is_shared(0x7C123456) && !is_shared(0x2222));
    Row r;
    r.creature = 1;
    r.tribe = tribe_ref(r, 2, kCrt, kCrtG, 0x7B000002);
    r.civ = civ_ref(r, 3, kCrt, kCrtG, 0x7B000003);
    r.designs[0x99E92F05] = {4, kVeh, kVehG, 0x7B000004, 0};
    std::set<uint32_t> host_has{0x7B000002, 0x7B000004};
    Row s = resolved(r, [&](uint32_t i) { return host_has.count(i) != 0; });
    CHECK(s.tribe.shared == 0x7B000002 && s.civ.shared == 0 && s.designs[0x99E92F05].shared == 0x7B000004);
    CHECK(s.civ.own == 3 && s.civ.made_for == 2); // the owner's side stays
    CHECK(pick(s, Stage::Civ, all) == Source::Tribe); // the Civ outfit counts nowhere until its records are there
}

static void test_format()
{
    printf("format\n");
    Row r;
    CHECK(r.empty());
    Row e;
    CHECK(decode(encode(r), e) && e == r && e.empty());
    r.creature = 0x11223344;
    r.tribe = tribe_ref(r, 0x55667788, kCrt, kCrtG, 0x7B0000AA);
    r.civ = civ_ref(r, 0x99AABBCC, kCrt, kCrtG, 0);
    r.designs[0x99E92F05] = {0x1234, 0x2399BE55, 0x40636200, 0x7B0000BB, 0};
    r.designs[0x4E3F7777] = {0x5678, kVeh, kVehG, 0, 0};
    std::vector<uint8_t> d = encode(r);
    CHECK(d.size() == 1 + 4 + 20 + 20 + 1 + 2 * 24);
    CHECK(d[0] == kRowVersion && d[1] == 0x44 && d[4] == 0x11); // little endian
    Row b;
    CHECK(decode(d, b) && b == r);
    // a newer version's trailing bytes are ignored
    std::vector<uint8_t> longer = d;
    longer.push_back(0xEE);
    CHECK(decode(longer, b) && b == r);
    // cut anywhere: no row
    for (size_t n = 0; n < d.size(); n++) {
        std::vector<uint8_t> cut(d.begin(), d.begin() + n);
        Row x;
        x.creature = 9;
        CHECK(!decode(cut, x) && x.empty());
    }
    // another version
    std::vector<uint8_t> v2 = d;
    v2[0] = 2;
    CHECK(!decode(v2, b));
    // a shared key that is not ours, a ref without a type, a design without a key: refused
    Row bad = r;
    bad.tribe.shared = 0x12345678;
    CHECK(!decode(encode(bad), b));
    bad = r;
    bad.civ.type = 0;
    CHECK(!decode(encode(bad), b));
    bad = r;
    bad.designs[0x0FF10521] = {};
    CHECK(!decode(encode(bad), b));
    bad = r;
    bad.tribe = {};
    bad.tribe.made_for = 5; // nothing made, yet "made for"
    CHECK(!decode(encode(bad), b));
    // a duplicate design id on the wire (encode never writes one)
    std::vector<uint8_t> dup = d;
    dup[45] = 3; // n = 3
    dup.insert(dup.end(), d.end() - 24, d.end());
    CHECK(!decode(dup, b));
    // more designs than the limit: the first kMaxDesigns go out
    Row many;
    for (uint32_t i = 1; i <= kMaxDesigns + 5; i++)
        many.designs[i] = {i, kVeh, kVehG, 0, 0};
    CHECK(decode(encode(many), b) && b.designs.size() == kMaxDesigns && b.designs.begin()->first == 1);
}

static void test_store()
{
    printf("store record\n");
    pstore::Store s;
    s.reset("game3/madas.spo");
    std::string bob = pstore::stable_key(7, "t:1");
    Row r;
    r.creature = 0x1111;
    r.tribe = tribe_ref(r, 0x2222, kCrt, kCrtG, shared_instance(0xABCDEF));
    r.designs[0x4E3F7777] = {0x3333, kVeh, kVehG, shared_instance(0x10), 0};
    CHECK(s.put(bob, "Bob", kStoreFeature, kStoreVersion, encode(r), 100));
    CHECK(!s.put(bob, "Bob", kStoreFeature, kStoreVersion, encode(r), 100)); // the same: no change
    // through the file's text and back
    pstore::Store t;
    CHECK(t.parse(s.text(), "game3/madas.spo") == pstore::Parsed::Ok);
    const pstore::Record* rec = t.get(bob, kStoreFeature);
    Row back;
    CHECK(rec && rec->ver == kStoreVersion && decode(rec->data, back) && back == r);
    // an empty row removes the record (a player with nothing kept is not written)
    CHECK(t.put(bob, "Bob", kStoreFeature, kStoreVersion, {}, 200, true));
    CHECK(!t.get(bob, kStoreFeature) && t.players.empty());
}

int main()
{
    test_rules();
    test_shared();
    test_format();
    test_store();
    if (g_fail) {
        printf("species_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("species_tests: all passed\n");
    return 0;
}
