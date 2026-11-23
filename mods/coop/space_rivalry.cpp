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

#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/validate.h"
#include "own_world.h"
#include "space_logic.h"
#include "space_ext.h"
#include "sporekit_game.hpp"
#include "space_kept.h"
#include "store.h"
#include "wire/space_rivalry.h"

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
// (C5: the known budget and its Space world live in the restore window of space_kept.h: g_win.known, g_win.applied)
static spacekept::RestoreWindow g_win;
static bool g_was_in_space;
static double g_last_ask;
// BUG-0813: money_ptr() is null in an editor or an adventure entered from Space too, not only after a load: every
// return asked the host again, whose copy is the last K_MONEY (up to 0.5 s old, and none while away), and K_SET put
// the guest's money back to it: a contract reward paid in the first Space frame, or what was spent just before the
// editor, was undone. The budget is asked for again only in another Space world (spaceext::world_epoch, which an
// editor or an adventure does not change). `coop.fix space.rival_budget_world off`: every return (0.7.12).
static bool g_budget_world_fix = true;
COOP_FIX("space.rival_budget_world", "BUG-0813", g_budget_world_fix,
         "space rivals: a guest's own budget is not reset to the host's copy after an editor or an adventure");
static uint32_t g_budget_kept;
// BUG-1029: the Allies merge counted only the guests the host had a K_MONEY of: a rejoiner (still loading the host's
// save, or before the K_SET answer) was left out, the host wiped its kept budget (now g_store), and the guest then
// sent its whole game money as the correction: the save's money (or the shared total that reached it first) instead
// of its budget. Now the host credits every guest of the roster with the budget it knows (its last K_MONEY, its kept
// one by name, 0 for a newcomer), a guest whose budget is not known in this world sends no correction (the host's copy
// is its budget), and a newcomer to Rivals starts with 0, not with the host's save money (owner decision 2026-11-13).
// `coop.fix space.merge_known_budget off`: 0.7.13.
static bool g_merge_known_fix = true;
COOP_FIX("space.merge_known_budget", "BUG-1029", g_merge_known_fix,
         "space rivals: the switch to allies merges each guest's known budget (a rejoiner's kept one, 0 for a newcomer)");
static uint32_t g_newcomers, g_merge_fix_skipped;
// BUG-1110: a reported budget (K_MONEY, K_SET) outside the game's money range 0..99 999 999 is no budget (counted and
// ignored), and the Allies merge adds the budgets in 64 bits, clamped once (an int sum overflowed). Robustness only: the
// game itself never has such money. `coop.fix space.money_range off`: any value taken, an int sum (0.7.14)
static bool g_money_range_fix = true;
COOP_FIX("space.money_range", "BUG-1110", g_money_range_fix,
         "space rivals: budgets outside the game's money range are ignored, the allies merge sum stays in range");
static uint32_t g_money_refused;
// BUG-1414 (audit SPC-2): the switch to Allies needs only the host in Space. A guest in an editor or an adventure entered
// from Space keeps its merge pending (no money_ptr there), but shared.cpp already takes the host's shared totals into its
// empire money (the cEmpire lives on, `own_budget` is off now): back in Space the correction was that total minus the
// host's credit, and the host added its own money and the other guests' budgets once more. Now the guest's shared Space
// totals wait while its merge is pending (merge_pending(), asked by shared.cpp), so the correction is its own budget's
// change; the host's next total (every 2 s at the latest) comes after it. `coop.fix space.merge_hold_totals off`: 0.7.16.
static bool g_merge_hold_fix = true;
COOP_FIX("space.merge_hold_totals", "BUG-1414", g_merge_hold_fix,
         "space rivals: a guest outside Space at the switch to allies gets the shared money only after its own budget merged");
