// Own progress in the Cell and Creature stages (own_progress.h, BUG-9265).
#include "own_progress.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "cellsync.h"
#include "fixes.h"
#include "founders.h"
#include "game.h"
#include "identity.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/protocol.h"
#include "party.h"
#include "settings.h"
#include "slotfs.h"
#include "world.h"
#include "cell_logic.h"
#include <climits>

namespace own_progress {

static const SkHost* H;
static SkMod* M;
static Ctx C;

// `coop.fix core.own_progress off`: the setting reads off (the shared total of 0.7.15), for the tests' "without" run
static bool g_fix = true;
COOP_FIX("core.own_progress", "BUG-9265", g_fix,
         "cell, creature: every player's own DNA and evolution bar (the session setting all.own_progress); off: the party's shared total");
// BUG-9287 (own_progress.h clip_meal)
static bool g_level_fix = true;
COOP_FIX("cell.level_together", "BUG-9287", g_level_fix,
         "cell, own progress: nobody grows a Cell level alone (the pool is the host's level): meals over the next level line are kept until every partner reached it");
// BUG-9286: the side file waits quietly for the planet's first save (a new game has no <planet>.spo until then)
static bool g_quiet_fix = true;
COOP_FIX("core.own_progress_quiet", "BUG-9286", g_quiet_fix,
         "own progress: <planet>.spo.duoprogress waits for the planet's first save without a warning (a new game)");

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }

bool active()
{
    if (!g_fix || !C.sess || !C.sess->connected() || C.sess->players().size() < 2)
        return false;
    if (C.one_creature && C.one_creature())
        return false; // creature.mode host: one creature, one progress (the host's)
    return settings::on(settings::kOwnProgress);
}

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

// ---- host: every guest's values (live: this session; recs: the planet's, saved next to it)
struct Live {
    Values v;
    bool reported = false;
    double at = 0;
};
static std::map<uint8_t, Live> g_live;
static std::vector<Record> g_recs;
static std::string g_planet;
static bool g_dirty, g_save_queued;
static double g_next_planet, g_next_save, g_table_at;
static std::vector<uint8_t> g_table_last;
static bool g_switch_due;      // host, Cell: a click on History's check held until every bar is full
static bool g_switch_resumed;  // host, Cell: ... and let go now (the "every bar is full" notice says the vote starts)
static uint32_t g_saves, g_save_errors, g_loaded_recs;

// ---- guest
static uint32_t g_loads_seen;
static bool g_loads_known;
static bool g_synced;              // the host gave our values back (or told there were none) for this copy
static uint32_t g_synced_mode;     // ... of this stage
static uint32_t g_wait_epoch[2] = {0xFFFFFFFFu, 0xFFFFFFFFu}; // Cell / Creature epochs at the load: wait for a new one
static double g_ask_at = -100, g_report_at = -100, g_stage_since = -1;
static Values g_last_report;
static std::vector<Player> g_table; // the host's table (kTable)
static double g_table_got = -100;
static float g_gain_owed;           // gains that came while this game had no creature

// ---- both: the notices
static bool g_was_full, g_was_all;
static double g_remind_at;
static uint32_t g_notice_mode;

// ---- tests (coop.progress)
static unsigned g_asks, g_sets, g_applied, g_reports_in, g_reports_out, g_gains_out, g_gains_in, g_holds, g_resumes,
    g_tables_in, g_full_notices, g_all_notices, g_waits_told;

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
}

static std::string name_of(uint8_t id)
{
    std::string n = C.sess ? C.sess->name_of(id) : std::string();
    return n.empty() ? "player " + std::to_string(id) : n;
}

static void feed(const std::string& s)
{
    if (C.feed)
        C.feed(s);
}

static uint32_t epoch_slot(uint32_t mode) { return mode == modes::kCell ? 0 : 1; }

