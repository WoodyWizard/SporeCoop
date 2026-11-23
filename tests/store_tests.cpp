// Native tests for the per-player store on the host's disk (player_store.h, E1 of docs/plan-0.7.15.md, BUG-1149) and
// the kept stores reading it (space_kept.h restore / rekey), the rejoin token's 30 days (rejoin_kept.h, BUG-1150).
// make test (built with AddressSanitizer).
#include <cstdio>
#include <string>
#include <vector>

#include "autosave.h"
#include "owner_claims.h"
#include "player_store.h"
#include "rejoin_kept.h"
#include "space_kept.h"

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

using pstore::Parsed;
using pstore::Store;

static std::vector<uint8_t> bytes(std::initializer_list<int> v)
{
    std::vector<uint8_t> o;
    for (int x : v)
        o.push_back((uint8_t)x);
    return o;
}

static void test_keys()
{
    printf("keys\n");
    const uint64_t salt = 0x1234567890ABCDEFull;
    std::string a = pstore::stable_key(salt, "t:00000000deadbeef"), b = pstore::stable_key(salt, "t:00000000deadbeee");
    CHECK(pstore::key_ok(a) && pstore::key_ok(b) && a != b);
    CHECK(a == pstore::stable_key(salt, "t:00000000deadbeef"));                 // the same player: the same key
    CHECK(a != pstore::stable_key(salt + 1, "t:00000000deadbeef"));             // another host's file: another key
    CHECK(a.find("deadbeef") == std::string::npos);                               // the token's hash is not in it
    CHECK(pstore::stable_key(salt, "n:Bob") != pstore::stable_key(salt, "t:Bob")); // a name is no token
    CHECK(pstore::stable_key(salt, "t:1", true) == "host" && pstore::stable_key(salt, "") == "");
    CHECK(!pstore::key_ok("s:12") && !pstore::key_ok("t:00000000deadbeef") && !pstore::key_ok("s:0123456789abcdeZ"));
    // the world of a game key
    CHECK(pstore::world_of_game_key("c:\\users\\x\\appdata\\roaming\\spore\\games\\game3\\madas.spo") == "game3/madas.spo");
    CHECK(pstore::world_of_game_key("C:\\Spore\\Games\\Game0\\Bob's World.spo") == "game0/bob's world.spo");
    CHECK(pstore::world_of_game_key("madas.spo") == "" && pstore::world_of_game_key("") == "");
    CHECK(pstore::world_of_game_key("x\\..\\madas.spo") == "");
}

static void test_round_trip()
{
    printf("round trip\n");
    Store s;
    s.reset("game3/madas.spo");
    s.saved = 1'800'000'000;
    std::string bob = pstore::stable_key(7, "t:1"), eve = pstore::stable_key(7, "t:2");
    CHECK(s.put(bob, "Bob", "space.colonies", 1, bytes({1, 2, 3, 4}), 100));
    CHECK(!s.put(bob, "Bob", "space.colonies", 1, bytes({1, 2, 3, 4}), 100)); // the same: no change
    CHECK(s.put(bob, "Bob", "space.badges", 2, bytes({}), 100));               // an empty record is a record
    CHECK(s.put(eve, "Ева Иванова 100%", "civ.cities", 1, bytes({0xFF, 0, 0x7F}), 200));
    CHECK(s.put("host", "Host", "civ.cities", 1, bytes({9}), 200));
    CHECK(s.put_world("space.rivals", 1, bytes({1, 0, 0, 0})));
    CHECK(!s.put("nobody", "X", "a", 1, {}, 1) && !s.put(bob, "Bob", "Bad Name", 1, {}, 1));
    std::string t = s.text();
    CHECK(t.find("Bob") != std::string::npos && t.find("t:1") == std::string::npos); // names yes, tokens no
    Store r;
    CHECK(r.parse(t, "game3/madas.spo") == Parsed::Ok);
    CHECK(r.saved == s.saved && r.players.size() == 3 && r.records() == 5);
    CHECK(r.get(bob, "space.colonies") && r.get(bob, "space.colonies")->data == bytes({1, 2, 3, 4}));
    CHECK(r.get(bob, "space.badges") && r.get(bob, "space.badges")->ver == 2 && r.get(bob, "space.badges")->data.empty());
    CHECK(r.name_of(eve) == "Ева Иванова 100%" && r.get(eve, "civ.cities")->data == bytes({0xFF, 0, 0x7F}));
    CHECK(r.get_world("space.rivals") && r.get_world("space.rivals")->data == bytes({1, 0, 0, 0}));
    CHECK(r.text() == t);
    // a record of a feature or version this build does not know is kept and written back as it was
    std::string newer = t.substr(0, t.rfind("end ")) + "x-new-line 1 2 3\n";
    newer.insert(newer.find("\np ") + 1, "w future.thing 9 abcd\n");
    newer += "end ";
    char sum[16];
    snprintf(sum, sizeof sum, "%08x\n", pstore::fnv32(newer.substr(0, newer.size() - 4)));
    newer += sum;
    Store f;
    CHECK(f.parse(newer, "game3/madas.spo") == Parsed::Ok && f.get_world("future.thing") &&
          f.get_world("future.thing")->ver == 9);
    CHECK(f.text().find("w future.thing 9 abcd") != std::string::npos);
}

