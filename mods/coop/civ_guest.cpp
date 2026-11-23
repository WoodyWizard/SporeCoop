// The guest's side of the Civ stage: the host's cities, buildings and ruins on the guest, the vehicle mirrors and
// the snapshot they come in (guest_state, guest_tick, prune_gone), the reuse hunt of the tests; moved out of civ.cpp
// (C8 of 0.7.15, BUG-1164).
#include "civ_internal.h"

namespace civ {

// ---------------- guest ----------------
void* local_city_near(const net::Vec3& p)
{
    void* best = nullptr;
    float bd = 60.f;
    for (void* c : all_cities()) {
        float d = net::dist(city_pos(c), p);
        if (d < bd) {
            bd = d;
            best = c;
        }
    }
    return best;
}

// the host's building list of a city against the local one: a building missing here is built, one
// the host does not have is removed (a mismatch must be seen on two passes in a row: an incremental
// MSG_CIV_BUILD may be on its way)
std::unordered_map<void*, int> g_bfix_seen;
// BUG-0324: the host's game rebuilt a building the siege had ruined (a siege that ended without a
// capture); the guest kept the ruin for good: reconcile_buildings only made ruins, never undid one.

static void reconcile_buildings(void* city, size_t n,
                                const std::function<void(size_t, uint32_t&, net::Vec3&, bool&)>& host_b)
{
    if (planner_holds(city) || planner_send_holds(city)) { // BUG-0493: after the planner closes (the next snapshot);
        g_n.planner_held++;                                 // BUG-9302: and the host has the building it kept
        g_bfix_seen[city] = 0;
        return;
    }
    auto local = vec_of((uint8_t*)city + kCityBuildings);
    std::vector<bool> used(local.size(), false);
    std::vector<size_t> missing;
    std::vector<void*> ruin_now;
    std::vector<std::pair<void*, net::Vec3>> off_spot;
    int ruins = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
        host_b(i, noun, p, ruin);
        bool found = false;
        for (size_t k = 0; k < local.size() && !found; k++) {
            net::Vec3 lp;
            if (used[k] || game::noun_id(local[k]) != noun || !game::object_position(local[k], lp) ||
                net::dist(lp, p) > 4.f)
                continue;
            // BUG-0324: a ruin here that the host has whole again (rebuilt after the siege): not this
            // building, so it goes as an extra and the host's comes back as a missing one
            if (!ruin && F<int>(local[k], kBuildingState) == 2 && type_for_noun(noun))
                continue;
            used[k] = found = true;
            if (net::dist(lp, p) > 1.f)
                off_spot.push_back({local[k], p}); // BUG-0602: matched, but not where the host's stands
            if (ruin && F<int>(local[k], kBuildingState) != 2)
                ruin_now.push_back(local[k]);
            ruins += ruin != (F<int>(local[k], kBuildingState) == 2);
        }
        if (!found)
            missing.push_back(i);
    }
    std::vector<void*> extra;
    for (size_t k = 0; k < local.size(); k++)
        if (!used[k] && type_for_noun(game::noun_id(local[k])))
            extra.push_back(local[k]);
    g_n.ruins_differ = ruins;
    // a building the host's siege destroyed: the same damage here (the generic combatant damage
    // with no attacker: hp 0, state 2; no capture damage or alarm, those come from the host)
    static const float zero[3] = {0, 0, 0};
    for (void* b : ruin_now) {
        {
            callguard::Hold applying(g_applying);
            thiscall<int>(a_CombatantDamage, (uint8_t*)b + 0x120, F<float>(b, kBuildingState + 4) + 1.f, 0xFFFFFFFFu, 0,
                          (const float*)zero, (void*)nullptr);
        }
        g_n.ruins_in++;
    }
    for (auto& [b, p] : off_spot)
        to_host_spot(b, p);
    int& seen = g_bfix_seen[city];
    if (missing.empty() && extra.empty()) {
        seen = 0;
        return;
    }
    if (++seen < 2)
        return;
    seen = 0;
    callguard::Hold applying(g_applying);
    for (void* b : extra) {
        thiscall<void>(a_DestroyBuilding, city, b);
        g_n.bfix_removed++;
    }
    for (size_t i : missing) {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
        host_b(i, noun, p, ruin);
        float near_before = 0;
        void* occ = nullptr;
        slot_of(city, nullptr, p, &near_before, &occ);
        if (void* b = build_in_city(city, noun, p, false)) {
            g_n.bfix_added++;
            net::Vec3 bp{};
            game::object_position(b, bp);
            if (net::dist(bp, p) > 4.f) {
                g_n.bfix_off_slot++;
                if (g_n.bfix_logged++ < 12)
                    H->log(M, SK_INFO,
                           "civ: BUG-0602: the host's building %08X of city %d at (%.1f %.1f %.1f) went to (%.1f %.1f %.1f) here, "
                           "%.1f m off; the nearest slot here was %.1f m from the host's spot (occupant %08X)",
                           noun, (int)(std::find(g_host_cities.begin(), g_host_cities.end(), city) - g_host_cities.begin()), p.x, p.y, p.z, bp.x, bp.y, bp.z, net::dist(bp, p), near_before,
                           occ ? game::noun_id(occ) : 0u);
            }
            to_host_spot(b, p); // BUG-0602
        }
    }
}

