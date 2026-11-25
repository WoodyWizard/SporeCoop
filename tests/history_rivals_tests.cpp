// Portable unit tests for BUG-1557: Rivals per-player Tribe/Civ consequence cards,
// Space philosophy recalculation, specialty mappings, and backwards-compatible store persistence.
// Built and run on Linux under ASan + UBSan via `make unit-cpp`.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "history_logic.h"
#include "wire/history.h"

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

static void test_midgame_philosophy_presets()
{
    printf("midgame_pick: all 10 philosophy presets, parsers, labels, and card cycler\n");
    std::set<std::string> keys;
    std::set<uint32_t> space_cards;
    for (int i = 0; i < kPhilosophyCount; i++) {
        const auto& p = kPhilosophies[i];
        CHECK(p.key && p.label_en && p.label_ru);
        CHECK(keys.insert(p.key).second);
        CHECK(valid_card(kSpace, p.space_card));
        CHECK(space_cards.insert(p.space_card).second);
        CHECK(archetype_index(p.space_card) >= 9 && archetype_index(p.space_card) <= 18);
        CHECK(find_philosophy(p.key) == &p);
        CHECK(find_philosophy_by_card(p.space_card) == &p);
        CHECK(philosophy_index_of(p.space_card) == i);
        if (std::string(p.key) == "wanderer") {
            for (int s = 0; s < 4; s++)
                CHECK(p.cards[s] == 0);
        } else {
            for (int s = 0; s < 4; s++)
                CHECK(valid_card(s, p.cards[s]));
        }
    }
    CHECK(keys.size() == 10 && space_cards.size() == 10);
    // Case-insensitive lookup and unknown rejection
    CHECK(find_philosophy("ZEALOT") && find_philosophy("ZEALOT")->space_card == kSpaceZealot);
    CHECK(find_philosophy("Scientist") && find_philosophy("Scientist")->space_card == kSpaceScientist);
    CHECK(find_philosophy("unknown") == nullptr);
    CHECK(find_philosophy_by_card(0) == nullptr && philosophy_index_of(0) == -1);

    // Stage/card parsers and labels
    CHECK(parse_stage_name("cell") == kCell && parse_stage_name("Creature") == kCreature);
    CHECK(parse_stage_name("tribe") == kTribe && parse_stage_name("civ") == kCiv);
    CHECK(parse_stage_name("space") == -1 && parse_stage_name("foo") == -1);
    CHECK(parse_card_name(kCell, "herbivore") == kCellHerbivore && parse_card_name(kCell, "omni") == kCellOmnivore);
    CHECK(parse_card_name(kCreature, "predator") == kCreatureAggressive && parse_card_name(kCreature, "social") == kCreatureSocial);
    CHECK(parse_card_name(kTribe, "friendly") == kTribeFriendly && parse_card_name(kTribe, "industrious") == kTribeIndustrious);
    CHECK(parse_card_name(kCiv, "religious") == kCivReligious && parse_card_name(kCiv, "eco") == kCivEconomic);
    CHECK(parse_card_name(kCiv, "bogus") == 0);
    CHECK(std::string(card_label(kSpace, kSpaceZealot, false)) == "Zealot");
    CHECK(std::string(card_label(kCell, 0, false)) == "None");

    // next_stage_card cycles through all 3 valid cards of each stage 0..3
    for (int st = 0; st < 4; st++) {
        uint32_t c0 = next_stage_card(st, 0);
        uint32_t c1 = next_stage_card(st, c0);
        uint32_t c2 = next_stage_card(st, c1);
        uint32_t c3 = next_stage_card(st, c2);
        CHECK(valid_card(st, c0) && valid_card(st, c1) && valid_card(st, c2));
        CHECK(c0 != c1 && c1 != c2 && c0 != c2 && c3 == c0);
    }
    CHECK(next_stage_card(kSpace, kSpaceWarrior) == 0);
}

