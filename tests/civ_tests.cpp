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
#include "net/interp.h"
#include "net/shared_ledger.h"
#include "ship_editor_logic.h"

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
    // BUG-1416 (CIV-1): a mirror taken out for a save goes back only while its civilization and the same vehicle live
    CHECK(took_goes_back(true, true, true));
    CHECK(!took_goes_back(true, false, false) && !took_goes_back(true, true, false) && !took_goes_back(false, true, true));
    // BUG-1417 (CIV-7): the planner building is sent only when the closed planner kept it
    CHECK(planner_kept(true, true, true));
    CHECK(!planner_kept(true, false, true) && !planner_kept(true, true, false) && !planner_kept(false, true, true));
    // BUG-1532: the sell-back refund of a building the guest's planner added and has not sent passes the income block
    CHECK(planner_refund_passes(true, true, true));
    CHECK(!planner_refund_passes(false, true, true) && !planner_refund_passes(true, false, true) &&
          !planner_refund_passes(true, true, false));
    // a guest's sold building of the city (the host has it) and its other income stay blocked
    CHECK(guest_income_blocked(false, true, true, true, 1600.f) && !planner_refund_passes(true, true, false));
    // BUG-1418 (CIV-6): a host alone claims nothing by its deal; what a city has stays; with players the deal claims
    CHECK(city_claim(false, "", false, false, "host", true).empty());
    CHECK(city_claim(true, "host", false, false, "host", true) == "host");
    CHECK(city_claim(true, "k-guest", true, false, "host", true) == "k-guest");
    CHECK(city_claim(true, "k-guest", true, false, "host", false) == "k-guest");
    CHECK(city_claim(false, "", false, false, "host", false) == "host");
    CHECK(city_claim(false, "", false, true, "", false) == "shared");
    CHECK(city_claim(true, "shared", true, false, "host", false) == "host");
    CHECK(city_claim(false, "", false, false, "", false).empty());
    CHECK(adopt_takes_city(true, false) && !adopt_takes_city(true, true) && !adopt_takes_city(false, false));
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

static void test_audit_v1()
{
    printf("audit WP-V1\n");
    // BUG-1479 (CIV-10): the first listener takes the answer, every later one drops the same data (open or closed)
    CHECK(spec_answer(true, true, false) == SpecAnswer::Take);
    CHECK(spec_answer(true, true, true) == SpecAnswer::Drop);
    CHECK(spec_answer(true, false, true) == SpecAnswer::Drop);
    CHECK(spec_answer(true, false, false) == SpecAnswer::Pass); // the game's own box (no question of the host)
    // fix off: 0.7.16.1, the closed question let the repeat through to the game's listener
    CHECK(spec_answer(false, true, false) == SpecAnswer::Take && spec_answer(false, true, true) == SpecAnswer::Drop);
    CHECK(spec_answer(false, false, true) == SpecAnswer::Pass && spec_answer(false, false, false) == SpecAnswer::Pass);
    // BUG-1480 (CIV-11): the space build goes the Space way in an editor or a load too
    CHECK(space_build_routed(true, true, kSpaceBuildBytes) && space_build_routed(true, false, kSpaceBuildBytes));
    CHECK(space_build_routed(false, true, kSpaceBuildBytes) && !space_build_routed(false, false, kSpaceBuildBytes));
    // a Civ guest's superweapon request (the same first byte) stays in the Civ hold outside Space
    CHECK(!space_build_routed(true, false, kSuperRequestBytes) && space_build_routed(true, true, kSuperRequestBytes));
    CHECK(kSpaceBuildBytes == 33 && kSuperRequestBytes == 17);
    // BUG-1481 (CIV-15 a): only a candidate waiting for its design is spared, and only with the fix
    CHECK(sweep_spares(true, true) && !sweep_spares(true, false));
    CHECK(!sweep_spares(false, true) && !sweep_spares(false, false));
}

