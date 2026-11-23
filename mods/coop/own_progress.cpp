// Own progress in the Cell and Creature stages (own_progress.h, BUG-1469; idea from PR #209 by IltonBJSilva).
#include "own_progress.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "cellsync.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/protocol.h"
#include "own_world.h"
#include "settings.h"
#include "store.h"
#include "world.h"

namespace own_progress {

static const SkHost* H;
static SkMod* M;
static Ctx C;

// `coop.fix core.own_progress off`: all.shared_dna 2 is read as 0 (own wallet, 0.7.16.1 exactly)
static bool g_fix = true;
COOP_FIX("core.own_progress", "BUG-1469", g_fix,
         "cell, creature: all.shared_dna 2 (own progress): every player's own DNA and evolution bar, the stage moves on when every bar is full; off: value 2 plays as own wallet");
// where the Cell gate sits: before the game's end sequence (History closes, the pool stays drawn) or, off, at the switch
// after it (the white end picture stays until the stage moves on: PR #209's BUG-9266, our BUG-1269's class)
static bool g_end_click_fix = true;
COOP_FIX("cell.end_click", "BUG-1469", g_end_click_fix,
         "cell, own progress: a click on History's check that must wait for the partners' bars is kept before the game's end sequence (no white screen)");
// Cell levels together (PR #209's BUG-9287): the one pool is populated at the host's level
static bool g_level_fix = true;
COOP_FIX("cell.level_together", "BUG-1469", g_level_fix,
         "cell, own progress: nobody grows a Cell level alone: an own meal over the next level line waits (banked) until every partner reached it");
// a guest's own values back after a load of the host's world (the copy holds the host's)
static bool g_restore_fix = true;
COOP_FIX("core.progress_restore", "BUG-1469", g_restore_fix,
         "cell, creature, own progress: a guest gets its own DNA, bar, food and brain level back after a join, rejoin or reload of the host's world (off: it keeps the host's)");

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static void feed(const std::string& s)
{
    if (C.feed)
        C.feed(s);
}
static std::string name_of(uint8_t id)
{
    std::string n = C.sess ? C.sess->name_of(id) : std::string();
    return n.empty() ? "player " + std::to_string(id) : n;
}
static uint32_t world_now() { return C.world ? C.world() : modes::current_world(); }

// the setting asks for own progress on this machine (a guest: the host's table)
static bool wanted()
{
    return g_fix && settings::get(settings::kSharedDna) == kOwnProgress && !(C.host_creature_mode && C.host_creature_mode());
}

// ---- host: the start and the old peers
static bool g_decided, g_effective; // value 2 decided for this world / played
static std::vector<uint8_t> g_join_check; // late joiners to check
static unsigned g_refused_starts, g_kicked;
static bool peer_reads_id(uint8_t id, bool* known = nullptr)
{
    bool has = false;
    std::vector<net::FamilyRev> f;
    bool k = C.sess && C.sess->peer_families(id, has, f);
    if (known)
        *known = k;
    return k && peer_reads(f, has);
}

// ---- guest: the host's word
static bool g_host_active;  // the host's last table said own progress is played
static std::vector<Player> g_table;
static double g_table_got = -100;

bool active()
{
    if (!C.sess || !C.sess->connected() || !wanted())
        return false;
    if (C.sess->role() == net::Role::Host)
        return g_decided && g_effective;
    bool has = false;
    std::vector<net::FamilyRev> f;
    return g_host_active && C.sess->host_families(has, f) && peer_reads(f, has);
}

int dna_mode()
{
    int v = mode_of(settings::get(settings::kSharedDna), g_fix);
    if (v == kOwnProgress && !active())
        return kOwnWallet; // refused (an older peer) or not decided yet: the 0.7.16 default
    return v;
}

bool end_click_fix() { return g_end_click_fix; }

bool local_values(Values& out)
{
    const uint32_t m = game::mode_id();
    Values v;
    v.mode = m;
    if (m == modes::kCell) {
        int food = 0, spent = 0;
        if (!cellsync::food_total(food) || !cellsync::spent_dna(spent))
            return false;
        v.food = food;
        v.spent = spent;
    } else if (m == modes::kCreature) {
        if (!game::avatar())
            return false;
        v.dna = game::dna();
        v.bar = game::evolution_bar();
        int lv = game::brain_level();
        if (!std::isfinite(v.bar) || lv < 0)
            return false;
        v.level = (uint8_t)std::min(lv, 16);
    } else
        return false;
    out = v;
    return true;
}

// ---- host: every guest's values (live: this session; recs: the world's, in the store)
struct Live {
    Values v;
    bool reported = false;
};
static std::map<uint8_t, Live> g_live;
static std::map<std::string, Record> g_recs; // stable key (or "id:N" without one: memory only) -> record
static std::map<uint8_t, int32_t> g_left_food; // Cell: guests that left this session (the level line keeps them)
static uint32_t g_store_serial;
static bool g_switch_due, g_due_at_end; // host, Cell: a click kept until every bar is full
static EndGate g_gate;
static double g_table_at = -100;
static std::vector<uint8_t> g_table_last;

// ---- guest
static uint32_t g_synced_mode, g_synced_epoch, g_synced_world; // the copy our values went back into
static bool g_synced;
static double g_ask_at = -100, g_report_at = -100, g_stage_since = -1, g_wait_told_at = -100;
static Values g_last_report;
static float g_gain_owed; // gains that came while this game had no creature

// ---- both: the notices, the bank
static bool g_was_full, g_was_all;
static double g_remind_at;
static uint32_t g_notice_mode;
static int g_bank;
static double g_level_told_at = -1e9;
static int g_level_told = -1;

// ---- tests (coop.progress)
static unsigned g_asks, g_sets, g_applied, g_reports_in, g_reports_out, g_gains_out, g_gains_in, g_holds, g_forced,
    g_resumes, g_tables_in, g_full_notices, g_all_notices, g_waits_told, g_loaded, g_saved, g_clipped, g_released;

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
}

