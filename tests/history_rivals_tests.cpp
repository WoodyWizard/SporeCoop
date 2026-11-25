// Portable unit tests for BUG-1557: Rivals per-player Tribe/Civ consequence cards,
// Space philosophy recalculation, specialty mappings, and backwards-compatible store persistence.
// Built and run on Linux under ASan + UBSan via `make unit-cpp`.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "history_logic.h"

static int g_fails = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            g_fails++;                                                                   \
        }                                                                                \
    } while (0)

using namespace history;

static const uint32_t kCarn = 0xCFB01B93u;
static const uint32_t kHerb = 0xA8EC6F99u;
static const uint32_t kCreatAggr = 0x17E5EF84u;
static const uint32_t kCreatSoc = 0xFD159D91u;
static const uint32_t kWarrior = 0xD9C806E4u;
static const uint32_t kShaman = 0xCF6C2AB4u;
static const uint32_t kZealot = 0x2B35F523u;
static const uint32_t kDiplomat = 0xB436C0BCu;

static void test_tribe_card_for()
{
    printf("tribe_card_for: Spore ally/conquer ratios\n");
    CHECK(tribe_card_for(0, 0) == 0);
    CHECK(tribe_card_for(1, 0) == kTribeFriendly);
    CHECK(tribe_card_for(5, 0) == kTribeFriendly);
    CHECK(tribe_card_for(2, 1) == kTribeFriendly);
    CHECK(tribe_card_for(4, 2) == kTribeFriendly);
    CHECK(tribe_card_for(0, 1) == kTribeAggressive);
    CHECK(tribe_card_for(0, 4) == kTribeAggressive);
    CHECK(tribe_card_for(1, 2) == kTribeAggressive);
    CHECK(tribe_card_for(2, 4) == kTribeAggressive);
    CHECK(tribe_card_for(1, 1) == kTribeIndustrious);
    CHECK(tribe_card_for(2, 2) == kTribeIndustrious);
    CHECK(tribe_card_for(3, 2) == kTribeIndustrious);
    CHECK(tribe_card_for(2, 3) == kTribeIndustrious);
}

static void test_civ_card_for()
{
    printf("civ_card_for: tallies, start_spec fallback, and tie-breakers\n");
    // No tallies: start_spec decides, else 0
    CHECK(civ_card_for(0, 0, 0, kCivSpecNone) == 0);
    CHECK(civ_card_for(0, 0, 0, 4) == 0);
    CHECK(civ_card_for(0, 0, 0, kCivSpecMilitary) == kCivMilitary);
    CHECK(civ_card_for(0, 0, 0, kCivSpecEconomic) == kCivEconomic);
    CHECK(civ_card_for(0, 0, 0, kCivSpecReligious) == kCivReligious);

    // Clear winner beats start_spec
    CHECK(civ_card_for(3, 1, 0, kCivSpecReligious) == kCivMilitary);
    CHECK(civ_card_for(1, 4, 2, kCivSpecMilitary) == kCivEconomic);
    CHECK(civ_card_for(1, 2, 5, kCivSpecEconomic) == kCivReligious);

    // Ties where start_spec IS tied for max: start_spec wins
    CHECK(civ_card_for(2, 2, 0, kCivSpecEconomic) == kCivEconomic);
    CHECK(civ_card_for(2, 0, 2, kCivSpecReligious) == kCivReligious);
    CHECK(civ_card_for(0, 3, 3, kCivSpecReligious) == kCivReligious);
    CHECK(civ_card_for(2, 2, 2, kCivSpecEconomic) == kCivEconomic);
    CHECK(civ_card_for(2, 2, 2, kCivSpecReligious) == kCivReligious);

    // Ties where start_spec is NOT tied for max (or 0): deterministic mil > eco > rel
    CHECK(civ_card_for(2, 2, 0, kCivSpecReligious) == kCivMilitary);
    CHECK(civ_card_for(2, 2, 0, kCivSpecNone) == kCivMilitary);
    CHECK(civ_card_for(1, 3, 3, kCivSpecMilitary) == kCivEconomic);
    CHECK(civ_card_for(0, 2, 2, kCivSpecNone) == kCivEconomic);
}