// BUG-1468: every player's own Civ money (civ_wallet_logic.h; the rules of PR #205 by IltonBJSilva)
static void test_wallets()
{
    printf("wallets\n");
    using namespace civwallet;
    const uint8_t host = 0;
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
    static_assert(kSharedCity == civlogic::kShared, "the shared city id is civ_internal.h's");
    // income by cities: host 1 city, guest 1: 2 cities, guest 2: 1 city -> 100 per city
    std::multiset<uint8_t> owners{0, 1, 1, 2};
    auto parts = split_income(400, owners, host, {1, 2});
    CHECK(parts.size() == 2 && near(parts[1], 200) && near(parts[2], 100));
    // a guest without a wallet (an older peer, away, not in Civ yet): its cities' part stays with the host
    parts = split_income(400, owners, host, {1});
    CHECK(parts.size() == 1 && near(parts[1], 200));
    // the shared capital: equal parts for the host and every guest with a wallet
    parts = split_income(300, {kSharedCity}, host, {1, 2});
    CHECK(parts.size() == 2 && near(parts[1], 100) && near(parts[2], 100));
    // the host's id in the wallet set (a stale entry) changes nothing
    parts = split_income(300, {kSharedCity}, host, {0, 1, 2});
    CHECK(parts.size() == 2 && near(parts[1], 100) && near(parts[2], 100));
    // nothing to split: no gain, a loss, no cities, not a number, infinity
    CHECK(split_income(0, owners, host, {1}).empty());
    CHECK(split_income(-50, owners, host, {1}).empty());
    CHECK(split_income(100, {}, host, {1}).empty());
    CHECK(split_income(std::numeric_limits<double>::quiet_NaN(), owners, host, {1}).empty());
    CHECK(split_income(std::numeric_limits<double>::infinity(), owners, host, {1}).empty());
    // money is never made: the parts never exceed the gain, for any owner mix
    for (auto o : std::vector<std::multiset<uint8_t>>{{0, 1, 2, kSharedCity}, {kSharedCity, kSharedCity}, {1, 1, 1}, {3, 4}}) {
        double sum = 0;
        for (auto& [p, v] : split_income(1000, o, host, {1, 2, 3}))
            sum += v;
        CHECK(sum <= 1000 + 1e-9 && sum >= 0);
    }
    // the trade between two players' cities: half to each end with a wallet
    auto tp = split_pair(40, 0, 1, host, {1});
    CHECK(tp.size() == 1 && near(tp[1], 20));
    tp = split_pair(40, 1, 2, host, {1, 2});
    CHECK(tp.size() == 2 && near(tp[1], 20) && near(tp[2], 20));
    CHECK(split_pair(40, 1, 2, host, {}).empty() && split_pair(-1, 1, 2, host, {1, 2}).empty());
    // a newcomer: an equal part of the host's money, whole money, nothing for less than two holders
    CHECK(newcomer_share(2000, 2) == 1000 && newcomer_share(1001, 2) == 500 && newcomer_share(900, 3) == 300);
    CHECK(newcomer_share(0, 2) == 0 && newcomer_share(-5, 2) == 0 && newcomer_share(1000, 1) == 0);
    CHECK(newcomer_share(std::numeric_limits<double>::infinity(), 2) == 0);
    // the join split of the plan's test: host 2000 -> 1000 / 1000 (the sum stays)
    {
        double h = 2000, g = newcomer_share(h, 2);
        h -= g;
        CHECK(h == 1000 && g == 1000 && h + g == 2000);
    }
    // the ledger: the host's operations count until the guest confirms them, then its report is the value
    Ledger l;
    l.set(1, 500);
    CHECK(l.value() == 500);
    l.op(2, +100); // an income share
    l.op(3, -250); // a vehicle the host bought for it
    CHECK(l.value() == 350);
    CHECK(l.report(2, 600)); // the guest applied op 2 (500 + 100), op 3 still on the way
    CHECK(l.value() == 350 && l.sent.size() == 1);
    CHECK(l.report(3, 330)); // it applied op 3 and spent 20 in its own planner meanwhile
    CHECK(l.value() == 330 && l.sent.empty());
    CHECK(!l.report(2, 9999)); // an old report (crossed a newer one) is ignored
    CHECK(l.value() == 330);
    CHECK(!l.report(4, std::numeric_limits<double>::quiet_NaN()) && !l.report(4, 1e12));
    l.op(4, -1000); // never below 0
    CHECK(l.value() == 0);
    CHECK(clamp_money(2e8) == kMoneyMax && clamp_money(-1) == 0 && clamp_money(std::numeric_limits<double>::quiet_NaN()) == 0);
    // a purchase in flight while a split arrives (the plan's race): charge -300 (seq 5) and income +50 (seq 6) both
    // unconfirmed, the guest's report of seq 4 arrives: both still count, nothing is lost or made
    l.set(4, 1000);
    l.op(5, -300);
    l.op(6, +50);
    CHECK(l.report(4, 990)); // it spent 10 of its own meanwhile
    CHECK(l.value() == 740 && l.sent.size() == 2);
    CHECK(l.report(6, 740) && l.value() == 740 && l.sent.empty());
    // turning it off: the kept wallets go back to the host once (sum before = sum after), bad entries are skipped
    CHECK(merge_back(1000, {500, 250}) == 1750);
    CHECK(merge_back(1000, {std::numeric_limits<double>::quiet_NaN(), -5, 2e9}) == 1000);
    CHECK(merge_back(kMoneyMax - 10, {100}) == kMoneyMax);
    // who has a wallet: the setting, the peer's mod (civ minor kMinor = 2, the same rev), the guest in the host's world
    CHECK(kMinor == 2);
    CHECK(host_gives(true, true, false) && !host_gives(false, true, false) && !host_gives(true, false, false) &&
          !host_gives(true, true, true));
    CHECK(guest_runs(true, true) && !guest_runs(false, true) && !guest_runs(true, false));
    CHECK(reads_wallet(true, true, 1, 2, 1) && reads_wallet(true, true, 1, 3, 1));
    CHECK(!reads_wallet(true, true, 1, 1, 1)); // a 0.7.16.1 peer (civ 1.1): shared money as before
    CHECK(!reads_wallet(false, true, 1, 2, 1) && !reads_wallet(true, false, 1, 2, 1) && !reads_wallet(true, true, 2, 2, 1));
    // civ.wallet_row: a guest with a wallet does not take the shared total; off: it does (one money); no wallet: it does
    CHECK(!guest_takes_shared(true, true) && guest_takes_shared(false, true) && guest_takes_shared(true, false));
    // the stored record: v1, 4 bytes
    CHECK(stored_ok(1, 4) && !stored_ok(2, 4) && !stored_ok(1, 8) && !stored_ok(0, 0));
}

