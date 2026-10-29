// Space feature "allies" (MSG_SPACE kinds 60..69): a partner's colonies as an allied empire in my
// copy of the galaxy (docs/re/space.md 15, docs/09-coop-design.md feature 4).
//
// Every player keeps the colonies of its own save. The colonies of a partner (space_map.h roster)
// appear in my galaxy as cities of a mirror empire "<partner>'s empire": its own political id, a
// cEmpire in the star manager, the stars owned by it (territory on the galaxy map), each colony
// planet at tech level 5 with the game's own record-level colony (cCivData + cCityData, the cities
// are generated when I land), allied with me (at war in rivals mode, spaceext::relation).
//
// Only stars nobody owns in my galaxy are mirrored (mine or an NPC empire's stay as they are: in
// the usual session my galaxy is a copy of the host's save, so the colonies the partner had when I
// joined are my own already). Every change is written down (the old fields of each star and
// planet) and undone before the game saves (hook on the save function) and when the session ends:
// a save never holds the mirror. A partner who leaves stays as a frozen ally until the session
// ends; a rejoin under the same name picks the mirror up again.
//
// Raids: the owner of a raided colony sends its raids (K_RAIDS, polled from the mission list). A
// partner on that planet in its own galaxy gets a raid of its own on the mirror colony (the game's
// `raid -start` code), fights it, and a win (K_DEFENDED) completes the owner's raid.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "game.h"
#include "modes.h"
#include "space_ext.h"
#include "space_logic.h"
#include "space_map.h"
#include "sporekit_game.hpp"
#include "fixes.h"

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

namespace spaceallies {

static const SkHost* H;
static SkMod* M;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }
static bool readable(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }

enum Kind : uint8_t {
    K_RAIDS = 60,     // u8 n, n x {u32 PlanetID, u8 bombers}: the raids on my colonies right now
    K_DEFENDED = 61,  // u8 owner id, u32 PlanetID: I beat the raid on your colony (in my galaxy)
};

// ---- symbols ----
static uintptr_t a_StarMgr, a_NextPol, a_GetEmpire, a_CreateEmpire, a_EmpireInit, a_AddStar, a_SetHomeStar,
    a_AddPlanet, a_SetSpecies, a_GetSpecies, a_PlanetTech, a_FillPlanet, a_MapErase, a_MapAt, a_HomeStarMap,
    a_RelMgr, a_RelMap, a_RelData, a_RelReset, a_AllyAdd, a_AllyRemove, a_EnemyAdd, a_EnemyRemove, a_StrAssign,
    a_Save, a_Expand, a_PlayerEmpire, a_Color, a_NounMgr, a_GetData, a_MissionCb[4], a_SetState, a_StartRaid,
    a_Context, a_ActivePlanet, a_RequirePlanets, a_AllCities, a_GetPlanets, a_AddCivData, a_GetPlayer, a_FlagsAt, a_AllUFOs, a_TakeDamage, a_PlayerUFO;

// ---- what each mirror changed (to undo it exactly) ----
struct StarSnap {
    uint32_t emp, tech, flags, species[3];
};
struct PlanetSnap {
    bool on = false;              // the mirror's colony is in the record now
    void* rec = nullptr;          // the cPlanetRecord, with our reference: the record stays in the
                                  // game's resource cache (the save writes every cached record,
                                  // also those of systems far away) and is restored by pointer
    uint32_t tech, flags;
    size_t civ_before;            // cCivData vector size before the mirror
    std::vector<void*> civ;       // the cCivData the mirror added (kept for the next apply)
};

struct Mirror {
    std::string name;             // the partner's player name (the key: survives a rejoin)
    uint8_t id = 0;
    bool online = true;
    std::vector<uint32_t> wanted; // the partner's colonies (PlanetIDs), from its roster
    uint32_t color = 0;           // ARGB (spacemap::player_color)
    spaceext::Relation rel = spaceext::Relation::Ally;
    uint32_t contact_before = 0;  // cPlayer's flags for the mirror's political id before the mirror
    spaceext::Relation rel_on = spaceext::Relation::Ally; // the relation written into the game
    // applied state (valid while applied)
    bool applied = false;
    void* emp = nullptr;          // the cEmpire (kept alive by our reference, re-inserted on apply)
    void* galaxy = nullptr;       // the player's cEmpire when emp was made: another one = a reload (BUG-0038)
    uint32_t epoch = 0;           // spaceext::world_epoch() when emp was made: another one = a reload (BUG-0225)
    uint32_t pol = 0;
    std::map<uint32_t, StarSnap> stars;
    std::map<uint32_t, PlanetSnap> planets;
    std::map<uint32_t, std::string> skipped; // PlanetID -> why not mirrored
    std::map<uint32_t, uint8_t> raids;       // the owner's raids now: PlanetID -> bombers
};
static std::map<std::string, Mirror> g_m;
static bool g_ok, g_hold;            // g_hold: no apply (unmirrored for a save, until g_reapply_at)
static double g_reapply_at, g_next_sync, g_next_raid_scan, g_last_raids_sent;
static uint32_t g_roster_seen = ~0u;
static uint32_t g_saves, g_unmirrors, g_applies, g_raids_started, g_raids_won, g_raids_completed, g_growth_blocked;
static std::vector<uint32_t> g_my_raids_sent;
static std::set<uint32_t> g_my_raids_seen;
static std::string g_last;
static std::string hex(uint32_t v);
static std::set<void*> g_aborted; // my raids I aborted (pointers only compared)
static bool g_aborted_fix = true;  // coop.space allies abortedfix 0: the set is never pruned (BUG-0136, tests)
COOP_FIX("allies.aborted", "BUG-0136", g_aborted_fix, "space: a won raid on a partner's colony does not count as aborted");
static bool g_end_pending;        // the session ended while I was on a mirror planet
static void end_all();
static std::map<uint32_t, double> g_raid_quiet; // PlanetID -> no new raid of mine there before

// ---------------- game helpers ----------------
static void* star_mgr() { return a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr; }
static void* player_empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }
static uint32_t my_pol()
{
    void* e = player_empire();
    return readable(e, 0x88) ? F<uint32_t>(e, 0x84) : 0xFFFFFFFFu;
}
static int context() { return a_Context ? cdecl_call<int>(a_Context) : -1; }
static void* active_planet_record()
{
    void* p = a_ActivePlanet ? cdecl_call<void*>(a_ActivePlanet) : nullptr;
    return readable(p, 0x140) ? F<void*>(p, 0x13C) : nullptr;
}

static void* empire_of(uint32_t pol)
{
    void* sm = star_mgr();
    return sm && a_GetEmpire && pol != 0xFFFFFFFFu ? thiscall<void*>(a_GetEmpire, sm, pol) : nullptr;
}

static std::vector<void*> ptr_vec(void* v, size_t max = 4096)
{
    std::vector<void*> out;
    if (!readable(v, 8))
        return out;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    if (!b || e <= b || (size_t)(e - b) > max || !readable(b, (e - b) * 4))
        return out;
    out.assign(b, e);
    return out;
}

// the planets of a star: cStarRecord::GetPlanetRecords loads them (resources 0x5220CB8 from the
// save's planet DB or the cache) when the star's vector +0x84 was emptied; a star never visited
// (+0xAC planet count 0) gets them generated (docs/re/space.md 15.4)
static std::vector<void*> planets_of(void* s)
{
    if (!F<uint8_t>(s, 0xAC) && a_RequirePlanets)
        thiscall<void>(a_RequirePlanets, star_mgr(), s, 0);
    void* v = a_GetPlanets ? thiscall<void*>(a_GetPlanets, s) : (uint8_t*)s + 0x84;
    return ptr_vec(v, 64);
}

static void* planet_record(uint32_t planet)
{
    void* s = spacecoop::star_record(planet & 0xFFFFFFu);
    if (!s)
        return nullptr;
    auto v = planets_of(s);
    uint32_t i = planet >> 24;
    if (i >= v.size() || !readable(v[i], 0x1A4) || F<uint32_t>(v[i], 0x184) != planet)
        return nullptr;
    return v[i];
}

