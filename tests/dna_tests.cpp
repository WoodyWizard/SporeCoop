// Native tests of dna_own.h (WP-D of 0.7.16, BUG-1291): the own wallets of "party income, own wallet", the bar
// observer, the store record and MSG_DNA's tail (make test, under ASan / UBSan).
#include <cmath>
#include <cstdio>

#include "dna_own.h"
#include "own_progress_logic.h"

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void who()
{
    CHECK(dna_own::active(true, false, false));
    CHECK(!dna_own::active(false, false, false)); // creature.own_dna off: the shared pool (0.7.15)
    CHECK(!dna_own::active(true, true, false));   // all.shared_dna on
    CHECK(!dna_own::active(true, false, true));   // creature.mode host: one species, one pool
    // a 0.7.15.2 peer (creature 1.1) plays the shared pool; one without families (before 0.7.15) too
    CHECK(!dna_own::peer_reads(true, true, 1, 1, 1));
    CHECK(dna_own::peer_reads(true, true, 1, 2, 1));
    CHECK(dna_own::peer_reads(true, true, 1, 5, 1));
    CHECK(!dna_own::peer_reads(false, false, 1, 0, 1));
    CHECK(!dna_own::peer_reads(true, false, 0, 0, 1));
    CHECK(!dna_own::peer_reads(true, true, 2, 2, 1)); // another rev: never joined anyway
}

static void income_and_spends()
{
    dna_own::Book b;
    const uint32_t W = 0x1234;
    // first sight: the host's DNA now
    bool made = false;
    CHECK(near(b.budget("s:a", W, 120, &made), 120) && made);
    CHECK(near(b.budget("s:a", W, 999, &made), 120) && !made); // known: the host's number no longer matters
    // the bar's first read is a baseline, not a gain
    CHECK(b.observe(500, W, 1) == 0);
    // the party gains 20 (anybody's): every wallet of the world +20
    CHECK(near(b.observe(520, W, 1), 20));
    CHECK(near(b.budget("s:a", W, 0), 140));
    // a guest's own spend: its wallet only, never below 0
    CHECK(near(b.add("s:a", W, -30, 0), 110));
    b.budget("s:b", W, 140);
    CHECK(near(b.add("s:b", W, -500, 0), 0));
    // a spend never moves the bar; the same bar is no gain
    CHECK(b.observe(520, W, 1) == 0);
    // a lower bar (a load, a setback): not passed on, a new baseline
    CHECK(b.observe(400, W, 1) == 0);
    CHECK(near(b.observe(410, W, 1), 10));
    CHECK(near(b.budget("s:a", W, 0), 120) && near(b.budget("s:b", W, 0), 10));
    // a new store (the host's world opened again) and another world: new baselines
    CHECK(b.observe(900, W, 2) == 0);
    CHECK(near(b.observe(905, W, 2), 5));
    CHECK(b.observe(950, W + 1, 2) == 0);
    CHECK(near(b.observe(951, W + 1, 2), 1));
    CHECK(near(b.budget("s:a", W, 0), 125)); // (the other world's gain is not this world's)
    // another world: first sight again, from the host's DNA
    CHECK(near(b.budget("s:a", W + 1, 77, &made), 77) && made);
    // no bar (no player, a load): nothing, and the next read is a baseline
    CHECK(b.observe(NAN, W + 1, 2) == 0);
    CHECK(b.observe(2000, W + 1, 2) == 0);
    // a broken read (a huge step) is no gain
    CHECK(b.observe(2000 + 2 * dna_own::kMaxStep, W + 1, 2) == 0);
    // adopt (the host's word)
    b.adopt("s:a", "s:c");
    CHECK(!b.has_budget("s:a", W + 1) && b.has_budget("s:c", W + 1));
}

static void cell()
{
    dna_own::Book b;
    const uint32_t W = 7;
    bool made = false;
    CHECK(b.spent("s:a", W, 12, &made) == 12 && made); // first sight: the host's count
    CHECK(b.spend_cell("s:a", W, 6, 99) == 18);         // its own editor spent 6
    CHECK(b.spent("s:a", W, 40) == 18);                 // the host's own spends are not the guest's
    CHECK(b.spent("s:a", W + 1, 3, &made) == 3 && made);
}