static uint32_t g_merge_held; // shared totals held while the merge was pending
// BUG-1482 (audit SPC-3): `coop.store adopt` gives a present guest the stored things of its name (a new rejoin token).
// Its Rivals budget only moved key in the host's store: nobody sent it to the guest (whose budget is known, so a K_SET
// would be dropped anyway), and the guest's next report (its money, usually a newcomer's 0) replaced it within 3 s. Now
// the host sends the adopted budget with a forced K_SET (have 3, space 1.3 guests) every 2 s and keeps it as that
// guest's budget until the guest reports it (spacelogic::AdoptHold). `coop.fix space.rival_adopt off`: 0.7.16.1.
static bool g_adopt_fix = true;
COOP_FIX("space.rival_adopt", "BUG-1482", g_adopt_fix,
         "space rivals: a budget the host gives a player with coop.store adopt reaches that player's game");
static std::map<uint8_t, spacelogic::AdoptHold> g_adopt; // host: guest id -> its adopted budget, until confirmed
static uint32_t g_adopted, g_adopt_sets, g_adopt_got, g_adopt_old_peer, g_adopt_reports_held;
// BUG-1483 (audit SPC-4): a guest playing a Space save of its own in a Rivals session (allowed: BUG-0709, BUG-0816) still
// asked the host for its budget (K_ASK) and took the answer (K_SET: the own save's money became the co-op budget or a
// newcomer's 0), reported the own save's money as its Rivals budget (K_MONEY) and merged it. Now nothing of the Rivals
// budget runs while own_world::space_other(): the own save keeps its money, the host keeps the co-op budget.
// `coop.fix space.rival_own_save off`: 0.7.16.1.
static bool g_own_save_fix = true;
COOP_FIX("space.rival_own_save", "BUG-1483", g_own_save_fix,
         "space rivals: a guest in a Space save of its own keeps that save's money (no co-op budget asked, set or reported)");
static uint32_t g_own_save_skips;
static bool own_save() // a guest in a Space save of its own (BUG-1483)
{
    return g_own_save_fix && !spaceext::is_host() && own_world::space_other();
}
static int g_fake_money;            // tests: the budget this guest reports instead of its money ...
static double g_fake_money_until;   // ... until then
static double g_hold_set_until; // tests: the host does not answer K_ASK until then (the window before K_SET)
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
// player key (spaceext::kept_key, BUG-1107) -> its last reported budget (rejoins): space_kept.h (C5)
static spacekept::KeptStore<int> g_store;
static void budget_put(const std::string& key, int v) { g_store.put(key, v); }
static const int* budget_find(const std::string& key)
{
    auto* e = g_store.find(key);
    return e ? &e->value : nullptr;
}
static void budget_clear() { g_store.clear(); }
// E1 (BUG-1152): Rivals over several evenings. The end of a session switched the mode off and dropped the guests'
// budgets and the relations between the players, so the next session began in Allies mode with the guests' money gone.
// Now the host keeps them for the world (the mode, the relations by player key, each player's budget), in memory across
// the session's end and in the per-player store on its disk (store.h: written with every save, read when its Space world
// opens); the next session of that world goes on in Rivals mode (a guest's K_ASK gets its budget back, the relations
// come back when both players are here). `coop.fix store.space_rivals off`: reset at the session's end (0.7.14).
static bool g_disk_fix = true;
COOP_FIX("store.space_rivals", "BUG-1152", g_disk_fix,
         "space rivals: the mode, the players' budgets and their relations go on in the next session of the world (kept on the host's disk)");
