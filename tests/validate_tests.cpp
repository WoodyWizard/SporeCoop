// Native tests for net/validate.h (make test; built with AddressSanitizer): the checks the
// message handlers of the mod use for what other players send.
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "net/buffer.h"
#include "net/give_target.h"
#include "net/validate.h"
#include "dna_route.h"
#include "dna_ledger.h"
#include "dna_pace.h"
#include "host_world.h"
#include "net/shared_ledger.h"
#include <string>

using namespace net;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

// a piece in a heap block of exactly its size (ASan sees reads past it)
static Chunk add(ChunkRx& rx, uint32_t id, uint32_t total, uint32_t off, size_t n, uint8_t fill, size_t chunk = 900)
{
    std::unique_ptr<uint8_t[]> p(new uint8_t[n ? n : 1]);
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(fill + i);
    return chunk_add(rx, id, total, off, p.get(), n, chunk, 8u << 20);
}

static void test_chunks()
{
    printf("chunk assembly\n");
    {   // a buffer in three pieces, in any order, repeats ignored
        ChunkRx rx;
        CHECK(add(rx, 7, 2000, 900, 900, 9) == Chunk::Partial);
        CHECK(add(rx, 7, 2000, 900, 900, 9) == Chunk::Partial);
        CHECK(add(rx, 7, 2000, 1800, 200, 18) == Chunk::Partial);
        CHECK(add(rx, 7, 2000, 0, 900, 0) == Chunk::Complete);
        CHECK(rx.data.size() == 2000 && rx.data[0] == 0 && rx.data[900] == 9 && rx.data[1999] == (uint8_t)(18 + 199));
    }
    {   // an empty buffer is one empty piece
        ChunkRx rx;
        CHECK(add(rx, 3, 0, 0, 0, 0) == Chunk::Complete);
        CHECK(rx.data.empty());
    }
    {   // id 0 starts a buffer like any other id
        ChunkRx rx;
        CHECK(add(rx, 0, 1000, 900, 100, 1) == Chunk::Partial);
        CHECK(rx.data.size() == 1000);
    }
    {   // later pieces of the same buffer with another total (larger) never write past it
        ChunkRx rx;
        CHECK(add(rx, 5, 1000, 900, 100, 0) == Chunk::Partial);
        CHECK(add(rx, 5, 1u << 20, 900 * 100, 900, 0) == Chunk::Drop);
        CHECK(add(rx, 5, 1u << 20, 900, 900, 0) == Chunk::Drop);
        CHECK(add(rx, 5, 1u << 20, 0, 900, 0) == Chunk::Drop);
        CHECK(rx.data.size() == 1000 && rx.have == 100);
    }
    {   // the first piece sets the size: a piece claiming a larger total is dropped, not written
        ChunkRx rx;
        CHECK(add(rx, 6, 1000, 0, 900, 0) == Chunk::Partial);
        CHECK(add(rx, 6, 4000, 1800, 900, 0) == Chunk::Drop);
        CHECK(rx.data.size() == 1000 && rx.have == 900);
    }
    {   // pieces that do not fit the buffer's layout
        ChunkRx rx;
        CHECK(add(rx, 1, 2000, 450, 900, 0) == Chunk::Drop);  // not at a piece start
        CHECK(add(rx, 1, 2000, 0, 899, 0) == Chunk::Drop);    // too short (a hole)
        CHECK(add(rx, 1, 2000, 0, 901, 0) == Chunk::Drop);    // too long
        CHECK(add(rx, 1, 2000, 1800, 300, 0) == Chunk::Drop); // past the end
        CHECK(add(rx, 1, 2000, 2700, 0, 0) == Chunk::Drop);   // beyond the buffer
        CHECK(add(rx, 1, 2000, 2000, 0, 0) == Chunk::Drop);   // an empty piece at the end
        CHECK(add(rx, 1, (8u << 20) + 1, 0, 900, 0) == Chunk::Drop);
        CHECK(!rx.started);
    }
    {   // a new id replaces an unfinished buffer
        ChunkRx rx;
        CHECK(add(rx, 1, 2000, 0, 900, 0) == Chunk::Partial);
        CHECK(add(rx, 2, 100, 0, 100, 0) == Chunk::Complete);
        CHECK(rx.id == 2 && rx.data.size() == 100);
    }
}