// the loaded cPlanet of a record (cStarManager +0x50 vector, docs/re/space.md 9.1), null if none
static void* loaded_planet(void* rec)
{
    void* sm = star_mgr();
    for (void* p : ptr_vec((uint8_t*)sm + 0x50, 64))
        if (readable(p, 0x140) && F<void*>(p, 0x13C) == rec)
            return p;
    return nullptr;
}
static void planet_set_pol(void* rec, uint32_t pol)
{
    void* p = loaded_planet(rec);
    if (p && readable((uint8_t*)p + 0xD4, 0x34))
        vcall<void>((uint8_t*)p + 0xD4, 0x48 / 4, pol); // cGameData::SetPoliticalID
}

static void str_assign(void* s, const std::u16string& v)
{
    if (a_StrAssign)
        thiscall<void>(a_StrAssign, s, v.data(), v.data() + v.size());
}

static std::u16string utf16(const std::string& s)
{
    std::u16string o;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = (uint8_t)s[i];
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (n > 1)
            c &= (0xFF >> (n + 1));
        for (int k = 1; k < n && i + k < s.size(); k++)
            c = c << 6 | ((uint8_t)s[i + k] & 0x3F);
        i += n;
        if (c >= 0x10000) {
            c -= 0x10000;
            o += (char16_t)(0xD800 + (c >> 10));
            o += (char16_t)(0xDC00 + (c & 0x3FF));
        } else
            o += (char16_t)c;
    }
    return o;
}

// the game's 12 empire colours (ids 0x53DBCF3.. as UpdateAndGetColor uses them): the nearest one
static uint32_t palette_id(uint32_t argb)
{
    uint32_t best = 0x53DBCF3u;
    float bd = 1e9f;
    for (uint32_t i = 0; i < 12 && a_Color; i++) {
        float* c = cdecl_call<float*>(a_Color, 0x53DBCF3u + i);
        if (!readable(c, 12))
            continue;
        float d = 0;
        for (int k = 0; k < 3; k++) {
            float want = ((argb >> (16 - 8 * k)) & 0xFF) / 255.f;
            d += (c[k] - want) * (c[k] - want);
        }
        if (d < bd)
            bd = d, best = 0x53DBCF3u + i;
    }
    return best;
}

// relationship flags (+4 of the data: bit 0 war, bit 1 ally), both directions; the entry is made
// by ResetRelationship when missing (docs/re/space.md 15.3)
static void rel_flags(uint32_t a, uint32_t b, uint32_t set, uint32_t clear)
{
    void* rm = a_RelMgr ? cdecl_call<void*>(a_RelMgr) : nullptr;
    if (!rm)
        return;
    for (int dir = 0; dir < 2; dir++) {
        uint32_t x = dir ? b : a, y = dir ? a : b;
        void* map = thiscall<void*>(a_RelMap, rm, x, y);
        void* d = map ? thiscall<void*>(a_RelData, rm, map, x, y) : nullptr;
        if (!d && set) {
            thiscall<void>(a_RelReset, rm, x, y);
            map = thiscall<void*>(a_RelMap, rm, x, y);
            d = map ? thiscall<void*>(a_RelData, rm, map, x, y) : nullptr;
        }
        if (readable(d, 8))
            F<uint32_t>(d, 4) = (F<uint32_t>(d, 4) & ~clear) | set;
    }
}

// cPlayer (cGameNounManager +0x74) keeps per political id the flags of the contact: 0x2 contacted
// (PlayerContactedEmpire), 0x4 aware (EmpireIsAwareOfPlayer). An empire not contacted greets the
// player on arrival in its territory with an envoy (cEmpire 0xC31690 -> first contact comm event),
// and the first contact resets the relationship: "Your alliance with ... has been severed".
static uint32_t* contact_flags(uint32_t pol)
{
    void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr;
    void* pl = nm && a_GetPlayer ? thiscall<void*>(a_GetPlayer, nm) : nullptr;
    if (!readable(pl, 0x11F0) || !a_FlagsAt)
        return nullptr;
    uint32_t key = pol;
    return thiscall<uint32_t*>(a_FlagsAt, (uint8_t*)pl + 0x11D8, &key);
}

static void set_relation(Mirror& m, bool on)
{
    void* me = player_empire();
    if (!me || !m.emp)
        return;
    if (uint32_t* f = contact_flags(m.pol)) {
        if (on) {
            m.contact_before = *f;
            *f |= 0x6;
        } else
            *f = m.contact_before;
    }
    uint32_t mp = F<uint32_t>(me, 0x84);
    bool war = m.rel == spaceext::Relation::War, ally = m.rel == spaceext::Relation::Ally;
    if (on) {
        rel_flags(m.pol, mp, war ? 1 : ally ? 2 : 0, 3);
        if (ally) {
            thiscall<void>(a_AllyAdd, me, m.emp);
            thiscall<void>(a_AllyAdd, m.emp, me);
        } else if (war) {
            thiscall<void>(a_EnemyAdd, me, m.emp);
            thiscall<void>(a_EnemyAdd, m.emp, me);
        }
    } else {
        rel_flags(m.pol, mp, 0, 3);
        thiscall<void>(a_AllyRemove, me, m.emp);
        thiscall<void>(a_AllyRemove, m.emp, me);
        thiscall<void>(a_EnemyRemove, me, m.emp);
        thiscall<void>(a_EnemyRemove, m.emp, me);
    }
}

static std::string empire_name(const Mirror& m)
{
    return m.name + spaceext::tr("'s empire", ": империя");
}

// BUG-0038: after a reload of the galaxy the mirror's cEmpire and political id belong to the old
// galaxy. ensure_empire put that object back into the new galaxy's map at the old id, where a real
// NPC empire of the new galaxy may live (ids come from the reloaded counter), with the old galaxy's
// star records still in its +0x88. Now the mirror starts over: a new cEmpire with an id of the new
// galaxy. The old object is not released (our reference is its last one, and its destructor would
// release the old galaxy's records): one cEmpire leaks per reload. `coop.space allies reloadfix 0`
// restores the old reuse (tests).
static bool g_reload_fix = true;
COOP_FIX("allies.reload", "BUG-0038", g_reload_fix, "space: the allies mirror starts over with a new cEmpire after a reload");
// BUG-0225: the player's cEmpire of a reloaded galaxy can have the freed old one's address; the
// Space world epoch (spaceext::world_epoch: a load or the galaxy menu shows non-Space frames) sees
// the reload whatever the address. `allies.reload_id` off: the pointer compare alone (0.7.3.0).
static bool g_reload_id_fix = true;
COOP_FIX("allies.reload_id", "BUG-0225", g_reload_id_fix, "space: a galaxy reload is seen by the Space world epoch, not the cEmpire address");
static uint32_t g_reload_resets;
static void reset_empire(Mirror& m)
{
    // a save also takes the unmirrored cEmpire out of the map (it is put back on the next apply):
    // only another player cEmpire (or another Space world) means another galaxy; none (the menu) =
    // not known yet
    void* pe = player_empire();
    if (!g_reload_fix || !m.emp || !pe)
        return;
    if (pe == m.galaxy && !(g_reload_id_fix && m.epoch && m.epoch != spaceext::world_epoch()))
        return;
    m.emp = nullptr;
    m.galaxy = nullptr;
    m.epoch = 0;
    m.pol = 0;
    m.contact_before = 0;
    m.rel_on = spaceext::Relation::Ally;
    g_reload_resets++;
}

