// Decision 5X B (docs/12-server-and-players.md#server-galaxy-menu-spike, alpha): while the player is connected to a
// galaxy server, Spore's own galaxy menu shows the server's planets. The menu fills its entries from the slot's saved
// games (SporeCoop::GGE_FillSaved) and empty stars, then makes their panels (SporeCoop::GGE_MakePanels): this module
// rewrites the entries just before the panels are made (name, creature, tech level, "a save"), the rest of the menu's
// stars become empty ones, and Play on a server planet (the Play step SporeCoop::GGE_PlayStep, before its check of the
// save file) enters it on the server instead of loading a file (the galaxy client then downloads and hosts it, or joins
// its host).
// docs/re/space.md#space-menu-entries.
#include "galaxy_menu.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "coop_ui.h"
#include "galaxy_client.h"
#include "notify.h"
#include "net/galaxy.h"
#include "game.h"
#include "modes.h"
#include "fixes.h"
#include "sporekit_game.hpp"

namespace galaxy_menu {

static const SkHost* H;
static SkMod* M;
static uintptr_t a_WCopy, a_StarGet, a_ReadStars;
using PanelsFn = void(__fastcall*)(void*, void*);
using StepFn = void(__fastcall*)(void*, void*, void*);
static PanelsFn o_Panels;
static StepFn o_Step;
static bool g_hooks; // both hooks are in (else the menu is the game's own)
static uintptr_t a_Gge;
static std::string g_play_name;
static double g_play_at = -100;
static bool g_on = true;
static bool g_last_online;
static uint32_t g_last_version;
static double g_rebuild_at;
static unsigned g_rewrites, g_plays, g_rebuilds;
static std::string g_last;
// BUG-9295: one row per planet (its eras are one planet to the player); Play enters the era this player last played
// there (coop.ini [galaxy_eras] "<galaxy>|<planet>"), else the planet's earliest era (the 5R stage choice is not built)
static bool g_one_row = true;
COOP_FIX("galaxy.menu_one_row", "BUG-9295", g_one_row, "galaxy menu: one row per planet, not one per era");
struct Row {
    galaxy::PlanetInfo shown;       // the row: the planet's name, all its eras' players, its latest era's icon
    std::vector<std::string> eras;  // its eras' keys
};
static std::vector<Row> rows()
{
    std::vector<Row> v;
    for (auto& p : galaxy::planets_known()) { // (players first: a planet's row where its busiest era is)
        const std::string planet = g_one_row ? net::galaxy::era_planet(p.name) : p.name;
        Row* row = nullptr;
        for (auto& x : v)
            if (x.shown.name == planet)
                row = &x;
        if (!row) {
            v.push_back({p, {}});
            row = &v.back();
            row->shown.name = planet;
            row->shown.players = 0;
        }
        row->eras.push_back(p.name);
        row->shown.players += p.players;
        if (p.tech > row->shown.tech) { // the latest era's icon and creature
            row->shown.tech = p.tech;
            memcpy(row->shown.species, p.species, sizeof p.species);
        }
    }
    return v;
}
static std::string remember_key(const std::string& planet) { return galaxy::galaxy_name() + "|" + planet; }
static std::string era_to_enter(const Row& row)
{
    if (row.eras.size() == 1)
        return row.eras[0];
    char b[128] = {};
    H->config_str(M, "galaxy_eras", remember_key(row.shown.name).c_str(), "", b, sizeof b);
    for (auto& e : row.eras)
        if (e == b)
            return e;
    std::string first = row.eras[0];
    for (auto& e : row.eras)
        if (net::galaxy::era_of(e) < net::galaxy::era_of(first))
            first = e;
    return first;
}
static std::string g_remembered;

// BUG-9335 (decision 5R A, "players progress on their own, a later stage can be started when the player reached it in
// their own Spore"): Play on a planet of several eras where this player has played none (nothing remembered) opens a
// stage choice instead of entering the earliest era: one button per era of the planet (its stage and players), the
// stages this player has not reached in its own Spore grey (a press says so, nothing entered). The choice is
// remembered as an era played (BUG-9295). Off: the earliest era, as before.
// BUG-9336: the choice also offers the stage the planet has no era of yet, the next one up from its stored Creature
// world ("Tribe (new)", grey when not reached): a press starts it (galaxy::start_era). Only Creature -> Tribe: the
// game's own stage end runs there (the Tribe stage's end has no such request yet).
static bool g_choice = true;
COOP_FIX("galaxy.stage_choice", "BUG-9335", g_choice, "galaxy menu: a player new to a planet chooses among its stages (the ones it reached), not the earliest");
static std::vector<std::string> g_pick_eras; // the open choice's eras (the menu's item codes index it)
static std::vector<std::string> g_pick_from; // BUG-9336: per item, the era a new stage starts from ("" an existing era)
static unsigned g_choices, g_picks, g_refusals, g_starts;
static std::string g_last_choice;
static bool has_remembered(const Row& row)
{
    char b[128] = {};
    H->config_str(M, "galaxy_eras", remember_key(row.shown.name).c_str(), "", b, sizeof b);
    for (auto& e : row.eras)
        if (e == b)
            return true;
    return false;
}
// Spore's own rule for starting a stage (docs/re/space.md#space-stage-unlocks): each start stage but Cell is a hidden
// achievement; the new game's stage chooser enables a stage when the menu's cheat byte (+0x1F5, `cheat levels -unlock`)
// is set or GGE_IsStageUnlocked(menu, key) (earned, or pending in the menu's set, as the Galactic Adventures Space start).
// Out of the menu (no menu object): the earned set alone (AchievementsManager_HasEarned).
static uintptr_t a_IsUnlocked, a_AchGet, a_HasEarned;
static uint32_t stage_key(uint8_t era)
{
    switch (era) {
    case net::galaxy::kEraCreature: return 0x26B05664;
    case net::galaxy::kEraTribe: return 0x24947C07;
    case net::galaxy::kEraCiv: return 0x2AB05C28;
    case net::galaxy::kEraSpace: return 0x2887B6DE;
    default: return 0;
    }
}
bool stage_reached(uint8_t era)
{
    const uint32_t key = stage_key(era);
    if (!key)
        return true; // Cell (and no era): always open
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    if (gge && a_IsUnlocked)
        return gge[0x1F5] || skg::thiscall<bool>(a_IsUnlocked, (void*)gge, key);
    if (a_AchGet && a_HasEarned)
        if (void* mgr = skg::cdecl_call<void*>(a_AchGet))
            return skg::thiscall<bool>(a_HasEarned, mgr, key);
    return true; // (no reader: not refused)
}
// BUG-9336: the era a new Tribe stage of this planet starts from (its stored Creature era), "" none (it has a Tribe era,
// no stored Creature world, or the switch is off)
static std::string start_from(const Row& row)
{
    if (!galaxy::stage_start_on())
        return "";
    for (auto& e : row.eras)
        if (net::galaxy::era_of(e) == net::galaxy::kEraTribe)
            return "";
    const std::string from = net::galaxy::era_key(row.shown.name, net::galaxy::kEraCreature);
    for (auto& p : galaxy::planets_known())
        if (p.name == from && p.stored)
            return from;
    return "";
}
static void on_stage_pick(int code)
{
    if (code < 0 || code >= (int)g_pick_eras.size())
        return;
    if (code < (int)g_pick_from.size() && !g_pick_from[code].empty()) { // BUG-9336: a new stage of the planet
        const uint8_t era = net::galaxy::era_of(g_pick_eras[code]);
        if (!stage_reached(era)) {
            g_refusals++;
            notify::show(std::string("You have not reached ") + net::galaxy::era_name(era) + " in your own Spore yet", "info");
            return;
        }
        g_starts++;
        g_play_at = H->time_seconds();
        H->log(M, SK_INFO, "galaxy menu: stage choice: a new %s stage from '%s' (BUG-9336)", net::galaxy::era_name(era),
               g_pick_from[code].c_str());
        notify::show(galaxy::start_era(g_pick_from[code], era), "info");
        return;
    }
    const std::string key = g_pick_eras[code];
    const uint8_t era = net::galaxy::era_of(key);
    if (!stage_reached(era)) {
        g_refusals++;
        notify::show(std::string("You have not reached ") + net::galaxy::era_name(era) + " in your own Spore yet", "info");
        return;
    }
    g_picks++;
    g_play_at = H->time_seconds();
    H->config_set_str(M, "galaxy_eras", remember_key(net::galaxy::era_planet(key)).c_str(), key.c_str());
    H->log(M, SK_INFO, "galaxy menu: stage choice '%s' (BUG-9335)", key.c_str());
    galaxy::enter(key);
}
static void open_choice(const Row& row)
{
    std::vector<std::string> eras = row.eras;
    std::sort(eras.begin(), eras.end(), [](const std::string& a, const std::string& b) {
        return net::galaxy::era_of(a) < net::galaxy::era_of(b);
    });
    std::vector<coop_ui::MenuItem> items;
    g_pick_eras.clear();
    g_pick_from.clear();
    g_last_choice.clear();
    for (auto& e : eras) {
        if (items.size() >= 6) // (the menu's rows)
            break;
        const uint8_t era = net::galaxy::era_of(e);
        int players = 0;
        for (auto& p : galaxy::planets_known())
            if (p.name == e)
                players = p.players;
        std::string t = net::galaxy::era_name(era);
        if (players)
            t += "  (" + std::to_string(players) + (players == 1 ? " player)" : " players)");
        const bool open = stage_reached(era);
        items.push_back({t, (int)g_pick_eras.size(), open ? coop_ui::kMenuBlue : coop_ui::kMenuGrey});
        g_pick_eras.push_back(e);
        g_pick_from.push_back("");
        g_last_choice += std::string(g_last_choice.empty() ? "" : ", ") + net::galaxy::era_name(era) + (open ? "" : " (grey)");
    }
    const std::string from = start_from(row); // BUG-9336
    if (!from.empty() && items.size() < 6) {
        const uint8_t era = net::galaxy::kEraTribe;
        const bool open = stage_reached(era);
        items.push_back({std::string(net::galaxy::era_name(era)) + "  (new)", (int)g_pick_eras.size(),
                         open ? coop_ui::kMenuBlue : coop_ui::kMenuGrey});
        g_pick_eras.push_back(net::galaxy::era_key(row.shown.name, era));
        g_pick_from.push_back(from);
        g_last_choice += std::string(", ") + net::galaxy::era_name(era) + " (new" + (open ? ")" : ", grey)");
    }
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    g_choices++;
    H->log(M, SK_INFO, "galaxy menu: stage choice on '%s': %s (BUG-9335)", row.shown.name.c_str(), g_last_choice.c_str());
    coop_ui::show_player_menu(sw * 0.5f - 112, sh * 0.3f, "Choose a stage", items, on_stage_pick);
}

static bool active() { return g_hooks && g_on && galaxy::online() && game::mode_id() == modes::kMenu; }

static std::u16string u16(const std::string& s)
{
    std::wstring w(s.size() + 1, L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], (int)w.size());
    std::u16string out;
    for (int i = 0; i < n; i++)
        out += (char16_t)w[i];
    return out;
}