// BUG-0529: the host's hit points of one city's buildings (chunk kHpChunk)
void guest_building_hp(net::Reader& r)
{
    uint8_t ci = r.u8(), n = r.u8();
    struct Row {
        uint32_t noun;
        net::Vec3 p;
        float hp;
    };
    std::vector<Row> rows(n);
    for (auto& b : rows) {
        b.noun = r.u32();
        b.p.x = r.f32(); b.p.y = r.f32(); b.p.z = r.f32();
        b.hp = r.f32();
    }
    if (!r.ok())
        return;
    g_n.hp_rx++;
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (!city || local_play() || planner_holds(city) || game::mode_id() != modes::kCiv)
        return;
    auto local = vec_of((uint8_t*)city + kCityBuildings);
    std::vector<bool> used(local.size(), false);
    static const float zero[3] = {0, 0, 0};
    for (auto& b : rows) {
        if (!std::isfinite(b.hp) || b.hp <= 0.f)
            continue; // a ruin: reconcile_buildings makes it
        void* best = nullptr;
        size_t bk = 0;
        float bd = 4.f;
        for (size_t k = 0; k < local.size(); k++) {
            net::Vec3 lp;
            if (used[k] || game::noun_id(local[k]) != b.noun || !game::object_position(local[k], lp))
                continue;
            float d = net::dist(lp, b.p);
            if (d < bd) {
                bd = d;
                best = local[k];
                bk = k;
            }
        }
        if (!best) {
            g_n.hp_nomatch++;
            continue;
        }
        used[bk] = true;
        if (F<int>(best, kBuildingState) == 2)
            continue; // ruined here, whole there: reconcile_buildings (BUG-0324)
        float& hp = F<float>(best, kBuildingState + 4);
        if (hp > b.hp + 0.5f) {
            {
                callguard::Hold applying(g_applying);
                thiscall<int>(a_CombatantDamage, (uint8_t*)best + 0x120, hp - b.hp, 0xFFFFFFFFu, 0, (const float*)zero,
                              (void*)nullptr);
            }
            if (F<int>(best, kBuildingState) != 2 && std::fabs(hp - b.hp) > 0.5f)
                hp = b.hp; // the game's damage scaled it: the host's value
            g_n.hp_lowered++;
        } else if (hp < b.hp - 0.5f) {
            hp = b.hp;
            g_n.hp_raised++;
        }
    }
}

// BUG-0493: vehicles of this game the mod removes once the city planner no longer moves them (kill_wait): a removal waits while the planner moves the vehicle, and (BUG-0478: the removal frees it now, Die
// alone did not) while a cinematic plays (its actors are the game's raw pointers, CLAUDE.md 2)
static bool hold_removal(void* v) { return civlogic::hold_removal(planner_moves(v), movie_playing()); }

// BUG-0470: the guest's mirror vehicles are the host's world, not this one's: a save of the guest's
// game keeps them out (savestrip writes a null reference; the civilization's vehicle lists are cleaned
// of nulls on load, savestrip.cpp).
// The civilizations' vehicle vector (+0xB0) and selectable vector (+0xD4, the vehicles' spatial
// interfaces) read null entries back on load and faulted (0xBF2BF2): the mirrors leave both lists
// while the save is written and come back on the next frame (savestrip::add_save_hooks).
struct Took {
    void* civ;
    uintptr_t off;
    void* p;
    void* v;         // the vehicle (p is its spatial interface in the selectable vector)
    uint32_t serial; // its cGameData::mID when it was taken out
};
static std::vector<Took> g_took;
// BUG-9301 (audit CIV-1): save_start runs inside the save's first write, save_end only on the next frame's
// savestrip::tick, after that frame's network receive: a host snapshot in between that no longer names a mirror
// removes it (kill_local: Vehicle_Remove takes it out of the game's list at once and frees it later), and save_end put
// the removed vehicle back into the civilization's vehicle and selectable vectors (a dangling pointer the civ update
// walks). Now only a vehicle still in the game's list (the same object: its serial) of a civilization still listed
// goes back; the taken list is cleared with the world and the session. Off: every taken entry back (0.7.16).
static bool g_took_check_fix = true;
COOP_FIX_UNSAFE("civ.save_took_listed", "BUG-9301", g_took_check_fix,
                "civ: a guest's save puts back only the mirror vehicles the game still has (one removed during the save stays out)");
static bool g_debug_save_kill; // tests (coop.civ savekill): the next save_end first removes a taken mirror
uint32_t g_took_back, g_took_gone, g_took_cleared;
static bool is_mirror(void* v) { return gv_bound(v) || kill_waiting(v); }
static void host_buildings_keys(bool own);
void save_start()
{
    host_buildings_keys(true); // BUG-0670: no key of ours in the save
    own_designs_save(true);    // BUG-1274: the culture's keys
    if (!is_guest() || game::mode_id() != modes::kCiv || !nm())
        return;
    for (void* civ : vec_of(thiscall<void*>(a_GetAllCivs, nm())))
        for (uintptr_t off : {(uintptr_t)0xB0, (uintptr_t)0xD4}) {
            void**& b = F<void**>(civ, off);
            void**& e = F<void**>(civ, off + 4);
            if (!b || e < b)
                continue;
            void** w = b;
            for (void** it = b; it != e; ++it) {
                void* v = *it ? (off == 0xB0 ? *it : (uint8_t*)*it - kVSpatial) : nullptr;
                if (is_mirror(v))
                    g_took.push_back({civ, off, *it, v, game::object_serial(v)});
                else
                    *w++ = *it;
            }
            for (void** it = w; it != e; ++it)
                *it = nullptr;
            e = w;
        }
}
void save_end()
{
    host_buildings_keys(false); // BUG-0670
    own_designs_save(false);    // BUG-1274
    if (g_debug_save_kill && !g_took.empty()) { // tests: as if this frame's host snapshot removed a mirror first
        g_debug_save_kill = false;
        void* v = g_took.front().v;
        H->log(M, SK_INFO, "civ: savekill: the taken mirror %p removed before the save's end (tests, BUG-9301)", v);
        if (exists(v))
            kill_local(v);
    }
    std::vector<void*> civs;
    if (g_took_check_fix && !g_took.empty()) {
        g_exist_frame = ~0ull; // the game's vehicle list of now
        if (game::mode_id() == modes::kCiv && nm())
            civs = vec_of(thiscall<void*>(a_GetAllCivs, nm()));
    }
    for (auto& t : g_took) { // back at the end of the list (the capacity is the one they left)
        if (g_took_check_fix) {
            bool civ_here = std::find(civs.begin(), civs.end(), t.civ) != civs.end();
            if (!civlogic::took_goes_back(civ_here, civ_here && exists(t.v),
                                          civ_here && exists(t.v) && game::object_serial(t.v) == t.serial)) {
                g_took_gone++;
                H->log(M, SK_INFO, "civ: a mirror vehicle %p taken out for the save is gone: not put back (BUG-9301)", t.v);
                continue;
            }
        }
        void**& e = F<void**>(t.civ, t.off + 4);
        if (e < F<void**>(t.civ, t.off + 8)) {
            *e++ = t.p;
            g_took_back++;
        }
    }
    g_took.clear();
}
// BUG-9301: the world or the session ended between a save's start and its end: nothing goes back
void took_clear()
{
    if (!g_took_check_fix || g_took.empty())
        return;
    g_took_cleared += (uint32_t)g_took.size();
    g_took.clear();
}
std::string took_status()
{
    char b[160];
    snprintf(b, sizeof b, "save_took: fix=%d taken=%u back=%u gone=%u cleared=%u armed=%d\n", (int)g_took_check_fix,
             (unsigned)g_took.size(), g_took_back, g_took_gone, g_took_cleared, (int)g_debug_save_kill);
    return b;
}
void took_debug_kill(bool on) { g_debug_save_kill = on; }
bool save_filter(void* o, uint32_t noun)
{
    if (noun != kVehicleNoun)
        return false;
    if (!g_gvr.ours(o, noun, true)) // C1: any bound one, Civ's rule, or one waiting for its removal
        return false;
    g_n.save_stripped++;
    return true;
}