static std::map<std::pair<std::string, std::string>, Relation> g_rel_keys; // host: sorted player key pair -> relation
static bool g_kept_rivals;      // host: the last session of g_state_world ended in Rivals mode
static std::string g_state_world; // host: the store world (store::world_name) the Rivals state above belongs to
static uint32_t g_store_serial; // host: the store read last
static uint32_t g_mode_resumed, g_rel_resumed, g_disk_read;
static bool g_was_host;
// BUG-0422: a merge credited the departed player's last budget to a newcomer under its id (the name-keyed
// budget store keeps a rejoiner's own)
PLAYER_STATE("space_rivalry", [](uint8_t id) { g_budget.erase(id); g_merge_credit_of.erase(id); g_adopt.erase(id); },
             [](uint8_t id) -> size_t { return g_budget.count(id) + g_merge_credit_of.count(id) + g_adopt.count(id); });
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
    if (g_disk_fix && spaceext::rivals()) // E1 (BUG-1152): the relations by player key, both ways
        for (size_t i = 0; i < r.size(); i++)
            for (size_t j = i + 1; j < r.size(); j++) {
                auto kp = key(spaceext::kept_key(r[i].first), spaceext::kept_key(r[j].first));
                if (kp.first.empty() || kp.second.empty())
                    continue;
                auto nk = key(r[i].second, r[j].second);
                auto it = g_rel_names.find(nk);
                if (it != g_rel_names.end())
                    g_rel_keys[kp] = it->second;
                else if (auto k = g_rel_keys.find(kp); k != g_rel_keys.end()) {
                    g_rel_names[nk] = k->second; // kept from the last session: these two are back
                    g_rel_resumed++;
                }
            }
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
    wire::space_rivalry::State st;
    st.rivals = spaceext::rivals();
    st.split = g_split_seq;
    st.total = g_split_total;
    st.share = g_split_share;
    st.merge = g_merge_seq;
    for (auto& [id, v] : g_merge_credit_of)
        st.credits.push_back({id, v});
    for (size_t i = 0; i < r.size(); i++)
        for (size_t j = i + 1; j < r.size(); j++) {
            auto it = g_rel_names.find(key(r[i].second, r[j].second));
            if (it != g_rel_names.end())
                st.rels.push_back({r[i].first, r[j].first, (uint8_t)it->second});
        }
    for (auto& p : g_props)
        st.props.push_back({p.from, p.to, p.kind});
    net::Writer w;
    st.write(w);
    spaceext::send(K_STATE, w); // (at most 4 players: a few dozen bytes)
    g_last_state = H->time_seconds();
}

static void host_event(uint8_t what, uint8_t a, uint8_t b)
{
    wire::space_rivalry::Event m;
    m.what = what;
    m.a = a;
    m.b = b;
    net::Writer w;
    m.write(w);
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
            g_rel_keys.clear();
            g_kept_rivals = false;
            g_props_names.clear();
            g_merge_credit_of.clear();
        } else {
            // the merge: everybody's last reported budget is added to the host's money; each player
            // corrects the difference to its money at the switch (K_MERGE_FIX)
            int sum = 0;
            std::vector<int> credits;
            g_merge_credit_of.clear();
            for (auto& [id, nm] : roster()) {
                if (id == 0)
                    continue;
                if (g_budget.count(id))
                    g_merge_credit_of[id] = g_budget[id];
                else if (g_merge_known_fix) { // BUG-1029: no report yet: the kept budget, or 0 for a newcomer
                    const int* kept = budget_find(spaceext::kept_key(id));
                    g_merge_credit_of[id] = kept ? *kept : 0;
                } else
                    continue;
                credits.push_back(g_merge_credit_of[id]);
                if (!g_money_range_fix)
                    sum = (int)((unsigned)sum + (unsigned)g_merge_credit_of[id]); // 0.7.14: an int sum (wraps)
            }
            if (g_money_range_fix)
                sum = net::money_sum(credits.begin(), credits.end());
            add_money(sum);
            g_merge_seq++;
            g_merges++;
            spaceext::set_rivals(false);
            g_rel_names.clear();
            g_rel_keys.clear();
            g_kept_rivals = false;
            g_props_names.clear();
        }
        g_budget.clear();
        budget_clear();
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
    wire::space_rivalry::Req m;
    m.action = action;
    m.target = target;
    net::Writer w;
    m.write(w);
    spaceext::send(K_REQ, w);
    return "";
}

// ---------------- everybody ----------------
bool merge_pending()
{
    if (!g_merge_hold_fix || !g_merge_pending || spaceext::is_host())
        return false;
    g_merge_held++;
    return true;
}