static std::string utf8(const char16_t* b, const char16_t* e)
{
    std::wstring w(b, e);
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static void set_name(uint8_t* str, const std::u16string& v) // an eastl::wstring of the game (its allocator)
{
    skg::thiscall<void>(a_WCopy, (void*)str, (const void*)v.data(), (const void*)(v.data() + v.size()));
}

static void rewrite(void* set)
{
    uint8_t* begin = *(uint8_t**)((uint8_t*)set + 8);
    uint8_t* end = *(uint8_t**)((uint8_t*)set + 0xC);
    if (!begin || end <= begin)
        return;
    const size_t n = (size_t)(end - begin) / 0xBC;
    std::vector<Row> rs = rows();
    std::vector<galaxy::PlanetInfo> ps;
    for (auto& x : rs)
        ps.push_back(x.shown);
    size_t shown = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t* e = begin + i * 0xBC;
        uint32_t* key = (uint32_t*)(e + 0x3C);
        if (i < ps.size()) {
            const galaxy::PlanetInfo& p = ps[i];
            set_name(e, u16(g_one_row ? p.name : net::galaxy::era_display(p.name))); // (BUG-9295: the planet; before, "Samuth (Tribe)")
            void* prof = p.species[0] ? game::species_by_key({p.species[0], p.species[1], p.species[2]}) : nullptr;
            key[0] = prof ? p.species[0] : 0;
            key[1] = prof ? p.species[1] : 0;
            key[2] = prof ? p.species[2] : 0;
            *(void**)(e + 0x48) = prof;
            *(uint32_t*)(e + 0x8C) = 0;      // the save is there (the server has it)
            *(uint32_t*)(e + 0xA8) = p.tech; // the stage icon
            e[0xAC] = 1;
            e[0xAD] = 1;
            shown++;
        } else if (e[0xAC]) { // one of the player's own saves: an empty star while online
            set_name(e, u"");
            key[0] = key[1] = key[2] = 0;
            *(void**)(e + 0x48) = nullptr;
            *(uint32_t*)(e + 0xA8) = 5;
            e[0xAC] = 0;
        }
    }
    g_rewrites++;
    g_last = std::to_string(shown) + " of " + std::to_string(ps.size()) + " server planet(s) in " + std::to_string(n) + " entries";
    H->log(M, SK_INFO, "galaxy menu: %s (5X B)", g_last.c_str());
}