// BUG-0478: Die(v, 0, -1) alone leaves the dead vehicle in the world for good (in the noun list, the
// civilization's lists, moved on by its AI): the host's vehicle that died is gone from the host's
// game, the guest kept a wreck (the detector's "a vehicle more on the guest", more the longer it
// played). The game removes a vehicle with 0xCAA7B0 (the disband path: Die, the behavior manager,
// cCivilization 0xBF3D90 which destroys it).


void kill_local(void* v)
{
    callguard::Hold applying(g_applying);
    if (a_VehicleRemove) {
        thiscall<void>(a_VehicleRemove, v, 0); // no refund
        g_exist_frame = ~0ull;                 // freed: the frame's list of vehicles is read again
    } else
        thiscall<void>(a_Die, v, 0, 0xFFFFFFFFu);
}

// BUG-0380: the catch-up founded one city per 15 s, so a guest that missed several foundings (the
// campaign's shortcut founds the remaining cities with instant=1, which is not broadcast; a join)
// lacked those cities, and every vehicle of their nations (no civilization to make the mirror
// for), for up to a minute and a half: sync-check "v: 6 missing on the guest" in every
// campaign_space. The city simulator counts its own foundings at once (+0xB0 sites not founded yet,
// +0xAC all sites, docs/re/civ.md#civ-city-burst): a guest whose foundings (appeared or on their
// way) are fewer than the host's cities founds the difference in one go, and never founds while its
// own foundings are only waiting to appear (a paused game holds them).
constexpr uintptr_t kExSites = 0xAC, kExSitesLeft = 0xB0; // cCivExpansion: all sites, sites not founded yet
size_t g_cities_waiting_told;                     // the "founded here, on their way" line, once per count
// the sites this game founded (appeared or still on their way), -1 when the fields look wrong
static int founded_here(void* ex)
{
    if (!ex || !skg::readable(ex, kExSitesLeft + 4))
        return -1;
    int all = F<int>(ex, kExSites), left = F<int>(ex, kExSitesLeft);
    return all > 0 && all <= 64 && left >= 0 && left <= all ? all - left : -1;
}

// a founded city appears a few seconds after SpawnNextCity (docs/re/civ.md 9.3): the count must
// stay short for 6 s, and 15 s must pass after any founding here, so a replayed event is not
// founded a second time; never more cities than the host has
static void city_catch_up(size_t host_total)
{
    if (!g_fix_cities)
        return;
    // BUG-0231: a second guard behind the message gate: a guest that plays its own world (another
    // world of the host's stage) or plays on while the host is away founds nothing for the host
    if (local_play())
        return;
    double now = H->time_seconds();
    size_t have = all_cities().size();
    if (have >= host_total) {
        g_city_short_since = -1;
        g_cities_waiting_told = 0;
        return;
    }
    if (g_city_short_since < 0)
        g_city_short_since = now;
    void* ex = skg::cdecl_call<void*>(a_ExpansionGet);
    int founded = founded_here(ex);
    // 2 s short first: a founding of the host whose broadcast is on its way is not made twice
    if (founded >= 0 && now - g_city_short_since < 2.0)
        return;
    if (founded >= 0 && (size_t)founded >= host_total) { // founded here: they appear by themselves
        if (g_cities_waiting_told != host_total)
            H->log(M, SK_INFO, "civ: the host has %u cities, this game %u and %d founded (on their way)",
                   (unsigned)host_total, (unsigned)have, founded);
        g_cities_waiting_told = host_total;
        return;
    }
    if (founded >= 0) {
        int n = (int)host_total - founded, made = 0;
        {
            callguard::Hold applying(g_applying);
            for (int i = 0; i < n; i++)
                if (o_SpawnNextCity(ex, g_instant_found_fix ? 1 : 0, nullptr)) // BUG-0624: late already
                    made++;
                else
                    break; // no free site left
        }
        g_last_city_spawn = now;
        g_city_short_since = -1;
        g_n.cities_caught_up += made;
        H->log(M, SK_INFO, "civ: the host has %u cities, this game %u (%d founded): founding %d of %d at once",
               (unsigned)host_total, (unsigned)have, founded, made, n);
        return;
    }
    // fields that look wrong: the old pace below
    if (now - g_city_short_since < 6.0 || now - g_last_city_spawn < 15.0)
        return;
    ex = skg::cdecl_call<void*>(a_ExpansionGet);
    if (!ex)
        return;
    {
        callguard::Hold applying(g_applying);
        o_SpawnNextCity(ex, 0, nullptr);
    }
    g_last_city_spawn = now;
    g_city_short_since = -1;
    g_n.cities_caught_up++;
    H->log(M, SK_INFO, "civ: the host has %u cities, this game %u: founding the next site", (unsigned)host_total,
           (unsigned)all_cities().size());
}

