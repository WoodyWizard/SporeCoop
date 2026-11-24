// Native unit tests for co-op Galactic Adventures (WP-A of 0.7.99, BUG-1474 / BUG-0793):
//   - marker + class signature determinism
//   - E-A HostJournal / GuestJournal ordering, deduplication, late join replay, Replay reset, outcome hold
//   - E-B ActorPairing (marker, class) within 12 m, 5 s unpaired expiry, stream timeout AI handback
//   - per-captain puppet scale (1.0x vs legacy 1.15x fallback)
//   - wire/adventure.h roundtrips, strict prefix truncation rejection, invalid value rejection
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "adventure_logic.h"
#include "wire/adventure.h"

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

template <class T>
static std::vector<uint8_t> encode_msg(const T& m)
{
    net::Writer w;
    m.write(w);
    return w.data;
}

template <class T>
static void check_roundtrip_and_prefixes(const T& m)
{
    auto bytes = encode_msg(m);
    CHECK(!bytes.empty());
    net::Reader r(bytes.data(), bytes.size());
    T out;
    CHECK(out.read(r));
    CHECK(r.left() == 0);
    CHECK(encode_msg(out) == bytes);
    // Every strict prefix must be rejected
    for (size_t k = 0; k < bytes.size(); k++) {
        net::Reader rp(bytes.data(), k);
        T tmp;
        CHECK(!tmp.read(rp));
    }
}

static void test_marker_sig()
{
    printf("adventure: marker + class signature determinism\n");
    uint8_t m1[0x24] = {};
    uint8_t m2[0x24] = {};
    m1[4] = 0x11;
    m2[4] = 0x11;
    uint32_t s1 = adventure::marker_sig(m1, 3);
    uint32_t s2 = adventure::marker_sig(m2, 3);
    CHECK(s1 != 0 && s1 == s2);
    // Different class or marker bytes yield different signatures
    CHECK(adventure::marker_sig(m1, 4) != s1);
    m2[4] = 0x22;
    CHECK(adventure::marker_sig(m2, 3) != s1);
    // Null marker (wave spawner creature) still produces deterministic non-zero hash per class
    uint32_t n0 = adventure::marker_sig(nullptr, 2);
    uint32_t n1 = adventure::marker_sig(nullptr, 2);
    uint32_t n2 = adventure::marker_sig(nullptr, 5);
    CHECK(n0 != 0 && n0 == n1 && n0 != n2);
}

