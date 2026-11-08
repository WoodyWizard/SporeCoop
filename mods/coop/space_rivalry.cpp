// Rivals mode: a host option that makes every player an empire of its own (docs/09-coop-design.md,
// Space 0.7 feature 10; how it works in the game: docs/re/space.md 11).
//
// * Mode: the host switches between "allies" (one empire: shared money, shared help) and "rivals".
//   The host is the authority for the mode and for the relations; it broadcasts the whole state
//   (K_STATE) on every change, when a player joins or leaves, and every 5 s.
// * Budgets: in rivals mode shared.cpp stops sharing the Space money (it asks spaceext::rivals()).
//   Switching to rivals splits the shared money fairly: everybody gets total / players (the host
//   the rest of the division) plus what it earned or spent in the last moments that the host had
//   not counted yet. Switching back merges the budgets: the host adds the budgets the players
//   reported last (K_MONEY, every second), each player sends the difference between that report
//   and its money at the switch (K_MERGE_FIX), then shared.cpp syncs the sum as before. Money is
//   neither created nor lost by switching back and forth.
// * Relations between two players: neutral (the default in rivals mode), ally (proposed by one,
//   accepted by the other), war (declared by one, everybody is told), peace (proposed by one of
//   the two, accepted by the other: back to neutral). Kept by player name on the host, so a player
//   who rejoins (maybe with another id) gets its relations back.
// * War = PvP: a partner's ship is a Defender puppet with our own political id, and the game's
//   weapons never hurt their own empire (the projectile explosion skips targets of the attacker's
//   political id, TakeDamage returns on it too). The puppet of a player we are at war with gets
//   political id -1 (no empire, like pirates): our weapons hit it, squadron's TakeDamage hook asks
//   on_hit(), the hit goes to the puppet's owner (K_HIT) who applies it to its own ship through
//   the game's TakeDamage (the same hook: the rescue filter, then the native death and respawn).
//   The rescue never helps a player at war with the ship (spacecoop.cpp partners_here).
#include "space_rivalry.h"
#include "player_life.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game.h"
#include "modes.h"
#include "space_ext.h"
#include "sporekit_game.hpp"

using spaceext::Relation;