static bool guest_cities(net::Reader& r) // false: the list does not fit the message
{
    uint8_t total = r.u8(), c0 = r.u8(), nc = r.u8();
    if (!r.ok() || c0 + nc > total)
        return false;
    if (c0 == 0)
        city_catch_up(total);
    if (g_host_cities.size() != total)
        g_host_cities.resize(total, nullptr);
    for (int i = 0; i < nc && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        uint32_t pol = r.u32();
        uint8_t owner = r.u8();
        float capture = r.f32();
        uint32_t capture_by = r.u32();
        if (!r.ok())
            return false;
        void* c = local_city_near(p);
        g_host_cities[c0 + i] = c;
        if (!c)
            continue;
        if (owner != 0xFE && owner != 0xFF)
            g_city_owner[c] = owner; // kShared included
        else
            g_city_owner.erase(c);
        if (pol_of(c) != pol && planner_holds(c)) // BUG-0493: the next snapshot after the planner
            g_n.planner_held++;
        else if (pol_of(c) != pol && !thiscall<void*>(a_CivByPol, nm(), pol)) {
            // BUG-0492: an owner this game has no civilization for: the city would belong to nobody
            // here (its menu and discovery card read the missing civilization)
            if (g_n.owner_unknown++ < 10)
                H->log(M, SK_INFO, "civ: the host's owner %08X of city %d is no civilization here: not changed (BUG-0492)", pol,
                       c0 + i);
        }
        else if (pol_of(c) != pol) { // captured on the host
            {
                callguard::Hold applying(g_applying);
                thiscall<void>(a_ChangeOwner, c, pol, 0, 0);
            }
            g_n.captures_in++;
        }
        // the siege meter: the guest's city is never hit (its mirrors do not shoot), so its own
        // progress stays 0 (and decays): show the host's (SetCaptureProgress updates the meter)
        if (std::isfinite(capture) && (std::fabs(F<float>(c, kCityCapture) - capture) > 0.5f ||
                                       F<uint32_t>(c, kCityCapture + 4) != capture_by)) {
            callguard::Hold applying(g_applying);
            thiscall<void>(a_SetCapture, c, capture_by, capture);
        }
    }
    return true;
}

static void guest_nodes_and_buildings(net::Reader& r)
{
    uint8_t nn = r.u8(); // spice node owners (a guest's derricks appear where the host's vehicles claimed)
    for (int i = 0; i < nn && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        uint32_t pol = r.u32();
        if (!r.ok())
            return;
        void* node = game::find_object(kNodeNoun, p, 5.f);
        if (!node || pol_of(node) == pol)
            continue;
        {
            callguard::Hold applying(g_applying);
            if (pol == 0xFFFFFFFFu)
                thiscall<void>(a_NodeUnclaim, node);
            else
                thiscall<void>(a_NodeClaim, node, pol);
        }
        g_n.nodes_in++;
    }
    uint8_t ci = r.u8();
    if (!r.ok() || ci == 0xFF)
        return;
    struct HB {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
    };
    std::vector<HB> hb(r.u8());
    for (auto& b : hb) {
        b.noun = r.u32();
        b.p.x = r.f32(); b.p.y = r.f32(); b.p.z = r.f32();
        b.ruin = r.u8() != 0;
    }
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (r.ok() && city)
        reconcile_buildings(city, hb.size(), [&](size_t i, uint32_t& noun, net::Vec3& p, bool& ruin) {
            noun = hb[i].noun; p = hb[i].p; ruin = hb[i].ruin;
        });
}

// BUG-0469: the design key a mirror vehicle of that purpose and locomotion is built with, and the
// vehicle is created from that key (CreateVehicle reads the model of the design: cVehicle::Load
// 0xCA67C0). The civ's own design for the purpose and locomotion is checked with the parts and paints
// of its model (creations::ensure_model):
//  - the design this game has: the key itself;
//  - a part or paint this game does not have, with a substitute: our copy of the model (under the
//    design's own key, above the game's packages: the mirror gets the similar base game parts and no
//    key of ours reaches the save);
//  - one that cannot be built here at all: a zero key, the game's own design of the same purpose and
//    locomotion (cVehicle::Load generates one from the object templates for 0 and -1), logged;
//  - false: the civilization has no design for that purpose and locomotion here (no mirror, 0.7.3.5).
// `coop.fix content.all_kinds off`: the design key as it is (0.7.3.5).
static std::unordered_set<uint32_t> g_veh_design_logged;
bool g_veh_force; // tests (BUG-0469): mirrors are built with this design key instead
uint32_t g_veh_force_key[3];

