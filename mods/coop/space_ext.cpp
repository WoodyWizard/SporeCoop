#include "space_ext.h"
#include "stage_epoch.h"
#include "world_id.h"
#include "store.h"
#include <algorithm>
#include <cstdlib>
#include <map>

#include "coop_ui.h"
#include "fixes.h"
#include "settings.h"
#include "game.h"
#include "modes.h"
#include "rejoin.h"
#include "sporekit_game.hpp"
#include "world.h"

namespace spaceext {

static const SkHost* H;
static SkMod* M;
static const spacecoop::Ctx* C;
static std::vector<bool> g_on; // per feature: init succeeded

std::vector<Feature>& all()
{
    static std::vector<Feature> v; // a function-local static: safe from any static initializer
    return v;
}

bool add(Feature f)
{
    all().push_back(std::move(f));
    return true;
}

const SkHost* host() { return H; }
SkMod* mod() { return M; }
const spacecoop::Ctx& ctx() { return *C; }
bool connected() { return C && C->sess && C->sess->connected(); }
bool is_host() { return connected() && C->sess->role() == net::Role::Host; }
uint8_t my_id() { return C && C->sess ? C->sess->my_id() : 0; }
std::string name_of(uint8_t id) { return C && C->sess ? C->sess->name_of(id) : "?"; }
bool peer_families(uint8_t id, bool& has, std::vector<net::FamilyRev>& out)
{
    has = false;
    out.clear();
    return C && C->sess && C->sess->peer_families(id, has, out);
}
static bool g_kept_token_fix = true;
COOP_FIX("space.kept_token", "BUG-1107", g_kept_token_fix,
         "space: the host gives a player's kept colonies, hold, badges and budget back to the same game (its rejoin token), not to the name");
std::string kept_key(uint8_t id)
{
    if (!C || !C->sess)
        return "";
    if (!g_kept_token_fix) {
        std::string n = C->sess->name_of(id);
        return n == "?" ? "" : n;
    }
    // E1 (BUG-1149): the stable key of the per-player store on the host's disk (a salted digest of the player key), so
    // that what the kept stores read from that store at world load is found under the same key
    std::string k = store::key_of(id);
    return k.empty() ? C->sess->player_key(id) : k;
}
static bool g_away;
bool away() { return g_away; }
void set_away(bool on) { g_away = on; }
bool in_space() { return !g_away && game::mode_id() == modes::kSpace; }

static uintptr_t a_GetPlayerEmpire;
// C2 (BUG-1141): the Space world is world_id.h's (coop.cpp steps it before any message; its rule is this module's of
// 0.7.14, BUG-0228 / BUG-0225: Space entered, another cEmpire (world_anchor), another save file (world_save); editors
// and adventures entered from Space keep it). world_epoch is the serial of the last Space world open (another serial:
// another Space world; 0: Space not seen yet since the mod loaded).
static uint32_t g_space_serial;
static bool space_open() { return world_id::tracker().cur.stage == stage_epoch::index_of(modes::kSpace); }
uint32_t world_epoch()
{
    if (space_open())
        g_space_serial = world_id::tracker().cur.serial;
    return g_space_serial;
}
static bool g_file_fix = true; // BUG-0225: off = the Space world ignores the save file (0.7.3.1)
COOP_FIX("space.world_file", "BUG-0225", g_file_fix,
         "space: another save file in the Space stage is another Space world (not only another cEmpire)");
// the planet file being played, as a crc; 0 for the menu name, snapshot.spo or nothing
static uint32_t planet_file_crc()
{
    std::string n = modes::current_world_name();
    if (n.size() <= 4 || n.compare(n.size() - 4, 4, ".spo") != 0 || n == "snapshot.spo")
        return 0;
    return modes::current_world();
}
uint32_t world_save() { return g_file_fix ? planet_file_crc() : 0; }
const void* world_anchor()
{
    return game::mode_id() == modes::kSpace && a_GetPlayerEmpire ? skg::cdecl_call<void*>(a_GetPlayerEmpire) : nullptr;
}
bool russian() { return coop_ui::russian(); }
static uint32_t g_saves;
static std::vector<void (*)()> g_save_fns;
void on_save(void (*fn)())
{
    if (std::find(g_save_fns.begin(), g_save_fns.end(), fn) == g_save_fns.end())
        g_save_fns.push_back(fn);
}
void note_save()
{
    g_saves++;
    for (auto fn : g_save_fns)
        fn();
}
uint32_t saves() { return g_saves; }

// BUG-0225 (#159): owed refunds go by the Space world and its save file, not by the player's cEmpire
// address and a save counter of every stage. Off: the 0.7.3.0 rule (trade) and the 0.7.3.1 one (spice
// gifts: the same Space world only).
static bool g_owed_id_fix = true;
COOP_FIX("space.owed_id", "BUG-0225", g_owed_id_fix,
         "space: an owed refund is paid only into the game it was taken from (same world, or its file saved after)");
static spacelogic::WorldLedger g_ledger;
static uint32_t g_saves_noted;
static std::string g_key;
static uint32_t g_key_epoch;
bool owed_id() { return g_owed_id_fix; }
std::string game_key()
{
    if (g_key.empty() || g_key_epoch != world_epoch()) {
        g_key = world::current_game_key();
        g_key_epoch = world_epoch();
    }
    return g_key;
}
spacelogic::WorldTag world_tag() { return g_ledger.tag(space_open() ? world_epoch() : 0, space_open() ? game_key() : ""); }
spacelogic::OwedAction owed_action(const spacelogic::WorldTag& t)
{
    return spacelogic::owed_action(g_ledger, t, space_open() ? world_epoch() : 0, space_open() ? game_key() : "");
}
void note_saved()
{
    if (!space_open()) // a save of another stage's game, or between a load and its first Space frame
        return;
    std::string k = world::current_game_key(); // fresh: the save may have named the game just now
    if (!k.empty())
        g_key = k, g_key_epoch = world_epoch();
    g_ledger.note_save(world_epoch(), k);
    if (!k.empty())
        g_saves_noted++;
    if (H)
        H->log(M, SK_INFO, "space: saved world %u (%s)", world_epoch(), k.empty() ? "unknown game" : k.c_str());
}
static void on_slot_replaced(const std::string& dir)
{
    g_ledger.note_replaced(dir);
    if (dir == spacelogic::game_dir(g_key)) // never the slot being played (BUG-0035); just in case
        g_key.clear();
    if (H)
        H->log(M, SK_INFO, "space: slot %s replaced by a received world", dir.c_str());
}

static bool g_rivals;
static std::map<uint16_t, Relation> g_rel; // (low id << 8 | high id) -> relation

bool rivals() { return g_rivals; }
Relation relation(uint8_t a, uint8_t b)
{
    if (!g_rivals || a == b)
        return Relation::Ally;
    auto it = g_rel.find(a < b ? (uint16_t)(a << 8 | b) : (uint16_t)(b << 8 | a));
    return it == g_rel.end() ? Relation::Neutral : it->second;
}
void set_rivals(bool on) { g_rivals = on; }
void set_relation(uint8_t a, uint8_t b, Relation r) { g_rel[a < b ? (uint16_t)(a << 8 | b) : (uint16_t)(b << 8 | a)] = r; }
void clear_relations() { g_rel.clear(); }

void notice(const std::string& text)
{
    if (C && C->notice)
        C->notice(text);
}

void send(uint8_t kind, const net::Writer& body)
{
    if (!connected())
        return;
    net::Writer w;
    w.u8(kind);
    w.u8(C->sess->my_id());
    w.raw(body.data.data(), body.data.size());
    if (C->sess->role() == net::Role::Host)
        C->sess->broadcast_reliable(net::MSG_SPACE, w);
    else
        C->sess->send_reliable_to(0, net::MSG_SPACE, w);
}

void init_all(const SkHost* h, SkMod* m, const spacecoop::Ctx* c)
{
    H = h;
    M = m;
    C = c;
    a_GetPlayerEmpire = h->sym("Simulator::GetPlayerEmpire");
    world::set_slot_replaced_hook(on_slot_replaced);
    auto& v = all();
    g_on.assign(v.size(), false);
    for (size_t i = 0; i < v.size(); i++) {
        g_on[i] = !v[i].init || v[i].init(h, m);
        H->log(M, g_on[i] ? SK_INFO : SK_WARN, "space feature %s: %s", v[i].name.c_str(), g_on[i] ? "on" : "off");
    }
}

// F3 (BUG-0768): the host's session settings switch whole features (space.trade, .contracts, .diplomacy, .allies,
// .finale, .rivals): a feature switched off gets no tick, no messages (they are dropped), no overlay and no page rows
static int setting_of(const std::string& name)
{
    static const std::pair<const char*, settings::Id> m[] = {
        {"trade", settings::kSpaceTrade},         {"contracts", settings::kSpaceContracts},
        {"diplomacy", settings::kSpaceDiplomacy}, {"allies", settings::kSpaceAllies},
        {"finale", settings::kSpaceFinale},       {"rivalry", settings::kSpaceRivals}};
    for (auto& [n, id] : m)
        if (name == n)
            return id;
    return -1;
}
bool feature_allowed(const std::string& name)
{
    int id = setting_of(name);
    return id < 0 || settings::on((settings::Id)id);
}
static bool live(size_t i) { return g_on[i] && feature_allowed(all()[i].name); }

void tick_all(double now)
{
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (live(i) && v[i].tick)
            v[i].tick(now);
        else if (g_on[i] && v[i].tick_off)
            v[i].tick_off(now);
}

bool dispatch(uint8_t kind, uint8_t origin, net::Reader& r)
{
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (v[i].kind_lo && kind >= v[i].kind_lo && kind <= v[i].kind_hi) {
            if (live(i) && v[i].on_message)
                v[i].on_message(kind, origin, r);
            return true;
        }
    return false;
}

void session_end_all()
{
    g_away = false;
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (g_on[i] && v[i].on_session_end)
            v[i].on_session_end();
}

void overlay_all()
{
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (live(i) && v[i].draw_overlay)
            v[i].draw_overlay();
}

std::vector<Row> rows_all()
{
    std::vector<Row> out;
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (live(i) && v[i].rows)
            for (auto& r : v[i].rows())
                out.push_back(std::move(r));
    return out;
}

std::string status_all()
{
    std::string s;
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++) {
        if (!g_on[i]) {
            s += v[i].name + ": off\n";
            continue;
        }
        if (!feature_allowed(v[i].name)) {
            s += v[i].name + ": off (the session setting)\n";
            continue;
        }
        std::string l = v[i].status ? v[i].status() : v[i].name + ": on";
        if (!l.empty() && l.back() != '\n')
            l += '\n';
        s += l;
    }
    return s;
}

