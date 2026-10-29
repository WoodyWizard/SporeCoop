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

int main(int argc, char** argv)
{
    g_nofix = argc > 1 && std::string(argv[1]) == "--nofix";
    test_give_pick_leave();
    test_dna_route();
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