static void __fastcall h_Panels(void* set, void* edx)
{
    if (active() && set)
        rewrite(set);
    o_Panels(set, edx);
}

// Play on the menu's planet `planet` (its shown name): true when it is a server planet (entered, or its stage choice
// opened). The step comes again every frame until the menu is left: once, and never while on that planet (each ENTER of
// the planet it is on made the client leave and join again, 2026-11-17)
static bool play(const std::string& planet)
{
    for (auto& row : rows())
        if ((g_one_row ? row.shown.name : net::galaxy::era_display(row.shown.name)) == planet) {
            const std::string key = era_to_enter(row);
            const double now = H->time_seconds();
            if (galaxy::current_planet() != key && (planet != g_play_name || now - g_play_at > 30)) {
                g_play_name = planet;
                g_play_at = now;
                // BUG-9335: several eras; BUG-9336: or a new stage this player may start there
                if (g_choice && !has_remembered(row) &&
                    (row.eras.size() > 1 || (!start_from(row).empty() && stage_reached(net::galaxy::kEraTribe)))) {
                    open_choice(row);
                    return true;
                }
                g_plays++;
                H->log(M, SK_INFO, "galaxy menu: Play on the server planet '%s': entering it (5X B)", planet.c_str());
                galaxy::enter(key);
            }
            return true;
        }
    return false;
}