static std::string key_of(uint8_t id)
{
    std::string k = store::key_of(id);
    return k.empty() ? "id:" + std::to_string(id) : k;
}

// ---- the players' table (who is full): the players of the host's world
static std::vector<Player> players_now()
{
    std::vector<Player> out;
    Values me;
    const bool have = local_values(me);
    const uint8_t my = C.sess ? C.sess->my_id() : 0;
    auto mine = [&](Player& x) {
        x.known = have;
        x.mode = have ? me.mode : 0;
        x.full = have && full(me);
        x.pct = have ? percent(me) : 0;
        x.food = have ? me.food : 0;
    };
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (const auto& p : C.sess->players()) {
            if (p.id != my && own_world::peer_elsewhere(p.id))
                continue; // another world: not waited for (it ignores the host's votes too, BUG-0801)
            Player x;
            x.id = p.id;
            x.name = p.name;
            if (p.id == my)
                mine(x);
            else if (auto it = g_live.find(p.id); it != g_live.end() && it->second.reported) {
                const Values& v = it->second.v;
                x.known = true;
                x.mode = v.mode;
                x.full = full(v);
                x.pct = percent(v);
                x.food = v.food;
            }
            out.push_back(x);
        }
        return out;
    }
    for (const auto& p : g_table) {
        Player x = p;
        x.name = name_of(p.id);
        if (p.id == my) {
            mine(x);
            x.known = x.known && g_synced;
        }
        out.push_back(x);
    }
    return out;
}

static uint32_t stage_now()
{
    uint32_t m = game::mode_id();
    return is_stage(m) ? m : 0;
}

bool all_full(std::string* waiting)
{
    const uint32_t m = stage_now();
    if (waiting)
        *waiting = "";
    if (!active() || !m)
        return false;
    if (is_guest() && H->time_seconds() - g_table_got > 5) {
        if (waiting)
            *waiting = "the host's table (not known yet)";
        return false;
    }
    return own_progress::all_full(players_now(), m, C.sess->my_id(), waiting);
}