namespace rivalry {

static const SkHost* H;
static SkMod* M;
static uintptr_t a_TakeDamage, a_PlayerUFOGet, a_PlayerEmpire, a_PlayerEmpireID, a_AddMoney;

// cGameDataUFO (docs/re/space.md 0): political id +0x30, combatant +0x508, state +0x53C (2 = dead),
// hp +0x540 (the puppets: modes::space_puppet_ok)
constexpr uintptr_t kPol = 0x30, kCombat = 0x508, kState = 0x53C, kHp = 0x540;
constexpr uint32_t kEnemyPol = 0xFFFFFFFFu; // "no empire": hostile to everybody, like pirates

enum Kind : uint8_t { K_STATE = 50, K_REQ = 51, K_EVENT = 52, K_HIT = 53, K_MONEY = 54, K_MERGE_FIX = 55, K_ASK = 56, K_SET = 57 };
enum Action : uint8_t { A_RIVALS = 1, A_ALLIES = 2, A_PROPOSE_ALLY = 3, A_ACCEPT = 4, A_WAR = 5, A_PROPOSE_PEACE = 6 };
enum Event : uint8_t { E_RIVALS = 1, E_ALLIES = 2, E_PROPOSE_ALLY = 3, E_ALLIANCE = 4, E_WAR = 5, E_PROPOSE_PEACE = 6, E_PEACE = 7 };
enum PropKind : uint8_t { P_ALLY = 1, P_PEACE = 2 };

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

// ---------------- state of every machine (the host's copy is the authority) ----------------
struct Prop {
    uint8_t from, to, kind;
};
static std::vector<Prop> g_props;           // pending proposals (ids)
static std::map<uint8_t, int> g_budget;     // player -> its own budget (rivals mode, K_MONEY)
static bool g_have_state;                   // a K_STATE arrived in this session
static uint16_t g_split_seen, g_merge_seen; // the last split / merge applied
static bool g_split_pending, g_merge_pending;
static int g_split_delta, g_merge_credit;
static double g_last_report;
static int g_last_reported = -1;
static double g_last_attacked;
static uint32_t g_hits_sent, g_hits_got, g_pvp_deaths, g_splits, g_merges, g_enemy_puppets;
static float g_dmg_got;
static uint32_t g_puppet_hits, g_restored;
// a guest's budget after a (re)join or a world load: the host's copy of it is the true one (a
// rejoining guest loads the host's save again, whose money is not its budget); no reports until then
static bool g_budget_known, g_was_in_space;
static double g_last_ask;
static int g_last_hit_type, g_last_hit_att;

// ---------------- the host's authority ----------------
struct NameProp {
    std::string from, to;
    uint8_t kind;
};
static std::map<std::pair<std::string, std::string>, Relation> g_rel_names; // sorted name pair
static std::vector<NameProp> g_props_names;
static uint16_t g_split_seq, g_merge_seq;
static int g_split_total, g_split_share;
static std::map<uint8_t, int> g_merge_credit_of; // the budgets the last merge counted
static std::map<std::string, int> g_budget_name;  // player name -> its last reported budget (rejoins)
// BUG-0422: a merge credited the departed player's last budget to a newcomer under its id (the name-keyed
// g_budget_name keeps a rejoiner's own)
PLAYER_STATE("space_rivalry", [](uint8_t id) { g_budget.erase(id); g_merge_credit_of.erase(id); },
             [](uint8_t id) -> size_t { return g_budget.count(id) + g_merge_credit_of.count(id); });
static std::vector<std::pair<uint8_t, std::string>> g_roster;
static double g_last_state;

static uint8_t me() { return spaceext::my_id(); }

static void* my_ufo()
{
    void* mgr = a_PlayerUFOGet ? skg::cdecl_call<void*>(a_PlayerUFOGet) : nullptr;
    return mgr ? F<void*>(mgr, 0x40) : nullptr;
}

// the Space money of the own empire (cEmpire::mEmpireMoney +0xD0), nullptr when not in space
static int* money_ptr()
{
    if (!spaceext::in_space() || !a_PlayerEmpire)
        return nullptr;
    void* emp = skg::cdecl_call<void*>(a_PlayerEmpire);
    return emp && skg::readable(emp, 0xD4) ? (int*)((uint8_t*)emp + 0xD0) : nullptr;
}

static void add_money(int d)
{
    void* emp = a_PlayerEmpire && money_ptr() ? skg::cdecl_call<void*>(a_PlayerEmpire) : nullptr;
    if (emp && d)
        skg::thiscall<void>(a_AddMoney, emp, d); // clamps to 0..99 999 999
}

static std::pair<std::string, std::string> key(const std::string& a, const std::string& b)
{
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

static std::vector<std::pair<uint8_t, std::string>> roster()
{
    std::vector<std::pair<uint8_t, std::string>> v;
    if (spaceext::connected())
        for (auto& p : spaceext::ctx().sess->players())
            v.push_back({p.id, p.name});
    std::sort(v.begin(), v.end());
    return v;
}

static std::string rel_name(Relation r, bool ru)
{
    if (r == Relation::Ally)
        return ru ? "союзник" : "ally";
    if (r == Relation::War)
        return ru ? "война" : "at war";
    return ru ? "нейтралитет" : "neutral";
}

// ---------------- notices (every machine words its own) ----------------
static void tell(uint8_t what, uint8_t a, uint8_t b)
{
    std::string A = a == me() ? spaceext::tr("you", "вы") : spaceext::name_of(a);
    std::string B = b == me() ? spaceext::tr("you", "вам") : spaceext::name_of(b);
    std::string t;
    switch (what) {
    case E_RIVALS:
        t = A + spaceext::tr(" switched to rivals mode: every player is an empire of its own with its own budget",
                             ": режим соперников, каждый игрок - своя империя со своим бюджетом");
        break;
    case E_ALLIES:
        t = A + spaceext::tr(" switched to allies mode: one empire again, the budgets are merged",
                             ": режим союзников, снова одна империя, бюджеты объединены");
        break;
    case E_PROPOSE_ALLY:
        t = A + spaceext::tr(" proposed an alliance to ", " предлагает союз: ") + B +
            (b == me() ? spaceext::tr(" (accept it on the Space page, F9)", " (принять на странице Космос, F9)") : "");
        break;
    case E_ALLIANCE: t = A + spaceext::tr(" and ", " и ") + B + spaceext::tr(": allies now", ": теперь союзники"); break;
    case E_WAR: t = A + spaceext::tr(" declared war on ", " объявляет войну: ") + B + "!"; break;
    case E_PROPOSE_PEACE:
        t = A + spaceext::tr(" proposed peace to ", " предлагает мир: ") + B +
            (b == me() ? spaceext::tr(" (accept it on the Space page, F9)", " (принять на странице Космос, F9)") : "");
        break;
    case E_PEACE: t = A + spaceext::tr(" and ", " и ") + B + spaceext::tr(" made peace", ": мир"); break;
    default: return;
    }
    spaceext::notice(t);
}

// ---------------- host: apply, broadcast ----------------
static void host_rebuild()
{
    // the id-keyed relations of space_ext.h from the name-keyed ones, for the players here now
    spaceext::clear_relations();
    g_props.clear();
    auto r = roster();
    for (size_t i = 0; i < r.size(); i++)
        for (size_t j = i + 1; j < r.size(); j++) {
            auto it = g_rel_names.find(key(r[i].second, r[j].second));
            if (it != g_rel_names.end())
                spaceext::set_relation(r[i].first, r[j].first, it->second);
        }
    auto id_of = [&](const std::string& n) -> int {
        for (auto& [id, nm] : r)
            if (nm == n)
                return id;
        return -1;
    };
    for (auto& p : g_props_names) {
        int f = id_of(p.from), t = id_of(p.to);
        if (f >= 0 && t >= 0)
            g_props.push_back({(uint8_t)f, (uint8_t)t, p.kind});
    }
}

static void host_broadcast()
{
    if (!spaceext::is_host())
        return;
    host_rebuild();
    auto r = roster();
    net::Writer w;
    w.u8(spaceext::rivals());
    w.u16(g_split_seq);
    w.u32((uint32_t)g_split_total);
    w.u32((uint32_t)g_split_share);
    w.u16(g_merge_seq);
    w.u8((uint8_t)g_merge_credit_of.size());
    for (auto& [id, v] : g_merge_credit_of) {
        w.u8(id);
        w.u32((uint32_t)v);
    }
    std::vector<std::pair<std::pair<uint8_t, uint8_t>, Relation>> rels;
    for (size_t i = 0; i < r.size(); i++)
        for (size_t j = i + 1; j < r.size(); j++) {
            auto it = g_rel_names.find(key(r[i].second, r[j].second));
            if (it != g_rel_names.end())
                rels.push_back({{r[i].first, r[j].first}, it->second});
        }
    w.u8((uint8_t)rels.size());
    for (auto& [ab, rel] : rels) {
        w.u8(ab.first);
        w.u8(ab.second);
        w.u8((uint8_t)rel);
    }
    w.u8((uint8_t)g_props.size());
    for (auto& p : g_props) {
        w.u8(p.from);
        w.u8(p.to);
        w.u8(p.kind);
    }
    spaceext::send(K_STATE, w); // (at most 4 players: a few dozen bytes)
    g_last_state = H->time_seconds();
}

static void host_event(uint8_t what, uint8_t a, uint8_t b)
{
    net::Writer w;
    w.u8(what);
    w.u8(a);
    w.u8(b);
    spaceext::send(K_EVENT, w);
    tell(what, a, b);
}

static void drop_props(const std::string& a, const std::string& b)
{
    g_props_names.erase(std::remove_if(g_props_names.begin(), g_props_names.end(),
                                       [&](const NameProp& p) {
                                           return (p.from == a && p.to == b) || (p.from == b && p.to == a);
                                       }),
                        g_props_names.end());
}

// one player's request (the host's own ones too); "" = done, else why not
static std::string host_apply(uint8_t action, uint8_t origin, uint8_t target)
{
    if (!spaceext::is_host())
        return "not the host";
    if (action == A_RIVALS || action == A_ALLIES) {
        if (origin != 0)
            return "only the host switches the mode";
        if ((action == A_RIVALS) == spaceext::rivals())
            return "already in that mode";
        int* m = money_ptr();
        if (!m)
            return "not in space";
        if (action == A_RIVALS) {
            // a fair split: total / players for everybody, the rest of the division for the host
            int n = (int)std::max<size_t>(1, roster().size());
            g_split_total = *m;
            g_split_share = *m / n;
            add_money(-(n - 1) * g_split_share);
            g_split_seq++;
            g_splits++;
            spaceext::set_rivals(true);
            g_rel_names.clear();
            g_props_names.clear();
            g_merge_credit_of.clear();
        } else {
            // the merge: everybody's last reported budget is added to the host's money; each player
            // corrects the difference to its money at the switch (K_MERGE_FIX)
            int sum = 0;
            g_merge_credit_of.clear();
            for (auto& [id, nm] : roster())
                if (id != 0 && g_budget.count(id)) {
                    g_merge_credit_of[id] = g_budget[id];
                    sum += g_budget[id];
                }
            add_money(sum);
            g_merge_seq++;
            g_merges++;
            spaceext::set_rivals(false);
            g_rel_names.clear();
            g_props_names.clear();
        }
        g_budget.clear();
        g_budget_name.clear();
        g_last_reported = -1;
        host_broadcast();
        host_event(action == A_RIVALS ? E_RIVALS : E_ALLIES, origin, 0);
        return "";
    }
    if (!spaceext::rivals())
        return "not in rivals mode";
    std::string a = spaceext::name_of(origin), b = spaceext::name_of(target);
    bool known = false;
    for (auto& [id, nm] : roster())
        known |= id == target;
    if (!known || target == origin)
        return "no such player";
    auto k = key(a, b);
    Relation cur = g_rel_names.count(k) ? g_rel_names[k] : Relation::Neutral;
    auto proposed = [&](const std::string& from, const std::string& to, uint8_t kind) {
        for (auto& p : g_props_names)
            if (p.from == from && p.to == to && p.kind == kind)
                return true;
        return false;
    };
    uint8_t ev = 0;
    switch (action) {
    case A_PROPOSE_ALLY:
        if (cur != Relation::Neutral)
            return "only neutral players can make an alliance";
        if (proposed(b, a, P_ALLY)) { // both want it
            g_rel_names[k] = Relation::Ally;
            drop_props(a, b);
            ev = E_ALLIANCE;
        } else {
            if (!proposed(a, b, P_ALLY))
                g_props_names.push_back({a, b, P_ALLY});
            ev = E_PROPOSE_ALLY;
        }
        break;
    case A_PROPOSE_PEACE:
        if (cur != Relation::War)
            return "not at war";
        if (proposed(b, a, P_PEACE)) {
            g_rel_names[k] = Relation::Neutral;
            drop_props(a, b);
            ev = E_PEACE;
        } else {
            if (!proposed(a, b, P_PEACE))
                g_props_names.push_back({a, b, P_PEACE});
            ev = E_PROPOSE_PEACE;
        }
        break;
    case A_ACCEPT: // origin accepts target's proposal
        if (proposed(b, a, P_ALLY) && cur == Relation::Neutral) {
            g_rel_names[k] = Relation::Ally;
            ev = E_ALLIANCE;
        } else if (proposed(b, a, P_PEACE) && cur == Relation::War) {
            g_rel_names[k] = Relation::Neutral;
            ev = E_PEACE;
        } else
            return "nothing to accept";
        drop_props(a, b);
        break;
    case A_WAR:
        if (cur == Relation::War)
            return "already at war";
        g_rel_names[k] = Relation::War; // one-sided (an alliance ends with it)
        drop_props(a, b);
        ev = E_WAR;
        break;
    default:
        return "unknown action";
    }
    host_broadcast();
    host_event(ev, origin, target);
    return "";
}

// a button or a command: the host applies it, a guest asks the host
static std::string request(uint8_t action, uint8_t target)
{
    if (!spaceext::connected())
        return "not connected";
    if (spaceext::is_host())
        return host_apply(action, 0, target);
    net::Writer w;
    w.u8(action);
    w.u8(target);
    spaceext::send(K_REQ, w);
    return "";
}

// ---------------- everybody ----------------
static void apply_pending_money()
{
    int* m = money_ptr();
    if (!m)
        return;
    if (g_split_pending) {
        g_split_pending = false;
        add_money(g_split_delta);
        g_splits++;
        g_budget_known = true;
        spaceext::notice(spaceext::tr("Your own budget: ", "Ваш бюджет: ") + std::to_string(*m));
    }
    if (g_merge_pending) {
        g_merge_pending = false;
        int d = *m - g_merge_credit; // earned or spent after the last report the host counted
        if (d) {
            net::Writer w;
            w.u32((uint32_t)d);
            spaceext::send(K_MERGE_FIX, w);
        }
        g_merges++;
    }
}

static void on_state(net::Reader& r)
{
    bool riv = r.u8() != 0;
    uint16_t split = r.u16();
    int total = (int)r.u32(), share = (int)r.u32();
    uint16_t merge = r.u16();
    int credit = 0;
    bool have_credit = false;
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        uint8_t id = r.u8();
        int v = (int)r.u32();
        if (id == me()) {
            credit = v;
            have_credit = true;
        }
    }
    std::vector<std::pair<std::pair<uint8_t, uint8_t>, Relation>> rels;
    n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        uint8_t a = r.u8(), b = r.u8(), rel = r.u8();
        rels.push_back({{a, b}, (Relation)std::min<uint8_t>(rel, 2)});
    }
    std::vector<Prop> props;
    n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        Prop p;
        p.from = r.u8();
        p.to = r.u8();
        p.kind = r.u8();
        props.push_back(p);
    }
    if (!r.ok())
        return;
    if (!g_have_state) { // joined (or rejoined) now: the own money is the own budget already
        g_have_state = true;
        g_split_seen = split;
        g_merge_seen = merge;
    }
    if (riv != spaceext::rivals()) {
        g_budget.clear();
        g_last_reported = -1;
    }
    spaceext::set_rivals(riv);
    spaceext::clear_relations();
    for (auto& [ab, rel] : rels)
        spaceext::set_relation(ab.first, ab.second, rel);
    g_props = props;
    if (split != g_split_seen) {
        g_split_seen = split;
        if (riv) { // keep what we earned or spent that the host had not counted yet
            g_split_pending = true;
            g_split_delta = share - total;
        }
    }
    if (merge != g_merge_seen) {
        g_merge_seen = merge;
        if (!riv) {
            g_merge_pending = true;
            g_merge_credit = have_credit ? credit : 0;
        }
    }
    apply_pending_money(); // at once: before shared.cpp's next total overwrites the money
}