// the mirror's cEmpire in the star manager: made once (then kept alive by our reference and put
// back into the map on every apply), so nothing in the game ever sees it freed
static bool ensure_empire(Mirror& m)
{
    void* sm = star_mgr();
    if (!sm)
        return false;
    if (m.emp && empire_of(m.pol) == m.emp)
        return true;
    // not in the map any more: after a save, or the galaxy was reloaded (also after a save on the
    // way to the menu unmirrored it, so no "applied" check saw the reload)
    reset_empire(m);
    if (!m.emp) {
        uint32_t pol = thiscall<uint32_t>(a_NextPol, sm, 1);
        void* e = thiscall<void*>(a_CreateEmpire, sm, pol); // new cEmpire, inserted into +0x150
        if (!readable(e, 0x158))
            return false;
        vcall<void>(e, 0); // our own reference (AddRef)
        thiscall<void>(a_EmpireInit, e, pol, 0);
        m.emp = e;
        m.pol = pol;
        m.galaxy = player_empire();
        m.epoch = spaceext::world_epoch();
    } else {
        uint32_t key = m.pol;
        void** slot = thiscall<void**>(a_MapAt, (uint8_t*)sm + 0x150, &key);
        if (!slot)
            return false;
        if (*slot != m.emp) {
            vcall<void>(m.emp, 0);
            void* old = *slot;
            *slot = m.emp;
            if (old)
                vcall<void>(old, 1);
        }
    }
    F<uint32_t>(m.emp, 0x10) = palette_id(m.color);
    str_assign((uint8_t*)m.emp + 0x3C, utf16(empire_name(m)));
    F<uint32_t>(m.emp, 0xB0) = 0xFFFFFFFFu; // home star: set by the first star
    F<uint32_t>(m.emp, 0xB4) = 0xFFFFFFFFu;
    return true;
}

static void erase_from_vec(void* vec, void* p)
{
    void** b = F<void**>(vec, 0);
    void** e = F<void**>(vec, 4);
    for (void** it = b; it && it < e; it++)
        if (*it == p) {
            memmove(it, it + 1, (e - it - 1) * sizeof(void*));
            F<void**>(vec, 4) = e - 1;
            vcall<void>(p, 1); // the vector's reference
            return;
        }
}

static void restore_planet(Mirror& m, uint32_t id)
{
    auto it = m.planets.find(id);
    if (it == m.planets.end() || !it->second.on)
        return;
    it->second.on = false;
    if (void* rec = it->second.rec) {
        PlanetSnap& s = it->second;
        auto civ = ptr_vec((uint8_t*)rec + 0x15C, 64);
        // keep what the mirror added (re-added on the next apply); never freed: a loaded planet may
        // still point at its cities
        s.civ.assign(civ.begin() + std::min(s.civ_before, civ.size()), civ.end());
        void** b = F<void**>(rec, 0x15C);
        F<void**>(rec, 0x160) = b + std::min(s.civ_before, civ.size());
        F<uint32_t>(rec, 0x194) = s.tech;
        F<uint32_t>(rec, 0x2C) = s.flags;
        planet_set_pol(rec, 0xFFFFFFFFu);
    }
}

static void restore_star(Mirror& m, uint32_t id)
{
    auto it = m.stars.find(id);
    if (it == m.stars.end())
        return;
    if (void* s = spacecoop::star_record(id)) {
        StarSnap& z = it->second;
        F<uint32_t>(s, 0x54) = z.emp;
        F<uint32_t>(s, 0x4C) = z.tech;
        F<uint32_t>(s, 0x5C) = z.flags;
        memcpy((uint8_t*)s + 0x74, z.species, 12);
        if (m.emp)
            erase_from_vec((uint8_t*)m.emp + 0x88, s); // cEmpire::mStars
    }
}

// drop the snapshots (after a reload of the galaxy, or once undone): our references go
static void forget(Mirror& m)
{
    for (auto& [id, p] : m.planets)
        if (p.rec)
            vcall<void>(p.rec, 1);
    m.planets.clear();
    m.stars.clear();
}

// undo everything of one mirror (the cEmpire leaves the star manager, our reference keeps it)
static void unapply(Mirror& m)
{
    if (!m.applied)
        return;
    m.applied = false;
    void* sm = star_mgr();
    if (!m.emp || !sm || empire_of(m.pol) != m.emp) { // the galaxy was reloaded: nothing of ours is left
        forget(m);
        reset_empire(m);
        return;
    }
    set_relation(m, false);
    for (auto& [id, s] : m.planets)
        restore_planet(m, id);
    for (auto& [id, s] : m.stars)
        restore_star(m, id);
    m.stars.clear();
    uint32_t key = m.pol;
    thiscall<void>(a_HomeStarMap, sm, m.pol, (void*)nullptr); // home star map +0x12C: erase
    thiscall<int>(a_MapErase, (uint8_t*)sm + 0x150, &key);   // the empire map: erase (releases its ref)
    g_unmirrors++;
}

static bool add_star(Mirror& m, void* s)
{
    uint32_t id = F<uint32_t>(s, 0x70);
    if (m.stars.count(id))
        return true;
    StarSnap z;
    z.emp = F<uint32_t>(s, 0x54);
    z.tech = F<uint32_t>(s, 0x4C);
    z.flags = F<uint32_t>(s, 0x5C);
    memcpy(z.species, (uint8_t*)s + 0x74, 12);
    m.stars[id] = z;
    F<uint32_t>(s, 0x54) = m.pol;
    thiscall<void>(a_AddStar, m.emp, s);
    if (F<uint32_t>(m.emp, 0xB0) == 0xFFFFFFFFu)
        thiscall<void>(a_SetHomeStar, m.emp, s);
    return true;
}

static void add_planet(Mirror& m, void* rec)
{
    uint32_t id = F<uint32_t>(rec, 0x184);
    PlanetSnap& s = m.planets[id];
    if (s.on)
        return;
    s.on = true;
    if (s.rec != rec) {
        vcall<void>(rec, 0); // AddRef
        if (s.rec)
            vcall<void>(s.rec, 1);
        s.rec = rec;
    }
    s.tech = F<uint32_t>(rec, 0x194);
    s.flags = F<uint32_t>(rec, 0x2C);
    s.civ_before = ptr_vec((uint8_t*)rec + 0x15C, 64).size();
    cdecl_call<void>(a_PlanetTech, rec, 5);
    if (!s.civ.empty()) { // the cities of the last apply
        for (void* c : s.civ)
            thiscall<void>(a_AddCivData, rec, c);
        F<uint32_t>(rec, 0x2C) |= 0x40;
    } else
        cdecl_call<void>(a_FillPlanet, rec, 5); // cCivData of the star's empire + its cCityData
    auto civ = ptr_vec((uint8_t*)rec + 0x15C, 64);
    for (size_t i = s.civ_before; i < civ.size(); i++)
        if (readable(civ[i], 0x18))
            F<uint32_t>(civ[i], 0x14) = m.pol;
    thiscall<void>(a_AddPlanet, m.emp, rec);
    planet_set_pol(rec, m.pol);
}

// why a colony of the partner can not be one of the mirror's here ("" = it can)
static std::string blocked(Mirror& m, uint32_t planet, void*& rec, void*& star)
{
    rec = planet_record(planet);
    star = rec ? spacecoop::star_record(planet & 0xFFFFFFu) : nullptr;
    if (!rec || !star)
        return "not in this galaxy";
    uint32_t owner = F<uint32_t>(star, 0x54);
    if (m.stars.count(planet & 0xFFFFFFu))
        owner = m.stars[planet & 0xFFFFFFu].emp;
    if (owner == my_pol())
        return "your own star system";
    if (owner != 0xFFFFFFFFu)
        return "a star system of another empire here";
    if (F<uint32_t>(star, 0x5C) & 3)
        return "a home star";
    bool on = m.planets.count(planet) && m.planets[planet].on;
    if (!on && F<uint32_t>(rec, 0x194) >= 4)
        return "a civilization lives there";
    void* ap = active_planet_record();
    if (ap == rec && context() == 0 && !on)
        return "you are on this planet now (it changes when you leave)";
    return "";
}

