// Space feature "eco" (MSG_SPACE kind 90): a planet's ecosystem, the plant and animal species placed on it, is the
// same in every game (BUG-1148, docs/re/space.md#space-ecosystem).
//
// The game keeps a planet's species in its record: cPlanetRecord +0xBC (plants) and +0xD0 (animals), vectors of
// species keys. The food web of the planet is read from them (row n = the n-th plant of each size class, the
// (2n-1)-th / 2n-th herbivore, the n-th carnivore), and so is the "stabilized" level: the highest T level up to the
// planet's T score with 3 animals per level (SporeCoop::PlanetRecord_EcoLevel 0xBBCFB0, which also decides how many
// colony cities the planet holds). A cargo drop that lands adds its species through PlanetEco_AddPlant 0xC81C50 /
// PlanetEco_AddAnimal 0xC82590 (thiscall on the planet's terrain data, the player flag set). Each game kept its own
// lists: a guest's planet with a full food web was empty and not stabilized on the host (no message carried them).
//
// Now, after the player's own placement (the two functions with the player flag), the planet's lists go to every
// player while they change (K_ECO, at most once a second, until 30 s after the last placement), with the drop
// position of each species. A player on that planet adds the missing species through the same game functions (the
// grid cell, the plants, and a herd of each new animal species, as the game makes them when a planet is entered); a
// player elsewhere sets its record's lists to them, so the planet is populated and stabilized when it arrives. A
// planet this game has not generated yet (or a message outside Space) waits and is applied when the planet exists
// (BUG-1155). The host keeps the latest lists of each planet and sends the ones of a star system to a player who
// enters it, and all of them to a player who joins. `coop.fix space.eco_sync off`: each game keeps its own (0.7.14).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "call_guard.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "space_ext.h"
#include "sporekit_game.hpp"
#include "wire/space_eco.h"

using skg::cdecl_call;
using skg::thiscall;