static void on_hit_msg(uint8_t origin, net::Reader& r)
{
    uint8_t victim = r.u8();
    float dmg = r.f32();
    int type = (int)r.u32();
    float dir[3] = {r.f32(), r.f32(), r.f32()};
    if (!r.ok() || victim != me() || !spaceext::rivals() || spaceext::relation(me(), origin) != Relation::War)
        return;
    void* u = my_ufo();
    if (!u || F<int>(u, kState) == 2 || !(dmg > 0) || dmg > 1e6f)
        return;
    // the shooter must be here (a late hit after a jump is dropped); its puppet is the attacker
    void* att = nullptr;
    bool here = false;
    uint32_t mine = spaceext::ctx().my_place ? spaceext::ctx().my_place() : 0;
    if (spaceext::ctx().partners)
        for (auto& p : spaceext::ctx().partners())
            if (p.id == origin && mine && p.place == mine) {
                here = true;
                if (p.puppet && modes::space_puppet_ok(p.puppet)) // (BUG-0111)
                    att = p.puppet;
            }
    if (!here)
        return;
    if (!std::isfinite(dir[0]) || !std::isfinite(dir[1]) || !std::isfinite(dir[2]))
        dir[0] = dir[1] = 0, dir[2] = 1;
    float hp0 = F<float>(u, kHp);
    // through the hooked TakeDamage: squadron's hook runs the rescue filter (an enemy never
    // rescues), then the game itself: damage, the native death cinematic and respawn
    skg::thiscall<int>(a_TakeDamage, (uint8_t*)u + kCombat, dmg, kEnemyPol, type, (const float*)dir,
                       att ? (uint8_t*)att + kCombat : nullptr);
    g_hits_got++;
    g_dmg_got += std::max(0.f, hp0 - F<float>(u, kHp));
    double now = H->time_seconds();
    if (F<int>(u, kState) == 2) {
        g_pvp_deaths++;
        spaceext::notice(spaceext::name_of(origin) + spaceext::tr(" destroyed your ship", " уничтожает ваш корабль"));
    } else if (now - g_last_attacked > 15) {
        spaceext::notice(spaceext::name_of(origin) + spaceext::tr(" is attacking you!", " атакует вас!"));
    }
    g_last_attacked = now;
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    switch (kind) {
    case K_STATE:
        if (origin == 0 && !spaceext::is_host())
            on_state(r);
        break;
    case K_REQ: {
        uint8_t action = r.u8(), target = r.u8();
        if (r.ok() && spaceext::is_host()) {
            std::string e = host_apply(action, origin, target);
            if (!e.empty())
                H->log(M, SK_INFO, "rivalry: request %u of player %u refused: %s", action, origin, e.c_str());
        }
        break;
    }
    case K_EVENT: {
        uint8_t what = r.u8(), a = r.u8(), b = r.u8();
        if (r.ok() && origin == 0)
            tell(what, a, b);
        break;
    }
    case K_HIT:
        on_hit_msg(origin, r);
        break;
    case K_MONEY: {
        int v = (int)r.u32();
        if (r.ok() && spaceext::rivals()) {
            g_budget[origin] = v;
            if (spaceext::is_host())
                g_budget_name[spaceext::name_of(origin)] = v;
        }
        break;
    }
    case K_ASK: // a guest (re)joined or loaded the world: its budget, if the host knows it
        if (spaceext::is_host() && spaceext::rivals()) {
            auto it = g_budget_name.find(spaceext::name_of(origin));
            net::Writer w;
            w.u8(origin);
            w.u8(it != g_budget_name.end());
            w.u32(it != g_budget_name.end() ? (uint32_t)it->second : 0u);
            spaceext::send(K_SET, w);
        }
        break;
    case K_SET: {
        uint8_t to = r.u8(), have = r.u8();
        int v = (int)r.u32();
        int* m = money_ptr();
        if (!r.ok() || to != me() || origin != 0 || !spaceext::rivals() || !m || g_budget_known)
            break;
        if (have && v != *m) {
            add_money(v - *m);
            g_restored++;
            spaceext::notice(spaceext::tr("Your own budget is back: ", "Ваш бюджет восстановлен: ") + std::to_string(*m));
        }
        g_budget_known = true;
        break;
    }
    case K_MERGE_FIX: {
        int d = (int)r.u32();
        if (r.ok() && spaceext::is_host() && !spaceext::rivals() && std::abs(d) < 100000000)
            add_money(d); // shared.cpp sends the new total
        break;
    }
    default: break;
    }
}