static void apply(Mirror& m)
{
    if (!g_ok || !spaceext::in_space() || !star_mgr() || !player_empire())
        return;
    if (m.applied && (!m.emp || empire_of(m.pol) != m.emp)) { // a reload threw it away
        m.applied = false;
        forget(m);
        reset_empire(m);
    }
    m.skipped.clear();
    std::vector<std::pair<void*, void*>> todo;
    std::set<uint32_t> want;
    for (uint32_t p : m.wanted) {
        void *rec, *star;
        std::string why = blocked(m, p, rec, star);
        if (!why.empty()) {
            m.skipped[p] = why;
            continue;
        }
        want.insert(p);
        todo.push_back({star, rec});
    }
    // colonies the partner lost (or that are blocked now): undone, unless I am on that planet
    void* ap = context() == 0 ? active_planet_record() : nullptr;
    for (auto it = m.planets.begin(); it != m.planets.end();) {
        uint32_t id = it->first;
        if (want.count(id) || (ap && ap == planet_record(id) && it->second.on)) {
            ++it;
            continue;
        }
        restore_planet(m, id);
        if (it->second.rec)
            vcall<void>(it->second.rec, 1);
        it = m.planets.erase(it);
    }
    if (m.applied)
        for (auto it = m.stars.begin(); it != m.stars.end();) {
            bool used = false;
            for (auto& [pid, ps] : m.planets) // wanted ones, and a lost one I am on
                used |= (pid & 0xFFFFFFu) == it->first && (ps.on || want.count(pid));
            if (!used) {
                restore_star(m, it->first);
                it = m.stars.erase(it);
            } else
                ++it;
        }
    if (todo.empty() && !m.applied)
        return;
    if (!ensure_empire(m))
        return;
    bool first = !m.applied;
    m.applied = true;
    size_t stars_before = m.stars.size();
    for (auto& [star, rec] : todo) {
        add_star(m, star);
        add_planet(m, rec);
    }
    // the empire's species (citizens, buildings, the ships; also the species key of each of its
    // stars): the player's own (the same save in the usual session; the partner's species is not in
    // my files otherwise)
    if (first || m.stars.size() != stars_before)
        if (void* sp = thiscall<void*>(a_GetSpecies, player_empire()))
            thiscall<void>(a_SetSpecies, m.emp, sp);
    str_assign((uint8_t*)m.emp + 0x3C, utf16(empire_name(m))); // SetSpeciesProfile renames it
    if (first) {
        set_relation(m, true);
        m.rel_on = m.rel;
    } else if (m.rel != m.rel_on) { // rivals mode: an alliance or a war between the players
        spaceext::Relation want = m.rel;
        m.rel = m.rel_on;
        set_relation(m, false);
        m.rel = want;
        set_relation(m, true);
        m.rel_on = want;
    }
    g_applies++;
}

static void apply_all()
{
    if (g_hold)
        return;
    for (auto& [n, m] : g_m)
        apply(m);
}

static void unapply_all()
{
    for (auto& [n, m] : g_m)
        unapply(m);
}

// ---------------- the save: never with the mirror in it ----------------
using Save_t = void(SK_THISCALL*)(void*, const char16_t*, int);
static Save_t o_Save;
static void SK_THISCALL h_Save(void* mgr, const char16_t* name, int flag)
{
    bool any = false;
    for (auto& [n, m] : g_m)
        any |= m.applied;
    if (any) {
        unapply_all();
        g_hold = true;
        g_reapply_at = H->time_seconds() + 3.0; // the game may finish writing after the call
    }
    g_saves++;
    spaceext::note_save();
    o_Save(mgr, name, flag);
}

// NPC empires grow (colonise more stars) on their own: not the mirror (it would leave stars behind)
using Expand_t = void(SK_THISCALL*)(void*, uint32_t);
static Expand_t o_Expand;
static void SK_THISCALL h_Expand(void* self, uint32_t pol)
{
    for (auto& [n, m] : g_m)
        if (m.emp && m.pol == pol) {
            g_growth_blocked++;
            return;
        }
    o_Expand(self, pol);
}

// ---------------- raids ----------------
static std::vector<void*> missions()
{
    std::vector<void*> v;
    void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr;
    if (!nm || !a_GetData)
        return v;
    void* list = thiscall<void*>(a_GetData, nm, a_MissionCb[0], a_MissionCb[1], a_MissionCb[2], a_MissionCb[3],
                                 0x2AA5ADAu);
    return readable(list, 12) ? ptr_vec((uint8_t*)list + 4, 4096) : v;
}
static bool is_raid(void* m)
{
    if (!readable(m, 0x200))
        return false;
    uint32_t id = F<uint32_t>(m, 0x78);
    return id == 0xA4B0A2DFu || id == 0xC4956AA7u;
}
static void* mission_target(void* m)
{
    void* p = F<void*>(m, 0x1E8);
    return readable(p, 0x140) ? F<void*>(p, 0x13C) : nullptr;
}
// active raids on planet records: target record -> mission (accepted = 2 too: the manager makes a
// new raid active only at its next update, seconds later)
static std::map<void*, void*> active_raids(bool accepted = false)
{
    std::map<void*, void*> r;
    for (void* m : missions())
        if (is_raid(m) && (F<uint32_t>(m, 0x84) == 3 || (accepted && F<uint32_t>(m, 0x84) == 2)))
            if (void* rec = mission_target(m))
                r[rec] = m;
    return r;
}

