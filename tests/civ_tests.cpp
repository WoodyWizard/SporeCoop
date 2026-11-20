// Native tests for the portable Civ co-op rules (civ_logic.h, D3 of docs/plan-0.7.15.md): the shared money (BUG-0613,
// BUG-0807, the guest's income), the owners of the host's vehicles (BUG-0031) and the holds (BUG-0493, BUG-0951,
// BUG-0151). make test; built with AddressSanitizer / UBSan.
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <vector>

#include "civ_logic.h"
#include "civ_wallet_logic.h"

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
    // BUG-0151
    CHECK(local_play(true, true, true));
    CHECK(!local_play(false, true, true) && !local_play(true, false, true) && !local_play(true, true, false));
}

// BUG-9259: every player's own Civ money (civ_wallet_logic.h)
static void test_wallets()
{
    printf("wallets\n");
    using namespace civwallet;
    const uint8_t host = 0;
    // income by cities: host 1 city, guest 1: 2 cities, guest 2: 1 city -> 100 per city
    std::multiset<uint8_t> owners{0, 1, 1, 2};
    auto parts = split_income(400, owners, host, {1, 2});
    CHECK(parts.size() == 2 && std::fabs(parts[1] - 200) < 1e-9 && std::fabs(parts[2] - 100) < 1e-9);
    // a guest without a wallet (away, not in Civ yet): its cities' part stays with the host
    parts = split_income(400, owners, host, {1});
    CHECK(parts.size() == 1 && std::fabs(parts[1] - 200) < 1e-9);
    // the shared capital: equal parts for the host and every guest with a wallet
    parts = split_income(300, {kSharedCity}, host, {1, 2});
    CHECK(parts.size() == 2 && std::fabs(parts[1] - 100) < 1e-9 && std::fabs(parts[2] - 100) < 1e-9);
    // nothing to split: no gain, a loss, no cities, not a number
    CHECK(split_income(0, owners, host, {1}).empty());
    CHECK(split_income(-50, owners, host, {1}).empty());
    CHECK(split_income(100, {}, host, {1}).empty());
    CHECK(split_income(std::numeric_limits<double>::quiet_NaN(), owners, host, {1}).empty());
    // the parts never exceed the gain
    parts = split_income(1000, {0, 1, 2, kSharedCity}, host, {1, 2});
    double sum = 0;
    for (auto& [p, v] : parts)
        sum += v;
    CHECK(sum <= 1000 + 1e-9 && sum > 0);
    // a newcomer: an equal part of the host's money, whole dollars, nothing for less than two holders
    CHECK(newcomer_share(1000, 2) == 500 && newcomer_share(1001, 2) == 500 && newcomer_share(900, 3) == 300);
    CHECK(newcomer_share(0, 2) == 0 && newcomer_share(-5, 2) == 0 && newcomer_share(1000, 1) == 0);
    CHECK(newcomer_share(std::numeric_limits<double>::infinity(), 2) == 0);
    // the ledger: the host's operations count until the guest confirms them, then its report is the value
    Ledger l;
    l.set(1, 500);
    CHECK(l.value() == 500);
    l.op(2, +100); // an income share
    l.op(3, -250); // a vehicle the host bought for it
    CHECK(l.value() == 350);
    CHECK(l.report(2, 600));       // the guest applied op 2 (500 + 100), op 3 still on the way
    CHECK(l.value() == 350 && l.sent.size() == 1);
    CHECK(l.report(3, 330));       // it applied op 3 and spent 20 in its own planner meanwhile
    CHECK(l.value() == 330 && l.sent.empty());
    CHECK(!l.report(2, 9999));     // an old report (crossed a newer one) is ignored
    CHECK(l.value() == 330);
    CHECK(!l.report(4, std::numeric_limits<double>::quiet_NaN()) && !l.report(4, 1e12));
    l.op(4, -1000);                // never below 0
    CHECK(l.value() == 0);
    CHECK(clamp_money(2e8) == kMoneyMax && clamp_money(-1) == 0);
}

int main()
{
    test_money();
    test_owners();
    test_holds();
    test_wallets();
    printf(g_fail ? "civ_tests: %d FAILED\n" : "civ_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
