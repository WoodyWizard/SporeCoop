// Native tests for the portable Space co-op decisions (space_logic.h): the co-op jump's target
// (BUG-0072) and the aborted-raid set (BUG-0136); the kept state of a rejoining guest (space_kept.h, C5)
// (make test; built with AddressSanitizer).
#include <cstdio>
#include <map>
#include <set>

#include "rejoin_kept.h"
#include "space_kept.h"
#include "space_logic.h"
#include <string>

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

using spacelogic::JumpPartner;

static void test_jump_target()
{
    printf("jump target\n");
    const uint32_t mine = 0x100, other = 0x200;
    // three players: the first partner is in my system, the second in another one
    std::vector<JumpPartner> v{{0, mine}, {2, other}};
    uint8_t id = 0xEE;
    uint32_t star = 0;
    // the button (can_jump) names partner 2 ...
    CHECK(spacelogic::pick_jump_target(v, mine, 0xFF, true, id, star) && id == 2 && star == other);
    // ... the old jump(0xFF) went to partner 0, in my own system: "already in the partner's system"
    CHECK(spacelogic::pick_jump_target(v, mine, 0xFF, false, id, star) && id == 0 && star == mine);
    // a chosen partner is found even in my system (the jump refuses it with its own message)
    CHECK(spacelogic::pick_jump_target(v, mine, 0, false, id, star) && id == 0);
    // partners whose star is not known are passed over; none left: no target
    std::vector<JumpPartner> w{{1, 0}, {3, mine}};
    CHECK(!spacelogic::pick_jump_target(w, mine, 0xFF, true, id, star));
    CHECK(spacelogic::pick_jump_target(w, other, 0xFF, true, id, star) && id == 3);
}

// space_allies watch_raids with one raid address the allocator gives twice: an accepted raid
// aborted before it was watched, then a new raid at the same address that is watched and won
static bool second_raid_counts_as_won(bool prune)
{
    std::set<void*> aborted;
    std::map<void*, int> watched;
    int mission;
    void* addr = &mission;
    aborted.insert(addr); // helper_raids aborts it in state 2: never watched
    if (prune)
        spacelogic::prune_aborted(aborted, watched); // the next watch_raids
    // later: a raid at the recycled address, watched while active, then won (gone from the list)
    watched[addr] = 3;
    if (prune)
        spacelogic::prune_aborted(aborted, watched);
    bool was_aborted = aborted.erase(addr) != 0; // watch_raids: "aborted", the win is skipped
    watched.erase(addr);
    return !was_aborted;
}

static void test_aborted()
{
    printf("aborted raids\n");
    CHECK(!second_raid_counts_as_won(false)); // the code before BUG-0136
    CHECK(second_raid_counts_as_won(true));
    // an aborted raid that is watched stays until watch_raids handles its end
    std::set<void*> aborted;
    std::map<void*, int> watched;
    int a, b;
    aborted.insert(&a);
    aborted.insert(&b);
    watched[&a] = 1;
    spacelogic::prune_aborted(aborted, watched);
    CHECK(aborted.size() == 1 && aborted.count(&a));
}

// BUG-0227: the armada wave on the host's return
static void test_wave_return()
{
    using spacelogic::WaveReturn;
    for (bool running : {false, true}) {
        CHECK(spacelogic::wave_on_return(6, running) == WaveReturn::Fail);
        CHECK(spacelogic::wave_on_return(5, running) == WaveReturn::Won);
    }
    CHECK(spacelogic::wave_on_return(0, true) == WaveReturn::Resume);
    CHECK(spacelogic::wave_on_return(0, false) == WaveReturn::NewRaid);
    // the "raised once" key: by serial a new ship at an old address is new; the kinds never match
    std::set<uint64_t> raised;
    raised.insert(spacelogic::tough_key(0x1234560, 0, false)); // a pointer key (the reuse simulation)
    CHECK(raised.insert(spacelogic::tough_key(0x1234560, 77, true)).second);   // by serial: raised
    CHECK(!raised.insert(spacelogic::tough_key(0x1234560, 77, true)).second);  // the same ship: once
    CHECK(raised.insert(spacelogic::tough_key(0x1234560, 78, true)).second);   // a new object there
    CHECK(!raised.insert(spacelogic::tough_key(0x1234560, 99, false)).second); // by pointer: not raised
    CHECK(spacelogic::tough_key(77, 0, false) != spacelogic::tough_key(0, 77, true));
}