static void apply_pending_money()
{
    int* m = money_ptr();
    if (!m)
        return;
    if (own_save()) { // BUG-1483: not this save's money; a split or a merge waits for the host's world
        g_own_save_skips++;
        return;
    }
    if (g_split_pending) {
        g_split_pending = false;
        add_money(g_split_delta);
        g_splits++;
        g_win.known = true;
        g_win.applied = spaceext::world_epoch();
        spaceext::notice(spaceext::tr("Your own budget: ", "Ваш бюджет: ") + std::to_string(*m));
    }
    if (g_merge_pending) {
        g_merge_pending = false;
        int d = *m - g_merge_credit; // earned or spent after the last report the host counted
        // BUG-1029: our budget was not known in this world (a rejoin or a load of the host's save, before K_SET): the
        // game's money is the save's (or the shared total), not ours; the host's credit is our budget
        if (g_merge_known_fix && !spaceext::is_host() &&
            !(g_win.known && g_win.applied && g_win.applied == spaceext::world_epoch())) {
            if (d)
                H->log(M, SK_INFO, "rivalry: merge with an unknown budget: no correction (money %d, credit %d)", *m,
                       g_merge_credit);
            d = 0;
            g_merge_fix_skipped++;
        }
        if (d) {
            wire::space_rivalry::MergeFix m;
            m.d = d;
            net::Writer w;
            m.write(w);
            spaceext::send(K_MERGE_FIX, w);
        }
        g_merges++;
    }
}