static void record()
{
    dna_own::Wallet x;
    x.creature = {0xAABBCCDD, 123.5f};
    x.cell = {0x11, 42};
    auto d = dna_own::encode(x);
    CHECK(d.size() == 16);
    dna_own::Wallet y;
    CHECK(dna_own::decode(d, y));
    CHECK(y.creature.world == 0xAABBCCDD && near(y.creature.value, 123.5f) && y.cell.world == 0x11 &&
          near(y.cell.value, 42));
    d.pop_back();
    CHECK(!dna_own::decode(d, y)); // cut: not taken
    dna_own::Wallet bad;
    bad.creature = {1, -5};
    CHECK(!dna_own::decode(dna_own::encode(bad), y));
    bad.creature = {1, NAN};
    CHECK(!dna_own::decode(dna_own::encode(bad), y));
}

static void tail()
{
    // the host's MSG_DNA: total, the causes, the pace byte, then the tail
    net::Writer w;
    w.f32(88);
    w.u8(0);
    w.u8(2);
    dna_own::Tail t;
    t.flags = dna_own::kFlagOwn;
    t.bar = 640;
    t.world = 0x99;
    t.write(w);
    {   // a 0.7.15 guest reads total, causes, pace and stops: the tail does not disturb it
        net::Reader r(w.data.data(), w.data.size());
        float total = r.f32();
        unsigned n = r.u8();
        int pace = r.u8();
        CHECK(r.ok() && near(total, 88) && n == 0 && pace == 2);
    }
    {
        net::Reader r(w.data.data(), w.data.size());
        r.f32();
        r.u8();
        r.u8();
        dna_own::Tail g = dna_own::Tail::read(r);
        CHECK(g.has && g.own() && near(g.bar, 640) && g.world == 0x99);
    }
    {   // an older host (no tail): no own total
        net::Writer o;
        o.f32(5);
        o.u8(0);
        o.u8(1);
        net::Reader r(o.data.data(), o.data.size());
        r.f32();
        r.u8();
        r.u8();
        CHECK(!dna_own::Tail::read(r).has);
    }
}

static void apply()
{
    // after a reload the guest's game has the host's save value and bar: set the budget, raise the bar to the party's
    auto a = dna_own::apply_own(60, 700, 100, 650);
    CHECK(near(a.raise, 50) && a.set);
    // in step: nothing
    a = dna_own::apply_own(60, 700, 60, 700);
    CHECK(a.raise == 0 && !a.set);
    // the bar is never lowered (a guest bar ahead: only the budget)
    a = dna_own::apply_own(60, 690, 70, 700);
    CHECK(a.raise == 0 && a.set);
    // no bar to read: the budget only
    a = dna_own::apply_own(60, NAN, 70, 700);
    CHECK(a.raise == 0 && a.set);
    // BUG-1393: the party's setback under one point (the host's respawn: 36.9 -> 36.0) is followed when asked
    a = dna_own::apply_own(37, 36.0f, 37, 36.9f, true);
    CHECK(near(a.raise, -0.9f) && a.set);
    a = dna_own::apply_own(37, 36.0f, 37, 36.9f, false); // (the switch off: kept)
    CHECK(a.raise == 0 && !a.set);
    // within the compare's half point, or a fall of a point or more (a load): never down
    a = dna_own::apply_own(37, 36.6f, 37, 36.9f, true);
    CHECK(a.raise == 0 && !a.set);
    a = dna_own::apply_own(37, 35.5f, 37, 36.9f, true);
    CHECK(a.raise == 0 && !a.set);
    a = dna_own::apply_own(60, 690, 70, 700, true);
    CHECK(a.raise == 0 && a.set);
}

