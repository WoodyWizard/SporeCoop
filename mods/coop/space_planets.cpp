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
#include <cmath>
#include <cstdio>
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
static std::vector<Colony> colonies(uint32_t pol)
{
    std::vector<Colony> out;
    void* spd = a_Spd ? *(void**)a_Spd : nullptr;
    if (!spd || !pol)
        return out;
    void** b = F<void**>(spd, 0x20);
    void** e = F<void**>(spd, 0x24);
    if (!b || e < b || e - b > 256)
        return out;
    for (void** it = b; it < e && out.size() < kMaxColonies; ++it) {
        void* rec = *it ? F<void*>(*it, 0x13C) : nullptr;
        if (!rec)
            continue;
        Colony c;
        c.id = F<uint32_t>(rec, 0x184);
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
    auto cols = colonies(pol);
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
    net::Writer w;
    w.u32(pol);
    w.u8((uint8_t)now_v.size());
    for (auto& [id, v] : now_v) {
        w.u32(id);
        w.u8((uint8_t)v.size());
        for (float x : v)
            w.f32(x);
    }
    spaceext::send(K_SPICE, w);
    g_tx++;
}

static void host_taken(uint8_t origin, net::Reader& r)
{
    uint32_t pol = r.u32(), id = r.u32();
    uint8_t n = r.u8();
    std::vector<float> left;
    for (int i = 0; i < n && i < (int)kMaxCities; i++)
        left.push_back(r.f32());
    if (!r.ok() || pol != player_pol())
        return;
    g_taken_rx++;
    for (auto& c : colonies(pol)) {
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

// ---- guest
struct GuestColony {
    std::vector<float> last;  // the cities' values after our last write or check
    double pending_until = 0; // a collection was sent: only lower host values are written until then
};
static std::map<uint32_t, GuestColony> g_gc;
static double g_last_check = -100;
static uint32_t g_rx, g_written, g_taken_tx;

static void guest_check(double now, bool force = false)
{
    if (!force && now - g_last_check < kCheckEvery)
        return;
    g_last_check = now;
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
    if (!active())
        return;
    if (spaceext::is_host())
        host_tick(now);
    else
        guest_check(now);
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    if (!active())
        return;
    if (kind == K_SPICE && origin == 0 && !spaceext::is_host())
        guest_state(r, H->time_seconds());
    else if (kind == K_SPICE_TAKEN && spaceext::is_host())
        host_taken(origin, r);
}

static void on_session_end()
{
    g_sent.clear();
    g_gc.clear();
    g_last_send = -100;
}

static std::string status()
{
    char b[200];
    snprintf(b, sizeof b, "colonyspice: fix=%d tx=%u taken_rx=%u taken=%u rx=%u written=%u taken_tx=%u", (int)g_fix, g_tx,
             g_taken_rx, g_taken_applied, g_rx, g_written, g_taken_tx);
    return b;
}

static std::string debug(const std::vector<std::string>& a)
{
    // coop.space colonyspice: this game's colonies and the stored spice of each city
    std::string s = status() + "\n";
    char b[96];
    for (auto& c : colonies(player_pol())) {
        snprintf(b, sizeof b, "colony %08X cities %zu:", c.id, c.cities.size());
        s += b;
        for (float* p : c.cities) {
            snprintf(b, sizeof b, " %.1f", *p);
            s += b;
        }
        s += "\n";
    }
    (void)a;
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