static bool mirror_pol(uint32_t pol)
{
    for (auto& [n, m] : g_m)
        if (m.emp && m.pol == pol)
            return true;
    return false;
}

} // namespace spaceallies
bool spaceext::allies_mirror_pol(uint32_t pol) { return spaceallies::mirror_pol(pol); }
std::vector<void*> spaceext::game_missions() { return spaceallies::missions(); }
namespace spaceallies {

static Mirror* mirror_of_planet(uint32_t planet)
{
    for (auto& [n, m] : g_m)
        if (m.applied && m.planets.count(planet) && m.planets[planet].on)
            return &m;
    return nullptr;
}

// owner: my raided colonies -> everybody
static void raids_tick(double now)
{
    std::vector<std::pair<uint32_t, uint8_t>> mine;
    uint32_t pol = my_pol();
    for (auto& [rec, m] : active_raids()) {
        if (!readable(rec, 0x1A4))
            continue;
        uint32_t id = F<uint32_t>(rec, 0x184);
        if (mirror_of_planet(id))
            continue; // a raid of my own on a partner's colony
        void* star = spacecoop::star_record(id & 0xFFFFFFu);
        if (!star || F<uint32_t>(star, 0x54) != pol)
            continue;
        mine.push_back({id, (uint8_t)std::min<uint32_t>(F<uint32_t>(m, 0x1F8), 30)});
    }
    std::vector<uint32_t> ids;
    for (auto& p : mine)
        ids.push_back(p.first);
    if (ids != g_my_raids_sent || now - g_last_raids_sent > 10.0) {
        g_my_raids_sent = ids;
        g_last_raids_sent = now;
        net::Writer w;
        w.u8((uint8_t)std::min<size_t>(mine.size(), 50));
        for (size_t i = 0; i < mine.size() && i < 50; i++) {
            w.u32(mine[i].first);
            w.u8(mine[i].second);
        }
        spaceext::send(K_RAIDS, w);
    }
}

// helper: a raid of my own on the mirror colony I am on, while its owner's raid runs
static void helper_raids()
{
    void* ap = context() == 0 ? active_planet_record() : nullptr;
    uint32_t here = ap ? F<uint32_t>(ap, 0x184) : 0;
    auto act = active_raids(true);
    std::set<uint32_t> raided; // PlanetIDs (the raid's target record need not be the same object
                               // as the active planet's: compare ids)
    // a raid of mine on a mirror colony lives only while I am on that planet and the owner's raid
    // runs: away from the planet the game would simulate it off-screen against the mirror's cities
    for (auto& [rec, mi] : act) {
        uint32_t id = readable(rec, 0x188) ? F<uint32_t>(rec, 0x184) : 0;
        if (id)
            raided.insert(id);
        Mirror* m = mirror_of_planet(id);
        if (m && (id != here || !m->raids.count(id)) && a_SetState) {
            thiscall<void>(a_SetState, mi, 7); // aborted
            g_aborted.insert(mi);
            g_last = "raid on " + m->name + "'s colony aborted (here " + hex(here) + ", owner's raid " +
                     std::to_string(m->raids.count(id)) + ")";
        }
    }
    if (!ap)
        return;
    for (auto& [n, m] : g_m) {
        if (!m.applied || !m.planets.count(here) || !m.planets[here].on || !m.raids.count(here))
            continue;
        const spacemap::Roster* r = m.online ? spacemap::roster_of(m.id) : nullptr;
        // the owner fights it there itself (with me: squadron mirrors its ships to me)
        bool owner_here = r && (r->place & 0x40000000u) && (r->place & 0x3FFFFFFFu) == here;
        auto q = g_raid_quiet.find(here);
        if (q != g_raid_quiet.end() && H->time_seconds() < q->second)
            continue; // one ended just now (the owner's raid ends with my win's message)
        if (!owner_here && !raided.count(here) && a_StartRaid) {
            int bombers = std::max(2, (int)m.raids[here]);
            thiscall<int>(a_StartRaid, nullptr, 0, ap, bombers); // the `raid -start` code: owner = star's empire
            g_raids_started++;
            g_last = "raid on " + m.name + "'s colony started here";
        }
    }
}

// a raid of mine on a mirror colony ended: a win completes the owner's raid. The manager takes a
// finished raid through 5 (completed) or 6 (failed) to 1 within one update, so the state does not
// tell; the ships do: a raid ends "completed" when none of its ships is left (docs/re/space.md 6),
// a failed one sends them away alive.
struct Watch {
    uint32_t planet = 0;
    int ships = 0;          // its living ships at the last look
    bool had_ships = false;
};
static std::map<void*, Watch> g_watch; // my raids on mirror colonies (pointers only compared)

static std::map<void*, int> raid_ships()
{
    std::map<void*, int> n;
    void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr;
    void* vec = nm && a_AllUFOs ? thiscall<void*>(a_AllUFOs, nm) : nullptr;
    for (void* u : ptr_vec(vec, 1024))
        if (readable(u, 0x808) && F<void*>(u, 0x800) && F<float>(u, 0x540) > 0 && F<int>(u, 0x53C) != 2 &&
            !mirror_pol(F<uint32_t>(u, 0x30)) && F<uint32_t>(u, 0x30) != my_pol())
            n[F<void*>(u, 0x800)]++;
    return n;
}

static void watch_raids()
{
    auto act = active_raids();
    auto ships = raid_ships();
    std::set<void*> now;
    for (auto& [rec, mi] : act) {
        uint32_t id = F<uint32_t>(rec, 0x184);
        if (!mirror_of_planet(id))
            continue;
        now.insert(mi);
        Watch& w = g_watch[mi];
        w.planet = id;
        w.ships = ships.count(mi) ? ships[mi] : 0;
        w.had_ships |= w.ships > 0;
    }
    for (auto it = g_watch.begin(); it != g_watch.end();) {
        if (now.count(it->first)) {
            ++it;
            continue;
        }
        Watch w = it->second;
        g_raid_quiet[w.planet] = H->time_seconds() + 15.0;
        bool aborted = g_aborted.erase(it->first) != 0;
        bool left = ships.count(it->first) && ships[it->first] > 0;
        it = g_watch.erase(it);
        Mirror* m = mirror_of_planet(w.planet);
        if (aborted || left || !w.had_ships || !m || !m->raids.count(w.planet))
            continue;
        net::Writer wr;
        wr.u8(m->id);
        wr.u32(w.planet);
        spaceext::send(K_DEFENDED, wr);
        g_raids_won++;
        m->raids.erase(w.planet);
        g_last = "beat the raid on " + m->name + "'s colony";
        spaceext::notice(m->name + spaceext::tr("'s colony is safe: you beat the raid",
                                                ": колония спасена, вы отбили налёт"));
    }
    if (g_aborted_fix) // BUG-0136: an aborted raid never watched (accepted, state 2) leaves the set
        spacelogic::prune_aborted(g_aborted, g_watch);
}

// ---------------- sync with the rosters ----------------
static void sync_from_rosters()
{
    uint8_t me = spaceext::my_id();
    std::set<std::string> seen;
    for (uint8_t id : spacemap::roster_ids()) {
        if (id == me)
            continue;
        const spacemap::Roster* r = spacemap::roster_of(id);
        if (!r)
            continue;
        std::string name = spaceext::name_of(id);
        if (name.empty() || name == "?")
            continue;
        Mirror& m = g_m[name];
        m.name = name;
        m.id = id;
        m.online = true;
        m.color = spacemap::player_color(id);
        m.rel = spaceext::relation(me, id);
        m.wanted.clear();
        for (auto& c : r->colonies)
            m.wanted.push_back(c.planet);
        seen.insert(name);
    }
    for (auto& [n, m] : g_m)
        if (!seen.count(n) && m.online && m.id != 0xFF) {
            m.online = false; // left: a frozen ally until the session ends (or the partner is back)
            m.raids.clear();
        }
}

static bool any_applied()
{
    for (auto& [n, m] : g_m)
        if (m.applied)
            return true;
    return false;
}

static void tick(double now)
{
    if (!g_ok)
        return;
    if (!spaceext::in_space()) {
        // the galaxy may be gone (a load, the menu): forget the applied state; the save hook took
        // the mirror out of any save made on the way
        for (auto& [n, m] : g_m)
            if (m.applied && (!star_mgr() || empire_of(m.pol) != m.emp)) {
                m.applied = false;
                forget(m);
                reset_empire(m);
            }
        return;
    }
    if (g_end_pending) {
        static double next;
        if (now < next)
            return;
        next = now + 1.0;
        if (!spaceext::connected())
            H->run_outside_frame([](void*) { end_all(); }, nullptr);
        else
            g_end_pending = false; // a new session: the mirrors are wanted again
        return;
    }
    if (g_hold && now >= g_reapply_at) {
        g_hold = false;
        g_next_sync = 0;
    }
    if (spaceext::connected() && now >= g_next_raid_scan) {
        g_next_raid_scan = now + 1.0;
        raids_tick(now);
        H->run_outside_frame([](void*) { watch_raids(); helper_raids(); }, nullptr);
    }
    uint32_t ch = spacemap::roster_changes();
    if (ch == g_roster_seen && now < g_next_sync)
        return;
    g_roster_seen = ch;
    g_next_sync = now + 5.0;
    if (spaceext::connected())
        sync_from_rosters();
    if (!g_hold)
        H->run_outside_frame([](void*) { apply_all(); }, nullptr);
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    if (kind == K_RAIDS) {
        std::map<uint32_t, uint8_t> raids;
        uint8_t n = r.u8();
        for (uint8_t i = 0; i < n && r.ok(); i++) {
            uint32_t p = r.u32();
            uint8_t b = r.u8();
            if (r.ok())
                raids[p] = b;
        }
        if (!r.ok())
            return; // a cut list: keep the raids we know
        for (auto& [nm, m] : g_m)
            if (m.online && m.id == origin) {
                for (auto& [p, b] : raids)
                    if (!m.raids.count(p) && m.planets.count(p))
                        spaceext::notice(m.name + spaceext::tr("'s colony is raided: fly there and fight in your galaxy",
                                                               ": налёт на колонию, летите туда и сражайтесь"));
                m.raids = raids;
            }
    } else if (kind == K_DEFENDED) {
        uint8_t owner = r.u8();
        uint32_t planet = r.u32();
        if (!r.ok() || owner != spaceext::my_id())
            return;
        H->run_outside_frame(
            [](void* u) {
                uint32_t planet = (uint32_t)(uintptr_t)u;
                for (auto& [rec, mi] : active_raids())
                    if (F<uint32_t>(rec, 0x184) == planet && a_SetState) {
                        thiscall<void>(a_SetState, mi, 5); // completed: the native "defended" path
                        g_raids_completed++;
                    }
            },
            (void*)(uintptr_t)planet);
        spaceext::notice(spaceext::name_of(origin) +
                         spaceext::tr(" beat the raid on your colony", " отбил налёт на вашу колонию"));
    }
}

// the session is over: every mirror goes. On a mirror planet itself that waits until I leave it
// (its cities live in the planet's simulation; a save is not possible there anyway)
static bool on_mirror_planet()
{
    void* ap = context() == 0 ? active_planet_record() : nullptr;
    return ap && mirror_of_planet(F<uint32_t>(ap, 0x184)) != nullptr;
}
static void end_all()
{
    if (spaceext::in_space() && on_mirror_planet()) {
        g_end_pending = true;
        return;
    }
    g_end_pending = false;
    unapply_all();
    for (auto& [n, m] : g_m)
        forget(m);
    g_m.clear();
    g_watch.clear();
    if (g_aborted_fix)
        g_aborted.clear();
    g_raid_quiet.clear();
}

static void on_session_end()
{
    H->run_outside_frame([](void*) { end_all(); }, nullptr);
    g_my_raids_sent.clear();
}

// ---------------- the Space page ----------------
static int mirrored(const Mirror& m)
{
    int n = 0;
    for (auto& [p, s] : m.planets)
        n += s.on;
    return n;
}

static std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    for (auto& [n, m] : g_m) {
        if (!mirrored(m))
            continue; // nothing of that partner is mirrored (all shared, or nothing yet)
        spaceext::Row r;
        char b[200];
        const char* rel = m.rel == spaceext::Relation::War ? spaceext::tr("at war", "война")
                          : m.rel == spaceext::Relation::Ally ? spaceext::tr("ally", "союзник")
                                                               : spaceext::tr("neutral", "нейтралитет");
        snprintf(b, sizeof b, spaceext::tr("%s: colonies in your galaxy: %d (%s)%s", "%s: колоний в вашей галактике: %d (%s)%s"),
                 empire_name(m).c_str(), mirrored(m), rel,
                 m.online ? "" : spaceext::tr(", away", ", не в игре"));
        r.text = b;
        if (!m.planets.empty()) {
            uint32_t star = m.planets.begin()->first & 0xFFFFFFu;
            for (auto& [p, s] : m.planets)
                if (m.raids.count(p))
                    star = p & 0xFFFFFFu;
            r.button = spaceext::tr("Fly there", "Лететь");
            r.on_click = [star] {
                std::string e = spacecoop::travel(star);
                if (!e.empty())
                    spaceext::notice("can't fly: " + e);
            };
        }
        v.push_back(std::move(r));
        for (auto& [p, b2] : m.raids)
            if (m.planets.count(p)) {
                spaceext::Row q;
                q.text = m.name + spaceext::tr(": colony raided! Defend it on the planet", ": налёт на колонию! Защитите её");
                q.color = 0xFFFF6040;
                uint32_t star = p & 0xFFFFFFu;
                q.button = spaceext::tr("Jump there", "Прыгнуть");
                q.on_click = [star] {
                    std::string e = spacecoop::travel(star);
                    if (!e.empty())
                        spaceext::notice("can't fly: " + e);
                };
                v.push_back(std::move(q));
            }
    }
    return v;
}