static void test_cycle()
{
    printf("snapshot chunk numbers\n");
    CHECK(cycle_chunk_ok(0, 1));
    CHECK(cycle_chunk_ok(2, 3));
    CHECK(!cycle_chunk_ok(0, 0));
    CHECK(!cycle_chunk_ok(3, 3));
    CHECK(!cycle_chunk_ok(255, 1));
}

static void test_space_origin()
{
    printf("MSG_SPACE header\n");
    // host: a guest's message speaks for that guest
    CHECK(space_origin_ok(true, 2, 2, 1));
    CHECK(space_origin_ok(true, 2, 2, 21));  // contracts K_ACCEPT
    CHECK(!space_origin_ok(true, 2, 0, 1));  // as the host
    CHECK(!space_origin_ok(true, 2, 3, 10)); // as another guest
    CHECK(!space_origin_ok(true, 0, 0, 1));
    for (uint8_t k : {20, 22, 32, 50, 52, 57}) {
        CHECK(space_host_only(k));
        CHECK(!space_origin_ok(true, 2, 2, k)); // a guest never sends these
        CHECK(space_origin_ok(false, 0, 0, k));
        CHECK(!space_origin_ok(false, 0, 2, k)); // relayed from a guest
    }
    // guest: everything comes from the host; a relayed guest message keeps its origin
    CHECK(space_origin_ok(false, 0, 2, 10));
    CHECK(space_origin_ok(false, 0, 0, 1));
    CHECK(!space_origin_ok(false, 1, 1, 1));
    for (uint8_t k : {1, 2, 3, 4, 5, 6, 10, 11, 21, 30, 31, 33, 34, 40, 41, 42, 51, 53, 54, 55, 56, 60, 61, 70, 76})
        CHECK(!space_host_only(k));
}

static void test_numbers()
{
    printf("numbers from the wire\n");
    float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(sane_damage(12.5f, 100) == 12.5f);
    CHECK(sane_damage(-5, 100) == 0);
    CHECK(sane_damage(nan, 100) == 0);
    CHECK(sane_damage(inf, 100) == 0);
    CHECK(sane_damage(-inf, 100) == 0);
    CHECK(sane_damage(1e9f, 100) == 100);
    float ok3[3] = {1, 2, 3}, bad3[3] = {1, nan, 3};
    CHECK(finite3(ok3) && !finite3(bad3));
    int t = -1;
    CHECK(money_total(999, 1000, t) && t == 999000);
    CHECK(!money_total(999, 0x7FFFFFFF, t));
    CHECK(!money_total(3, 0x40000000, t));
    CHECK(!money_total(-1, 5, t) && !money_total(5, -1, t));
    CHECK(money_total(0, 0, t) && t == 0);
}

struct FakeSess {
    struct P {
        uint8_t id;
    };
    std::vector<P> v;
    std::vector<P> players() const { return v; }
};

static void test_players()
{
    printf("player ids\n");
    FakeSess s{{{0}, {2}}};
    CHECK(is_player(s, 0) && is_player(s, 2));
    CHECK(!is_player(s, 1) && !is_player(s, 0xFF));
}

static void test_give_target()
{
    printf("gift recipient (BUG-0075)\n");
    // three players, guest 2 picks guest 1: the gift goes to 1, not to the host
    CHECK(give_target({0, 1, 2}, 2, 1) == 1);
    CHECK(give_target({0, 1, 2}, 2, 1, false) == 0); // the old behaviour: always the first other player
    // nothing picked yet, or the pick left, or the pick is me: the first other player
    CHECK(give_target({0, 1, 2}, 2, kNoGiveTarget) == 0);
    CHECK(give_target({0, 2}, 2, 1) == 0);
    CHECK(give_target({0, 1, 2}, 0, 0) == 1);
    CHECK(give_target({2, 0, 1}, 0, kNoGiveTarget) == 1);
    CHECK(give_target({1}, 1, 0) == kNoGiveTarget); // alone
    // cycling goes through every other player and wraps
    CHECK(give_target_next({0, 1, 2, 3}, 1, kNoGiveTarget) == 2);
    CHECK(give_target_next({0, 1, 2, 3}, 1, 2) == 3);
    CHECK(give_target_next({0, 1, 2, 3}, 1, 3) == 0);
    CHECK(give_target_next({0, 1, 2, 3}, 1, 0) == 2);
    CHECK(give_target_next({0, 1}, 1, 0) == 0);
    CHECK(give_target_next({1}, 1, 0) == kNoGiveTarget);
}

