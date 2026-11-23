// Native tests of revive_logic.h (WP-R1 of 0.7.17, BUG-1472; rules from PR #214 by IltonBJSilva): who catches a lethal
// hit (the pack first), the window, the revive, the party wipe, the price per DNA mode, the progress debt, the wire of
// MSG_RESCUE and the old-peer gate (make test, under ASan / UBSan).
#include <cmath>
#include <cstdio>

#include "revive_logic.h"

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

using namespace revive;

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void catching()
{
    HitIn in;
    CHECK(catch_hit(in) == Catch::Pass); // not lethal
    in.lethal = true;
    CHECK(catch_hit(in) == Catch::Pass); // revive off, the pack not there: the game's death (0.7.16.1)
    in.pack = true;
    in.revive = in.partner_here = true;
    CHECK(catch_hit(in) == Catch::Pack); // the pack rescue is the first line (owner question 4, provisional)
    in.pack = false;
    CHECK(catch_hit(in) == Catch::Down);
    in.partner_here = false;
    CHECK(catch_hit(in) == Catch::Pass); // alone: nobody could revive it
    in.partner_here = true;
    in.host_reads = false;
    CHECK(catch_hit(in) == Catch::Pass); // a guest under an older host (creature 1.2): dies as before
    in.host_reads = true;
    in.revive = false;
    CHECK(catch_hit(in) == Catch::Pass); // creature.revive off: the hit kills
    CHECK(lethal(10, 10) && lethal(11, 10) && !lethal(9, 10) && !lethal(10, 0) && !lethal(NAN, 5) && !lethal(-1, 5));
}

static void window_and_revive()
{
    Watch w;
    CHECK(begin_ok(w, 1) && !begin_ok(w, 0));
    w = {1, true, 100.0, -1, false};
    CHECK(!begin_ok(w, 2)); // one at a time
    CHECK(step(w, false, 110, kWindowS) == Step::Waiting);
    CHECK(near((float)seconds_left(w, 110, kWindowS), 50));
    CHECK(step(w, true, 111, kWindowS) == Step::Waiting && near(revive_progress(w, 111), 0));
    CHECK(near(revive_progress(w, 111.75), 0.5f));
    CHECK(step(w, false, 112, kWindowS) == Step::Waiting && w.near_since < 0); // the partner stepped away: again
    CHECK(step(w, true, 113, kWindowS) == Step::Waiting);
    CHECK(step(w, true, 114.5, kWindowS) == Step::Revived);
    // a revive that would end after the window does not count
    Watch late{2, true, 0.0, -1, false};
    CHECK(step(late, true, 59.0, kWindowS) == Step::Waiting);
    CHECK(step(late, true, 60.0, kWindowS) == Step::Expired);
    // give up: expired at once, also with a partner next to it
    Watch g{3, true, 0.0, -1, true};
    CHECK(step(g, true, 1.0, kWindowS) == Step::Expired);
    // an ended knockout's repeated request does not start a new one; a newer number does (also over the wrap)
    Watch e{5, false, 0, -1, false};
    CHECK(!begin_ok(e, 5) && !begin_ok(e, 4) && begin_ok(e, 6));
    Watch wrap{0xFFFFFFFFu, false, 0, -1, false};
    CHECK(begin_ok(wrap, 1));
    CHECK(step(e, true, 0, kWindowS) == Step::Waiting); // an ended one: no verdict twice
}

static void wipe()
{
    double since = -1;
    CHECK(!wipe_due(since, true, false, 10) && near((float)since, 10));
    CHECK(!wipe_due(since, true, false, 11.9));
    CHECK(wipe_due(since, true, false, 12.1) && since < 0);
    CHECK(!wipe_due(since, true, true, 13) && since < 0); // someone stands
    CHECK(!wipe_due(since, false, false, 13) && since < 0); // nobody down
}

