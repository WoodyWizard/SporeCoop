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

int main()
{
    test_jump_target();
    test_aborted();
    test_wave_return();
    printf(g_fail ? "space_tests: %d FAILED\n" : "space_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
