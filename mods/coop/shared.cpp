#include "shared.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "modes.h"
#include "net/shared_ledger.h"
#include "space_ext.h"
#include "sporekit_game.hpp"
#include "fixes.h"

namespace shared {

static const SkHost* H;
static SkMod* M;

static uintptr_t S(const char* n) { return H ? H->sym(n) : 0; }

static void* noun_manager()
{
    uintptr_t get = S("Simulator::cGameNounManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// ---- resource accessors (nullptr address = not available in this mode/state)
// tribe food = sum of the food bundles in the tribe's cGameBundleContainer (+0x20C, verified:
// cTribe::GetTotalFood 0xC8E850 sums it); bundle list = vector at container +4, amount (float) at
// bundle +0x134. cGameNounManager::mpPlayerTribe is at +0x70.
static void* player_tribe()
{
    void* nm = noun_manager();
    void* t = nm ? skg::field<void*>(nm, 0x70) : nullptr;
    return t && skg::readable(t, 0x230) ? t : nullptr;
}

static bool food_bundles(void* tribe, float**& b, float**& e)
{
    uint8_t* cont = (uint8_t*)tribe + 0x20C;
    b = *(float***)(cont + 4);
    e = *(float***)(cont + 8);
    return b && e >= b && e - b < 64 && skg::readable(b, (e - b) * 4 + 1);
}

static bool g_food(double& v)
{
    void* t = player_tribe();
    float **b, **e;
    if (!t || !food_bundles(t, b, e))
        return false;
    v = 0;
    for (float** it = b; it < e; it++)
        if (*it && skg::readable(*it, 0x138))
            v += *(float*)((uint8_t*)*it + 0x134);
    return true;
}

static void s_food(double v)
{
    void* t = player_tribe();
    float **b, **e;
    double cur;
    if (!t || !food_bundles(t, b, e) || b == e || !g_food(cur))
        return;
    double d = v - cur;
    const float shown = (float)d; // UpdateFoodVisuals shows "+N/-N": it takes the change, not the total
    for (float** it = b; it < e && std::fabs(d) > 1e-4; it++) { // spend from the bundles in order
        if (!*it || !skg::readable(*it, 0x138))
            continue;
        float& amt = *(float*)((uint8_t*)*it + 0x134);
        double n = amt + d;
        if (n < 0) {
            d = n;
            amt = 0;
        } else {
            amt = (float)n;
            d = 0;
        }
    }
    if (uintptr_t f = S("Simulator::cTribe::UpdateFoodVisuals"))
        if (std::fabs(shown) >= 1.f)
            skg::thiscall<void>(f, t, shown);
}

// civilization: GetPlayerCivilization() -> cCivilization::mWealth (+0x98)
static float* civ_money()
{
    void* nm = noun_manager();
    uintptr_t f = S("Simulator::cGameNounManager::GetPlayerCivilization");
    void* civ = nm && f ? skg::thiscall<void*>(f, nm) : nullptr;
    return civ && skg::readable(civ, 0x9C) ? (float*)((uint8_t*)civ + 0x98) : nullptr;
}

// space: GetPlayerEmpire() -> cEmpire::mEmpireMoney (+0xD0, int)
static int* space_money()
{
    uintptr_t f = S("Simulator::GetPlayerEmpire");
    void* emp = f ? skg::cdecl_call<void*>(f) : nullptr;
    return emp && skg::readable(emp, 0xD4) ? (int*)((uint8_t*)emp + 0xD0) : nullptr;
}

struct Res {
    uint8_t id;
    const char* name;
    uint32_t mode;
    bool (*get)(double&);
    void (*set)(double);
    // state
    bool have = false;   // synced at least once in this session
    double synced = 0;   // value after the last sync (guest) / last broadcast (host)
    net::SharedLedger led; // guest: changes not sent yet / sent and not counted by the host yet
    double last_send = 0;
    double gained = 0, spent = 0; // host: sum of the guests' positive / negative changes
    std::map<uint8_t, uint32_t> acked; // host: last change counted per guest (echoed in MSG_RES)
    bool ack_dirty = false;            // host: a change was counted, send the total with its ack
};

// Switches for tests (`coop.res fix <name> 0|1`); 0 = the code before the fix:
//  reset: the sync state ends with the session, a guest sends nothing before WELCOME (BUG-0049)
//  floor: the totals never go below 0, a guest keeps its in-flight changes (BUG-0106)
static bool g_reset_fix = true;
static bool g_floor_fix = true;
COOP_FIX("res.reset", "BUG-0049", g_reset_fix, "shared: the sync state ends with the session, a guest sends nothing before WELCOME");
COOP_FIX("res.floor", "BUG-0106", g_floor_fix, "shared: totals never go below 0, a guest keeps its in-flight changes");
// BUG-0220: a guest keeps its sync state when another player joins and folds an unsampled local
// change into its report before it takes a host total (a spend at a join came back, spent twice)
static bool g_join_keep_fix = true;
COOP_FIX("res.join_keep", "BUG-0220", g_join_keep_fix, "shared: a guest's spend at another player's join is not undone");

// the game's AddMoney clamps to 0..99 999 999; raw writes of a total do the same (BUG-0106)
static double money_clamp(double v)
{
    if (!g_floor_fix)
        return v;
    return v < 0 ? 0 : v > 99999999.0 ? 99999999.0 : v;
}

static bool g_civ(double& v) { float* p = civ_money(); if (p) v = *p; return p; }
static void s_civ(double v) { if (float* p = civ_money()) *p = (float)money_clamp(v); }
static bool g_space(double& v) { int* p = space_money(); if (p) v = *p; return p; }
static void s_space(double v) { if (int* p = space_money()) *p = (int)std::lround(money_clamp(v)); }

static Res g_res[] = {
    {1, "food", 0x1654C02, g_food, s_food},
    {2, "money", 0x1654C04, g_civ, s_civ},
    {3, "money", 0x1654C05, g_space, s_space},
};

static uint32_t g_mode; // the game mode of the last tick: status / debug_add use its resources

// rivals mode (space_rivalry.cpp): every player has its own Space budget, nothing is shared
static bool own_budget(const Res& r) { return r.mode == modes::kSpace && spaceext::rivals(); }

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
}

void on_session_change()
{
    for (auto& r : g_res)
        r.have = false;
}

void on_player_join(net::Role role)
{
    if (net::reset_on_join(role == net::Role::Host, g_join_keep_fix))
        on_session_change(); // the host: the newcomer gets the totals at once
}

void on_session_end()
{
    if (!g_reset_fix)
        return;
    for (auto& r : g_res) {
        r.have = false;
        r.led.reset();
        r.acked.clear();
        r.ack_dirty = false;
    }
}

void on_player_leave(uint8_t id)
{
    for (auto& r : g_res)
        r.acked.erase(id); // the id is given to the next newcomer, whose sequence starts at 1
}

void tick(net::Session& s, uint32_t mode, double now)
{
    g_mode = mode;
    if (s.role() == net::Role::None) {
        on_session_end(); // nothing changed offline is ever reported to a host (BUG-0049)
        return;
    }
    if (g_reset_fix && s.role() == net::Role::Client && !s.connected()) {
        on_session_end(); // joining: wait for WELCOME and the host's first total
        return;
    }
    for (auto& r : g_res) {
        double v;
        if (own_budget(r)) { // paused: unsent changes stay with the player (space_rivalry.cpp splits)
            r.have = false;
            r.led.reset();
            continue;
        }
        if (r.mode != mode || !r.get(v)) {
            r.have = false;
            continue;
        }
        if (s.role() == net::Role::Host) {
            // broadcast the total when it changed (and once per 2 s as a safety net)
            if (!r.have || r.ack_dirty || std::fabs(v - r.synced) > 1e-3 || now - r.last_send > 2.0) {
                net::Writer w;
                w.u8(r.id);
                w.f64(v);
                if (g_floor_fix) { // older guests stop reading here
                    w.u8((uint8_t)r.acked.size());
                    for (auto& [id, seq] : r.acked) {
                        w.u8(id);
                        w.u32(seq);
                    }
                }
                r.ack_dirty = false;
                s.broadcast_reliable(net::MSG_RES, w);
                r.synced = v;
                r.have = true;
                r.last_send = now;
            }
            continue;
        }
        // guest: whatever changed locally since the last host total is our own earning/spending
        if (!r.have)
            continue; // wait for the first total from the host
        double d = v - r.synced;
        if (std::fabs(d) > 1e-3) {
            r.led.pending += d;
            r.synced = v;
        }
        if (r.led.pending != 0 && now - r.last_send > 0.25) {
            net::Writer w;
            w.u8(r.id);
            w.f64(r.led.pending);
            uint32_t seq = r.led.send();
            if (g_floor_fix)
                w.u32(seq); // older hosts stop reading before it
            else
                r.led.in_flight.clear();
            s.send_reliable_to(0, net::MSG_RES_ADD, w);
            r.last_send = now;
        }
    }
}

void on_message(net::Session& s, uint8_t type, uint8_t from, net::Reader& rd)
{
    uint8_t id = rd.u8();
    double v = rd.f64();
    if (!rd.ok() || !std::isfinite(v))
        return;
    for (auto& r : g_res) {
        if (r.id != id)
            continue;
        if (own_budget(r))
            return; // a total or a change sent before the switch to rivals mode: not shared any more
        double cur;
        if (!r.get(cur))
            return;
        if (type == net::MSG_RES && s.role() == net::Role::Client) {
            if (!g_floor_fix) { // before BUG-0106: only the unsent change is kept
                r.set(v + r.led.pending);
                r.synced = v + r.led.pending;
                r.have = true;
                return;
            }
            if (g_join_keep_fix && r.have)
                r.led.fold(cur, r.synced); // a change since the last tick is ours, not undone
            // keep changes made since our last report and those the host has not counted yet
            bool has_ack = false;
            uint32_t ack = 0;
            if (rd.left() >= 1) {
                uint8_t n = rd.u8();
                for (int i = 0; i < n && rd.ok(); i++) {
                    uint8_t pid = rd.u8();
                    uint32_t q = rd.u32();
                    if (rd.ok() && pid == s.my_id()) {
                        has_ack = true;
                        ack = q;
                    }
                }
                if (!rd.ok())
                    return;
                if (!has_ack) { // the host counted nothing of ours in this session yet
                    has_ack = true;
                    ack = r.led.next_seq - 1 - (uint32_t)r.led.in_flight.size();
                }
            }
            double shown = r.led.shown(v, has_ack, ack);
            r.set(shown);
            double got;
            // what the game holds now: a floored food bundle or money is not an earning next tick
            r.synced = r.get(got) ? got : shown;
            r.have = true;
        } else if (type == net::MSG_RES_ADD && s.role() == net::Role::Host) {
            if (rd.left() >= 4) {
                uint32_t seq = rd.u32();
                if (rd.ok()) {
                    r.acked[from] = seq;
                    r.ack_dirty = true;
                }
            }
            if (std::fabs(v) < 1e7)
                r.set(net::shared_add(cur, v, g_floor_fix));
            (v > 0 ? r.gained : r.spent) += v; // tests: what the guests changed
            H->log(M, SK_INFO, "shared %s %+.1f from player %u", r.name, v, from);
        }
        return;
    }
}

std::string status()
{
    std::string out;
    char b[128];
    for (auto& r : g_res) {
        double v;
        // only this mode's resources: in Space the player cCivilization still exists and its
        // money would show up as a second "money" line (and take debug_add's money)
        if (r.mode != g_mode || !r.get(v))
            continue;
        double fl = 0;
        for (auto& e : r.led.in_flight)
            fl += e.v;
        snprintf(b, sizeof b, "shared %s=%.1f synced=%d pending=%.1f in_flight=%.1f from_guests=+%.0f/%.0f\n", r.name, v,
                 r.have, r.led.pending, fl, r.gained, r.spent);
        out += b;
    }
    return out.empty() ? "no shared resources in this mode\n" : out;
}

std::string fix(const std::string& name, int on)
{
    bool* f = name == "reset" ? &g_reset_fix : name == "floor" ? &g_floor_fix : nullptr;
    if (!f)
        return "usage: coop.res fix reset|floor [0|1]\n";
    if (on >= 0)
        *f = on != 0;
    return "shared fix " + name + " " + (*f ? "on" : "off") + "\n";
}

bool debug_add(const std::string& name, double v, std::string& err)
{
    for (auto& r : g_res) {
        double cur;
        if (name == r.name && r.mode == g_mode && r.get(cur)) {
            r.set(cur + v);
            return true;
        }
    }
    err = "no shared '" + name + "' here";
    return false;
}

} // namespace shared