bool on_hit(void* ufo, void* attacker, float dmg, int type, const float* dir)
{
    if (!spaceext::connected() || !spaceext::rivals() || !spaceext::ctx().partners)
        return false;
    for (auto& p : spaceext::ctx().partners()) {
        if (p.puppet != ufo)
            continue;
        g_puppet_hits++; // (tests: any hit on a partner's ship here, also by NPCs or with no attacker)
        g_last_hit_type = type;
        g_last_hit_att = attacker == my_ufo() ? 1 : attacker ? 2 : 0;
        if (type == 10 || !attacker || attacker != my_ufo())
            return false;
        if (spaceext::relation(me(), p.id) != Relation::War)
            return false; // allies and neutrals: the game drops it (same political id, invincible puppet)
        net::Writer w;
        w.u8(p.id);
        w.f32(dmg);
        w.u32((uint32_t)type);
        w.f32(dir ? dir[0] : 0);
        w.f32(dir ? dir[1] : 0);
        w.f32(dir ? dir[2] : 1);
        spaceext::send(K_HIT, w);
        g_hits_sent++;
        return true;
    }
    return false;
}

// the puppets of players at war with us belong to no empire (our weapons hit them), the others
// to ours (the game's own friendly-fire rules keep them safe)
static void puppets_tick()
{
    if (!spaceext::in_space() || !spaceext::ctx().partners || !a_PlayerEmpireID)
        return;
    uint32_t mine = skg::cdecl_call<uint32_t>(a_PlayerEmpireID);
    uint32_t n = 0;
    for (auto& p : spaceext::ctx().partners()) {
        void* u = p.puppet;
        if (!modes::space_puppet_ok(u))
            continue; // freed by the game, not spawned yet (docs/re/space.md 9.4), or a game ship at its address (BUG-0111)
        bool war = spaceext::rivals() && spaceext::relation(me(), p.id) == Relation::War;
        uint32_t want = war ? kEnemyPol : mine;
        if (F<uint32_t>(u, kPol) != want)
            F<uint32_t>(u, kPol) = want; // cGameData::SetPoliticalID is this store (0x804880)
        n += war;
    }
    g_enemy_puppets = n;
}