// BUG-0225 (#159): where an owed refund is paid (the whole owed_action table)
static void test_owed_action()
{
    printf("owed action\n");
    using spacelogic::OwedAction;
    using spacelogic::WorldLedger;
    const std::string A = "c:\\games\\game0\\madas.spo", A2 = "c:\\games\\game1\\madas.spo",
                      B = "c:\\games\\game2\\other.spo";
    {   // the same world in memory: paid, saved or not, whatever game key now
        WorldLedger l;
        auto t = l.tag(5, A);
        CHECK(spacelogic::owed_action(l, t, 5, A) == OwedAction::Pay);
        CHECK(spacelogic::owed_action(l, t, 5, "") == OwedAction::Pay);
    }
    {   // the same file loaded again, not saved since the take: the reloaded game still has it
        WorldLedger l;
        auto t = l.tag(5, A);
        CHECK(spacelogic::owed_action(l, t, 6, A) == OwedAction::Drop);
        // a save made BEFORE the take does not count
        WorldLedger l2;
        l2.note_save(5, A);
        auto t2 = l2.tag(5, A);
        CHECK(spacelogic::owed_action(l2, t2, 6, A) == OwedAction::Drop);
    }
    {   // saved after the take (that world), then loaded again: the file lacks it
        WorldLedger l;
        auto t = l.tag(5, A);
        l.note_save(5, A);
        CHECK(spacelogic::owed_action(l, t, 6, A) == OwedAction::Pay);
        // a save of a reload of that file (epoch 6, which still had the items) does not count
        WorldLedger l2;
        auto t2 = l2.tag(5, A);
        l2.note_save(6, A);
        CHECK(spacelogic::owed_action(l2, t2, 7, A) == OwedAction::Drop);
        // a save of another game does not count (the 0.7.3.0 save counter did)
        WorldLedger l3;
        auto t3 = l3.tag(5, A);
        l3.note_save(6, B);
        CHECK(spacelogic::owed_action(l3, t3, 7, A) == OwedAction::Drop);
    }
    {   // a failed save marks nothing: the caller does not call note_save; unknown keys are ignored
        WorldLedger l;
        auto t = l.tag(5, A);
        l.note_save(5, "");
        l.note_save(0, A);
        CHECK(spacelogic::owed_action(l, t, 6, A) == OwedAction::Drop);
    }
    {   // another game now: kept (also when that game was saved), paid later in its own game
        WorldLedger l;
        auto t = l.tag(5, A);
        l.note_save(5, A);
        CHECK(spacelogic::owed_action(l, t, 6, B) == OwedAction::Keep);
        CHECK(spacelogic::owed_action(l, t, 6, A2) == OwedAction::Keep); // same planet, other slot
        CHECK(spacelogic::owed_action(l, t, 6, "") == OwedAction::Keep); // unknown now
        CHECK(spacelogic::owed_action(l, t, 7, A) == OwedAction::Pay);
    }
    {   // the tag's game is unknown: dropped once the world changed
        WorldLedger l;
        auto t = l.tag(5, "");
        CHECK(spacelogic::owed_action(l, t, 5, A) == OwedAction::Pay);
        CHECK(spacelogic::owed_action(l, t, 6, A) == OwedAction::Drop);
        CHECK(spacelogic::owed_action(l, t, 6, "") == OwedAction::Drop);
    }
    {   // a received world replaced the slot: dropped, even when saved after the take
        WorldLedger l;
        auto t = l.tag(5, A);
        l.note_save(5, A);
        l.note_replaced(spacelogic::game_dir(A));
        CHECK(spacelogic::owed_action(l, t, 6, A) == OwedAction::Drop);
        CHECK(spacelogic::owed_action(l, t, 6, B) == OwedAction::Drop);
        // a replacement BEFORE the take, or of another slot, does not count
        WorldLedger l2;
        l2.note_replaced(spacelogic::game_dir(A));
        auto t2 = l2.tag(5, A);
        l2.note_replaced(spacelogic::game_dir(B));
        l2.note_save(5, A);
        CHECK(spacelogic::owed_action(l2, t2, 6, A) == OwedAction::Pay);
    }
    CHECK(spacelogic::game_dir("c:\\x\\y.spo") == "c:\\x" && spacelogic::game_dir("y.spo").empty());
}

// C5 (space_kept.h): the host's store and the guest's window, through the cases of BUG-0195, 0678, 0786, 0810, 1029,
// 1034 and 1107
using spacekept::KeptStore;
using spacekept::RestoreWindow;