// BUG-1478 (CIV-5): the guest hooks hold only in the host's Civ world; the host takes world commands only from there
static void test_local_play_gate()
{
    printf("local_play_gate\n");
    CHECK(guest_holds(true, true, true, false));   // the guest in the host's Civ world: held as before
    CHECK(!guest_holds(true, true, true, true));   // another Civ world of its own: its own game plays it
    CHECK(guest_holds(false, true, true, true));   // switch off: held everywhere (0.7.16.1)
    CHECK(!guest_holds(true, false, true, false)); // the host / alone: never
    CHECK(!guest_holds(true, true, false, false)); // not in Civ
    CHECK(host_takes_world_cmd(true, false) && !host_takes_world_cmd(true, true) && host_takes_world_cmd(false, true));
    // the income gate on top of it: a guest elsewhere keeps its income, in the host's world it is dropped
    CHECK(guest_income_blocked(false, true, guest_holds(true, true, true, false), true, 80.f));
    CHECK(!guest_income_blocked(false, true, guest_holds(true, true, true, true), true, 80.f));
    CHECK(guest_income_blocked(false, true, guest_holds(false, true, true, true), true, 80.f));
}

// BUG-1236 (civ.income_cause): the cities' passive income is no host cause; everything else is
static void test_passive_income_cause()
{
    printf("passive_income_cause\n");
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
    // passive +200 alone: no host cause with the fix (passive noted), +200 without (passive 0: 0.7.16.1)
    CHECK(near(net::host_own_cause(1200, 1000, 0, 200), 0));
    CHECK(near(net::host_own_cause(1200, 1000, 0, 0), 200));
    // passive +200 with a host purchase -1000 in the same total: exactly -1000 (not -800)
    CHECK(near(net::host_own_cause(1200, 2000, 0, 200), -1000));
    // a sale of the host (+400, not passive) is still the host's
    CHECK(near(net::host_own_cause(1400, 1000, 0, 0), 400));
    // passive +200 with a guest's spend -500 applied: no host cause
    CHECK(near(net::host_own_cause(700, 1000, -500, 200), 0));
    // passive +200, a guest's +300, the host's -600: exactly -600
    CHECK(near(net::host_own_cause(900, 1000, 300, 200), -600));
    // two passive ticks in one total (the 6 s tick 160 and a delivery 80)
    CHECK(near(net::host_own_cause(1240, 1000, 0, 240), 0));
    // own money: income 400 of which 200 went to a guest's wallet: the host's money +200, passive noted 200: no cause
    CHECK(near(net::host_own_cause(1200, 1000, 0, 400 - 200), 0));
    // the game's cap: the measured gain (49 of 200) is noted, nothing left
    CHECK(near(net::host_own_cause(999999, 999950, 0, 49), 0));
}

