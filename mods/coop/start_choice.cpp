// Every player's own start in a new Cell game (start_choice.h, BUG-9273).
#include "start_choice.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "cellsync.h"
#include "coop_ui.h"
#include "creations.h"
#include "find.h"
#include "fixes.h"
#include "founders.h"
#include "game.h"
#include "identity.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/protocol.h"
#include "notify.h"
#include "own_progress.h"
#include "own_world.h"
#include "party.h"
#include "settings.h"
#include "start_choice_logic.h"
#include "world.h"

namespace start_choice {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

// the player colours a choice may name (find_core.h)
static constexpr int kColorCount = findx::kColors;

// ---- host
static std::string g_planet;            // the planet the records belong to ("" = none)
static std::vector<Record> g_recs;      // its choices, by identity key
static bool g_dirty, g_save_queued;
static double g_next_planet, g_next_save, g_table_at;
static std::vector<uint8_t> g_table_last;
static std::set<uint8_t> g_asked;           // players asked since they joined
static std::map<uint8_t, double> g_here_since; // player -> since when it plays the host's Cell world
static uint32_t g_asks, g_answers, g_kept, g_refused, g_saves, g_save_errors, g_loaded, g_tables_tx;
// ---- every game: the choices of the players here (the host's table)
static std::map<uint8_t, Choice> g_rows;
// ---- guest
static bool g_prompt;          // asked, not answered yet
static bool g_mine_known;      // our own choice is known (answered, or the host's table has it)
static Choice g_mine;
static double g_apply_at, g_color_at, g_zero_due;
static bool g_apply_due, g_color_due; // our cell / colour are to be played (guest_apply)
static uint32_t g_loads_seen;
static bool g_color_refused;
static uint32_t g_rx_asks, g_answers_tx, g_cell_applies, g_color_requests, g_fresh_starts, g_tables_rx;

SESSION_STATE("start_choice.session", nullptr, [] {
    g_asked.clear();
    g_here_since.clear();
    g_rows.clear();
    g_table_last.clear();
    g_prompt = false;
    g_mine_known = false;
    g_mine = {};
    g_color_refused = false;
    g_apply_due = g_color_due = false;
}, []() -> size_t { return g_asked.size() + g_rows.size() + (g_prompt ? 1 : 0); });

LIFECYCLE_HOOK("start_choice.leave", lifecycle::kPlayerLeave, 0, [](const lifecycle::Ctx& c) {
    g_asked.erase(c.player); // a rejoin is a new join: its record (by identity) decides, not this flag
    g_here_since.erase(c.player);
    g_rows.erase(c.player);
}, false);

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
}

static bool is_host() { return S && S->connected() && S->role() == net::Role::Host; }
static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }

bool enabled()
{
    return settings::on(settings::kStartChoice) && settings::get(settings::kCreatureMode) == 0;
}

static creations::Key cell_key(int diet)
{
    return {kDietCell[diet], kCellModelType, kCellModelGroup};
}

// ---- host: the planet's records
static void save_outside(void*)
{
    g_save_queued = false;
    if (!g_dirty || g_planet.empty() || g_planet != world::current_game_key())
        return;
    if (founders::write_side(g_planet, L".duostart", encode_file(g_recs))) {
        g_dirty = false;
        g_saves++;
    } else if (g_save_errors++ < 5)
        H->log(M, SK_WARN, "start choice: <planet>.spo.duostart not written");
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
    g_asked.clear(); // another planet: its own records decide
    std::vector<uint8_t> bytes;
    std::vector<Record> recs;
    if (founders::read_side(planet, L".duostart", bytes, 8 + kMaxRecords * (256 + 64))) {
        if (decode_file(bytes, recs, kColorCount)) {
            g_recs = std::move(recs);
            g_loaded++;
            H->log(M, SK_INFO, "start choice: %zu player choice(s) of this planet", g_recs.size());
        } else
            H->log(M, SK_WARN, "start choice: <planet>.spo.duostart unreadable: left alone");
    }
}

static const Record* record_of_player(uint8_t player)
{
    return find_record(g_recs, identity::key(player));
}