namespace spaceeco {

enum Kind : uint8_t {
    K_ECO = 90, // any: a planet's plant and animal species (wire/space_eco.h)
};

namespace we = wire::space_eco;

static const SkHost* H;
static SkMod* M;
static uintptr_t a_ActivePlanet, a_StarMgr, a_GetPlanetRec, a_AddPlant, a_AddAnimal, a_EcoLevel, a_Insert, a_SpawnHerd,
    a_HerdMgr, a_TScore, a_PlayerUFOGet, a_PlantMgr, a_PlantFind, a_PlantClass;

static bool g_fix = true;
COOP_FIX("space.eco_sync", "BUG-1148", g_fix,
         "space: the plants and animals placed on a planet (its ecosystem and stabilized level) are the same in every game");
// the herd of a partner's new animal species on a planet this game is on (the game's own planet-entry spawn); tests
static bool g_spawn = true;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

constexpr double kTick = 0.5, kSendGap = 1.0, kDirtyFor = 30.0;
constexpr size_t kKeptMax = 64, kPendingMax = 128;
constexpr double kOwedAfter = 3.0; // s on the planet before an owed herd (the planet entry made its own)

static bool g_applying;
CALL_GUARD("space_eco.applying", g_applying);

// ---- state ----
static uint32_t g_dirty_place;                  // PlanetID | 0x40000000 of the own placement
static double g_dirty_until, g_last_send, g_last_tick;
static std::vector<we::Species> g_sent_p, g_sent_a; // what went out last for g_dirty_place
static std::map<std::string, std::array<float, 3>> g_pos; // species key -> where the own drop put it
static std::map<uint32_t, we::Eco> g_kept;     // host: the latest lists of each planet (PlanetID)
static std::map<uint32_t, double> g_kept_t;
static std::map<uint32_t, we::Eco> g_pending;  // lists for a planet not generated here yet / outside Space
static std::map<uint8_t, uint32_t> g_partner_star; // host: the star each partner was last seen in (0 = galaxy)
static std::map<uint32_t, std::vector<we::Species>> g_owed; // planet -> animal species set while away: herds owed
static double g_owed_since;
static uint32_t g_epoch;
static uint32_t g_own, g_tx, g_rx, g_set, g_live_added, g_herds, g_pend, g_pend_applied, g_resent, g_bad;

static std::string kstr(const wire::Key3& k)
{
    char b[32];
    snprintf(b, sizeof b, "%08X:%08X:%08X", k.v[0], k.v[1], k.v[2]);
    return b;
}

static bool valid_key(const uint32_t* k) { return k && (k[0] || k[1] || k[2]); }

// ---- the game's records ----
static void* active_planet()
{
    void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
    return p && !IsBadReadPtr(p, 0x158) ? p : nullptr;
}
static void* record_of(void* planet)
{
    void* r = planet ? F<void*>(planet, 0x13C) : nullptr;
    return r && !IsBadReadPtr(r, 0x1B0) ? r : nullptr;
}
static uint32_t my_place() { return spaceext::ctx().my_place ? spaceext::ctx().my_place() : 0; }

// the record of PlanetID `id` through the game, or null when the game would read past its data (BUG-1042's checks:
// the star of this galaxy, its planets generated here, the index under their count)
static void* record_checked(uint32_t id)
{
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    if (!sm || !a_GetPlanetRec || !spaceext::in_space())
        return nullptr;
    void* star = spacecoop::star_record(id & 0xFFFFFFu);
    uint32_t i = (id >> 24) & 0x3Fu;
    if (!star || IsBadReadPtr(star, 0xB0) || i >= F<uint8_t>(star, 0xAC))
        return nullptr;
    void* r = thiscall<void*>(a_GetPlanetRec, sm, id & 0x3FFFFFFFu);
    return r && !IsBadReadPtr(r, 0x1B0) && F<uint32_t>(r, 0x184) == (id & 0x3FFFFFFFu) ? r : nullptr;
}

// a record's key vector (+0xBC plants, +0xD0 animals): begin / end, null when it does not look like one
static bool vec_of(void* rec, uintptr_t off, uint32_t*& b, uint32_t*& e)
{
    b = F<uint32_t*>(rec, off);
    e = F<uint32_t*>(rec, off + 4);
    if (!b || !e)
        return b == e; // empty
    size_t n = (size_t)((uint8_t*)e - (uint8_t*)b);
    return e >= b && n % 12 == 0 && n / 12 <= 64 && !IsBadReadPtr(b, n ? n : 1);
}
static std::vector<we::Species> list_of(void* rec, uintptr_t off)
{
    std::vector<we::Species> v;
    uint32_t *b, *e;
    if (!rec || !vec_of(rec, off, b, e))
        return v;
    for (uint32_t* k = b; k && k < e && v.size() < we::kMaxSpecies; k += 3) {
        we::Species s;
        memcpy(s.key.v, k, 12);
        auto it = g_pos.find(kstr(s.key));
        if (it != g_pos.end())
            memcpy(s.pos, it->second.data(), 12);
        v.push_back(s);
    }
    return v;
}
static bool same_keys(const std::vector<we::Species>& a, const std::vector<we::Species>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].key != b[i].key)
            return false;
    return true;
}
static bool has(const std::vector<we::Species>& v, const wire::Key3& k)
{
    return std::any_of(v.begin(), v.end(), [&](const we::Species& s) { return s.key == k; });
}

// the record's list becomes `want` (keys, in order): emptied (keys need no destructor) and filled as the game's own
// push_back 0xC73460 does: in place while there is room, else the vector's insert at the end (it grows through the
// game's allocator). KeyVector_InsertValue moves the last element up first, so it is only for a full vector.
static bool set_list(void* rec, uintptr_t off, const std::vector<we::Species>& want)
{
    uint32_t *b, *e;
    if (!a_Insert || !vec_of(rec, off, b, e) || IsBadWritePtr((uint8_t*)rec + off, 12))
        return false;
    uint8_t* v = (uint8_t*)rec + off;
    F<uint32_t*>(v, 4) = b; // end = begin
    for (auto& s : want) {
        uint32_t k[3];
        memcpy(k, s.key.v, 12);
        uint32_t* end = F<uint32_t*>(v, 4);
        uint32_t* cap = F<uint32_t*>(v, 8);
        if (end && end < cap) {
            memcpy(end, k, 12);
            F<uint32_t*>(v, 4) = end + 3;
        } else
            thiscall<void>(a_Insert, v, end, k);
    }
    return true;
}

// a position for the game's grid (it divides by the larger coordinate): the sender's, else the own ship's
static void place_pos(const we::Species& s, float out[3])
{
    if (std::fabs(s.pos[0]) + std::fabs(s.pos[1]) + std::fabs(s.pos[2]) > 1.f) {
        memcpy(out, s.pos, 12);
        return;
    }
    out[0] = 0, out[1] = 0, out[2] = 100.f;
    void* mgr = a_PlayerUFOGet ? cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    void* u = mgr && !IsBadReadPtr(mgr, 0x44) ? F<void*>(mgr, 0x40) : nullptr;
    if (u && !IsBadReadPtr(u, 0x44)) {
        float* p = &F<float>(u, 0x38);
        if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) &&
            std::fabs(p[0]) + std::fabs(p[1]) + std::fabs(p[2]) > 1.f)
            memcpy(out, p, 12);
    }
}