static void test_bad_files()
{
    printf("damaged and foreign files\n");
    Store s;
    s.reset("game3/madas.spo");
    s.put(pstore::stable_key(1, "t:1"), "Bob", "space.colonies", 1, bytes({1}), 1);
    std::string t = s.text();
    Store r;
    CHECK(r.parse("", "game3/madas.spo") == Parsed::Empty && r.players.empty());
    CHECK(r.parse(t, "game4/madas.spo") == Parsed::Foreign && r.players.empty() && r.world == "game4/madas.spo");
    CHECK(r.parse(t.substr(0, t.size() / 2), "game3/madas.spo") == Parsed::Damaged && r.players.empty()); // cut
    std::string changed = t;
    changed[changed.find("Bob")] = 'R'; // one byte changed: the sum no longer matches
    CHECK(r.parse(changed, "game3/madas.spo") == Parsed::Damaged && r.players.empty());
    CHECK(r.parse("garbage\nend 00000000\n", "game3/madas.spo") == Parsed::Damaged);
    std::string other = "sporecoop-store 2\nworld game3/madas.spo\n";
    char sum[24];
    snprintf(sum, sizeof sum, "end %08x\n", pstore::fnv32(other));
    CHECK(r.parse(other + sum, "game3/madas.spo") == Parsed::Damaged); // another format version
    // a record line before any player line, bad hex: damaged
    std::string bad = "sporecoop-store 1\nworld game3/madas.spo\nr space.colonies 1 00\n";
    snprintf(sum, sizeof sum, "end %08x\n", pstore::fnv32(bad));
    CHECK(r.parse(bad + sum, "game3/madas.spo") == Parsed::Damaged);
    std::string badhex = "sporecoop-store 1\nworld game3/madas.spo\nw a 1 0g\n";
    snprintf(sum, sizeof sum, "end %08x\n", pstore::fnv32(badhex));
    CHECK(r.parse(badhex + sum, "game3/madas.spo") == Parsed::Damaged);
}

