#include "shared.h"
#include "game.h"

#include <cmath>
#include <cstdio>
#include <functional>
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

// BUG-0572: the game's own spending (cTribe::AddFood -n -> cBundleManager::RemoveBundles) removes the emptied
// bundles: the vector at tribe+0x210 is then empty (begin == end), and s_food only edited the amounts of the bundles
// there, so a guest whose game spent its last food kept 0 for good while the host's total went up. On: a positive
// change the bundles cannot hold is made by the game's own AddFood (it creates the bundles, as for an NPC tribe's
// gathering), and a never-filled container (both pointers null) reads as 0 food. Off: the code before.
static bool g_food_add_fix = true;
COOP_FIX("res.food_add", "BUG-0572", g_food_add_fix,
         "shared: a guest's tribe that has no food bundles left takes the host's food through the game's AddFood");

static bool food_bundles(void* tribe, float**& b, float**& e)
{
    uint8_t* cont = (uint8_t*)tribe + 0x20C;
    b = *(float***)(cont + 4);
    e = *(float***)(cont + 8);
    if (g_food_add_fix && !b && !e)
        return true; // an empty container that never held a bundle
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

static uint32_t g_food_created; // tests: totals the game's AddFood made bundles for (BUG-0572)

static void s_food(double v)
{
    void* t = player_tribe();
    float **b, **e;
    double cur;
    if (!t || !food_bundles(t, b, e) || (b == e && !g_food_add_fix) || !g_food(cur))
        return;
    double d = v - cur;
    const float shown = (float)d; // UpdateFoodVisuals shows "+N/-N": it takes the change, not the total
    if (g_food_add_fix && d >= 0.5 && (b == e || !*b)) {
        // BUG-0572: no bundle to edit: the game's own AddFood makes them (whole food; the rest below)
        uintptr_t add = S("SporeCoop::cTribe_AddFood");
        if (!add)
            return;
        int n = (int)std::floor(d);
        skg::thiscall<void>(add, t, n);
        g_food_created++;
        d -= n;
        if (!food_bundles(t, b, e))
            return;
    }
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
    if (std::fabs(shown) >= 1.f)
        game::tribe_food_visuals(t, shown); // BUG-0585: not without the tribe's food mat
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
    double owed = 0;                   // host (BUG-0691): guests' changes acked while the game could not hold them
    std::map<uint8_t, double> causes;  // host (BUG-0696): each player's change since the last total
    double applied = 0;                // host (BUG-0696): the guests' changes written into the game since then
    uint32_t epoch = 0;                // guest (BUG-0692): game::stage_epoch(mode) of the world the sync state is of
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
// BUG-0231: a guest that plays another world of the host's stage took the host's money / food
// total into its own world (and sent its own world's changes to the host). Held while elsewhere
// (SharedLedger::pause: the sync state waits, nothing is lost). Off: totals applied while elsewhere.
static bool g_elsewhere_fix = true;
COOP_FIX("res.elsewhere", "BUG-0231", g_elsewhere_fix, "shared: a guest in another world keeps its own money and food");
// BUG-0691: the host returned before the ack when its game could not read the resource (a load, the galaxy menu, no
// player tribe / civilization): a guest's credit sent then was never counted and never acked (the guest showed it
// in flight for good, the host never had it). On: the host always acks and keeps the change as owed, paid into its
// game's value in the first tick the resource is readable again in its mode. Off: dropped (0.7.10).
static bool g_owed_fix = true;
COOP_FIX("res.owed", "BUG-0691", g_owed_fix,
         "shared: a guest's money / food credit reaching the host while it loads or is in a menu is counted when it is back");
// BUG-0692: a guest's tick dropped its sync state (have = false) in every frame out of the resource's mode, an editor
// visit included; back in the stage, a spend before the host's next total was not folded and that total undid it
// (the purchase made, the money back) and the host never counted it. On: out of the mode, the guest keeps the sync
// state while the stage's world stays the same (game::stage_epoch: an editor visit keeps it, a load or another
// world raises it). Off: dropped on every mode switch (0.7.10).
static bool g_mode_keep_fix = true;
COOP_FIX("res.mode_keep", "BUG-0692", g_mode_keep_fix,
         "shared: a guest's purchase right after an editor visit is not undone by the host's money / food total");
// tests (coop_res_away.py): `coop.res block <s>`: the resources read as unavailable for <s> s (the host in a load or
// the menu, BUG-0691); `coop.res away <s> <name> <change>`: the tick sees an editor's mode for <s> s, then the change
// is made in the first frame back, before the tick samples (a purchase right after an editor, BUG-0692)
static double g_block_until, g_away_until;
static std::string g_away_name;
static double g_away_change;
static bool g_away_due;
static uint32_t g_owed_paid; // tests: owed changes paid into the host's game
static bool get_now(Res& r, double& v, double now) { return now >= g_block_until && r.get(v); }
// a guest's own changes go to the host as MSG_RES_ADD; off: kept back (tests only, BUG-0199)
static bool g_guest_send = true;
COOP_FIX("res.guest_send", "BUG-0199", g_guest_send, "shared: a guest's own earnings and spendings go to the host");
// BUG-0696: the shared total (MSG_RES) carried no cause: a guest's money or food jumped and nobody said whose purchase
// or earning it was. On: the host appends each player's change since its last total (u8 n, {u8 player, f64 change},
// after the ack list; older guests stop reading before it) and every game shows a notice for another player's change
// of 25 or more; the host's own change is its game's change minus the guests'. Off: no cause (0.7.10).
static bool g_cause_fix = true;
COOP_FIX("res.cause", "BUG-0696", g_cause_fix, "shared: a notice says whose purchase or earning changed the shared money or food");
static std::function<void(const std::string&)> g_feed;
static uint32_t g_causes_shown;
static constexpr double kCauseMin = 25; // a change below it is not said (the cities' income, a single food)
static const char* label(const Res& r) { return r.id == 1 ? "food" : "money"; }
static void say_cause(net::Session& s, const Res& r, uint8_t player, double v)
{
    if (!g_feed || std::fabs(v) < kCauseMin || player == s.my_id())
        return;
    char b[160];
    snprintf(b, sizeof b, "%s %s %.0f %s (shared)", s.name_of(player).c_str(), v > 0 ? "earned" : "spent", std::fabs(v),
             label(r));
    g_feed(b);
    g_causes_shown++;
}
// BUG-1009: a guest's change the floor (0) or the money cap cut was counted in full in `applied` and in the guest's
// cause: the next total's own part (v - synced - applied) then named the difference as the host's earning or spending
// ("<host> earned 300 (shared)" when only the pool ran dry). On: what the game really took is counted.
// `coop.fix shared.cause_clamped off`: the change as sent (0.7.13).
static bool g_cause_clamp_fix = true;
COOP_FIX("shared.cause_clamped", "BUG-1009", g_cause_clamp_fix,
         "shared: a guest's change cut at 0 by the shared money's floor is counted as cut in the notices' causes");
// add `change` to the resource: the part the game really took (the floor at 0, money_clamp's cap), or the change
// itself with the fix off
static double add_counted(Res& r, double cur, double change)
{
    r.set(net::shared_add(cur, change, g_floor_fix));
    double after;
    if (!g_cause_clamp_fix || !r.get(after) || !std::isfinite(after))
        return change;
    return after - cur;
}
static bool g_hold;          // the last tick: the guest is elsewhere and the totals are held
static uint32_t g_held_totals; // tests: host totals ignored while held

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
        r.owed = 0;
        r.causes.clear();
        r.applied = 0;
    }
}