static void prices()
{
    CHECK(near(dna_cost(100), 10) && near(dna_cost(95), 10) && near(dna_cost(5), 1) && near(dna_cost(0.5f), 0) &&
          near(dna_cost(1), 1) && near(dna_cost(NAN), 0));
    PriceIn in;
    in.player_dna = 200;
    in.pool_dna = 500;
    // own wallet: the downed player's own wallet
    in.dna_mode = 0;
    in.own_wallet = true;
    Price p = price_for(in);
    CHECK(p.kind == kPriceWallet && near(p.amount, 20));
    // own wallet, but no wallet (host creature mode, creature.own_dna off): the shared pool
    in.own_wallet = false;
    p = price_for(in);
    CHECK(p.kind == kPricePool && near(p.amount, 50));
    // shared: the pool
    in.dna_mode = 1;
    in.own_wallet = true;
    p = price_for(in);
    CHECK(p.kind == kPricePool && near(p.amount, 50));
    // own progress: a debt of its own DNA's share
    in.dna_mode = 2;
    p = price_for(in);
    CHECK(p.kind == kPriceDebt && near(p.amount, 20));
    // creature.revive_debt off: PR #214's rule, the host's total
    in.own_price = false;
    p = price_for(in);
    CHECK(p.kind == kPricePool && near(p.amount, 50));
    // nothing to pay
    in.own_price = true;
    in.player_dna = 0;
    p = price_for(in);
    CHECK(p.kind == kPriceNone && near(p.amount, 0));
}

static void debt()
{
    float d = 30;
    CHECK(near(pay_from_gain(d, 10), 0) && near(d, 20));
    CHECK(near(pay_from_gain(d, 25), 5) && near(d, 0));
    CHECK(near(pay_from_gain(d, 7), 7) && near(d, 0)); // paid: the gains count whole
    d = 5;
    CHECK(near(pay_from_gain(d, -3), -3) && near(d, 5)); // a spend is not a gain
    d = NAN;
    CHECK(near(pay_from_gain(d, 4), 4));
    CHECK(sane_debt(0) && sane_debt(12) && !sane_debt(-1) && !sane_debt(NAN) && !sane_debt(2e7f));
}

static void wire()
{
    // the verdict
    Verdict v;
    v.id = 7;
    v.result = R_EXPIRED;
    v.price = {kPriceDebt, 12};
    net::Writer w;
    write_verdict(w, v);
    net::Reader r(w.data.data(), w.data.size());
    Verdict x;
    CHECK(read_verdict(r, x) && x.id == 7 && x.result == R_EXPIRED && x.price.kind == kPriceDebt && near(x.price.amount, 12));
    net::Reader cut(w.data.data(), w.data.size() - 1);
    CHECK(!read_verdict(cut, x));
    net::Writer bad;
    bad.u32(7);
    bad.u8(9); // an unknown result
    bad.u8(0);
    bad.f32(0);
    net::Reader rb(bad.data.data(), bad.data.size());
    CHECK(!read_verdict(rb, x));
    // the lines
    std::vector<Line> ls = {{1, tenths_left(55.04), 30}, {0, tenths_left(0.0), 0}};
    CHECK(ls[0].tenths == 551 && tenths_left(1000) == kMaxTenths && tenths_left(-3) == 0);
    net::Writer wl;
    write_lines(wl, ls);
    net::Reader rl(wl.data.data(), wl.data.size());
    std::vector<Line> got;
    CHECK(read_lines(rl, got) && got == ls);
    net::Writer wm;
    wm.u8(1);
    wm.u8(9); // player 9: no such seat
    wm.u16(10);
    wm.u8(0);
    net::Reader rm(wm.data.data(), wm.data.size());
    CHECK(!read_lines(rm, got) && got.empty());
    net::Writer we;
    write_lines(we, {});
    net::Reader re(we.data.data(), we.data.size());
    CHECK(read_lines(re, got) && got.empty()); // the last window closed
}

static void old_peers()
{
    using net::FamilyRev;
    const uint8_t cr = (uint8_t)net::Family::creature;
    CHECK(peer_reads({{cr, 1, 3}}, true) && peer_reads({{cr, 1, 4}}, true));
    CHECK(!peer_reads({{cr, 1, 2}}, true)); // 0.7.16.1
    CHECK(!peer_reads({{cr, 1, 3}}, false) && !peer_reads({}, true)); // no families: before 0.7.15
    CHECK(!peer_reads({{cr, 2, 3}}, true));
    CHECK(kMinor == 3);
}

int main()
{
    catching();
    window_and_revive();
    wipe();
    prices();
    debt();
    wire();
    old_peers();
    printf("revive_tests: %s\n", g_fail ? "FAIL" : "ok");
    return g_fail ? 1 : 0;
}