// make test runs it with the fixes; `validate_tests --nofix` runs the checks of the wave 5 fixes
// with their switches off: they must fail
static bool g_nofix = false;

static void test_give_pick_leave()
{
    printf("gift pick after its player left (BUG-0234)\n");
    const bool fix = !g_nofix;
    // guest 2 picked guest 1; 1 leaves, a newcomer gets id 1: the gift goes to the first other player
    uint8_t pick = give_pick_after_leave(1, 1, fix);
    CHECK(give_target({0, 1, 2}, 2, pick) == 0);
    CHECK(give_pick_after_leave(3, 1, fix) == 3); // another player left: the pick stays
}

static void test_dna_ledger()
{
    printf("shared DNA tells where it came from (dna_ledger.h, the player request of 2026-11-01)\n");
    using namespace dna_ledger;
    // a guest's credited kill: the game's gains within kCreditSec are that guest's hunt
    Credit c;
    CHECK(c.gain(5, 10.0).cause == Cause::Host && c.gain(5, 10.0).player == 0);
    c.kill(2, 10.0);
    CHECK(c.gain(7, 10.1).cause == Cause::Hunt && c.gain(7, 10.1).player == 2);
    CHECK(c.gain(7, 10.0 + kCreditSec + 0.1).cause == Cause::Host);
    // the ledger text
    CHECK(causes_text({}) == "unknown");
    CHECK(causes_text({{Cause::Hunt, 1, 12}, {Cause::Host, 0, 5}}) == "hunt:1+12,host:0+5");
    // who sees a notice: the host only a guest's hunt; a guest every partner's gain and its own kill
    CHECK(worth_a_notice({Cause::Hunt, 1, 12}, 0) && !worth_a_notice({Cause::Host, 0, 12}, 0));
    CHECK(!worth_a_notice({Cause::Earned, 1, 12}, 0));
    CHECK(worth_a_notice({Cause::Host, 0, 12}, 1) && worth_a_notice({Cause::Hunt, 1, 12}, 1));
    CHECK(worth_a_notice({Cause::Earned, 2, 3}, 1) && !worth_a_notice({Cause::Earned, 1, 3}, 1));
    CHECK(!worth_a_notice({Cause::Host, 0, 0.4f}, 1) && !worth_a_notice({Cause::Host, 0, -10}, 1));
    // gathered: three kills of the pack in a row are one notice, then not again for kGap
    Notices n;
    n.add(Cause::Host, 0, 5, 1.0);
    n.add(Cause::Host, 0, 7, 1.5);
    n.add(Cause::Hunt, 2, 4, 1.6);
    CHECK(n.due(2.0).empty()); // still gathering
    auto d = n.due(2.6);
    CHECK(d.size() == 1 && d[0].cause == Cause::Host && d[0].amount == 12);
    n.add(Cause::Host, 0, 3, 3.0);
    CHECK(n.due(5.0).empty()); // the hunt is due, the gap is not over
    d = n.due(2.7 + kGap);
    CHECK(d.size() == 2);
    CHECK(notice_text({Cause::Hunt, 2, 12}, "Bob", false) == "+12 DNA from Bob's hunt");
    CHECK(notice_text({Cause::Hunt, 1, 9}, "Bob", true).rfind("+9 DNA from your hunt", 0) == 0);
    // Cell: a partner's meals, gathered longer
    CHECK(notice_text({Cause::Meal, kPartners, 20}, "", false) == "+20 food (DNA) from your partners' meals");
    CHECK(worth_a_notice({Cause::Meal, 1, 8}, 0) && worth_a_notice({Cause::Meal, kPartners, 8}, 1));
    Notices m;
    m.add(Cause::Meal, 1, 4, 0.0);
    m.add(Cause::Meal, 1, 4, 5.0);
    CHECK(m.due(10.0).empty() && m.due(kGatherMeals).size() == 1);
}