// the Play step (thiscall, one stack argument): step +0xC the picked entry, +0x8 a countdown (the action at 0)
static void __fastcall h_Step(void* self, void* edx, void* arg)
{
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    uint8_t* e = self ? *(uint8_t**)((uint8_t*)self + 0xC) : nullptr;
    if (active() && e && gge && *(int*)((uint8_t*)self + 8) <= 0 && !gge[0x1F0] && !gge[0x140] && e[0xAC]) {
        const char16_t* b = *(const char16_t* const*)e;
        const char16_t* en = *(const char16_t* const*)(e + 4);
        const std::string planet = b && en > b ? utf8(b, en) : "";
        if (play(planet))
            return; // (no local file: the galaxy client loads it)
    }
    o_Step(self, edx, arg);
}

// the menu reads its stars again (cStarManager::ReadStarsFromSaveDB sends the message that rebuilds the entries)
static void rebuild(void*)
{
    if (game::mode_id() != modes::kMenu || !a_StarGet || !a_ReadStars)
        return;
    if (void* sm = skg::cdecl_call<void*>(a_StarGet)) {
        skg::thiscall<void>(a_ReadStars, sm);
        g_rebuilds++;
    }
}

void tick(double now)
{
    { // BUG-9295: the era this player plays on a planet, for the menu's next Play there
        const std::string cur = galaxy::current_planet();
        if (!cur.empty() && cur != g_remembered && !net::galaxy::is_crossing(cur) && net::galaxy::era_of(cur)) {
            g_remembered = cur;
            H->config_set_str(M, "galaxy_eras", remember_key(net::galaxy::era_planet(cur)).c_str(), cur.c_str());
        }
    }
    if (!g_hooks || game::mode_id() != modes::kMenu)
        return;
    const bool online = g_on && galaxy::online();
    const uint32_t ver = galaxy::planets_version();
    if ((online != g_last_online || (online && ver != g_last_version)) && now >= g_rebuild_at) {
        g_last_online = online;
        g_last_version = ver;
        g_rebuild_at = now + 2;
        H->run_outside_frame(rebuild, nullptr);
    }
}