void on_player_leave(uint8_t id)
{
    for (auto& r : g_res)
        r.acked.erase(id); // the id is given to the next newcomer, whose sequence starts at 1
}

void tick(net::Session& s, uint32_t mode, double now, bool elsewhere)
{
    const uint32_t real_mode = mode;
    if (now < g_away_until)
        mode = modes::kEditor; // tests: an editor visit as the tick sees it
    g_mode = mode;
    g_hold = g_elsewhere_fix && elsewhere && s.role() == net::Role::Client;
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
        if (g_away_due && now >= g_away_until && r.name == g_away_name && r.mode == real_mode && r.get(v)) {
            g_away_due = false; // tests: the purchase in the first frame back from the editor
            r.set(v + g_away_change);
            H->log(M, SK_INFO, "shared %s: the test's purchase %+.1f in the first frame back (synced=%d)", r.name, g_away_change,
                   (int)r.have);
        }
        if (own_budget(r)) { // paused: unsent changes stay with the player (space_rivalry.cpp splits)
            r.have = false;
            r.led.reset();
            continue;
        }
        if (r.mode != mode || !get_now(r, v, now)) {
            // BUG-0692: a guest out of the mode (an editor) keeps its sync state while the stage's world is the same
            if (!(g_mode_keep_fix && s.role() == net::Role::Client && r.have && r.mode != mode &&
                  game::stage_epoch(r.mode) == r.epoch)) {
                if (r.have && g_away_due)
                    H->log(M, SK_INFO, "shared %s: sync state dropped out of its mode (test)", r.name);
                r.have = false;
            }
            continue;
        }
        if (g_mode_keep_fix && s.role() == net::Role::Client && r.have && game::stage_epoch(r.mode) != r.epoch)
            r.have = false; // another world of the stage: wait for the host's total
        if (s.role() == net::Role::Host && r.owed != 0 && !g_hold) { // BUG-0691: pay what was acked while away
            r.applied += add_counted(r, v, r.owed); // BUG-1009: what the game took
            H->log(M, SK_INFO, "shared %s %+.1f owed to the guests paid", r.name, r.owed);
            r.owed = 0;
            g_owed_paid++;
            if (!r.get(v))
                continue;
        }
        if (g_hold) { // BUG-0231: another world: its value is not the pack's; the ledger waits
            r.have = false;
            r.led.pause();
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
                    if (g_cause_fix && r.have) { // BUG-0696: whose changes these are (older guests stop before)
                        double own = v - r.synced - r.applied;
                        if (std::fabs(own) >= kCauseMin)
                            r.causes[0] += own;
                        w.u8((uint8_t)std::min<size_t>(r.causes.size(), 16));
                        size_t i = 0;
                        for (auto& [id, c] : r.causes) {
                            if (i++ >= 16)
                                break;
                            w.u8(id);
                            w.f64(c);
                        }
                    }
                }
                r.causes.clear();
                r.applied = 0;
                r.ack_dirty = false;
                s.broadcast_reliable(net::MSG_RES, w);
                r.synced = v;
                r.have = true;
                r.last_send = now;
            }
            continue;
        }
        // guest: whatever changed locally since the last host total is our own earning/spending
        // (`coop.fix res.guest_send off`: nothing is sent, the host's next total undoes it: the twin of the
        // tests of a guest's earning in its own game, e.g. space_store, BUG-0199)
        if (!r.have || !g_guest_send)
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
        const double t = H ? H->time_seconds() : 0;
        if (!get_now(r, cur, t)) {
            if (!(g_owed_fix && type == net::MSG_RES_ADD && s.role() == net::Role::Host))
                return;
            // BUG-0691: the host's game cannot hold the change now (a load, the menu): ack it, pay it when back
            if (rd.left() >= 4) {
                uint32_t seq = rd.u32();
                if (rd.ok()) {
                    r.acked[from] = seq;
                    r.ack_dirty = true;
                }
            }
            if (std::fabs(v) < 1e7) {
                r.owed += v;
                r.causes[from] += v;
            }
            (v > 0 ? r.gained : r.spent) += v;
            H->log(M, SK_INFO, "shared %s %+.1f from player %u owed (the game cannot hold it now)", r.name, v, from);
            return;
        }
        if (type == net::MSG_RES && s.role() == net::Role::Client) {
            if (!r.led.take_total(g_hold)) {
                g_held_totals++;
                return; // BUG-0231: the guest plays another world
            }
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
                if (g_cause_fix && rd.left() >= 1) { // BUG-0696: whose changes moved the total
                    uint8_t nc = rd.u8();
                    for (int i = 0; i < nc && rd.ok(); i++) {
                        uint8_t pid = rd.u8();
                        double c = rd.f64();
                        if (rd.ok() && std::isfinite(c))
                            say_cause(s, r, pid, c);
                    }
                }
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
            r.epoch = game::stage_epoch(r.mode); // BUG-0692: the world this sync state is of
            if (g_away_due)
                H->log(M, SK_INFO, "shared %s: a host total %.1f taken (test)", r.name, v);
        } else if (type == net::MSG_RES_ADD && s.role() == net::Role::Host) {
            if (rd.left() >= 4) {
                uint32_t seq = rd.u32();
                if (rd.ok()) {
                    r.acked[from] = seq;
                    r.ack_dirty = true;
                }
            }
            if (std::fabs(v) < 1e7) {
                double d = add_counted(r, cur, v); // BUG-1009: the part the floor / the cap let through
                r.applied += d;
                r.causes[from] += d;
                say_cause(s, r, from, d); // BUG-0696: on the host at once
            }
            (v > 0 ? r.gained : r.spent) += v; // tests: what the guests changed
            H->log(M, SK_INFO, "shared %s %+.1f from player %u", r.name, v, from);
        }
        return;
    }
}

