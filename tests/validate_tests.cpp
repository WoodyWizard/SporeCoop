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
#include "settings_core.h"
#include "find_core.h"
#include "net/shared_ledger.h"
#include "net/held_cmds.h"
#include "net/sreport.h"
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

// BUG-0693: a guest's command held while the host's stage is away runs once, in order, in the same world only
static void test_held_cmds()
{
    HeldCommands h;
    const uint8_t a[] = {2, 7}, b[] = {3, 9, 9};
    h.push(1, a, sizeof a, 5, 10.0);
    h.push(2, b, sizeof b, 5, 11.0);
    std::vector<uint8_t> ran;
    h.drain(5, 20.0, [&](const HeldCommands::Cmd& c) { ran.push_back(c.bytes[0]); });
    CHECK(ran.size() == 2 && ran[0] == 2 && ran[1] == 3 && h.q.empty() && h.run == 2);
    h.push(1, a, sizeof a, 5, 0.0);   // another world when back: dropped
    h.push(1, a, sizeof a, 6, 0.0);   // too old: dropped
    h.push(3, b, sizeof b, 6, 100.0); // the same world, in time: runs
    ran.clear();
    h.drain(6, 150.0, [&](const HeldCommands::Cmd& c) { ran.push_back(c.from); });
    CHECK(ran.size() == 1 && ran[0] == 3 && h.dropped == 2);
    for (int i = 0; i < 40; i++)
        h.push(1, a, sizeof a, 1, 0.0);
    CHECK(h.q.size() == HeldCommands::kMax && h.dropped == 2 + 8);
    h.forget(1);
    CHECK(h.q.empty());
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

static bool g_nofix = false;

static void test_cycle()
{
    printf("snapshot chunk numbers\n");
    CHECK(cycle_chunk_ok(0, 1));
    CHECK(cycle_chunk_ok(2, 3));
    CHECK(!cycle_chunk_ok(0, 0));
    CHECK(!cycle_chunk_ok(3, 3));
    CHECK(!cycle_chunk_ok(255, 1));
    // BUG-0612 (net.cycle_order): a late chunk of an older cycle is dropped, the newer cycle completes
    CHECK(cycle_order(5, 4, true) == Cycle::Newer);
    CHECK(cycle_order(3, 4, true) == Cycle::Older);
    CHECK(cycle_order(4, 4, true) == Cycle::Same);
    CHECK(cycle_order(0, 0xFFFF, true) == Cycle::Newer); // the counter wraps
    CHECK(cycle_order(0xFFFF, 0, true) == Cycle::Older);
    CHECK(cycle_order(9, 0, false) == Cycle::Newer);
    const bool fix = !g_nofix;
    CycleTracker t;
    bool started = false;
    CHECK(t.begin(5, fix, &started) && started);
    t.add(0);
    CHECK(t.begin(5, fix, &started) && !started);
    t.add(1);
    bool late = t.begin(4, fix); // cycle 4's last chunk, late (behind cycle 5's first two)
    if (late)
        t.add(2);
    CHECK(!late);
    if (t.begin(5, fix))
        t.add(2);
    CHECK(t.complete(3));
    CHECK(t.older == (fix ? 1u : 0u));
    t.reset();
    CHECK(!t.have && t.count() == 0);
    t.begin(1, fix);
    t.add(200);
    CHECK(t.count() == 1 && !t.complete(2));
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
    // BUG-0420: every message type has a class (an unlisted MsgType fails the build: -Werror=switch in host_world.h);
    // the host's world state is dropped while elsewhere, a type this build does not know too (default deny)
    // BUG-0709: another Space save than the host's (both known); unknown or another mode: not held
    CHECK(host_world::space_other(true, space, 5, true, space, 6));
    CHECK(!host_world::space_other(true, space, 5, true, space, 5) && !host_world::space_other(true, space, 0, true, space, 6));
    CHECK(!host_world::space_other(true, space, 5, true, space, 0) && !host_world::space_other(true, space, 5, true, cr, 6));
    CHECK(!host_world::space_other(false, space, 5, true, space, 6) && !host_world::space_other(true, cr, 5, true, space, 6));
    CHECK(host_world::welcome_world(space, 7, 9) == 9 && host_world::welcome_world(cr, 7, 9) == 7);
    using host_world::MsgClass;
    using host_world::msg_class;
    using host_world::world_state_msg;
    CHECK(msg_class(net::MSG_CHAT) == MsgClass::Session && msg_class(net::MSG_VOTE) == MsgClass::Session);
    CHECK(msg_class(net::MSG_SPECIES) == MsgClass::HostWorld && msg_class(net::MSG_WSTATE) == MsgClass::HostWorld);
    CHECK(msg_class(net::MSG_NPC_STATE) == MsgClass::Module && msg_class(net::MSG_UFO_HIT) == MsgClass::Module);
    CHECK(msg_class(0) == MsgClass::Unknown && msg_class(200) == MsgClass::Unknown && msg_class(44 + 100) == MsgClass::Unknown);
    CHECK(world_state_msg(net::MSG_DNA, true, true) && world_state_msg(net::MSG_DNA, false, false));
    CHECK(world_state_msg(net::MSG_CIV_STATE, true, true) && !world_state_msg(net::MSG_CIV_STATE, true, false));
    CHECK(world_state_msg(250, true, true) && !world_state_msg(250, false, true)); // unknown: default deny when on
    CHECK(!world_state_msg(net::MSG_VOTE, true, true) && !world_state_msg(net::MSG_NPC_STATE, true, true));
    for (int t = 0; t < 256; t++) { // the old hand list of 0.7.10 is a subset of HostWorld
        bool old = t == net::MSG_DNA || t == net::MSG_WORLD_TIME || (t >= net::MSG_CELL_STATE && t <= net::MSG_CELL_FOOD) ||
                   t == net::MSG_CELL_KO || t == net::MSG_CELL_BITTEN || t == net::MSG_TRIBE_STATE || t == net::MSG_TRIBE_CMD ||
                   t == net::MSG_SPECIES || t == net::MSG_SOCIAL || t == net::MSG_NEST || t == net::MSG_PARTS ||
                   t == net::MSG_FIELDS || t == net::MSG_WSTATE || t == net::MSG_PLAYER_RULE || t == net::MSG_MIRROR_FX;
        CHECK(!old || msg_class((uint8_t)t) == MsgClass::HostWorld);
    }

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

// F3 of 0.7.12: the session settings (settings_core.h): who may set one, the wire and the file formats, a world's open
static void test_settings()
{
    using namespace settings;
    CHECK(host_world::msg_class(net::MSG_SETTINGS) == host_world::MsgClass::Session);
    for (int i = 0; i < kCount; i++) { // declared once, unique names, defaults in range
        CHECK(find(kDefs[i].key) == i && kDefs[i].label && kDefs[i].help && kDefs[i].group);
        CHECK(kDefs[i].def >= kDefs[i].min && kDefs[i].def <= kDefs[i].max);
    }
    CHECK(kDefs[kCellRegen].scope == Scope::Live && kDefs[kCellRegen].def == 1);
    CHECK(kDefs[kPartnerArrows].scope == Scope::Live && kDefs[kPartnerArrows].off_in_rivals);
    CHECK(std::string(kDefs[kDnaPace].ini) == "dna_pace" && std::string(kDefs[kNpcTargets].ini) == "npcs_target_partners");
    // who sets: a guest never; a session setting only before the start; a live one any time
    CHECK(can_set(kCellRegen, false, false) == Refusal::Guest && can_set(kDnaPace, false, true) == Refusal::Guest);
    CHECK(can_set(kDnaPace, true, true) == Refusal::SessionStarted && can_set(kDnaPace, true, false) == Refusal::None);
    CHECK(can_set(kCellRegen, true, true) == Refusal::None);
    int v = -1;
    CHECK(parse_value(kCellRegen, "off", v) && v == 0 && parse_value(kCellRegen, "default", v) && v == 1);
    CHECK(!parse_value(kCellRegen, "2", v) && !parse_value(kCellRegen, "x", v) && !parse_value(kCellRegen, "", v));
    // the wire: the whole table round trips; a key this build does not know is skipped; a cut message applies nothing
    Values a;
    a.v[kCellRegen] = 0;
    a.v[kDnaPace] = 0;
    net::Writer w;
    write_msg(w, a, true);
    {
        net::Reader r(w.data.data(), w.size());
        Values got;
        bool sent[kCount], st = false;
        CHECK(read_msg(r, got, sent, st) && got == a && st);
        for (bool x : sent)
            CHECK(x);
    }
    {
        net::Writer n; // a newer host: one setting more, one of ours missing
        n.u8(0); n.u8(0); n.u8(2);
        n.str8("all.future_thing"); n.u32(7);
        n.str8("cell.regen"); n.u32(0);
        net::Reader r(n.data.data(), n.size());
        Values got;
        bool sent[kCount], st = true;
        CHECK(read_msg(r, got, sent, st) && got.v[kCellRegen] == 0 && sent[kCellRegen] && !sent[kDnaPace] && !st);
        CHECK(got.v[kDnaPace] == 1); // untouched
    }
    for (size_t cut = 0; cut < w.size(); cut++) {
        net::Reader r(w.data.data(), cut);
        Values got;
        bool sent[kCount], st = false;
        CHECK(!read_msg(r, got, sent, st) && got == Values());
    }
    {
        net::Writer k; // another kind: not applied
        k.u8(1); k.u8(0); k.u8(0);
        net::Reader r(k.data.data(), k.size());
        Values got;
        bool sent[kCount], st = false;
        CHECK(!read_msg(r, got, sent, st));
    }
    // the file: round trip; another world's file (a crc collision) is not this world's; bad lines skipped
    Values d, out;
    std::string f = format_file("planet.spo", a);
    CHECK(parse_file(f, "planet.spo", d, out) && out == a);
    CHECK(!parse_file(f, "other.spo", d, out) && out == d);
    CHECK(parse_file("world planet.spo\r\ncell.regen 0\r\nnope 1\r\nall.dna_pace 9\r\n", "planet.spo", d, out) &&
          out.v[kCellRegen] == 0 && out.v[kDnaPace] == 1);
    // a world opens: the lobby's choice wins, else the stored values, else the defaults (coop.ini)
    Values cur = a, stored, defs;
    stored.v[kPackRescue] = 0;
    CHECK(on_world_open(true, true, stored, defs, cur) == Opened::Pending && cur == a);
    CHECK(on_world_open(false, true, stored, defs, cur) == Opened::Stored && cur == stored);
    CHECK(on_world_open(false, false, stored, defs, cur) == Opened::Defaults && cur == defs);
    CHECK(diff_text(defs, Values()).empty() && diff_text(stored, Values()) == "creature.pack_rescue=0");
}

// F2 of 0.7.12 (BUG-0764): the player colours, the arrow at the screen edge
static void test_find()
{
    using namespace findx;
    CHECK(host_world::msg_class(net::MSG_COLORS) == host_world::MsgClass::Session);
    CHECK(settings::kDefs[settings::kPartnerMap].scope == settings::Scope::Live && settings::kDefs[settings::kPartnerMap].def == 1);
    // the palette: distinct colours, names found
    for (int i = 0; i < kColors; i++) {
        CHECK(color_find(kPalette[i].key) == i);
        for (int j = 0; j < i; j++)
            CHECK(kPalette[i].argb != kPalette[j].argb);
    }
    CHECK(color_find("3") == 3 && color_find("8") == -1 && color_find("x") == -1);
    // four players get the first four colours, distinct; a leaver's colour goes to the next newcomer
    Colors t;
    CHECK(auto_pick(t, 0) == 0 && auto_pick(t, 1) == 1 && auto_pick(t, 2) == 2 && auto_pick(t, 3) == 3);
    CHECK(auto_pick(t, 1) == 1); // keeps its own
    t.c[2] = -1;
    CHECK(auto_pick(t, 5) == 2);
    // a pick: not another player's
    CHECK(can_take(t, 0, 1) == Refused::Taken && can_take(t, 0, 0) == Refused::None && can_take(t, 0, 6) == Refused::None);
    CHECK(can_take(t, 0, 8) == Refused::Range && can_take(t, 0, -1) == Refused::Range);
    // the wire: the table round trips; a cut one changes nothing
    net::Writer w;
    write_table(w, t);
    {
        net::Reader r(w.data.data(), w.size());
        Colors got;
        CHECK(r.u8() == 0 && read_table(r, got) && got == t);
    }
    {
        net::Reader r(w.data.data(), w.size() - 1);
        Colors got;
        got.c[9] = 4;
        r.u8();
        CHECK(!read_table(r, got) && got.c[9] == 4);
    }
    // the arrow: a partner to the right, outside the view: on the right edge, pointing right
    Arrow a;
    CHECK(arrow_place(true, false, 3000, 540, 1920, 1080, 46, 46, a) && !a.on_screen);
    CHECK(std::fabs(a.x - (1920 - 46)) < 0.5f && std::fabs(a.y - 540) < 0.5f && std::fabs(a.ang) < 1e-4f);
    // up-left: on the ray from the centre
    CHECK(arrow_place(true, false, -1000, -2000, 1920, 1080, 46, 46, a) && !a.on_screen);
    CHECK(angle_diff_deg(a.ang, std::atan2(-2000.f - 540, -1000.f - 960)) < 0.1f && std::fabs(a.y - 46) < 0.5f);
    // in the view: no arrow; behind with no direction: at the bottom
    CHECK(arrow_place(true, true, 500, 500, 1920, 1080, 46, 46, a) && a.on_screen);
    CHECK(arrow_place(false, false, 0, 0, 1920, 1080, 46, 46, a) && !a.on_screen && std::fabs(a.y - (1080 - 46)) < 0.5f);
    CHECK(!arrow_place(true, false, NAN, 1, 1920, 1080, 46, 46, a));
    float v[12];
    CHECK(arrow_tris(a, 22, 0, v) == 2 && std::fabs(v[0] - a.x) < 1e-3f && std::fabs(v[1] - a.y) < 1e-3f);
    CHECK(dist_text(80.4f) == "80 m" && dist_text(1520) == "1.5 km");
    CHECK(angle_diff_deg(3.1f, -3.1f) < 5.f);
}

// BUG-0767 (F4 of 0.7.12): the session report: the content check of a received report, the host's round, the zip
static std::vector<uint8_t> zip_of(const std::vector<std::pair<std::string, std::string>>& files)
{
    std::vector<uint8_t> z;
    sreport::ZipOut o;
    o.out = [&z](const uint8_t* p, size_t n) {
        z.insert(z.end(), p, p + n);
        return true;
    };
    for (auto& [n, d] : files)
        o.add(n, (const uint8_t*)d.data(), d.size());
    o.finish();
    return z;
}

static void test_sreport()
{
    using namespace sreport;
    // names of a SporeKit report archive pass; folders, "..", drive letters, backslashes, programs do not
    for (const char* ok : {"README.txt", "LEFT_OUT.txt", "report-20261108-101010-i21.txt", "crash-20261108-1.txt",
                           "logs/sporekit.log", "logs/sporekit.21.log", "previous/sporekit-20261107.log",
                           "crashes/crash-1.dmp", "crashes/crash-1.txt", "reports/report-1.txt", "shots/report-1.jpg",
                           "settings/coop.ini"})
        CHECK(allowed_name(ok));
    for (const char* bad : {"../evil.bat", "logs/../evil.log", "C:/x.txt", "C:\\x.txt", "logs\\sporekit.log",
                            "logs/a/b.log", "evil.exe", "logs/evil.exe", "shots/x.png", "/README.txt", "",
                            ".hidden.txt", "settings/.ini", "report-1.txt ", "other/x.txt", "README.txt/x.log",
                            "logs/sp\xC3\xA4.log", "settings/a:b.ini"})
        CHECK(!allowed_name(bad));
    // a real-shaped report passes the check; its names come back
    auto good = zip_of({{"README.txt", "SporeCoop problem report"}, {"report-1.txt", "== summary"},
                        {"logs/sporekit.log", std::string(5000, 'x')}, {"settings/coop.ini", "[coop]\nname=Bob\n"}});
    std::vector<std::string> names;
    std::string why;
    CHECK(check_zip(good.data(), good.size(), &why, &names) && names.size() == 4);
    // a name that climbs out, a second folder level, a program: refused
    auto bad = zip_of({{"README.txt", "x"}, {"../../evil.bat", "@echo off"}});
    CHECK(!check_zip(bad.data(), bad.size(), &why));
    auto bad2 = zip_of({{"logs/a/b.log", "x"}});
    CHECK(!check_zip(bad2.data(), bad2.size(), &why));
    // junk, a cut zip, a flipped byte in a stored entry, an entry twice, an empty archive
    std::vector<uint8_t> junk(4096);
    for (size_t i = 0; i < junk.size(); i++)
        junk[i] = (uint8_t)(i * 37 + 11);
    CHECK(!check_zip(junk.data(), junk.size(), &why));
    CHECK(!check_zip(good.data(), good.size() - 1, &why));
    for (size_t cut = 0; cut < good.size(); cut += 97) // every prefix: refused, no read outside it (ASan)
        CHECK(!check_zip(good.data(), cut, &why));
    auto flip = good;
    flip[60] ^= 0x20; // inside README.txt's data
    CHECK(!check_zip(flip.data(), flip.size(), &why));
    auto twice = zip_of({{"README.txt", "a"}, {"README.txt", "b"}});
    CHECK(!check_zip(twice.data(), twice.size(), &why));
    auto empty = zip_of({});
    CHECK(!check_zip(empty.data(), empty.size(), &why));
    // the entry names of the session zip: plain, unique
    std::set<std::string> taken;
    CHECK(entry_name("Host-", "Alice", 0, taken) == "Host-Alice.zip");
    CHECK(entry_name("Guest-", "Bob", 1, taken) == "Guest-Bob.zip");
    CHECK(entry_name("Guest-", "Bob", 2, taken) == "Guest-Bob-2.zip");
    CHECK(entry_name("Guest-", "../C:\\x y", 3, taken) == "Guest-C_x_y.zip");
    CHECK(entry_name("Guest-", "\xD0\x91\xD0\xBE\xD0\xB1", 4, taken) == "Guest-player4.zip");

    // a round: Bob sends, Carol says no, Dave announces too much, Eve overruns, Finn sends junk, Gus never answers
    Round r;
    r.id = 77;
    r.t0 = 0;
    const char* nm[] = {"Bob", "Carol", "Dave", "Eve", "Finn", "Gus", "Hal"};
    for (uint8_t i = 0; i < 7; i++) {
        Guest g;
        g.id = (uint8_t)(i + 1);
        g.name = nm[i];
        r.guests.push_back(g);
    }
    auto send_all = [&](uint8_t id, const std::vector<uint8_t>& d, uint32_t announce) {
        Answer a{77, 1, announce};
        Why w = r.on_answer(id, a, 1);
        for (size_t off = 0; off < d.size() && w == Why::None; off += kChunkBytes) {
            Piece p;
            p.round = 77;
            p.offset = (uint32_t)off;
            p.n = (uint16_t)std::min(kChunkBytes, d.size() - off);
            p.data = d.data() + off;
            w = r.on_piece(id, p, 2);
        }
        if (w == Why::None)
            w = r.on_done(id, Done{77, crc32(d.data(), d.size())}, 3);
        return w;
    };
    CHECK(send_all(1, good, (uint32_t)good.size()) == Why::None && r.guests[0].st == State::Sent &&
          r.guests[0].data == good);
    CHECK(r.on_answer(2, Answer{77, 0, 0}, 1) == Why::None && r.guests[1].st == State::NotSent);
    CHECK(r.on_answer(3, Answer{77, 1, kMaxReport + 1}, 1) == Why::TooBig && r.guests[2].st == State::Refused);
    auto over = good;
    over.resize(good.size() + 5000, 0x77);
    CHECK(send_all(4, over, (uint32_t)good.size()) == Why::TooBig && r.guests[3].st == State::Refused &&
          r.guests[3].data.empty()); // stopped at the announced size, nothing kept
    CHECK(send_all(5, junk, (uint32_t)junk.size()) == Why::Malformed && r.guests[4].st == State::Refused);
    // a piece out of order (Hal)
    CHECK(r.on_answer(7, Answer{77, 1, 3000}, 1) == Why::None);
    Piece p;
    p.round = 77;
    p.offset = 1000;
    p.n = 10;
    p.data = good.data();
    CHECK(r.on_piece(7, p, 2) == Why::Order && r.guests[6].st == State::Refused);
    // messages of another round or after the end: ignored
    CHECK(r.on_answer(2, Answer{78, 1, 100}, 1) == Why::None && r.guests[1].st == State::NotSent);
    CHECK(!r.finished());
    r.tick(kAnswerWait - 1);
    CHECK(r.guests[5].st == State::Asked);
    r.tick(kAnswerWait + 1);
    CHECK(r.guests[5].st == State::TimedOut && r.finished());
    CHECK(r.count(State::Sent) == 1 && r.count(State::Refused) == 4 && r.count(State::NotSent) == 1);
    CHECK(r.line_of(r.guests[2]) == "Dave: refused (too big)" && r.line_of(r.guests[1]) == "Carol: not sent");
    // a guest that leaves mid-transfer
    Round r2;
    r2.id = 5;
    Guest g;
    g.id = 1;
    g.name = "Bob";
    r2.guests.push_back(g);
    r2.on_answer(1, Answer{5, 1, 3000}, 1);
    r2.on_leave(1);
    CHECK(r2.guests[0].st == State::Left && r2.finished());
    // a stalled transfer times out
    Round r3;
    r3.id = 6;
    r3.guests.push_back(g);
    r3.on_answer(1, Answer{6, 1, 3000}, 10);
    r3.tick(10 + kStallWait - 1);
    CHECK(r3.guests[0].st == State::Receiving);
    r3.tick(10 + kStallWait + 1);
    CHECK(r3.guests[0].st == State::TimedOut);
    // the session zip is itself a valid zip with the reports stored as received
    auto sess = zip_of({{"Host-Alice.zip", std::string(good.begin(), good.end())},
                        {"Guest-Bob.zip", std::string(good.begin(), good.end())}, {"SESSION.txt", "x"}});
    size_t at = 0;
    for (size_t i = 0; i + good.size() <= sess.size(); i++)
        if (!memcmp(sess.data() + i, good.data(), good.size())) {
            at++;
            i += good.size() - 1;
        }
    CHECK(at == 2);
    // the messages read back; a piece longer than one packet or past its message does not
    net::Writer w;
    Piece q;
    q.round = 9;
    q.offset = 1000;
    q.n = 3;
    const uint8_t b3[3] = {1, 2, 3};
    q.data = b3;
    q.write(w);
    net::Reader rd(w.data.data(), w.data.size());
    Piece q2;
    CHECK(rd.u8() == kChunk && q2.read(rd) && q2.n == 3 && q2.own[2] == 3 && q2.offset == 1000);
    net::Reader rd2(w.data.data(), w.data.size() - 1);
    rd2.u8();
    Piece q3;
    CHECK(!q3.read(rd2));
}

int main(int argc, char** argv)
{
    g_nofix = argc > 1 && std::string(argv[1]) == "--nofix";
    test_give_pick_leave();
    test_dna_route();
    test_dna_pace();
    test_dna_ledger();
    test_host_world();
    test_settings();
    test_find();
    test_held_cmds();
    test_chunks();
    test_cycle();
    test_space_origin();
    test_numbers();
    test_players();
    test_give_target();
    test_sreport();
    if (g_fail) {
        printf("%d FAILED\n", g_fail);
        return 1;
    }
    printf("validate tests OK\n");
    return 0;
}
