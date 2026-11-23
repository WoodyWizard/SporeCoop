// Native tests for the portable Civ co-op rules (civ_logic.h, D3 of docs/plan-0.7.15.md): the shared money (BUG-0613,
// BUG-0807, the guest's income), the owners of the host's vehicles (BUG-0031) and the holds (BUG-0493, BUG-0951,
// BUG-0151). make test; built with AddressSanitizer / UBSan.
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "civ_logic.h"

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

using namespace civlogic;

static void test_money()
{
    printf("money\n");
    // the guest's own income on the player civilization is dropped; a host message the mod applies, a loss, another
    // civilization, the host and another mode are not
    CHECK(guest_income_blocked(false, true, true, true, 80.f));
    CHECK(!guest_income_blocked(true, true, true, true, 80.f));
    CHECK(!guest_income_blocked(false, true, true, true, -5.f));
    CHECK(!guest_income_blocked(false, true, true, true, 0.f));
    CHECK(!guest_income_blocked(false, true, true, false, 80.f));
    CHECK(!guest_income_blocked(false, false, true, true, 80.f));
    CHECK(!guest_income_blocked(false, true, false, true, 80.f));
    // BUG-0613: never below 0 (the old path paid whatever the money was)
    CHECK(can_pay(100.f, 100.f));
    CHECK(can_pay(150.f, 100.f));
    CHECK(!can_pay(99.f, 100.f));
    CHECK(!can_pay(0.f, 0.5f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(can_pay(nan, 100.f)); // as the game's compare: a NaN never refuses
    // BUG-0807: the refund is what was paid, within 0..max; a value that is no number gives nothing
    CHECK(refund_amount(250.f, 100000.f) == 250.f);
    CHECK(refund_amount(-40.f, 100000.f) == 0.f);
    CHECK(refund_amount(5e9f, 100000.f) == 100000.f);
    CHECK(refund_amount(nan, 100000.f) == 0.f);
    CHECK(refund_amount(std::numeric_limits<float>::infinity(), 100000.f) == 0.f);
}

static void test_owners()
{
    printf("owners\n");
    // a new vehicle: an NPC nation's is nobody's, whatever the rest says
    CHECK(new_vehicle_owner(false, 2, 1) == kNpc);
    // the player nation's: the buyer first, else the owner of the nearest player city
    CHECK(new_vehicle_owner(true, 2, 1) == 2);
    CHECK(new_vehicle_owner(true, -1, 1) == 1);
    // the shared capital built it, or a city without owner: the host's (never kShared, never kNoOwner)
    CHECK(new_vehicle_owner(true, -1, kShared) == 0);
    CHECK(new_vehicle_owner(true, -1, kNoOwner) == 0);
    // no player city at all: it stays an NPC's (the host's table as in 0.7.14)
    CHECK(new_vehicle_owner(true, -1, kNpc) == kNpc);

    // BUG-0031: the vehicles of a player who left
    std::vector<uint8_t> here{0, 2, 3};
    uint8_t out = 0xEE;
    CHECK(!orphan_owner(2, here, 3, out) && out == 0xEE); // a present owner keeps it
    CHECK(!orphan_owner(kNpc, here, 3, out));             // an NPC's stays an NPC's
    CHECK(!orphan_owner(kNoOwner, here, 3, out));
    CHECK(orphan_owner(1, here, 3, out) && out == 3);     // to the nearest city's owner, who is here
    CHECK(orphan_owner(1, here, 1, out) && out == 0);     // never to a player who is not here (the old bug: kept)
    CHECK(orphan_owner(1, here, 4, out) && out == 0);
    CHECK(orphan_owner(1, here, kShared, out) && out == 0);
    CHECK(orphan_owner(1, here, kNoOwner, out) && out == 0);
    CHECK(orphan_owner(5, {}, 0, out) && out == 0);       // nobody listed: the host

    CHECK(owns(2, 2) && !owns(2, 3) && !owns(kNpc, 0));
}

static void test_holds()
{
    printf("holds\n");
    CHECK(hold_removal(true, false) && hold_removal(false, true) && !hold_removal(false, false));
    // BUG-0951: a dead mirror, or another vehicle at its address (not alive by the serial), is never removed by us
    CHECK(unseen_mirror(true, false, false) == Unseen::Remove && kill_unseen(true, false));
    CHECK(unseen_mirror(true, false, true) == Unseen::Defer);
    CHECK(unseen_mirror(false, false, true) == Unseen::Remove && !kill_unseen(false, false));
    CHECK(unseen_mirror(true, true, true) == Unseen::Remove && !kill_unseen(true, true));
    // the waiting removals: the game removed it first (not listed, dead): dropped without a call
    CHECK(pump_waits(true, false, true) && !pump_kills(false, false) && !pump_kills(true, true));
    CHECK(!pump_waits(false, false, true) && !pump_waits(true, true, true));
    CHECK(!pump_waits(true, false, false) && pump_kills(true, false));
    // BUG-9301 (CIV-1): a mirror taken out for a save goes back only while its civilization and the same vehicle live
    CHECK(took_goes_back(true, true, true));
    CHECK(!took_goes_back(true, false, false) && !took_goes_back(true, true, false) && !took_goes_back(false, true, true));
    // BUG-0151
    CHECK(local_play(true, true, true));
    CHECK(!local_play(false, true, true) && !local_play(true, false, true) && !local_play(true, true, false));
}

static void test_citizens()
{
    // WP-C1 (BUG-1273): a guest's city takes the guest's line; the host's, the shared capital, NPC and nobody's do not
    CHECK(citizen_line_owner(1, true, 4) == 1 && citizen_line_owner(3, true, 4) == 3);
    CHECK(citizen_line_owner(0, true, 4) == -1);
    CHECK(citizen_line_owner(0xFD, true, 4) == -1 && citizen_line_owner(kNpc, true, 4) == -1 &&
          citizen_line_owner(kNoOwner, true, 4) == -1);
    CHECK(citizen_line_owner(4, true, 4) == -1);
    CHECK(citizen_line_owner(1, false, 4) == -1); // creature.mode host: every city the civilization's
}

int main()
{
    test_money();
    test_owners();
    test_holds();
    test_citizens();
    printf(g_fail ? "civ_tests: %d FAILED\n" : "civ_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
