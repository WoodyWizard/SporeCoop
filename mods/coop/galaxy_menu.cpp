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

#include <cstring>
#include <string>
#include <vector>

#include "galaxy_client.h"
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

// the Play step (thiscall, one stack argument): step +0xC the picked entry, +0x8 a countdown (the action at 0)
static void __fastcall h_Step(void* self, void* edx, void* arg)
{
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    uint8_t* e = self ? *(uint8_t**)((uint8_t*)self + 0xC) : nullptr;
    if (active() && e && gge && *(int*)((uint8_t*)self + 8) <= 0 && !gge[0x1F0] && !gge[0x140] && e[0xAC]) {
        const char16_t* b = *(const char16_t* const*)e;
        const char16_t* en = *(const char16_t* const*)(e + 4);
        const std::string planet = b && en > b ? utf8(b, en) : "";
        for (auto& row : rows())
            if ((g_one_row ? row.shown.name : net::galaxy::era_display(row.shown.name)) == planet) {
                const std::string key = era_to_enter(row);
                const galaxy::PlanetInfo p{key};
                // the step comes again every frame until the menu is left: once, and never while on that planet (each
                // ENTER of the planet it is on made the client leave and join again, 2026-11-17)
                const double now = H->time_seconds();
                if (galaxy::current_planet() != p.name && (planet != g_play_name || now - g_play_at > 30)) {
                    g_play_name = planet;
                    g_play_at = now;
                    g_plays++;
                    H->log(M, SK_INFO, "galaxy menu: Play on the server planet '%s': entering it (5X B)", planet.c_str());
                    galaxy::enter(p.name);
                }
                return; // (no local file: the galaxy client loads it)
            }
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
    char b[300];
    snprintf(b, sizeof b, "galaxy menu (5X B): %s, hooks %s, active %d, rewrites %u, plays %u, rebuilds %u, last: %s",
             g_on ? "on" : "off", g_hooks ? "on" : "missing", (int)active(), g_rewrites, g_plays, g_rebuilds,
             g_last.empty() ? "-" : g_last.c_str());
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