static void tick(double now)
{
    if (!spaceext::connected())
        return;
    if (spaceext::is_host()) {
        auto r = roster();
        if (r != g_roster || now - g_last_state > 5.0) { // a player joined or left; and a safety net
            g_roster = r;
            host_broadcast();
        }
    }
    puppets_tick();
    if (g_split_pending || g_merge_pending)
        apply_pending_money();
    // a guest in a freshly loaded world (a rejoin loads the host's save): ask the host for its budget
    bool sp = money_ptr() != nullptr;
    if (sp && !g_was_in_space)
        g_budget_known = false;
    g_was_in_space = sp;
    if (spaceext::is_host())
        g_budget_known = true;
    else if (spaceext::rivals() && sp && !g_budget_known && !g_split_pending && now - g_last_ask > 2.0) {
        spaceext::send(K_ASK, net::Writer());
        g_last_ask = now;
    }
    // the own budget for everybody (the merge counts the host's last copy of it)
    if (spaceext::rivals() && g_budget_known) {
        if (int* m = money_ptr()) {
            g_budget[me()] = *m;
            if (!spaceext::is_host() && ((*m != g_last_reported && now - g_last_report > 0.5) || now - g_last_report > 3.0)) {
                net::Writer w;
                w.u32((uint32_t)*m);
                spaceext::send(K_MONEY, w);
                g_last_reported = *m;
                g_last_report = now;
            } else if (spaceext::is_host() && now - g_last_report > 1.0) {
                net::Writer w;
                w.u32((uint32_t)*m);
                spaceext::send(K_MONEY, w);
                g_last_report = now;
            }
        }
    }
}