std::string command(int argc, const char* const* argv)
{
    const std::string sub = argc > 1 ? argv[1] : "status";
    if (sub == "on" || sub == "off") {
        g_on = sub == "on";
        g_last_version = ~0u;
    } else if (sub == "rebuild")
        H->run_outside_frame(rebuild, nullptr);
    if (sub == "forget" && argc > 2) // tests (BUG-9335): this player has played no era of planet argv[2]
        H->config_set_str(M, "galaxy_eras", remember_key(argv[2]).c_str(), "");
    if (sub == "pick" && argc > 2) // tests (BUG-9335): as a press on the open choice's button n
        on_stage_pick(atoi(argv[2]));
    if (sub == "play" && argc > 2) { // tests (BUG-9335): the menu's Play on planet argv[2] (its code path, no click)
        if (!active())
            return "not in the galaxy menu while online (Play is the menu's)";
        g_play_at = -100;
        if (!play(argv[2]))
            return std::string("no server planet '") + argv[2] + "' in the menu";
    }
    char b[600];
    snprintf(b, sizeof b, "galaxy menu (5X B): %s, hooks %s, active %d, rewrites %u, plays %u, rebuilds %u, last: %s; "
             "stage choice (BUG-9335): opened %u, entered %u, refused %u, new stages started %u (BUG-9336), last: %s; reached: %d%d%d%d%d",
             g_on ? "on" : "off", g_hooks ? "on" : "missing", (int)active(), g_rewrites, g_plays, g_rebuilds,
             g_last.empty() ? "-" : g_last.c_str(), g_choices, g_picks, g_refusals, g_starts,
             g_last_choice.empty() ? "-" : g_last_choice.c_str(), (int)stage_reached(1), (int)stage_reached(2),
             (int)stage_reached(3), (int)stage_reached(4), (int)stage_reached(5));
    return b;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    a_WCopy = H->sym("wstring_copy");
    a_StarGet = H->sym("Simulator::cStarManager::Get");
    a_ReadStars = H->sym("Simulator::cStarManager::ReadStarsFromSaveDB");
    a_Gge = H->sym("SporeCoop::GalaxyGameEntry_ptr");
    a_IsUnlocked = H->sym("SporeCoop::GGE_IsStageUnlocked"); // BUG-9335
    a_AchGet = H->sym("Pollinator::cAchievementsManager::Get");
    a_HasEarned = H->sym("SporeCoop::AchievementsManager_HasEarned");
    uintptr_t panels = H->sym("SporeCoop::GGE_MakePanels"), step = H->sym("SporeCoop::GGE_PlayStep");
    const bool ok = a_WCopy && a_Gge && panels && step &&
                    H->hook_create(M, (void*)panels, (void*)&h_Panels, (void**)&o_Panels) == 0 &&
                    H->hook_create(M, (void*)step, (void*)&h_Step, (void**)&o_Step) == 0;
    // (2026-11-18 review: the originals stay as hook_create left them. Nulling both when only the second hook failed
    // left the first hook installed and calling a null original; the feature is off through g_hooks instead)
    g_hooks = ok;
    H->log(M, SK_INFO, "galaxy menu: hooks %s (5X B)", ok ? "on" : "off");
}

} // namespace galaxy_menu