// ---- own progress (WP-P1 / P2 of 0.7.17, BUG-1469; own_progress_logic.h)
static void progress_mode()
{
    using namespace own_progress;
    CHECK(mode_of(0, true) == kOwnWallet && mode_of(1, true) == kShared && mode_of(2, true) == kOwnProgress);
    CHECK(mode_of(2, false) == kOwnWallet); // core.own_progress off: value 2 read as 0
    CHECK(mode_of(1, false) == kShared && mode_of(7, true) == kOwnWallet);
    // the old-peer gate: creature 1.3 and cell 1.2
    std::vector<net::FamilyRev> f = {{(uint8_t)net::Family::creature, 1, 3}, {(uint8_t)net::Family::cell, 1, 2}};
    CHECK(peer_reads(f, true));
    CHECK(!peer_reads(f, false));
    std::vector<net::FamilyRev> old = {{(uint8_t)net::Family::creature, 1, 2}, {(uint8_t)net::Family::cell, 1, 2}};
    CHECK(!peer_reads(old, true)); // 0.7.16.1: creature 1.2
    std::vector<net::FamilyRev> oldcell = {{(uint8_t)net::Family::creature, 1, 3}, {(uint8_t)net::Family::cell, 1, 1}};
    CHECK(!peer_reads(oldcell, true));
    CHECK(!peer_reads({{(uint8_t)net::Family::creature, 1, 3}}, true)); // no cell family at all
    // the start
    Start s = decide_start(true, {});
    CHECK(s.active && s.notice.empty());
    s = decide_start(true, {"Bob", "Ann"});
    CHECK(!s.active && s.notice.find("Bob and Ann play an older") != std::string::npos);
    s = decide_start(false, {"Bob"});
    CHECK(!s.active && s.notice.empty());
    CHECK(update_text().find("0.7.17") != std::string::npos);
}

static void progress_full()
{
    using namespace own_progress;
    Values c;
    c.mode = kCell;
    c.food = 640;
    CHECK(!full(c) && percent(c) == 64);
    c.food = 1000;
    CHECK(full(c) && percent(c) == 100);
    Values k;
    k.mode = kCreature;
    k.level = 3;
    CHECK(!full(k) && percent(k) == 75);
    k.level = 4;
    CHECK(full(k) && percent(k) == 100);
    CHECK(!full(Values{}) && percent(Values{}) == 0);
    // who is waited for
    std::vector<Player> ps = {{0, "Host", true, kCell, true, 100, 1000}, {1, "Bob", true, kCell, false, 64, 640},
                              {2, "Ann", false, 0, false, 0, 0}};
    std::string w;
    CHECK(!all_full(ps, kCell, 0, &w));
    CHECK(w == "Bob 64 %, Ann (not known yet)");
    CHECK(!all_full(ps, kCell, 1, &w) && w == "Ann (not known yet)"); // Bob's own line left out, the host is full
    ps[1].full = true, ps[1].pct = 100;
    ps[2] = {2, "Ann", true, kCell, true, 100, 1000};
    CHECK(all_full(ps, kCell, 0, &w) && w.empty());
    CHECK(!all_full(ps, kCreature, 0)); // another stage's reports do not count
    CHECK(all_full({{0, "Host", true, kCell, true, 100, 1000}}, kCell, 0)); // the host alone
    CHECK(!all_full({}, kCell, 0));
    // the stage-end gate: hold, a second click within 10 s forces, later a new hold
    EndGate g;
    CHECK(g.click(false, 100) == End::Hold);
    CHECK(g.click(false, 105) == End::Force);
    CHECK(g.click(false, 106) == End::Hold);
    CHECK(g.click(false, 120) == End::Hold);
    CHECK(g.click(true, 121) == End::Go);
    CHECK(g.click(false, 122) == End::Hold); // (the go cleared the refusal)
}