static void session_end()
{
    spaceext::set_rivals(false);
    spaceext::clear_relations();
    g_props.clear();
    g_budget.clear();
    g_have_state = false;
    g_split_pending = g_merge_pending = false;
    g_budget_known = g_was_in_space = false;
    g_budget_name.clear();
    g_last_reported = -1;
    g_rel_names.clear();
    g_props_names.clear();
    g_merge_credit_of.clear();
    g_roster.clear();
}

// ---------------- the Space page ----------------
static std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    if (!spaceext::connected() || !spaceext::in_space())
        return v;
    bool ru = spaceext::russian();
    bool riv = spaceext::rivals();
    spaceext::Row mode;
    mode.text = riv ? spaceext::tr("Mode: Rivals (own empires and budgets)", "Режим: соперники (свои империи и бюджеты)")
                    : spaceext::tr("Mode: Allies (one empire, shared money)", "Режим: союзники (одна империя, общие деньги)");
    if (spaceext::is_host()) {
        mode.button = riv ? spaceext::tr("Switch to allies", "Стать союзниками") : spaceext::tr("Switch to rivals", "Стать соперниками");
        uint8_t a = riv ? A_ALLIES : A_RIVALS;
        mode.on_click = [a] {
            std::string e = request(a, 0);
            if (!e.empty())
                spaceext::notice(spaceext::tr("can't switch: ", "нельзя переключить: ") + e);
        };
    } else
        mode.text += spaceext::tr(", the host decides", ", решает хозяин");
    mode.color = riv ? 0xFFFFB040 : 0;
    v.push_back(std::move(mode));
    if (!riv)
        return v;
    auto act = [](uint8_t a, uint8_t id) {
        return [a, id] {
            std::string e = request(a, id);
            if (!e.empty())
                spaceext::notice(e);
        };
    };
    for (auto& [id, nm] : roster()) {
        if (id == me())
            continue;
        Relation rel = spaceext::relation(me(), id);
        const Prop* in = nullptr;
        bool out = false;
        for (auto& p : g_props) {
            if (p.from == id && p.to == me())
                in = &p;
            if (p.from == me() && p.to == id)
                out = true;
        }
        spaceext::Row r;
        r.text = nm + ": " + rel_name(rel, ru);
        r.color = rel == Relation::War ? 0xFFFF5040 : rel == Relation::Ally ? 0xFF60FF60 : 0;
        if (in) { // their proposal: accept it here
            spaceext::Row a;
            a.text = nm + (in->kind == P_ALLY ? spaceext::tr(" proposes an alliance", " предлагает союз")
                                              : spaceext::tr(" proposes peace", " предлагает мир"));
            a.button = spaceext::tr("Accept", "Принять");
            a.on_click = act(A_ACCEPT, id);
            a.color = 0xFFFFD040;
            v.push_back(std::move(a));
        }
        if (rel == Relation::Neutral) {
            if (out)
                r.text += spaceext::tr(", alliance proposed", ", союз предложен");
            else if (!in) {
                r.button = spaceext::tr("Propose alliance", "Предложить союз");
                r.on_click = act(A_PROPOSE_ALLY, id);
            }
            v.push_back(std::move(r));
            spaceext::Row w;
            w.text = "";
            w.button = spaceext::tr("Declare war", "Объявить войну");
            w.on_click = act(A_WAR, id);
            v.push_back(std::move(w));
        } else if (rel == Relation::Ally) {
            r.button = spaceext::tr("Declare war", "Объявить войну");
            r.on_click = act(A_WAR, id);
            v.push_back(std::move(r));
        } else {
            if (out)
                r.text += spaceext::tr(", peace proposed", ", мир предложен");
            else if (!in) {
                r.button = spaceext::tr("Propose peace", "Предложить мир");
                r.on_click = act(A_PROPOSE_PEACE, id);
            }
            v.push_back(std::move(r));
        }
    }
    return v;
}