// BUG-1107 / BUG-1109: another game under the same name finds nothing by its key; the name is a fallback only on the
// host's word (adopt)
static void test_name_fallback()
{
    printf("name fallback\n");
    const uint64_t salt = 99;
    Store s;
    s.reset("game0/a.spo");
    std::string bob = pstore::stable_key(salt, "t:aaaa"), other = pstore::stable_key(salt, "t:bbbb");
    s.put(bob, "Bob", "space.colonies", 1, bytes({1, 2}), 10);
    CHECK(!s.get(other, "space.colonies")); // another game named Bob: nothing
    // its name finds the stored Bob (a second Bob of the session is "Bob 2"), but never itself or the host
    CHECK(s.by_name("Bob", other) == std::vector<std::string>{bob});
    CHECK(s.by_name("Bob 2", other) == std::vector<std::string>{bob});
    CHECK(s.by_name("Bob", bob).empty() && s.by_name("Bobby").empty() && s.by_name("").empty());
    // the host hands them over: the records move, the old key is gone
    CHECK(s.adopt(bob, other, "Bob", 20));
    CHECK(!s.get(bob, "space.colonies") && s.get(other, "space.colonies")->data == bytes({1, 2}));
    CHECK(!s.adopt(bob, other, "Bob", 20) && !s.adopt(other, other, "Bob", 20) && !s.adopt(other, "x", "Bob", 20));
    // both have a record: the handed over one wins
    s.put(bob, "Bob", "space.colonies", 1, bytes({7}), 30);
    s.put(bob, "Bob", "space.badges", 1, bytes({8}), 30);
    CHECK(s.adopt(bob, other, "Bob", 40) && s.get(other, "space.colonies")->data == bytes({7}) &&
          s.get(other, "space.badges")->data == bytes({8}));
}

static void test_bounds()
{
    printf("bounds\n");
    Store s;
    s.reset("game0/a.spo");
    s.put("host", "Host", "civ.cities", 1, bytes({1}), 0);
    for (int i = 0; i < 100; i++)
        s.put(pstore::stable_key(5, "t:" + std::to_string(i)), "P" + std::to_string(i), "space.badges", 1, bytes({1}), 1000 + i);
    CHECK(s.players.size() == pstore::kMaxPlayers && s.players.count("host")); // the oldest went, never the host
    CHECK(!s.players.count(pstore::stable_key(5, "t:0")) && s.players.count(pstore::stable_key(5, "t:99")));
    CHECK(!s.put("host", "Host", "big", 1, std::vector<uint8_t>(pstore::kMaxRecordBytes + 1), 0));
    // drop_empty: a player with nothing left is no player
    std::string k = pstore::stable_key(5, "t:99");
    CHECK(s.put(k, "P99", "space.badges", 1, {}, 2000, true) && !s.players.count(k));
    CHECK(s.keys_with("civ.cities") == std::vector<std::string>{"host"});
}

static void test_codec()
{
    printf("codec\n");
    pstore::Out o;
    o.u8(7);
    o.u16(0xBEEF);
    o.u32(0xDEADBEEF);
    o.i32(-5);
    o.f32(1.5f);
    o.str("Ева");
    pstore::In in(o.d);
    CHECK(in.u8() == 7 && in.u16() == 0xBEEF && in.u32() == 0xDEADBEEF && in.i32() == -5 && in.f32() == 1.5f &&
          in.str() == "Ева" && in.end());
    CHECK(in.u8() == 0 && !in.ok); // reading past the end
    std::vector<uint8_t> cut = {5, 'a'};
    pstore::In c(cut);
    CHECK(c.str().empty() && !c.ok);
    CHECK(pstore::esc("a b%c\n") == "a%20b%25c%0A" && pstore::esc("") == "%");
    std::string u;
    CHECK(pstore::unesc("a%20b%25c%0A", u) && u == "a b%c\n" && pstore::unesc("%", u) && u.empty());
    CHECK(!pstore::unesc("a%2", u) && !pstore::unesc("a%zz", u));
}

