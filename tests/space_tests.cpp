// Native tests for the portable Space co-op decisions (space_logic.h): the co-op jump's target
// (BUG-0072) and the aborted-raid set (BUG-0136); the kept state of a rejoining guest (space_kept.h, C5)
// (make test; built with AddressSanitizer).
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "rejoin_kept.h"
#include "space_kept.h"
#include "space_logic.h"
#include "squadron_logic.h"
#include "worldobj_space.h"
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

static void test_jump_rivals()
{
    printf("jump target in Space Rivals (BUG-1562)\n");
    const uint32_t mine = 0x100, other1 = 0x200, other2 = 0x300;
    uint8_t id = 0xEE;
    uint32_t star = 0;
    bool blocked = false;

    // 1. Single Neutral/War rival in another star
    std::vector<JumpPartner> rival_only{{1, other1, false}};
    // Space Rivals OFF (or fix off): rival is allowed for both any-partner and explicit jump
    CHECK(spacelogic::pick_jump_target(rival_only, mine, 0xFF, true, id, star, false, &blocked) && id == 1 &&
          star == other1 && !blocked);
    CHECK(spacelogic::pick_jump_target(rival_only, mine, 1, false, id, star, false, &blocked) && id == 1 &&
          star == other1 && !blocked);
    // Space Rivals ON: both any-partner and explicit jump are blocked with blocked_rival=true
    id = 0xEE;
    star = 0;
    CHECK(!spacelogic::pick_jump_target(rival_only, mine, 0xFF, true, id, star, true, &blocked) && blocked && star == 0);
    CHECK(!spacelogic::pick_jump_target(rival_only, mine, 1, false, id, star, true, &blocked) && blocked && star == 0);

    // 2. Rival with unknown star (galaxy view): explicit jump still reports blocked_rival=true
    std::vector<JumpPartner> rival_unknown{{1, 0, false}};
    CHECK(!spacelogic::pick_jump_target(rival_unknown, mine, 1, false, id, star, true, &blocked) && blocked);

    // 3. Allied partner in another star: allowed for both any-partner and explicit jump
    std::vector<JumpPartner> ally_only{{1, other1, true}};
    CHECK(spacelogic::pick_jump_target(ally_only, mine, 0xFF, true, id, star, true, &blocked) && id == 1 &&
          star == other1 && !blocked);
    CHECK(spacelogic::pick_jump_target(ally_only, mine, 1, false, id, star, true, &blocked) && id == 1 &&
          star == other1 && !blocked);

    // 4. Mixed roster: non-allied rival first, allied partner second -> any-partner skips rival and picks ally
    std::vector<JumpPartner> mixed{{1, other1, false}, {2, other2, true}};
    CHECK(spacelogic::pick_jump_target(mixed, mine, 0xFF, true, id, star, true, &blocked) && id == 2 && star == other2);
    // Explicit jump to rival 1 is still refused; explicit jump to ally 2 succeeds
    star = 0;
    CHECK(!spacelogic::pick_jump_target(mixed, mine, 1, false, id, star, true, &blocked) && blocked && star == 0);
    CHECK(spacelogic::pick_jump_target(mixed, mine, 2, false, id, star, true, &blocked) && !blocked && id == 2 &&
          star == other2);

    // 5. Rival in another star + ally in my own star: skip_mine=true fails (can_jump false), skip_mine=false finds ally
    std::vector<JumpPartner> ally_here{{1, other1, false}, {2, mine, true}};
    star = 0;
    CHECK(!spacelogic::pick_jump_target(ally_here, mine, 0xFF, true, id, star, true, &blocked) && blocked && star == 0);
    CHECK(spacelogic::pick_jump_target(ally_here, mine, 0xFF, false, id, star, true, &blocked) && id == 2 &&
          star == mine);

    // 6. Allied partner with unknown star: returns false with blocked_rival=false (not a rival refusal)
    std::vector<JumpPartner> ally_unknown{{2, 0, true}};
    CHECK(!spacelogic::pick_jump_target(ally_unknown, mine, 0xFF, true, id, star, true, &blocked) && !blocked);
    CHECK(!spacelogic::pick_jump_target(ally_unknown, mine, 2, false, id, star, true, &blocked) && !blocked);
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

// NET-2 (BUG-1498): the slot's token counts only for the host and name it was written for
static void test_rejoin_slot_pair()
{
    printf("rejoin slot pair (BUG-1498)\n");
    using rejoin::SlotLine;
    const uint64_t a = SlotLine::pair_of("127.0.0.1:47820", "Bob"), b = SlotLine::pair_of("10.0.0.5:47820", "Bob");
    CHECK(a && b && a != b && a != SlotLine::pair_of("127.0.0.1:47820", "Carol"));
    CHECK(a == SlotLine::pair_of("127.0.0.1:47820", "Bob")); // stable: a restart after a crash finds it
    std::string t = SlotLine::text(0x1234, a);
    CHECK(t.find("127.0.0.1") == std::string::npos && t.find("Bob") == std::string::npos);
    SlotLine s;
    CHECK(s.parse(t) && s.token == 0x1234 && s.pair == a);
    CHECK(s.usable_for(a, true));  // the same host and name (a crash rejoin): the token goes again
    CHECK(!s.usable_for(b, true)); // another host (a failed join, a crash then another host): not sent
    CHECK(s.usable_for(b, false)); // net.rejoin_slot_pair off: as 0.7.16.1, any join
    // a slot an older version wrote (the token alone): any join, as before
    SlotLine o;
    CHECK(o.parse("0000000000abcdef\n") && o.token == 0xabcdef && o.pair == 0 && o.usable_for(b, true));
    // an empty or damaged slot: no token
    SlotLine e;
    CHECK(!e.parse("") && !e.parse("xyz") && !e.parse("0000000000000000 0000000000000001\n") && !e.usable_for(a, true));
    CHECK(SlotLine::text(0, a).empty());
}

// BUG-1288 (WP-S1 of 0.7.17): the host's table of guest leaders and the roles (squadron_logic.h)
static void test_fleet_leads()
{
    printf("fleet leaders (BUG-1288)\n");
    using namespace squadron_logic;
    const uint32_t S = kSpaceMode;
    const Place A{S, 0x80000011u}, B{S, 0x80000022u}, galaxy{S, 0};
    auto peer = [](uint8_t id, const Place& p, bool capable = true) {
        return PeerPlace{id, p.mode, p.world, true, capable};
    };
    // a place with the host has no row; one guest alone has an inactive row (not announced)
    std::vector<PeerPlace> v{peer(0, A), peer(1, A), peer(2, B)};
    auto t = step_leads({}, v, 0, true);
    CHECK(t.size() == 1 && t[0].place == B && t[0].leader == 2 && !t[0].active() && announced(t).empty());
    // a second guest comes later with a lower id: the first one there keeps the lead (sticky, not the lowest id)
    v = {peer(0, A), peer(1, B), peer(2, B)};
    auto t2 = step_leads(t, v, 1, true);
    CHECK(t2.size() == 1 && t2[0].leader == 2 && t2[0].active() && announced(t2).size() == 1);
    CHECK(!same_announced(t, t2) && same_announced(t2, step_leads(t2, v, 2, true)));
    // the host leaves A, both guests there: a new row, the lowest id leads
    v = {peer(0, B), peer(1, A), peer(2, A)};
    auto t3 = step_leads({}, v, 0, true);
    CHECK(t3.size() == 1 && t3[0].place == A && t3[0].leader == 1 && t3[0].active());
    // the host comes back to A: no row (the host leads, 0.7.16)
    v = {peer(0, A), peer(1, A), peer(2, A)};
    CHECK(step_leads(t3, v, 1, true).empty());
    // the leader zooms out (galaxy view): it keeps the place for kLeaderGrace, then the follower leads
    v = {peer(0, A), peer(1, galaxy), peer(2, B)};
    std::vector<Lead> lb{{B, 1, 2, -1}};
    auto g1 = step_leads(lb, v, 10, true);
    CHECK(g1.size() == 1 && g1[0].leader == 1 && g1[0].away_since == 10 && g1[0].active() && g1[0].present == 1);
    auto g2 = step_leads(g1, v, 10 + kLeaderGrace - 0.5, true);
    CHECK(g2.size() == 1 && g2[0].leader == 1 && g2[0].away_since == 10);
    auto g3 = step_leads(g2, v, 10 + kLeaderGrace + 0.1, true);
    CHECK(g3.size() == 1 && g3[0].leader == 2 && g3[0].away_since < 0 && !g3[0].active());
    // ... back within the grace: still the leader, no longer away
    v = {peer(0, A), peer(1, B), peer(2, B)};
    auto g4 = step_leads(g2, v, 13, true);
    CHECK(g4.size() == 1 && g4[0].leader == 1 && g4[0].away_since < 0);
    // the leader went to another valid place, or left the session: a new leader at once
    v = {peer(0, A), peer(1, A), peer(2, B), peer(3, B)};
    auto n1 = step_leads(lb, v, 0, true);
    CHECK(n1.size() == 1 && n1[0].leader == 2);
    v = {peer(0, A), peer(2, B), peer(3, B)};
    CHECK(step_leads(lb, v, 0, true)[0].leader == 2);
    // an older guest (space 1.2) is never named and never counts
    v = {peer(0, A), peer(1, B, false), peer(2, B)};
    auto o1 = step_leads({}, v, 0, true);
    CHECK(o1.size() == 1 && o1[0].leader == 2 && !o1[0].active());
    v = {peer(0, A), peer(1, B, false), peer(2, B, false)};
    CHECK(step_leads({}, v, 0, true).empty());
    // a guest without a state, the galaxy view, another mode: no place
    v = {peer(0, A), {1, S, B.world, false, true}, peer(2, galaxy), peer(3, Place{0x1654C04u, 0x1234})};
    CHECK(step_leads({}, v, 0, true).empty());
    // the switch off (or Rivals): no rows at all
    v = {peer(0, A), peer(1, B), peer(2, B)};
    CHECK(step_leads(t2, v, 0, false).empty());
    // two guest-led places at once (4 players: host elsewhere, two pairs)
    v = {peer(0, galaxy), peer(1, A), peer(2, B), peer(3, A)};
    auto two = step_leads({{B, 2, 1, -1}}, v, 0, true);
    CHECK(two.size() == 2 && find_lead(two, A) && find_lead(two, A)->leader == 1 && find_lead(two, A)->active() &&
          find_lead(two, B)->leader == 2 && !find_lead(two, B)->active());

    // the roles: the host always leads; with the host here a guest mirrors it (0.7.16), whatever the table says
    std::vector<Lead> ann{{B, 2, 2, -1}};
    auto r = local_role(true, 0, A, false, true, true, ann);
    CHECK(r.role == FleetRole::Leader && r.leader == 0);
    r = local_role(false, 1, B, true, true, true, ann);
    CHECK(r.role == FleetRole::Mirror && r.leader == 0);
    r = local_role(false, 1, B, false, true, true, ann);
    CHECK(r.role == FleetRole::Mirror && r.leader == 2);
    r = local_role(false, 2, B, false, true, true, ann);
    CHECK(r.role == FleetRole::Leader && r.leader == 2);
    // the switch off, an older host, the galaxy view, a place without a row: None (0.7.16 away from the host)
    CHECK(local_role(false, 1, B, false, false, true, ann).role == FleetRole::None);
    CHECK(local_role(false, 1, B, false, true, false, ann).role == FleetRole::None);
    CHECK(local_role(false, 1, galaxy, false, true, true, ann).role == FleetRole::None);
    CHECK(local_role(false, 1, A, false, true, true, ann).role == FleetRole::None);
    CHECK(local_role(false, 1, A, true, false, false, {}).role == FleetRole::Mirror); // fix off with the host: 0.7.16

    // the relay checks: only the named leader's state, a follower's hit to the leader, the leader's damage to a follower
    std::vector<PeerPlace> pp{peer(0, A), peer(1, B), peer(2, B), peer(3, A)};
    std::vector<Lead> tab{{B, 2, 2, -1}};
    CHECK(can_forward_state(2, 2, B, tab));
    CHECK(!can_forward_state(1, 1, B, tab) && !can_forward_state(2, 1, B, tab) && !can_forward_state(2, 2, A, tab));
    CHECK(!can_forward_state(2, 2, B, {{B, 2, 1, -1}})); // an inactive row: nobody to send to
    CHECK(should_forward_state_to(pp[1], B, 2) && !should_forward_state_to(pp[2], B, 2) &&
          !should_forward_state_to(pp[0], B, 2) && !should_forward_state_to(pp[3], B, 2));
    CHECK(!should_forward_state_to(peer(1, B, false), B, 2)); // an older guest never gets MSG_UFO_LEAD
    CHECK(can_forward_hit(1, 2, B, tab, pp));
    CHECK(!can_forward_hit(3, 2, B, tab, pp) && !can_forward_hit(1, 1, B, tab, pp) && !can_forward_hit(2, 2, B, tab, pp) &&
          !can_forward_hit(0, 2, B, tab, pp) && !can_forward_hit(1, 2, A, tab, pp));
    CHECK(can_forward_damage(2, 2, 1, B, tab, pp));
    CHECK(!can_forward_damage(1, 1, 2, B, tab, pp) && !can_forward_damage(2, 2, 3, B, tab, pp) &&
          !can_forward_damage(2, 2, 0, B, tab, pp) && !can_forward_damage(2, 2, 2, B, tab, pp) &&
          !can_forward_damage(2, 1, 1, B, tab, pp));

    // what a role change does: the host-led changes of 0.7.16 do nothing (mirror_tick drops the host's mirrors)
    auto tr = step_role(FleetRole::Mirror, 0, A, FleetRole::None, kNoLeader, galaxy);
    CHECK(tr.changed && !tr.drop_mirrors && !tr.clear_leader && !tr.reset_rx);
    tr = step_role(FleetRole::Mirror, 0, A, FleetRole::Mirror, 0, B);
    CHECK(tr.changed && !tr.drop_mirrors && !tr.reset_rx);
    tr = step_role(FleetRole::Leader, 0, A, FleetRole::Leader, 0, B); // the host's ids are never cleared
    CHECK(tr.changed && !tr.clear_leader);
    // no promotion: the host leaves, a follower becomes the leader and drops its mirrors (2 players never get here:
    // a guest alone has no announced row)
    tr = step_role(FleetRole::Mirror, 0, A, FleetRole::Leader, 1, A);
    CHECK(tr.drop_mirrors && tr.clear_leader && tr.reset_rx);
    tr = step_role(FleetRole::Mirror, 0, A, FleetRole::Mirror, 1, A);
    CHECK(tr.drop_mirrors && !tr.clear_leader && tr.reset_rx);
    tr = step_role(FleetRole::Mirror, 2, B, FleetRole::Leader, 1, B);
    CHECK(tr.drop_mirrors && tr.clear_leader && tr.reset_rx);
    // the host arrives where a guest led: the leader's ids go, the followers' mirrors of it go
    tr = step_role(FleetRole::Leader, 2, B, FleetRole::Mirror, 0, B);
    CHECK(!tr.drop_mirrors && tr.clear_leader && tr.reset_rx);
    tr = step_role(FleetRole::Mirror, 2, B, FleetRole::Mirror, 0, B);
    CHECK(tr.drop_mirrors && tr.reset_rx);
    tr = step_role(FleetRole::Leader, 2, B, FleetRole::None, kNoLeader, galaxy);
    CHECK(tr.clear_leader && !tr.drop_mirrors && !tr.reset_rx);
    CHECK(!step_role(FleetRole::Mirror, 2, B, FleetRole::Mirror, 2, B).changed);
}

// WP-X1 of 0.7.17 (audit SPC-3, 6, 11, 15, 17): BUG-1482, 1485, 1486, 1489, 1491
static void test_audit_0717()
{
    printf("audit 0.7.17 (space)\n");
    using namespace spacelogic;
    // BUG-1485: a payment goes into the game now only in Space, in the world the spice came from
    CHECK(sale_pays_now(true, true, 4, 4));
    CHECK(!sale_pays_now(true, true, 4, 5));  // another Space world (a load, an own save)
    CHECK(!sale_pays_now(false, true, 4, 4)); // loading, an editor
    CHECK(sale_pays_now(true, false, 4, 5));  // no world tags (space.owed_id off): as before
    // BUG-1486: the host's colony only
    const uint32_t me = 0x10, npc = 0x22;
    CHECK(host_colony(5, {me}, me, false));
    CHECK(host_colony(5, {npc, me}, me, false));
    CHECK(!host_colony(5, {npc}, me, false));    // an NPC empire's colony
    CHECK(!host_colony(5, {}, me, false));       // no civilization data
    CHECK(!host_colony(4, {me}, me, false));     // not a colony
    CHECK(!host_colony(5, {me}, me, true));      // a partner's own-star mirror colony (my pol)
    CHECK(!host_colony(5, {0xFFFFFFFFu}, 0xFFFFFFFFu, false)); // no empire
    // BUG-1489: non-finite deltas refused, finite ones clamped
    float o = 7;
    CHECK(terra_delta(0.05f, o) && o == 0.05f);
    CHECK(terra_delta(-0.2f, o) && o == -0.2f);
    CHECK(terra_delta(1e30f, o) && o == kTerraDeltaMax);
    CHECK(terra_delta(-5.f, o) && o == -kTerraDeltaMax);
    o = 7;
    CHECK(!terra_delta(std::nanf(""), o) && o == 7);
    CHECK(!terra_delta(INFINITY, o) && !terra_delta(-INFINITY, o));
    // BUG-1482: the adopted budget's hold
    AdoptHold h;
    CHECK(h.report(123) && !h.due(0)); // no hold: every report is the budget
    h.start(9000, 100);
    CHECK(h.due(100) && !h.due(101) && h.due(102.5)); // resent every 2 s
    CHECK(!h.report(0));                              // the guest's money before the K_SET: not its budget
    CHECK(h.on && h.report(9000) && !h.on);           // its report of the adopted budget ends the hold
    CHECK(h.report(8000) && !h.due(200));
    h.start(5, 0);
    CHECK(h.due(0) && !h.due(kAdoptGiveUp + 1) && !h.on && h.report(1)); // never confirmed: given up
    // BUG-1491: an offer with no answer after the receiver's own life plus the slack comes back
    CHECK(!offer_unanswered(10, 10 + 120 + kOfferAnswerSlack - 1, 120));
    CHECK(offer_unanswered(10, 10 + 120 + kOfferAnswerSlack + 1, 120));
}

static void test_colonize_mission()
{
    printf("space colonize tutorial mission (BUG-1539)\n");
    using namespace spacelogic;
    // Target planet colonized by an allied partner advances state 0 and state 1
    CHECK(colonize_mission_done_by_partner(true, true, true, false, 3, 0, kColonizeTutorialId, true, false));
    CHECK(colonize_mission_done_by_partner(true, true, true, false, 3, 1, 0x12345678u, true, false));
    // Initial tutorial completed by partner advances 0x398ACA5E even on another planet, not arbitrary missions
    CHECK(colonize_mission_done_by_partner(true, true, true, false, 3, 0, kColonizeTutorialId, false, true));
    CHECK(!colonize_mission_done_by_partner(true, true, true, false, 3, 0, 0x12345678u, false, true));
    // Guards: fix off, rivals mode, not active, or already at objective state 2
    CHECK(!colonize_mission_done_by_partner(false, true, true, false, 3, 0, kColonizeTutorialId, true, true));
    CHECK(!colonize_mission_done_by_partner(true, true, true, true, 3, 0, kColonizeTutorialId, true, true));
    CHECK(!colonize_mission_done_by_partner(true, true, true, false, 5, 0, kColonizeTutorialId, true, true));
    CHECK(!colonize_mission_done_by_partner(true, true, true, false, 3, 2, kColonizeTutorialId, true, true));
    CHECK(!colonize_mission_done_by_partner(true, false, true, false, 3, 0, kColonizeTutorialId, true, true));
    CHECK(!colonize_mission_done_by_partner(true, true, false, false, 3, 0, kColonizeTutorialId, true, true));
}

static void test_space_citizen_lead()
{
    printf("space citizen lead (BUG-1404)\n");
    using namespace wobj_space;
    // Tick cadence and velocity min-dt switch cleanly with space.citizen_lead
    CHECK(std::fabs(citizen_tick_every(true) - 0.25) < 1e-9);
    CHECK(std::fabs(citizen_tick_every(false) - 0.50) < 1e-9);
    CHECK(std::fabs(citizen_vel_min_dt(true) - 0.15) < 1e-9);
    CHECK(std::fabs(citizen_vel_min_dt(false) - 0.30) < 1e-9);

    // Steady-state lead with latency: adds one-way latency below the knee
    CHECK(std::fabs(citizen_extrap_time(0.10, 0.05, true) - 0.15) < 1e-9);
    // Negative / excessive one_way latency is clamped cleanly
    CHECK(std::fabs(citizen_extrap_time(-0.20, -0.10, true) - 0.0) < 1e-9);
    CHECK(citizen_extrap_time(0.10, 5.0, true) <= kLeadMax + 1e-9);

    // Past the knee (0.30 s): damped and capped at kLeadMax (0.50 s) instead of 1.0 s linear overshoot
    double damped = citizen_extrap_time(0.80, 0.04, true);
    double old_lin = citizen_extrap_time(0.80, 0.04, false);
    CHECK(damped > kLeadKnee && damped <= kLeadMax + 1e-9);
    CHECK(std::fabs(old_lin - 0.80) < 1e-9);
    CHECK(std::fabs(citizen_extrap_time(2.0, 0.0, false) - kOldMax) < 1e-9);
    CHECK(std::fabs(citizen_extrap_time(2.0, 0.0, true) - (kLeadKnee + kLeadTau)) < 1e-4);
    CHECK(std::fabs(citizen_extrap_time(2.0, 0.10, true) - kLeadMax) < 1e-9);

    // On a 5 m/s runner that stopped 0.8 s ago, OFF overshoots by 4.0 m while ON stays <= 2.5 m
    net::Vec3 hp{10.f, 20.f, 30.f}, vel{5.f, 0.f, 0.f};
    net::Vec3 t_on = citizen_target(hp, vel, 0.80, 0.04, true);
    net::Vec3 t_off = citizen_target(hp, vel, 0.80, 0.04, false);
    CHECK(t_on.x - hp.x <= 2.5f + 1e-4f);
    CHECK(t_off.x - hp.x >= 3.99f);
    CHECK(t_on.y == hp.y && t_on.z == hp.z);
}

static void test_tutorial_drive_sync()
{
    printf("space tutorial & drive sync (BUG-1559, BUG-1570)\n");
    using namespace spacelogic;

    // Pack / unpack round-trip of all 6 bits
    TutorialState full{true, true, true, true, true, true};
    uint8_t m_full = tutorial_pack(full);
    CHECK(m_full == kTutAllBits);
    TutorialState u = tutorial_unpack(m_full);
    CHECK(u.first_comm && u.flight_school && u.mission201 && u.has_interstellar && u.has_interplanetary && u.has_starter_kit);
    CHECK(tutorial_pack({}) == 0);

    // Synthetic mission_id sentinel never collides with real Spore mission IDs
    uint8_t out_mask = 0xFF;
    CHECK(!is_tutorial_sync_mission_id(0, out_mask));
    CHECK(!is_tutorial_sync_mission_id(kColonizeTutorialId, out_mask));
    CHECK(!is_tutorial_sync_mission_id(0x7BDBB97Du, out_mask)); // Story201
    uint32_t sid = tutorial_sync_mission_id(kTutFlightSchool | kTutMission201 | kTutInterstellarDrive | kTutStarterKit);
    CHECK(is_tutorial_sync_mission_id(sid, out_mask));
    CHECK(out_mask == (kTutFlightSchool | kTutMission201 | kTutInterstellarDrive | kTutStarterKit));

    // Guest missing unlocks + drives + starter tools receives all host unlocks monotonically
    TutorialState guest_empty{};
    auto a = tutorial_sync_decide(true, false, true, true, false, false, false, true, true, m_full, guest_empty);
    CHECK(a.set_first_comm && a.set_flight_school && a.set_mission201);
    CHECK(a.grant_interstellar && a.grant_interplanetary);
    CHECK(a.grant_scan && a.grant_laser && a.grant_abduct && a.grant_drop_cargo && a.grant_health && a.grant_energy);

    // BUG-1570: starter_fix OFF leaves starter tools ungranted while keeping BUG-1559 flags + drives
    auto a_no_starter =
        tutorial_sync_decide(true, false, true, true, false, false, false, true, true, m_full, guest_empty, false);
    CHECK(a_no_starter.any_flag() && a_no_starter.any_drive() && !a_no_starter.any_starter());

    // BUG-1570 backward compat: old host sending only kTutFlightSchool (no bit 5) still grants starter tools
    auto a_old_host =
        tutorial_sync_decide(true, false, true, true, false, false, false, true, true, kTutFlightSchool, guest_empty, true);
    CHECK(a_old_host.set_flight_school && a_old_host.grant_scan && a_old_host.grant_laser && a_old_host.grant_abduct &&
          a_old_host.grant_drop_cargo && a_old_host.grant_health && a_old_host.grant_energy);

    // Fix OFF -> no flags, no drives, no starters
    auto a_off = tutorial_sync_decide(false, false, true, true, false, false, false, true, true, m_full, guest_empty);
    CHECK(!a_off.any());

    // Host / outside Space / disconnected / elsewhere / space_other -> no-op
    CHECK(!tutorial_sync_decide(true, true, true, true, false, false, false, true, true, m_full, guest_empty).any());
    CHECK(!tutorial_sync_decide(true, false, false, true, false, false, false, true, true, m_full, guest_empty).any());
    CHECK(!tutorial_sync_decide(true, false, true, false, false, false, false, true, true, m_full, guest_empty).any());
    CHECK(!tutorial_sync_decide(true, false, true, true, true, false, false, true, true, m_full, guest_empty).any());
    CHECK(!tutorial_sync_decide(true, false, true, true, false, true, false, true, true, m_full, guest_empty).any());

    // Monotonicity: already-unlocked guest with all tools never downgrades or re-grants
    TutorialState guest_done{true, true, true, true, true, true, true, true, true, true, true, true};
    CHECK(!tutorial_sync_decide(true, false, true, true, false, false, false, true, true, m_full, guest_done).any());
    CHECK(!tutorial_sync_decide(true, false, true, true, false, false, false, true, true, 0, guest_done).any());
    // Guest already has flight_school, interplanetary, and starter tools, lacks m201 & interstellar -> grants only what is missing
    TutorialState guest_mid{false, true, false, false, true, true, true, true, true, true, true, true};
    auto a_mid = tutorial_sync_decide(true, false, true, true, false, false, false, true, true,
                                      kTutFlightSchool | kTutMission201 | kTutInterstellarDrive | kTutInterplanetaryDrive,
                                      guest_mid);
    CHECK(!a_mid.set_first_comm && !a_mid.set_flight_school && a_mid.set_mission201);
    CHECK(a_mid.grant_interstellar && !a_mid.grant_interplanetary && !a_mid.any_starter());

    // hold_grants (kept cargo hold restore window) delays drive & starter grants while still allowing tutorial flags
    auto a_hold = tutorial_sync_decide(true, false, true, true, false, false, true, true, true, m_full, guest_empty);
    CHECK(a_hold.any_flag() && !a_hold.any_drive() && !a_hold.any_starter());

    // Missing pointers only apply the available subsystem
    auto a_no_inv = tutorial_sync_decide(true, false, true, true, false, false, false, true, false, m_full, guest_empty);
    CHECK(a_no_inv.any_flag() && !a_no_inv.any_drive() && !a_no_inv.any_starter());
    auto a_no_mm = tutorial_sync_decide(true, false, true, true, false, false, false, false, true, m_full, guest_empty);
    CHECK(!a_no_mm.any_flag() && a_no_mm.any_drive() && a_no_mm.any_starter());

    // BUG-1570: kept-hold restore protection (`keep_starter_in_restore`)
    std::vector<uint32_t> empty_want;
    CHECK(keep_starter_in_restore(true, kToolScan, empty_want));
    CHECK(keep_starter_in_restore(true, kToolLaser1, empty_want));
    CHECK(keep_starter_in_restore(true, kToolAbduct, empty_want));
    CHECK(keep_starter_in_restore(true, kToolDropCargo, empty_want));
    CHECK(keep_starter_in_restore(true, kToolHealth1, empty_want));
    CHECK(keep_starter_in_restore(true, kToolEnergy1, empty_want));
    CHECK(keep_starter_in_restore(true, kToolInterplanetaryDrive, empty_want));
    CHECK(keep_starter_in_restore(true, kToolInterstellarDrive, empty_want));
    // Non-starter tools (e.g., Antimatter Bomb, Laser tier 2) are NOT protected
    CHECK(!keep_starter_in_restore(true, 0x144DF50Au, empty_want));
    CHECK(!keep_starter_in_restore(true, kToolLaser2, empty_want));
    CHECK(!keep_starter_in_restore(true, kToolHealth2, empty_want));
    // Starter fix OFF disables protection
    CHECK(!keep_starter_in_restore(false, kToolScan, empty_want));
    CHECK(!keep_starter_in_restore(false, kToolLaser1, empty_want));
    CHECK(!keep_starter_in_restore(false, kToolAbduct, empty_want));
    // Upgraded guest keeps its upgrade without forcing tier-1 base tools to stay
    std::vector<uint32_t> upgraded_want = {kToolLaser2, kToolHealth3, kToolEnergy2, kToolInterstellarUp4};
    CHECK(!keep_starter_in_restore(true, kToolLaser1, upgraded_want));
    CHECK(!keep_starter_in_restore(true, kToolHealth1, upgraded_want));
    CHECK(!keep_starter_in_restore(true, kToolEnergy1, upgraded_want));
    CHECK(!keep_starter_in_restore(true, kToolInterstellarDrive, upgraded_want));
    CHECK(keep_starter_in_restore(true, kToolScan, upgraded_want));
    CHECK(keep_starter_in_restore(true, kToolAbduct, upgraded_want));

    // Host send trigger rules
    CHECK(!tutorial_sync_should_send(false, true, true, true, m_full, 0, 1, 0, 2, 0));   // fix off
    CHECK(!tutorial_sync_should_send(true, false, true, true, m_full, 0, 1, 0, 2, 0));   // guest
    CHECK(!tutorial_sync_should_send(true, true, false, true, m_full, 0, 1, 0, 2, 0));   // not in space
    CHECK(!tutorial_sync_should_send(true, true, true, false, m_full, 0, 1, 0, 2, 0));   // disconnected
    CHECK(!tutorial_sync_should_send(true, true, true, true, 0, 0, 1, 0, 2, 0));         // zero mask
    CHECK(!tutorial_sync_should_send(true, true, true, true, m_full, 0, 1, 0, 0, 0));    // no peers in space
    CHECK(tutorial_sync_should_send(true, true, true, true, m_full, 0, 1, 0, 2, 0));     // initial send
    CHECK(!tutorial_sync_should_send(true, true, true, true, m_full, m_full, 1, 1, 2, 2)); // steady state
    CHECK(tutorial_sync_should_send(true, true, true, true, m_full, kTutFlightSchool, 1, 1, 2, 2)); // mask changed
    CHECK(tutorial_sync_should_send(true, true, true, true, m_full, m_full, 2, 1, 2, 2));           // epoch changed
    CHECK(tutorial_sync_should_send(true, true, true, true, m_full, m_full, 1, 1, 6, 2));           // new peer joined
    CHECK(!tutorial_sync_should_send(true, true, true, true, m_full, m_full, 1, 1, 2, 6));          // peer left
}

static void test_kept_reload_in_session()
{
    printf("space kept reload in-session (BUG-1578)\n");
    using namespace spacekept;
    // Modes that tear down Space vs modes that keep the Space world in memory
    CHECK(!mode_tears_down_space(0));            // unknown
    CHECK(!mode_tears_down_space(0x01654C05u));  // kSpace
    CHECK(!mode_tears_down_space(0x01654C06u));  // kEdit
    CHECK(!mode_tears_down_space(0x00DBDBA1u));  // kEditor
    CHECK(!mode_tears_down_space(0x01654C10u));  // kScenario (Galactic Adventure)
    CHECK(mode_tears_down_space(0x02CCD1D2u));   // kMenu (Galaxy menu)
    CHECK(mode_tears_down_space(0x01654C08u));   // kLoad (Loading screen)
    CHECK(mode_tears_down_space(0x01654C04u));   // kCiv (another stage)

    // Simulate guest keeping 2 colonies, then mid-session world reload (kMenu -> kLoad -> kSpace, same id=1)
    KeptStore<std::vector<uint32_t>> s;
    s.world(1);
    bool first = false;
    auto& e = s.bind("tok-guest", 1, "Akasaka", true, &first);
    CHECK(first && !e.have);
    CHECK(s.keep(e, {0x0052E018u, 0x0152E099u}));
    CHECK(e.value.size() == 2);

    // Entering an editor does NOT tear down Space or unbind the guest
    CHECK(!mode_tears_down_space(0x00DBDBA1u));
    auto& e_ed = s.bind("tok-guest", 1, "Akasaka", true, &first);
    CHECK(!first && e_ed.value.size() == 2);

    // Mid-session MSG_WORLD reload sends guest through kMenu -> host unbinds via leave(1)
    CHECK(mode_tears_down_space(0x02CCD1D2u));
    CHECK(s.leave(1) == 1);
    CHECK(s.bound(1) == 0);

    // Guest returns to Space with same id=1 and sends empty fresh roster: bind() reports first==true -> host restores!
    auto& e_re = s.bind("tok-guest", 1, "Akasaka", true, &first);
    CHECK(first && e_re.value.size() == 2 && e_re.value[0] == 0x0052E018u && e_re.value[1] == 0x0152E099u);
    s.begin(e_re, 100.0);
    // Empty roster arriving while restoring must NOT overwrite kept colonies
    CHECK(!s.keep(e_re, {}));
    CHECK(e_re.value.size() == 2);
    CHECK(s.done(1, 0, false) == &e_re);
    CHECK(!e_re.restoring && e_re.value.size() == 2);
}

static void test_contracts_rivals_world_and_puppet_pol()
{
    printf("space contracts rivals suppression, world reset, and rival puppet pol (BUG-1582)\n");
    using namespace spacelogic;

    // Co-op contracts auto-offer: allowed in Space co-op, blocked in Space Rivals and legacy Rivals
    CHECK(contract_auto_offer_allowed(true, true, true, 600.0, false, false));
    CHECK(!contract_auto_offer_allowed(true, true, true, 600.0, true, false));  // Space Rivals active
    CHECK(!contract_auto_offer_allowed(true, true, true, 600.0, false, true));  // legacy Rivals active
    CHECK(!contract_auto_offer_allowed(true, true, true, 600.0, true, true));   // both active
    CHECK(!contract_auto_offer_allowed(true, false, true, 600.0, false, false)); // not in space
    CHECK(!contract_auto_offer_allowed(true, true, false, 600.0, false, false)); // no partners
    CHECK(!contract_auto_offer_allowed(true, true, true, 0.0, false, false));    // period disabled
    CHECK(contract_auto_offer_allowed(false, true, true, 600.0, true, true));    // fix off -> legacy behavior

    // World change resets stale contracts across saves
    CHECK(contract_should_reset_on_world_change(true, 1u, 2u, true));
    CHECK(!contract_should_reset_on_world_change(true, 2u, 2u, true));   // same world
    CHECK(!contract_should_reset_on_world_change(true, 1u, 2u, false));  // no active/pending contract
    CHECK(!contract_should_reset_on_world_change(true, 0u, 2u, true));   // unknown contract epoch
    CHECK(!contract_should_reset_on_world_change(false, 1u, 2u, true));  // fix off

    // Rival puppet political ID: prefers rival's mirror empire pol over pirate sentinel 0xFFFFFFFF
    constexpr uint32_t kMine = 0x80000001u;
    constexpr uint32_t kMirror = 0x80000042u;
    CHECK(rival_puppet_pol(true, true, true, kMine, kMirror) == kMirror);
    CHECK(rival_puppet_pol(true, true, true, kMine, 0u) == 0xFFFFFFFFu);          // no mirror yet -> fallback
    CHECK(rival_puppet_pol(true, true, true, kMine, 0xFFFFFFFFu) == 0xFFFFFFFFu); // sentinel -> fallback
    CHECK(rival_puppet_pol(true, true, true, kMine, kMine) == 0xFFFFFFFFu);       // never use own pol at war
    CHECK(rival_puppet_pol(true, true, false, kMine, kMirror) == kMine);          // neutral/ally -> friendly
    CHECK(rival_puppet_pol(true, false, true, kMine, kMirror) == kMine);          // not rivals -> friendly
    CHECK(rival_puppet_pol(false, true, true, kMine, kMirror) == 0xFFFFFFFFu);    // fix off -> legacy pirate pol
}

int main()
{
    test_jump_target();
    test_jump_rivals();
    test_aborted();
    test_wave_return();
    test_owed_action();
    test_kept_store();
    test_kept_map();
    test_kept_rivals();
    test_restore_window();
    test_rejoin_kept();
    test_rejoin_slot_pair();
    test_fleet_leads();
    test_audit_0717();
    test_colonize_mission();
    test_space_citizen_lead();
    test_tutorial_drive_sync();
    test_kept_reload_in_session();
    test_contracts_rivals_world_and_puppet_pol();
    printf(g_fail ? "space_tests: %d FAILED\n" : "space_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
