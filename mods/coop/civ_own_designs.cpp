// WP-C2 (BUG-1274, docs/plan-0.7.16.md item 2, docs/re/civ.md#civ-own-designs): a player's vehicles and buildings
// show that player's own designs. The civilization is one (its culture set is the host's, BUG-1157 / BUG-1158); the
// game draws each vehicle and building from a model key of its own (+0x34 +0x90 = +0xC4) and SetModelKey (vt 0x94 of
// +0x34) changes it with the design's stats (Civ_SetDesign does nothing else per object). So on every game, a
// vehicle or building whose owner (a vehicle's: civ.cpp's tables; a building's: its city's) has an own design for its
// design id gets that design's key (species::design of the species line, docs/01-architecture.md#arch-species-line:
// the same shared key 0x7B...... on every game), checked four times a second, at once after a purchase, at a change
// of the owner's line (species::on_change) and at the end of every Civ_SetDesign (which re-models a whole kind); one whose
// owner has none (the host, a guest without a design for it, a gift to such a player, creature.mode host) keeps or
// gets back the key it had (the culture's). While a save is written the culture key is in +0xC4 (the savestrip hooks:
// the host's save keeps the host's designs only, owner decision of 2026-11-18; an object that shows a line key without
// a record of ours gets the culture set's key for its design id, species::is_line_key); the session's end puts it back.
// `coop.fix civ.own_designs off`: every object the culture's design (0.7.15).
#include "civ_internal.h"
#include "designs.h"
#include "savestrip.h"
#include "species_line.h"