static std::string status()
{
    std::string s = "rivalry: rivals=" + std::to_string((int)spaceext::rivals());
    int* m = money_ptr();
    s += " money=" + (m ? std::to_string(*m) : std::string("-"));
    s += " rel";
    for (auto& [id, nm] : roster())
        if (id != me())
            s += " " + std::to_string(id) + "=" +
                 (spaceext::relation(me(), id) == Relation::War ? "war" : rel_name(spaceext::relation(me(), id), false));
    s += " budgets";
    for (auto& [id, v] : g_budget)
        s += " " + std::to_string(id) + ":" + std::to_string(v);
    s += " props";
    for (auto& p : g_props)
        s += " " + std::to_string(p.from) + ">" + std::to_string(p.to) + (p.kind == P_ALLY ? "ally" : "peace");
    char b[320];
    snprintf(b, sizeof b,
             " hits_sent=%u hits_got=%u dmg_got=%.1f pvp_deaths=%u enemy_puppets=%u splits=%u merges=%u puppet_hits=%u"
             " (last type %d by %s) restored=%u\n",
             g_hits_sent, g_hits_got, g_dmg_got, g_pvp_deaths, g_enemy_puppets, g_splits, g_merges, g_puppet_hits,
             g_last_hit_type, g_last_hit_att == 1 ? "me" : g_last_hit_att == 2 ? "other" : "none", g_restored);
    return s + b;
}