static void test_journal_and_late_join()
{
    printf("adventure: E-A host journal, ordered guest replay, late join, replay reset, outcome hold\n");
    using namespace adventure;
    wire::Key3 mission{{0xBF0CE0D0u, 0x366A930Du, 0x408A0000u}};

    HostJournal hj;
    hj.start_run(mission, 0x101u, 0);
    CHECK(hj.active() && hj.run_id == 0x101u && hj.next_seq == 0 && hj.current_act == 0);

    // Goal credit in act 0, slot 0 (1/2, then duplicate ignored, then 2/2)
    wire::adventure::GoalData g0{0, 0, 2, 7, 0, 2, 0, 1};
    const JournalEntry* e1 = hj.commit_goal(g0);
    CHECK(e1 && e1->seq == 1 && e1->goal.count == 1);
    CHECK(hj.commit_goal(g0) == nullptr); // duplicate count 1 ignored
    g0.count = 2;
    const JournalEntry* e2 = hj.commit_goal(g0);
    CHECK(e2 && e2->seq == 2 && e2->goal.count == 2);

    // Act transition 0 -> 1
    const JournalEntry* e3 = nullptr;
    CHECK(hj.commit_act(1, &e3) == HostJournal::ActResult::Committed && e3 && e3->seq == 3 && e3->act == 1);
    CHECK(hj.commit_act(1, &e3) == HostJournal::ActResult::Ignored);

    // Goal credit in act 1 + building death
    wire::adventure::GoalData g1{1, 1, 3, 12, 0, 1, 0, 1};
    const JournalEntry* e4 = hj.commit_goal(g1);
    CHECK(e4 && e4->seq == 4 && e4->goal.act == 1 && e4->goal.slot == 1);
    const JournalEntry* e5 = hj.commit_death(0xAABBCCDDu, 1);
    CHECK(e5 && e5->seq == 5 && e5->deaths == 1);
    CHECK(hj.commit_death(0xAABBCCDDu, 1) == nullptr); // monotonic dedupe

    // Outcome sealing: first 5/6 wins, invalid states rejected, further goals blocked
    CHECK(hj.commit_outcome(4) == nullptr);
    const JournalEntry* e6 = hj.commit_outcome(5);
    CHECK(e6 && e6->seq == 6 && e6->outcome_state == 5);
    CHECK(hj.commit_outcome(6) == nullptr); // already sealed
    wire::adventure::GoalData g_after{1, 2, 1, 0, 0, 1, 0, 1};
    CHECK(hj.commit_goal(g_after) == nullptr); // sealed

    // Backwards act triggers Replay reset signal
    CHECK(hj.commit_act(0) == HostJournal::ActResult::ResetNeeded);

    // Late join: guest joins after seq 6 with last_seq = 0
    auto all = hj.entries_after(0);
    CHECK(all.size() == 6);
    auto tail = hj.entries_after(3);
    CHECK(tail.size() == 3 && tail[0].seq == 4 && tail[2].seq == 6);

    // Guest out-of-order enqueue + ordered drain with act jump
    GuestJournal gj;
    CHECK(gj.on_welcome(mission, 0x101u, 1, 3, 6));
    CHECK(!gj.on_welcome(mission, 0x101u, 1, 3, 6)); // same run: does not reset

    // Enqueue out of order: 6, 4, 3, 5, 2, 1
    CHECK(gj.enqueue(all[5]));
    CHECK(gj.enqueue(all[3]));
    CHECK(gj.enqueue(all[2]));
    CHECK(gj.enqueue(all[4]));
    CHECK(gj.enqueue(all[1]));
    // Before seq 1 arrives, nothing fires (strict sequence order)
    CHECK(gj.next_action(0, 3).kind == GuestJournal::ActionKind::None);

    CHECK(gj.enqueue(all[0]));
    // While guest is still on intro card (state 1), nothing applies
    CHECK(gj.next_action(0, 1).kind == GuestJournal::ActionKind::None);

    // Now playing in act 0: seq 1, seq 2 apply
    auto a1 = gj.next_action(0, 3);
    CHECK(a1.kind == GuestJournal::ActionKind::ApplyGoal && a1.seq == 1 && a1.entry.goal.count == 1);
    auto a2 = gj.next_action(0, 3);
    CHECK(a2.kind == GuestJournal::ActionKind::ApplyGoal && a2.seq == 2 && a2.entry.goal.count == 2);

    // Seq 3 is Act 1 while guest is still in Act 0: requests JumpToAct(1) and holds seq 3 until guest is in Act 1
    auto a3 = gj.next_action(0, 3);
    CHECK(a3.kind == GuestJournal::ActionKind::JumpToAct && a3.target_act == 1);
    // Still in act 0 -> still requests JumpToAct(1), does NOT apply outcome early!
    auto a3_again = gj.next_action(0, 3);
    CHECK(a3_again.kind == GuestJournal::ActionKind::JumpToAct && a3_again.target_act == 1);

    // Guest completes JumpToAct(1): seq 3 consumed, seq 4 (Goal act 1) applies next!
    auto a4 = gj.next_action(1, 3);
    CHECK(a4.kind == GuestJournal::ActionKind::ApplyGoal && a4.seq == 4 && a4.entry.goal.slot == 1);
    auto a5 = gj.next_action(1, 3);
    CHECK(a5.kind == GuestJournal::ActionKind::ApplyDeath && a5.seq == 5 && a5.entry.object_id == 0xAABBCCDDu);
    auto a6 = gj.next_action(1, 3);
    CHECK(a6.kind == GuestJournal::ActionKind::ApplyOutcome && a6.seq == 6 && a6.entry.outcome_state == 5);
    CHECK(gj.applied_seq == 6 && gj.outcome_applied == 5);
    CHECK(gj.next_action(1, 3).kind == GuestJournal::ActionKind::None);

    // Guest hold and native defeat pass-through rules
    CHECK(should_hold_guest_state(6, true, false));
    CHECK(!should_hold_guest_state(6, true, true));
    CHECK(!should_hold_guest_state(6, false, false));
    CHECK(!should_hold_guest_state(5, true, false));
    CHECK(should_pass_credit_natively(kGoalKindDefeat, true));
    CHECK(!should_pass_credit_natively(kGoalKindDefeat, false));
    CHECK(!should_pass_credit_natively(2, true));
}