static void test_midgame_pick_rules_and_update()
{
    printf("apply_midgame_pick & update(): validation, anti-forge, Space recompute, Wanderer, reload\n");
    const uint32_t host_space[kStages] = {kCellCarnivore, kCreatureAggressive, kTribeAggressive, kCivMilitary, kSpaceWarrior};
    Record r = seed(host_space, 0.f, 0.f);
    CHECK(!r.has_picked_custom() && !r.has_ext());

    // Invalid card ID rejects the entire pick without mutating r
    const uint32_t bad_pick[4] = {kCellHerbivore, 0xDEADBEEFu, kTribeFriendly, kCivReligious};
    bool acc = true;
    CHECK(apply_midgame_pick(r, host_space, bad_pick, kSpace, &acc) == 0 && !acc);
    CHECK(r.cards[kCell] == kCellCarnivore && r.picked_mask == 0);

    // Pick Zealot philosophy in Space: updates Tribe/Civ and clears Space for recomputation
    const PhilosophyPreset* zealot = find_philosophy("zealot");
    CHECK(zealot != nullptr);
    unsigned ch = apply_midgame_pick(r, host_space, zealot->cards, kSpace, &acc);
    CHECK(acc && ch == ((1u << kTribe) | (1u << kCiv) | (1u << kSpace)));
    CHECK(r.cards[kCell] == kCellCarnivore && r.cards[kCreature] == kCreatureAggressive);
    CHECK(r.cards[kTribe] == kTribeFriendly && r.cards[kCiv] == kCivReligious);
    CHECK(r.cards[kSpace] == 0 && r.picked_mask == 0x0Fu && r.has_picked_custom());

    auto decide_space = [](int st, const Record& x) -> uint32_t {
        if (st == kSpace) {
            for (const auto& p : kPhilosophies)
                if (memcmp(x.cards, p.cards, sizeof p.cards) == 0)
                    return p.space_card;
            return kSpaceWarrior;
        }
        return 0;
    };

    // update() in Together mode (own_tribe=false, own_civ=false) MUST preserve guest's picked cards and compute Zealot!
    unsigned ch_u1 = update(r, host_space, decide_space, false, false, kSpace, true);
    CHECK(ch_u1 == (1u << kSpace) && r.cards[kSpace] == kSpaceZealot);
    CHECK(r.cards[kTribe] == kTribeFriendly && r.cards[kCiv] == kCivReligious);
    // Steady state: no further changes on repeat ticks
    CHECK(update(r, host_space, decide_space, false, false, kSpace, true) == 0);
    CHECK(update(r, host_space, decide_space, true, true, kSpace, true) == 0);

    // Pick Wanderer (all zero) in Space: clears cards[0..3] to 0, recomputes kSpaceWanderer, stays protected
    const PhilosophyPreset* wanderer = find_philosophy("wanderer");
    CHECK(wanderer != nullptr);
    unsigned ch_w = apply_midgame_pick(r, host_space, wanderer->cards, kSpace, &acc);
    CHECK(acc && ch_w == 0x1Fu && r.cards[0] == 0 && r.cards[1] == 0 && r.cards[2] == 0 && r.cards[3] == 0 && r.cards[4] == 0);
    CHECK(update(r, host_space, decide_space, false, false, kSpace, true) == (1u << kSpace));
    CHECK(r.cards[kSpace] == kSpaceWanderer && r.cards[0] == 0 && r.cards[3] == 0);
    CHECK(update(r, host_space, decide_space, false, false, kSpace, true) == 0);

    // Fresh Space start where host has {0, 0, 0, 0, kSpaceWanderer}: guest can still pick Shaman!
    const uint32_t host_fresh_space[kStages] = {0, 0, 0, 0, kSpaceWanderer};
    Record rf = seed(host_fresh_space, 0.f, 0.f);
    const PhilosophyPreset* shaman = find_philosophy("shaman");
    CHECK(apply_midgame_pick(rf, host_fresh_space, shaman->cards, kSpace, &acc) == 0x1Fu && acc);
    CHECK(update(rf, host_fresh_space, decide_space, false, false, kSpace, true) == (1u << kSpace));
    CHECK(rf.cards[kSpace] == kSpaceShaman && rf.cards[kCell] == kCellHerbivore && rf.cards[kCiv] == kCivReligious);

    // Anti-forging in Creature stage: ONLY Cell (stage 0) is eligible; Creature/Tribe/Civ are ignored!
    const uint32_t host_creature[kStages] = {kCellCarnivore, 0, 0, 0, 0};
    Record rc = seed(host_creature, 0.f, 0.f);
    const uint32_t forge_try[4] = {kCellOmnivore, kCreatureSocial, kTribeFriendly, kCivReligious};
    unsigned ch_c = apply_midgame_pick(rc, host_creature, forge_try, kCreature, &acc);
    CHECK(acc && ch_c == (1u << kCell));
    CHECK(rc.cards[kCell] == kCellOmnivore && rc.cards[kCreature] == 0 && rc.cards[kTribe] == 0 && rc.cards[kCiv] == 0);
    CHECK(rc.picked_mask == (1u << kCell));

    // Trying to pick ONLY future stages in Creature is rejected
    const uint32_t future_only[4] = {0, kCreatureSocial, kTribeFriendly, 0};
    CHECK(apply_midgame_pick(rc, host_creature, future_only, kCreature, &acc) == 0 && !acc);

    // Wanderer (all-zero) is rejected before Space
    CHECK(apply_midgame_pick(rc, host_creature, wanderer->cards, kCreature, &acc) == 0 && !acc);

    // In Cell stage: NO stage can be picked
    const uint32_t host_cell[kStages] = {0, 0, 0, 0, 0};
    Record rcell = seed(host_cell, 0.f, 0.f);
    CHECK(apply_midgame_pick(rcell, host_cell, forge_try, kCell, &acc) == 0 && !acc);

    // Reloading an earlier save (e.g. Creature) clears picked stages that are no longer past
    unsigned ch_reload = update(rf, host_creature, decide_space, false, false, kCreature, true);
    CHECK((ch_reload & ((1u << kCreature) | (1u << kTribe) | (1u << kCiv) | (1u << kSpace))) != 0);
    CHECK(rf.cards[kCell] == kCellHerbivore && rf.cards[kCreature] == 0 && rf.cards[kTribe] == 0 && rf.cards[kCiv] == 0 && rf.cards[kSpace] == 0);
    CHECK(rf.picked_mask == (1u << kCell));

    // With honor_picked=false (SPORE_FIX_OFF=history.midgame_pick): picked Tribe/Civ revert to host in Together mode
    Record r_off = seed(host_space, 0.f, 0.f);
    apply_midgame_pick(r_off, host_space, zealot->cards, kSpace, &acc);
    update(r_off, host_space, decide_space, false, false, kSpace, false);
    CHECK(r_off.cards[kTribe] == kTribeAggressive && r_off.cards[kCiv] == kCivMilitary && r_off.cards[kSpace] == kSpaceWarrior);
}