// ---- host: the store record `progress` (docs/01-architecture.md#arch-player-store)
static void recs_load()
{
    pstore::Store* s = store::world();
    uint32_t ser = store::serial();
    if (!s || ser == g_store_serial)
        return;
    g_store_serial = ser;
    std::map<std::string, Record> keep; // a player seen since the last save, without a stored record, stays
    for (auto& [k, r] : g_recs)
        if (!s->get(k, pstore::kRecProgress))
            keep[k] = r;
    g_recs = keep;
    int n = 0;
    for (auto& [k, p] : s->players) {
        const pstore::Record* r = s->get(k, pstore::kRecProgress);
        Record x;
        if (r && r->ver == kStoreVersion && decode(r->data, x)) {
            g_recs[k] = x;
            n++;
        }
    }
    g_loaded += n;
    if (n)
        H->log(M, SK_INFO, "own progress: %d player record(s) read from the store of %s", n, store::world_name().c_str());
}
static void recs_save(pstore::Store& s, int64_t now)
{
    if (!C.sess || C.sess->role() != net::Role::Host)
        return;
    for (auto& [k, r] : g_recs) {
        if (k.rfind("s:", 0) != 0)
            continue; // no stable key: memory only
        std::string name;
        for (auto& p : C.sess->players())
            if (p.id != 0 && store::key_of(p.id) == k)
                name = p.name;
        s.put(k, name, pstore::kRecProgress, kStoreVersion, encode(r), now);
        g_saved++;
    }
}
static void recs_adopt(const std::string& from, const std::string& to)
{
    auto it = g_recs.find(from);
    if (it == g_recs.end() || from == to)
        return;
    g_recs[to] = it->second;
    g_recs.erase(it);
}
static bool g_store_reg = store::add({pstore::kRecProgress, recs_save, recs_adopt});

static void host_record(uint8_t player, const Values& v)
{
    Record& r = g_recs[key_of(player)];
    r.world = world_now();
    r.v = v; // (the debt column stays: WP-R1)
}

static void send_table(double now)
{
    net::Writer w;
    write_table(w, g_effective, players_now());
    if (w.data == g_table_last && now - g_table_at < 1.0)
        return;
    if (now - g_table_at < 0.25)
        return;
    g_table_last = w.data;
    g_table_at = now;
    for (const auto& p : C.sess->players())
        if (p.id != C.sess->my_id())
            C.sess->send_reliable_to(p.id, net::MSG_PROGRESS, w);
}

static void host_start_tick()
{
    const bool want = wanted() && settings::started();
    if (!want) {
        const bool was = g_decided && g_effective;
        g_decided = g_effective = false;
        if (was && C.sess->connected())
            send_table(H->time_seconds() + 100); // the guests hear "not played" at once (the setting or the world changed)
        g_switch_due = false;
        return;
    }
    if (g_decided)
        return;
    std::vector<std::string> old;
    for (const auto& p : C.sess->players())
        if (p.id != C.sess->my_id() && !peer_reads_id(p.id))
            old.push_back(p.name.empty() ? "player " + std::to_string(p.id) : p.name);
    Start s = decide_start(true, old);
    g_decided = true;
    g_effective = s.active;
    g_join_check.clear();
    if (!s.notice.empty()) {
        g_refused_starts++;
        feed(s.notice);
        H->log(M, SK_WARN, "own progress: %s", s.notice.c_str());
    } else
        H->log(M, SK_INFO, "own progress: played in this world (all.shared_dna 2)");
}

static void host_join_tick()
{
    if (!g_effective || g_join_check.empty())
        return;
    std::vector<uint8_t> keep;
    for (uint8_t id : g_join_check) {
        bool known = false;
        bool reads = peer_reads_id(id, &known);
        if (!known)
            continue; // gone meanwhile
        if (reads)
            continue;
        std::string n = name_of(id);
        if (C.sess->kick(id, update_text())) {
            g_kicked++;
            feed(n + " was not let in: its SporeCoop is older and does not play own progress (it was told to update)");
            H->log(M, SK_WARN, "own progress: %s (an older mod) was not let in", n.c_str());
        }
    }
    g_join_check = keep;
}

static void host_tick(double now)
{
    host_start_tick();
    recs_load();
    if (!g_decided)
        return;
    host_join_tick();
    send_table(now);
    if (!g_effective)
        return;
    // the kept click runs once every bar is full (outside the frame, as the vote's action runs it)
    if (g_switch_due && game::mode_id() == modes::kCell && !(C.vote_active && C.vote_active()) && all_full()) {
        g_switch_due = false;
        g_resumes++;
        H->log(M, SK_INFO, "own progress: every player's bar is full: the kept click on History's check goes on");
        if (C.resume_end)
            C.resume_end(g_due_at_end);
    }
    if (game::mode_id() != modes::kCell)
        g_switch_due = false;
}