// ---- hooks: the own placement ----
using Add_t = int(SK_THISCALL*)(void*, const uint32_t*, const float*, int);
static Add_t o_AddPlant, o_AddAnimal;
static void note_own(void* terrain, const uint32_t* key, const float* pos, int player)
{
    if (g_applying || !(player & 0xFF) || !g_fix || !spaceext::in_space() || !spaceext::connected() || !valid_key(key))
        return;
    void* p = active_planet();
    if (!p || F<void*>(p, 0x154) != terrain)
        return;
    uint32_t place = my_place();
    if (!(place & 0x40000000u))
        return;
    wire::Key3 k;
    memcpy(k.v, key, 12);
    if (pos && std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]))
        g_pos[kstr(k)] = {pos[0], pos[1], pos[2]};
    if (g_dirty_place != place)
        g_sent_p.clear(), g_sent_a.clear();
    g_dirty_place = place;
    g_dirty_until = H->time_seconds() + kDirtyFor;
    g_own++;
}
static int SK_THISCALL h_AddPlant(void* t, const uint32_t* key, const float* pos, int player)
{
    int r = o_AddPlant(t, key, pos, player);
    note_own(t, key, pos, player);
    return r;
}
static int SK_THISCALL h_AddAnimal(void* t, const uint32_t* key, const float* pos, int player)
{
    int r = o_AddAnimal(t, key, pos, player);
    note_own(t, key, pos, player);
    return r;
}

// ---- send ----
static void keep(const we::Eco& m)
{
    if (!spaceext::is_host())
        return;
    g_kept[m.planet] = m;
    g_kept_t[m.planet] = H->time_seconds();
    while (g_kept.size() > kKeptMax) { // the oldest goes
        auto old = std::min_element(g_kept_t.begin(), g_kept_t.end(),
                                    [](auto& a, auto& b) { return a.second < b.second; });
        g_kept.erase(old->first);
        g_kept_t.erase(old);
    }
}
static void send_eco(const we::Eco& m)
{
    net::Writer w;
    m.write(w);
    spaceext::send(K_ECO, w);
    g_tx++;
}

static void send_tick(double now)
{
    if (!g_dirty_place || now > g_dirty_until || now - g_last_send < kSendGap)
        return;
    if (my_place() != g_dirty_place)
        return;
    void* rec = record_of(active_planet());
    if (!rec || F<uint32_t>(rec, 0x184) != (g_dirty_place & 0x3FFFFFFFu))
        return;
    we::Eco m;
    m.planet = g_dirty_place & 0x3FFFFFFFu;
    m.plants = list_of(rec, 0xBC);
    m.animals = list_of(rec, 0xD0);
    if (same_keys(m.plants, g_sent_p) && same_keys(m.animals, g_sent_a))
        return;
    send_eco(m);
    keep(m);
    g_sent_p = m.plants;
    g_sent_a = m.animals;
    g_last_send = now;
    g_dirty_until = now + kDirtyFor;
}

// the game's creatures of an animal species on the planet this game is on (the cCreatureAnimal noun list; puppets
// and dead ones not counted): what the player sees of that species
static int creatures_of(const wire::Key3& k)
{
    struct U {
        uint32_t inst;
        int n;
    } u{k.v[0], 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto* u = (U*)up;
            if (noun == 0x18EB45Eu && !game::is_destroyed(o) && !game::is_dead(o) && !game::is_puppet(o) &&
                game::species_instance(o) == u->inst)
                u->n++;
            return true;
        },
        &u);
    return u.n;
}

// a herd of a partner's animal species where it placed it (the game's own spawn of the planet entry); counted
static void spawn_herd(const we::Species& sp)
{
    if (!g_spawn || !a_SpawnHerd || !a_HerdMgr)
        return;
    void* mgr = cdecl_call<void*>(a_HerdMgr);
    if (!mgr)
        return;
    float pos[3];
    place_pos(sp, pos);
    uint32_t k[3];
    memcpy(k, sp.key.v, 12);
    callguard::Hold applying(g_applying);
    thiscall<void>(a_SpawnHerd, mgr, k, pos);
    g_herds++;
}