// the table of the choices of the players here, to every guest (on a change, and every 10 s)
static void send_table(double now, bool force)
{
    std::vector<Row> rows;
    for (const auto& p : S->players())
        if (p.id != S->my_id())
            if (const Record* r = record_of_player(p.id))
                rows.push_back({p.id, r->c});
    net::Writer w;
    write_table(w, rows);
    if (!force && w.data == g_table_last && now - g_table_at < 10)
        return;
    g_table_last = w.data;
    g_table_at = now;
    g_rows.clear();
    for (const auto& r : rows)
        g_rows[r.player] = r.c;
    for (const auto& p : S->players())
        if (p.id != S->my_id())
            S->send_reliable_to(p.id, net::MSG_START_CHOICE, w);
    g_tables_tx++;
}

static void host_tick(double now)
{
    planet_tick(now);
    const bool cell = game::mode_id() == modes::kCell && !g_planet.empty() && g_planet == world::current_game_key();
    for (const auto& p : S->players()) {
        if (p.id == S->my_id())
            continue;
        const bool here = cell && own_world::peer_here(p.id);
        if (!here) {
            g_here_since.erase(p.id);
            continue;
        }
        auto it = g_here_since.emplace(p.id, now).first;
        AskState st;
        st.setting = enabled();
        st.cell = cell && !world::cinematic_playing();
        st.seated = party::seat_of_player(p.id) >= 1;
        st.in_world = now - it->second >= 3; // settled: its world loaded and shown for a moment
        st.recorded = record_of_player(p.id) != nullptr;
        st.asked = g_asked.count(p.id) != 0;
        if (!should_ask(st))
            continue;
        g_asked.insert(p.id);
        net::Writer w;
        w.u8(kAsk);
        S->send_reliable_to(p.id, net::MSG_START_CHOICE, w);
        g_asks++;
        H->log(M, SK_INFO, "start choice: asked player %u (%s) for its own start on this planet", p.id, p.name.c_str());
    }
    if (enabled())
        send_table(now, false);
    if (g_dirty && !g_save_queued && now >= g_next_save) {
        g_next_save = now + 2;
        g_save_queued = true;
        H->run_outside_frame(save_outside, nullptr); // a file write: outside the frame (CLAUDE.md 2)
    }
}

static void host_answer(uint8_t from, net::Reader& r)
{
    Choice c;
    if (!read_choice(r, c, kColorCount)) {
        g_refused++;
        H->log(M, SK_WARN, "start choice: player %u sent a choice that is not one: refused", from);
        return;
    }
    if (g_planet.empty() || !enabled()) {
        g_refused++;
        return;
    }
    g_answers++;
    if (!record_once(g_recs, identity::key(from), c)) {
        g_kept++; // (a second answer: the first one stays)
        H->log(M, SK_INFO, "start choice: player %u answered again: its first choice stays", from);
    } else {
        g_dirty = true;
        H->log(M, SK_INFO, "start choice: player %u (%s) starts as a %s named '%s' (colour %d)", from,
               S->name_of(from).c_str(), diet_name(c.diet), c.name.c_str(), (int)c.color);
    }
    send_table(H->time_seconds(), true);
}

// ---- guest: play our choice (after the answer, and again after a rejoin: the host's table has it)
// Once per answer, per change of our row and per world load (a rejoin, a reload): the player may pick another cell in
// F9 or another colour in the lobby afterwards, and that stays until the next load.
static void guest_apply(double now)
{
    if (const uint32_t loads = world::loads_started(); loads != g_loads_seen) {
        g_loads_seen = loads;
        g_apply_due = g_color_due = true;
    }
    if (!g_mine_known || !enabled() || game::mode_id() != modes::kCell || own_world::elsewhere() || now < g_apply_at)
        return;
    if (g_apply_due) {
        g_apply_due = false;
        creations::Key want = cell_key(g_mine.diet);
        if (!(creations::mine(creations::kCell).key == want)) {
            creations::choose(creations::kCell, want);
            g_cell_applies++;
            H->log(M, SK_INFO, "start choice: our own cell: the %s cell %08X", diet_name(g_mine.diet), want.instance);
        }
    }
    if (g_color_due && g_mine.color >= 0 && !g_color_refused && now >= g_color_at) {
        if (pfind::color_index(S->my_id()) == g_mine.color) {
            g_color_due = false;
            return;
        }
        g_color_at = now + 3; // (the host's table answers a request: asked again until it shows ours, or refused)
        std::string why;
        if (!pfind::request_color(g_mine.color, &why)) {
            g_color_refused = true; // (another player has it: ours stays the one the host gave)
            g_color_due = false;
            H->log(M, SK_INFO, "start choice: our colour %d refused: %s", (int)g_mine.color, why.c_str());
        }
        g_color_requests++;
    }
}