namespace civ {

static bool g_fix = true;
COOP_FIX("civ.own_designs", "BUG-1274", g_fix,
         "civ: a player's vehicles and buildings show that player's own designs (the host's save keeps the culture's)");

// The design key a player's objects of a design id take here, {} = the culture's (the host, no own design, creature.mode
// host: species::design answers {} for all of them)
static creations::Key line_design(uint8_t pid, uint32_t did)
{
    if (pid >= kShared)
        return {};
    return species::design(pid, did);
}

struct OwnRec {
    uint32_t serial = 0;    // the object's cGameData::mID when it was re-keyed (a reused address is another object)
    uint32_t orig[3] = {};  // the culture's key (what the game gave it), written back for saves and at the end
    uint32_t ours = 0;      // the instance of the player's design it shows
    uint32_t ours_key[3] = {}; // its whole key (a culture key of zeros has no type and group to reuse)
    bool building = false;
};
static std::unordered_map<void*, OwnRec> g_own;
static unsigned g_rekeys, g_back, g_overwritten, g_saves, g_after_set, g_set_calls;
static double g_sweep_at = -100;
static bool g_in_save;
// a change of a player's line (a design set or cleared, its records registered here, own / host mode): sweep now
static bool g_line_reg = species::on_change("civ.own_designs", [](uint8_t) { g_sweep_at = -100; });
// a save: objects showing a line key without a record of ours (stripped to the culture set's key, put back after)
struct SaveKept {
    void* o;
    uint32_t serial;
    uint32_t key[3];
};
static std::vector<SaveKept> g_save_kept;
static unsigned g_save_unknown, g_save_filtered;

using SetDesign_t = void(SK_THISCALL*)(void*, uint32_t, const uint32_t*);
static SetDesign_t o_SetDesign;

static uint32_t* model_key(void* o) { return &F<uint32_t>(o, kVModelKey); } // (a building's too: +0x34 +0x90)
static void set_key(void* o, const uint32_t* k) { vcall<void>((uint8_t*)o + kVSpatial, 0x94 / 4, k); }

// the design id cCity::SetBuildingModel (0xBDB940) reads for a building, by its noun; 0 = not a design
static uint32_t building_did(void* b)
{
    switch (game::noun_id(b)) {
    case 0x018EB106: return 0x4E3F7777; // house
    case 0x018EA2CC: return 0x47C10953; // factory
    case 0x01A56ABA: return 0x72C49181; // entertainment
    case 0x018EA1EB: return 0x99E92F05; // City Hall
    }
    return 0;
}
static uint32_t vehicle_did(void* v) { return (uint32_t)skg::cdecl_call<int>(a_DesignId, F<int>(v, kVPurpose), F<int>(v, kVLoco)); }

// the player civilization's vehicles (its list +0xB0, as civ::redesign_vehicles) that the game lists alive
static std::vector<void*> player_vehicles()
{
    std::vector<void*> out;
    void* pc = nm() ? player_civ() : nullptr;
    if (!pc)
        return out;
    std::vector<void*> mine = vec_of((uint8_t*)pc + 0xB0);
    std::unordered_set<void*> civ_list(mine.begin(), mine.end());
    for (void* v : all_vehicles()) // (the game's list: alive, not dead)
        if (civ_list.count(v))
            out.push_back(v);
    return out;
}

bool own_designs_holds(void* o)
{
    auto it = g_own.find(o);
    return it != g_own.end() && model_key(o)[0] == it->second.ours;
}

// BUG-1274 (lab 20261119-133307 line_stages): a vehicle of a design id the culture has no design for (a Civ game started
// from Tribe: only the capital's specialty has a vehicle design; a captured city of another specialty) is made with the
// key {0, 0, 0} (cVehicle::Load's SetModelKey with it: no model, a placeholder's 10 hit points). Its owner's own design
// applies too, its culture key is that zero key. `coop.fix civ.own_designs_nodesign off`: such a vehicle is left alone.
static bool g_fix_nodesign = true;
COOP_FIX("civ.own_designs_nodesign", "BUG-1274", g_fix_nodesign,
         "civ: a player's vehicle of a kind its culture has no design for yet shows that player's own design");
static bool zero_key(const uint32_t* k) { return !k[0] && !k[1] && !k[2]; }

// one object: its owner's design, or back to the culture's
static void reconcile(void* o, uint8_t owner, uint32_t did, bool building)
{
    uint32_t* mk = model_key(o);
    bool none = !building && g_fix_nodesign && zero_key(mk); // (no culture design of that id, see above)
    if (!none && (!mk[0] || mk[0] == 0xFFFFFFFFu || mk[1] != (building ? kBuildingModelType : 0x24682294u)))
        return; // no model yet, or the game's generated design of an unknown kind
    creations::Key w = g_fix && did ? line_design(owner, did) : creations::Key{};
    uint32_t serial = game::object_serial(o);
    auto it = g_own.find(o);
    if (it != g_own.end() && it->second.serial != serial) {
        g_own.erase(it);
        it = g_own.end();
    }
    if (!w.instance) {
        if (it == g_own.end())
            return;
        if (mk[0] == it->second.ours) {
            set_key(o, it->second.orig); // a gift to a player without a design, the switch, host mode
            g_back++;
        }
        g_own.erase(it);
        return;
    }
    if (it != g_own.end() && mk[0] == it->second.ours && it->second.ours == w.instance)
        return; // shows it
    OwnRec r;
    if (it != g_own.end() && mk[0] == it->second.ours)
        r = it->second; // another design of the owner (or another owner's): the culture key stays
    else {
        if (it != g_own.end())
            g_overwritten++; // the game re-modelled it (Civ_SetDesign, a load): its new culture key
        r.serial = serial;
        r.building = building;
        memcpy(r.orig, mk, 12);
        auto hb = building ? g_bld_rekeyed.find(o) : g_bld_rekeyed.end();
        if (hb != g_bld_rekeyed.end() && hb->second.ours == mk[0]) // a guest: the host's design under a key of ours
            memcpy(r.orig, hb->second.orig, 12);                     // (BUG-0670): its culture key is the host's
    }
    uint32_t k[3] = {w.instance, w.type, w.group};
    set_key(o, k);
    r.ours = w.instance;
    memcpy(r.ours_key, k, 12);
    g_own[o] = r;
    g_rekeys++;
    H->log(M, SK_INFO, "civ: player %u's %s %p (design %08X) shows its own design %08X (culture %08X, BUG-1274)", owner,
           building ? "building" : "vehicle", o, did, w.instance, r.orig[0]);
}

// every vehicle of the player civilization and every building of a player's city
static void sweep()
{
    if (!g_ok || g_in_save || game::mode_id() != modes::kCiv || !nm() || !(is_host() || is_guest()))
        return;
    std::unordered_set<void*> seen; // the live objects (a record of one left alone now stays: its save strip)
    for (void* v : player_vehicles()) {
        seen.insert(v);
        uint8_t o = owner_of_vehicle(v);
        if (planner_moves(v) || (o == 0xFF && !g_own.count(v)))
            continue; // moved by the planner, or not in the tables (yet)
        reconcile(v, o, vehicle_did(v), false);
    }
    for (void* city : all_cities()) {
        uint8_t o = owner_of_city(city);
        bool skip = o == 0xFF || o == 0xFE || planner_holds_city(city);
        for (void* b : vec_of((uint8_t*)city + kCityBuildings)) {
            if (!b)
                continue;
            seen.insert(b);
            if (!skip && F<int>(b, kBuildingState) != 2)
                reconcile(b, o, building_did(b), true);
        }
    }
    for (auto it = g_own.begin(); it != g_own.end();) // gone: forgotten, never read
        it = seen.count(it->first) ? std::next(it) : g_own.erase(it);
}

void own_designs_tick(double now)
{
    if (now - g_sweep_at < 0.25)
        return;
    g_sweep_at = now;
    sweep();
}
void own_designs_now() // a purchase, a building (the owner is known now)
{
    sweep();
}

// the end of the game's setter: it re-modelled every vehicle of a locomotion and purpose (or the buildings of a type)
// with the culture's key; the players' own designs go back on in the same call
static void SK_THISCALL h_SetDesign(void* civ, uint32_t id, const uint32_t* key)
{
    o_SetDesign(civ, id, key);
    g_set_calls++;
    if (!g_fix || g_own.empty())
        return;
    unsigned before = g_rekeys;
    sweep();
    g_after_set += g_rekeys - before;
}

// the culture's keys while a save is written (raw: the save writes the key, no model is rebuilt) / ours again
void own_designs_save(bool start)
{
    g_in_save = start; // (no sweep while the game writes)
    bool any = !g_own.empty() || !g_save_kept.empty() ||
               (start && game::mode_id() == modes::kCiv && !species::players().empty());
    if (!any || !nm()) {
        g_save_kept.clear();
        return;
    }
    auto each = [&](void* o, uint32_t did) {
        uint32_t* mk = model_key(o);
        if (!start) // (one stripped without a record: its line key back, the live objects only)
            for (auto& k : g_save_kept)
                if (k.o == o && game::object_serial(o) == k.serial)
                    memcpy(mk, k.key, 12);
        auto it = g_own.find(o);
        if (it == g_own.end() || game::object_serial(o) != it->second.serial) {
            // a line key without a record of ours (none should exist: a guard that the host's save holds no key of a
            // line): the culture set's key of its design id
            uint32_t ck[3];
            if (start && mk[0] && species::is_line_key({mk[0], mk[1], mk[2]}) && did && designs::culture_key(did, ck)) {
                g_save_kept.push_back({o, game::object_serial(o), {mk[0], mk[1], mk[2]}});
                memcpy(mk, ck, 12);
                g_save_unknown++;
                g_saves++;
            }
            return;
        }
        if (start && mk[0] == it->second.ours) {
            memcpy(mk, it->second.orig, 12);
            g_saves++;
        } else if (!start && mk[0] == it->second.orig[0] && mk[1] == it->second.orig[1]) {
            memcpy(mk, it->second.ours_key, 12); // (a zero culture key: the type and group too)
        }
    };
    for (void* v : player_vehicles())
        each(v, vehicle_did(v));
    for (void* city : all_cities())
        for (void* b : vec_of((uint8_t*)city + kCityBuildings))
            if (b)
                each(b, building_did(b));
    if (start)
        H->log(M, SK_INFO, "civ: own designs: %u objects carry the culture's key in saves so far, %u without a record, "
               "%u keys filtered (%zu records, BUG-1274)", g_saves, g_save_unknown, g_save_filtered, g_own.size());
    if (!start)
        g_save_kept.clear();
}

// the session ends: every object of ours the culture's design again (this game plays on alone)
void own_designs_session_end()
{
    g_in_save = false;
    if (g_own.empty() || !nm())
        return;
    auto each = [&](void* o) {
        auto it = g_own.find(o);
        if (it != g_own.end() && game::object_serial(o) == it->second.serial && model_key(o)[0] == it->second.ours) {
            set_key(o, it->second.orig);
            g_back++;
        }
    };
    for (void* v : player_vehicles())
        each(v);
    for (void* city : all_cities())
        for (void* b : vec_of((uint8_t*)city + kCityBuildings))
            if (b)
                each(b);
    g_own.clear();
}
static void own_reset()
{
    g_own.clear();
    g_save_kept.clear();
    g_in_save = false;
}
static size_t own_held() { return g_own.size(); }
WORLD_STATE("civ.own_designs", &g_fix, own_reset, own_held);

// BUG-1274 (lab 20261119-140235 civ_own_designs): with every object stripped, the host's save still named the guest's
// vehicle design key in its main stream (0x01897C18) and the cPlayer record (0x02C21781), and its house design key in a
// house record (0x018EB106): the game keeps those keys elsewhere too. Every key written into a save goes through the
// savestrip key filters (BUG-1315, Stream_WriteResourceKey and dword blocks): a design key of a line becomes the culture
// set's key of that design id (the first id the line names it for; none: the key of none, as a kind without a design).
static bool save_key_filter(uint32_t k[3])
{
    if (!g_fix || !k[0] || (k[1] != 0x24682294u && k[1] != kBuildingModelType))
        return false;
    for (uint8_t pid : species::players())
        for (uint32_t did : species::design_ids(pid)) {
            creations::Key w = species::design(pid, did);
            if (w.instance != k[0] || w.type != k[1] || w.group != k[2])
                continue;
            uint32_t ck[3] = {};
            if (!designs::culture_key(did, ck))
                memset(ck, 0, sizeof ck);
            memcpy(k, ck, sizeof ck);
            g_save_filtered++;
            return true;
        }
    if (!species::is_line_key({k[0], k[1], k[2]}))
        return false;
    for (auto& [o, r] : g_own) // (a line key no player names now: an object's record of it)
        if (r.ours == k[0]) {
            memcpy(k, r.orig, 12);
            g_save_filtered++;
            return true;
        }
    k[0] = k[1] = k[2] = 0;
    g_save_filtered++;
    return true;
}

void own_designs_init()
{
    savestrip::add_key_filter(save_key_filter);
    if (uintptr_t a = H->sym("SporeCoop::Civ_SetDesign"))
        H->hook_create(M, (void*)a, (void*)h_SetDesign, (void**)&o_SetDesign);
}

// tests: a vehicle spawned with a design key the game's own way (cVehicle::Load builds its model and stats from the
// key), the reference for the hit points a re-keyed vehicle must have
static void* g_ref;
static uint32_t g_ref_serial;

std::string own_designs_debug(const std::vector<std::string>& a)
{
    char b[260];
    if (a.size() >= 3 && a[0] == "set") { // set <hex design id> <hex creation instance | 0>: the lab's setter (a guest):
        // a vehicle or building of its My Creations as its own design, through the species line (species::set_design)
        uint32_t did = (uint32_t)strtoul(a[1].c_str(), nullptr, 16), inst = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        bool bld = did == 0x99E92F05u || did == 0x4E3F7777u || did == 0x47C10953u || did == 0x72C49181u;
        creations::Key k = inst ? creations::Key{inst, bld ? kBuildingModelType : 0x24682294u, bld ? 0x40636200u : 0x40646200u}
                                : creations::Key{};
        if (!species::set_design(did, k))
            return "not set: refused (a guest in own mode only, a creation with its model on this game; coop.species)\n";
        snprintf(b, sizeof b, "own design %08X for %08X set (the species line sends it to the host)\n", inst, did);
        return b;
    }
    if (a.size() >= 3 && a[0] == "did") { // did <host city index> <loco> [purpose]: the design id a purchase there
        int ci = atoi(a[1].c_str());        // makes (or that purpose's)
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city && a.size() < 4)
            return "no such city\n";
        int purpose = a.size() >= 4 ? atoi(a[3].c_str()) : F<int>(city, kCitySpecialty);
        snprintf(b, sizeof b, "did %08X purpose=%d loco=%d\n",
                 (uint32_t)skg::cdecl_call<int>(a_DesignId, purpose, atoi(a[2].c_str())), purpose, atoi(a[2].c_str()));
        return b;
    }
    if (a.size() >= 4 && a[0] == "hpref" && is_host()) { // hpref <purpose> <loco> <hex inst>: a reference vehicle
        void* city = g_host_cities.empty() ? nullptr : g_host_cities[0];
        void* civ = player_civ();
        if (!city || !civ)
            return "no city\n";
        uint32_t inst = (uint32_t)strtoul(a[3].c_str(), nullptr, 16);
        g_ref = thiscall<void*>(a_SpawnVehicle, city, atoi(a[1].c_str()), atoi(a[2].c_str()), inst, 0x24682294u,
                                0x40646200u, 0);
        g_ref_serial = g_ref ? game::object_serial(g_ref) : 0;
        snprintf(b, sizeof b, "reference %p (slots left %d)\n", g_ref, thiscall<int>(a_SlotsLeft, civ, atoi(a[1].c_str())));
        return b;
    }
    if (!a.empty() && a[0] == "hpref" && is_host()) { // hpref: the reference's hit points, then it is removed
        void* v = g_ref;
        g_ref = nullptr;
        if (!v || !exists(v) || game::object_serial(v) != g_ref_serial)
            return "no reference\n";
        snprintf(b, sizeof b, "reference model=%08X hp=%.1f max=%.1f\n", model_key(v)[0], F<float>(v, kVHp),
                 F<float>(v, 0x534));
        if (a_VehicleRemove)
            thiscall<void>(a_VehicleRemove, v, 0); // no refund
        return b;
    }
    // the list: every vehicle of the player civilization and every building of a player's city
    std::string out;
    for (void* v : player_vehicles()) {
        uint16_t id = 0;
        if (is_host()) {
            HvRef h = hv_find(v);
            id = h ? h.id : 0;
        } else {
            GvRef g = gv_of(v);
            id = g ? g.id : 0;
        }
        auto it = g_own.find(v);
        uint8_t o = owner_of_vehicle(v);
        snprintf(b, sizeof b, "  ownveh #%u %p owner=%d purpose=%d loco=%d did=%08X model=%08X culture=%08X hp=%.1f max=%.1f\n",
                 id, v, o == 0xFF ? -1 : (int)o, F<int>(v, kVPurpose), F<int>(v, kVLoco), vehicle_did(v), model_key(v)[0],
                 it == g_own.end() ? 0u : it->second.orig[0], F<float>(v, kVHp), F<float>(v, 0x534));
        out += b;
    }
    int ci = 0;
    for (void* city : nm() ? all_cities() : std::vector<void*>{}) {
        uint8_t o = owner_of_city(city);
        int hi = host_index_of_city(city);
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings)) {
            if (!bl || !building_did(bl))
                continue;
            auto it = g_own.find(bl);
            net::Vec3 p{};
            game::object_position(bl, p);
            snprintf(b, sizeof b, "  ownbld city=%d host=%d owner=%d %p noun=%08X did=%08X model=%08X culture=%08X pos=(%.1f %.1f %.1f)\n",
                     ci, hi, o == 0xFF ? -1 : (int)o, bl, game::noun_id(bl), building_did(bl), model_key(bl)[0],
                     it == g_own.end() ? 0u : it->second.orig[0], p.x, p.y, p.z);
            out += b;
        }
        ci++;
    }
    snprintf(b, sizeof b,
             "own designs: fix=%d rekeys=%u back=%u overwritten=%u after_setdesign=%u setdesign_calls=%u saves=%u "
             "save_unknown=%u save_filtered=%u now=%zu\n",
             (int)g_fix, g_rekeys, g_back, g_overwritten, g_after_set, g_set_calls, g_saves, g_save_unknown,
             g_save_filtered, g_own.size());
    out += b;
    // the players' designs as the species line answers them here (the key every game builds them from)
    for (uint8_t pid : species::players())
        for (uint32_t did : species::design_ids(pid)) {
            creations::Key k = species::design(pid, did);
            snprintf(b, sizeof b, "  pdesign player=%u id=%08X orig=%08X ours=%08X/%08X/%08X\n", pid, did,
                     species::own_design(did).instance, k.instance, k.type, k.group);
            out += b;
        }
    return out;
}

} // namespace civ