// the host's side of one feature as space_trade / space_badges drive it: a report (fresh: the first of a world the
// guest loaded from the host) either is kept or, for a player with a kept state, makes the store send that back
struct HostSim {
    KeptStore<int> st;
    std::vector<std::pair<uint8_t, int>> out; // (to, value) sent back
    // false: not kept (a restore is running or started)
    bool report(const std::string& key, uint8_t id, bool fresh, int v, double now)
    {
        auto& e = st.bind(key, id, key, false);
        if (fresh && e.have) {
            st.begin(e, now);
            out.push_back({id, e.value});
            return false;
        }
        return st.keep(e, v);
    }
};

static void test_kept_store()
{
    printf("kept store\n");
    HostSim h;
    CHECK(!h.st.world(0) && h.st.world(5) && !h.st.world(5));
    // a first join: nothing kept, the fresh report is the player's state
    CHECK(h.report("tokA", 1, true, 100, 0) && h.out.empty());
    CHECK(h.report("tokA", 1, false, 120, 1) && h.st.find("tokA")->value == 120);
    // the guest leaves (the per-frame check), rejoins as id 2: its fresh report (the host's copy, 999) brings the
    // kept 120 back and is not kept itself
    h.st.tick(2, [](uint8_t) { return false; }, [](auto&) {});
    CHECK(h.st.entries()["tokA"].id == spacekept::kNoId);
    CHECK(!h.report("tokA", 2, true, 999, 3) && h.out.size() == 1 && h.out[0] == std::make_pair((uint8_t)2, 120));
    // while the restore runs a report is the old copy
    CHECK(!h.report("tokA", 2, false, 999, 4) && h.st.find("tokA")->value == 120);
    // an answer with another serial, or from another player, does not end it
    uint32_t serial = h.st.entries()["tokA"].serial;
    CHECK(!h.st.done(2, serial + 1, true) && !h.st.done(3, serial, true));
    // no answer for 10 s: sent again (and only then)
    int resent = 0;
    auto here = [](uint8_t) { return true; };
    auto again = [&](spacekept::Entry<int>& e) { resent++; h.st.begin(e, 20); };
    h.st.tick(12.9, here, again);
    CHECK(resent == 0);
    h.st.tick(13.1, here, again);
    CHECK(resent == 1 && h.st.entries()["tokA"].serial != serial);
    CHECK(!h.st.done(2, serial, true)); // the first one's answer: old
    CHECK(h.st.done(2, h.st.entries()["tokA"].serial, true));
    CHECK(h.report("tokA", 2, false, 130, 21) && h.st.find("tokA")->value == 130);
    CHECK(h.st.sent_back() == 2 && h.st.answered() == 1);

    // BUG-1107: another game of the same name has another key: it gets nothing of tokA's, and tokA's stays
    CHECK(h.report("tokB", 3, true, 50, 22) && h.out.size() == 1);
    CHECK(h.st.find("tokA")->value == 130 && h.st.find("tokB")->value == 50);

    // session end in the middle of a restore: nobody bound, nobody restoring; the values stay for the next session
    h.st.tick(23, [](uint8_t id) { return id != 2; }, [](auto&) {});
    CHECK(!h.report("tokA", 1, true, 999, 24) && h.st.entries()["tokA"].restoring);
    h.st.session_end();
    CHECK(!h.st.entries()["tokA"].restoring && h.st.bound(1) == 0 && h.st.find("tokA")->value == 130);
    CHECK(!h.report("tokA", 1, true, 999, 30) && h.out.size() == 3 && h.out[2].second == 130);

    // a rejoin into another Space world: the old world's state is not this one's
    CHECK(h.st.world(6) && h.st.size() == 0);
    CHECK(h.report("tokA", 1, true, 999, 31) && h.st.find("tokA")->value == 999);
}

// space_map: the roster's own colonies; the first roster after a join brings the kept ones back; a crash rejoin
// that keeps the player id (BUG-0810) needs the session's leave
static bool map_crash_rejoin_restores(bool leave_fix)
{
    KeptStore<std::vector<uint32_t>> st;
    int sent = 0;
    auto roster = [&](const std::string& key, uint8_t id, std::vector<uint32_t> own) {
        bool first = false;
        auto& e = st.bind(key, id, key, true, &first);
        if (first && !e.value.empty()) {
            st.begin(e, 0);
            sent++;
            return;
        }
        if (!e.restoring)
            e.value = std::move(own);
    };
    roster("tok", 1, {0x01000005});
    if (leave_fix)
        st.leave(1); // PLAYER_LEAVE and the new HELLO in one message: the same id again
    roster("tok", 1, {}); // the rejoined guest's first roster: the host's world, without its colony
    return sent == 1 && st.entries()["tok"].value == std::vector<uint32_t>{0x01000005};
}