static std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    auto id = [&](size_t i) { return (uint8_t)(a.size() > i ? atoi(a[i].c_str()) : 0); };
    std::string e;
    if (a[0] == "mode" && a.size() > 1)
        e = request(a[1] == "rivals" ? A_RIVALS : A_ALLIES, 0);
    else if (a[0] == "ally")
        e = request(A_PROPOSE_ALLY, id(1));
    else if (a[0] == "accept")
        e = request(A_ACCEPT, id(1));
    else if (a[0] == "war")
        e = request(A_WAR, id(1));
    else if (a[0] == "peace")
        e = request(A_PROPOSE_PEACE, id(1));
    else if (a[0] == "shoot" && a.size() > 2) {
        // tests: our ship hits player <id>'s puppet with <dmg>, through the hooked TakeDamage (the
        // path of a real shot after the projectile; real shots: palette "combat", key 1, a click)
        void* u = my_ufo();
        void* pup = nullptr;
        if (spaceext::ctx().partners)
            for (auto& p : spaceext::ctx().partners())
                if (p.id == id(1) && p.puppet && game::ufo_listed(p.puppet))
                    pup = p.puppet;
        if (!u || !pup)
            return "no ship or no puppet of that player here";
        float dir[3] = {0, 0, 1};
        uint32_t pol = a_PlayerEmpireID ? skg::cdecl_call<uint32_t>(a_PlayerEmpireID) : 0;
        skg::thiscall<int>(a_TakeDamage, (uint8_t*)pup + kCombat, (float)atof(a[2].c_str()), pol, 0, (const float*)dir,
                           (uint8_t*)u + kCombat);
        return "shot (hits_sent=" + std::to_string(g_hits_sent) + ")";
    } else if (a[0] == "pol") { // tests: political ids of the own ship and the partners' puppets
        std::string s;
        char b[96];
        if (a_PlayerEmpireID) {
            snprintf(b, sizeof b, "me %08X", skg::cdecl_call<uint32_t>(a_PlayerEmpireID));
            s += b;
        }
        if (spaceext::ctx().partners)
            for (auto& p : spaceext::ctx().partners())
                if (p.puppet && game::ufo_listed(p.puppet)) {
                    snprintf(b, sizeof b, "; puppet %u %08X hp=%.0f", p.id, F<uint32_t>(p.puppet, kPol), F<float>(p.puppet, kHp));
                    s += b;
                }
        return s;
    } else
        return "usage: coop.space rivalry [mode rivals|allies | ally <id> | accept <id> | war <id> | peace <id> | "
               "shoot <id> <dmg> | pol]";
    return e.empty() ? "ok" : e;
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    a_TakeDamage = H->sym("Simulator::cGameDataUFO::TakeDamage");
    a_PlayerUFOGet = H->sym("Simulator::cSimulatorPlayerUFO::Get");
    a_PlayerEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_PlayerEmpireID = H->sym("Simulator::GetPlayerEmpireID");
    a_AddMoney = H->sym("Simulator::cEmpire::AddMoney");
    return a_TakeDamage && a_PlayerUFOGet && a_PlayerEmpire && a_PlayerEmpireID && a_AddMoney;
}

static bool reg = spaceext::add({.name = "rivalry",
                                 .kind_lo = 50,
                                 .kind_hi = 59,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = session_end,
                                 .rows = rows,
                                 .status = status,
                                 .debug = debug});

} // namespace rivalry
