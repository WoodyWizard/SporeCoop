// Native tests of dna_own.h (WP-D of 0.7.16, BUG-1291): the own wallets of "party income, own wallet", the bar
// observer, the store record and MSG_DNA's tail (make test, under ASan / UBSan).
#include <cmath>
#include <cstdio>

#include "dna_own.h"

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

int main()
{
    who();
    income_and_spends();
    cell();
    record();
    tail();
    apply();
    if (g_fail) {
        printf("dna_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("dna_tests: all passed\n");
    return 0;
}