static bool mirror_design(void* civ, int purpose, int loco, uint32_t key[3])
{
    if (!civ)
        return false; // no civilization of that political id here: no mirror (as 0.7.3.5)
    if (g_veh_force) { // tests (BUG-0469): a design key the guest builds in the mirror path
        key[0] = g_veh_force_key[0];
        key[1] = g_veh_force_key[1];
        key[2] = g_veh_force_key[2];
    } else {
        uint32_t* k = thiscall<uint32_t*>(a_DesignKey, civ, skg::cdecl_call<int>(a_DesignId, purpose, loco));
        if (!k)
            return false;
        key[0] = k[0];
        key[1] = k[1];
        key[2] = k[2];
    }
    if (!key[0])
        return true; // the game's own design for that purpose and locomotion (it generates one)
    if (!g_veh_force) { // BUG-0647: the host's own design, sent by the host, under a key of ours
        bool pending = false;
        creations::Key hk = creations::host_design({key[0], key[1], key[2]}, creations::kMkVehicle, &pending);
        if (pending)
            return false; // asked the host: the mirror comes with its answer (the next state cycle)
        key[0] = hk.instance;
        key[1] = hk.type;
        key[2] = hk.group;
    }
    creations::Model m = creations::ensure_model({key[0], key[1], key[2]}, creations::kMkVehicle);
    if (m.key.instance) {
        g_n.veh_design_checked++;
        key[0] = m.key.instance;
        key[1] = m.key.type;
        key[2] = m.key.group;
        return true;
    }
    if (g_veh_design_logged.insert(key[0]).second)
        H->log(M, SK_INFO, "civ: the model of the vehicle design %08X cannot be built here: the mirror takes the "
               "game's own design of that purpose and locomotion (BUG-0469)", key[0]);
    key[0] = key[1] = key[2] = 0;
    g_n.veh_design_fallback++;
    return true;
}

// BUG-0647: a vehicle of this game's loaded copy of the host's world bound as a mirror: its model is the key
// the save names (the host's own design id), which this game resolves to its own creation of that id. The
// host's design under our key replaces it (the model object's SetModelKey, vt 0x94 of +0x34, as
// cVehicle::Load 0xCA67C0 sets it). false: asked the host, no answer yet (do not bind it yet).
static bool bound_design(void* v)
{
    uint32_t* mk = &F<uint32_t>(v, kVModelKey);
    if (!mk[0] || mk[0] == 0xFFFFFFFFu || mk[1] != 0x24682294 || own_designs_holds(v)) // (BUG-1274: its owner's own)
        return true;
    bool pending = false;
    creations::Key hk = creations::host_design({mk[0], mk[1], mk[2]}, creations::kMkVehicle, &pending);
    if (pending)
        return false;
    if (hk.instance == mk[0])
        return true;
    creations::Model m = creations::ensure_model(hk, creations::kMkVehicle);
    if (!m.key.instance)
        return true; // cannot be built here: it keeps the model it has
    uint32_t k[3] = {m.key.instance, m.key.type, m.key.group}, was = mk[0];
    void* sp = (uint8_t*)v + kVSpatial;
    vcall<void>(sp, 0x94 / 4, (const uint32_t*)k);
    g_n.bound_redesigned++;
    H->log(M, SK_INFO, "civ: a bound vehicle's model %08X is the host's own design: %08X here (BUG-0647)", was, k[0]);
    return true;
}

// BUG-1157 (designs.cpp): after the host's new vehicle design reached this guest's culture set, the game's setter
// (0xBF4C50) re-modelled the civilization's vehicles with the host's key, which this game may resolve to its own
// creation of that id: each one gets the host's design under a key of ours as a bound vehicle does. Returns the
// vehicles still waiting for the host's answer.
unsigned redesign_vehicles()
{
    void* civ = is_guest() && nm() ? player_civ() : nullptr;
    unsigned pending = 0;
    for (void* v : civ ? vec_of((uint8_t*)civ + 0xB0) : std::vector<void*>{})
        if (v && !bound_design(v))
            pending++;
    return pending;
}

// BUG-0670 (docs/re/civ.md#civ-host-buildings): a building's model is the design key cCity::SetBuildingModel
// (0xBDB940) reads from its civilization itself (GetDesignKey), then the building's model object's SetModelKey
// (vt 0x94 of +0x34, the key at +0x34 +0x90 as for vehicles). The host's player civilization names its building
// designs by keys of the host's own My Creations; the guest's copy of it names the same keys, which the guest's game
// resolved to the guest's own creation of that id: other city halls, houses, factories than the host's (the class of
// BUG-0647). On a guest every building of every city is checked once a second (built by build_in_city, by the
// guest's own planner, or loaded with the host's world): a design of the host's own (asked with
// creations::host_design, the BUG-0647 path) gets the host's records under a key of ours, and the building's model
// object that key (SetModelKey). The building's own key is written back while the game saves and when the session
// ends (no key of ours in a save). `coop.fix civ.host_buildings off`: the guest's own creation of that id (0.7.9).
static bool g_host_buildings_fix = true;
COOP_FIX("civ.host_buildings", "BUG-0670", g_host_buildings_fix,
         "civ: a guest's buildings of the host's civilization show the host's own building designs, not the guest's creation of the same id");