// the meal counters the Cell card is decided from (Cell_GetConsequenceTrait: +0x20 the plant food, +0x24, +0x28;
// docs/re/cell.md#cell-start-choice): a new own cell starts without the host's meals the copy of its world holds
static void zero_meal_counters()
{
    if (uint8_t* g = modes::cell_game(); g && game::mode_id() == modes::kCell && !IsBadReadPtr(g + 0x5190, 4))
        if (uint8_t* d = *(uint8_t**)(g + 0x5190); d && !IsBadWritePtr(d + 0x20, 12)) {
            memset(d + 0x20, 0, 12);
            H->log(M, SK_INFO, "start choice: the meal counters of our new cell start at 0");
        }
}

static void guest_tick(double now)
{
    if (g_zero_due > 0 && now >= g_zero_due) {
        g_zero_due = 0;
        zero_meal_counters();
    }
    if (g_prompt && (!enabled() || game::mode_id() != modes::kCell)) {
        g_prompt = false; // (the setting went off, or the stage is over: nothing to choose)
        H->log(M, SK_INFO, "start choice: the question is gone (setting off or not Cell)");
    }
    guest_apply(now);
}

void tick(double now)
{
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

bool prompt() { return g_prompt && is_guest(); }

std::string default_name()
{
    std::string n = S ? S->name_of(S->my_id()) : std::string();
    return clean_name(n.empty() ? std::string("Cell") : n + "ite");
}

bool answer(int diet, const std::string& name, int color, std::string* why)
{
    Choice c;
    c.diet = (uint8_t)diet;
    c.color = (int8_t)color;
    c.name = clean_name(name);
    auto fail = [&](const char* s) {
        if (why)
            *why = s;
        return false;
    };
    if (!is_guest())
        return fail("only a guest chooses here (the host's start is the game's own wizard)");
    if (diet < 0 || diet >= kDiets)
        return fail("choose a diet");
    if (c.name.empty())
        return fail("type a name for your creature");
    if (!valid(c, kColorCount))
        return fail("not a colour");
    net::Writer w;
    w.u8(kAnswer);
    write_choice(w, c);
    S->send_reliable_to(0, net::MSG_START_CHOICE, w);
    g_answers_tx++;
    const bool first = !g_mine_known;
    g_mine = c;
    g_mine_known = true;
    g_prompt = false;
    g_color_refused = false;
    g_apply_at = g_color_at = 0;
    g_apply_due = g_color_due = true;
    if (first && own_progress::active() && game::mode_id() == modes::kCell) {
        // a new cell of our own starts with its own empty bar (the copy of the host's world holds the host's food)
        cellsync::set_food_total(0);
        cellsync::set_spent_dna(0);
        g_zero_due = H->time_seconds() + 1; // the meal counters after the food step ran (guest_tick)
        g_fresh_starts++;
    }
    guest_apply(H->time_seconds());
    H->log(M, SK_INFO, "start choice: answered: a %s named '%s', colour %d", diet_name(c.diet), c.name.c_str(), (int)c.color);
    return true;
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (is_host()) {
        if (kind == kAnswer && from != S->my_id())
            host_answer(from, r);
        return;
    }
    if (!is_guest() || from != 0)
        return;
    if (kind == kAsk) {
        g_rx_asks++;
        if (!enabled() || game::mode_id() != modes::kCell || g_mine_known)
            return;
        g_prompt = true;
        coop_ui::open(true); // the window opens on its "Your own cell" page (coop_ui.cpp)
        notify::show("Choose your own cell: its diet, name and colour (the co-op window, F9)");
        H->log(M, SK_INFO, "start choice: the host asks for our own start");
        return;
    }
    if (kind == kTable) {
        std::vector<Row> rows;
        if (!read_table(r, rows, kColorCount))
            return;
        g_tables_rx++;
        g_rows.clear();
        for (const auto& row : rows) {
            g_rows[row.player] = row.c;
            if (row.player == S->my_id()) {
                if (!g_mine_known || !(g_mine == row.c)) {
                    g_color_refused = false;
                    g_apply_due = g_color_due = true;
                }
                g_mine = row.c; // (the host's record: the first answer wins)
                g_mine_known = true;
                g_prompt = false;
            }
        }
    }
}

std::string name_of(uint8_t player)
{
    auto it = g_rows.find(player);
    return it == g_rows.end() ? std::string() : it->second.name;
}

uint32_t cell_card(uint64_t id)
{
    if (!id)
        return 0;
    char b[24];
    snprintf(b, sizeof b, "id:%016llX", (unsigned long long)id);
    const Record* r = find_record(g_recs, b);
    return r ? kDietCard[r->c.diet] : 0;
}

std::string status()
{
    std::string s;
    char b[400];
    snprintf(b, sizeof b,
             "start choice: setting=%d enabled=%d role=%s planet=%s records=%zu\n"
             "  host: asks=%u answers=%u kept=%u refused=%u saves=%u save_errors=%u loaded=%u tables=%u asked=%zu\n"
             "  guest: prompt=%d mine=%d diet=%s name='%s' colour=%d asks_rx=%u answers=%u cell_applies=%u colour_requests=%u "
             "colour_refused=%d fresh=%u tables_rx=%u\n",
             (int)settings::on(settings::kStartChoice), (int)enabled(), is_host() ? "host" : is_guest() ? "guest" : "none",
             g_planet.empty() ? "-" : g_planet.substr(g_planet.find_last_of("\\/") + 1).c_str(), g_recs.size(), g_asks,
             g_answers, g_kept, g_refused, g_saves, g_save_errors, g_loaded, g_tables_tx, g_asked.size(), (int)prompt(),
             (int)g_mine_known, g_mine_known ? diet_name(g_mine.diet) : "-", g_mine.name.c_str(), (int)g_mine.color,
             g_rx_asks, g_answers_tx, g_cell_applies, g_color_requests, (int)g_color_refused, g_fresh_starts, g_tables_rx);
    s += b;
    if (uint8_t* g = modes::cell_game(); g && game::mode_id() == modes::kCell && !IsBadReadPtr(g + 0x5190, 4)) {
        uint8_t* d = *(uint8_t**)(g + 0x5190); // cCellSerializableData: the food bar and the counters the Cell card reads
        if (d && !IsBadReadPtr(d + 0x1C, 0x14)) {
            snprintf(b, sizeof b, "  cell data: food=%d +0x20=%d +0x24=%d +0x28=%d spent=%d\n", *(int*)(d + 0x1C), *(int*)(d + 0x20),
                     *(int*)(d + 0x24), *(int*)(d + 0x28), *(int*)(d + 0x2C));
            s += b;
        }
    }
    for (const auto& [p, c] : g_rows) {
        snprintf(b, sizeof b, "  row player=%u diet=%s name='%s' colour=%d\n", p, diet_name(c.diet), c.name.c_str(), (int)c.color);
        s += b;
    }
    for (const auto& r : g_recs) {
        snprintf(b, sizeof b, "  record %s diet=%s name='%s' colour=%d\n", r.key.c_str(), diet_name(r.c.diet), r.c.name.c_str(),
                 (int)r.c.color);
        s += b;
    }
    return s;
}

int command(int argc, const char* const* argv, std::string& out)
{
    const std::string a1 = argc > 1 ? argv[1] : "";
    if (a1 == "pick" && argc >= 4) { // guest: answer without the window: pick <diet> <name> [colour]
        int d = diet_find(argv[2]);
        int col = argc >= 5 ? findx::color_find(argv[4]) : -1;
        std::string why;
        bool ok = answer(d, argv[3], col, &why);
        out = ok ? "answered\n" : "refused: " + why + "\n";
        return ok ? 0 : 1;
    }
    if (a1 == "ask" && argc >= 3) { // host (tests): ask that player again (its record decides, as on a new join)
        g_asked.erase((uint8_t)atoi(argv[2]));
        out = "the player is asked again when it has no choice recorded\n";
        return 0;
    }
    if (a1 == "forget" && argc >= 3) { // host (tests): the player's recorded choice goes (the file too)
        const std::string key = identity::key((uint8_t)atoi(argv[2]));
        size_t n = g_recs.size();
        std::erase_if(g_recs, [&](const Record& r) { return r.key == key; });
        if (g_recs.size() != n)
            g_dirty = true;
        g_asked.erase((uint8_t)atoi(argv[2]));
        out = g_recs.size() != n ? "forgotten\n" : "no record\n";
        return 0;
    }
    out = status();
    return 0;
}

} // namespace start_choice
