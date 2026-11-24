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
#include "creations.h"
#include "galaxy_crt.h"
#include "identity.h"
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
            row->shown.owner = p.owner;
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
// the stage a player new to the planet may start there (BUG-9336, BUG-9343): the one after the planet's latest era, from
// that era's stored world. Creature -> Tribe; Tribe -> Civ (BUG-9343, switch galaxy.civ_start). era 0: none
struct NewStage {
    std::string from;
    uint8_t era = 0;
};
static bool g_civ_start = true;
COOP_FIX("galaxy.civ_start", "BUG-9343", g_civ_start, "galaxy menu: a planet whose latest stage is Tribe offers its Civ stage (new)");
static bool g_space_start = true;
COOP_FIX("galaxy.space_start", "BUG-9346", g_space_start, "galaxy menu: a planet whose latest stage is Civ offers its Space stage (new)");
static NewStage new_stage(const Row& row)
{
    NewStage n;
    if (!galaxy::stage_start_on())
        return n;
    uint8_t top = 0;
    std::string top_key;
    for (auto& e : row.eras)
        if (net::galaxy::era_of(e) > top && !net::galaxy::is_crossing(e))
            top = net::galaxy::era_of(e), top_key = e;
    if (top != net::galaxy::kEraCreature && !(g_civ_start && top == net::galaxy::kEraTribe) &&
        !(g_space_start && top == net::galaxy::kEraCiv))
        return n;
    for (auto& p : galaxy::planets_known())
        if (p.name == top_key && p.stored)
            n.from = top_key, n.era = (uint8_t)(top + 1);
    return n;
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
    const NewStage ns = new_stage(row); // BUG-9336, BUG-9343
    const std::string& from = ns.from;
    if (ns.era && items.size() < 6) {
        const uint8_t era = ns.era;
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

// BUG-9337 (decision 8L A): the stage choice is Spore's own Choose A Stage screen (the new game's screen with the paw,
// fire, globe and rocket, docs/re/space.md#space-native-stage-choice), opened for the server planet's menu entry the
// way the menu opens it for an empty star (the new-game state 4's entry, then the menu's state switch). Its stage
// buttons are enabled for the planet's eras and the new stage this player may start (and reached in its own Spore);
// a press closes the screen as its Back does and enters or starts that stage on the server (on_stage_pick), never a
// new game. Off: our "Choose a stage" box (BUG-9335).
static bool g_native = true;
COOP_FIX("galaxy.native_stage_choice", "BUG-9337", g_native, "galaxy menu: the stage choice is Spore's own Choose A Stage screen (off: our box)");
typedef void(__fastcall* FlowOpenFn)(void* flow, void* edx, void* entry);
typedef void(__fastcall* FlowButtonFn)(void* flow, void* edx, uint32_t id);
static FlowOpenFn o_FlowOpen;
static FlowButtonFn o_FlowButton;
static bool g_flow_hooks;
static uintptr_t a_NewGameState, a_SetState, a_FindWin, a_PlanetSet, a_PlayState;
typedef void(__fastcall* SetStateFn)(void* gge, void* edx, int state, int push);
static SetStateFn o_SetState;
static const uint32_t kStageButtons[5] = {0x053F06E1, 0x053F06EE, 0x053F06F5, 0x053F06FC, 0x053F0707}; // Cell..Space
static const uint32_t kFlowBack = 0x0595E0A8;
static void* g_native_entry;       // the server planet's entry the screen is open for (nullptr: none of ours)
static double g_native_at;
static int g_native_codes[6];      // per era (1..5): the index into g_pick_eras, -1 not offered
static unsigned g_native_opens, g_native_picks, g_native_closes;
static std::string g_native_last;
static std::string g_native_planet; // 8M A: the planet the screen is open for
static void (*g_gone_fn)();         // 8M A: the planet details panel goes with the screen
// BUG-9350: a load that leaves the menu while the new-game screen (state 4: Choose A Stage, ours or an empty star's)
// is open (a rejoin, a party move, an enter) left the screen drawn over the planet, and its X there crashed the game
// (its Back runs the menu's code outside the menu). The mod's load from the menu waits: the screen's Back is pressed
// first (the path a stage press uses, outside the frame), the load follows once the menu has left state 4.
static bool g_close_on_leave = true;
COOP_FIX("galaxy.stage_screen_close", "BUG-9350", g_close_on_leave,
         "galaxy menu: Spore's Choose A Stage screen is closed before a load from the menu");
static double g_close_first_at, g_close_posted_at;
static unsigned g_left_closes, g_left_timeouts;

// switch_now: the menu's state switch here (tests); else the caller switches (h_SetState: Play's switch made state 4)
static bool open_native(const Row& row, uint8_t* entry, bool switch_now)
{
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    uint8_t* st = a_NewGameState ? *(uint8_t**)a_NewGameState : nullptr;
    if (!g_native || !g_flow_hooks || !gge || !st || !a_SetState || !entry)
        return false;
    g_pick_eras.clear();
    g_pick_from.clear();
    g_last_choice.clear();
    for (int& c : g_native_codes)
        c = -1;
    for (auto& e : row.eras) {
        const uint8_t era = net::galaxy::era_of(e);
        if (era < 1 || era > net::galaxy::kEraLast || g_native_codes[era] >= 0)
            continue;
        g_native_codes[era] = (int)g_pick_eras.size();
        g_pick_eras.push_back(e);
        g_pick_from.push_back("");
        g_last_choice += std::string(g_last_choice.empty() ? "" : ", ") + net::galaxy::era_name(era) +
                         (stage_reached(era) ? "" : " (grey)");
    }
    const NewStage ns = new_stage(row); // BUG-9336: the new Tribe stage; BUG-9343: the new Civ stage
    const std::string& from = ns.from;
    if (ns.era && g_native_codes[ns.era] < 0) {
        const uint8_t era = ns.era;
        g_native_codes[era] = (int)g_pick_eras.size();
        g_pick_eras.push_back(net::galaxy::era_key(row.shown.name, era));
        g_pick_from.push_back(from);
        g_last_choice += std::string(", ") + net::galaxy::era_name(era) + " (new" + (stage_reached(era) ? ")" : ", grey)");
    }
    *(void**)(st + 0xC) = entry; // (what the menu's click on an empty star sets, 0xDF7960)
    g_native_entry = entry;
    g_native_planet = row.shown.name;
    g_native_at = H->time_seconds();
    if (switch_now)
        skg::thiscall<void>(a_SetState, (void*)gge, 4, 0);
    g_choices++;
    g_native_opens++;
    g_native_last = row.shown.name + ": " + g_last_choice;
    H->log(M, SK_INFO, "galaxy menu: Spore's Choose A Stage screen on '%s': %s (BUG-9337)", row.shown.name.c_str(),
           g_last_choice.c_str());
    return true;
}

// the screen's stage buttons: enabled for the stages offered and reached (window vt 0x7C(2, on): enabled)
static void native_buttons(void* flow)
{
    if (!a_FindWin)
        return;
    for (int i = 0; i < 5; i++) {
        const uint8_t era = (uint8_t)(i + 1);
        const bool on = g_native_codes[era] >= 0 && stage_reached(era);
        if (void* w = skg::thiscall<void*>(a_FindWin, (uint8_t*)flow + 0x28, kStageButtons[i], 1))
            skg::vcall<void>(w, 0x7C / 4, 2, (int)on);
    }
}

static void __fastcall h_FlowOpen(void* flow, void* edx, void* entry)
{
    o_FlowOpen(flow, edx, entry);
    if (g_native_entry && entry == g_native_entry)
        native_buttons(flow);
    else
        g_native_entry = nullptr; // (a new game on an empty star: the game's own screen as it is)
}

static void native_closed()
{
    g_native_entry = nullptr;
    if (g_gone_fn) g_gone_fn();
    g_last_version = ~0u; // the next tick rebuilds the entries (the screen wrote into this one)
}

static void __fastcall h_FlowButton(void* flow, void* edx, uint32_t id)
{
    if (g_native_entry && flow && *(void**)((uint8_t*)flow + 0x628) == g_native_entry) {
        for (int i = 0; i < 5; i++)
            if (id == kStageButtons[i]) {
                const int code = g_native_codes[i + 1];
                if (code < 0 || !stage_reached((uint8_t)(i + 1)))
                    return; // (not offered: the button is disabled)
                o_FlowButton(flow, edx, kFlowBack); // closed as its Back does (the menu's planets again)
                native_closed();
                g_native_picks++;
                H->log(M, SK_INFO, "galaxy menu: Choose A Stage: %s pressed (BUG-9337)", net::galaxy::era_name((uint8_t)(i + 1)));
                on_stage_pick(code);
                return;
            }
        if (id == kFlowBack) {
            g_native_closes++;
            native_closed();
        }
    }
    o_FlowButton(flow, edx, id);
}

// BUG-9350: the menu shows the new-game screen (state 4) now; its flow, else nullptr
static void* open_flow()
{
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    uint8_t* st = a_NewGameState ? *(uint8_t**)a_NewGameState : nullptr;
    if (!g_flow_hooks || !o_FlowButton || !gge || !st || game::mode_id() != modes::kMenu || *(int*)(gge + 0x108) != 4)
        return nullptr;
    return *(void**)(st + 0x8);
}

// BUG-9350 (outside the frame, in the menu only): the screen's Back, as a stage press closes it
static void close_for_load(void*)
{
    void* flow = open_flow();
    if (!flow)
        return;
    o_FlowButton(flow, nullptr, kFlowBack);
    if (g_native_entry) {
        g_native_closes++;
        native_closed();
    }
    g_left_closes++;
    H->log(M, SK_INFO, "galaxy menu: Choose A Stage closed before a load from the menu (BUG-9350)");
}

bool stage_screen_holds_load(double now)
{
    if (!g_close_on_leave || !open_flow()) {
        g_close_first_at = 0;
        return false;
    }
    if (!g_close_first_at)
        g_close_first_at = now;
    if (now - g_close_first_at > 5) { // (the screen did not close: load as before)
        if (g_close_first_at > 0) {
            g_left_timeouts++;
            H->log(M, SK_WARN, "galaxy menu: Choose A Stage still open 5 s after its Back: loading anyway (BUG-9350)");
        }
        g_close_first_at = -1e9; // (once per open screen)
        return false;
    }
    if (now - g_close_posted_at > 1) {
        g_close_posted_at = now;
        H->run_outside_frame(close_for_load, nullptr);
    }
    return true;
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

// 5Z B (the menu's gaps before the owner's own test): a planet's creature this game lacks comes from the galaxy's
// creation store (the server lists whose upload it is; galaxy_client puts the host's creature there, switch
// `galaxy.menu_pictures`); the menu is built again once it is here.
// BUG-9355: off: as before (only creatures this game has are shown)
static bool g_pictures_on = true;
COOP_FIX("galaxy.menu_pictures", "BUG-9355", g_pictures_on, "galaxy menu: a planet's creature this game lacks comes from the galaxy's store");
static bool g_pictures_waiting;      // a row's creature was asked from the store and is not here yet
static double g_pictures_next;
static unsigned g_pictures_store;    // rows of the last build whose creature came from the store
static unsigned g_here_built;        // the planets' creatures here from the store at the last build
static void* picture(const galaxy::PlanetInfo& p, uint32_t out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (!p.species[0])
        return nullptr;
    // another player's creature always comes from the store: creation ids count up per profile, so the same key here
    // can be a creature of this player's own (BUG-9290); the store installs it under a key that is free here
    const bool store = p.owner && g_pictures_on && p.owner != identity::own();
    uint32_t lk[3] = {p.species[0], p.species[1], p.species[2]};
    if (store && !galaxy_crt::local_key(p.owner, p.species, lk)) {
        g_pictures_waiting = true; // (asked from the store; the tick builds the menu again once it is here)
        return nullptr;
    }
    if (store && !galaxy_crt::here(p.owner, p.species))
        return nullptr; // (the store has none: "?", never a creature of this player's under the same key)
    void* prof = game::species_by_key({lk[0], lk[1], lk[2]});
    if (prof) {
        memcpy(out, lk, sizeof lk);
        if (store && galaxy_crt::here(p.owner, p.species))
            g_pictures_store++;
    }
    return prof;
}
// 5Z B: more planets than the menu's stars (8 with an empty slot): the last star turns the page ("Next planets"); its
// Play shows the next page instead of entering anything. Off: the planets past the stars are not shown (as before)
static bool g_pages_on = true;
COOP_FIX("galaxy.menu_pages", "BUG-9356", g_pages_on, "galaxy menu: more planets than stars: the last star turns the page");
static size_t g_page, g_pages = 1;
static unsigned g_page_turns;
static std::string g_more_name; // the page star's name ("" none)

static void rewrite(void* set)
{
    uint8_t* begin = *(uint8_t**)((uint8_t*)set + 8);
    uint8_t* end = *(uint8_t**)((uint8_t*)set + 0xC);
    if (!begin || end <= begin)
        return;
    const size_t n = (size_t)(end - begin) / 0xBC;
    std::vector<Row> rs = rows();
    const size_t all = rs.size();
    g_more_name.clear();
    g_pages = 1;
    if (g_pages_on && rs.size() > n && n >= 2) {
        const size_t per = n - 1;
        g_pages = (rs.size() + per - 1) / per;
        if (g_page >= g_pages)
            g_page = 0;
        const size_t from = g_page * per, to = std::min(rs.size(), from + per);
        rs = std::vector<Row>(rs.begin() + from, rs.begin() + to);
        g_more_name = "More planets (" + std::to_string((g_page + 1) % g_pages + 1) + "/" + std::to_string(g_pages) + ")";
    } else
        g_page = 0;
    std::vector<galaxy::PlanetInfo> ps;
    for (auto& x : rs)
        ps.push_back(x.shown);
    size_t shown = 0;
    g_pictures_waiting = false;
    g_pictures_store = 0;
    g_here_built = 0;
    for (auto& row : rows())
        g_here_built += row.shown.species[0] && row.shown.owner && galaxy_crt::here(row.shown.owner, row.shown.species);
    for (size_t i = 0; i < n; i++) {
        uint8_t* e = begin + i * 0xBC;
        uint32_t* key = (uint32_t*)(e + 0x3C);
        if (!g_more_name.empty() && i == n - 1) { // 5Z B: the page star
            set_name(e, u16(g_more_name));
            key[0] = key[1] = key[2] = 0;
            *(void**)(e + 0x48) = nullptr;
            *(uint32_t*)(e + 0x8C) = 0;
            *(uint32_t*)(e + 0xA8) = 0;
            e[0xAC] = 1;
            e[0xAD] = 1;
        } else if (i < ps.size()) {
            const galaxy::PlanetInfo& p = ps[i];
            set_name(e, u16(g_one_row ? p.name : net::galaxy::era_display(p.name))); // (BUG-9295: the planet; before, "Samuth (Tribe)")
            uint32_t k[3];
            void* prof = picture(p, k); // 5Z B: from the store when this game lacks it
            key[0] = k[0];
            key[1] = k[1];
            key[2] = k[2];
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
    g_last = std::to_string(shown) + " of " + std::to_string(all) + " server planet(s) in " + std::to_string(n) + " entries";
    if (g_pages > 1)
        g_last += ", page " + std::to_string(g_page + 1) + " of " + std::to_string(g_pages);
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
// BUG-9335: several eras; BUG-9336: or a new stage this player may start there
static bool wants_choice(const Row& row)
{
    return g_choice && !has_remembered(row) &&
           (row.eras.size() > 1 || (new_stage(row).era && stage_reached(new_stage(row).era)));
}

static const Row* row_of(const std::vector<Row>& rs, const std::string& planet)
{
    for (auto& row : rs)
        if ((g_one_row ? row.shown.name : net::galaxy::era_display(row.shown.name)) == planet)
            return &row;
    return nullptr;
}

// entry: the menu entry, for Spore's own screen (tests: coop.gmenu play); nullptr from the Play step: our box (the
// Play button's state switch already went to Spore's screen when it could, h_SetState)
// BUG-9358: the Play step reads its entry's name every frame until the menu is left; an entry rewritten meanwhile (the
// list changed: the planet entered got a player and moved up) named another planet, which was entered next (seen on the
// MSI 2026-11-23: Testplanet01 entered, left a second later for Samuth). The menu's entries are rebuilt only on the
// planets (state 2), and a Play takes no other planet for 30 s. Off: as before
static bool g_play_hold = true;
COOP_FIX("galaxy.play_hold", "BUG-9358", g_play_hold, "galaxy menu: a Play keeps its planet while the list changes");
static unsigned g_play_held;
static std::string g_entered;  // the planet a Play entered last
static double g_entered_at = -100;
static bool play(const std::string& planet, uint8_t* entry = nullptr)
{
    if (g_play_hold && !g_entered.empty() && planet != g_entered && H->time_seconds() - g_entered_at < 30) {
        if (!g_play_held++ || g_play_held % 100 == 0)
            H->log(M, SK_WARN, "galaxy menu: Play on '%s' ignored: '%s' was entered %.0f s ago (BUG-9358)", planet.c_str(),
                   g_entered.c_str(), H->time_seconds() - g_entered_at);
        return true;
    }
    for (auto& row : rows())
        if ((g_one_row ? row.shown.name : net::galaxy::era_display(row.shown.name)) == planet) {
            const std::string key = era_to_enter(row);
            const double now = H->time_seconds();
            if (galaxy::current_planet() != key && (planet != g_play_name || now - g_play_at > 30)) {
                g_play_name = planet;
                g_play_at = now;
                if (wants_choice(row)) {
                    if (!open_native(row, entry, true)) // BUG-9337: Spore's own screen; our box when it cannot open
                        open_choice(row);
                    return true;
                }
                g_plays++;
                g_entered = planet, g_entered_at = now; // BUG-9358
                H->log(M, SK_INFO, "galaxy menu: Play on the server planet '%s': entering it (5X B)", planet.c_str());
                galaxy::enter(key);
            }
            return true;
        }
    return false;
}

// BUG-9337: the menu's state switch. The planet panel's Play sets the Play state's entry and switches to state 0xC (its
// stage card and the Play step); for a server planet that wants a stage choice it goes to state 4 (Spore's Choose
// A Stage screen) instead, as the panel of an empty star does. (Switching from the Play step left the stage card on
// screen: 2026-11-22, TITAN.)
// BUG-9338: when Spore's screen is not used (its switch off), our box opened from the Play step, on top of the Play
// state's stage loading card (which stayed): Play goes back to the planets (state 2, as the screen's Back) and the box
// opens there. Off: as before (the box over the card)
static bool g_box_no_card = true;
COOP_FIX("galaxy.choice_box_no_card", "BUG-9338", g_box_no_card, "galaxy menu: our stage box opens over the planets, not over Spore's stage loading card");
static std::string entry_name(const uint8_t* e)
{
    const char16_t* b = *(const char16_t* const*)e;
    const char16_t* en = *(const char16_t* const*)(e + 4);
    return b && en > b ? utf8(b, en) : "";
}

// 5Z B: the planets' page star
static void turn_page()
{
    g_page = g_pages > 1 ? (g_page + 1) % g_pages : 0;
    g_page_turns++;
    g_last_version = ~0u; // (the next tick builds the menu again)
    H->log(M, SK_INFO, "galaxy menu: the planets' page %u of %u (5Z B)", (unsigned)g_page + 1, (unsigned)g_pages);
}

// 5Z B (owner 2026-11-17: "when a player is on the Online Galaxy Menu, we should allow them to delete their creature on
// the world still. Deleting their creature will let them rejoin that planet with a new one."): the planet panel's bin
// on a server planet asks "Delete my creature here" instead of Spore's own box (which deletes a save file). The worlds
// this player played the planet's eras in are remembered (coop.ini [galaxy_worlds]); the delete forgets this player's
// creature records there (creations::forget_world), so the next entry asks for a first creature. Its old nest stays in
// the stored world for its family and goes to a new species with the nest rotation (7F, BUG-9286).
static bool g_delete_on = true;
COOP_FIX("galaxy.delete_creature", "BUG-9357", g_delete_on, "galaxy menu: the planet panel's bin on a server planet deletes this player's creature there");
static uintptr_t a_DeleteState, a_CameraBack;
static std::string g_delete_planet;
static unsigned g_delete_asks, g_deletes, g_delete_none;
static std::string g_worlds_seen_for;
static double g_worlds_since;
static std::vector<uint32_t> worlds_of(const std::string& planet)
{
    char b[256] = {};
    H->config_str(M, "galaxy_worlds", remember_key(planet).c_str(), "", b, sizeof b);
    std::vector<uint32_t> v;
    for (char* t = strtok(b, ","); t; t = strtok(nullptr, ","))
        if (uint32_t w = (uint32_t)strtoul(t, nullptr, 16))
            v.push_back(w);
    return v;
}
// in a stage on a server era for 10 s: its world is one of the planet's for this player
static void remember_world(double now)
{
    const std::string cur = galaxy::current_planet();
    const uint32_t world = modes::current_world();
    const std::string at = cur + "#" + std::to_string(world);
    if (cur.empty() || net::galaxy::is_crossing(cur) || !world || !galaxy::era_of_mode(game::mode_id())) {
        g_worlds_seen_for.clear();
        return;
    }
    if (at != g_worlds_seen_for) {
        g_worlds_seen_for = at;
        g_worlds_since = now;
        return;
    }
    if (now - g_worlds_since < 10 || g_worlds_since < 0)
        return;
    g_worlds_since = -1; // (once)
    const std::string planet = net::galaxy::era_planet(cur);
    std::vector<uint32_t> v = worlds_of(planet);
    if (std::find(v.begin(), v.end(), world) != v.end())
        return;
    v.push_back(world);
    if (v.size() > 8)
        v.erase(v.begin());
    std::string val;
    for (uint32_t w : v) {
        char h[12];
        snprintf(h, sizeof h, "%08X", w);
        val += (val.empty() ? "" : ",") + std::string(h);
    }
    H->config_set_str(M, "galaxy_worlds", remember_key(planet).c_str(), val.c_str());
    H->log(M, SK_INFO, "galaxy menu: world %08X is one of '%s' for this player (5Z B)", world, planet.c_str());
}
static void on_delete_pick(int code)
{
    const std::string planet = g_delete_planet;
    g_delete_planet.clear();
    if (code != 1 || planet.empty())
        return;
    if (net::galaxy::era_planet(galaxy::current_planet()) == planet) {
        notify::show("Leave " + planet + " first, then delete your creature there.", "info");
        return;
    }
    int n = 0;
    for (uint32_t w : worlds_of(planet))
        n += creations::forget_world(w);
    if (!n) {
        g_delete_none++;
        notify::show("You have no creature of your own on " + planet + ".", "info");
        return;
    }
    g_deletes++;
    H->log(M, SK_INFO, "galaxy menu: this player's creature on '%s' deleted (%d entries, 5Z B)", planet.c_str(), n);
    notify::show("Your creature on " + planet + " is deleted. Next time you play there, you make a new one.", "info");
}
static void ask_delete(const std::string& planet)
{
    g_delete_planet = planet;
    g_delete_asks++;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    coop_ui::show_player_menu(sw * 0.5f - 112, sh * 0.3f, planet,
                              {{"Delete my creature", 1, coop_ui::kMenuRed}, {"Cancel", 0, coop_ui::kMenuGrey}},
                              on_delete_pick);
    H->log(M, SK_INFO, "galaxy menu: the bin on '%s': \"Delete my creature here\" asked (5Z B)", planet.c_str());
}

static void __fastcall h_SetState(void* gge, void* edx, int state, int push)
{
    if (state == 0xB && g_delete_on && a_DeleteState && g_flow_hooks && active()) { // 5Z B: the panel's bin
        uint8_t* ds = *(uint8_t**)a_DeleteState;
        uint8_t* e = ds ? *(uint8_t**)(ds + 0x48) : nullptr;
        if (e && e[0xAC]) {
            const std::string planet = entry_name(e);
            const std::vector<Row> rs = rows();
            if (planet == g_more_name || row_of(rs, planet)) {
                *(void**)(ds + 0x48) = nullptr; // (Spore's box does not open: no save file to delete)
                if (planet != g_more_name)
                    ask_delete(planet);
                return;
            }
        }
    }
    if (state == 0xC && g_flow_hooks && active() && a_PlayState && !g_more_name.empty()) { // 5Z B: the page star's Play
        uint8_t* ps = *(uint8_t**)a_PlayState;
        uint8_t* e = ps ? *(uint8_t**)(ps + 0xC) : nullptr;
        if (e && e[0xAC] && entry_name(e) == g_more_name) {
            turn_page();
            o_SetState(gge, edx, 2, push); // (the planets: no stage card, no Play step)
            // the view zoomed in on the star stays there in state 2 (seen on the MSI, 2026-11-23: dark until Esc): what
            // the planets' Esc does on a saved planet's panel (0xDF6300): the camera back to the galaxy, then the planet
            // set's vt 0x60(nullptr, 1) (the panel closed)
            if (a_CameraBack)
                skg::cdecl_call<void>(a_CameraBack);
            if (uint8_t* set = a_PlanetSet ? *(uint8_t**)a_PlanetSet : nullptr)
                skg::vcall<void>(set, 0x60 / 4, (void*)nullptr, 1);
            return;
        }
    }
    if (state == 0xC && g_flow_hooks && active() && a_PlayState) {
        uint8_t* ps = *(uint8_t**)a_PlayState;
        uint8_t* e = ps ? *(uint8_t**)(ps + 0xC) : nullptr;
        if (e && e[0xAC]) {
            const std::string planet = entry_name(e);
            const std::vector<Row> rs = rows();
            const Row* row = row_of(rs, planet);
            if (row && galaxy::current_planet() != era_to_enter(*row) && wants_choice(*row)) {
                if (g_native && a_NewGameState && open_native(*row, e, false)) {
                    g_play_name = planet;
                    g_play_at = H->time_seconds();
                    state = 4;
                } else if (g_box_no_card) { // BUG-9338
                    g_play_name = planet;
                    g_play_at = H->time_seconds();
                    open_choice(*row);
                    state = 2;
                }
            }
        }
    }
    if (g_native_entry && state != 4 && g_gone_fn) // 8M A: the screen goes (Esc, the menu left): the panel first
        g_gone_fn();
    o_SetState(gge, edx, state, push);
}

// the Play step (thiscall, one stack argument): step +0xC the picked entry, +0x8 a countdown (the action at 0)
// the menu entry shown as `planet` (tests)
static uint8_t* entry_of(const std::string& planet)
{
    uint8_t* set = a_PlanetSet ? *(uint8_t**)a_PlanetSet : nullptr;
    uint8_t* b = set ? *(uint8_t**)(set + 8) : nullptr;
    uint8_t* en = set ? *(uint8_t**)(set + 0xC) : nullptr;
    for (uint8_t* e = b; e && e + 0xBC <= en; e += 0xBC) {
        const char16_t* nb = *(const char16_t* const*)e;
        const char16_t* ne = *(const char16_t* const*)(e + 4);
        if (e[0xAC] && nb && ne > nb && utf8(nb, ne) == planet)
            return e;
    }
    return nullptr;
}

static void __fastcall h_Step(void* self, void* edx, void* arg)
{
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    uint8_t* e = self ? *(uint8_t**)((uint8_t*)self + 0xC) : nullptr;
    if (active() && e && gge && *(int*)((uint8_t*)self + 8) <= 0 && !gge[0x1F0] && !gge[0x140] && e[0xAC]) {
        const char16_t* b = *(const char16_t* const*)e;
        const char16_t* en = *(const char16_t* const*)(e + 4);
        const std::string planet = b && en > b ? utf8(b, en) : "";
        if (!g_more_name.empty() && planet == g_more_name) { // 5Z B: the page star (its Play went to state 2 already)
            turn_page();
            return;
        }
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
    if (g_native_entry && now - g_native_at > 2) { // BUG-9337: the screen left without a stage (Esc), or the menu left
        uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
        if (!gge || game::mode_id() != modes::kMenu || *(int*)(gge + 0x108) != 4) {
            H->log(M, SK_INFO, "galaxy menu: Choose A Stage counted closed: mode %08X, menu state %d (BUG-9350)",
                   game::mode_id(), gge ? *(int*)(gge + 0x108) : -1);
            g_native_closes++;
            native_closed();
        }
    }
    remember_world(now); // 5Z B
    if (!g_hooks || game::mode_id() != modes::kMenu)
        return;
    const bool online = g_on && galaxy::online();
    if (online && g_pictures_on && g_pictures_waiting && now >= g_pictures_next) { // 5Z B: a creature from the store arrived: build again
        g_pictures_next = now + 2;
        unsigned here = 0;
        for (auto& row : rows())
            here += row.shown.species[0] && row.shown.owner && galaxy_crt::here(row.shown.owner, row.shown.species);
        if (here > g_here_built)
            g_last_version = ~0u; // (one arrived since the build that asked: built again)
    }
    const uint32_t ver = galaxy::planets_version();
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    const bool on_planets = !g_play_hold || !gge || *(int*)(gge + 0x108) == 2; // BUG-9358: not while Play runs
    if ((online != g_last_online || (online && ver != g_last_version)) && now >= g_rebuild_at && on_planets) {
        g_last_online = online;
        g_last_version = ver;
        g_rebuild_at = now + 2;
        H->run_outside_frame(rebuild, nullptr);
    }
}

void on_screen_gone(void (*fn)()) { g_gone_fn = fn; }

bool stage_screen(StageScreen& out)
{
    if (!g_native_entry || !a_NewGameState || !a_FindWin || game::mode_id() != modes::kMenu)
        return false;
    uint8_t* gge = a_Gge ? *(uint8_t**)a_Gge : nullptr;
    uint8_t* st = *(uint8_t**)a_NewGameState;
    uint8_t* flow = st ? *(uint8_t**)(st + 8) : nullptr; // (state 4 +0x8: the new-game flow, docs/re/space.md)
    if (!gge || *(int*)(gge + 0x108) != 4 || !flow || *(void**)(flow + 0x628) != g_native_entry)
        return false;
    out = StageScreen{};
    out.planet = g_native_planet;
    for (int i = 0; i < 5; i++) {
        const uint8_t era = (uint8_t)(i + 1);
        const int code = g_native_codes[era];
        if (code >= 0 && code < (int)g_pick_eras.size()) {
            out.era[era] = g_pick_eras[code];
            out.fresh[era] = code < (int)g_pick_from.size() && !g_pick_from[code].empty();
        }
        out.reached[era] = stage_reached(era);
        out.button[era] = skg::thiscall<void*>(a_FindWin, flow + 0x28, kStageButtons[i], 1);
    }
    return true;
}

// tests: the planet panel of the menu entry named `g_open_name`, as a click on its star (the planet set's vt 0x60(entry,
// 1): the panel and the camera on it, docs/re/space.md#space-menu-entries)
static std::string g_open_name;
static void open_panel(void*)
{
    uint8_t* set = a_PlanetSet ? *(uint8_t**)a_PlanetSet : nullptr;
    uint8_t* e = entry_of(g_open_name);
    if (set && e && game::mode_id() == modes::kMenu)
        skg::vcall<void>(set, 0x60 / 4, (void*)e, 1);
}

std::string command(int argc, const char* const* argv)
{
    const std::string sub = argc > 1 ? argv[1] : "status";
    if (sub == "on" || sub == "off") {
        g_on = sub == "on";
        g_last_version = ~0u;
    } else if (sub == "rebuild")
        H->run_outside_frame(rebuild, nullptr);
    if (sub == "open" && argc > 2) { // tests: the panel of the star named argv[2] (the game's own focus, no click)
        g_open_name = argv[2];
        if (!entry_of(g_open_name))
            return "no menu entry '" + g_open_name + "'";
        H->run_outside_frame(open_panel, nullptr);
        return "opening the panel of '" + g_open_name + "'";
    }
    if (sub == "page") // tests (5Z B): as the page star's Play
        turn_page();
    if (sub == "delete" && argc > 2) // tests (5Z B): as the panel's bin on planet argv[2]
        ask_delete(argv[2]);
    if (sub == "deletepick" && argc > 2) // tests (5Z B): as a press in the delete box (1 delete, 0 cancel)
        on_delete_pick(atoi(argv[2]));
    if (sub == "worlds" && argc > 2) { // tests (5Z B): the worlds remembered for planet argv[2]
        std::string v;
        for (uint32_t w : worlds_of(argv[2])) {
            char h[12];
            snprintf(h, sizeof h, "%08X ", w);
            v += h;
        }
        return "worlds of '" + std::string(argv[2]) + "': " + (v.empty() ? "none" : v);
    }
    if (sub == "panel") // tests (8M A, BUG-9340): the planet details panel as shown
        return coop_ui::planet_panel_debug();
    if (sub == "forget" && argc > 2) // tests (BUG-9335): this player has played no era of planet argv[2]
        H->config_set_str(M, "galaxy_eras", remember_key(argv[2]).c_str(), "");
    if (sub == "pick" && argc > 2) // tests (BUG-9335): as a press on the open choice's button n
        on_stage_pick(atoi(argv[2]));
    if (sub == "play" && argc > 2) { // tests (BUG-9335): the menu's Play on planet argv[2] (its code path, no click)
        if (!active())
            return "not in the galaxy menu while online (Play is the menu's)";
        g_play_at = g_entered_at = -100;
        if (!play(argv[2], entry_of(argv[2])))
            return std::string("no server planet '") + argv[2] + "' in the menu";
    }
    char b[1300];
    snprintf(b, sizeof b, "galaxy menu (5X B): %s, hooks %s, active %d, rewrites %u, plays %u, rebuilds %u, last: %s; "
             "stage choice (BUG-9335): opened %u, entered %u, refused %u, new stages started %u (BUG-9336), last: %s; reached: %d%d%d%d%d",
             g_on ? "on" : "off", g_hooks ? "on" : "missing", (int)active(), g_rewrites, g_plays, g_rebuilds,
             g_last.empty() ? "-" : g_last.c_str(), g_choices, g_picks, g_refusals, g_starts,
             g_last_choice.empty() ? "-" : g_last_choice.c_str(), (int)stage_reached(1), (int)stage_reached(2),
             (int)stage_reached(3), (int)stage_reached(4), (int)stage_reached(5));
    const size_t n = strlen(b);
    snprintf(b + n, sizeof b - n, "\nSpore's Choose A Stage screen (BUG-9337, %s): hooks %s, opened %u, stages pressed %u, closed %u, "
             "open now %d, last: %s; closed before a load (BUG-9350, %s): %u, timeouts %u", g_native ? "on" : "off",
             g_flow_hooks ? "on" : "missing", g_native_opens, g_native_picks, g_native_closes, (int)(g_native_entry != nullptr),
             g_native_last.empty() ? "-" : g_native_last.c_str(), g_close_on_leave ? "on" : "off", g_left_closes, g_left_timeouts);
    const size_t n2 = strlen(b);
    snprintf(b + n2, sizeof b - n2, "\n5Z B: pages %s (page %u of %u, turned %u); pictures from the store %u (waiting %d); "
             "delete my creature %s (asked %u, deleted %u, none there %u)", g_pages_on ? "on" : "off", (unsigned)g_page + 1,
             (unsigned)g_pages, g_page_turns, g_pictures_store, (int)g_pictures_waiting, g_delete_on ? "on" : "off",
             g_delete_asks, g_deletes, g_delete_none);
    const size_t n3 = strlen(b);
    snprintf(b + n3, sizeof b - n3, "; Play keeps its planet (BUG-9358, %s): other planets ignored %u", g_play_hold ? "on" : "off",
             g_play_held);
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
    // BUG-9337: Spore's own Choose A Stage screen (the new game's flow)
    a_NewGameState = H->sym("SporeCoop::GGE_NewGameState_ptr");
    a_SetState = H->sym("SporeCoop::GGE_SetState");
    a_FindWin = H->sym("UTFWin::UILayout::FindWindowByID");
    a_PlanetSet = H->sym("SporeCoop::GGE_PlanetSet_ptr");
    a_PlayState = H->sym("SporeCoop::GGE_PlayState_ptr");
    a_DeleteState = H->sym("SporeCoop::GGE_DeleteState_ptr"); // 5Z B
    a_CameraBack = H->sym("SporeCoop::GGE_CameraToGalaxy");    // BUG-9356
    uintptr_t fopen = H->sym("SporeCoop::NewGameFlow_Open"), fbutton = H->sym("SporeCoop::NewGameFlow_OnButton");
    g_flow_hooks = ok && a_NewGameState && a_SetState && a_FindWin && fopen && fbutton &&
                   H->hook_create(M, (void*)fopen, (void*)&h_FlowOpen, (void**)&o_FlowOpen) == 0 &&
                   H->hook_create(M, (void*)fbutton, (void*)&h_FlowButton, (void**)&o_FlowButton) == 0 &&
                   H->hook_create(M, (void*)a_SetState, (void*)&h_SetState, (void**)&o_SetState) == 0;
    H->log(M, SK_INFO, "galaxy menu: Choose A Stage hooks %s (BUG-9337)", g_flow_hooks ? "on" : "off");
}

} // namespace galaxy_menu