// ---- apply ----
// true: applied (or nothing to do); false: not possible now (pending)
static bool try_apply(const we::Eco& m)
{
    if (!spaceext::in_space())
        return false;
    void* p = active_planet();
    void* rec = record_of(p);
    if (rec && my_place() == (m.planet | 0x40000000u) && F<uint32_t>(rec, 0x184) == m.planet) {
        // on that planet: the missing species through the game's own functions (no removal here: a species placed
        // here a moment ago may not have reached the partner yet; dying out is the game's rule on both sides)
        void* terrain = F<void*>(p, 0x154);
        if (!terrain || IsBadReadPtr(terrain, 0x2034) || !a_AddPlant || !a_AddAnimal)
            return false;
        auto have_p = list_of(rec, 0xBC), have_a = list_of(rec, 0xD0);
        callguard::Hold applying(g_applying);
        for (int animal = 0; animal < 2; animal++)
            for (auto& s : animal ? m.animals : m.plants) {
                if (has(animal ? have_a : have_p, s.key))
                    continue;
                float pos[3];
                place_pos(s, pos);
                uint32_t k[3];
                memcpy(k, s.key.v, 12);
                thiscall<int>(animal ? a_AddAnimal : a_AddPlant, terrain, k, pos, 0);
                g_live_added++;
                if (animal && creatures_of(s.key) == 0)
                    spawn_herd(s);
            }
        return true;
    }
    rec = record_checked(m.planet);
    if (!rec)
        return false;
    auto had = list_of(rec, 0xD0);
    if (same_keys(list_of(rec, 0xBC), m.plants) && same_keys(had, m.animals))
        return true;
    if (set_list(rec, 0xBC, m.plants) && set_list(rec, 0xD0, m.animals))
        g_set++;
    // the game makes no herd of a species it did not see placed (the planet entry 0xFF96B0 spawns only planet objects
    // and, with 2 % chance, one random species): the new ones get a herd when this game is on that planet
    auto& owed = g_owed[m.planet];
    for (auto& a : m.animals)
        if (!has(had, a.key) && !has(owed, a.key))
            owed.push_back(a);
    return true;
}

// the herds owed on the planet this game has been on for a few seconds (BUG-1148)
static void owed_tick(double now)
{
    uint32_t place = my_place();
    void* p = active_planet();
    void* rec = record_of(p);
    if (!(place & 0x40000000u) || !rec || F<uint32_t>(rec, 0x184) != (place & 0x3FFFFFFFu) || !F<void*>(p, 0x154)) {
        g_owed_since = 0;
        return;
    }
    auto it = g_owed.find(place & 0x3FFFFFFFu);
    if (it == g_owed.end())
        return;
    if (!g_owed_since)
        g_owed_since = now;
    if (now - g_owed_since < kOwedAfter)
        return;
    auto list = list_of(rec, 0xD0); // still one of the planet's species (it may have died out since)
    for (auto& a : it->second)
        if (has(list, a.key) && creatures_of(a.key) == 0)
            spawn_herd(a);
    g_owed.erase(it);
}

static void on_eco(const we::Eco& m)
{
    g_rx++;
    if (!g_fix)
        return;
    keep(m);
    if (try_apply(m))
        g_pending.erase(m.planet);
    else if (g_pending.count(m.planet) || g_pending.size() < kPendingMax) {
        g_pending[m.planet] = m;
        g_pend++;
    }
}

static void on_message(uint8_t kind, uint8_t, net::Reader& r)
{
    if (kind != K_ECO)
        return;
    we::Eco m;
    if (!m.read(r) || !(m.planet & 0xFFFFFFu)) {
        g_bad++;
        return;
    }
    on_eco(m);
}

// host: what a partner that joined or entered a star system has to know
static void resend_tick()
{
    if (!spaceext::is_host() || !spaceext::ctx().partners)
        return;
    std::map<uint8_t, uint32_t> now;
    for (auto& p : spaceext::ctx().partners()) {
        uint32_t s = (p.place & 0xC0000000u) ? (p.place & 0xFFFFFFu) : 0;
        now[p.id] = s;
        auto it = g_partner_star.find(p.id);
        bool joined = it == g_partner_star.end();
        if (!joined && it->second == s)
            continue;
        for (auto& [id, m] : g_kept)
            if (joined || (s && (id & 0xFFFFFFu) == s)) {
                send_eco(m);
                g_resent++;
            }
    }
    g_partner_star = now;
}