std::string brief()
{
    std::string out;
    char b[64];
    for (auto& r : g_res) {
        double v;
        if (r.mode != g_mode || !r.get(v))
            continue;
        snprintf(b, sizeof b, "%s%s=%.0f%s", out.empty() ? "" : " ", r.name, v, r.have ? "" : "(unsynced)");
        out += b;
    }
    return out;
}

std::string status()
{
    std::string out;
    char b[192];
    for (auto& r : g_res) {
        double v;
        // only this mode's resources: in Space the player cCivilization still exists and its
        // money would show up as a second "money" line (and take debug_add's money)
        if (r.mode != g_mode || !r.get(v))
            continue;
        double fl = 0;
        for (auto& e : r.led.in_flight)
            fl += e.v;
        snprintf(b, sizeof b, "shared %s=%.1f synced=%d pending=%.1f in_flight=%.1f from_guests=+%.0f/%.0f held=%d/%u created=%u owed=%.1f/%u causes=%u\n",
                 r.name, v, r.have, r.led.pending, fl, r.gained, r.spent, (int)r.led.held, g_held_totals, g_food_created,
                 r.owed, g_owed_paid, g_causes_shown);
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

void set_feed(std::function<void(const std::string&)> f) { g_feed = std::move(f); }

std::string test_knob(const std::string& what, double secs, const std::string& name, double change)
{
    double t = H ? H->time_seconds() : 0;
    if (what == "block") {
        g_block_until = t + secs;
        return "shared: resources unreadable for " + std::to_string(secs) + " s\n";
    }
    if (what == "away") {
        g_away_until = t + secs;
        g_away_name = name;
        g_away_change = change;
        g_away_due = !name.empty();
        return "shared: an editor's mode for " + std::to_string(secs) + " s, then " + name + " " + std::to_string(change) + "\n";
    }
    return "usage: coop.res block <s> | coop.res away <s> [<name> <change>]\n";
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