static void test_dna_pace()
{
    printf("the party's DNA grows at the single-player pace (dna_pace.h, BUG-0510)\n");
    using namespace dna_pace;
    CHECK(divisor(true, 2) == 2 && divisor(true, 0) == 1 && divisor(false, 4) == 1 && divisor(true, 999) == 255);
    CHECK(share(20, 2) == 10 && share(20, 1) == 20 && share(-25, 2) == -25 && share(0, 3) == 0);
    // two players, each earns 20: the shared total grows by 20 (one player's), not 40
    CHECK(share(20, 2) + share(20, 2) == 20);
    // integer meals keep the remainder: four meals of 3 points at N=2 give 1+2+1+2 = 6
    Carry c;
    int sum = 0;
    for (int i = 0; i < 4; i++)
        sum += c.take(3, 2);
    CHECK(sum == 6);
    Carry c3; // N=3, ten meals of 1: 3 points (and the rest kept, never lost)
    sum = 0;
    for (int i = 0; i < 10; i++)
        sum += c3.take(1, 3);
    CHECK(sum == 3 && c3.rest > 0.3f && c3.rest < 0.34f);
    CHECK(c.take(-5, 2) == -5 && c.take(7, 1) == 7); // a spend / the full sum: whole
}

static void test_dna_route()
{
    printf("DNA kept while the host was away stays in its stage and world (BUG-0233)\n");
    using namespace dna_route;
    const bool fix = !g_nofix;
    const uint32_t kCell = 0x1654C00, kCreature = 0x1654C01, kTribe = 0x1654C02, kCiv = 0x1654C04,
                   kSpace = 0x1654C05, kNoStage = 0; // a menu, an editor or a load
    const uint32_t stages[] = {kCreature, kTribe, kCiv};
    // an editor or a load counts as away in Creature, Tribe and Civ only
    CHECK(away_stage(kCreature, stages, 3) && away_stage(kTribe, stages, 3) && away_stage(kCiv, stages, 3));
    CHECK(!away_stage(kCell, stages, 3) && !away_stage(kSpace, stages, 3));
    // solo DNA at the end of the away: the host back in our world (row 9)
    CHECK(solo_end(fix, true, true, kCreature, kCreature) == SoloEnd::SendBack);
    // the host loaded another save of the stage, we followed (row 10, BUG-0151)
    CHECK(solo_end(fix, false, true, kCreature, kCreature) == SoloEnd::Send || !fix);
    // the host went on to Tribe while we were away in Creature (row 11): dropped, no "is back"
    CHECK(solo_end(fix, false, true, kTribe, kCreature) == SoloEnd::Drop);
    CHECK(solo_end(fix, false, false, kCreature, kCreature) == SoloEnd::Drop); // still loading
    // owed DNA on the host (rows 13-15): world 7 is where it was owed
    CHECK(owed(fix, kNoStage, false, false, 0, 7, kCreature) == Owed::Keep);       // a menu or an editor: wait
    CHECK(owed(fix, kCreature, true, true, 7, 7, kCreature) == Owed::Pay);         // back in that world
    CHECK(owed(fix, kCreature, true, false, 7, 7, kCreature) == Owed::Keep);       // no creature yet
    CHECK(owed(fix, kCreature, true, true, 9, 7, kCreature) == Owed::Clear);       // another Creature save
    CHECK(owed(fix, kTribe, true, false, 7, 7, kCreature) == Owed::Clear);         // the next stage
    CHECK(owed(fix, kSpace, true, true, 7, 7, kCreature) == Owed::Clear);          // a captain is no creature of that world
}