// the kept stores (space_kept.h) read the disk at world load: the restore goes back at the player's next join
static void test_kept_restore()
{
    printf("kept store from disk\n");
    spacekept::KeptStore<std::vector<uint32_t>> st;
    CHECK(st.world(1)); // the host's game started: its first Space world, the store empty
    std::string bob = pstore::stable_key(3, "t:b");
    CHECK(st.restore(bob, "Bob", {0x100, 0x200}) && st.size() == 1);
    CHECK(!st.find(pstore::stable_key(3, "t:other"))); // another game of the name: nothing (BUG-1109)
    // Bob joins (id 1): its first report is the host's copy: the kept colonies go back
    bool first = false;
    auto& e = st.bind(bob, 1, "Bob", true, &first);
    CHECK(first && e.have && e.value.size() == 2);
    st.begin(e, 10);
    CHECK(!st.keep(e, {}) && e.value.size() == 2); // the host's copy is not taken while the restore runs
    CHECK(st.done(1, 0, false) == &e && st.keep(e, {0x100, 0x200, 0x300}) && e.value.size() == 3);
    // a value of this session is not replaced by a later restore
    CHECK(!st.restore(bob, "Bob", {0x1}) && st.find(bob)->value.size() == 3);
    // the host's word: Bob's things to the game that came with a new token (bound as id 2)
    std::string bob2 = pstore::stable_key(3, "t:b2");
    st.bind(bob2, 2, "Bob", true);
    CHECK(st.rekey(bob, bob2) && !st.find(bob) && st.find(bob2)->value.size() == 3 && st.find(bob2)->id == 2);
    CHECK(!st.rekey(bob, bob2));
    CHECK(st.restored() == 1);
}

// BUG-1150: the clean leave's token lasts 30 days with net.rejoin_month (a day without)
static void test_token_month()
{
    printf("rejoin token: 30 days\n");
    using rejoin::KeptFile;
    const int64_t t0 = 1'800'000'000;
    KeptFile f;
    f.ensure_salt(42);
    f.put("10.0.0.2:47820", "Bob", 0x77, t0);
    std::string text = f.text();
    KeptFile day, month;
    CHECK(day.parse(text) && month.parse(text));
    month.ttl = KeptFile::kTtlMonth;
    const int64_t week = t0 + 7 * 24 * 3600;
    CHECK(day.find("10.0.0.2:47820", "Bob", week) == 0);       // off: the next week's join gets a fresh token (FAIL side)
    CHECK(month.find("10.0.0.2:47820", "Bob", week) == 0x77);  // on: the same token, the host's store finds the player
    CHECK(month.find("10.0.0.2:47820", "Bob", t0 + KeptFile::kTtlMonth + 1) == 0);
    CHECK(!month.expire(week) && month.size() == 1 && day.expire(week) && day.size() == 0);
}

// E2 (BUG-0864): the host's autosave clock
static void test_autosave()
{
    printf("autosave clock\n");
    autosave::Clock c;
    c.interval = 600;
    CHECK(c.due(1000) == ""); // not running: no session
    c.start(100);
    CHECK(c.due(699) == "" && c.due(700) == "timer");
    // any save of the game starts the timer again (a share save, the player's own)
    c.saved(650);
    CHECK(c.due(700) == "" && c.due(1249) == "" && c.due(1250) == "timer");
    // a milestone: after the settle delay, before the timer
    c.milestone("a new stage", 800);
    CHECK(c.due(801) == "" && c.due(800 + autosave::kMilestoneDelay) == "a new stage" && c.milestones == 1);
    // the game refuses (an editor): again after kRetry
    c.tried(806, false, "an editor is open");
    CHECK(c.refused == 1 && c.refusal == "an editor is open" && c.due(808) == "" && c.due(806 + autosave::kRetry) == "a new stage");
    // posted: nothing more until its write is seen; the write confirms it and ends the milestone
    c.tried(812, true);
    CHECK(c.due(900) == "" && c.posted == 1);
    c.saved(813);
    CHECK(c.confirmed == 1 && !c.pending && c.due(900) == "" && c.due(813 + 600) == "timer");
    // a posted save whose write never comes is lost after kConfirm, tried again after kRearm
    c.tried(1500, true);
    c.tick(1500 + autosave::kConfirm - 1);
    CHECK(c.lost == 0 && c.due(1520) == "");
    c.tick(1500 + autosave::kConfirm + 1);
    CHECK(c.lost == 1 && c.due(1531 + autosave::kRearm - 1) == "" && c.due(1531 + autosave::kRearm) == "timer");
    // a save older than the milestone does not end it
    c.milestone("a colony founded", 2000);
    c.saved(1999);
    CHECK(c.pending);
    // off: nothing, milestones neither
    autosave::Clock off;
    off.interval = 0;
    off.start(0);
    off.milestone("x", 1);
    CHECK(!off.running() && off.due(10000) == "" && off.milestones == 0);
    c.stop();
    CHECK(c.due(1e9) == "");
    CHECK(autosave::ago_text(-1, false) == "not saved yet" && autosave::ago_text(30, false) == "saved just now" &&
          autosave::ago_text(185, false) == "saved 3 min ago" && autosave::ago_text(7300, false) == "saved 2 h ago");
}