static void test_actor_pairing_and_expiry()
{
    printf("adventure: E-B (marker, class) pairing within 12 m and 5 s unpaired expiry\n");
    using namespace adventure;
    ActorPairing ap;

    std::vector<LocalCandidate> cands = {
        {101, 0x1111u, 2, {10.f, 0.f, 0.f}, false}, // 10 m from origin
        {102, 0x1111u, 2, {3.f, 0.f, 0.f}, false},  // 3 m from origin (nearest!)
        {103, 0x1111u, 2, {12.5f, 0.f, 0.f}, false}, // 12.5 m (> 12 m limit)
        {104, 0x2222u, 2, {1.f, 0.f, 0.f}, false},  // wrong marker_hash
        {105, 0x1111u, 5, {1.f, 0.f, 0.f}, false},  // wrong class_id
        {106, 0x1111u, 2, {0.5f, 0.f, 0.f}, true},  // dead: skipped
    };

    float origin[3] = {0.f, 0.f, 0.f};
    // First host actor pairs with nearest eligible (102 at 3 m)
    CHECK(ap.pair(5001, 0x1111u, 2, origin, cands) == 102);
    CHECK(ap.local_for_host(5001) == 102 && ap.host_for_local(102) == 5001 && ap.is_local_paired(102));

    // Second host actor with same (marker, class) pairs with next nearest unpaired within 12 m (101 at 10 m)
    CHECK(ap.pair(5002, 0x1111u, 2, origin, cands) == 101);
    // Third host actor at origin cannot pair 103 (12.5 m > 12 m) or 104/105/106
    CHECK(ap.pair(5003, 0x1111u, 2, origin, cands) == 0);
    // If host actor 5003 is at x=12.0, 103 is only 0.5 m away -> pairs!
    float near103[3] = {12.f, 0.f, 0.f};
    CHECK(ap.pair(5003, 0x1111u, 2, near103, cands) == 103);

    // Unpaired 5 s expiry: 104 and 105 are unpaired; stream must warm up for 3 s first
    ap.note_stream_rx(10.0);
    CHECK(ap.stream_active(10.5) && !ap.stream_warmed(12.5));
    CHECK(ap.collect_expired_unpaired(12.5, cands).empty());

    // Stream warmed at t = 13.0 -> unpaired timers start at 13.0
    ap.note_stream_rx(13.0);
    CHECK(ap.stream_warmed(13.0));
    CHECK(ap.collect_expired_unpaired(13.0, cands).empty());
    ap.note_stream_rx(17.9);
    CHECK(ap.collect_expired_unpaired(17.9, cands).empty()); // 4.9 s < 5.0 s

    // At t = 18.0 (5.0 s unpaired): 104 and 105 expire, paired (101, 102, 103) and dead (106) do NOT!
    ap.note_stream_rx(18.0);
    auto exp = ap.collect_expired_unpaired(18.0, cands);
    CHECK(exp.size() == 2);
    bool saw104 = false, saw105 = false;
    for (uint32_t id : exp) {
        if (id == 104) saw104 = true;
        if (id == 105) saw105 = true;
    }
    CHECK(saw104 && saw105);

    // Stream timeout (> 5 s silent): stream_active becomes false (AI handed back) and unpaired timers clear
    CHECK(!ap.stream_active(23.1));
    CHECK(ap.collect_expired_unpaired(23.1, cands).empty());
    CHECK(ap.unpaired_since.empty());

    // Prune dead locals cleans up stale pairings
    ap.prune_dead_locals({101});
    CHECK(ap.local_for_host(5001) == 0 && ap.local_for_host(5002) == 101 && ap.local_for_host(5003) == 0);
}

static void test_captain_scale_and_compat()
{
    printf("adventure: per-captain puppet scale and creature 1.4 compat gate\n");
    using namespace adventure;
    // Exact 1.0x scale when fix is on, 1.15x when off
    CHECK(std::fabs(captain_puppet_scale(2.5f, 3.0f, true) - 2.5f) < 1e-5f);
    CHECK(std::fabs(captain_puppet_scale(2.5f, 3.0f, false) - 2.5f * 1.15f) < 1e-5f);
    // Fallback to own scale when remote body is missing / invalid / NaN
    CHECK(std::fabs(captain_puppet_scale(0.f, 3.0f, true) - 3.0f) < 1e-5f);
    CHECK(std::fabs(captain_puppet_scale(std::numeric_limits<float>::quiet_NaN(), 2.2f, true) - 2.2f) < 1e-5f);
    CHECK(std::fabs(captain_puppet_scale(99.f, 1.8f, true) - 1.8f) < 1e-5f);
    CHECK(std::fabs(captain_puppet_scale(0.f, 0.f, true) - 1.0f) < 1e-5f);

    // Creature 1.4 compatibility gate
    std::vector<net::FamilyRev> old_peer = {{(uint8_t)net::Family::creature, 1, 3}};
    std::vector<net::FamilyRev> new_peer = {{(uint8_t)net::Family::creature, 1, 4}};
    CHECK(!peer_supports(old_peer, true));
    CHECK(!peer_supports(new_peer, false));
    CHECK(peer_supports(new_peer, true));
}