// ---- guest: our values back after a copy of the host's world opened, then the reports
static void apply_values(const Values& want)
{
    Values cur;
    if (!local_values(cur) || cur.mode != want.mode)
        return;
    g_applied++;
    if (want.mode == modes::kCell) {
        if (want.spent != cur.spent)
            cellsync::set_spent_dna(want.spent);
        if (want.food != cur.food)
            cellsync::set_food_total(want.food);
        H->log(M, SK_INFO, "own progress: own Cell values back: food %d -> %d, spent %d -> %d", cur.food, want.food,
               cur.spent, want.spent);
        return;
    }
    CreatureApply a = creature_apply(cur.bar, cur.level, want);
    if (a.up > 0)
        game::dna_add_raw(a.up); // the game's AddEvolutionPoints: DNA and bar (brain upgrades follow)
    if (std::isfinite(a.bar_write))
        game::set_evolution_bar(a.bar_write);
    game::dna_set_raw(a.dna);
    if (a.level >= 0)
        game::set_brain_level(a.level);
    H->log(M, SK_INFO, "own progress: own Creature values back: DNA %.0f -> %.0f, bar %.0f -> %.0f, brain %d -> %d",
           cur.dna, want.dna, cur.bar, want.bar, (int)cur.level, a.level >= 0 ? a.level : (int)cur.level);
}

static void guest_tick(double now)
{
    const uint32_t m = game::mode_id();
    if (!is_stage(m)) {
        g_stage_since = -1;
        return;
    }
    if (g_stage_since < 0)
        g_stage_since = now;
    Values v;
    if (!local_values(v) || now - g_stage_since < 2 || world::cinematic_playing() || own_world::elsewhere())
        return; // (another world of ours is not the host's record)
    const uint32_t epoch = game::stage_epoch(m), w = modes::current_world();
    if (g_synced && (g_synced_mode != m || g_synced_epoch != epoch || g_synced_world != w))
        g_synced = false; // a load, a received world, another stage: what the host keeps of this one, if anything
    if (!g_synced) {
        if (now - g_ask_at < 3)
            return;
        g_ask_at = now;
        net::Writer o;
        o.u8(kAsk);
        o.u32(m);
        C.sess->send_reliable_to(0, net::MSG_PROGRESS, o);
        g_asks++;
        return;
    }
    if (g_gain_owed > 0 && m == modes::kCreature && game::avatar()) {
        game::dna_add_raw(g_gain_owed);
        H->log(M, SK_INFO, "own progress: +%.0f DNA of our deeds paid while we had no creature", g_gain_owed);
        g_gain_owed = 0;
    }
    if ((v == g_last_report && now - g_report_at < 1.0) || now - g_report_at < 0.25)
        return;
    g_last_report = v;
    g_report_at = now;
    net::Writer o;
    o.u8(kReport);
    write_values(o, v);
    C.sess->send_reliable_to(0, net::MSG_PROGRESS, o);
    g_reports_out++;
}

// ---- the notices
static void notice_tick(double now)
{
    const uint32_t m = stage_now();
    Values v;
    if (!m || !local_values(v) || (is_guest() && !g_synced))
        return; // (an editor, a load: nothing changes)
    if (m != g_notice_mode) {
        g_notice_mode = m;
        g_was_full = full(v); // a stage just entered: no "just filled" for a bar that came full
        g_was_all = false;
        g_remind_at = now + 60;
    }
    std::string waiting;
    const bool me_full = full(v);
    const bool all = all_full(&waiting);
    if (me_full && !g_was_full && !all) {
        g_full_notices++;
        feed(full_text(m == modes::kCell, waiting));
        g_remind_at = now + 60;
    } else if (me_full && !all && now >= g_remind_at) {
        g_remind_at = now + 60;
        feed(remind_text(waiting));
    }
    if (all && !g_was_all && C.sess->players().size() > 1) {
        g_all_notices++;
        if (is_host())
            feed(m == modes::kCell ? (g_switch_due ? "Every player's bar is full: the vote to move on to the Creature stage starts"
                                                   : "Every player's bar is full: click History's check to move on to the Creature stage together")
                                   : "Every player's bar is full: use the evolve button to move on to the Tribe stage together (a vote follows)");
        else
            feed("Every player's bar is full: " + name_of(0) + " (host) moves everybody on together (a vote follows)");
    }
    g_was_full = me_full;
    g_was_all = all;
}

