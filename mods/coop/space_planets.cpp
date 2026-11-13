// Space feature "colonyspice" (MSG_SPACE kinds 80..84): a shared colony's stored spice is the host's
// (BUG-0537, docs/re/space.md#space-planet-life).
//
// Each city of a colony stores the spice it makes (cCityData +0x1C). The game shows it as a pickup next to
// the city (a cPlanetaryArtifact of item type 4, noun 0x2DD8C42, kept in step with the cities by
// SporeCoop::Colony_UpdateSpicePickups 0xC82B70, which the game runs for the active planet every ~6 s) and
// empties every city of the player's colonies into the cargo hold when the ship enters their star system.
// In co-op the colonies are the shared empire's, but every game kept its own cities' spice: a guest that
// came back to the home system took the colony's spice into its hold while the host's colony kept it (the
// same spice twice), and from then on the host's planet showed the pickups by every city, the guest's none
// (the detector's `Space:noun_only_host:planetary_artifact`).
//
// Now the host's cities are the colony's: the host sends the stored spice of each of its colonies' cities
// (on a change, else every 10 s); a guest writes it into its own cities (the game then puts or takes the
// pickups itself). When a guest's game takes spice out of a city (the system entry, a pickup), the guest
// tells the host, which takes the same out of its city, so the spice is collected once. Rivals mode (own
// empires) is left alone. `coop.fix space.colony_spice off`: each game keeps its own (0.7.5).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "fixes.h"
#include "modes.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

using skg::cdecl_call;

