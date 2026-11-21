// A partner's own launch at the Civ -> Space change (BUG-9295; the idea is Lineages PR #196's per-guest launch,
// BUG-9071, written new for 0.7.15: docs/re/civ.md#civ-guest-launch).
//
// The host's History check starts the spaceship editor vote; when it ends the host designs its ship and its game goes
// to Space. A guest's own History check did nothing: its game never saw the planet won (the team campaign raises the
// nations' levels and shows the planet-won state on the host's game only, civ_nations.cpp campaign_task), so its
// check only closed the History. Now, when that vote ends on a guest with its own nation (civ_nations own_faction):
// * its game's player civilization gets level 4 (cCivilization_RaiseLevel) and the game's planet-won state
//   (GameCiv_ShowPlanetWon + GameCiv_PlanetWonPause), as on the host. Its History check then submits the game's own
//   spaceship editor request (0x96B24187; vote.cpp lets a guest's own design editor through), the guest designs its
//   ship, and its own game launches into Space with it (its own planet-side start, its own flight tutorial);
// * the host's Space world (the share that goes out when the host leaves its home planet) waits on this guest while
//   its own launch runs: in Civ (at most kCivWait, then it is loaded as before), in its editor and on its home planet
//   in Space (the game's context 0). When the guest leaves the planet (context 1 / 2) the host's world is loaded; the
//   guest's own empire (space_empires.cpp) then flies its newest own design: the ship it has just made.
// `coop.fix civ.guest_launch off`: nothing of this (the guest's History does nothing, it gets the host's world at once).
#include "civ_launch.h"

#include <cstdio>

#include "civ_internal.h"
#include "civ_nations.h"
#include "designs.h"
#include "fixes.h"
#include "lifecycle.h"
#include "modes.h"
#include "world.h"

using skg::cdecl_call;
using skg::thiscall;