// ---- Cell levels together (own_progress_logic.h food_cap)
static int cap_now(int* low_level = nullptr, std::string* who = nullptr)
{
    if (!g_level_fix || !active() || game::mode_id() != modes::kCell)
        return INT_MAX;
    std::vector<int32_t> foods;
    std::vector<std::string> names;
    const uint8_t my = C.sess->my_id();
    if (is_host()) {
        for (auto& [id, l] : g_live)
            if (id != my && l.reported && l.v.mode == modes::kCell && !own_world::peer_elsewhere(id)) {
                foods.push_back(l.v.food);
                names.push_back(name_of(id));
            }
        for (auto& [id, f] : g_left_food) // a partner who left keeps its level (it comes back to the same pool)
            if (!g_live.count(id)) {
                foods.push_back(f);
                names.push_back("your partner (away)");
            }
    } else if (H->time_seconds() - g_table_got < 5) {
        for (auto& p : g_table)
            if (p.id != my && p.known && p.mode == modes::kCell) {
                foods.push_back(p.food);
                names.push_back(name_of(p.id));
            }
    }
    const int cap = food_cap(foods, low_level);
    if (who && !foods.empty())
        *who = names[std::min_element(foods.begin(), foods.end()) - foods.begin()];
    return cap;
}

int clip_meal(int amount, int cur)
{
    if (amount <= 0)
        return amount;
    int lv = 0;
    std::string who;
    const int cap = cap_now(&lv, &who);
    const int give = clip(amount, cur, cap, g_bank);
    if (give == amount)
        return amount;
    g_clipped++;
    const double now = H->time_seconds();
    if (g_level_told != lv || now - g_level_told_at > 30) {
        g_level_told = lv;
        g_level_told_at = now;
        feed("Nobody grows alone: your cell waits at level " + std::to_string(lv) + " until " + who +
             " gets there too (your meals are kept: +" + std::to_string(g_bank) + ")");
    }
    H->log(M, SK_INFO, "own progress: meal +%d over the level line (food %d, limit %d while %s is on level %d): +%d now, %d kept",
           amount, cur, cap, who.c_str(), lv, give, g_bank);
    return give;
}

// the kept meals go to the game as far as the level line allows (all of them without a limit: a partner left for good,
// the session or the setting ended); outside Cell they wait for this game's next Cell stage
static void bank_tick(double now)
{
    static double next;
    if (g_bank <= 0 || game::mode_id() != modes::kCell || now < next)
        return;
    next = now + 2; // (set_food_total sets a total in the next cell step: one at a time)
    int cur = 0;
    if (!cellsync::food_total(cur))
        return;
    const int n = release(g_bank, cur, cap_now());
    if (n <= 0 || !cellsync::set_food_total(cur + n))
        return;
    g_bank -= n;
    g_released++;
    H->log(M, SK_INFO, "own progress: %d kept meal point(s) added (food %d -> %d, %d still kept)", n, cur, cur + n, g_bank);
    if (!g_bank)
        feed("Your kept meals are in: +" + std::to_string(n) + " food");
}

void tick(double now)
{
    if (!H || !C.sess)
        return;
    bank_tick(now); // (also after the session or the setting ended: everything kept goes in)
    if (is_host())
        host_tick(now);
    else if (is_guest() && active())
        guest_tick(now);
    if (active())
        notice_tick(now);
    else
        g_switch_due = false;
}

bool hold_stage_end(bool cell, bool at_end)
{
    if (!is_host() || !active())
        return false;
    std::string waiting;
    const bool all = all_full(&waiting);
    switch (g_gate.click(all, H->time_seconds())) {
    case End::Go:
        g_switch_due = false;
        return false;
    case End::Force:
        g_forced++;
        g_switch_due = false;
        feed(force_text(waiting));
        H->log(M, SK_INFO, "own progress: the host moves on without %s (a second click, the vote decides)", waiting.c_str());
        return false;
    case End::Hold:
        break;
    }
    g_holds++;
    if (cell) {
        g_switch_due = true;
        g_due_at_end = at_end;
    }
    feed(hold_text(cell, waiting));
    H->log(M, SK_INFO, "own progress: the stage end waits until every player's bar is full (%s; %s)", waiting.c_str(),
           cell ? (at_end ? "History's check kept before the end sequence" : "the switch held after the end sequence")
                : "the stage-end request refused");
    return true;
}