static void on_state(net::Reader& r)
{
    wire::space_rivalry::State st;
    if (!st.read(r))
        return;
    bool riv = st.rivals;
    uint16_t split = st.split, merge = st.merge;
    int total = st.total, share = st.share;
    int credit = 0;
    bool have_credit = false;
    for (auto& c : st.credits)
        if (c.id == me()) {
            credit = c.v;
            have_credit = true;
        }
    std::vector<std::pair<std::pair<uint8_t, uint8_t>, Relation>> rels;
    for (auto& x : st.rels)
        rels.push_back({{x.a, x.b}, (Relation)std::min<uint8_t>(x.rel, 2)});
    std::vector<Prop> props;
    for (auto& p : st.props)
        props.push_back({p.from, p.to, p.kind});
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
    wire::space_rivalry::Hit m; // (read: the damage above 0 and at most 1e6)
    if (!m.read(r) || m.victim != me() || !spaceext::rivals() || spaceext::relation(me(), origin) != Relation::War)
        return;
    float dmg = m.dmg;
    int type = m.type;
    float dir[3] = {m.dir[0], m.dir[1], m.dir[2]};
    void* u = my_ufo();
    if (!u || F<int>(u, kState) == 2)
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
        wire::space_rivalry::Req m;
        if (m.read(r) && spaceext::is_host()) {
            std::string e = host_apply(m.action, origin, m.target);
            if (!e.empty())
                H->log(M, SK_INFO, "rivalry: request %u of player %u refused: %s", m.action, origin, e.c_str());
        }
        break;
    }
    case K_EVENT: {
        wire::space_rivalry::Event m;
        if (m.read(r) && origin == 0)
            tell(m.what, m.a, m.b);
        break;
    }
    case K_HIT:
        on_hit_msg(origin, r);
        break;
    case K_MONEY: {
        wire::space_rivalry::Money m;
        if (!m.read(r))
            break;
        int v = m.v;
        if (g_money_range_fix && !net::money_in_range(v)) {
            if (g_money_refused++ < 5)
                H->log(M, SK_WARN, "rivalry: a budget of player %u outside the game's range ignored (%d)", origin, v);
            break;
        }
        if (spaceext::rivals()) {
            if (spaceext::is_host() && g_adopt_fix) { // BUG-1482: the guest's money before it took the adopted budget
                auto it = g_adopt.find(origin);
                if (it != g_adopt.end()) {
                    bool take = it->second.report(v);
                    if (!take) {
                        g_adopt_reports_held++;
                        break;
                    }
                    if (!it->second.on) {
                        g_adopt_got++;
                        g_adopt.erase(it);
                    }
                }
            }
            g_budget[origin] = v;
            if (spaceext::is_host()) {
                std::string k = spaceext::kept_key(origin);
                budget_put(k, v);
                if (g_adopt_fix) // BUG-1482: the stored budget carries its player's name (what `coop.store adopt` looks for)
                    if (auto& e = g_store.entries()[k]; e.name.empty())
                        e.name = spaceext::name_of(origin);
            }
        }
        break;
    }
    case K_ASK: // a guest (re)joined or loaded the world: its budget, if the host knows it
        if (spaceext::is_host() && spaceext::rivals() && H->time_seconds() >= g_hold_set_until) {
            std::string nm = spaceext::kept_key(origin);
            const int* kept = budget_find(nm);
            uint8_t have = kept != nullptr;
            int v = have ? *kept : 0;
            if (!have && g_merge_known_fix) {
                // BUG-1029 (owner decision 2026-11-13): a newcomer to Rivals starts its own empire with 0, not with
                // the host's save money it loaded; the host knows that budget from now on (a merge counts it)
                have = 2;
                v = 0;
                budget_put(nm, 0);
                g_budget[origin] = 0;
                g_newcomers++;
            }
            wire::space_rivalry::Set m;
            m.to = origin;
            m.have = have;
            m.v = v;
            net::Writer w;
            m.write(w);
            spaceext::send(K_SET, w);
        }
        break;
    case K_SET: {
        wire::space_rivalry::Set s;
        if (!s.read(r))
            break;
        uint8_t to = s.to, have = s.have;
        int v = s.v;
        if (g_money_range_fix && !net::money_in_range(v)) { // BUG-1110
            g_money_refused++;
            v = net::money_clamp(v);
        }
        int* m = money_ptr();
        bool adopted = g_adopt_fix && have == spacelogic::kSetAdopted; // BUG-1482: the host's word
        if (to != me() || origin != 0 || !spaceext::rivals() || !m || (g_win.known && !adopted))
            break;
        if (own_save()) { // BUG-1483
            g_own_save_skips++;
            break;
        }
        if (adopted) {
            if (v != *m)
                add_money(v - *m);
            g_adopted++;
            spaceext::notice(spaceext::tr("The host gave you your stored budget: ", "Хост вернул вам ваш сохранённый бюджет: ") +
                             std::to_string(*m));
        } else if (have == 2) { // BUG-1029: new to this Rivals session
            if (v != *m)
                add_money(v - *m);
            g_newcomers++;
            spaceext::notice(spaceext::tr("Rivals mode: your own empire starts with a budget of ",
                                          "Режим соперников: ваша империя начинает с бюджетом ") +
                             std::to_string(*m));
        } else if (have && v != *m) {
            add_money(v - *m);
            g_restored++;
            spaceext::notice(spaceext::tr("Your own budget is back: ", "Ваш бюджет восстановлен: ") + std::to_string(*m));
        }
        g_win.known = true;
        g_win.applied = spaceext::world_epoch();
        break;
    }
    case K_MERGE_FIX: {
        wire::space_rivalry::MergeFix m; // (read: |d| < 1e8)
        if (m.read(r) && spaceext::is_host() && !spaceext::rivals())
            add_money(m.d); // shared.cpp sends the new total
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
        wire::space_rivalry::Hit h;
        h.victim = p.id;
        h.dmg = dmg;
        h.type = type;
        h.dir[0] = dir ? dir[0] : 0;
        h.dir[1] = dir ? dir[1] : 0;
        h.dir[2] = dir ? dir[2] : 1;
        net::Writer w;
        h.write(w);
        spaceext::send(K_HIT, w);
        g_hits_sent++;
        return true;
    }
    return false;
}