// BUG-0438 (civ.veh_burst): 20 Hz moving-vehicle kinematics, host velocity selection, and burst-window lead
static void test_veh_burst()
{
    printf("veh_burst\n");
    // Sequence gap detection: consecutive advances are not gaps; 1..62 dropped packets are; wraparound works; huge jumps ignored.
    CHECK(!seq_gap(10, 10) && !seq_gap(10, 11));
    CHECK(seq_gap(10, 12) && seq_gap(10, 15));
    CHECK(!seq_gap(0xFFFF, 0) && seq_gap(0xFFFF, 1) && seq_gap(0xFFFE, 1));
    CHECK(!seq_gap(100, 99)); // out-of-order / old packet

    // Extrapolation time: outside a burst window (host hitch), stays clamped at normal knee/tau/cap (0.30 s).
    double normal_long = veh_extrap_time(0.45, 0.035, 0.14, false, true, 0.30, 0.12, true);
    CHECK(normal_long < 0.30 && normal_long > 0.25);
    // Inside a confirmed loss burst (seq gap armed) with burst_fix ON, extrapolates cleanly past 0.40 s without stalling at 0.28 s.
    double burst_on = veh_extrap_time(0.40, 0.035, 0.14, true, true, 0.30, 0.12, true);
    double burst_off = veh_extrap_time(0.40, 0.035, 0.14, true, false, 0.30, 0.12, true);
    CHECK(burst_on > 0.42 && burst_on <= kVehBurstLead);
    CHECK(burst_off < 0.30); // off reproduces the 0.28 s stall (3+ m trail at 20 m/s)
    // At a 0.40 s gap on a 20 m/s straight drive (true lead needed = 0.435 s), error drops from >3 m to <0.3 m:
    CHECK((0.435 - burst_on) * 20.0 < 0.35);
    CHECK((0.435 - burst_off) * 20.0 > 2.8);

    // Host velocity selection: prefers locomotion velocity when valid and non-zero, falls back to finite-difference, rejects NaN/excess.
    float out[3] = {};
    float loco_ok[3] = {20.f, 0.f, 0.f}, zero[3] = {0.f, 0.f, 0.f};
    float p0[3] = {0.f, 0.f, 0.f}, p1[3] = {1.f, 0.f, 0.f};
    veh_vel_pick(loco_ok, p1, p0, true, 0.05, 50.f, out);
    CHECK(std::fabs(out[0] - 20.f) < 1e-4f);
    veh_vel_pick(zero, p1, p0, true, 0.05, 50.f, out); // loco 0 -> finite-difference 1 / 0.05 = 20 m/s
    CHECK(std::fabs(out[0] - 20.f) < 1e-4f);
    float loco_nan[3] = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f};
    veh_vel_pick(loco_nan, p1, p0, true, 0.05, 50.f, out); // NaN loco -> falls back to clean finite-difference
    CHECK(std::fabs(out[0] - 20.f) < 1e-4f);
    float loco_jump[3] = {200.f, 0.f, 0.f};
    veh_vel_pick(loco_jump, p0, p0, true, 0.05, 50.f, out); // exceeds max_speed -> 0
    CHECK(out[0] == 0.f && out[1] == 0.f && out[2] == 0.f);

    // Moving / stop-tail filter: sends while moving, for kVehStopTail s after stopping, then stops streaming idle vehicles.
    CHECK(veh_kin_send(loco_ok, p1, p1, true, 10.0, 10.0));
    CHECK(veh_kin_send(zero, p1, p1, true, 10.2, 10.0));   // within stop tail: sends vel=0
    CHECK(!veh_kin_send(zero, p1, p1, true, 10.5, 10.0));  // idle past stop tail: skipped

    // VelocityEstimate::update_vel: immediate turn response (zero EMA lag) vs old EMA update.
    net::VelocityEstimate e_vel, e_ema;
    e_vel.max_speed = e_ema.max_speed = 60.f;
    for (int i = 0; i < 5; i++) {
        double t = i * 0.05;
        net::Vec3 p{(float)(20.0 * t), 0.f, 0.f};
        e_vel.update_vel(p, {20.f, 0.f, 0.f}, t + 0.035, t);
        e_ema.update(p, t + 0.035, t);
    }
    // Sharp 90-degree turn at t = 0.25 s: velocity switches to (0, 20, 0) immediately.
    e_vel.update_vel({4.0f, 1.0f, 0.f}, {0.f, 20.f, 0.f}, 0.285, 0.25);
    e_ema.update({4.0f, 1.0f, 0.f}, 0.285, 0.25);
    CHECK(std::fabs(e_vel.vel.x) < 1e-4f && std::fabs(e_vel.vel.y - 20.f) < 1e-4f);
    CHECK(std::fabs(e_ema.vel.x) > 5.f); // old EMA still dragged along old +X direction
}