namespace spaceplanets {

enum Kind : uint8_t {
    K_SPICE = 80,       // host -> all: u32 empire pol, u8 n, n x {u32 PlanetID, u8 cities, cities x f32 stored spice}
    K_SPICE_TAKEN = 81, // guest -> host: u32 empire pol, u32 PlanetID, u8 cities, cities x f32 what is left in this game
};

static const SkHost* H;
static SkMod* M;
static uintptr_t a_Spd, a_PlayerEmpire;
static bool g_fix = true;
COOP_FIX("space.colony_spice", "BUG-0537", g_fix,
         "space: a shared colony's stored spice is collected once (the host's colony cities, the same pickups on every planet)");

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

constexpr size_t kMaxColonies = 48, kMaxCities = 16;
// BUG-0834: kMaxColonies was a cap: the 49th and later colonies (and every colony above 256 in the list) were never
// sent, so each game kept its own spice there again (the BUG-0537 double collection). Now every colony of the list
// (up to kListMax) is synced, K_SPICE goes in parts of kMaxColonies. `coop.fix space.colony_spice_all off`: the cap
// (0.7.12); `coop.space colonyspice cap <n>` (tests) lowers that old cap, so a few colonies show it.
constexpr size_t kListMax = 1024;
static bool g_all_fix = true;
COOP_FIX("space.colony_spice_all", "BUG-0834", g_all_fix,
         "space: the stored spice of every shared colony is synced, not only of the first 48");
static size_t g_old_cap = kMaxColonies;
static uint32_t g_cut;
// BUG-0840: the host's colony list also holds the mirror of a guest's own colony in a shared star system
// (space_allies.cpp add_planet_own: the guest's colony shown as one of the host's empire, with the mirror's cities):
// the host sent the mirror's spice (about 0) for that planet and the guest wrote it into its real colony. The host
// leaves such mirrors out. `coop.fix space.colony_spice_mirror off`: sent like a colony (0.7.12).
static bool g_mirror_fix = true;
COOP_FIX("space.colony_spice_mirror", "BUG-0840", g_mirror_fix,
         "space: a partner's own colony shown in a shared star system is not synced from the host's mirror of it");
static uint32_t g_mirror_skipped;
constexpr double kSendEvery = 10.0, kCheckEvery = 0.5, kPendingHold = 4.0;
constexpr float kEps = 0.01f;

struct Colony {
    uint32_t id = 0;            // PlanetID (record +0x184)
    std::vector<float*> cities; // stored spice of each city of the player's cCivData (this frame only)
};

static uint32_t player_pol()
{
    void* emp = a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr;
    return emp ? F<uint32_t>(emp, 0x84) : 0;
}

// the player's colonies (SpacePlayerData +0x20 vector<cPlanet*>, record +0x13C) and their cities
// (record +0x15C cCivData with +0x14 == the player's pol, its +0x3C cCityData vector, docs/re/space.md 5.2)
static std::vector<Colony> colonies(uint32_t pol, bool host_side = false, bool capped = true)
{
    std::vector<Colony> out;
    void* spd = a_Spd ? *(void**)a_Spd : nullptr;
    if (!spd || !pol)
        return out;
    void** b = F<void**>(spd, 0x20);
    void** e = F<void**>(spd, 0x24);
    if (!b || e < b || e - b > (g_all_fix ? (ptrdiff_t)kListMax : 256))
        return out;
    size_t cap = g_all_fix || !capped ? kListMax : g_old_cap; // (the test listing shows every colony)
    for (void** it = b; it < e; ++it) {
        if (out.size() >= cap) {
            g_cut++;
            break;
        }
        void* rec = *it ? F<void*>(*it, 0x13C) : nullptr;
        if (!rec)
            continue;
        Colony c;
        c.id = F<uint32_t>(rec, 0x184);
        if (host_side && g_mirror_fix && spaceext::allies_own_mirror(c.id)) {
            g_mirror_skipped++; // BUG-0840: a partner's own colony, shown here: the partner's game has its spice
            continue;
        }
        void** cb = F<void**>(rec, 0x15C);
        void** ce = F<void**>(rec, 0x160);
        for (void** civ = cb; civ && ce >= cb && ce - cb < 64 && civ < ce; ++civ)
            if (*civ && F<uint32_t>(*civ, 0x14) == pol) {
                void** yb = F<void**>(*civ, 0x3C);
                void** ye = F<void**>(*civ, 0x40);
                for (void** y = yb; y && ye >= yb && ye - yb < 64 && y < ye; ++y)
                    if (*y && c.cities.size() < kMaxCities)
                        c.cities.push_back(&F<float>(*y, 0x1C));
            }
        if (!c.cities.empty())
            out.push_back(std::move(c));
    }
    return out;
}

static bool active()
{
    return g_fix && spaceext::connected() && spaceext::in_space() && !spaceext::rivals();
}

// ---- host
static std::map<uint32_t, std::vector<float>> g_sent; // PlanetID -> what was sent last
static double g_last_send = -100;
static uint32_t g_tx, g_taken_rx, g_taken_applied;

static void host_tick(double now)
{
    uint32_t pol = player_pol();
    auto cols = colonies(pol, true);
    bool changed = false;
    std::map<uint32_t, std::vector<float>> now_v;
    for (auto& c : cols) {
        auto& v = now_v[c.id];
        for (float* p : c.cities)
            v.push_back(std::isfinite(*p) ? *p : 0.f);
        auto s = g_sent.find(c.id);
        if (s == g_sent.end() || s->second.size() != v.size())
            changed = true;
        else
            for (size_t i = 0; i < v.size(); i++)
                if (std::fabs(v[i] - s->second[i]) > kEps)
                    changed = true;
    }
    if (!changed && now - g_last_send < kSendEvery)
        return;
    g_last_send = now;
    g_sent = now_v;
    // in parts of at most kMaxColonies colonies (a guest applies each part to the colonies it names)
    auto it = now_v.begin();
    do {
        net::Writer w;
        w.u32(pol);
        size_t n = std::min<size_t>(kMaxColonies, (size_t)std::distance(it, now_v.end()));
        w.u8((uint8_t)n);
        for (size_t k = 0; k < n; k++, ++it) {
            w.u32(it->first);
            w.u8((uint8_t)it->second.size());
            for (float x : it->second)
                w.f32(x);
        }
        spaceext::send(K_SPICE, w);
        g_tx++;
    } while (it != now_v.end());
}

// BUG-0822: a guest's K_SPICE_TAKEN that came while the host was not in Space (an editor, an adventure: active()
// false) was dropped; the guest sends it once, so back in Space the host's next K_SPICE wrote the old amounts into
// every game and the spice could be collected again. Now it waits (memory only, for the Space world it came in) and
// is applied on the host's first Space frame back. `coop.fix space.colony_spice_away off`: dropped (0.7.12).
static bool g_away_fix = true;
COOP_FIX("space.colony_spice_away", "BUG-0822", g_away_fix,
         "space: shared colony spice a guest collects while the host is in an editor is taken from the host's colony too");
struct Taken {
    uint8_t origin;
    uint32_t pol, id, epoch;
    std::vector<float> left;
};
static std::vector<Taken> g_taken_wait;
static uint32_t g_taken_queued, g_taken_late, g_taken_stale;

static void apply_taken(const Taken& t)
{
    if (t.pol != player_pol())
        return;
    uint8_t origin = t.origin;
    uint32_t id = t.id;
    const std::vector<float>& left = t.left;
    g_taken_rx++;
    for (auto& c : colonies(t.pol, true)) {
        if (c.id != id || c.cities.size() != left.size())
            continue;
        float took = 0;
        for (size_t i = 0; i < left.size(); i++)
            if (std::isfinite(left[i]) && left[i] >= 0 && left[i] < *c.cities[i] - kEps) {
                took += *c.cities[i] - left[i];
                *c.cities[i] = left[i]; // the game's own pickup update follows (the active planet)
            }
        if (took > 0) {
            g_taken_applied++;
            H->log(M, SK_INFO, "space planets: player %u collected %.0f spice of colony %08X: taken here too", origin, took, id);
        }
    }
    g_last_send = -100; // the new values go out now
}

static bool read_taken(uint8_t origin, net::Reader& r, Taken& t)
{
    t.origin = origin;
    t.pol = r.u32();
    t.id = r.u32();
    uint8_t n = r.u8();
    for (int i = 0; i < n && i < (int)kMaxCities; i++)
        t.left.push_back(r.f32());
    t.epoch = spaceext::world_epoch();
    return r.ok();
}

// the host back in Space: the collections that came while it was away, in the same Space world
static void taken_wait_tick()
{
    if (g_taken_wait.empty())
        return;
    auto v = std::move(g_taken_wait);
    g_taken_wait.clear();
    for (auto& t : v) {
        if (t.epoch != spaceext::world_epoch()) {
            g_taken_stale++;
            continue;
        }
        g_taken_late++;
        apply_taken(t);
    }
}

// ---- guest
struct GuestColony {
    std::vector<float> last;  // the cities' values after our last write or check
    double pending_until = 0; // a collection was sent: only lower host values are written until then
};
static std::map<uint32_t, GuestColony> g_gc;
static double g_last_check = -100;
static uint32_t g_rx, g_written, g_taken_tx;
// BUG-1039: g_gc lived for the whole session: after another Space world (a rejoin or a load of the host's save) or a
// Rivals period (no sync, the values frozen), the first host K_SPICE compared the guest's new values with the old ones
// and every lower one went to the host as a collection (K_SPICE_TAKEN): the host's colonies dropped to the older
// amounts. On: the last-known values are of one Space world and of one Allies period; the first K_SPICE after either
// only writes. `coop.fix space.colony_spice_epoch off`: kept for the session (0.7.13).
static bool g_gc_epoch_fix = true;
COOP_FIX("space.colony_spice_epoch", "BUG-1039", g_gc_epoch_fix,
         "space: a guest's colony spice of an older world or of a Rivals period is not sent to the host as collected");
static uint32_t g_gc_epoch, g_gc_dropped;
static void gc_fresh()
{
    if (!g_gc_epoch_fix)
        return;
    uint32_t e = spaceext::world_epoch();
    if (spaceext::rivals() || e != g_gc_epoch) {
        if (!g_gc.empty())
            g_gc_dropped++;
        g_gc.clear();
        g_gc_epoch = spaceext::rivals() ? 0 : e;
    }
}

static void guest_check(double now, bool force = false)
{
    if (!force && now - g_last_check < kCheckEvery)
        return;
    g_last_check = now;
    gc_fresh(); // BUG-1039
    uint32_t pol = player_pol();
    for (auto& c : colonies(pol)) {
        auto it = g_gc.find(c.id);
        if (it == g_gc.end() || it->second.last.size() != c.cities.size())
            continue; // no host value for it yet
        GuestColony& g = it->second;
        bool took = false;
        std::vector<float> v;
        for (size_t i = 0; i < c.cities.size(); i++) {
            v.push_back(*c.cities[i]);
            if (std::isfinite(v[i]) && v[i] < g.last[i] - 0.5f) // taken out by this game (not our write)
                took = true;
        }
        if (took) {
            net::Writer w;
            w.u32(pol);
            w.u32(c.id);
            w.u8((uint8_t)v.size());
            for (float x : v)
                w.f32(x);
            spaceext::send(K_SPICE_TAKEN, w);
            g_taken_tx++;
            g.pending_until = now + kPendingHold;
            H->log(M, SK_INFO, "space planets: this game collected spice of colony %08X: the host is told", c.id);
        }
        g.last = v;
    }
}

static void guest_state(net::Reader& r, double now)
{
    uint32_t pol = r.u32();
    uint8_t n = r.u8();
    std::map<uint32_t, std::vector<float>> host;
    for (int k = 0; k < n && k < (int)kMaxColonies; k++) {
        uint32_t id = r.u32();
        uint8_t m = r.u8();
        auto& v = host[id];
        for (int i = 0; i < m && i < (int)kMaxCities; i++)
            v.push_back(r.f32());
    }
    if (!r.ok() || pol != player_pol()) // another galaxy (the player's empire is not the host's)
        return;
    g_rx++;
    guest_check(now, true); // a collection of this game before the host's values are written
    for (auto& c : colonies(pol)) {
        auto h = host.find(c.id);
        if (h == host.end() || h->second.size() != c.cities.size())
            continue;
        GuestColony& g = g_gc[c.id];
        bool hold = now < g.pending_until;
        g.last.resize(c.cities.size());
        for (size_t i = 0; i < c.cities.size(); i++) {
            float hv = h->second[i];
            if (!std::isfinite(hv) || hv < 0 || hv > 1e6f)
                continue;
            if (std::fabs(*c.cities[i] - hv) > kEps && (!hold || hv < *c.cities[i])) {
                *c.cities[i] = hv;
                g_written++;
            }
            g.last[i] = *c.cities[i];
        }
    }
}

// ---- feature
static void tick(double now)
{
    if (!spaceext::is_host())
        gc_fresh(); // BUG-1039: a Rivals period or another world ends the last-known values
    if (!active())
        return;
    if (spaceext::is_host()) {
        taken_wait_tick(); // BUG-0822: first what came while away
        host_tick(now);
    } else
        guest_check(now);
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    // BUG-0822: the host out of Space in a session (an editor, an adventure) keeps a guest's collection for later
    if (kind == K_SPICE_TAKEN && spaceext::is_host() && !active() && g_away_fix && g_fix && spaceext::connected() &&
        !spaceext::rivals() && spaceext::world_epoch()) {
        Taken t;
        if (read_taken(origin, r, t) && g_taken_wait.size() < 256) {
            g_taken_wait.push_back(std::move(t));
            g_taken_queued++;
        }
        return;
    }
    if (!active())
        return;
    if (kind == K_SPICE && origin == 0 && !spaceext::is_host())
        guest_state(r, H->time_seconds());
    else if (kind == K_SPICE_TAKEN && spaceext::is_host()) {
        Taken t;
        if (read_taken(origin, r, t))
            apply_taken(t);
    }
}

static void on_session_end()
{
    g_sent.clear();
    g_gc.clear();
    g_gc_epoch = 0;
    g_last_send = -100;
    g_taken_wait.clear();
    g_old_cap = kMaxColonies;
}

static std::string status()
{
    char b[320];
    snprintf(b, sizeof b,
             "colonyspice: fix=%d tx=%u taken_rx=%u taken=%u rx=%u written=%u taken_tx=%u queued=%u late=%u stale=%u cut=%u "
             "mirrors_skipped=%u gc_dropped=%u",
             (int)g_fix, g_tx, g_taken_rx, g_taken_applied, g_rx, g_written, g_taken_tx, g_taken_queued, g_taken_late,
             g_taken_stale, g_cut, g_mirror_skipped, g_gc_dropped);
    return b;
}

static std::string debug(const std::vector<std::string>& a)
{
    // coop.space colonyspice [set <PlanetID hex> <spice> | cap <n>]: this game's colonies and the stored spice of each city
    char b[96];
    if (a.size() > 2 && a[0] == "set") { // tests: every city of that colony stores <spice> (as the game's production does)
        uint32_t id = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        float v = (float)atof(a[2].c_str());
        int n = 0;
        for (auto& c : colonies(player_pol(), false, false))
            if (c.id == id)
                for (float* p : c.cities)
                    *p = v, n++;
        snprintf(b, sizeof b, "colony %08X: %d cities set to %.1f\n", id, n, v);
        return b;
    }
    if (a.size() > 1 && a[0] == "cap") { // tests (BUG-0834): the old cap of space.colony_spice_all off
        g_old_cap = (size_t)std::max(1, atoi(a[1].c_str()));
        snprintf(b, sizeof b, "old cap %u colonies (used with space.colony_spice_all off)\n", (unsigned)g_old_cap);
        return b;
    }
    std::string s = status() + "\n";
    for (auto& c : colonies(player_pol(), false, false)) {
        snprintf(b, sizeof b, "colony %08X cities %zu:", c.id, c.cities.size());
        s += b;
        for (float* p : c.cities) {
            snprintf(b, sizeof b, " %.1f", *p);
            s += b;
        }
        s += "\n";
    }
    return s;
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    a_Spd = H->sym("Simulator::SpacePlayerData::ptr");
    a_PlayerEmpire = H->sym("Simulator::GetPlayerEmpire");
    return a_Spd && a_PlayerEmpire;
}

static bool reg = spaceext::add({.name = "colonyspice",
                                 .kind_lo = 80,
                                 .kind_hi = 84,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = on_session_end,
                                 .status = status,
                                 .debug = debug});

} // namespace spaceplanets