static void test_kept_map()
{
    printf("kept colonies\n");
    CHECK(map_crash_rejoin_restores(true));
    CHECK(!map_crash_rejoin_restores(false)); // the code before BUG-0810: the empty roster became the kept list
    // exclusive: a newcomer with a departed player's id does not find that player's entry bound to it
    KeptStore<std::vector<uint32_t>> st;
    st.bind("old", 1, "Bob", true);
    bool first = false;
    st.bind("new", 1, "Bob", true, &first);
    CHECK(first && st.entries()["old"].id == spacekept::kNoId && st.entries()["new"].id == 1);
    // the answer names the colonies the guest could not take: the host drops them (done() without a serial)
    auto& e = st.entries()["new"];
    e.value = {1, 2, 3};
    st.begin(e, 0);
    auto* d = st.done(1, 0, false);
    CHECK(d == &e && !e.restoring);
    std::vector<uint32_t> failed{2};
    std::erase_if(d->value, [&](uint32_t p) { return p == failed[0]; });
    CHECK((e.value == std::vector<uint32_t>{1, 3}));
    CHECK(!st.done(1, 0, false)); // no restore running
}

// space_rivalry: keyed budgets (K_MONEY puts, K_ASK finds; a newcomer starts with 0, BUG-1029)
static void test_kept_rivals()
{
    printf("kept budgets\n");
    KeptStore<int> st;
    CHECK(!st.find("a"));
    st.put("a", 7000);
    st.put("a", 6500);
    CHECK(st.find("a") && st.find("a")->value == 6500 && !st.find("b"));
    st.put("b", 0); // a newcomer
    CHECK(st.find("b")->value == 0 && st.size() == 2);
    st.clear(); // the mode switch / the session end
    CHECK(!st.find("a") && st.size() == 0);
}

// the guest's window (space_trade's inv_restore_pending, space_badges, space_map) and the queue of BUG-1034
static void test_restore_window()
{
    printf("restore window\n");
    RestoreWindow w;
    // joined while its own Space world (3) ran: that world's state is its own, never replaced
    w.joined(true, 3);
    w.joined(true, 4); // only the first frame counts
    CHECK(w.join == 3 && !w.from_host(3) && !w.from_host(0) && w.from_host(4));
    CHECK(!w.open(3) && !w.pending(3, 0, 20) && !w.may_apply(3));
    // the host's world (4) loaded: open until the restore; the fresh report goes, 20 s at most without an answer
    CHECK(w.open(4) && w.pending(4, 0, 20) && w.is_fresh(4));
    w.sent(4, true, 100);
    CHECK(!w.is_fresh(4) && w.pending(4, 119.9, 20) && !w.pending(4, 120.1, 20));
    // gifts and sales inside the window wait, in order, at most 64
    spacekept::HeldQueue q;
    uint8_t body[3] = {1, 2, 3};
    for (int i = 0; i < 70; i++)
        if (w.pending(4, 105, 20) && q.hold(70, (uint8_t)i, body, 3)) {
        }
    CHECK(q.size() == spacekept::HeldQueue::kMax && q.held() == spacekept::HeldQueue::kMax);
    // the kept copy written: settled; what waited is handled now, in order
    CHECK(w.may_apply(4));
    w.applied = 4;
    CHECK(!w.open(4) && !w.pending(4, 105, 20) && !w.may_apply(4));
    auto held = q.take();
    CHECK(held.size() == 64 && held[0].origin == 0 && held[63].origin == 63 && held[5].payload.size() == 3 && q.empty());
    // "nothing kept for you": settled too
    w.sent(5, true, 200);
    CHECK(w.pending(5, 201, 20));
    w.none = 5;
    CHECK(!w.pending(5, 201, 20));
    // a new session: a new join (outside Space: every Space world after it is the host's); what was applied stays
    w.session_end();
    w.joined(false, 7);
    CHECK(w.join == 0 && w.from_host(7) && w.is_fresh(4) && w.applied == 4 && w.none == 0);
}