void tell_wait(bool cell)
{
    g_waits_told++;
    const double now = H->time_seconds();
    if (now - g_wait_told_at < 3)
        return;
    g_wait_told_at = now;
    std::string waiting;
    if (all_full(&waiting))
        feed("Every player's bar is full: " + name_of(0) + " (host) moves everybody on together (a vote follows)");
    else
        feed(std::string("Nobody evolves alone: you move on to the ") + (cell ? "Creature" : "Tribe") +
             " stage together once every player's bar is full (waiting for: " + waiting + ")");
}

void give_gain(uint8_t player, float dna, uint8_t cause)
{
    if (!is_host() || !(dna > 0) || dna > 10000)
        return;
    net::Writer w;
    w.u8(kGain);
    w.f32(dna);
    w.u8(cause);
    C.sess->send_reliable_to(player, net::MSG_PROGRESS, w);
    g_gains_out++;
    H->log(M, SK_INFO, "own progress: +%.0f DNA the host's game paid for %s's %s goes to %s", dna, name_of(player).c_str(),
           cause == kGainFriend ? "friend" : cause == kGainHunt ? "hunt" : "deed", name_of(player).c_str());
}

void take_gain(float dna, const char* why)
{
    if (!(dna > 0) || dna > 10000)
        return;
    g_gains_in++;
    if (game::mode_id() == modes::kCreature && game::avatar())
        game::dna_add_raw(dna); // the game's AddEvolutionPoints: our DNA and our bar (not reported: not forwarded)
    else
        g_gain_owed += dna;
    H->log(M, SK_INFO, "own progress: +%.0f DNA of our %s (paid on the host's game)", dna, why);
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    if (!r.ok() || !C.sess)
        return;
    if (C.sess->role() == net::Role::Host) {
        if (from == C.sess->my_id())
            return;
        if (kind == kAsk) {
            uint32_t mode = r.u32();
            if (!r.ok())
                return;
            g_asks++;
            recs_load();
            auto it = g_recs.find(key_of(from));
            const Values* v = g_restore_fix && g_effective ? restore_for(it == g_recs.end() ? nullptr : &it->second,
                                                                         world_now(), mode)
                                                           : nullptr;
            g_live[from].reported = false;
            net::Writer w;
            w.u8(kSet);
            w.u32(mode);
            w.u8(v ? 1 : 0);
            if (v)
                write_values(w, *v);
            C.sess->send_reliable_to(from, net::MSG_PROGRESS, w);
            g_sets++;
            H->log(M, SK_INFO, "own progress: %s's values of %s %s", name_of(from).c_str(), modes::mode_name(mode),
                   v ? "go back to it" : !g_restore_fix ? "are the world's (core.progress_restore off)" : "are the world's (none kept)");
        } else if (kind == kReport) {
            Values v;
            if (!read_values(r, v) || !g_effective)
                return;
            g_reports_in++;
            Live& l = g_live[from];
            l.v = v;
            l.reported = true;
            host_record(from, v);
        }
        return;
    }
    if (from != 0)
        return;
    if (kind == kTable) {
        bool act = false;
        std::vector<Player> t;
        if (!read_table(r, act, t))
            return;
        if (act != g_host_active)
            H->log(M, SK_INFO, "own progress: the host %s", act ? "plays own progress" : "does not play own progress");
        g_host_active = act;
        g_table = std::move(t);
        g_table_got = H->time_seconds();
        g_tables_in++;
    } else if (kind == kSet) {
        uint32_t mode = r.u32();
        bool has = r.u8() != 0;
        Values v;
        if (!r.ok() || (has && !read_values(r, v)))
            return;
        if (mode != game::mode_id() || g_synced)
            return; // an answer for another stage (asked again), or a second answer
        g_sets++;
        if (has)
            apply_values(v);
        g_synced = true;
        g_synced_mode = mode;
        g_synced_epoch = game::stage_epoch(mode);
        g_synced_world = modes::current_world();
        g_report_at = -100;
        g_last_report = {};
        if (has)
            feed("Your own DNA and evolution bar are back (" + std::to_string(percent(v)) + " % of this stage)");
    } else if (kind == kGain) {
        float d = r.f32();
        uint8_t cause = r.left() >= 1 ? r.u8() : 0;
        if (r.ok() && active())
            take_gain(d, cause == kGainFriend ? "friend" : cause == kGainHunt ? "hunt" : "deed");
    }
}