// BUG-0231 (GitHub #165): "is the host's world the one I play", the DNA rows it owns, the shared hold
static void test_host_world()
{
    printf("host_world: classify, DNA rows 1-8 and 12, the shared hold (BUG-0231)\n");
    using host_world::HostWorld;
    using host_world::classify;
    const uint32_t cr = host_world::kCreature, civ = host_world::kCiv, space = 0x1654C05, adv = 0x1654C10;
    // not a connected guest, Space, adventures, menus: never gated
    CHECK(classify(false, cr, 7, true, cr, 8, 0) == HostWorld::NotGated);
    CHECK(classify(true, space, 7, true, space, 8, 0) == HostWorld::NotGated);
    CHECK(classify(true, adv, 7, false, 0, 0, 0) == HostWorld::NotGated);
    CHECK(classify(true, 0x2CCD1D2, 7, true, cr, 7, 0) == HostWorld::NotGated);
    // the host in another stage: follow_phase pulls the guest along
    CHECK(classify(true, cr, 7, true, civ, 7, 7) == HostWorld::NotGated);
    // same stage: the state decides
    CHECK(classify(true, civ, 7, true, civ, 7, 0) == HostWorld::Same);
    CHECK(classify(true, civ, 7, true, civ, 8, 7) == HostWorld::Other); // the state wins over WELCOME
    // no state yet: WELCOME's world, 0 (older host) counts as Other
    CHECK(classify(true, cr, 7, false, 0, 0, 7) == HostWorld::Same);
    CHECK(classify(true, cr, 7, false, 0, 0, 0) == HostWorld::Other);
    CHECK(classify(true, cr, 7, false, 0, 0, 9) == HostWorld::Other);
    // this machine's world unknown: Other, never Same
    CHECK(classify(true, cr, 0, true, cr, 0, 0) == HostWorld::Other);
    CHECK(classify(true, cr, 0, false, 0, 0, 0) == HostWorld::Other);
    // the host names its world in WELCOME only in a gated stage
    CHECK(host_world::welcome_world(cr, 7) == 7 && host_world::welcome_world(space, 7) == 0);

    using namespace dna_route;
    CHECK(guest_gain(false, true, true, true) == Gain::Local);  // 1
    CHECK(guest_gain(true, false, true, true) == Gain::Local);  // 1 (a loss)
    CHECK(guest_gain(true, true, true, true) == Gain::Own);     // 2: before the away row
    CHECK(guest_gain(true, true, false, true) == Gain::Solo);   // 3
    CHECK(guest_gain(true, true, false, false) == Gain::Forward); // 4-5
    CHECK(host_total(true, false, true) == Total::Pending);     // 6
    CHECK(host_total(false, true, true) == Total::Drop);        // 7
    CHECK(host_total(false, false, false) == Total::Drop);      // 7
    CHECK(host_total(false, false, true) == Total::Apply);      // 8
    CHECK(host_add(true, true) == Add::Pay && host_add(false, true) == Add::Owe && host_add(false, false) == Add::Drop); // 12

    // the shared hold: pending and in-flight changes survive it, totals are ignored while held
    SharedLedger g;
    g.pending = -300; // a spend in the shared world, just before the guest loaded its own save
    uint32_t seq = g.send();
    g.pending = -20;  // another one, not sent yet
    g.pause();
    CHECK(g.held && !g.take_total(true) && g.held); // still elsewhere: the host's total is ignored
    CHECK(g.take_total(false) && !g.held);          // back: the first total ends the hold
    double shown = g.shown(1000, true, seq - 1);    // the host has not counted the -300 yet
    CHECK(std::fabs(shown - 680) < 1e-9 && std::fabs(g.pending + 20) < 1e-9 && g.in_flight.size() == 1);
}

int main(int argc, char** argv)
{
    g_nofix = argc > 1 && std::string(argv[1]) == "--nofix";
    test_give_pick_leave();
    test_dna_route();
    test_dna_pace();
    test_dna_ledger();
    test_host_world();
    test_chunks();
    test_cycle();
    test_space_origin();
    test_numbers();
    test_players();
    test_give_target();
    if (g_fail) {
        printf("%d FAILED\n", g_fail);
        return 1;
    }
    printf("validate tests OK\n");
    return 0;
}