static void test_wire_codecs()
{
    printf("adventure: wire/adventure.h roundtrips, truncation, and validation\n");
    namespace wa = wire::adventure;
    wa::RunHeader hdr{{{0x11223344u, 0x366A930Du, 0x408A0000u}}, 0xCAFEBABEu};

    check_roundtrip_and_prefixes(wa::Hello{hdr, 0x55AAu, 12});
    check_roundtrip_and_prefixes(wa::Welcome{hdr, 0x55AAu, 2, 3, 14});
    check_roundtrip_and_prefixes(wa::Propose{hdr, {1, 0, 2, 5, 0, 3, 0, 1}});
    check_roundtrip_and_prefixes(wa::Commit{hdr, 7, {1, 0, 2, 5, 0, 3, 0, 2}});
    check_roundtrip_and_prefixes(wa::ActChange{hdr, 8, 2});
    check_roundtrip_and_prefixes(wa::Outcome{hdr, 9, 5});
    check_roundtrip_and_prefixes(wa::Outcome{hdr, 10, 6});
    check_roundtrip_and_prefixes(wa::Death{hdr, 11, 0xDEADBEEFu, 3});
    check_roundtrip_and_prefixes(wa::Hit{hdr, 0x12345678u, 4, 9001u, 17.5f, 2});

    wa::ActorBatch batch;
    batch.hdr = hdr;
    batch.time_ms = 42000;
    for (int i = 0; i < 3; i++) {
        wa::ActorSample s;
        s.host_key = 1000u + (uint32_t)i;
        s.marker_hash = 0xAA00u + (uint32_t)i;
        s.class_id = i;
        s.pos[0] = (float)i * 2.5f; s.pos[1] = -3.f; s.pos[2] = 500.f;
        s.rot[0] = 0.f; s.rot[1] = 0.f; s.rot[2] = 0.f; s.rot[3] = 1.f;
        s.vel[0] = 1.f; s.vel[1] = 0.f; s.vel[2] = 0.f;
        s.hp = 45.f;
        s.max_hp = 50.f;
        s.flags = (i == 2) ? wa::kActorFlagDead : 0;
        batch.actors.push_back(s);
    }
    check_roundtrip_and_prefixes(batch);

    // Invalid payload rejection checks
    {
        auto b = encode_msg(wa::Outcome{hdr, 1, 4}); // invalid outcome state 4
        net::Reader r(b.data(), b.size());
        wa::Outcome o;
        CHECK(!o.read(r));
    }
    {
        auto b = encode_msg(wa::Hit{hdr, 1, 0, 99, std::numeric_limits<float>::quiet_NaN(), 0});
        net::Reader r(b.data(), b.size());
        wa::Hit h;
        CHECK(!h.read(r));
    }
    {
        auto b = encode_msg(wa::Hit{hdr, 1, 0, 99, -5.f, 0});
        net::Reader r(b.data(), b.size());
        wa::Hit h;
        CHECK(!h.read(r));
    }
    {
        auto b = encode_msg(wa::Death{hdr, 1, 0x1234u, 0}); // deaths == 0 invalid
        net::Reader r(b.data(), b.size());
        wa::Death d;
        CHECK(!d.read(r));
    }
    {
        // ActorBatch with n = 21 (> kMaxActorsPerBatch) rejected
        net::Writer w;
        hdr.write(w);
        w.u32(100);
        w.u8(21);
        net::Reader r(w.data.data(), w.data.size());
        wa::ActorBatch ab;
        CHECK(!ab.read(r));
    }
}

int main()
{
    test_marker_sig();
    test_journal_and_late_join();
    test_actor_pairing_and_expiry();
    test_captain_scale_and_compat();
    test_wire_codecs();
    printf(g_fail ? "adventure_tests: FAILED (%d)\n" : "adventure_tests: ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