static void test_specialty_mappings()
{
    printf("civ_spec_for_tribe_card and native Spore cCity specialty conversions\n");
    CHECK(civ_spec_for_tribe_card(kTribeAggressive) == 0);  // Military
    CHECK(civ_spec_for_tribe_card(kTribeIndustrious) == 1); // Economic
    CHECK(civ_spec_for_tribe_card(kTribeFriendly) == 2);    // Religious
    CHECK(civ_spec_for_tribe_card(0) == -1);
    CHECK(civ_spec_for_tribe_card(kCarn) == -1);

    // Spore native cCity +0x540 / +0x29C: 0=Military, 1=Religious, 2=Economic
    CHECK(native_city_spec_to_history(0) == kCivSpecMilitary);
    CHECK(native_city_spec_to_history(1) == kCivSpecReligious);
    CHECK(native_city_spec_to_history(2) == kCivSpecEconomic);
    CHECK(native_city_spec_to_history(-1) == kCivSpecNone);
    CHECK(native_city_spec_to_history(3) == kCivSpecNone);

    CHECK(history_spec_to_native_city(kCivSpecMilitary) == 0);
    CHECK(history_spec_to_native_city(kCivSpecReligious) == 1);
    CHECK(history_spec_to_native_city(kCivSpecEconomic) == 2);
    CHECK(history_spec_to_native_city(kCivSpecNone) == -1);
    CHECK(history_spec_to_native_city(9) == -1);

    // Round-trip: Tribe Friendly -> civ_spec 2 (Rel) -> history 3 (Rel) -> native 1 (Rel) -> history 3 (Rel)
    int pref = civ_spec_for_tribe_card(kTribeFriendly);
    int native = history_spec_to_native_city((uint8_t)(pref + 1));
    CHECK(native == 1 && native_city_spec_to_history(native) == kCivSpecReligious);

    pref = civ_spec_for_tribe_card(kTribeIndustrious);
    native = history_spec_to_native_city((uint8_t)(pref + 1));
    CHECK(native == 2 && native_city_spec_to_history(native) == kCivSpecEconomic);

    pref = civ_spec_for_tribe_card(kTribeAggressive);
    native = history_spec_to_native_city((uint8_t)(pref + 1));
    CHECK(native == 0 && native_city_spec_to_history(native) == kCivSpecMilitary);
}

static void test_record_tallies_and_update()
{
    printf("update(): own_tribe/own_civ on vs off, Space archetype recalculation\n");
    const uint32_t host_space[kStages] = {kCarn, kCreatAggr, kTribeAggressive, kCivMilitary, kWarrior};
    Record r = seed(host_space, 0.f, 0.f);
    CHECK(!r.has_tribe_own() && !r.has_civ_own() && !r.has_rivals_ext());

    // Record divergent guest Tribe (Friendly) and Civ (Religious) outcomes
    CHECK(add_tribe_outcome(r, true));
    CHECK(add_tribe_outcome(r, true));
    CHECK(set_civ_start(r, kCivSpecReligious));
    CHECK(!set_civ_start(r, kCivSpecNone) && !set_civ_start(r, 9));
    CHECK(add_civ_outcome(r, kCivSpecReligious));
    CHECK(add_civ_outcome(r, kCivSpecReligious));
    CHECK(!add_civ_outcome(r, 0) && !add_civ_outcome(r, 4));
    CHECK(r.has_tribe_own() && r.has_civ_own() && r.has_rivals_ext());

    auto decide_fn = [](int st, const Record& x) -> uint32_t {
        if (st == kTribe)
            return tribe_card_for(x.tribe_allied, x.tribe_conquered);
        if (st == kCiv)
            return civ_card_for(x.civ_military, x.civ_economic, x.civ_religious, x.civ_start_spec);
        if (st == kSpace) {
            if (x.cards[kTribe] == kTribeFriendly && x.cards[kCiv] == kCivReligious)
                return kZealot;
            if (x.cards[kTribe] == kTribeIndustrious && x.cards[kCiv] == kCivEconomic)
                return kDiplomat;
            return kWarrior;
        }
        return 0;
    };

    // With own_tribe=false, own_civ=false (SPORE_FIX_OFF=rivals.own_history / Together): cards stay host's
    unsigned ch_off = update(r, host_space, decide_fn, false, false);
    CHECK(ch_off == 0);
    CHECK(r.cards[kTribe] == kTribeAggressive && r.cards[kCiv] == kCivMilitary && r.cards[kSpace] == kWarrior);

    // With own_tribe=true, own_civ=true (BUG-1557 ON): Tribe, Civ AND Space all diverge from host
    unsigned ch_on = update(r, host_space, decide_fn, true, true);
    CHECK(ch_on == ((1u << kTribe) | (1u << kCiv) | (1u << kSpace)));
    CHECK(r.cards[kTribe] == kTribeFriendly);
    CHECK(r.cards[kCiv] == kCivReligious);
    CHECK(r.cards[kSpace] == kZealot);

    // Second pass with no new tallies: no changes
    CHECK(update(r, host_space, decide_fn, true, true) == 0);

    // Further outcomes shift guest's Tribe to Industrious and Civ to Economic -> Space recomputes again
    add_tribe_outcome(r, false);
    add_tribe_outcome(r, false); // 2 allied, 2 conquered -> Industrious
    add_civ_outcome(r, kCivSpecEconomic);
    add_civ_outcome(r, kCivSpecEconomic);
    add_civ_outcome(r, kCivSpecEconomic); // eco=3 > rel=2 -> Economic
    unsigned ch_shift = update(r, host_space, decide_fn, true, true);
    CHECK(ch_shift == ((1u << kTribe) | (1u << kCiv) | (1u << kSpace)));
    CHECK(r.cards[kTribe] == kTribeIndustrious);
    CHECK(r.cards[kCiv] == kCivEconomic);
    CHECK(r.cards[kSpace] == kDiplomat);

    // Stage not yet completed on host: guest's Tribe/Civ cards stay 0 until host finishes stage
    const uint32_t host_in_tribe[kStages] = {kCarn, kCreatAggr, 0, 0, 0};
    Record rt = seed(host_in_tribe, 0.f, 0.f);
    add_tribe_outcome(rt, true);
    CHECK(update(rt, host_in_tribe, decide_fn, true, true) == 0);
    CHECK(rt.cards[kTribe] == 0 && rt.cards[kCiv] == 0 && rt.cards[kSpace] == 0);

    const uint32_t host_done_tribe[kStages] = {kCarn, kCreatAggr, kTribeAggressive, 0, 0};
    unsigned ch_t = update(rt, host_done_tribe, decide_fn, true, true);
    CHECK(ch_t == (1u << kTribe) && rt.cards[kTribe] == kTribeFriendly);

    // Empty tallies with own_tribe=true: falls back to host's card if guest has no card yet
    Record empty_g = seed(host_in_tribe, 0.f, 0.f);
    CHECK(update(empty_g, host_done_tribe, decide_fn, true, true) == (1u << kTribe));
    CHECK(empty_g.cards[kTribe] == kTribeAggressive);

    // Flipping own_tribe/own_civ back OFF reverts Tribe and Civ to host's
    unsigned ch_revert = update(r, host_space, decide_fn, false, false);
    CHECK((ch_revert & (1u << kTribe)) != 0 && (ch_revert & (1u << kCiv)) != 0);
    CHECK(r.cards[kTribe] == kTribeAggressive && r.cards[kCiv] == kCivMilitary);
}