static void test_guest_ship_editor()
{
    printf("guest spaceship editor (BUG-1531)\n");
    using namespace ship_editor;
    constexpr uint32_t kCiv = 0x01654C04u, kTribe = 0x01654C02u;

    // Guards: fix off, host, elsewhere, wrong mode, wrong editor id
    State s;
    CHECK(!s.on_vote_end(kShipEditor, kCiv, kCiv, true, false, false, 10.0));   // fix off
    CHECK(!s.on_vote_end(kShipEditor, kCiv, kCiv, false, false, true, 10.0));   // host
    CHECK(!s.on_vote_end(kShipEditor, kCiv, kCiv, true, true, true, 10.0));     // elsewhere
    CHECK(!s.on_vote_end(kShipEditor, kTribe, kCiv, true, false, true, 10.0));  // wrong mode
    CHECK(!s.on_vote_end(0x99E92F05u, kCiv, kCiv, true, false, true, 10.0));    // City Hall, not ship
    CHECK(s.phase == Phase::Idle && s.votes == 0);

    // Happy path: vote passes -> settles 0.5 s -> opens -> editor seen -> result captured -> returns to Civ
    CHECK(s.on_vote_end(kShipEditor, kCiv, kCiv, true, false, true, 10.0));
    CHECK(s.phase == Phase::Pending && s.votes == 1 && s.held());
    // Duplicate vote while active is ignored
    CHECK(!s.on_vote_end(kShipEditor, kCiv, kCiv, true, false, true, 10.1));
    // Busy/paused resets settle clock
    CHECK(s.step_pending(true, true, true, true, false, false, false, true, false, false, 10.4, 0.1) == StepAction::Wait);
    CHECK(s.step_pending(true, true, true, true, false, false, false, false, false, false, 10.7, 0.3) == StepAction::Wait);
    CHECK(s.step_pending(true, true, true, true, false, false, false, false, false, false, 11.0, 0.3) == StepAction::Open);
    CHECK(s.phase == Phase::Opening);
    // Defer inside outside-frame task returns to Pending and settles again
    s.defer_opening(11.0);
    CHECK(s.phase == Phase::Pending);
    CHECK(s.step_pending(true, true, true, true, false, false, false, false, false, false, 11.6, 0.6) == StepAction::Open);
    s.on_submit_result(true, 11.6);
    CHECK(s.phase == Phase::Open && s.opened == 1);
    // Ignores unrelated tags (e.g. native tag or outfitter tag)
    uint32_t k_other[3] = {0x11111111u, 0x476A98C7u, 0x40656200u};
    CHECK(!s.on_result(kNativeTag, kUfoDesign, k_other, false));
    // Sees Editor mode, receives own tag result, returns to Civ
    CHECK(s.step_open(false, true, false, 12.0) == StepAction::Wait && s.editor_seen);
    uint32_t k_mine[3] = {0xAABBCCDDu, 0x476A98C7u, 0x40656200u};
    CHECK(s.on_result(kOwnShipTag, kUfoDesign, k_mine, false));
    CHECK(s.results == 1 && s.cancels == 0 && s.res.key[0] == 0xAABBCCDDu);
    CHECK(s.step_open(false, false, true, 13.0) == StepAction::Wait); // loading transition
    CHECK(s.step_open(true, false, false, 13.5) == StepAction::Returned);
    CHECK(s.phase == Phase::Done && !s.held());

    // Session end cancels Pending (BUG-0234), but keeps an already Open editor tracked so return unpauses
    State s2;
    CHECK(s2.on_vote_end(kShipEditor, kCiv, kCiv, true, false, true, 20.0));
    s2.on_session_end();
    CHECK(s2.phase == Phase::Idle && s2.dropped == 1 && !s2.held());
    CHECK(s2.on_vote_end(kShipEditor, kCiv, kCiv, true, false, true, 25.0));
    CHECK(s2.step_pending(true, true, true, true, false, false, false, false, false, false, 25.6, 0.6) == StepAction::Open);
    s2.on_submit_result(true, 25.6);
    s2.on_session_end();
    CHECK(s2.phase == Phase::Open && s2.held());
    CHECK(s2.step_open(false, true, false, 26.0) == StepAction::Wait);
    CHECK(s2.on_result(kOwnShipTag, kUfoDesign, nullptr, true) && s2.cancels == 1);
    CHECK(s2.step_open(true, false, false, 27.0) == StepAction::Returned && s2.phase == Phase::Done);

    // Timeout if editor never appears after Submit
    State s3;
    CHECK(s3.on_vote_end(kShipEditor, kCiv, kCiv, true, false, true, 50.0));
    CHECK(s3.step_pending(true, true, true, true, false, false, false, false, false, false, 50.6, 0.6) == StepAction::Open);
    s3.on_submit_result(true, 50.6);
    CHECK(s3.step_open(true, false, false, 50.6 + kOpenLimit + 1.0) == StepAction::Drop);
    CHECK(s3.phase == Phase::Idle && s3.dropped == 1);

    // Hard timeout caps paused wait; direct exit to Space/Menu drops cleanly without Civ unpause
    State s4;
    CHECK(s4.on_vote_end(kShipEditor, kCiv, kCiv, true, false, true, 100.0));
    for (int i = 1; i <= 305; i++)
        s4.step_pending(true, true, true, true, false, false, false, false, true, false, 100.0 + i, 1.0);
    CHECK(s4.phase == Phase::Idle && s4.dropped == 1);
}