static void progress_levels()
{
    using namespace own_progress;
    int low = 0;
    CHECK(food_cap({}) == INT_MAX);
    CHECK(food_cap({24}, &low) == 99 && low == 1); // the guest on level 1: the host stays below 100
    CHECK(food_cap({99}) == 199);                  // every partner at the line: the next level opens
    CHECK(food_cap({150, 420}) == 199);
    CHECK(food_cap({1000}) == INT_MAX);
    CHECK(food_cap({950}) == INT_MAX); // level 10 is the last: the stage end waits instead
    CHECK(food_cap({850}) == 899);
    int bank = 0;
    CHECK(clip(30, 80, 99, bank) == 19 && bank == 11);
    CHECK(clip(30, 99, 99, bank) == 0 && bank == 41);
    CHECK(clip(5, 10, 99, bank) == 5 && bank == 41);
    CHECK(clip(30, 80, INT_MAX, bank) == 30 && bank == 41);
    CHECK(clip(-5, 98, 99, bank) == -5 && bank == 41); // a loss is never clipped
    CHECK(release(41, 99, 99) == 0);
    CHECK(release(41, 99, 199) == 41);
    CHECK(release(141, 99, 199) == 100);
    CHECK(release(41, 99, INT_MAX) == 41);
    CHECK(release(0, 0, INT_MAX) == 0);
    CHECK(level_of_food(0) == 1 && level_of_food(99) == 1 && level_of_food(100) == 2 && level_of_food(1000) == 10);
}

static void progress_wire()
{
    using namespace own_progress;
    Values v{kCreature, 125.f, 160.f, 0, 0, 2};
    net::Writer w;
    write_values(w, v);
    net::Reader r(w.data.data(), w.data.size());
    Values back;
    CHECK(read_values(r, back) && back == v);
    // a bad value is refused
    Values bad = v;
    bad.mode = 5;
    net::Writer wb;
    write_values(wb, bad);
    net::Reader rb(wb.data.data(), wb.data.size());
    CHECK(!read_values(rb, back) && back == v);
    // the table
    std::vector<Player> ps = {{0, "", true, kCell, false, 31, 310}, {2, "", false, 0, false, 0, 0}};
    net::Writer wt;
    write_table(wt, true, ps);
    net::Reader rt(wt.data.data(), wt.data.size());
    CHECK(rt.u8() == kTable);
    bool act = false;
    std::vector<Player> got;
    CHECK(read_table(rt, act, got) && act && got.size() == 2 && got[0].food == 310 && got[0].pct == 31 && got[0].known &&
          !got[0].full && got[1].id == 2 && !got[1].known && rt.left() == 0);
    net::Reader cut(wt.data.data(), wt.data.size() - 3);
    cut.u8();
    CHECK(!read_table(cut, act, got) && got.size() == 2);
    // the store record (29 bytes, v1)
    Record x{0xABCD, v, 0};
    auto d = encode(x);
    CHECK(d.size() == 29);
    Record y;
    CHECK(decode(d, y) && y == x);
    d.push_back(0);
    CHECK(!decode(d, y));
    Record neg = x;
    neg.debt = -1;
    CHECK(!decode(encode(neg), y));
    // what a guest gets back
    CHECK(restore_for(&x, 0xABCD, kCreature) == &x.v);
    CHECK(!restore_for(&x, 0xABCE, kCreature)); // another world
    CHECK(!restore_for(&x, 0xABCD, kCell));     // another stage
    CHECK(!restore_for(nullptr, 0xABCD, kCreature));
    // the restore order: the bar up through the game, a lower level written back
    CreatureApply a = creature_apply(3100.f, 4, v);
    CHECK(a.up == 0 && near(a.bar_write, 160.f) && near(a.dna, 125.f) && a.level == 2);
    a = creature_apply(100.f, 1, v);
    CHECK(near(a.up, 60.f) && std::isnan(a.bar_write) && a.level == -1);
    a = creature_apply(NAN, -1, v);
    CHECK(a.up == 0 && std::isnan(a.bar_write) && a.level == -1);
}

int main()
{
    who();
    income_and_spends();
    cell();
    record();
    tail();
    apply();
    progress_mode();
    progress_full();
    progress_levels();
    progress_wire();
    if (g_fail) {
        printf("dna_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("dna_tests: all passed\n");
    return 0;
}