static std::string status()
{
    char b[400];
    int ap = 0, pl = 0, st = 0;
    for (auto& [n, m] : g_m)
        ap += m.applied, pl += (int)m.planets.size(), st += (int)m.stars.size();
    snprintf(b, sizeof b,
             "allies: mirrors=%d applied=%d stars=%d planets=%d hold=%d saves=%u unmirrors=%u applies=%u growth_blocked=%u "
             "raids_started=%u raids_won=%u raids_completed=%u",
             (int)g_m.size(), ap, st, pl, (int)g_hold, g_saves, g_unmirrors, g_applies, g_growth_blocked,
             g_raids_started, g_raids_won, g_raids_completed);
    return std::string(b) + (g_last.empty() ? "" : " last='" + g_last + "'");
}

static std::string hex(uint32_t v)
{
    char b[12];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}

static std::string str16(void* s)
{
    if (!readable(s, 12))
        return "";
    const char16_t* b = F<const char16_t*>(s, 0);
    const char16_t* e = F<const char16_t*>(s, 4);
    std::string o;
    if (!b || e <= b || e - b > 200 || !readable(b, (e - b) * 2))
        return o;
    for (const char16_t* c = b; c < e; c++)
        o += *c < 0x80 ? (char)*c : '?';
    return o;
}

static std::string describe_star(uint32_t id)
{
    void* s = spacecoop::star_record(id);
    if (!s)
        return "star " + std::to_string(id) + ": not found\n";
    uint32_t pol = F<uint32_t>(s, 0x54);
    void* e = empire_of(pol);
    std::string o = "star " + std::to_string(id) + " '" + str16((uint8_t*)s + 0x60) + "' emp=" + hex(pol) +
                    " owner='" + (e ? str16((uint8_t*)e + 0x3C) : std::string("-")) + "'" +
                    (pol == my_pol() ? " (mine)" : "") + " tech=" + std::to_string(F<int>(s, 0x4C)) +
                    " flags=" + hex(F<uint32_t>(s, 0x5C)) + "\n";
    for (void* p : planets_of(s)) {
        if (!readable(p, 0x1A4))
            continue;
        o += "  planet " + hex(F<uint32_t>(p, 0x184)) + " '" + str16((uint8_t*)p + 0x18) +
             "' tech=" + std::to_string(F<int>(p, 0x194));
        for (void* cd : ptr_vec((uint8_t*)p + 0x15C, 32))
            if (readable(cd, 0x48))
                o += " civ(pol=" + hex(F<uint32_t>(cd, 0x14)) + " cities=" +
                     std::to_string((F<uint8_t*>(cd, 0x40) - F<uint8_t*>(cd, 0x3C)) / 4) + ")";
        o += "\n";
    }
    return o;
}