// BUG-1231 (civ.citizen_gesture): standing city citizen gestures follow the host's animation
static void test_citizen_gestures()
{
    printf("citizen_gestures\n");
    constexpr uint32_t kCheer = 0x04A1DD75u, kTalk = 0x04A3442Du;
    CHECK(!is_citizen_gesture(0));
    CHECK(!is_citizen_gesture(kCitizenGoneState));
    CHECK(!is_citizen_gesture(kCitizenRunAnim));
    CHECK(!is_citizen_gesture(kCitizenIdleAnim));
    CHECK(is_citizen_gesture(kCheer) && is_citizen_gesture(kTalk));

    uint32_t last = 0;
    double at = -100;
    // fix off: never plays or ends gestures
    CHECK(citizen_gesture_step(false, false, kCitizenIdleAnim, kCheer, last, at, 10.0) == CitizenAnimAction::Keep);
    // old host (0) or gone sentinel: no-op
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, 0, last, at, 10.0) == CitizenAnimAction::Keep);
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, kCitizenGoneState, last, at, 10.0) == CitizenAnimAction::Keep);
    // new standing gesture from host -> PlayGesture
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, kCheer, last, at, 10.0) == CitizenAnimAction::PlayGesture);
    CHECK(last == kCheer && at == 10.0);
    // already playing the same gesture -> Keep
    CHECK(citizen_gesture_step(true, false, kCheer, kCheer, last, at, 10.5) == CitizenAnimAction::Keep);
    // one-shot finished locally before next host snapshot (within repeat_gap) -> Keep (do not falsely restart)
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, kCheer, last, at, 10.8) == CitizenAnimAction::Keep);
    // host still playing the same gesture after repeat_gap -> replay
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, kCheer, last, at, 12.0) == CitizenAnimAction::PlayGesture);
    CHECK(at == 12.0);
    // host switches to a different gesture -> PlayGesture immediately
    CHECK(citizen_gesture_step(true, false, kCheer, kTalk, last, at, 12.2) == CitizenAnimAction::PlayGesture);
    CHECK(last == kTalk && at == 12.2);
    // host returns to plain idle while guest is still gesturing -> EndToIdle and clear last
    CHECK(citizen_gesture_step(true, false, kTalk, kCitizenIdleAnim, last, at, 13.0) == CitizenAnimAction::EndToIdle);
    CHECK(last == 0);
    // already idle on both -> Keep
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, kCitizenIdleAnim, last, at, 13.5) == CitizenAnimAction::Keep);
    // moving clears last_gesture so the same gesture can play again after stopping
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, kCheer, last, at, 14.0) == CitizenAnimAction::PlayGesture);
    CHECK(citizen_gesture_step(true, true, kCitizenRunAnim, kCheer, last, at, 14.2) == CitizenAnimAction::Keep);
    CHECK(last == 0);
    CHECK(citizen_gesture_step(true, false, kCitizenIdleAnim, kCheer, last, at, 14.5) == CitizenAnimAction::PlayGesture);
}