static void on_session_end()
{
    g_live.clear();
    g_left_food.clear();
    g_switch_due = false;
    g_decided = g_effective = false;
    g_join_check.clear();
    g_gate = {};
    g_host_active = false;
    g_synced = false;
    g_table.clear();
    g_table_got = -100;
    g_table_last.clear();
    g_table_at = -100;
    g_gain_owed = 0;
    g_was_full = g_was_all = false;
    g_notice_mode = 0;
    g_stage_since = -1;
    g_ask_at = -100;
}
LIFECYCLE_HOOK("own_progress.session", lifecycle::kSessionEnd, -10, [](const lifecycle::Ctx&) { on_session_end(); }, false);
// lifecycle-exempt: called from session_glue.cpp cb.player_join (the registry never fires kPlayerJoin, BUG-1533)
void on_player_join(uint8_t id)
{
    if (!is_host())
        return;
    g_join_check.push_back(id); // checked in the tick (an older mod is not let in while own progress is played)
    g_left_food.erase(id);
}

// lifecycle-exempt: called from session_glue.cpp's leave callback (the registry never fires kPlayerLeave, BUG-1533)
void on_player_leave(uint8_t id)
{
    auto it = g_live.find(id);
    if (it != g_live.end() && it->second.reported && it->second.v.mode == modes::kCell)
        g_left_food[id] = it->second.v.food; // the level line keeps it (it comes back to the same pool)
    g_live.erase(id);
}

void resync()
{
    g_synced = false;
    g_ask_at = -100;
}

std::string status()
{
    char b[720];
    Values v;
    const bool have = local_values(v);
    std::string waiting;
    const bool all = all_full(&waiting);
    int low = 0;
    const int cap = cap_now(&low);
    snprintf(b, sizeof b,
             "own progress: active=%d setting=%d mode=%d fix=%d end_click=%d level_together=%d restore=%d role=%s stage=%s "
             "have=%d dna=%.1f bar=%.1f level=%d food=%d spent=%d full=%d pct=%d all_full=%d waiting=[%s]\n",
             (int)active(), settings::get(settings::kSharedDna), dna_mode(), (int)g_fix, (int)g_end_click_fix,
             (int)g_level_fix, (int)g_restore_fix, is_host() ? "host" : is_guest() ? "guest" : "none",
             modes::mode_name(game::mode_id()), (int)have, have ? v.dna : 0.f, have ? v.bar : 0.f,
             have ? (int)v.level : -1, have ? v.food : 0, have ? v.spent : 0, (int)(have && full(v)),
             have ? percent(v) : 0, (int)all, waiting.c_str());
    std::string out = b;
    snprintf(b, sizeof b,
             "  decided=%d effective=%d host_active=%d synced=%d switch_due=%d at_end=%d asks=%u sets=%u applied=%u "
             "reports_in=%u reports_out=%u gains_out=%u gains_in=%u owed=%.1f holds=%u forced=%u resumes=%u tables_in=%u "
             "full_notices=%u all_notices=%u waits_told=%u refused_starts=%u kicked=%u loaded=%u saved=%u\n",
             (int)g_decided, (int)g_effective, (int)g_host_active, (int)g_synced, (int)g_switch_due, (int)g_due_at_end,
             g_asks, g_sets, g_applied, g_reports_in, g_reports_out, g_gains_out, g_gains_in, g_gain_owed, g_holds,
             g_forced, g_resumes, g_tables_in, g_full_notices, g_all_notices, g_waits_told, g_refused_starts, g_kicked,
             g_loaded, g_saved);
    out += b;
    snprintf(b, sizeof b, "  cap=%d low_level=%d bank=%d clipped=%u released=%u\n", cap == INT_MAX ? -1 : cap,
             cap == INT_MAX ? -1 : low, g_bank, g_clipped, g_released);
    out += b;
    for (const auto& p : players_now()) {
        snprintf(b, sizeof b, "  player %u %s: known=%d stage=%s full=%d pct=%d food=%d\n", (unsigned)p.id,
                 name_of(p.id).c_str(), (int)p.known, modes::mode_name(p.mode), (int)p.full, p.pct, p.food);
        out += b;
    }
    for (const auto& [k, r] : g_recs) {
        snprintf(b, sizeof b, "  record %s: world=%08X stage=%s dna=%.1f bar=%.1f level=%d food=%d spent=%d debt=%.1f\n",
                 k.c_str(), r.world, modes::mode_name(r.v.mode), r.v.dna, r.v.bar, (int)r.v.level, r.v.food, r.v.spent,
                 r.debt);
        out += b;
    }
    return out;
}

} // namespace own_progress