static std::string debug(const std::vector<std::string>& a)
{
    if (a.empty() || a[0] == "status")
        return status();
    if (a[0] == "list") {
        std::string o = status() + "\n";
        for (auto& [n, m] : g_m) {
            o += "mirror '" + n + "' id=" + std::to_string(m.id) + (m.online ? " online" : " away") +
                 " pol=" + hex(m.pol) + " applied=" + std::to_string(m.applied) + " name='" +
                 (m.emp && m.applied ? str16((uint8_t*)m.emp + 0x3C) : std::string("")) + "' rel=" +
                 std::to_string((int)m.rel) + " wanted=" + std::to_string(m.wanted.size()) + "\n";
            for (auto& [s, z] : m.stars)
                o += "  star " + std::to_string(s) + "\n";
            for (auto& [p, z] : m.planets)
                o += "  planet " + hex(p) + (m.raids.count(p) ? " RAIDED" : "") + "\n";
            for (auto& [p, why] : m.skipped)
                o += "  skipped " + hex(p) + ": " + why + "\n";
        }
        return o;
    }
    if (a[0] == "star" && a.size() > 1)
        return describe_star((uint32_t)strtoul(a[1].c_str(), nullptr, 0));
    if (a[0] == "planet" && a.size() > 1)
        return describe_star((uint32_t)strtoul(a[1].c_str(), nullptr, 16) & 0xFFFFFFu);
    if (a[0] == "fake" && a.size() > 2) { // tests: fake <name> <PlanetID hex>...: a mirror without a partner
        Mirror& m = g_m[a[1]];
        m.name = a[1];
        m.id = 0xFF;
        m.online = false;
        m.color = 0xFF40C0FF;
        m.wanted.clear();
        for (size_t i = 2; i < a.size(); i++)
            m.wanted.push_back((uint32_t)strtoul(a[i].c_str(), nullptr, 16));
        apply(m);
        return describe_star(m.wanted[0] & 0xFFFFFFu);
    }
    if (a[0] == "apply") {
        g_hold = false;
        apply_all();
        return status();
    }
    if (a[0] == "remove") { // unmirror (as before a save), no re-apply until "apply"
        unapply_all();
        g_hold = true;
        g_reapply_at = 1e18;
        return status();
    }
    if (a[0] == "detach" && a.size() > 1) { // tests (BUG-0071): mirror <name> becomes a plain empire of this
        // galaxy: no contact, no alliance, its star and colony stay, the mod forgets it
        auto it = g_m.find(a[1]);
        if (it == g_m.end() || !it->second.applied)
            return "no such applied mirror\n";
        set_relation(it->second, false);
        for (auto& [id, p] : it->second.planets)
            if (p.rec)
                vcall<void>(p.rec, 1);
        std::string pol = hex(it->second.pol);
        g_m.erase(it);
        return "detached " + pol + "\n";
    }
    if (a[0] == "drop") { // forget every mirror (after unmirroring it)
        unapply_all();
        for (auto& [n, m] : g_m)
            forget(m);
        g_m.clear();
        return status();
    }
    if (a[0] == "empire" && a.size() > 1) {
        void* e = empire_of((uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        if (!e)
            return "no empire\n";
        return "empire " + a[1] + " '" + str16((uint8_t*)e + 0x3C) + "' stars=" +
               std::to_string(ptr_vec((uint8_t*)e + 0x88).size()) + " home=" + std::to_string(F<uint32_t>(e, 0xB0)) +
               " color=" + hex(F<uint32_t>(e, 0x10)) + " allies=" + std::to_string(ptr_vec((uint8_t*)e + 0x70).size()) +
               " enemies=" + std::to_string(ptr_vec((uint8_t*)e + 0x5C).size()) + " at=" + hex((uint32_t)(uintptr_t)e) +
               "\n";
    }
    if (a[0] == "abortedfix" && a.size() > 1) { // BUG-0136: 0 = aborted raids stay in the set for good
        g_aborted_fix = a[1] != "0";
        return std::string("abortedfix ") + (g_aborted_fix ? "1" : "0") + " aborted=" + std::to_string(g_aborted.size()) + "\n";
    }
    if (a[0] == "samegalaxy") { // tests (BUG-0225): as if the new galaxy's player cEmpire had the old address
        int n = 0;
        for (auto& [k, m] : g_m)
            if (m.emp) {
                m.galaxy = player_empire();
                n++;
            }
        return "samegalaxy: " + std::to_string(n) + " mirror(s)\n";
    }
    if (a[0] == "reloadfix" && a.size() > 1) { // BUG-0038: 0 = reuse the old galaxy's cEmpire after a reload
        g_reload_fix = a[1] != "0";
        return std::string("reloadfix ") + (g_reload_fix ? "1" : "0") + " resets=" + std::to_string(g_reload_resets) +
               "\n";
    }
    if (a[0] == "nextpol") { // cStarManager +0x1D4: the next political id NextPoliticalID hands out
        void* sm = star_mgr();
        if (sm && a.size() > 1 && a[1] == "bump") // tests: take one id, as a new empire would
            thiscall<uint32_t>(a_NextPol, sm, 1);
        return sm ? "nextpol " + hex(F<uint32_t>(sm, 0x1D4)) + "\n" : "no star manager\n";
    }
    if (a[0] == "cities") { // on a planet: the cities and their owners
        void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr;
        void* vec = nm && a_AllCities ? thiscall<void*>(a_AllCities, nm) : nullptr;
        std::string o;
        for (void* c : ptr_vec(vec, 256))
            if (readable(c, 0x130)) {
                void* e = empire_of(F<uint32_t>(c, 0x30));
                // cCity +0x124 position; the own ship +0x38 (tests fly over it)
                float* p = &F<float>(c, 0x124);
                void* pm = a_PlayerUFO ? cdecl_call<void*>(a_PlayerUFO) : nullptr;
                void* me = readable(pm, 0x44) ? F<void*>(pm, 0x40) : nullptr;
                float d = -1, xy[2] = {-1, -1};
                if (readable(me, 0x48)) {
                    float* q = &F<float>(me, 0x38);
                    d = std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
                }
                int on = H->world_to_screen ? H->world_to_screen(p, xy) : 0;
                char t[96];
                snprintf(t, sizeof t, " dist %.0f screen %d %.0f %.0f", d, on, xy[0], xy[1]);
                o += "city pol=" + hex(F<uint32_t>(c, 0x30)) + " owner='" + (e ? str16((uint8_t*)e + 0x3C) : std::string("-")) +
                     "'" + t + "\n";
            }
        return o.empty() ? "no cities\n" : o;
    }
    if (a[0] == "fight") { // tests: hit the ships of my raids on mirror colonies (the game's damage, as my shots)
        float dmg = a.size() > 1 ? (float)atof(a[1].c_str()) : 1000.f;
        std::set<void*> mine;
        for (auto& [rec, mi] : active_raids())
            if (mirror_of_planet(F<uint32_t>(rec, 0x184)))
                mine.insert(mi);
        void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr;
        void* vec = nm && a_AllUFOs ? thiscall<void*>(a_AllUFOs, nm) : nullptr;
        void* pm = a_PlayerUFO ? cdecl_call<void*>(a_PlayerUFO) : nullptr;
        void* me = readable(pm, 0x44) ? F<void*>(pm, 0x40) : nullptr;
        if (!me || !a_TakeDamage)
            return "no ship\n";
        uint32_t pol = my_pol();
        int n = 0;
        float dir[3] = {0, 0, 1};
        for (void* u : ptr_vec(vec, 512))
            if (u != me && readable(u, 0x808) && mine.count(F<void*>(u, 0x800)) && F<float>(u, 0x540) > 0 &&
                F<uint32_t>(u, 0x30) != pol && !mirror_pol(F<uint32_t>(u, 0x30))) { // the raiders, not the defenders
                thiscall<void>(a_TakeDamage, (uint8_t*)u + 0x508, dmg, pol, 0, (const float*)dir, (uint8_t*)me + 0x508);
                n++;
            }
        return "hit " + std::to_string(n) + " raid ships\n";
    }
    if (a[0] == "raids") {
        std::string o;
        auto ships = raid_ships();
        for (auto& [rec, m] : active_raids())
            o += "raid " + hex(F<uint32_t>(m, 0x78)) + " on " + hex(F<uint32_t>(rec, 0x184)) + " state " +
                 std::to_string(F<int>(m, 0x84)) + " bombers " + std::to_string(F<int>(m, 0x1F8)) +
                 " ships " + std::to_string(ships.count(m) ? ships[m] : 0) +
                 (mirror_of_planet(F<uint32_t>(rec, 0x184)) ? " (mirror colony)" : "") + "\n";
        return o.empty() ? "no raids\n" : o;
    }
    if (a[0] == "raid" && a.size() > 1 && a_StartRaid) { // tests: a raid on a planet of my galaxy (`raid -start`)
        void* rec = planet_record((uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        if (!rec)
            return "no such planet\n";
        thiscall<int>(a_StartRaid, nullptr, 0, rec, a.size() > 2 ? atoi(a[2].c_str()) : 3);
        return "raid started\n";
    }
    if (a[0] == "free") { // tests: unowned stars near mine (StarID distance)
        void* sm = star_mgr();
        uint32_t here = spacecoop::star_record(F<uint32_t>(player_empire(), 0xB0)) ? F<uint32_t>(player_empire(), 0xB0) : 0;
        void* hs = spacecoop::star_record(here);
        if (!sm || !hs)
            return "no home star\n";
        float* hp = &F<float>(hs, 0x3C);
        std::vector<std::pair<float, uint32_t>> c;
        uint8_t* b = F<uint8_t*>(sm, 0xC8);
        uint8_t* e = F<uint8_t*>(sm, 0xCC);
        for (uint8_t* sec = b; sec && sec + 20 <= e; sec += 20) {
            void** arr = *(void***)sec;
            void** end = *(void***)(sec + 4);
            for (void** it = arr; arr && it < end && it - arr < 4096; it++) {
                void* st = *it;
                if (!readable(st, 0xB0) || F<uint32_t>(st, 0x54) != 0xFFFFFFFFu || (F<uint32_t>(st, 0x5C) & 3))
                    continue;
                float* p = &F<float>(st, 0x3C);
                float d = std::sqrt((p[0] - hp[0]) * (p[0] - hp[0]) + (p[1] - hp[1]) * (p[1] - hp[1]) +
                                    (p[2] - hp[2]) * (p[2] - hp[2]));
                c.push_back({d, F<uint32_t>(st, 0x70)});
            }
        }
        std::sort(c.begin(), c.end());
        std::string o;
        for (size_t i = 0; i < c.size() && i < 8; i++) {
            char t[64];
            snprintf(t, sizeof t, "free %u %.2f\n", c[i].second, c[i].first);
            o += t;
        }
        return o.empty() ? "none\n" : o;
    }
    if (a[0] == "colonize") { // tests: colonize [PlanetID]: a colony of mine (record level), default: a planet here
        void* me = player_empire();
        void* rec = nullptr;
        if (a.size() > 1)
            rec = planet_record((uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        else {
            // a planet of the star I am at: the active one, else the first without a civilization
            void* ap = active_planet_record();
            uint32_t here = ap ? F<uint32_t>(ap, 0x184) & 0xFFFFFFu : 0;
            if (ap && F<uint32_t>(ap, 0x194) < 4)
                rec = ap;
            else if (void* st = spacecoop::star_record(here))
                for (void* r : planets_of(st))
                    if (!rec && readable(r, 0x1A4) && F<uint32_t>(r, 0x194) < 4)
                        rec = r;
        }
        if (!rec || !me)
            return "no planet to colonize\n";
        uint32_t id = F<uint32_t>(rec, 0x184);
        void* star = spacecoop::star_record(id & 0xFFFFFFu);
        if (!star)
            return "no star\n";
        uint32_t pol = F<uint32_t>(me, 0x84);
        if (F<uint32_t>(star, 0x54) != 0xFFFFFFFFu && F<uint32_t>(star, 0x54) != pol)
            return "the star belongs to another empire\n";
        F<uint32_t>(star, 0x54) = pol;
        thiscall<void>(a_AddStar, me, star);
        cdecl_call<void>(a_PlanetTech, rec, 5);
        cdecl_call<void>(a_FillPlanet, rec, 5);
        thiscall<void>(a_AddPlanet, me, rec);
        planet_set_pol(rec, pol);
        return "colonized " + hex(id) + "\n" + describe_star(id & 0xFFFFFFu);
    }
    return "usage: coop.space allies [list | star <StarID> | planet <PlanetID> | empire <pol> | raids | raid <PlanetID> [n] |"
           " apply | remove | drop | fake <name> <PlanetID>... | free | colonize [PlanetID]]\n";
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_NextPol = S("Simulator::cStarManager::NextPoliticalID");
    a_GetEmpire = S("Sim_StarManager_GetEmpire");
    a_CreateEmpire = S("SporeCoop::cStarManager_CreateEmpire");
    a_EmpireInit = S("SporeCoop::cEmpire_Init");
    a_AddStar = S("Simulator::cEmpire::AddStarOwnership");
    a_SetHomeStar = S("SporeCoop::cEmpire_SetHomeStar");
    a_AddPlanet = S("SporeCoop::cEmpire_AddPlanet");
    a_SetSpecies = S("Simulator::cEmpire::SetSpeciesProfile");
    a_GetSpecies = S("Simulator::cEmpire::GetSpeciesProfile");
    a_PlanetTech = S("SporeCoop::cPlanetRecord_SetTechLevel");
    a_FillPlanet = S("SporeCoop::cPlanetRecord_FillPlanetDataForTechLevel");
    a_MapErase = S("SporeCoop::MapUintPtr_Erase");
    a_MapAt = S("SporeCoop::MapUintPtr_At");
    a_HomeStarMap = S("SporeCoop::cStarManager_SetHomeStar");
    a_RelMgr = S("Simulator::cRelationshipManager::Get");
    a_RelMap = S("RelationshipManager_GetRelationshipMap");
    a_RelData = S("RelationshipManager_GetRelationshipData");
    a_RelReset = S("Simulator::cRelationshipManager::ResetRelationship");
    a_AllyAdd = S("SporeCoop::cEmpire_AddAlly");
    a_AllyRemove = S("SporeCoop::cEmpire_RemoveAlly");
    a_EnemyAdd = S("SporeCoop::cEmpire_AddEnemy");
    a_EnemyRemove = S("SporeCoop::cEmpire_RemoveEnemy");
    a_StrAssign = S("wstring_copy");
    a_Save = S("SporeCoop::cGamePersistenceManager_SaveGame");
    a_Expand = S("SporeCoop::cEmpireGrowth_Expand");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_Color = S("Simulator::GetCachedColorFromId");
    a_NounMgr = S("Simulator::cGameNounManager::Get");
    a_GetData = S("Simulator::cGameNounManager::GetData");
    a_MissionCb[0] = S("SporeCoop::MissionNouns_Cb1");
    a_MissionCb[1] = S("SporeCoop::MissionNouns_Cb2");
    a_MissionCb[2] = S("SporeCoop::MissionNouns_Cb3");
    a_MissionCb[3] = S("SporeCoop::MissionNouns_Cb4");
    a_SetState = S("Simulator::cMission::SetState");
    a_StartRaid = S("SporeCoop::StartRaidCheat");
    a_Context = S("SporeCoop::Space_GetCurrentContext");
    a_ActivePlanet = S("SporeCoop::Space_GetActivePlanet");
    a_RequirePlanets = S("Simulator::cStarManager::RequirePlanetsForStar");
    a_AllCities = S("Simulator::cGameNounManager::GetAllCities");
    a_GetPlanets = S("Simulator::cStarRecord::GetPlanetRecords");
    a_AddCivData = S("SporeCoop::cPlanetRecord_AddCivData");
    a_GetPlayer = S("SporeCoop::cGameNounManager_GetPlayer");
    a_AllUFOs = S("Simulator::GetAllUFOs");
    a_TakeDamage = S("Simulator::cGameDataUFO::TakeDamage");
    a_PlayerUFO = S("Simulator::cSimulatorPlayerUFO::Get");
    a_FlagsAt = S("SporeCoop::VectorMapUint_At");
    uintptr_t need[] = {a_StarMgr, a_NextPol, a_GetEmpire, a_CreateEmpire, a_EmpireInit, a_AddStar, a_SetHomeStar,
                        a_AddPlanet, a_SetSpecies, a_GetSpecies, a_PlanetTech, a_FillPlanet, a_MapErase, a_MapAt,
                        a_HomeStarMap, a_RelMgr, a_RelMap, a_RelData, a_RelReset, a_AllyAdd, a_AllyRemove,
                        a_EnemyAdd, a_EnemyRemove, a_StrAssign, a_Save, a_Expand, a_PlayerEmpire, a_Color,
                        a_Context, a_ActivePlanet, a_GetPlanets, a_AddCivData, a_RequirePlanets, a_GetPlayer, a_FlagsAt, a_NounMgr};
    for (size_t i = 0; i < sizeof need / sizeof need[0]; i++)
        if (!need[i]) {
            H->log(M, SK_WARN, "space allies: symbol #%d missing", (int)i);
            return false;
        }
    // the save hook is what keeps the saves clean: without it the feature stays off
    if (H->hook_create(M, (void*)a_Save, (void*)&h_Save, (void**)&o_Save) != 0) {
        H->log(M, SK_WARN, "space allies: no save hook");
        return false;
    }
    H->hook_create(M, (void*)a_Expand, (void*)&h_Expand, (void**)&o_Expand);
    g_ok = true;
    return true;
}

static bool reg = spaceext::add({.name = "allies",
                                 .kind_lo = 60,
                                 .kind_hi = 69,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = on_session_end,
                                 .rows = rows,
                                 .status = status,
                                 .debug = debug});

} // namespace spaceallies