bool debug(const std::vector<std::string>& args, std::string& out)
{
    if (args.empty())
        return false;
    if (args[0] == "features") {
        out = status_all();
        return true;
    }
    if (args[0] == "world") { // BUG-0225: the Space world and its save file (tests)
        char b[96];
        snprintf(b, sizeof b, "world=%u open=%d saves_noted=%u game=", world_epoch(), (int)space_open(), g_saves_noted);
        out = b + (space_open() ? game_key() : std::string()) + "\n";
        return true;
    }
    if (args[0] == "keptkey") { // BUG-1107: how the host keys each player's kept state (never the key itself)
        if (args.size() > 2 && args[1] == "other") { // tests: the next joins of this game come as another game
            rejoin::test_other(args[2] == "on" || args[2] == "1");
            out = std::string("next joins with another game's token: ") + (rejoin::test_other_on() ? "on" : "off") + "\n";
            return true;
        }
        out = std::string("kept_token fix=") + (g_kept_token_fix ? "1" : "0") + " other=" + (rejoin::test_other_on() ? "1" : "0") + "\n";
        if (connected())
            for (auto& p : C->sess->players()) {
                std::string k = kept_key(p.id);
                out += "  [" + std::to_string(p.id) + "] " + p.name + ": " +
                       (k.rfind("t:", 0) == 0 ? "token" : k.empty() ? "none" : "name") + "\n";
            }
        return true;
    }
    if (args[0] == "rows") { // tests: what the Space page shows (n: text | button)
        auto rows = rows_all();
        out.clear();
        for (size_t i = 0; i < rows.size(); i++)
            out += std::to_string(i) + ": " + rows[i].text + (rows[i].button.empty() ? "" : " | " + rows[i].button) + "\n";
        if (out.empty())
            out = "no rows\n";
        return true;
    }
    if (args[0] == "press" && args.size() > 1) { // tests: press the button of row n (outside the frame)
        auto rows = rows_all();
        size_t n = (size_t)atoi(args[1].c_str());
        if (n >= rows.size() || !rows[n].on_click) {
            out = "no button in row " + args[1] + "\n";
            return true;
        }
        rows[n].on_click();
        out = "pressed '" + rows[n].button + "'\n";
        return true;
    }
    auto& v = all();
    for (size_t i = 0; i < v.size() && i < g_on.size(); i++)
        if (v[i].name == args[0]) {
            if (!g_on[i])
                out = v[i].name + ": off (see sporekit.log: a symbol or a hook is missing)\n";
            else if (v[i].debug)
                out = v[i].debug(std::vector<std::string>(args.begin() + 1, args.end()));
            else
                out = v[i].status ? v[i].status() : v[i].name + ": no commands\n";
            return true;
        }
    return false;
}

} // namespace spaceext