// BUG-1203: "a colony founded" once per colony of a Space world, not after a read that missed one for a moment
static void test_new_colonies()
{
    printf("autosave new colony ids\n");
    autosave::NewIds n;
    CHECK(!n.grew(1, {0x01460035u, 0x00460014u}));               // the first read of the world: what it has
    CHECK(!n.grew(1, {0x01460035u}));                            // a read right after a save misses one
    CHECK(!n.grew(1, {0x01460035u, 0x00460014u}));               // ... it is back: no milestone (the bug: 1 -> 2)
    CHECK(n.grew(1, {0x01460035u, 0x00460014u, 0x0046000Cu}));   // a colony founded
    CHECK(!n.grew(1, {0x01460035u, 0x00460014u, 0x0046000Cu}));  // ... only once
    CHECK(!n.grew(1, {}) && !n.grew(1, {0x0046000Cu}));          // gone and back
    CHECK(!n.grew(2, {0x0046000Cu, 0x02460035u}));               // another Space world: its first read again
    CHECK(n.grew(2, {0x0046000Cu, 0x02460035u, 0x03460035u}));
    CHECK(!n.grew(0, {0x05460035u}) && !n.grew(0, {0x05460035u, 0x06460035u})); // no Space world: never
}

// BUG-1153 / BUG-1154: owners by position across a load
static void test_claims()
{
    printf("owner claims\n");
    using claims::Spot;
    std::vector<Spot> spots{{0, 0, 0, "host", 0}, {10, 0, 0, "s:00000000000000aa", 1}, {20, 0, 0, "s:00000000000000bb", 2}};
    // today's objects: a little moved, in another order, one new, one gone
    std::vector<std::array<float, 3>> objs{{20.5f, 0, 0}, {0.2f, 0, 0}, {100, 0, 0}, {9.0f, 0.5f, 0}};
    auto m = claims::match(objs, spots, 3.0f);
    CHECK(m == (std::vector<int>{2, 0, -1, 1}));
    // two objects near one spot: only the nearer one takes it
    std::vector<std::array<float, 3>> two{{10.4f, 0, 0}, {10.1f, 0, 0}};
    CHECK(claims::match(two, spots, 3.0f) == (std::vector<int>{-1, 1}));
    // nothing within the radius
    CHECK(claims::match({{50, 50, 50}}, spots, 3.0f) == (std::vector<int>{-1}));
    // the record round trip; a bad key or position is no record
    pstore::Out o;
    claims::put_spots(o, spots);
    pstore::In in(o.d);
    std::vector<Spot> back;
    CHECK(claims::get_spots(in, back) && in.end() && back.size() == 3 && back[1].key == "s:00000000000000aa" &&
          back[2].flags == 2 && back[1].x == 10);
    std::vector<Spot> bad{{0, 0, 0, "Bob", 0}};
    pstore::Out ob;
    claims::put_spots(ob, bad);
    pstore::In ib(ob.d);
    CHECK(!claims::get_spots(ib, back));
    // the holder: the present player of the key, else the host holds it for the absent one (never another guest)
    auto present = [](const std::string& k) { return k == "host" ? 0 : k == "s:00000000000000aa" ? 2 : -1; };
    CHECK(claims::holder("s:00000000000000aa", present) == 2 && claims::holder("s:00000000000000bb", present) == 0 &&
          claims::holder("host", present) == 0);
}