constexpr uintptr_t kBModelKey = 0x34 + 0x90; // (the vehicles' kVModelKey: the same spatial model object)
std::unordered_map<void*, BldRekey> g_bld_rekeyed; // guest: building -> its own key (live buildings only read)
static double g_bld_sweep_at = -100;
static std::vector<void*> city_buildings_all()
{
    std::vector<void*> out;
    if (!nm())
        return out;
    for (void* city : all_cities())
        for (void* b : vec_of((uint8_t*)city + kCityBuildings))
            if (b)
                out.push_back(b);
    return out;
}
static void set_building_key(void* b, const uint32_t* k)
{
    vcall<void>((uint8_t*)b + 0x34, 0x94 / 4, k);
}
static void host_buildings_tick(double now)
{
    if (!g_host_buildings_fix || !is_guest() || game::mode_id() != modes::kCiv || now - g_bld_sweep_at < 1.0)
        return;
    g_bld_sweep_at = now;
    unsigned pending = 0;
    for (void* b : city_buildings_all()) {
        uint32_t* mk = &F<uint32_t>(b, kBModelKey);
        if (!mk[0] || mk[0] == 0xFFFFFFFFu || mk[1] != kBuildingModelType)
            continue;
        auto it = g_bld_rekeyed.find(b);
        if (it != g_bld_rekeyed.end() && it->second.ours == mk[0])
            continue; // ours already
        if (own_designs_holds(b))
            continue; // BUG-1274: its owner's own design
        bool wait = false;
        creations::Key hk = creations::host_design({mk[0], mk[1], mk[2]}, creations::kMkBuilding, &wait);
        if (wait) {
            pending++;
            continue;
        }
        if (hk.instance == mk[0])
            continue; // not the host's own (a premade, a generated design): as it is
        creations::Model m = creations::ensure_model(hk, creations::kMkBuilding);
        if (!m.key.instance)
            continue; // cannot be built here: it keeps the model it has
        BldRekey r{{mk[0], mk[1], mk[2]}, m.key.instance};
        uint32_t k[3] = {m.key.instance, m.key.type, m.key.group};
        set_building_key(b, k);
        g_bld_rekeyed[b] = r;
        g_n.bld_rekeys++;
        H->log(M, SK_INFO, "civ: a building's model %08X is the host's own design: %08X here (BUG-0670)", r.orig[0], k[0]);
    }
    g_n.bld_pending = pending;
}
// the buildings of ours get their own keys back (raw: the save writes the key, no model is rebuilt) / ours again
static void host_buildings_keys(bool own)
{
    if (g_bld_rekeyed.empty() || !nm())
        return;
    for (void* b : city_buildings_all()) {
        auto it = g_bld_rekeyed.find(b);
        if (it == g_bld_rekeyed.end())
            continue;
        uint32_t* mk = &F<uint32_t>(b, kBModelKey);
        if (own && mk[0] == it->second.ours) {
            mk[0] = it->second.orig[0];
            g_n.bld_restored++;
        } else if (!own && mk[0] == it->second.orig[0])
            mk[0] = it->second.ours;
    }
}
void host_buildings_session_end()
{
    if (g_bld_rekeyed.empty() || !nm())
        return;
    for (void* b : city_buildings_all()) { // the guest's game plays on alone: its own designs
        auto it = g_bld_rekeyed.find(b);
        if (it != g_bld_rekeyed.end() && F<uint32_t>(b, kBModelKey) == it->second.ours)
            set_building_key(b, it->second.orig);
    }
    g_bld_rekeyed.clear();
}
std::string host_buildings_list()
{
    std::string out;
    char b[200];
    int ci = 0;
    for (void* city : all_cities()) {
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings)) {
            if (!bl)
                continue;
            const uint32_t* mk = &F<uint32_t>(bl, kBModelKey);
            auto it = g_bld_rekeyed.find(bl);
            snprintf(b, sizeof b, "  bld city=%d %p noun=%08X model=%08X/%08X/%08X own=%08X\n", ci, bl, game::noun_id(bl), mk[0],
                     mk[1], mk[2], it == g_bld_rekeyed.end() ? 0u : it->second.orig[0]);
            out += b;
        }
        ci++;
    }
    snprintf(b, sizeof b, "host buildings: fix=%d rekeys=%u pending=%u restored=%u now=%zu\n", (int)g_host_buildings_fix,
             g_n.bld_rekeys, g_n.bld_pending, g_n.bld_restored, g_bld_rekeyed.size());
    return out + b;
}