static void test_codec_and_store_compat()
{
    printf("encode/decode: 32-byte v1 legacy + 43-byte extended Rivals payload\n");
    // Legacy v1 (no Rivals tallies): exact 32 bytes
    Record v1;
    v1.cards[kCell] = kHerb;
    v1.cards[kCreature] = kCreatSoc;
    v1.cards[kTribe] = kTribeFriendly;
    v1.cards[kCiv] = kCivReligious;
    v1.cards[kSpace] = kShaman;
    v1.social = 123.5f;
    v1.combat = 12.25f;
    v1.cell_own = kHerb;

    auto b32 = encode(v1);
    CHECK(b32.size() == 32);
    Record dec1;
    CHECK(decode(b32, dec1) && dec1 == v1);

    // Extended v1 (with Rivals Tribe/Civ tallies): exact 43 bytes
    Record ext = v1;
    ext.tribe_allied = 3;
    ext.tribe_conquered = 1;
    ext.civ_military = 1;
    ext.civ_economic = 2;
    ext.civ_religious = 4;
    ext.civ_start_spec = kCivSpecReligious;
    auto b43 = encode(ext);
    CHECK(b43.size() == 43);
    Record dec2;
    CHECK(decode(b43, dec2) && dec2 == ext);

    // Reject truncated, overlong, or invalid civ_start_spec payloads
    auto bad_short = b43;
    bad_short.pop_back();
    CHECK(bad_short.size() == 42 && !decode(bad_short, dec2));

    auto bad_33 = b32;
    bad_33.push_back(0);
    CHECK(bad_33.size() == 33 && !decode(bad_33, dec2));

    auto bad_long = b43;
    bad_long.push_back(0);
    CHECK(bad_long.size() == 44 && !decode(bad_long, dec2));

    auto bad_spec = b43;
    bad_spec.back() = 4; // invalid civ_start_spec > 3
    CHECK(!decode(bad_spec, dec2));

    // Book::load handles mixed legacy (32B) and extended (43B) records in the same world store
    pstore::Store st;
    st.reset("game1/rivals.spo");
    st.put("s:0000000000000001", "Legacy", kStoreFeature, kStoreVersion, b32, 100);
    st.put("s:0000000000000002", "Rival", kStoreFeature, kStoreVersion, b43, 100);
    Book book;
    CHECK(book.load(st, "game1/rivals.spo") == 2);
    CHECK(book.recs["s:0000000000000001"] == v1);
    CHECK(book.recs["s:0000000000000002"] == ext);
}

int main()
{
    test_tribe_card_for();
    test_civ_card_for();
    test_specialty_mappings();
    test_record_tallies_and_update();
    test_codec_and_store_compat();
    if (g_fails) {
        fprintf(stderr, "history_rivals_tests: %d FAILED\n", g_fails);
        return 1;
    }
    printf("history_rivals_tests: ALL PASSED\n");
    return 0;
}