static void test_midgame_persistence_and_wire()
{
    printf("encode/decode with picked_mask and wire::history::Pick codec\n");
    // picked_mask alone (no Rivals tallies) encodes as 43 bytes and round-trips cleanly
    Record rp;
    rp.cards[kCell] = kCellHerbivore;
    rp.cards[kCreature] = kCreatureSocial;
    rp.cards[kTribe] = kTribeFriendly;
    rp.cards[kCiv] = kCivReligious;
    rp.cards[kSpace] = kSpaceShaman;
    rp.picked_mask = 0x0Fu;
    auto b = encode(rp);
    CHECK(b.size() == 43);
    Record dec;
    CHECK(decode(b, dec) && dec == rp && dec.picked_mask == 0x0Fu && dec.civ_start_spec == 0);

    // picked_mask combined with civ_start_spec + Rivals tallies
    rp.tribe_allied = 2;
    rp.civ_religious = 3;
    rp.civ_start_spec = kCivSpecReligious;
    rp.picked_mask = 0x05u; // Cell + Tribe picked
    b = encode(rp);
    CHECK(b.size() == 43 && decode(b, dec) && dec == rp && dec.civ_start_spec == kCivSpecReligious && dec.picked_mask == 0x05u);

    // Invalid spec nibble with valid upper picked_mask nibble is still rejected
    auto bad = b;
    bad.back() = (uint8_t)(0xF0u | 0x04u);
    CHECK(!decode(bad, dec));

    // wire::history::Pick round-trip (exact 16 bytes) and truncated rejection
    wire::history::Pick pk;
    pk.cards[0] = kCellCarnivore;
    pk.cards[1] = kCreatureAggressive;
    pk.cards[2] = kTribeFriendly;
    pk.cards[3] = kCivReligious;
    net::Writer w;
    pk.write(w);
    CHECK(w.data.size() == 16);
    net::Reader rd(w.data.data(), w.data.size());
    wire::history::Pick pk2;
    CHECK(pk2.read(rd) && rd.left() == 0);
    for (int i = 0; i < 4; i++)
        CHECK(pk2.cards[i] == pk.cards[i]);
    net::Reader trunc(w.data.data(), w.data.size() - 1);
    wire::history::Pick pk3;
    CHECK(!pk3.read(trunc));
}

int main()
{
    test_tribe_card_for();
    test_civ_card_for();
    test_specialty_mappings();
    test_record_tallies_and_update();
    test_codec_and_store_compat();
    test_midgame_philosophy_presets();
    test_midgame_pick_rules_and_update();
    test_midgame_persistence_and_wire();
    if (g_fails) {
        fprintf(stderr, "history_rivals_tests: %d FAILED\n", g_fails);
        return 1;
    }
    printf("history_rivals_tests: ALL PASSED\n");
    return 0;
}