void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8();
    if (!r.ok())
        return;
    g_rx.policy.keep_order = g_cycle_order_fix; // (the switch is read every chunk)
    if (!g_rx.begin(cycle, chunk, chunks))
        return; // a bad header, or a late chunk of an older cycle
    if (!guest_cities(r))
        return;
    guest_nodes_and_buildings(r);
    double now = H->time_seconds();
    uint8_t n = r.u8();
    const size_t body = (size_t)n * 41; // 41 bytes per vehicle
    double sent = net::snapshot_time(r, body);
    const uint8_t* tops = nullptr; // BUG-0787: the host vehicles' top speeds after the time (a host from 0.7.13 on)
    if (sent < 0 && r.ok() && n && r.left() == body + 4 + (size_t)n * 4) {
        uint32_t ms;
        memcpy(&ms, r.cur() + body, 4);
        sent = ms / 1000.0;
        tops = r.cur() + body + 4;
    }
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GV m;
        m.owner = r.u8(); m.pol = r.u32(); m.purpose = r.u8(); m.loco = r.u8();
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok())
            return;
        g_rx.saw(id);
        GvRef x = gv_at(id);
        GV& g = *x.g;
        void* v = x.v;
        g.owner = m.owner; g.pol = m.pol; g.purpose = m.purpose; g.loco = m.loco;
        g.pos = m.pos; g.rot = m.rot; g.hp = m.hp; g.seen = now;
        // BUG-0186: a step faster than twice the vehicle's own top speed is a jump of the host's game
        bool alive_v = v && gv_ok(x) && !F<uint8_t>(v, kVDead); // (BUG-0951)
        float top = alive_v ? F<float>(v, 0x218) : 0.f;
        g.host_top = -1.f;
        if (tops) {
            float ht;
            memcpy(&ht, tops + 4 * (size_t)i, 4);
            if (std::isfinite(ht) && ht > 0.f && ht < 10000.f) {
                g.host_top = ht;
                if (g_vel_cap_host_fix)
                    top = std::max(top, ht);
            }
        }
        g.est.max_speed = g_vel_cap_fix && alive_v ? 2.f * top + 10.f : 0.f;
        g.est.max_ahead = g_extrap_fix ? kVehAhead : 0.4;
        // BUG-0425 (drive.lead): moved on to the host's now as every remote body; at most kVehLead past its state:
        // its age is the send interval (0.1-0.13 s at 30 fps) plus the latency and a frame, which 0.15 s cut short
        // (the vehicle 0.1-0.24 s behind, p90 2.8-3.5 m at 15-25 m/s, body_trail_civ on a loaded lab). Longer is a
        // host hitch, when its vehicles stand (BUG-0186: 0.4 s threw one 10 m ahead)
        if (g_extrap_fix && net::g_lead_fix)
            g.est.max_ahead = kVehLead;
        g.est.update(m.pos, now, sent);
        if (v && (!gv_ok(x) || F<uint8_t>(v, kVDead))) { // (BUG-0951: also another vehicle at its address)
            uint16_t bid;
            if (g_veh_lifetime_fix && gv_id_of(v, bid) && bid == id && game::listed(v, kVehicleNoun))
                g_n.veh_stale++; // the game lists a vehicle there: another vehicle at the address (another serial)
            gv_unbind(id);
            v = nullptr;
        }
        if (v)
            continue;
        // bind a local vehicle of the same kind nearby, else create one for that civilization (with a
        // design key the tests force (BUG-0469): always create, so the creation path is reached)
        float best = 25.f;
        for (void* lv : all_vehicles()) {
            if (g_veh_force || gv_bound(lv) || pol_of(lv) != m.pol || F<int>(lv, kVPurpose) != m.purpose ||
                F<int>(lv, kVLoco) != m.loco)
                continue;
            float d = net::dist(veh_pos(lv), m.pos);
            if (d < best) {
                best = d;
                v = lv;
            }
        }
        if (v && !bound_design(v))
            continue; // BUG-0647: the host's answer for its model first (bound on a later cycle)
        bool made = !v;
        if (!v) {
            void* civ = thiscall<void*>(a_CivByPol, nm(), m.pol);
            uint32_t key[3] = {0, 0, 0};
            if (mirror_design(civ, (int)m.purpose, (int)m.loco, key)) { // BUG-0469
                float p[3] = {m.pos.x, m.pos.y, m.pos.z};
                v = thiscall<void*>(a_CreateVehicle, civ, (int)m.purpose, (int)m.loco, key[0], key[1], key[2],
                                    (const float*)p, 0);
                if (v)
                    g_n.created++;
            }
        }
        if (!v)
            continue;
        gv_bind(id, v, made);
    }
    // only a complete cycle tells which vehicles are gone: with a chunk lost (or late) every
    // vehicle of it was killed and made again on the next cycle
    if (!g_rx.end(r.ok())) // a cut chunk does not count: the cycle stays incomplete, nothing is removed
        return;
    // C1 (BUG-1140): every mirror the cycle did not name goes; its vehicle is removed now, or (BUG-0493) waits in the
    // registry's deferred list while the planner moves it or a cinematic plays (guest_tick's pump)
    g_rx.finish(g_gvr,
        [](auto& e, bool alive) {
            bool dead = alive && F<uint8_t>(e.ref.p, kVDead);
            if (civlogic::unseen_mirror(alive, dead, alive && !dead && hold_removal(e.ref.p)) ==
                civlogic::Unseen::Defer) {
                g_n.planner_kills_held++;
                return mirror::Unseen::Defer;
            }
            return mirror::Unseen::Remove;
        },
        [](auto& e, bool alive) {
            // (BUG-0951: the object bound, not one at its address)
            if (civlogic::kill_unseen(alive, alive && F<uint8_t>(e.ref.p, kVDead))) {
                kill_local(e.ref.p);
                g_n.removed++;
            }
        });
    // the guest's own vehicles that the host does not have (its local civ AI, the start vehicle)
    for (void* lv : all_vehicles())
        if (!gv_bound(lv)) {
            if (kill_waiting(lv))
                continue;
            if (hold_removal(lv)) {
                kill_wait(lv, game::object_serial(lv));
                g_n.planner_kills_held++;
                continue;
            }
            kill_local(lv);
            g_n.removed++;
            g_n.removed_local++; // tests: a vehicle of this game's own, not a mirror the host no longer has
        }
}

void guest_tick(double now)
{
    guest_fire(now); // BUG-0477
    host_buildings_tick(now); // BUG-0670
    // C1 (BUG-1140): the registry's deferred list (it drops a vehicle that is gone or a mirror again)
    g_gvr.pump(
        [](const mirror::Ref& r) {
            bool listed = exists(r.p), dead = listed && F<uint8_t>(r.p, kVDead);
            return civlogic::pump_waits(listed, dead, listed && !dead && hold_removal(r.p));
        },
        [](const mirror::Ref& r) {
            bool listed = exists(r.p);
            if (!civlogic::pump_kills(listed, listed && F<uint8_t>(r.p, kVDead)))
                return; // the game removed it first
            kill_local(r.p);
            g_n.removed++;
        });
    static double last_sel;
    if (now - last_sel > 0.2) {
        last_sel = now;
        drop_foreign_selection();
    }
    if (local_play()) {
        gv_each([](const GvRef& x) { x.g->has_put = false; });
        return; // the last states would stop every order for 3 s (StopMovement below)
    }
    static double last_tick = 0; // BUG-0438: the last frame's time (the placing shows up a frame later)
    const double fdt = std::min(std::max(now - last_tick, 0.0), 0.1);
    last_tick = now;
    gv_each([&](const GvRef& x) {
        GV& g = *x.g;
        if (!gv_ok(x) || now - g.seen > 3.0) { // (BUG-0951)
            g.has_put = false;
            return;
        }
        const net::Vec3 at = veh_pos(x.v);
        net::Vec3 cur = at;
        // moved on with the estimated velocity (no lag behind the host); BUG-0425 (drive.lead): by the one-way latency
        // the session measures (half the round trip), not a fixed 0.03 s
        net::Vec3 want = g.est.at(now + (g_place_now_fix ? fdt : 0.0), net::g_lead_fix ? net::g_one_way : 0.03);
        if (g_place_now_fix && g.has_put) // BUG-0438: on with the target's velocity, only the rest glided below
            cur = {cur.x + g.est.vel.x * (float)fdt, cur.y + g.est.vel.y * (float)fdt, cur.z + g.est.vel.z * (float)fdt};
        float d = net::dist(cur, want);
        if (g.has_put) {
            g.pushed = net::dist(at, g.put);
            if (g.pushed > g.pushed_max || now - g.pushed_at > 5.0) {
                g.pushed_max = g.pushed;
                g.pushed_at = now;
            }
        }
        g.err = d;
        net::Vec3 p = want;
        if (d < 30.f && d > 0.01f) { // glide out small corrections
            float k = 0.5f;
            p = {cur.x + (want.x - cur.x) * k, cur.y + (want.y - cur.y) * k, cur.z + (want.z - cur.z) * k};
        }
        void* sp = (uint8_t*)x.v + kVSpatial;
        float pp[3] = {p.x, p.y, p.z}, q[4] = {g.rot.x, g.rot.y, g.rot.z, g.rot.w};
        vcall<void>(sp, 0xEC / 4); // StopMovement: the locomotion does not pull it back
        vcall<void>(sp, 0x44 / 4, (const float*)pp, (const float*)q); // Teleport
        F<float>(x.v, kVHp) = g.hp;
        g.put = p;
        g.has_put = true;
    });
}