namespace civ::launch {

static bool g_fix = true;
COOP_FIX("civ.guest_launch", "BUG-9295", g_fix,
         "civ: at the Civ -> Space launch a partner with its own nation designs its own spaceship in its own History "
         "editor and launches with it (the host's Space world waits for its first flight)");

constexpr double kCivWait = 600.0;     // the History check not pressed for 10 min: the host's world is loaded as before
constexpr double kLaunchWait = 1800.0; // the own launch's hold ends after 30 min in any case
constexpr int kLevelOwned = 4;         // cCivilization +0x298: 4 = the planet is owned (GameCiv_IsPlanetOwned)
constexpr uintptr_t kLevel = 0x298;

enum class Phase : uint8_t { None, Prepare, Civ, Editor, Space };
static Phase g_phase = Phase::None;
static std::string g_planet;
static double g_at, g_posted;
static uint32_t g_armed, g_prepared, g_editors, g_launched, g_released, g_held;
static std::string g_last = "-";

static const char* phase_name(Phase p)
{
    switch (p) {
    case Phase::Prepare: return "prepare";
    case Phase::Civ: return "civ";
    case Phase::Editor: return "editor";
    case Phase::Space: return "space";
    default: return "none";
    }
}

static void end(const char* why)
{
    if (g_phase == Phase::None)
        return;
    g_last = why;
    H->log(M, SK_INFO, "civ launch: the own launch's hold ends (%s, phase %s)", why, phase_name(g_phase));
    g_phase = Phase::None;
    g_planet.clear();
    g_released++;
}

void reset() { end("the session ended"); }
LIFECYCLE_HOOK("civ.launch", lifecycle::kSessionEnd, -40, [](const lifecycle::Ctx&) { reset(); }, false);

void on_vote_end(uint32_t editor)
{
    if (!g_fix || !g_ok || !is_guest() || editor != designs::kShipEditor || game::mode_id() != modes::kCiv)
        return;
    if (!nations::own_faction()) {
        H->log(M, SK_INFO, "civ launch: the spaceship vote ended; no nation of my own: the host's ship and world");
        return;
    }
    g_phase = Phase::Prepare;
    g_planet = modes::current_world_name();
    g_at = H->time_seconds();
    g_posted = 0;
    g_armed++;
    H->log(M, SK_INFO, "civ launch: the spaceship vote ended: my own launch on %s (my nation %08X)", g_planet.c_str(),
           nations::own_faction());
}

// outside the frame (RaiseLevel can start the game's unlock caption, ShowPlanetWon builds UI)
static void prepare_task(void*)
{
    if (g_phase != Phase::Prepare || game::mode_id() != modes::kCiv)
        return;
    static uintptr_t raise = H->sym("SporeCoop::cCivilization_RaiseLevel"), get = H->sym("SporeCoop::GameCiv_Get"),
                     won = H->sym("SporeCoop::GameCiv_ShowPlanetWon"), pause = H->sym("SporeCoop::GameCiv_PlanetWonPause");
    void* pc = player_civ();
    void* gc = get ? cdecl_call<void*>(get) : nullptr;
    if (!pc || !gc || !raise || !won || !pause) {
        end("no player civilization or game mode");
        return;
    }
    const int have = F<int>(pc, kLevel);
    if (have < kLevelOwned)
        thiscall<void>(raise, pc, kLevelOwned);
    thiscall<void>(won, gc);
    thiscall<void>(pause, gc);
    g_prepared++;
    g_phase = Phase::Civ;
    H->log(M, SK_INFO, "civ launch: my game's planet is won (level %d -> %d): its History opens my spaceship editor", have,
           F<int>(pc, kLevel));
    if (C.feed)
        C.feed("Open History and click its checkmark to design your own spaceship and launch it");
}

static int space_context()
{
    static uintptr_t get = H->sym("Simulator::GetCurrentContext");
    return get ? cdecl_call<int>(get) : -1;
}

void tick()
{
    if (g_phase == Phase::None)
        return;
    if (!g_fix) {
        end("switched off");
        return;
    }
    const double now = H->time_seconds(), age = now - g_at;
    const uint32_t mode = game::mode_id();
    if (age > kLaunchWait) {
        end("the time ran out");
        return;
    }
    if (mode == modes::kMenu) {
        end("the galaxy menu");
        return;
    }
    if (modes::current_world_name() != g_planet) {
        end("another world");
        return;
    }
    switch (g_phase) {
    case Phase::Prepare:
        if (mode != modes::kCiv) {
            end("not in Civilization");
            return;
        }
        if (!world::cinematic_playing() && !world::time_paused() && now - g_posted > 2.0) {
            g_posted = now;
            H->run_outside_frame(prepare_task, nullptr);
        }
        return;
    case Phase::Civ:
    case Phase::Editor:
        if (mode == modes::kEditor || mode == modes::kEdit) {
            if (g_phase == Phase::Civ) {
                g_phase = Phase::Editor;
                g_editors++;
                H->log(M, SK_INFO, "civ launch: my spaceship editor is open");
            }
        } else if (mode == modes::kSpace) {
            g_phase = Phase::Space;
            g_launched++;
            H->log(M, SK_INFO, "civ launch: my game launched into Space with my own ship");
        } else if (mode == modes::kCiv && age > kCivWait)
            end("History not used");
        return;
    case Phase::Space:
        if (mode == modes::kSpace) {
            const int ctx = space_context();
            if (ctx == 1 || ctx == 2)
                end("my ship left its home planet");
        } else if (mode != modes::kLoad)
            end("not in Space");
        return;
    default:
        return;
    }
}

bool holds_load()
{
    if (g_phase == Phase::None || !g_fix)
        return false;
    g_held++;
    return true;
}

std::string status()
{
    char b[400];
    snprintf(b, sizeof b,
             "civ launch: fix=%d phase=%s planet=%s age=%.0f s armed=%u prepared=%u editors=%u launched=%u released=%u "
             "held_frames=%u last=%s\n",
             (int)g_fix, phase_name(g_phase), g_planet.empty() ? "-" : g_planet.c_str(),
             g_phase == Phase::None ? 0.0 : H->time_seconds() - g_at, g_armed, g_prepared, g_editors, g_launched, g_released,
             g_held, g_last.c_str());
    return b;
}

} // namespace civ::launch