// BUG-1424 (NET-12): a salt.txt or world file that cannot be read now is not replaced
static void test_unreadable()
{
    printf("unreadable store files (BUG-1424)\n");
    using pstore::Got;
    using pstore::SaltStep;
    using pstore::WorldStep;
    CHECK(pstore::salt_step(Got::Text, true, true) == SaltStep::Use);
    CHECK(pstore::salt_step(Got::Missing, false, true) == SaltStep::MakeNew);     // the first run of a host
    CHECK(pstore::salt_step(Got::Text, false, true) == SaltStep::MakeNew);        // there, no salt in it (kept as .bad)
    CHECK(pstore::salt_step(Got::Unreadable, false, true) == SaltStep::NoKeys);   // the fix: the old salt stays
    CHECK(pstore::salt_step(Got::Unreadable, false, false) == SaltStep::MakeNew); // 0.7.16: every key changed
    CHECK(pstore::world_step(Got::Text, true) == WorldStep::Parse && pstore::world_step(Got::Missing, true) == WorldStep::Empty);
    CHECK(pstore::world_step(Got::Unreadable, true) == WorldStep::Wait);
    CHECK(pstore::world_step(Got::Unreadable, false) == WorldStep::Empty); // 0.7.16: the next write replaced the world's file
    // what a new salt does to the stored players: the same token, another key (why only a missing file may make one)
    CHECK(pstore::stable_key(0x1111, "t:00000000deadbeef") != pstore::stable_key(0x2222, "t:00000000deadbeef"));
}

// NET-4 (BUG-1500): a player owning things only in a world record keeps its name in the file through a marker record
static void test_owner_markers()
{
    printf("owner markers (BUG-1500)\n");
    const uint64_t salt = 99;
    std::string bob = pstore::stable_key(salt, "t:aaaa"), carol = pstore::stable_key(salt, "t:cccc"),
                fresh = pstore::stable_key(salt, "t:ffff");
    Store s;
    s.reset("game0/t.spo");
    s.put_world("tribe.owners", 1, bytes({9, 9})); // the claims name bob and carol (keys inside the world record)
    // without markers: nobody of the name after a round trip (the 0.7.16.1 failure: "0 stored players")
    Store r;
    CHECK(r.parse(s.text(), "game0/t.spo") == Parsed::Ok && r.by_name("Bob", fresh).empty());
    // with markers: names written, found again after the round trip; the host is never marked
    CHECK(s.mark_owners("tribe.owner", {{bob, "Bob"}, {carol, "Carol"}, {"host", "Alice"}}, 100));
    CHECK(!s.mark_owners("tribe.owner", {{bob, "Bob"}, {carol, "Carol"}}, 100)); // nothing new
    CHECK(r.parse(s.text(), "game0/t.spo") == Parsed::Ok);
    CHECK(r.by_name("Bob", fresh) == std::vector<std::string>{bob} && r.name_of(carol) == "Carol");
    CHECK(r.by_name("Alice", fresh).empty());
    // an absent owner (name "" in the map): the stored name stays
    CHECK(!r.mark_owners("tribe.owner", {{bob, ""}, {carol, "Carol"}}, 200) && r.name_of(bob) == "Bob");
    // a key the claims no longer name loses its marker (and the player goes when it has nothing else)
    CHECK(r.mark_owners("tribe.owner", {{bob, ""}}, 300) && r.name_of(carol).empty() && !r.players.count(carol));
    // another feature's marker is its own
    CHECK(r.mark_owners("civ.owner", {{carol, "Carol"}}, 300) && r.get(bob, "tribe.owner") && r.get(carol, "civ.owner"));
    // adopt moves the marker with the player
    CHECK(r.adopt(bob, fresh, "Bob", 400) && r.get(fresh, "tribe.owner") && !r.get(bob, "tribe.owner"));
}

int main()
{
    test_unreadable();
    test_autosave();
    test_new_colonies();
    test_claims();
    test_keys();
    test_round_trip();
    test_bad_files();
    test_name_fallback();
    test_owner_markers();
    test_bounds();
    test_codec();
    test_kept_restore();
    test_token_month();
    printf(g_fail ? "store_tests: %d FAILED\n" : "store_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
