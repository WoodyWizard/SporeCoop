// Native tests for the portable Space co-op decisions (space_logic.h): the co-op jump's target
// (BUG-0072) and the aborted-raid set (BUG-0136) (make test; built with AddressSanitizer).
#include <cstdio>
#include <map>
#include <set>

#include "space_logic.h"

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

int main()
{
    test_jump_target();
    test_aborted();
    test_wave_return();
    test_owed_action();
    printf(g_fail ? "space_tests: %d FAILED\n" : "space_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