bool at_war(uint8_t id)
{
    return spaceext::connected() && spaceext::rivals() && id != me() && spaceext::relation(me(), id) == Relation::War;
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

// E1 (BUG-1152), the host in Space: the Rivals state of the world from its disk, once per store opened
static void disk_read()
{
    pstore::Store* w = store::world();
    if (!w || store::serial() == g_store_serial)
        return;
    g_store_serial = store::serial();
    if (spaceext::rivals() || g_kept_rivals ? g_state_world == w->world : false)
        return; // this session's own state of this world is newer than the disk's
    g_rel_keys.clear();
    g_kept_rivals = false;
    budget_clear();
    g_state_world = w->world;
    const pstore::Record* r = w->get_world("space.rivals");
    if (!r || r->ver != 1)
        return;
    pstore::In in(r->data);
    bool rivals = in.u8() != 0;
    uint16_t n = in.u16();
    std::map<std::pair<std::string, std::string>, Relation> rels;
    for (uint16_t i = 0; i < n && in.ok && i < 1000; i++) {
        std::string a = in.str(), b = in.str();
        uint8_t v = in.u8();
        if (in.ok && v <= 2 && pstore::key_ok(a) && pstore::key_ok(b))
            rels[key(a, b)] = (Relation)v;
    }
    if (!in.end() || !rivals)
        return;
    g_kept_rivals = true;
    g_rel_keys = std::move(rels);
    for (auto& k : w->keys_with("space.budget"))
        if (const pstore::Record* b = w->get(k, "space.budget"); b && b->ver == 1 && b->data.size() == 4) {
            pstore::In bi(b->data);
            int v = bi.i32();
            if (!g_money_range_fix || net::money_in_range(v))
                g_store.restore(k, w->name_of(k), v);
        }
    g_disk_read++;
    H->log(M, SK_INFO, "rivalry: world %s was in Rivals mode: %u budgets, %u relations from the host's disk", w->world.c_str(),
           (unsigned)g_store.size(), (unsigned)g_rel_keys.size());
}

// the game saves: the Rivals state of this world (store.h)
static void disk_save(pstore::Store& s, int64_t now)
{
    if (!g_disk_fix || g_state_world.empty() || s.world != g_state_world)
        return;
    bool rivals = spaceext::rivals() || g_kept_rivals;
    pstore::Out o;
    o.u8(rivals);
    uint16_t n = rivals ? (uint16_t)std::min<size_t>(g_rel_keys.size(), 1000) : 0;
    o.u16(n);
    uint16_t i = 0;
    for (auto& [kp, rel] : g_rel_keys)
        if (i++ < n) {
            o.str(kp.first);
            o.str(kp.second);
            o.u8((uint8_t)rel);
        }
    if (rivals || s.get_world("space.rivals")) // (a world never played in Rivals mode gets no record)
        s.put_world("space.rivals", 1, o.d);
    if (!rivals) { // Allies: no budgets of their own
        for (auto& k : s.keys_with("space.budget"))
            s.erase(k, "space.budget");
        return;
    }
    for (auto& [k, e] : g_store.entries())
        if (e.have && pstore::key_ok(k)) {
            pstore::Out b;
            b.i32(e.value);
            s.put(k, e.name, "space.budget", 1, b.d, now);
        }
}
// BUG-1482: the host gave the stored budget now under key `to` to the guest of that key: send it until it reports it
static void adopt_send(uint8_t id, int v)
{
    wire::space_rivalry::Set m;
    m.to = id;
    m.have = spacelogic::kSetAdopted;
    m.v = v;
    net::Writer w;
    m.write(w);
    spaceext::send(K_SET, w);
    g_adopt_sets++;
}
static void adopt_budget(const std::string& to)
{
    const int* v = budget_find(to);
    if (!g_adopt_fix || !v || !spaceext::is_host() || !spaceext::rivals())
        return;
    for (auto& [id, nm] : roster()) {
        if (id == me() || spaceext::kept_key(id) != to)
            continue;
        bool has = false;
        std::vector<net::FamilyRev> fam;
        if (!spaceext::peer_families(id, has, fam) || !wire::space_rivalry::reads_adopt(has, fam)) {
            g_adopt_old_peer++; // a 0.7.16.1 guest drops have 3: as in 0.7.16.1, its own reports go on
            H->log(M, SK_INFO, "rivalry: player %u's game is older than space 1.3: the adopted budget is not sent", id);
            continue;
        }
        g_budget[id] = *v;
        g_adopt[id].start(*v, H->time_seconds());
        H->log(M, SK_INFO, "rivalry: the stored budget %d given to player %u (coop.store adopt)", *v, id);
    }
}
static bool g_store_reg = store::add({"space.rivals", disk_save, [](const std::string& from, const std::string& to) {
                                          g_store.rekey(from, to); // its budget (its K_ASK, or the next one, gets it)
                                          adopt_budget(to);        // BUG-1482: ... or now
                                          std::map<std::pair<std::string, std::string>, Relation> m;
                                          for (auto& [kp, r] : g_rel_keys)
                                              m[key(kp.first == from ? to : kp.first, kp.second == from ? to : kp.second)] = r;
                                          g_rel_keys = std::move(m);
                                      }});

static void tick(double now)
{
    if (!spaceext::connected())
        return;
    if (spaceext::is_host() && g_disk_fix && money_ptr()) {
        g_was_host = true;
        disk_read();
        if (spaceext::rivals())
            g_state_world = store::world_name();
        if (g_kept_rivals && !spaceext::rivals() && g_state_world == store::world_name() && !g_state_world.empty()) {
            // the last session of this world ended in Rivals mode: it goes on (no split: everybody's money is its own)
            g_kept_rivals = false;
            spaceext::set_rivals(true);
            g_mode_resumed++;
            host_broadcast();
            spaceext::notice(spaceext::tr("Rivals mode goes on from the last session: every player is an empire of its own again",
                                          "Режим соперников продолжается с прошлой сессии: каждый игрок снова своя империя"));
        }
    }
    if (spaceext::is_host()) {
        auto r = roster();
        if (r != g_roster || now - g_last_state > 5.0) { // a player joined or left; and a safety net
            g_roster = r;
            host_broadcast();
        }
    }
    puppets_tick();
    if (spaceext::is_host()) // BUG-1482: the adopted budgets until each guest reports it
        for (auto it = g_adopt.begin(); it != g_adopt.end();) {
            if (!g_adopt_fix || !spaceext::rivals()) {
                it = g_adopt.erase(it);
                continue;
            }
            if (it->second.due(now))
                adopt_send(it->first, it->second.v);
            it = it->second.on ? std::next(it) : g_adopt.erase(it);
        }
    if (g_split_pending || g_merge_pending)
        apply_pending_money();
    // a guest in a freshly loaded world (a rejoin loads the host's save): ask the host for its budget
    bool sp = money_ptr() != nullptr;
    if (sp && !g_was_in_space) {
        if (g_budget_world_fix && g_win.known && g_win.applied && g_win.applied == spaceext::world_epoch())
            g_budget_kept++; // BUG-0813: back from an editor / adventure in the same Space world: still known
        else
            g_win.known = false;
    }
    g_was_in_space = sp;
    if (sp && g_win.known)
        g_win.applied = spaceext::world_epoch();
    if (spaceext::is_host())
        g_win.known = true;
    else if (spaceext::rivals() && sp && !g_win.known && !g_split_pending && now - g_last_ask > 2.0 && !own_save()) {
        spaceext::send(K_ASK, net::Writer());
        g_last_ask = now;
    }
    // the own budget for everybody (the merge counts the host's last copy of it)
    if (spaceext::rivals() && g_win.known && !own_save()) { // BUG-1483: an own save's money is no Rivals budget
        if (int* m = money_ptr()) {
            g_budget[me()] = *m;
            int report = now < g_fake_money_until ? g_fake_money : *m; // tests: a crafted budget (BUG-1110)
            if (!spaceext::is_host() && ((report != g_last_reported && now - g_last_report > 0.5) || now - g_last_report > 3.0)) {
                wire::space_rivalry::Money mo;
                mo.v = report;
                net::Writer w;
                mo.write(w);
                spaceext::send(K_MONEY, w);
                g_last_reported = report;
                g_last_report = now;
            } else if (spaceext::is_host() && now - g_last_report > 1.0) {
                wire::space_rivalry::Money mo;
                mo.v = *m;
                net::Writer w;
                mo.write(w);
                spaceext::send(K_MONEY, w);
                g_last_report = now;
            }
        }
    }
}

static void session_end()
{
    bool keep = g_disk_fix && g_was_host && spaceext::rivals() && !store::world_name().empty();
    if (keep) { // E1 (BUG-1152): the next session of this world goes on in Rivals mode
        g_kept_rivals = true;
        g_state_world = store::world_name();
        H->log(M, SK_INFO, "rivalry: the session ended in Rivals mode: kept for the next session of %s (%u budgets, %u relations)",
               g_state_world.c_str(), (unsigned)g_store.size(), (unsigned)g_rel_keys.size());
    }
    g_was_host = false;
    g_adopt.clear(); // BUG-1482
    spaceext::set_rivals(false);
    spaceext::clear_relations();
    g_props.clear();
    g_budget.clear();
    g_have_state = false;
    g_split_pending = g_merge_pending = false;
    g_win.known = g_was_in_space = false;
    g_win.applied = 0;
    if (keep)
        g_store.session_end(); // the budgets stay (nobody bound)
    else {
        budget_clear();
        if (g_disk_fix && !g_kept_rivals)
            g_rel_keys.clear();
    }
    g_last_reported = -1;
    g_rel_names.clear();
    g_props_names.clear();
    g_merge_credit_of.clear();
    g_roster.clear();
    g_hold_set_until = 0;
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
    char b[640];
    snprintf(b, sizeof b,
             " hits_sent=%u hits_got=%u dmg_got=%.1f pvp_deaths=%u enemy_puppets=%u splits=%u merges=%u puppet_hits=%u"
             " (last type %d by %s) restored=%u budget_kept=%u newcomers=%u merge_fix_skipped=%u money_refused=%u"
             " merge_pending=%d merge_held=%u adopted=%u adopt_sets=%u adopt_got=%u adopt_held=%u adopt_old_peer=%u"
             " adopt_pending=%u own_save=%d own_save_skips=%u\n",
             g_hits_sent, g_hits_got, g_dmg_got, g_pvp_deaths, g_enemy_puppets, g_splits, g_merges, g_puppet_hits,
             g_last_hit_type, g_last_hit_att == 1 ? "me" : g_last_hit_att == 2 ? "other" : "none", g_restored, g_budget_kept,
             g_newcomers, g_merge_fix_skipped, g_money_refused, (int)g_merge_pending, g_merge_held, g_adopted, g_adopt_sets,
             g_adopt_got, g_adopt_reports_held, g_adopt_old_peer, (unsigned)g_adopt.size(), (int)own_save(), g_own_save_skips);
    char d[240]; // E1 (BUG-1152)
    snprintf(d, sizeof d, "  disk: fix=%d kept_rivals=%d world=%s kept_budgets=%u key_relations=%u read=%u mode_resumed=%u rel_resumed=%u\n",
             (int)g_disk_fix, (int)g_kept_rivals, g_state_world.empty() ? "-" : g_state_world.c_str(), (unsigned)g_store.size(),
             (unsigned)g_rel_keys.size(), g_disk_read, g_mode_resumed, g_rel_resumed);
    return s + b + d;
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
    } else if (a[0] == "fakemoney" && a.size() > 1) { // tests (BUG-1110): this guest reports <v> as its budget for <s> s
        g_fake_money = (int)strtoll(a[1].c_str(), nullptr, 0);
        g_fake_money_until = H->time_seconds() + (a.size() > 2 ? atof(a[2].c_str()) : 10.0);
        g_last_reported = -1;
        return "reporting a budget of " + std::to_string(g_fake_money) + " for " + (a.size() > 2 ? a[2] : "10") + " s";
    } else if (a[0] == "holdset" && a.size() > 1) { // tests (BUG-1029): the host answers no K_ASK for <s> seconds
        g_hold_set_until = H->time_seconds() + atof(a[1].c_str());
        return "K_SET held for " + a[1] + " s";
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
               "shoot <id> <dmg> | pol | holdset <s>]";
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