void guest_build(net::Reader& r)
{
    uint8_t ci = r.u8();
    if (r.ok() && ci == kRoadList) { // BUG-0494: the host's trade routes
        guest_roads(r);
        return;
    }
    uint32_t noun = r.u32();
    net::Vec3 p;
    p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
    if (r.ok() && ci == kNextCitySite && (noun == 0 || noun == kFoundInstant)) { // the host's planet grew a new city: the same site here
        if (void* ex = skg::cdecl_call<void*>(a_ExpansionGet)) {
            bool ok{};
            {
                callguard::Hold applying(g_applying);
                ok = o_SpawnNextCity(ex, noun == kFoundInstant ? 1 : 0, nullptr); // BUG-0624: as the host founded it
            }
            g_n.cities_in++;
            H->log(M, SK_INFO, "civ: the host founded a city: here %s (cities now %u)", ok ? "founded" : "NOT founded",
                   (unsigned)all_cities().size());
            g_last_city_spawn = H->time_seconds();
        }
        return;
    }
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (!r.ok() || !city)
        return;
    if (planner_holds(city)) { // BUG-0493: the snapshots' reconciliation builds it after the planner
        g_n.planner_held++;
        return;
    }
    callguard::Hold applying(g_applying);
    if (void* b = build_in_city(city, noun, p, false)) {
        g_n.builds_in++;
        to_host_spot(b, p); // BUG-0602
    }
}

void prune_gone()
{
    // guests only: the host's map is rebuilt from the vehicle list in host_tick, and a vehicle it has
    // just bought for a guest is not in the list yet (dropping it lost the guest's ownership)
    std::vector<uint16_t> gone;
    gv_each([&](const GvRef& x) {
        if (!x.v)
            return;
        if (exists(x.v))
            x.g->listed = true;
        else if (x.g->listed)
            gone.push_back(x.id);
    });
    for (uint16_t id : gone)
        gv_unbind(id);
}

VehHunt g_vh;
void veh_hunt_tick()
{
    if (g_vh.left <= 0 || !g_vh.target)
        return;
    g_vh.left--;
    void* civ = player_civ();
    if (!civ || !a_CreateVehicle)
        return;
    float p[3] = {g_vh.at.x, g_vh.at.y, g_vh.at.z};
    void* v{};
    {
        callguard::Hold applying(g_applying);
        v = thiscall<void*>(a_CreateVehicle, civ, g_vh.purpose, g_vh.loco, 0u, 0u, 0u, (const float*)p, 0);
    }
    g_vh.tries++;
    if (!v)
        return;
    if ((uint8_t*)v == g_vh.target) {
        g_vh.hit = v;
        g_vh.hit_serial = game::object_serial(v);
        g_vh.left = 0;
        H->log(M, SK_INFO, "civ reuse: the freed vehicle address %p holds a new vehicle of this guest's civilization "
               "(serial %u, try %d)", v, g_vh.hit_serial, g_vh.tries);
        return;
    }
    kill_local(v);
}
std::string veh_hunt_status()
{
    char b[400];
    bool l = g_vh.hit && game::listed(g_vh.hit, 0x18C6DE8u) && game::object_serial(g_vh.hit) == g_vh.hit_serial;
    net::Vec3 q = l ? veh_pos(g_vh.hit) : net::Vec3{};
    GvRef it = gv_find(g_vh.id);
    net::Vec3 want = it ? it.g->pos : net::Vec3{};
    GvRef gi = l ? gv_of(g_vh.hit) : GvRef{};
    snprintf(b, sizeof b, "civ reuse target=%p mirror=%u left=%d tries=%d hit=%p alive=%d dead=%d own_civ=%d pos=(%.2f %.2f %.2f) "
             "host_vehicle=(%.2f %.2f %.2f) bound_as=%d stale=%u\n", (void*)g_vh.target, g_vh.id, g_vh.left, g_vh.tries,
             g_vh.hit, (int)l, l ? (int)F<uint8_t>(g_vh.hit, kVDead) : -1,
             (int)(l && player_civ() && pol_of(g_vh.hit) == pol_of(player_civ())), q.x, q.y, q.z, want.x, want.y, want.z,
             gi ? (int)gi.id : -1, g_n.veh_stale);
    return b;
}


} // namespace civ