// ---- the players' table (who is full)
static std::vector<Player> players_now()
{
    std::vector<Player> out;
    Values me;
    const bool have = local_values(me);
    const uint8_t my = C.sess ? C.sess->my_id() : 0;
    if (is_host()) {
        for (const auto& p : C.sess->players()) {
            Player x;
            x.id = p.id;
            x.name = p.name;
            if (p.id == my) {
                x.reported = have;
                x.mode = me.mode;
                x.full = have && full(me);
                x.pct = have ? percent(me) : 0;
            } else if (auto it = g_live.find(p.id); it != g_live.end() && it->second.reported) {
                x.reported = true;
                x.mode = it->second.v.mode;
                x.full = full(it->second.v);
                x.pct = percent(it->second.v);
            }
            out.push_back(x);
        }
        return out;
    }
    // guest: the host's table, our own line from our game
    for (const auto& p : g_table) {
        Player x = p;
        if (p.id == my) {
            x.reported = have && g_synced;
            x.mode = me.mode;
            x.full = have && full(me);
            x.pct = have ? percent(me) : 0;
        }
        x.name = name_of(p.id);
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
    if (!active() || !m) {
        if (waiting)
            *waiting = "";
        return false;
    }
    if (is_guest() && H->time_seconds() - g_table_got > 5) {
        if (waiting)
            *waiting = "the host's table (not known yet)";
        return false;
    }
    std::vector<Player> ps = players_now();
    // the waiting list names the others (this player knows its own bar)
    std::string w;
    bool all = own_progress::all_full(ps, m, &w);
    if (waiting) {
        std::vector<Player> others;
        const uint8_t my = C.sess->my_id();
        for (const auto& p : ps)
            if (p.id != my)
                others.push_back(p);
        others.push_back({my, "", true, m, true, 100}); // (at least two: the text names only the others)
        own_progress::all_full(others, m, waiting);
    }
    return all;
}

// ---- host: the planet's records
static unsigned g_waits_save; // BUG-9286: tries before the planet's first save
static bool planet_saved(const std::string& key)
{
    DWORD a = GetFileAttributesW(slotfs::widen(key).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void save_outside(void*)
{
    g_save_queued = false;
    if (!g_dirty || g_planet.empty() || g_planet != world::current_game_key())
        return;
    if (founders::write_side(g_planet, L".duoprogress", encode_file(g_recs))) {
        g_dirty = false;
        g_saves++;
    } else if (g_quiet_fix && !planet_saved(g_planet)) {
        // BUG-9286: a new game has no <planet>.spo before its first save (the first share or autosave writes it): the
        // records stay dirty and go out with the next try after it (the owner's log: two warnings in a new game)
        if (!g_waits_save++)
            H->log(M, SK_INFO, "own progress: <planet>.spo.duoprogress waits for the planet's first save");
    } else if (g_save_errors++ < 5)
        H->log(M, SK_WARN, "own progress: <planet>.spo.duoprogress not written");
}

static void planet_tick(double now)
{
    if (now < g_next_planet)
        return;
    g_next_planet = now + 1;
    const std::string planet = world::current_game_key();
    if (planet.empty() || !planet.ends_with(".spo") || game::mode_id() == modes::kMenu)
        return; // (the galaxy menu still names the planet played last)
    if (planet == g_planet)
        return;
    g_planet = planet;
    g_recs.clear();
    g_dirty = false;
    std::vector<uint8_t> bytes;
    std::vector<Record> recs;
    if (founders::read_side(planet, L".duoprogress", bytes, 8 + kMaxRecords * (256 + 32)) && decode_file(bytes, recs)) {
        g_recs = std::move(recs);
        g_loaded_recs++;
        H->log(M, SK_INFO, "own progress: %zu player record(s) of this planet", g_recs.size());
    }
}

static void host_record(uint8_t player, const Values& v)
{
    if (g_planet.empty())
        return;
    const std::string key = identity::key(player);
    Record& r = record_of(g_recs, key);
    if (r.v == v)
        return;
    r.v = v;
    g_dirty = true;
}

static void send_table(double now)
{
    std::vector<Player> ps = players_now();
    net::Writer w;
    w.u8(kTable);
    w.u8((uint8_t)ps.size());
    for (const auto& p : ps) {
        w.u8(p.id);
        w.u32(p.mode);
        w.u8((uint8_t)((p.reported ? 1 : 0) | (p.full ? 2 : 0)));
        w.u8((uint8_t)p.pct);
    }
    if (w.data == g_table_last && now - g_table_at < 3)
        return;
    g_table_last = w.data;
    g_table_at = now;
    for (const auto& p : C.sess->players())
        if (p.id != C.sess->my_id())
            C.sess->send_reliable_to(p.id, net::MSG_PROGRESS, w);
}

static void host_tick(double now)
{
    planet_tick(now);
    Values me;
    if (local_values(me))
        host_record(C.sess->my_id(), me);
    if (now - g_table_at >= 1)
        send_table(now);
    if (g_dirty && !g_save_queued && now >= g_next_save) {
        g_next_save = now + 3;
        g_save_queued = true;
        H->run_outside_frame(save_outside, nullptr); // a file write: outside the frame (CLAUDE.md 2)
    }
}

// ---- guest: give our values back after a copy of the host's world opened, then report them
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
    CreatureApply a = creature_apply(cur.bar, want);
    if (a.up > 0)
        game::dna_add_raw(a.up); // the game's AddEvolutionPoints: DNA and bar (brain upgrades follow)
    if (std::isfinite(a.bar_write))
        game::set_evolution_bar(a.bar_write);
    game::dna_set_raw(a.dna);
    if (want.level < cur.level) // the save's (the host's) higher brain level: ours back (a higher one comes with the bar)
        game::set_brain_level(want.level);
    H->log(M, SK_INFO, "own progress: own Creature values back: DNA %.0f -> %.0f, bar %.0f -> %.0f (brain %d, kept %d)",
           cur.dna, want.dna, cur.bar, want.bar, (int)cur.level, (int)want.level);
}

static bool guest_settled(uint32_t m, double now)
{
    if (!is_stage(m)) {
        g_stage_since = -1;
        return false;
    }
    if (g_stage_since < 0)
        g_stage_since = now;
    Values v;
    if (!local_values(v) || now - g_stage_since < 2 || world::cinematic_playing())
        return false;
    if (C.elsewhere && C.elsewhere())
        return false; // its own world: not the host's record
    const uint32_t e = game::stage_epoch(m);
    return g_wait_epoch[epoch_slot(m)] == 0xFFFFFFFFu || e != g_wait_epoch[epoch_slot(m)];
}

static void guest_tick(double now)
{
    const uint32_t loads = world::loads_started();
    if (!g_loads_known || loads != g_loads_seen) {
        if (g_loads_known) { // a load of ours started: its world is the host's copy (or our own)
            g_wait_epoch[0] = game::stage_epoch(modes::kCell);
            g_wait_epoch[1] = game::stage_epoch(modes::kCreature);
        }
        g_loads_known = true;
        g_loads_seen = loads;
        g_synced = false;
    }
    const uint32_t m = game::mode_id();
    if (!guest_settled(m, now))
        return;
    if (g_synced && g_synced_mode != m)
        g_synced = false; // another stage (the game's own switch): what the host keeps of that one, if anything
    if (!g_synced) {
        if (now - g_ask_at < 3)
            return;
        g_ask_at = now;
        net::Writer w;
        w.u8(kAsk);
        w.u32(m);
        C.sess->send_reliable_to(0, net::MSG_PROGRESS, w);
        g_asks++;
        return;
    }
    if (g_gain_owed > 0 && m == modes::kCreature && game::avatar()) {
        game::dna_add_raw(g_gain_owed);
        g_gain_owed = 0;
    }
    Values v;
    if (!local_values(v))
        return;
    if (v == g_last_report && now - g_report_at < 2)
        return;
    if (now - g_report_at < 0.5)
        return;
    g_last_report = v;
    g_report_at = now;
    net::Writer w;
    w.u8(kReport);
    write_values(w, v);
    w.u8(full(v) ? 1 : 0);
    C.sess->send_reliable_to(0, net::MSG_PROGRESS, w);
    g_reports_out++;
}

// ---- the notices
static void notice_tick(double now)
{
    const uint32_t m = stage_now();
    Values v;
    if (!m || !local_values(v) || (is_guest() && !g_synced)) {
        return; // (an editor, a load: nothing changes; the flags keep)
    }
    if (m != g_notice_mode) {
        g_notice_mode = m;
        g_was_full = full(v); // a stage just entered (a load): no "just filled" for a bar that came full
        g_was_all = false;
        g_remind_at = now + 60;
    }
    std::string waiting;
    const bool me_full = full(v);
    const bool all = all_full(&waiting);
    if (me_full && !g_was_full && !all) {
        g_full_notices++;
        // (Cell: the click on History's check turns the screen white until the move, BUG-1269: told to wait for it)
        feed(std::string("Your evolution bar is full. Nobody evolves alone: keep playing; ") +
             (m == modes::kCell ? "click History's check" : "use the evolve button") +
             " once every partner's bar is full too (you will be told). Waiting for: " + waiting);
        g_remind_at = now + 60;
    } else if (me_full && !all && now >= g_remind_at) {
        g_remind_at = now + 60;
        feed("Your bar is full: still waiting for " + waiting);
    }
    if (all && !g_was_all) {
        g_all_notices++;
        if (is_host())
            feed(m == modes::kCell ? (g_switch_due || g_switch_resumed ? "Every player's bar is full: the vote to move on to the Creature stage starts"
                                                   : "Every player's bar is full: click History's check to move on to the Creature stage together")
                                   : "Every player's bar is full: use the evolve button to move on to the Tribe stage together (a vote follows)");
        else
            feed("Every player's bar is full: " + name_of(0) + " (host) moves everybody on together (a vote follows)");
    }
    g_was_full = me_full;
    g_was_all = all;
    g_switch_resumed = false;
}

// ---- BUG-9287: nobody grows a Cell level alone (own_progress.h clip_meal)
static int g_bank;              // own meals kept above the level line
static unsigned g_clipped, g_released, g_level_waits;
static double g_level_told_at = -1e9;
static int g_level_told = -1;

// the highest food this game may have now: the last food of the lowest partner's level (INT_MAX: no limit)
static int food_cap(int* low_level = nullptr, std::string* who = nullptr)
{
    if (!g_level_fix || !active() || game::mode_id() != modes::kCell || !C.sess)
        return INT_MAX;
    int low = INT_MAX;
    const uint8_t my = C.sess->my_id();
    auto note = [&](uint8_t id, int food) {
        if (food < low) {
            low = food;
            if (who)
                *who = name_of(id);
        }
    };
    if (is_host()) {
        for (auto& [id, l] : g_live)
            if (id != my && l.reported && l.v.mode == modes::kCell)
                note(id, l.v.food);
    } else if (H->time_seconds() - g_table_got < 5) {
        for (auto& p : g_table) // (the table's percent: food * 100 / 1000, the level lines are multiples of 100)
            if (p.id != my && p.reported && p.mode == modes::kCell)
                note(p.id, p.pct * kCellFull / 100);
    }
    if (low == INT_MAX)
        return INT_MAX;
    const int lv = cell_logic::level_of_food(low);
    if (low_level)
        *low_level = lv;
    return lv >= cell_logic::level_of_food(kCellFull) ? INT_MAX : lv * 100 - 1;
}

int clip_meal(int amount, int cur)
{
    if (amount <= 0)
        return amount;
    int lv = 0;
    std::string who;
    const int cap = food_cap(&lv, &who);
    if (cap == INT_MAX || cur + amount <= cap)
        return amount;
    const int give = std::max(0, cap - cur);
    g_bank += amount - give;
    g_clipped++;
    const double now = H->time_seconds();
    if (g_level_told != lv || now - g_level_told_at > 30) {
        g_level_told = lv;
        g_level_told_at = now;
        g_level_waits++;
        feed("Nobody grows alone: your cell waits at level " + std::to_string(lv) + " until " + who +
             " gets there too (your meals are kept: +" + std::to_string(g_bank) + ")");
    }
    H->log(M, SK_INFO, "own progress: meal +%d over the level line (food %d, limit %d while %s is on level %d): +%d now, %d kept",
           amount, cur, cap, who.c_str(), lv, give, g_bank);
    return give;
}

// the kept meals go to the game as far as the level line allows (all of them without a limit: a partner left, the
// setting off); outside Cell they wait (the next Cell stage of this game)
static void bank_tick()
{
    static double next;
    const double now = H->time_seconds();
    // (set_food_total sets a total in the next cell step: one at a time, or a second one from the old total undid it)
    if (g_bank <= 0 || game::mode_id() != modes::kCell || now < next)
        return;
    next = now + 2;
    int cur = 0;
    if (!cellsync::food_total(cur))
        return;
    const int cap = food_cap();
    const int n = cap == INT_MAX ? g_bank : std::min(g_bank, cap - cur);
    if (n <= 0)
        return;
    if (!cellsync::set_food_total(cur + n))
        return;
    g_bank -= n;
    g_released++;
    H->log(M, SK_INFO, "own progress: %d kept meal point(s) added (food %d -> %d, %d still kept)", n, cur, cur + n, g_bank);
    if (!g_bank)
        feed("Your kept meals are in: +" + std::to_string(n) + " food");
}

void tick(double now)
{
    bank_tick(); // BUG-9287 (also after the session or the setting ended: everything kept goes in)
    if (!active()) {
        g_switch_due = false;
        return;
    }
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
    notice_tick(now);
}

bool hold_stage_end(bool host)
{
    if (!active())
        return false;
    const uint32_t m = game::mode_id();
    std::string waiting;
    const bool all = all_full(&waiting);
    if (host) {
        if (all)
            return false;
        g_holds++;
        if (m == modes::kCell) {
            g_switch_due = true;
            feed("Not yet: you move on to the Creature stage together once every player's bar is full (waiting for: " +
                 waiting + "). Your click is kept: the vote starts by itself then.");
        } else
            feed("Not yet: you move on to the Tribe stage together once every player's bar is full (waiting for: " + waiting +
                 "). Then use the evolve button again (Esc closes History).");
        return true;
    }
    g_waits_told++;
    if (all)
        feed("Every player's bar is full: " + name_of(0) + " (host) moves everybody on together (a vote follows)");
    else
        feed("Nobody evolves alone: you move on together once every player's bar is full (waiting for: " + waiting + ")");
    return true;
}

bool switch_resume_due()
{
    if (!g_switch_due || !is_host() || game::mode_id() != modes::kCell || (C.vote_active && C.vote_active()))
        return false;
    if (!all_full())
        return false;
    g_switch_due = false;
    g_switch_resumed = true;
    g_resumes++;
    return true;
}

void give_gain(uint8_t player, float dna)
{
    if (!is_host() || !(dna > 0) || dna > 10000)
        return;
    net::Writer w;
    w.u8(kGain);
    w.f32(dna);
    C.sess->send_reliable_to(player, net::MSG_PROGRESS, w);
    g_gains_out++;
    H->log(M, SK_INFO, "own progress: +%.0f DNA earned on this game by %s's deed goes to %s", dna, name_of(player).c_str(),
           name_of(player).c_str());
}

void take_gain(float dna, const char* why)
{
    if (!(dna > 0) || dna > 10000)
        return;
    g_gains_in++;
    if (game::mode_id() == modes::kCreature && game::avatar())
        game::dna_add_raw(dna); // the game's AddEvolutionPoints: our DNA and our bar
    else
        g_gain_owed += dna;
    H->log(M, SK_INFO, "own progress: +%.0f DNA of our %s (paid on the host's game)", dna, why);
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (is_host() && from != C.sess->my_id()) {
        if (kind == kAsk) {
            uint32_t mode = r.u32();
            if (!r.ok())
                return;
            g_asks++;
            planet_tick(H->time_seconds() + 10); // (a planet just opened: its records first)
            const Values* v = restore_for(g_recs, identity::key(from), mode);
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
                   v ? "go back to it" : "are the world's (none kept)");
        } else if (kind == kReport) {
            Values v;
            if (!read_values(r, v))
                return;
            g_reports_in++;
            Live& l = g_live[from];
            l.v = v;
            l.reported = true;
            l.at = H->time_seconds();
            host_record(from, v);
        }
        return;
    }
    if (!is_guest() || from != 0)
        return;
    if (kind == kSet) {
        uint32_t mode = r.u32();
        bool has = r.u8() != 0;
        Values v;
        if (!r.ok() || (has && !read_values(r, v)))
            return;
        if (mode != game::mode_id())
            return; // an answer for another stage: asked again
        g_sets++;
        if (has)
            apply_values(v);
        g_synced = true;
        g_synced_mode = mode;
        g_report_at = -100;
        g_last_report = {};
        if (has)
            feed("Your own DNA and evolution bar are back (" + std::to_string(percent(v)) + "% of this stage)");
    } else if (kind == kTable) {
        unsigned n = r.u8();
        std::vector<Player> t;
        for (unsigned i = 0; i < n && r.ok(); i++) {
            Player p;
            p.id = r.u8();
            p.mode = r.u32();
            uint8_t f = r.u8();
            p.pct = r.u8();
            p.reported = (f & 1) != 0;
            p.full = (f & 2) != 0;
            t.push_back(p);
        }
        if (!r.ok())
            return;
        g_table = std::move(t);
        g_table_got = H->time_seconds();
        g_tables_in++;
    } else if (kind == kGain) {
        float d = r.f32();
        if (r.ok())
            take_gain(d, "deed");
    }
}

void on_player_left(uint8_t player) { g_live.erase(player); }

void on_session_end()
{
    g_live.clear();
    g_switch_due = false;
    g_synced = false;
    g_loads_known = false;
    g_wait_epoch[0] = g_wait_epoch[1] = 0xFFFFFFFFu;
    g_table.clear();
    g_table_got = -100;
    g_table_last.clear();
    g_gain_owed = 0;
    g_was_full = g_was_all = false;
    g_notice_mode = 0;
    g_stage_since = -1;
}
LIFECYCLE_HOOK("own_progress.session", lifecycle::kSessionEnd, -10, [](const lifecycle::Ctx&) { on_session_end(); }, false);

void resync()
{
    g_synced = false;
    g_ask_at = -100;
}

std::string status()
{
    char b[640];
    Values v;
    const bool have = local_values(v);
    std::string waiting;
    const bool all = all_full(&waiting);
    snprintf(b, sizeof b,
             "own progress: active=%d setting=%d role=%s mode=%s have=%d dna=%.1f bar=%.1f level=%d food=%d spent=%d full=%d "
             "pct=%d all_full=%d waiting=[%s]\n",
             (int)active(), (int)settings::on(settings::kOwnProgress), is_host() ? "host" : is_guest() ? "guest" : "none",
             modes::mode_name(game::mode_id()), (int)have, have ? v.dna : 0.f, have ? v.bar : 0.f, have ? (int)v.level : -1,
             have ? v.food : 0, have ? v.spent : 0, (int)(have && full(v)), have ? percent(v) : 0, (int)all, waiting.c_str());
    std::string out = b;
    snprintf(b, sizeof b,
             "  synced=%d synced_mode=%s switch_due=%d asks=%u sets=%u applied=%u reports_in=%u reports_out=%u gains_out=%u "
             "gains_in=%u holds=%u resumes=%u tables_in=%u full_notices=%u all_notices=%u waits_told=%u saves=%u loaded=%u\n",
             (int)g_synced, modes::mode_name(g_synced_mode), (int)g_switch_due, g_asks, g_sets, g_applied, g_reports_in,
             g_reports_out, g_gains_out, g_gains_in, g_holds, g_resumes, g_tables_in, g_full_notices, g_all_notices,
             g_waits_told, g_saves, g_loaded_recs);
    out += b;
    int low = 0;
    const int cap = food_cap(&low);
    snprintf(b, sizeof b, "  level_together=%d cap=%d low_level=%d bank=%d clipped=%u released=%u level_waits=%u waits_save=%u\n",
             (int)g_level_fix, cap == INT_MAX ? -1 : cap, cap == INT_MAX ? -1 : low, g_bank, g_clipped, g_released, g_level_waits,
             g_waits_save); // BUG-9287, BUG-9286
    out += b;
    for (const auto& p : players_now()) {
        snprintf(b, sizeof b, "  player %u %s: known=%d mode=%s full=%d pct=%d\n", (unsigned)p.id, name_of(p.id).c_str(),
                 (int)p.reported, modes::mode_name(p.mode), (int)p.full, p.pct);
        out += b;
    }
    for (const auto& r : g_recs) {
        snprintf(b, sizeof b, "  record %s: mode=%s dna=%.1f bar=%.1f level=%d food=%d spent=%d\n", r.key.c_str(),
                 modes::mode_name(r.v.mode), r.v.dna, r.v.bar, (int)r.v.level, r.v.food, r.v.spent);
        out += b;
    }
    return out;
}

} // namespace own_progress