static void test_friendly_fire()
{
    printf("friendly_fire\n");
    CHECK(ff_damage_gate(false, 0, 1) == FfGate::Block);
    CHECK(ff_damage_gate(true, 0, 1) == FfGate::Allow);
    CHECK(ff_damage_gate(false, 1, 1) == FfGate::Normal);
    CHECK(ff_damage_gate(false, 0, kShared) == FfGate::Normal);
    CHECK(ff_damage_gate(false, 0, kNpc) == FfGate::Normal);
    CHECK(can_ff_order(true, 0, 1, 0));
    CHECK(!can_ff_order(false, 0, 1, 0));
    CHECK(!can_ff_order(true, 0, 1, 1)); // religious vehicle does not shoot
    CHECK(!can_ff_order(true, 0, 1, 2)); // economic vehicle does not shoot
    CHECK(!can_ff_order(true, 0, 0, 0));
    CHECK(!can_ff_order(true, 0, kShared, 0));
    CHECK(ff_combat_step(60.f, 45.f, 10.0, 9.0, 9.0) == FfStep::Move);
    CHECK(ff_combat_step(60.f, 45.f, 10.0, 10.5, 9.0) == FfStep::Wait);
    CHECK(ff_combat_step(30.f, 45.f, 10.0, 10.5, 9.0) == FfStep::Fire);
    CHECK(ff_combat_step(30.f, 45.f, 10.0, 9.0, 10.5) == FfStep::Wait);
}

int main()
{
    test_money();
    test_audit_v1();
    test_owners();
    test_holds();
    test_citizens();
    test_wallets();
    test_local_play_gate();
    test_passive_income_cause();
    test_veh_burst();
    test_guest_ship_editor();
    test_citizen_gestures();
    test_friendly_fire();
    printf(g_fail ? "civ_tests: %d FAILED\n" : "civ_tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