// BUG-1135: the tokens of clean leaves on disk: same host and name only, no readable host or name, expiry, bound
static void test_rejoin_kept()
{
    printf("rejoin kept file\n");
    using rejoin::KeptFile;
    const int64_t t0 = 1'800'000'000;
    KeptFile f;
    CHECK(f.find("127.0.0.1:47820", "Bob", t0) == 0); // no salt yet: nothing, and put does nothing either
    f.put("127.0.0.1:47820", "Bob", 0x1111, t0);
    CHECK(f.size() == 0);
    f.ensure_salt(0xABCDEF0123456789ull);
    f.put("127.0.0.1:47820", "Bob", 0x1111, t0);
    f.put("127.0.0.1:47820", "Carol", 0x2222, t0);
    f.put("10.0.0.5:47820", "Bob", 0x3333, t0);
    CHECK(f.find("127.0.0.1:47820", "Bob", t0 + 60) == 0x1111);
    CHECK(f.find("127.0.0.1:47820", "Carol", t0 + 60) == 0x2222);
    CHECK(f.find("10.0.0.5:47820", "Bob", t0 + 60) == 0x3333); // another host: its own token, never Bob's of the first
    CHECK(f.find("127.0.0.1:47820", "bob", t0) == 0);         // another name (case matters, as the session's names)
    CHECK(f.find("127.0.0.1:47821", "Bob", t0) == 0);         // another port is another host address
    CHECK(f.find("127.0.0.1:47820", "Bo", t0) == 0 && f.find("127.0.0.1:47820B", "ob", t0) == 0); // no shifted split
    // the text holds no host address, no name and no port
    std::string t = f.text();
    CHECK(t.find("127.0.0.1") == std::string::npos && t.find("Bob") == std::string::npos &&
          t.find("47820") == std::string::npos && t.find("10.0.0.5") == std::string::npos);
    // round trip
    KeptFile g;
    CHECK(g.parse(t) && g.size() == 3 && g.find("127.0.0.1:47820", "Bob", t0 + 1) == 0x1111);
    // another salt (another PC, a new file): the same pair has another key
    KeptFile h;
    h.ensure_salt(0x1234);
    CHECK(h.key("127.0.0.1:47820", "Bob") != f.key("127.0.0.1:47820", "Bob"));
    // the pair's newer leave replaces its token
    f.put("127.0.0.1:47820", "Bob", 0x4444, t0 + 100);
    CHECK(f.size() == 3 && f.find("127.0.0.1:47820", "Bob", t0 + 101) == 0x4444);
    // expiry: a day
    CHECK(f.find("127.0.0.1:47820", "Carol", t0 + KeptFile::kTtl) == 0x2222);
    CHECK(f.find("127.0.0.1:47820", "Carol", t0 + KeptFile::kTtl + 1) == 0);
    CHECK(f.find("127.0.0.1:47820", "Carol", t0 - 10) == 0); // a clock set back: not used
    CHECK(f.expire(t0 + KeptFile::kTtl + 50) && f.size() == 1 && f.find("127.0.0.1:47820", "Bob", t0 + KeptFile::kTtl + 50) == 0x4444);
    // bounded: the oldest go
    KeptFile b;
    b.ensure_salt(7);
    for (int i = 0; i < 40; i++)
        b.put("h", "p" + std::to_string(i), 100 + i, t0 + i);
    CHECK(b.size() == KeptFile::kMax);
    CHECK(b.find("h", "p0", t0 + 40) == 0 && b.find("h", "p39", t0 + 40) == 139 && b.find("h", "p24", t0 + 40) == 124);
    CHECK(b.find("h", "p23", t0 + 40) == 0);
    // a damaged or foreign file is an empty store; a truncated line is skipped
    KeptFile d;
    CHECK(!d.parse("garbage\n0123 4567 8\n") && d.size() == 0);
    CHECK(!d.parse("") && d.size() == 0);
    CHECK(d.parse("sporecoop-rejoin 1 00000000000000ff\n00000000000000aa 00000000000000bb 5\nzz\n") && d.size() == 1);
    // more lines than the bound: only kMax are read
    std::string big = "sporecoop-rejoin 1 0000000000000001\n";
    for (int i = 1; i <= 50; i++)
        big += std::to_string(1000 + i) + " " + std::to_string(2000 + i) + " 5\n";
    CHECK(d.parse(big) && d.size() == KeptFile::kMax);
}

int main()
{
    test_jump_target();
    test_aborted();
    test_wave_return();
    test_owed_action();
    test_kept_store();
    test_kept_map();
    test_kept_rivals();
    test_restore_window();
    test_rejoin_kept();
    printf(g_fail ? "space_tests: %d FAILED\n" : "space_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