static void clear_all()
{
    g_dirty_place = 0;
    g_sent_p.clear();
    g_sent_a.clear();
    g_pos.clear();
    g_kept.clear();
    g_kept_t.clear();
    g_pending.clear();
    g_partner_star.clear();
    g_owed.clear();
    g_owed_since = 0;
}

static void tick(double now)
{
    if (now - g_last_tick < kTick)
        return;
    g_last_tick = now;
    uint32_t ep = spaceext::world_epoch();
    if (ep != g_epoch) { // another Space world: its planets are other planets
        clear_all();
        g_epoch = ep;
    }
    if (!g_fix || !spaceext::connected())
        return;
    send_tick(now);
    for (auto it = g_pending.begin(); it != g_pending.end();)
        if (try_apply(it->second)) {
            g_pend_applied++;
            it = g_pending.erase(it);
        } else
            ++it;
    resend_tick();
    if (spaceext::in_space())
        owed_tick(now);
}

// a session's lists (mod state only): they belong to the session that sent them
SESSION_STATE("space_eco", nullptr, clear_all, []() -> size_t {
    return g_kept.size() + g_pending.size() + g_partner_star.size() + g_owed.size() + (g_dirty_place ? 1 : 0);
});

static std::string status()
{
    char b[320];
    snprintf(b, sizeof b,
             "eco: fix=%d own=%u tx=%u rx=%u set=%u live_added=%u herds=%u pending=%zu pend=%u pend_applied=%u kept=%zu "
             "resent=%u bad=%u",
             (int)g_fix, g_own, g_tx, g_rx, g_set, g_live_added, g_herds, g_pending.size(), g_pend, g_pend_applied,
             g_kept.size(), g_resent, g_bad);
    return b;
}

// a species' food web column: a plant's size class (PlantCargo_SizeClass: 6D60A1CC, 029C388A, 3A8BE428), an animal's
// diet (species profile +0x608: carnivore)
static std::string kind_of(bool animal, const wire::Key3& k)
{
    char b[16];
    if (animal) {
        void* prof = game::species_by_key({k.v[0], k.v[1], k.v[2]});
        return prof && !IsBadReadPtr(prof, 0x60C) ? (F<int>(prof, 0x608) ? "/c" : "/h") : "/?";
    }
    void* mgr = a_PlantMgr ? cdecl_call<void*>(a_PlantMgr) : nullptr;
    uint32_t kk[3] = {k.v[0], k.v[1], k.v[2]};
    void* item = mgr && a_PlantFind ? thiscall<void*>(a_PlantFind, mgr, kk) : nullptr;
    snprintf(b, sizeof b, "/%08X", item && a_PlantClass ? thiscall<uint32_t>(a_PlantClass, item) : 0u);
    return b;
}

static std::string dump(void* rec)
{
    char b[160];
    float b0 = F<float>(rec, 0xB0), b4 = F<float>(rec, 0xB4);
    int ts = a_TScore ? cdecl_call<int>(a_TScore, b0, b4) : -1;
    int eco = a_EcoLevel ? thiscall<int>(a_EcoLevel, nullptr, rec) : -1;
    snprintf(b, sizeof b, "planet %08X type=%d b0=%.3f b4=%.3f tscore=%d eco=%d flags=%X\n", F<uint32_t>(rec, 0x184),
             F<int>(rec, 0x28), b0, b4, ts, eco, F<uint32_t>(rec, 0x2C));
    std::string s = b;
    // the record's planet objects (cPlanetObjectData, 0x2C bytes: +0 type, +4 position, +0x12 artifact type, +0x1C key,
    // +0x28 flags; flag 4: an animal the planet entry 0xFF96B0 makes a herd of)
    uint8_t* ob = F<uint8_t*>(rec, 0x134);
    uint8_t* oe = F<uint8_t*>(rec, 0x138);
    if (ob && oe > ob && (oe - ob) % 0x2C == 0 && (oe - ob) / 0x2C < 512 && !IsBadReadPtr(ob, oe - ob)) {
        snprintf(b, sizeof b, "objects %d:", (int)((oe - ob) / 0x2C));
        s += b;
        for (uint8_t* o = ob; o < oe && o < ob + 0x2C * 24; o += 0x2C) {
            snprintf(b, sizeof b, " [t%X f%02X a%u %08X]", F<uint32_t>(o, 0), F<uint8_t>(o, 0x28), F<uint8_t>(o, 0x12),
                     F<uint32_t>(o, 0x1C));
            s += b;
        }
        s += "\n";
    }
    for (int animal = 0; animal < 2; animal++) {
        auto v = list_of(rec, animal ? 0xD0 : 0xBC);
        snprintf(b, sizeof b, "%s %zu:", animal ? "animals" : "plants", v.size());
        s += b;
        bool here = animal && rec == record_of(active_planet());
        for (auto& x : v)
            s += " " + kstr(x.key) + kind_of(animal, x.key) + (here ? "#" + std::to_string(creatures_of(x.key)) : "");
        s += "\n";
    }
    return s;
}

static std::string debug(const std::vector<std::string>& a)
{
    // coop.space eco [PlanetID hex]: the planet's record (T score, stabilized level, species); the active one without
    // coop.space eco place <plant|animal> <i> <PlanetID>: tests: the i-th species of that planet's list is placed on the
    //   active planet at the own ship, through the game's own function with the player flag (as a landed cargo drop)
    // coop.space eco spawn 0|1: tests: a partner's new animal species also gets a herd here
    char b[200];
    if (a.size() > 1 && a[0] == "spawn") {
        g_spawn = atoi(a[1].c_str()) != 0;
        return std::string("eco: spawn ") + (g_spawn ? "on" : "off") + "\n";
    }
    if (a.size() > 3 && a[0] == "place") {
        bool animal = a[1] == "animal";
        void* src = record_checked((uint32_t)strtoul(a[3].c_str(), nullptr, 16));
        void* p = active_planet();
        void* terrain = p ? F<void*>(p, 0x154) : nullptr;
        if (!src || !terrain)
            return "eco: no such planet record here, or no active planet\n";
        auto v = list_of(src, animal ? 0xD0 : 0xBC);
        size_t i = (size_t)atoi(a[2].c_str());
        if (i >= v.size())
            return "eco: the source planet has no such species\n";
        we::Species s = v[i];
        memset(s.pos, 0, sizeof s.pos);
        float pos[3];
        place_pos(s, pos);
        uint32_t k[3];
        memcpy(k, s.key.v, 12);
        int r = thiscall<int>(animal ? a_AddAnimal : a_AddPlant, terrain, k, pos, 1);
        snprintf(b, sizeof b, "eco: placed %s %s at %.0f %.0f %.0f (%d)\n", animal ? "animal" : "plant",
                 kstr(s.key).c_str(), pos[0], pos[1], pos[2], r);
        return b;
    }
    void* rec = nullptr;
    if (!a.empty())
        rec = record_checked((uint32_t)strtoul(a[0].c_str(), nullptr, 16));
    else
        rec = record_of(active_planet());
    std::string s = status() + "\n";
    return rec ? s + dump(rec) : s + "no planet record\n";
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_ActivePlanet = S("SporeCoop::Space_GetActivePlanet");
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_GetPlanetRec = S("Simulator::cStarManager::GetPlanetRecord");
    a_AddPlant = S("SporeCoop::PlanetEco_AddPlant");
    a_AddAnimal = S("SporeCoop::PlanetEco_AddAnimal");
    a_EcoLevel = S("SporeCoop::PlanetRecord_EcoLevel");
    a_Insert = S("SporeCoop::KeyVector_InsertValue");
    a_SpawnHerd = S("SporeCoop::PlanetEco_SpawnHerd");
    a_HerdMgr = S("SporeCoop::PlanetEco_HerdSpawner");
    a_TScore = S("Simulator::cTerraformingManager::CalculateTScore");
    a_PlayerUFOGet = S("Simulator::cSimulatorPlayerUFO::Get");
    a_PlantMgr = S("SporeCoop::PlantCargo_Manager");
    a_PlantFind = S("SporeCoop::PlantCargo_Template");
    a_PlantClass = S("SporeCoop::PlantCargo_SizeClass");
    if (a_AddPlant)
        H->hook_create(M, (void*)a_AddPlant, (void*)h_AddPlant, (void**)&o_AddPlant);
    if (a_AddAnimal)
        H->hook_create(M, (void*)a_AddAnimal, (void*)h_AddAnimal, (void**)&o_AddAnimal);
    return a_ActivePlanet && a_StarMgr && a_GetPlanetRec && o_AddPlant && o_AddAnimal && a_Insert;
}

static bool reg = spaceext::add({.name = "eco",
                                 .kind_lo = 90,
                                 .kind_hi = 94,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .status = status,
                                 .debug = debug});

} // namespace spaceeco
