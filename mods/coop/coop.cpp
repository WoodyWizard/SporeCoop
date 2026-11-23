// SporeCoop mod: creature-stage co-op ("pack" mode). See docs/03-gameplay.md.
// the release version (tools/package.sh names the zip after it)
#ifndef COOP_VERSION
#define COOP_VERSION "0.7.16"
#endif

#include <winsock2.h>
#include "lifecycle.h"
#include "call_guard.h"
#include "player_life.h"
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <mutex>
#include <cstddef>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <csignal>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

#include "adapters.h"
#include "coop_ui.h"
#include "driver.h"
#include "game.h"
#include "modes.h"
#include "stage_epoch.h"
#include "world_id.h"
#include "world_name.h"
#include "mouse.h"
#include "shared.h"
#include "sui.h"
#include "teamplay.h"
#include "tribe.h"
#include "drama.h"
#include "cellsync.h"
#include "audio.h"
#include "census.h"
#include "civ.h"
#include "civ_editors.h"
#include "autoupdate.h"
#include "gamefixes.h"
#include "spacecoop.h"
#include "squadron.h"
#include "npc.h"
#include "creature_hp.h"
#include "player_rules.h"
#include "mirror_fx.h"
#include "effects.h"
#include "nest.h"
#include "fields.h"
#include "wsync.h"
#include "worldobj.h"
#include "realign.h"
#include "parts.h"
#include "rejoin.h"
#include "savestrip.h"
#include "cell_logic.h"
#include "busy.h"
#include "cocoon.h"
#include "social.h"
#include "world.h"
#include "find.h"
#include "net/discovery.h"
#include "net/fixes.h"
#include "net/give_target.h"
#include "net/interp.h"
#include "net/lobby.h"
#include "net/session.h"
#include "net/utf8.h"
#include "creation_props.h"
#include "creations.h"
#include "species_line.h"
#include "designs.h"
#include "notify.h"
#include "hud_portrait.h"
#include "notify_text.h"
#include "syncdbg.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include <shlobj.h>
#include "share_wait.h"
#include "dna_route.h"
#include "dna_ledger.h"
#include "dna_pace.h"
#include "settings.h"
#include "store.h"
#include "autosave_glue.h"
#include "sreport.h"
#include "space_ext.h"
#include "host_world.h"
#include "own_world.h"
#include "diag_coop.h"
#include "trace.h"
#include "view_centres.h"
#include "coop_state.h"

const SkHost* H;
SkMod* M;
extern const SkHost* g_host_for_modes; // modes.cpp
extern SkMod* g_mod_for_modes;

void coop_register_debug_commands(const SkHost* host, SkMod* self); // debug.cpp

namespace coop_core {


net::Session g_sess;
std::map<uint8_t, Remote> g_remotes;
std::string g_name = "Player";
double g_last_send = 0;
static float g_sent_speed = 0; // BUG-1093: the speed in the last state sent (m/s)
void* g_last_avatar = nullptr;
std::deque<std::pair<double, std::string>> g_feed;
bool g_overlay = true;
bool g_share_world = true; // host sends its save to joining guests
bool g_anim_log = false;
double g_last_time_sync = 0;
bool g_tag_self = false; // debug: draw a tag over our own avatar to check the projection
float g_dbg_sc[3];

// echo test: mirrors our own avatar as a fake remote player
bool g_echo = false;
float g_echo_offset = 6.f;
double g_echo_delay = 0.0;
std::deque<std::pair<double, net::PlayerState>> g_echo_q;

// kind: the panel kind ("chat", "join", "leave", ...); null: guessed from a system text (BUG-0129)
void feed_kind(const std::string& s, const char* kind)
{
    g_feed.emplace_back(H->time_seconds(), s);
    while (g_feed.size() > 8)
        g_feed.pop_front();
    LOG("%s", s.c_str());
    notify::show(s, kind); // a notification panel; the full history is in the co-op window
}

void feed(const std::string& s) { feed_kind(s, nullptr); }

// ---- lobby: chat log, ready flags, round trip times, LAN discovery ----
std::deque<std::string> g_chat;
std::deque<uint32_t> g_chat_col; // F2: the colour of each line (its writer's; 0 = the window's own)
std::map<uint8_t, bool> g_ready;   // by player id (the host keeps the truth)
std::map<uint8_t, uint8_t> g_roles; // civilization roles by player id (the host keeps the truth)
// roles end with their player and session, the lobby table keeps them with their ids (BUG-0064,
// BUG-0079); `coop.role fix 0` = the code before (tests)
bool g_role_fix = true;
COOP_FIX("role.fix", "BUG-0064 BUG-0079", g_role_fix, "civ: roles end with their player and session (coop.role fix)");
std::map<uint8_t, int> g_lobby_rtt; // guest: host-measured rtt of every player
// BUG-0772 (not reproduced, a guard): the ready flags and round trips are in the BUG-0422 registry, so a join and a
// leave of an id clear them like every other per-player state. The ticket's path (a leaver without a Remote kept
// its flag) is not reached: the host's player_join makes the Remote (lab 20261112-111316, docs/tickets/BUG-0772).
PLAYER_STATE(
    "ready",
    [](uint8_t id) {
        g_ready.erase(id);
        g_lobby_rtt.erase(id);
    },
    [](uint8_t id) -> size_t {
        auto it = g_ready.find(id);
        return it != g_ready.end() && it->second ? 1 : 0;
    });
bool g_lobby_dirty = false;
double g_last_lobby = 0;
net::Discovery g_disc;
bool g_disc_on = false;
int g_lan_fake_n = 0;           // coop.lanfake: games on the network that answer (tests, BUG-0183)
uint32_t g_lan_fake_until = 0;

void chat_log(const std::string& s, uint32_t argb)
{
    g_chat.push_back(s);
    g_chat_col.push_back(argb);
    while (g_chat.size() > 50)
        g_chat.pop_front();
    while (g_chat_col.size() > g_chat.size())
        g_chat_col.pop_front();
}

// an event everybody sees (feed + lobby chat): guests send it to the host, the host relays it
void notice(const std::string& text, int from)
{
    feed(text);
    chat_log("* " + text);
    net::Writer w;
    w.str8(text);
    if (g_sess.role() == net::Role::Host)
        g_sess.broadcast_reliable(net::MSG_NOTICE, w, from);
    else if (g_sess.role() == net::Role::Client && from < 0)
        g_sess.send_reliable_to(0, net::MSG_NOTICE, w);
}

// BUG-0286: the lookup lives in adapters.cpp (off the main thread with the fix core.adapters)
std::vector<LocalAddr> local_addresses() { return adapters::list(); }

void lan_scan(bool on)
{
    // BUG-0286: the addresses come from a background lookup: new ones join a running scan
    static uint32_t gen;
    if (on && g_disc_on && adapters::generation() != gen) {
        gen = adapters::generation();
        g_disc.targets = {0xFFFFFFFFu, 0x0100007Fu};
        for (auto& a : local_addresses())
            if (a.broadcast != 0xFFFFFFFFu)
                g_disc.targets.push_back(a.broadcast);
        return;
    }
    if (on == g_disc_on)
        return;
    g_disc_on = on;
    if (!on) {
        g_disc.stop();
        return;
    }
    g_disc.targets = {0xFFFFFFFFu, 0x0100007Fu};
    for (auto& a : local_addresses())
        if (a.broadcast != 0xFFFFFFFFu)
            g_disc.targets.push_back(a.broadcast);
    gen = adapters::generation();
    g_disc.start();
}

uint32_t build_id()
{
    // first 4 bytes of the sha256 of the game build: peers must run the same build. A build that
    // only uses another build's symbols (same code, other exe hash) counts as that build.
    std::string b = H->exe_sha256();
    if (H->size >= offsetof(SkHost, symbols_build) + sizeof(void*) && H->symbols_build && *H->symbols_build())
        b = H->symbols_build();
    return (uint32_t)strtoul(b.substr(0, 8).c_str(), nullptr, 16);
}

bool stage_mode(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv ||
           m == modes::kSpace;
}

void notice(const std::string& text, int from); // feed + chat log on every player

// Guest: the host left the stage for the main menu (pause menu "Exit to Main Menu") while we play.
// The host simulates the NPCs, so the planet was empty and nobody said why (play_creature.py
// 2026-10-23). While the host is away the guest is told, plays on alone (its own game simulates
// its NPCs again, npc.cpp) and keeps the DNA it earns; the DNA goes to the host's pool when the
// host is back (the host's total would otherwise undo it).
bool g_host_away = false;
double g_host_away_since = -1;
float g_solo_dna = 0;
uint32_t g_host_away_count = 0;
bool g_host_away_fix = true; // BUG-0085: the away state ends with the session, without a "host is back"
COOP_FIX("core.host_away_reset", "BUG-0085", g_host_away_fix, "core: the away state ends with the session");

// BUG-0205: the host in an editor of its own (the guest left its editor first, or did not get one)
// or on a loading screen is away too: it sends no NPC, vehicle or citizen states meanwhile, and the
// guest's proxies stood frozen. Not while this guest's own editor of the same vote is still due (it
// opens up to ~20 s after the host's).
extern uint32_t g_own_editor_due;
// BUG-0233: an editor or a load of the host counts as away only in the stages whose modules play
// on locally meanwhile (npc in Creature, tribe / civ local_play); in Cell and Space the guest got
// "you play on alone" / "is back" for every editor visit of the host. The main menu counts in every
// stage (BUG-0151). Off: every stage (0.7.3.0).
// BUG-0205: Cell too since cellsync plays the guest's own pool while the host is away.
constexpr uint32_t kAwayStages[] = {modes::kCreature, modes::kTribe, modes::kCiv, modes::kCell};
bool g_away_stages_fix = true;
COOP_FIX("core.away_stages", "BUG-0233", g_away_stages_fix, "core: a host editor or load counts as away only in Creature, Tribe, Civ");
static bool g_override_out_fix = true;
COOP_FIX("diag.override_out", "BUG-1338", g_override_out_fix,
         "sync-check: a player whose states report another mode on purpose (the tests' coop.hoststate) is not compared");
// BUG-0233: DNA kept while the host was away stays in its stage and world: a guest's solo DNA goes
// to the pack only when the host plays that stage again (no "is back" when the host did not come
// back to the guest's world), the host's owed DNA is paid only into the Creature world it was owed
// in and dropped when that world or stage is over. Off: sent on any "back", paid into any world.
bool g_away_dna_stage_fix = true;
COOP_FIX("core.away_dna_stage", "BUG-0233", g_away_dna_stage_fix, "core: DNA kept while the host was away stays in its stage and world");
uint32_t g_solo_mode = 0;      // guest: the stage mode g_solo_dna was earned in
uint32_t g_dna_owed_world = 0; // host: the stage world g_dna_owed was owed in
uint32_t g_stage_world = 0;    // the world of the last stage this machine played
uint32_t g_solo_dropped = 0, g_owed_dropped = 0; // tests
// why the host is not playing our stage: 'm' main menu, 'e' editor, 'l' loading, 0 it is (or unknown)
// tests (BUG-1084, `coop.awaytest 1|0`, a guest): the host counts as in an editor (its game's editor without the
// editor: its states go on, the away rules hold them off) until it is turned off again
bool g_away_test = false;
char host_out_of_stage()
{
    if (g_sess.role() != net::Role::Client || !g_sess.connected() || !stage_mode(g_mode) || !g_local)
        return 0;
    if (g_away_test)
        return 'e';
    auto it = g_remotes.find(0);
    if (it == g_remotes.end() || it->second.buf.empty())
        return 0;
    uint32_t m = it->second.buf.latest().mode;
    if (m == modes::kMenu)
        return 'm';
    if (g_own_editor_due)
        return 0;
    if (g_away_stages_fix && !dna_route::away_stage(g_mode, kAwayStages, (int)std::size(kAwayStages)))
        return 0; // BUG-0233
    if (m == modes::kEdit || m == modes::kEditor)
        return 'e';
    return m == modes::kLoad ? 'l' : 0;
}

bool host_here() // the host plays in our world again (not loading it any more)
{
    auto it = g_remotes.find(0);
    if (it == g_remotes.end() || it->second.buf.empty())
        return false;
    const auto& ls = it->second.buf.latest();
    return same_place(ls) && (ls.flags & net::PF_IN_WORLD);
}

// BUG-0130: the guest plays another world of the host's stage (joined from its own game, loaded
// a save of its own, or the host loaded another save): nothing of the host's world is applied to
// it (DNA, food, cells, tribe, relations, time of day, NPCs), its game would save the changes.
// `coop.fix core.same_place off` = the old behaviour (tests).
bool g_place_fix = true;
COOP_FIX("core.same_place", "BUG-0130", g_place_fix, "core: nothing of the host's world is applied to a guest in another world");
uint32_t g_place_dropped = 0;
float g_dna_pending = -1; // the host's DNA total that arrived while we were elsewhere
// unknown: what to say before the host's first state arrived (just joined).
// The 0.7.3.0 predicate: kept for the off side of core.world_gate (the gate) only; every module
// asks host_world_now() / guest_elsewhere() / host_absent_for() below.
bool host_elsewhere(bool unknown)
{
    if (!g_place_fix || g_sess.role() != net::Role::Client || !stage_mode(g_mode))
        return false;
    auto it = g_remotes.find(0);
    if (it == g_remotes.end() || it->second.buf.empty())
        return unknown; // not known yet
    const auto& ls = it->second.buf.latest();
    return ls.mode == g_mode && !same_place(ls);
}

// BUG-0789: an adventure (kScenario) lights its planet itself: the time-of-day object's orientation is computed from
// the clock AND the captain's position (fork RE, docs/re/adventure.md#adventure-ga-fork), so the host's is wrong for
// another captain. The host sends no MSG_WORLD_TIME from an adventure and a guest in one applies none (the wsync sky
// row is not in play in kScenario either). `coop.fix wsync.adventure_sky off`: the legacy
// packet in an adventure as before.
static bool g_adventure_sky_fix = true;
COOP_FIX("wsync.adventure_sky", "BUG-0789", g_adventure_sky_fix, "wsync: in an adventure each game keeps its own sky (no host time of day sent or applied)");
bool adventure_sky_own(uint32_t mode) { return g_adventure_sky_fix && mode == modes::kScenario; }
// BUG-0231: a guest in another world of the host's Civ or Tribe stage applied the host's vehicle,
// city and alarm state to its own planet (bound and drove its own vehicles as mirrors, killed the
// rest, founded cities up to the host's count) and its Tribe/Civ AI and orders stood still
// (`local_play` followed only the host's main menu / editor). The Civ messages are dropped like the
// rest of the host's world, every gated message asks guest_elsewhere(), and Tribe and Civ play on
// locally while the guest is elsewhere. Off: as in 0.7.3.0.
bool g_world_gate_fix = true;
COOP_FIX("core.world_gate", "BUG-0231", g_world_gate_fix, "core: a guest in another world applies no Civ state and plays its tribe / civ itself");
// BUG-0420: the host's evolution vote reached a guest that plays another world (MSG_VOTE was not world-gated): its vote
// screen opened and, at the end, the evolution editor of the guest's own creature in its own world. Off: as 0.7.10.
bool g_vote_world_fix = true;
COOP_FIX("core.vote_world", "BUG-0420", g_vote_world_fix, "core: a guest in another world gets no vote screen and no editor from the host's vote");
uint32_t g_vote_elsewhere;
// BUG-0422: on a leave and a join every module registered in player_life.h forgets the id (ids are reused). Off: the
// five modules of 0.7.10 only.
bool g_player_forget_fix = true;
COOP_FIX("core.player_forget", "BUG-0422", g_player_forget_fix, "core: a player who joins under a departed player's id gets nothing of that player's state");
// C2 of 0.7.15 (BUG-1141, owner decision 2; docs/01-architecture.md#arch-world-id, #arch-c2c3-wiring): one world
// identity (world_id.h) stepped right after the stage counters; a change fires the lifecycle observers (lifecycle.h
// LIFECYCLE_HOOK, kWorldChange) with the WORLD_STATE resets at order 0 of that fan-out. An editor visit is no world
// change (the host away, C3). Before (0.7.14): the sum of the stage counters reset the WORLD_STATE entries, Tribe and
// Civ had their own detector (stage_epoch.h Watch), Space its own counter, and avatar_change_check reset the Creature
// modules on every change of the local entity, an editor visit included.
static uint32_t g_world_fires;
// the save the stage plays: a real planet file only ("snapshot.spo" with nothing remembered and the menu's name are
// not known: 0), as space_ext's world file (BUG-0225) and world_name.h
static uint32_t planet_file_now()
{
    return world_name::is_planet(modes::current_world_name()) ? modes::current_world() : 0;
}
// the stage's root object (Tribe: the player tribe, Civ: the player civilization, Space: the player cEmpire)
static const void* world_anchor(uint32_t mode)
{
    if (mode == modes::kTribe)
        return tribe::world_anchor();
    if (mode == modes::kCiv)
        return civ::world_anchor();
    if (mode == modes::kSpace)
        return spaceext::world_anchor();
    return nullptr;
}
static void world_id_tick()
{
    world_id::Frame f;
    f.mode = game::mode_id();
    if (stage_epoch::index_of(f.mode) >= 0) {
        f.count = game::stage_epoch(f.mode);
        f.save = f.mode == modes::kSpace ? spaceext::world_save() : planet_file_now();
        f.anchor = world_anchor(f.mode);
    }
    world_id::Tracker& t = world_id::tracker();
    world_id::Id from;
    world_id::Why why = world_id::Why::None;
    if (!t.step(f, &from, &why))
        return;
    g_world_fires++;
    LOG("world: %s -> %s (%s, BUG-1141)", from.open() ? modes::mode_name(from.mode) : "none",
        t.cur.open() ? modes::mode_name(t.cur.mode) : "none", world_id::why_name(why));
    lifecycle::Ctx c;
    c.event = lifecycle::kWorldChange;
    c.from_world = from.serial;
    c.to_world = t.cur.serial;
    c.from_stage = from.stage;
    c.to_stage = t.cur.stage;
    c.why = (uint8_t)why;
    lifecycle::fanout().state_resets = g_lifecycle_fix;
    lifecycle::fire(c, true); // (on_frame, outside g_sess.update(): game calls allowed)
}
std::string world_id_status()
{
    return std::string("world_id: ") + world_id::tracker().status() +
           " fires=" + std::to_string(g_world_fires) + "\n";
}

// "Is the host's world the one I play?" (BUG-0231, GitHub #165; host_world.h has the rule and its
// semantics). Recomputed on every call from this machine's g_mode / g_world, the host's latest
// PlayerState and, before the first one, the world the host named in WELCOME. Messages are handled
// inside g_sess.update(), before g_mode / g_world are refreshed: they see the previous frame's
// values (the first frame after a load still has kLoad, so NotGated). core.same_place off: NotGated.
static_assert(host_world::kCell == modes::kCell && host_world::kCreature == modes::kCreature &&
                  host_world::kTribe == modes::kTribe && host_world::kCiv == modes::kCiv,
              "host_world.h mode ids");
host_world::HostWorld host_world_now()
{
    if (!g_place_fix)
        return host_world::HostWorld::NotGated;
    bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    auto it = g_remotes.find(0);
    bool has = it != g_remotes.end() && !it->second.buf.empty();
    const net::PlayerState* ls = has ? &it->second.buf.latest() : nullptr;
    return host_world::classify(guest, g_mode, g_world, has, ls ? ls->mode : 0, ls ? ls->world : 0,
                                g_sess.welcome_world());
}

// this guest plays another world of the host's stage (or cannot tell yet): nothing of the host's
// world is applied here, nothing of this world goes to the host
bool guest_elsewhere() { return host_world_now() == host_world::HostWorld::Other; }

// BUG-0709: this guest plays another Space save than the host's (the host's save name from WELCOME); the shared
// Space money is held. `coop.fix res.space_save off`: applied there (0.7.10).
static bool g_space_save_fix = true;
COOP_FIX("res.space_save", "BUG-0709", g_space_save_fix, "space: a guest in a Space save of its own keeps its own money");
// BUG-0933: the host's save was the one named in WELCOME for the whole session: a guest that joined before the host
// loaded another save (or advanced into a save with another name) was "in a Space save of its own" from then on and
// never got the shared Space money (nor did its earnings reach the host). On: the guest follows the host's save
// (host_world.h host_save_now: the host's state in a gated stage, MSG_HOST_WORLD from the host when its world id
// changes and after a join). `coop.fix res.space_save_follow off`: the WELCOME value for good (0.7.11).
static bool g_save_follow_fix = true;
COOP_FIX("res.space_save_follow", "BUG-0933", g_save_follow_fix,
         "space: a guest that joined before the host changed its save shares the Space money again");
uint32_t g_host_save = 0;       // guest: the host's save heard or seen after WELCOME (0: none yet)
uint32_t g_world_announced = 0; // host: the world id last sent as MSG_HOST_WORLD
uint32_t g_host_save_heard = 0; // tests: MSG_HOST_WORLD taken
uint32_t host_save_known()
{
    return g_save_follow_fix && g_host_save ? g_host_save : g_sess.welcome_world();
}
// once per frame, before the session's update (a WELCOME and the announcement after it come inside the update)
static void host_save_tick()
{
    if (g_sess.role() == net::Role::Host) {
        g_host_save = 0;
        if (g_save_follow_fix && host_world::announce_world(g_sess.world, g_world_announced)) {
            net::Writer w;
            w.u32(g_sess.world);
            g_sess.broadcast_reliable(net::MSG_HOST_WORLD, w);
            g_world_announced = g_sess.world;
        }
        return;
    }
    g_world_announced = 0;
    if (g_sess.role() != net::Role::Client || !g_sess.connected()) {
        g_host_save = 0; // the next session starts with its WELCOME
        return;
    }
    auto it = g_remotes.find(0);
    if (it != g_remotes.end() && !it->second.buf.empty()) {
        const auto& ls = it->second.buf.latest();
        g_host_save = host_world::host_save_now(g_host_save, true, ls.mode, ls.world);
    }
}
bool guest_space_other()
{
    if (!g_space_save_fix)
        return false;
    bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    auto it = g_remotes.find(0);
    bool has = it != g_remotes.end() && !it->second.buf.empty();
    return host_world::space_other(guest, g_mode, g_mode == modes::kSpace ? modes::current_world() : 0, has,
                                   has ? it->second.buf.latest().mode : 0, host_save_known());
}

// BUG-1063: the player list's "other world" (co-op window rows, the status block) was !same_place(); in Space the place
// is a star or planet (0 in the galaxy view), so every partner at another star, and all of them while the own ship was
// in the galaxy view or respawning, "played another world". In Space it now means another Space save: known to a
// guest about the host (guest_space_other); other rows say nothing there. Off: !same_place() in Space too (0.7.13).
static bool g_other_world_fix = true;
COOP_FIX("core.ui_other_world", "BUG-1063", g_other_world_fix, "ui: in Space the player list says other world only for another Space save, not another star");
static bool other_world_label(uint8_t id, const net::PlayerState& ls)
{
    if (ls.mode != g_mode)
        return false;
    if (!g_other_world_fix || g_mode != modes::kSpace)
        return !same_place(ls);
    return id == 0 && guest_space_other();
}

// the module asking host_absent_for (npc runs in Creature and Tribe)
enum class Absent { Npc, Tribe, Civ };

// true when the host does not drive this module's shared objects on this guest now: the guest
// plays them itself (its own AI and orders). The away part (main menu, editor, load: BUG-0151,
// BUG-0233) is g_host_away; the elsewhere part is switched per module: npc by core.same_place
// (BUG-0130, as before), Tribe and Civ by core.world_gate.
bool host_absent_for(Absent who)
{
    bool away = g_host_away;
    bool elsewhere = guest_elsewhere() && (who == Absent::Npc ? g_place_fix : g_world_gate_fix);
    return away || elsewhere;
}

// O1 of 0.7.13 (own_world.h): the one answer to "is this guest in the host's world right now?" for the modules
static double g_host_left_since = -1;
} // namespace coop_core (own_world is called from the other modules)
namespace own_world {
bool elsewhere() { return guest_elsewhere(); }
bool space_other()
{
    bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    auto it = g_remotes.find(0);
    bool has = it != g_remotes.end() && !it->second.buf.empty();
    return host_world::space_other(guest, g_mode, g_mode == modes::kSpace ? modes::current_world() : 0, has,
                                   has ? it->second.buf.latest().mode : 0, host_save_known());
}
bool host_left()
{
    bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    auto it = g_remotes.find(0);
    bool has = it != g_remotes.end() && !it->second.buf.empty();
    bool now_left = host_world::host_in_other_stage(guest, g_mode, has, has ? it->second.buf.latest().mode : 0);
    double t = H->time_seconds();
    if (!now_left) {
        g_host_left_since = -1;
        return false;
    }
    if (g_host_left_since < 0)
        g_host_left_since = t;
    return t - g_host_left_since >= 2.0;
}
static const net::PlayerState* peer_state(uint8_t id)
{
    auto it = g_remotes.find(id);
    return it == g_remotes.end() || it->second.echo || it->second.buf.empty() ? nullptr : &it->second.buf.latest();
}
bool peer_here(uint8_t id)
{
    const net::PlayerState* s = peer_state(id);
    return host_world::peer_here(s != nullptr, s ? s->mode : 0, s ? s->world : 0, g_mode, g_world);
}
bool peer_elsewhere(uint8_t id)
{
    const net::PlayerState* s = peer_state(id);
    return host_world::peer_elsewhere(s != nullptr, s ? s->mode : 0, s ? s->world : 0, g_mode, g_world);
}
} // namespace own_world
namespace coop_core {

// a message of the host's world state a guest elsewhere must not apply (MSG_RES is held inside
// shared.cpp instead: res.elsewhere). BUG-0420: one class per message type (host_world.h msg_class, a
// build error on an unlisted MsgType); a type this build does not know is dropped too (default deny).
// `coop.fix core.msg_classes off`: unknown types pass as in 0.7.10.
static bool g_msg_classes_fix = true;
COOP_FIX("core.msg_classes", "BUG-0420", g_msg_classes_fix, "core: a guest elsewhere drops every host message of an unknown type (default deny)");
static bool world_state_msg(uint8_t type)
{
    return host_world::world_state_msg(type, g_msg_classes_fix, g_world_gate_fix);
}

// the gate: does this machine drop the host's message? (0.7.3.0 with core.world_gate off: the old
// predicate, "unknown" counted only for the DNA total)
bool gate_drops(uint8_t type)
{
    if (!world_state_msg(type))
        return false;
    return g_world_gate_fix ? guest_elsewhere() : host_elsewhere(type == net::MSG_DNA);
}

void host_away_tick(double now)
{
    if (stage_mode(g_mode) && g_world)
        g_stage_world = g_world;
    if (g_dna_owed > 0 && g_sess.role() == net::Role::Host) {
        auto o = dna_route::owed(g_away_dna_stage_fix, g_mode, stage_mode(g_mode), game::avatar() != nullptr, g_world,
                                 g_dna_owed_world, modes::kCreature);
        if (o == dna_route::Owed::Pay) {
            dna_host_gain({dna_ledger::Cause::Owed, 0, g_dna_owed}, game::dna());
            game::dna_add_raw(g_dna_owed);
            feed("the DNA your partners earned meanwhile: +" + std::to_string((int)g_dna_owed));
            g_dna_owed = 0;
            g_dna_owed_paid++;
        } else if (o == dna_route::Owed::Clear) { // BUG-0233: another stage or world
            LOG("owed DNA %.1f dropped: owed in world %08X, now mode %08X world %08X", g_dna_owed, g_dna_owed_world,
                g_mode, g_world);
            g_dna_owed = 0;
            g_owed_dropped++;
        }
    }
    if (g_dna_spend_owed < 0 && g_sess.role() == net::Role::Host && g_mode == modes::kCreature && game::avatar()) {
        float d = std::max(g_dna_spend_owed, -game::dna()); // BUG-0466: never below 0
        dna_host_gain({dna_ledger::Cause::Earned, 0, d}, game::dna());
        game::dna_add_raw(d);
        g_dna_spend_owed = 0;
    }
    if (g_dna_pending >= 0 && g_sess.role() == net::Role::Client && host_here() && game::avatar() && !g_host_away) {
        float d = g_dna_pending - game::dna(); // the host's total, once we play its world
        if (d != 0 && g_own_heard && g_dna_bar_set_fix)
            game::dna_set_raw(g_dna_pending); // WP-D: our own DNA (the bar follows the host's next total)
        else if (d != 0)
            game::dna_add_raw(d);
        g_dna_pending = -1;
        g_dna_rx++;
    }
    // away: in the main menu for 1.5 s (not a passing state between two worlds); back: in our world
    // with its creature for 1 s (a guest gain sent while the host loads is dropped there)
    if (g_host_away_fix && g_host_away && !g_sess.connected()) {
        // BUG-0085: the session ended while the host was away: it is not back (the guest was told
        // "player0 (host) is back: your N DNA go to the pack" and nothing was sent)
        g_host_away = false;
        g_host_away_since = -1;
        g_solo_dna = 0;
        g_solo_mode = 0;
        return;
    }
    // an editor or a load counts after 4 s: a host passing through the loading screen between two
    // places, or the shared editor of a vote opening a moment later here, is not away
    char why = g_host_away ? 0 : host_out_of_stage();
    bool change = g_host_away ? (host_here() && !g_away_test) || !g_sess.connected() || !stage_mode(g_mode) : why != 0;
    if (!change)
        g_host_away_since = -1;
    else if (g_host_away_since < 0)
        g_host_away_since = now;
    double wait = g_host_away ? 1.0 : why == 'm' ? 1.5 : 4.0;
    bool away = change && now - g_host_away_since > wait ? !g_host_away : g_host_away;
    if (away == g_host_away)
        return;
    g_host_away_since = -1;
    g_host_away = away;
    std::string host = g_sess.name_of(0);
    if (away) {
        g_host_away_count++;
        auto hi = g_remotes.find(0);
        if (hi != g_remotes.end() && !hi->second.buf.empty() && (hi->second.buf.latest().flags & net::PF_RESYNC)) {
            // BUG-0332: the host's one reload of the new Tribe world
            feed(host + " (host) is loading the new world once, so that everybody has the same ground (about 10 s)");
            return;
        }
        const char* where = why == 'e' ? "is in an editor" : why == 'l' ? "is loading" : "left for the main menu";
        feed(host + " (host) " + where + ": you play on alone until " + host + " is back");
        return;
    }
    auto it = g_remotes.find(0);
    bool has = it != g_remotes.end() && !it->second.buf.empty();
    const net::PlayerState* hs = has ? &it->second.buf.latest() : nullptr;
    auto end = dna_route::solo_end(g_away_dna_stage_fix, host_here(), hs && (hs->flags & net::PF_IN_WORLD),
                                   hs ? hs->mode : 0, g_solo_mode);
    if (end != dna_route::SoloEnd::Drop && g_solo_dna > 0 && g_sess.connected()) {
        net::Writer w;
        w.f32(g_solo_dna);
        g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
        g_dna_fwd++;
        if (g_solo_await_fix) { // BUG-0829: the host's totals until the one that pays it are older than it
            g_solo_await = g_solo_dna;
            g_solo_await_until = now + kSoloAwait;
        }
    }
    std::string dna = g_solo_dna >= 1 ? ": your " + std::to_string((int)g_solo_dna) + " DNA go to the pack" : "";
    if (end == dna_route::SoloEnd::SendBack)
        feed(host + " (host) is back" + dna);
    else if (end == dna_route::SoloEnd::Send)
        feed(host + " (host) plays again" + dna);
    else { // BUG-0233: the host did not come back to this stage (it moved on, or we did)
        LOG("host away ended without its return (host mode %08X): solo DNA %.1f of mode %08X dropped",
            hs ? hs->mode : 0, g_solo_dna, g_solo_mode);
        if (g_solo_dna > 0)
            g_solo_dropped++;
    }
    g_solo_dna = 0;
    g_solo_mode = 0;
}

// Network keepalive without frames: a modal game dialog (e.g. "game saved") can stop the frames
// while the window still pumps messages; the session and the world transfer then keep running
// from the window procedure, so guests do not time out.
std::atomic<double> g_last_frame{0};
std::atomic<bool> g_keepalive_run{false}, g_keepalive_queued{false};
// BUG-0107: a joinable std::thread in a global aborted the game at exit (~thread() calls
// std::terminate when the process ends through ExitProcess without cSporeApp::Shutdown, so
// on_unload never joined it). The thread is held through a pointer whose object is only destroyed
// after a join; at process exit nothing is destroyed. `coop.fix core.keepalive_ptr off`: the old
// global object (tests: coop.exittest then aborts).
bool g_keepalive_ptr_fix = true;
COOP_FIX_UNSAFE("core.keepalive_ptr", "BUG-0107", g_keepalive_ptr_fix, "core: the keepalive thread cannot abort the game at exit");
std::thread* g_keepalive = nullptr;
std::thread g_keepalive_old; // the old way, only with the fix off

// BUG-0687 diagnostics: the host's frames stopped for good right after a guest left while this task still ran (the
// main thread sat in a nested message loop that was not the game's: no Canvas::Tick, no Present) and the watchdog's
// dump 30 s later never reached the log. Once per stall, 3 s in, the main thread's own call chain (read on the main
// thread itself, as SporeKit's stack.here: no suspend) and its windows (a #32770 is a dialog) go to the log.
static double g_stall_logged_for = -1;
static BOOL CALLBACK stall_window(HWND w, LPARAM l)
{
    auto* out = (std::string*)l;
    char cls[64] = "", title[64] = "";
    GetClassNameA(w, cls, sizeof cls);
    GetWindowTextA(w, title, sizeof title);
    if (out->size() < 400)
        *out += std::string(" [") + cls + " '" + title + "' visible=" + (IsWindowVisible(w) ? "1" : "0") + "]";
    return TRUE;
}
static void stall_note(double since)
{
    if (g_stall_logged_for == since)
        return;
    g_stall_logged_for = since;
    std::string chain;
    uint8_t* teb = (uint8_t*)NtCurrentTeb();
    uint32_t* hi = *(uint32_t**)(teb + 4);
    volatile uint32_t marker = 0;
    HMODULE exe = GetModuleHandleW(nullptr);
    auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
    uintptr_t lo = (uintptr_t)exe + nt->OptionalHeader.BaseOfCode, top = lo + nt->OptionalHeader.SizeOfCode; // (code only: no text)
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    int n = 0, nu = 0;
    for (uint32_t* p = (uint32_t*)&marker; p < hi && n < 24; p++) {
        uint32_t v = *p;
        char b[24];
        const uint8_t* c = (const uint8_t*)(uintptr_t)v;
        if (v > lo + 6 && v < top && (c[-5] == 0xE8 || c[-6] == 0xFF || c[-3] == 0xFF || c[-2] == 0xFF)) { // a return address
            snprintf(b, sizeof b, " %08X", v);
            chain += b;
            n++;
        } else if (u32 && nu < 4 && v > (uintptr_t)u32 && v < (uintptr_t)u32 + 0x200000) {
            snprintf(b, sizeof b, " user32+%X", (unsigned)(v - (uintptr_t)u32));
            chain += b;
            nu++;
        }
    }
    std::string wins;
    EnumThreadWindows(GetCurrentThreadId(), stall_window, (LPARAM)&wins);
    LOG("keepalive: no frame for %.1f s, the main thread runs a message loop (BUG-0687): chain%s", H->time_seconds() - since,
        chain.c_str());
    LOG("keepalive: the main thread's windows%s", wins.c_str());
}

void keepalive_task(void*)
{
    g_keepalive_queued = false;
    if (H->time_seconds() - g_last_frame < 0.5 || g_sess.role() == net::Role::None)
        return;
    if (H->time_seconds() - g_last_frame > 3.0)
        stall_note(g_last_frame);
    g_sess.update();
    if (g_sess.role() == net::Role::Host)
        world::tick(g_sess);
}

void keepalive_start()
{
    g_keepalive_run = true;
    auto loop = [] {
        while (g_keepalive_run) {
            Sleep(100);
            if (!g_keepalive_queued && H->time_seconds() - g_last_frame > 0.5 && g_last_frame > 0) {
                g_keepalive_queued = true;
                H->run_outside_frame(keepalive_task, nullptr);
            }
        }
    };
    if (g_keepalive_ptr_fix)
        g_keepalive = new std::thread(loop);
    else
        g_keepalive_old = std::thread(loop);
}

void keepalive_stop()
{
    g_keepalive_run = false;
    if (g_keepalive && g_keepalive->joinable())
        g_keepalive->join();
    delete g_keepalive;
    g_keepalive = nullptr;
    if (g_keepalive_old.joinable())
        g_keepalive_old.join();
}

void menu_tick(double now);

// ---- diagnostics (SporeKit 0.3.4+: crash / problem reports, docs/02-modkit.md 2.7) ----
// the co-op state for a report, built on the frame (a crash report must not read game objects):
// two buffers, the report reads the last complete one. BUG-1022: a report thread that took the pointer and was
// preempted for 2 s+ read a buffer the main thread was assigning again: the buffers change under g_diag_mu and the
// report gets a copy of its own (per thread), made under it
static std::string g_diag_buf[2];
static std::atomic<int> g_diag_cur{0};
static std::mutex g_diag_mu;
static double g_diag_t = -100;
static bool g_fault_told;

static bool host_has_diag() { return H->size >= offsetof(SkHost, logs_dir) + sizeof(void*) && H->diag_report; }

// the previous game crashed / hung (SporeKit wrote its archive then): tell where the file to send is
static void last_problem_tick(double now)
{
    static double first = -1;
    static bool done;
    if (done)
        return;
    if (first < 0)
        first = now;
    if (now - first < 8) // the menu is up by then
        return;
    done = true;
    if (H->size < offsetof(SkHost, last_problem) + sizeof(void*) || !H->last_problem)
        return;
    std::string p = H->last_problem();
    if (p.empty())
        return;
    std::string what = p.substr(0, p.find('\n')), zip = p.substr(p.find('\n') + 1);
    zip = zip.substr(zip.find_last_of("\\/") + 1);
    feed("SporeCoop: the last game " + what + ". A report to send is ready: mods\\logs\\" + zip +
         " (F9, \"Report a problem\" opens the folder)");
    chat_log("* the last game " + what + ": mods\\logs\\" + zip);
}

static void diag_tick(double now)
{
    last_problem_tick(now);
    if (now - g_diag_t < 2.0)
        return;
    g_diag_t = now;
    char b[160];
    snprintf(b, sizeof b, "SporeCoop " COOP_VERSION ", name '%s', game mode 0x%08X, world 0x%08X\n", g_name.c_str(),
             game::mode_id(), g_world);
    std::string s = b;
    s += g_sess.status() + world::status();
    s += "busy: " + busy::names(game_busy()) + "\n"; // BUG-0404: the busy gate
    s += host_busy_status(); // C3 (BUG-1142)
    for (auto& [id, r] : g_remotes)
        s += "remote " + std::to_string(id) + " '" + r.name + "' puppet " + (r.puppet ? "yes" : "no") + "\n";
    size_t first = g_chat.size() > 8 ? g_chat.size() - 8 : 0;
    for (size_t i = first; i < g_chat.size(); i++)
        s += "chat: " + g_chat[i] + "\n";
    std::lock_guard<std::mutex> lk(g_diag_mu);
    int next = 1 - g_diag_cur.load();
    g_diag_buf[next] = std::move(s);
    g_diag_cur = next;
}

static const char* diag_context_fn(void*)
{
    thread_local std::string copy; // valid until this thread's next call (SporeKit copies it at once)
    std::lock_guard<std::mutex> lk(g_diag_mu);
    copy = g_diag_buf[g_diag_cur.load()];
    return copy.c_str();
}

// a fault inside SporeKit or coop.dll was caught (SK_EV_FAULT): tell the player once per run where
// the report is, a mod bug is behind it
static void on_fault(void*, int, void* arg)
{
    support::on_fault();
    H->log(M, SK_WARN, "a fault in the mod's code was caught: %s", arg ? (const char*)arg : "?");
    if (g_fault_told)
        return;
    g_fault_told = true;
    feed("SporeCoop caught an error in its own code (logged). If something goes wrong: F9, \"Report a problem\"");
}

std::string make_report(const char* reason)
{
    if (!host_has_diag())
        return "";
    g_diag_t = -100;
    diag_tick(H->time_seconds()); // the state right now
    if (!support::shot_setting()) // switched off since the screenshots were taken
        support::report_shots_forget();
    char path[MAX_PATH * 2] = {0};
    H->diag_report(reason, path, sizeof path);
    return path;
}

// the local player's entity changed (a load, the editor, the stage left): the puppets and proxies
// belong to the old world
static_assert(modes::kTribe == stage_epoch::kTribe && modes::kCiv == stage_epoch::kCiv &&
                  modes::kEditor == stage_epoch::kEditor && modes::kEdit == stage_epoch::kEdit &&
                  modes::kSpace == stage_epoch::kSpace && modes::kScenario == stage_epoch::kScenario &&
                  modes::kCell == stage_epoch::kCell && modes::kCreature == stage_epoch::kCreature,
              "stage_epoch.h mode ids");
// C2 (BUG-1141): a world change fires the modules' hooks (LIFECYCLE_HOOK kWorldChange: the
// puppets, npc, nest, fields, wobj); this check is no world detector any more. It covers what world_id.h does not
// see: the local creature replaced inside the same world (the co-op window's own creature, BUG-0833) and the stage
// torn down before the mode changes (BUG-0091, BUG-0308). Decision 2: the local player's editor visit (the local
// entity gone in an editor mode, and back from it) keeps the world: only the puppets are forgotten (frame_drive does
// the same without a local entity), the NPC proxies, nest mirrors, fields and world objects stay.
static uint32_t g_world_fires_seen; // the world fires this check saw (a change and its hooks this frame: no second reset)
void avatar_change_check()
{
    uint32_t mode = game::mode_id();
    const modes::Adapter* a = modes::for_mode(mode);
    void* local = a ? a->local() : nullptr;
    bool fired = g_world_fires != g_world_fires_seen; // the hooks ran for a world change this frame
    g_world_fires_seen = g_world_fires;
    if (local == g_last_avatar)
        return;
    if (g_last_avatar) {
        bool editor = mode == modes::kEdit || mode == modes::kEditor;
        forget_puppets();
        if (!fired && !(editor && !local)) {
            npc::on_world_change();
            nest::on_world_change();
            fields::on_world_change();
            wobj::on_world_change();
        }
    }
    g_last_avatar = local;
}

// Tests: fix switches that act while the game starts (before a console can switch them, e.g. BUG-0506,
// BUG-0506) are switched off by a file <AppData>\Spore\SporeCoop\fixes_off.txt of the profile, one name per
// line, read once when the mod loads. The file is the test's (coop_save_areas.py writes and removes it).
static void apply_startup_off()
{
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) // profile-aware (hooked)
        return;
    FILE* f = _wfopen((std::wstring(p) + L"\\Spore\\SporeCoop\\fixes_off.txt").c_str(), L"r");
    if (!f)
        return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        std::string n(line);
        while (!n.empty() && (n.back() == '\n' || n.back() == '\r' || n.back() == ' '))
            n.pop_back();
        if (n.empty() || n[0] == '#')
            continue;
        bool ok = fix::set(n, false);
        H->log(M, SK_WARN, "coop: fix %s switched off at startup (fixes_off.txt)%s", n.c_str(), ok ? "" : ": unknown name");
    }
    fclose(f);
}

static void join_apart_tick(double now);
// One frame of the mod (B2 of 0.7.15): on_frame runs these phases in this order; the order is part of several fixes
// (docs/01-architecture.md#arch-start-and-frame).

// on_frame, phase: before any message: clocks, the stage counters and world changes (BUG-0217), the puppets, proxies and bodies as drawn (BUG-0091)
static void frame_observe(double now)
{
    g_last_frame = now;
    support::tick(now); // first: its frame timing (FPS, hitches) and the session clock
    {
        bool clock_ok = false;
        double st = support::session_time(&clock_ok);
        squadron::frame_start(st, clock_ok); // BUG-0378 tests: the ships as drawn, before any write of ours
    }
    diag_tick(now);
    notify::tick(now);
    gamefixes::frame(); // BUG-0506: once
    game::world_epoch(); // BUG-0119: sees every frame outside the creature stage
    game::stage_epoch_tick(); // BUG-0217: before any message or module tick of this frame
    modes::world_name_tick(); // BUG-0258: the planet stays known through snapshot.spo
    modes::test_tick();       // BUG-0223 tests: a forced frame without the local entity
    world_id_tick();          // BUG-1141 (BUG-0217, BUG-0223, BUG-1055): the world changed: its hooks and WORLD_STATE resets
    creations::tick(now);
    species::tick(now); // WP-0 (BUG-1270): the species line (after the creations: it reads their keys)
    designs::tick(now); // BUG-1272: a guest's own outfit to its species line
    civedit::tick(now); // WP-C3 (BUG-1275): a guest's own Civ editors' results into its line
    if (g_frame_fix) // BUG-0091: forget the puppets of a world the game tore down before any message uses them
        avatar_change_check();
    monitor_frame_start(now); // the puppets as drawn, before any driving (docs/06-testing.md#testing-monitor)
    npc::monitor_tick(now);   // the proxies as drawn
    bodyrec_tick(now);        // coop.bodyrec (tests, BUG-0425): every body as drawn
    modes::own_ground_tick(now); // BUG-0592: the own creature not left in the ground
}

// on_frame, phase: the session's messages, which see the last frame's g_mode / g_world / g_local (BUG-0231)
static void frame_receive(double now)
{
    // BUG-0231: the world a joining guest hears in WELCOME (before the host's first state); the
    // values of the last frame, as every message of this update sees them. Not switched: 0.7.3.0
    // guests never read it
    if (g_sess.role() == net::Role::Host)
        g_sess.world = host_world::welcome_world(g_mode, g_world, g_mode == modes::kSpace ? modes::current_world() : 0);
    g_sess.mode = g_mode; // B6 (BUG-1137): the stage whose message families a joining guest must match
    host_save_tick(); // BUG-0933: the host says its save again when it changed; a guest follows it
    g_sess.update();
    partner_pass_tick(); // BUG-0913: what the collision and hover hooks may skip this frame
    lobby_tick(now);
}

// on_frame, phase: the guest's latency for the drive, the votes, the editors, the co-op window and the menus
static void frame_votes_and_ui(double now)
{
    if (g_sess.role() == net::Role::Client) { // BUG-0425 (drive.lead): the one-way latency to the host, half its round trip
        int rtt = g_sess.rtt_of(0);
        if (rtt >= 0)
            net::g_one_way = std::min(std::max(rtt / 2000.0, 0.0), 0.15);
        // BUG-0651 (drive.lead_low): the link's own latency, half the round trip's lower quartile
        const float q = g_sess.rtt_quantile_of(0, 0.25f);
        net::g_one_way_low = q >= 0 ? std::min(std::max(q / 2000.0, 0.0), 0.3) : -1.0;
        static double s_rtt_log = 0;
        if (q >= 0 && now - s_rtt_log >= 10.0) { // (tests, support: what the no-lead rule reads)
            s_rtt_log = now;
            LOG("net: round trip p10 %.0f p25 %.0f p50 %.0f p90 %.0f ms, mean %d ms (BUG-0651)", g_sess.rtt_quantile_of(0, 0.1f),
                q, g_sess.rtt_quantile_of(0, 0.5f), g_sess.rtt_quantile_of(0, 0.9f), rtt);
        }
    } else
        net::g_one_way_low = -1;
    vote_tick(now);
    submit_tick(now);
    own_editor_tick(now);
    civ_arrival_tick(now); // WP-X1 (BUG-1276)
    evo_flag_tick(now); // BUG-0920
    offer_tick(now); // BUG-0516
    vote_action_tick(now);
    coop_ui::tick();
    menu_tick(now);
}

// on_frame, phase: the host's world (share, save strip) and the modules that tick before the mode is read again
static void frame_world_ticks(double now)
{
    if (g_sess.role() == net::Role::Host)
        world::tick(g_sess);
    else
        world::guest_tick(); // a received world waiting for the player to leave its editor
    savestrip::tick();
    npc::tick(now);
    player_rules::tick(now); // BUG-0558: the host's UFO on the guests
    nest::tick(now); // BUG-0384
    fields::tick(now); // BUG-0419 G2 (after the modules that make the mirrors)
    wsync::tick(now);  // W1: host-owned world state (time of day, relations, stages done, the Tribe goal)
    wobj::tick(now); // BUG-0472, BUG-0459
    realign::tick(now); // W2: the whole planet after transitions (BUG-0564)
    parts::tick(now); // BUG-0401
    effects::tick(now); // BUG-0451: sends the queued effects on the main thread
    social::tick(now);
    evo_key_tick(now);
    creature_hp::tick();
    shared::tick(g_sess, game::mode_id(), now, guest_elsewhere() || guest_space_other()); // BUG-0231, BUG-0709: held while elsewhere
}

// on_frame, phase: host: the DNA total (on a change, every 2 s) and the time of day (every 1 s)
static void frame_send_totals(double now)
{
    if (g_sess.role() == net::Role::Host && game::avatar()) {
        own_dna_credit_now(); // BUG-1341: this frame's party gain is in every own wallet before the totals go out
        float total = game::dna();
        // WP-D (BUG-1291): sent per guest: a guest with an own wallet gets its own budget (dna_own.h), the others the
        // host's (the shared pool); the tail (after the pace byte: older guests stop before it) names which
        std::vector<std::pair<uint8_t, float>> to;
        std::vector<dna_own::Tail> tails;
        bool due = total != g_dna_sent || (cellsync::totals_resend_enabled() && now - g_dna_at > 2.0);
        for (auto& p : g_sess.players()) {
            if (p.id == 0)
                continue;
            dna_own::Tail t;
            float mine = dna_total_for(p.id, total, t);
            auto it = g_dna_sent_to.find(p.id);
            due = due || it == g_dna_sent_to.end() || it->second != mine;
            to.push_back({p.id, mine});
            tails.push_back(t);
        }
        // also every 2 s: a guest drops it while it has no avatar (loading, editor), BUG-0128
        if (due) {
            g_dna_sent = total;
            g_dna_at = now;
            for (size_t i = 0; i < to.size(); i++) {
                net::Writer w;
                w.f32(to[i].second);
                // (optional tail, older guests read the total only) why it changed since the last one
                w.u8((uint8_t)g_dna_out.size());
                for (auto& c : g_dna_out) {
                    w.u8((uint8_t)c.cause);
                    w.u8(c.player);
                    w.f32(c.amount);
                }
                w.u8((uint8_t)dna_pace_announce()); // BUG-0510 (after the causes: older guests stop there)
                tails[i].write(w);                  // WP-D (after the pace byte: 0.7.15 guests stop there)
                g_sess.send_reliable_to(to[i].first, net::MSG_DNA, w);
                g_dna_sent_to[to[i].first] = to[i].second;
                if (tails[i].own())
                    g_own_totals++;
            }
            g_dna_out.clear();
        }
    }
    if (g_sess.role() == net::Role::Host && now - g_last_time_sync > 1.0) {
        g_last_time_sync = now;
        net::Quat q;
        if (adventure_sky_own(game::mode_id())) {
            // BUG-0789: an adventure's sky is its own (no MSG_WORLD_TIME from an adventure)
        } else if (game::time_of_day(q)) {
            net::Writer w;
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            for (auto& p : g_sess.players())
                if (p.id != 0)
                    g_sess.send_unreliable_to(p.id, net::MSG_WORLD_TIME, w);
        }
    }
}

// on_frame, phase: this frame's g_mode (on_mode_change), g_adapter, g_world, g_local; settings, HUD helpers, the second avatar check
static void frame_local_update(double now)
{
    uint32_t prev_mode = g_mode;
    g_mode = game::mode_id();
    // the effect hooks are installed once (game::init); this only gates their work to the played
    // stages (a hook removed at a stage change faulted the UI skinner's render thread, BUG-0451 review)
    game::set_world_relay(g_mode == modes::kCell || g_mode == modes::kCreature || g_mode == modes::kTribe
                          || g_mode == modes::kCiv || g_mode == modes::kSpace);
    if (g_mode != prev_mode)
        on_mode_change(prev_mode, g_mode);
    g_adapter = modes::for_mode(g_mode);
    g_world = modes::current_place(g_adapter);
    g_local = g_adapter ? g_adapter->local() : nullptr;
    settings::tick(); // F3: the host's world opened or closed (its stored settings)
    store::tick();    // E1 (BUG-1149): the host's world opened: its per-player store
    autosave::tick(); // E2 (BUG-0864): the host's autosave (the save itself outside the frame)
    g_sess.max_players = settings::get(settings::kPlayerLimit); // F3 (BUG-0768): the host's join rules
    pfind::tick(H->time_seconds()); // F2: the partners on the minimap (outside the frame)
    portrait::tick(H->time_seconds()); // BUG-0937: the HUD portrait follows the avatar's species (outside the frame)
    pfind::hud_tick(H->time_seconds()); // BUG-0949: the UI windows the labels keep off (outside the frame)
    sreport::tick();  // F4: the session report's round (host) and the guest's question / transfer
    game::leave_probe_tick(g_local); // BUG-0264 tests: an avatar used while the stage is torn down
    modes::cell_probe_tick();        // BUG-0271: the cell pool freed while the mode reads Cell
    void* av = game::avatar();
    avatar_change_check();
    join_apart_tick(now); // BUG-0989

    (void)av;
}

// on_frame, phase: strays, the host's phase (follow_phase), the share wait, the away state, DNA, the stage modules
static void frame_stage_ticks(double now)
{
    remove_strays(now);
    follow_phase(now);
    share_wait_tick();
    own_dna_tick(); // WP-D (BUG-1291): before the away state (an owed payment of this frame is seen in the next)
    host_away_tick(now);
    host_busy_tick(now); // C3 (BUG-1142): the host tells its pause; a guest's game waits for it
    guest_mating_tick(now); // BUG-1305: a guest's mating whose mate is a host copy ends in its editor
    dna_spend_tick(now); // BUG-0466
    dna_notices_tick(now);
    tribe::tick(now);
    bool walk_done = !gamefixes::leave_walk_armed();
    if (!walk_done)
        tribe::for_each_owned([](void* c, uint8_t, void* done) { // BUG-0310 tests (coop.leavewalk)
            if (!*(bool*)done)
                *(bool*)done = gamefixes::leave_walk_tick(c);
        }, &walk_done);
    cellsync::tick(now);
    civ::tick(now);
    civ::planner_probe_tick(); // BUG-0682 tests (off unless `coop.civ planner_probe on`)
    squadron::tick(now);
    autoupdate::tick();
    spacecoop::tick(now);
    if (g_sess.connected() && g_mode == modes::kCreature)
        teamplay::tick(game::avatar(), now);
}

// on_frame, phase: our player state (net::state_due: every 0.05 s, a stop at once) and the echo test remote
static void frame_send_state(double now)
{
    // BUG-1093 (net.send_phase): every 0.05 s with the schedule's phase kept, a stop at once (net::state_due)
    float sp_now = 0;
    if (g_sent_speed > 1.f && g_mode == modes::kCreature)
        if (void* l = local_now())
            sp_now = drive::speed(game::velocity(l));
    if (net::state_due(now, g_last_send, g_mode == modes::kCreature ? g_sent_speed : 0.f, sp_now)) {
        net::PlayerState st = local_state();
        g_sent_speed = drive::speed(st.vel);
        if (g_sess.connected())
            g_sess.send_state(st);
        if (g_echo && g_local) {
            // fake remote: our own state, shifted sideways, delayed
            st.id = 200;
            st.time_ms = (uint32_t)(now * 1000.0);
            st.mode = g_mode;
            st.world = g_world;
            net::Quat q = st.rot;
            // local "right" vector from the orientation quaternion (x axis)
            float rx = 1 - 2 * (q.y * q.y + q.z * q.z), ry = 2 * (q.x * q.y + q.w * q.z),
                  rz = 2 * (q.x * q.z - q.w * q.y);
            st.pos.x += rx * g_echo_offset;
            st.pos.y += ry * g_echo_offset;
            st.pos.z += rz * g_echo_offset;
            g_echo_q.emplace_back(now + g_echo_delay, st);
        }
    }
    while (!g_echo_q.empty() && g_echo_q.front().first <= now) {
        auto& r = g_remotes[200];
        r.id = 200;
        r.name = "Echo";
        r.echo = true;
        r.buf.push(g_echo_q.front().second, now);
        g_echo_q.pop_front();
    }
}

// on_frame, phase: every partner's puppet driven (or forgotten without a local entity), then the recorders
static void frame_drive(double now)
{
    for (auto& [id, r] : g_remotes) {
        if (!g_local) {
            if (r.puppet)
                forget_puppets();
            continue;
        }
        drive_puppet(r, now);
    }
    cocoon::tick(now); // WP-K (BUG-1303): the cocoon's shells and bursts that ended are released
    track_tick(now);
    trace_tick(now);
}

void on_frame(void*, int, void*)
{
    modes::CellScope cell_scope; // BUG-0271: no game code runs in here, the pool check holds for it
    game::LiveScope live_scope;  // BUG-0426: the object list of this frame (game::listed) holds in here
    double now = H->time_seconds();
    frame_observe(now);
    frame_receive(now);
    frame_votes_and_ui(now);
    frame_world_ticks(now);
    frame_send_totals(now);
    frame_local_update(now);
    frame_stage_ticks(now);
    frame_send_state(now);
    frame_drive(now);
}

// Arial 18 bold, approx.: letter cells, not bytes (BUG-0159: a Cyrillic name was centred as twice
// its width, every arrow label moved by its 3-byte arrow)
int text_width(const std::string& s) { return utf8::overlay_width(s); }

// world marks: "look here" pings and beacons of the stage modules (a city under attack, a
// partner's ship ...): a label at the world point, an arrow on the screen edge when it is off screen
struct Mark {
    std::string text;
    net::Vec3 pos;
    uint32_t mode;
    double until;
    uint32_t argb;
};
std::vector<Mark> g_marks;

void add_mark(uint8_t from, const net::Vec3& p, uint32_t mode)
{
    g_marks.push_back({"[ ! ] " + g_sess.name_of(from), p, mode, H->time_seconds() + 6.0, 0xFFFFD040});
    feed(g_sess.name_of(from) + " pings a place");
}

void add_beacon(const net::Vec3& p, const std::string& text, double seconds, uint32_t argb)
{
    for (auto& m : g_marks) // the same beacon again: refresh it (it may follow a moving target)
        if (m.text == text || (m.argb == argb && net::dist(m.pos, p) < 1.f && m.text.substr(0, 6) == text.substr(0, 6))) {
            m.text = text;
            m.pos = p;
            m.until = H->time_seconds() + seconds;
            return;
        }
    g_marks.push_back({text, p, g_mode, H->time_seconds() + seconds, argb});
}

void draw_marks()
{
    double now = H->time_seconds();
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    for (size_t i = 0; i < g_marks.size();) {
        if (g_marks[i].until < now) {
            g_marks.erase(g_marks.begin() + i);
            continue;
        }
        const Mark& m = g_marks[i++];
        if (m.mode != g_mode)
            continue;
        float w[3] = {m.pos.x, m.pos.y, m.pos.z}, sc[2] = {0, 0};
        bool blink = ((int)(now * 4)) & 1;
        uint32_t col = blink ? m.argb : (m.argb & 0xFF000000) | ((m.argb >> 1) & 0x7F7F7F);
        bool front = H->world_to_screen(w, sc) != 0;
        if (front && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh) {
            H->overlay_text((int)sc[0] - text_width(m.text) / 2, (int)sc[1] - 10, col, m.text.c_str());
            continue;
        }
        if (sw <= 0)
            continue;
        // off screen: an arrow on the edge towards it (behind the camera: at the bottom)
        float cx = sw / 2.f, cy = sh / 2.f;
        float dx = front ? sc[0] - cx : 0.f, dy = front ? sc[1] - cy : 1.f;
        float fx, fy;
        if (!cell_logic::edge_point(cx, cy, dx, dy, 90, 60, 1, 0.5f, fx, fy)) // a NaN position (BUG-0160)
            continue;
        int x = (int)fx, y = (int)fy;
        const char* arrow = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? "\u25BA" : "\u25C4") : (dy > 0 ? "\u25BC" : "\u25B2");
        std::string t = std::string(arrow) + " " + m.text;
        H->overlay_text(x - text_width(t) / 2, y - 10, col, t.c_str());
    }
}

uint32_t g_tags_drawn = 0;   // name tags over partners drawn in the last overlay pass (coop.status; all.name_tags)
// BUG-0907: in Tribe a partner's name tag was drawn at its pointer (the planet point under its mouse: the Tribe
// adapter's "puppet" is that marker, modes.cpp), with nothing under it: a name over empty ground, on a slope or at
// the fire pit, wherever the partner's mouse last was (same_view_tribe and play_tribe key frames, lab
// 20261110-201619). The partner's label in Tribe is the tag of its chief and members (draw_owner_tags), and its arrow
// when the chief is off screen (find_targets): the pointer gets no name. Off: the name at the pointer (0.7.11).
bool g_tribe_tag_fix = true;
COOP_FIX("tribe.pointer_tag", "BUG-0907", g_tribe_tag_fix, "tribe: a partner's name is over its chief and members only, not over the empty ground under its mouse pointer");

// tribe stage: the owner's name over every member of the player tribe (own members in gold)
void draw_owner_tags()
{
    tribe::for_each_owned(
        [](void* c, uint8_t owner, void*) {
            net::Vec3 p = game::position(c);
            float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            float up = len > 1 ? 2.6f / len : 0;
            float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
            if (!H->world_to_screen(w, sc))
                return;
            std::string n = owner == g_sess.my_id() ? g_name : g_sess.name_of(owner);
            bool chief = tribe::is_chief(c);
            if (chief)
                n = "\u2605 " + n + "'s chief";
            float at[3] = {p.x, p.y, p.z};
            pfind::tag(chief ? "chief" : "member", owner, n, sc[0], sc[1], at, chief ? 1 : 3); // F2 colour, BUG-0949
            g_tags_drawn++;
        },
        nullptr);
}

// civilization: owner names over the player's cities and vehicles
void draw_civ_tags()
{
    civ::for_each_owned(
        [](void*, const net::Vec3& p, uint8_t owner, bool city, void*) {
            float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            float up = len > 1 ? (city ? 30.f : 6.f) / len : 0;
            float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
            if (!H->world_to_screen(w, sc))
                return;
            std::string n = owner == g_sess.my_id() ? g_name : g_sess.name_of(owner);
            if (city)
                n = "[" + n + "]";
            float at[3] = {p.x, p.y, p.z};
            pfind::tag(city ? "city" : "vehicle", owner, n, sc[0], sc[1], at, city ? 3 : 4); // F2 colour, BUG-0949
            g_tags_drawn++;
        },
        nullptr);
}

// A menu over the game (the pause menu "Options", the co-op window): the world labels (name
// tags, pings, owner tags) are drawn by the overlay on top of everything, so they were drawn over
// the menu's buttons (play_creature.py 2026-10-23). The pause menu is one window that stays
// loaded and is shown / hidden (control id 0x0615B50D, docs/08-ui.md); looked up outside the
// frame a few times a second.
constexpr uint32_t kPauseMenuId = 0x0615B50D;
std::atomic<bool> g_menu_up{false};
// BUG-0149: the cheat console (Ctrl+Shift+C) is open: keys typed there are no ability keys. Its
// window (control id 0x014A4606, its text edit 0x014A4608) exists only while it is open.
constexpr uint32_t kCheatConsoleId = 0x014A4606;
std::atomic<bool> g_text_up{false};
std::atomic<bool> g_menu_check_queued{false};
double g_menu_checked = 0;
uint32_t g_menu_hidden = 0; // frames whose labels were hidden (coop.status)
// BUG-0985: the partners' tags and edge arrows stood over a stage's cutscene (the host in the Creature stage's
// intro, 0.7.13 final check, campaign advance-cell): world labels stay off while a cinematic holds the game (it
// plays with the input locked or the time stood) in Cell, Creature, Tribe and Civ. Space and the galaxy keep them
// (their flights play as cinematics with the input locked; the arrows are how a player finds the others there).
// Off: the labels are drawn over every cinematic (0.7.12).
bool g_cine_labels_fix = true;
COOP_FIX("find.cinematic", "BUG-0985", g_cine_labels_fix,
         "Cell, Creature, Tribe, Civ: no partner tag or arrow over a cutscene that holds the game");
uint32_t g_cine_hidden = 0; // frames whose labels a cinematic hid (coop.status)
static bool cinematic_holds_labels()
{
    if (!g_cine_labels_fix || !world::cinematic_playing())
        return false;
    if (g_mode != modes::kCell && g_mode != modes::kCreature && g_mode != modes::kTribe && g_mode != modes::kCiv)
        return false;
    return coop_ui::input_lock_now() > 0 || world::time_paused();
}

bool shown_chain(sui::Win w)
{
    for (int i = 0; w && i < 32; i++, w = sui::parent(w))
        if (!sui::visible(w))
            return false;
    return true;
}

// a visible window with this control id below w (closed boxes stay in the tree, hidden)
bool visible_below(sui::Win w, uint32_t id, int depth = 0)
{
    if (!w || depth > 12 || !sui::visible(w))
        return false;
    if (sui::control_id(w) == id)
        return true;
    for (sui::Win c : sui::children(w))
        if (visible_below(c, id, depth + 1))
            return true;
    return false;
}

// the pause menu or one of the game's message boxes ("Do you want to save your game?", "Are you
// sure?": container 0x06318E78, like the "game saved" box) is up; outside the frame only
bool pause_ui_up()
{
    sui::Win root = sui::main_window();
    return root && (shown_chain(sui::find(root, kPauseMenuId)) || visible_below(root, 0x06318E78u));
}

void menu_check_task(void*)
{
    g_menu_check_queued = false;
    bool up = coop_ui::is_open();
    bool pm = false;
    if (sui::Win root = sui::main_window())
        pm = shown_chain(sui::find(root, kPauseMenuId));
    host_busy_pause_menu(pm); // C3 (BUG-1142): the Cell host's only pause sign
    up = up || pm;
    g_menu_up = up;
    sui::Win root = sui::main_window();
    g_text_up = root && shown_chain(sui::find(root, kCheatConsoleId));
}

void menu_tick(double now)
{
    if (now - g_menu_checked < 0.2 || g_menu_check_queued)
        return;
    g_menu_checked = now;
    g_menu_check_queued = true;
    H->run_outside_frame(menu_check_task, nullptr);
}

bool g_status_block_fix = true;   // BUG-0155
COOP_FIX("core.status_block", "BUG-0155", g_status_block_fix, "ui: the status text and notifications start below the co-op window");
std::array<int, 4> g_status_block{}; // the status block's screen rect of the last frame (coop.status)
int g_status_drawn_right; // the right end of the block's widest drawn line (estimate, coop.status; BUG-1219)
// BUG-1219: the Creature "Pack: ..." line of the status block ran off the screen's right edge (block at sw - 480)
static bool g_status_wrap_fix = true;
COOP_FIX("core.status_wrap", "BUG-1219", g_status_wrap_fix,
         "ui: the status block's Creature \"Pack: ...\" line is cut into lines that end before the screen's right edge");

// BUG-0939: the planet's size the partners' positions are held against: the own avatar's distance from the planet's
// centre in the planet stages, 0 where the world has no such centre (Cell's plane, Space)
static float find_ref_radius(const float me[3])
{
    if (g_mode != modes::kCreature && g_mode != modes::kTribe && g_mode != modes::kCiv)
        return 0.f;
    return std::sqrt(me[0] * me[0] + me[1] * me[1] + me[2] * me[2]);
}
static bool find_placed(const net::Vec3& p, float ref_r)
{
    float q[3] = {p.x, p.y, p.z};
    return findx::placed(q, ref_r);
}

// F2 (BUG-0764): where every partner is in this stage, for the arrows and the minimap. Cell, Creature, Space: its
// creature / cell / ship (the puppet); Tribe: its chief (else its pointer); Civ: where it plays now (its pointer
// marker, the camera's focus) else its first city. me: the own avatar (Tribe: the own chief).
static std::vector<pfind::Target> find_targets(float me[3], bool& has_me)
{
    std::vector<pfind::Target> v;
    has_me = false;
    if (!g_local || !g_sess.connected() || !stage_mode(g_mode))
        return v;
    net::Vec3 mp = modes::entity_pos(g_adapter, g_local);
    me[0] = mp.x, me[1] = mp.y, me[2] = mp.z;
    has_me = std::isfinite(mp.x) && std::isfinite(mp.y) && std::isfinite(mp.z);
    struct Pick {
        std::map<uint8_t, net::Vec3> chief, city;
        bool own_chief = false;
        net::Vec3 own{};
    } pk;
    if (g_mode == modes::kTribe)
        tribe::for_each_owned(
            [](void* c, uint8_t owner, void* u) {
                auto* q = (Pick*)u;
                if (!tribe::is_chief(c))
                    return;
                if (owner == g_sess.my_id()) {
                    q->own_chief = true;
                    q->own = game::position(c);
                } else
                    q->chief[owner] = game::position(c);
            },
            &pk);
    if (g_mode == modes::kCiv)
        civ::for_each_owned(
            [](void*, const net::Vec3& p, uint8_t owner, bool city, void* u) {
                auto* q = (Pick*)u;
                if (city && !q->city.count(owner))
                    q->city[owner] = p;
            },
            &pk);
    if (pk.own_chief) {
        me[0] = pk.own.x, me[1] = pk.own.y, me[2] = pk.own.z;
        has_me = true;
    }
    // BUG-0939 (find.placed): only a partner shown here (its puppet: a fresh in-world state of this stage and world)
    // with a position that is a place; the own position too (an own pointer never put on the planet is no "me")
    bool fix = pfind::g_placed_fix;
    if (fix && has_me && !findx::placed(me, 0.f))
        has_me = false;
    float ref_r = has_me ? find_ref_radius(me) : 0.f;
    for (auto& [id, r] : g_remotes) {
        if (id == g_sess.my_id() || r.echo)
            continue;
        bool here = r.puppet && r.puppet_adapter;
        if (fix && !here)
            continue;
        pfind::Target t{};
        t.id = id;
        t.name = r.name;
        net::Vec3 p{};
        bool have = false;
        if (here)
            p = modes::entity_pos(r.puppet_adapter, r.puppet);
        if (g_mode == modes::kTribe && pk.chief.count(id)) {
            p = pk.chief[id];
            have = true;
            t.what = coop_ui::russian() ? "вождь" : "chief";
        } else if (here && (!fix || find_placed(p, ref_r))) {
            have = true;
            t.flat = r.puppet_adapter->flat;
        } else if (g_mode == modes::kCiv && pk.city.count(id)) {
            p = pk.city[id];
            have = true;
            t.what = coop_ui::russian() ? "столица" : "capital";
        }
        if (!have || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            continue;
        if (fix && !find_placed(p, ref_r))
            continue;
        t.pos[0] = p.x, t.pos[1] = p.y, t.pos[2] = p.z;
        v.push_back(t);
    }
    for (const pfind::Target& g : pfind::ghosts()) // tests (BUG-0949)
        v.push_back(g);
    return v;
}

void on_overlay(void*, int, void*)
{
    modes::CellScope cell_scope; // BUG-0271
    if (!g_overlay)
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    // world labels stay off while a menu is up (and the co-op window: also right when it opens)
    bool labels = !g_menu_up && !coop_ui::is_open();
    if (!labels)
        g_menu_hidden++;
    else if (cinematic_holds_labels()) { // BUG-0985
        labels = false;
        g_cine_hidden++;
    }
    // F3 (BUG-0768): all.name_tags off hides the partners' names (creature, cell, ship, tribe members, civ cities and
    // vehicles); pings, the knock-out bar and the arrows stay
    bool tags = labels && settings::on(settings::kNameTags);
    g_tags_drawn = 0;
    pfind::labels_begin(); // (tests: coop.find labels)
    if (labels) {
        draw_marks();
        cellsync::draw_overlay();
        spacecoop::draw_overlay();
        if (tags) {
            draw_owner_tags();
            draw_civ_tags();
        }
    }
    { // F2: arrows to the partners off screen; the minimap takes the same positions (outside the frame)
        float me[3] = {0, 0, 0};
        bool has_me = false;
        std::vector<pfind::Target> ts = find_targets(me, has_me);
        if (labels)
            pfind::draw_arrows(ts, has_me ? me : nullptr);
        pfind::set_map_targets(ts, g_mode);
    }
    // name tags above remote players
    void* av = labels ? g_local : nullptr;
    if (!tags)
        av = nullptr;
    if (g_tag_self && av) {
        net::Vec3 p = modes::entity_pos(g_adapter, av);
        float sc[2] = {0, 0}, w[3] = {p.x, p.y, p.z};
        int ok = H->world_to_screen(w, sc);
        g_dbg_sc[0] = sc[0]; g_dbg_sc[1] = sc[1]; g_dbg_sc[2] = (float)ok;
        if (ok)
            H->overlay_text((int)sc[0] - 4, (int)sc[1] - 10, 0xFFFF4040, "+");
    }
    for (auto& [id, r] : g_remotes) {
        if (!r.puppet || !av)
            continue;
        auto* ad = r.puppet_adapter;
        if (g_tribe_tag_fix && ad->mode == modes::kTribe) // BUG-0907: the pointer has nothing to label
            continue;
        net::Vec3 p = modes::entity_pos(ad, r.puppet);
        if (pfind::g_placed_fix && !find_placed(p, 0.f)) // BUG-0939: a pointer never put on the planet has no name
            continue;
        float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        float h = ad->tag_height ? ad->tag_height(r.puppet) : 3.f;
        float up = len > 1 ? h / len : 0; // above the head, along the planet normal
        float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
        if (ad->flat) {
            w[0] = p.x;
            w[1] = p.y + h; // cell world: screen up is +y on the plane
            w[2] = p.z;
        }
        if (H->world_to_screen(w, sc)) {
            std::string tag = r.name;
            float at[3] = {p.x, p.y, p.z};
            pfind::tag("player", id, tag, sc[0], sc[1], at, 0); // F2 colour, BUG-0949
            g_tags_drawn++;
        }
    }
    if (av) // tests (BUG-0949): the made-up partners' tags, at their points
        for (const pfind::Target& g : pfind::ghosts()) {
            float w[3] = {g.pos[0], g.pos[1], g.pos[2]}, sc[2];
            if (H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh)
                pfind::tag("player", g.id, g.name, sc[0], sc[1], w, 0);
        }
    pfind::labels_flush(); // BUG-0949: the tags and arrows of this pass, apart and off the HUD
    std::string head;
    if (g_sess.role() == net::Role::Host)
        head = coop_ui::demo_text("CO-OP host :" + std::to_string(g_sess.port())) + "  " + std::to_string(g_sess.players().size()) +
               "/" + std::to_string(g_sess.max_players);
    else if (g_sess.role() == net::Role::Client)
        head = std::string("CO-OP guest  ") + (g_sess.connected() ? "connected" : "connecting...");
    else if (g_echo)
        head = "CO-OP echo test";
    // BUG-0155: the block is not drawn over the co-op window or a menu (the lobby shows the same),
    // and the notification panels start below it; `coop.fix core.status_block off` = the old way
    // (g_menu_up is only refreshed in a game: in the main menu the window's own flag counts)
    bool covered = coop_ui::is_open() || (!labels && g_local);
    if (head.empty() || (covered && g_status_block_fix)) {
        g_status_block = {0, 0, 0, 0};
        pfind::set_status_rect(0, 0, 0, 0);
        notify::set_top_min(0);
        return;
    }
    int x = sw > 0 ? sw - 480 : 900, y = 70, wmax = utf8::overlay_width(head);
    H->overlay_text(x, y, 0xFFFFD040, head.c_str());
    y += 22;
    g_status_drawn_right = x + wmax;
    if (g_sess.connected() && g_mode == modes::kCreature) {
        std::string t = teamplay::status();
        if (!t.empty()) {
            // BUG-1219: the line (~100 letters) is wider than the block's 480 px: cut between its fields into lines that
            // end before the screen's edge. Old: one line, cut off by the edge (and outside the block's rect).
            int room = (sw > 0 ? sw : x + 480) - x - 12;
            std::vector<std::string> lines = g_status_wrap_fix ? utf8::wrap_fields(t, "  |  ", room) : std::vector<std::string>{t};
            for (auto& l : lines) {
                H->overlay_text(x, y, 0xFF8CF08C, l.c_str());
                g_status_drawn_right = std::max(g_status_drawn_right, x + utf8::overlay_width(l));
                if (g_status_wrap_fix)
                    wmax = std::max(wmax, utf8::overlay_width(l));
                y += 20;
            }
        }
    }
    for (auto& p : g_sess.players()) {
        std::string line = (p.id == g_sess.my_id() ? "> " : "  ") + p.name;
        auto it = g_remotes.find(p.id);
        if (p.id != g_sess.my_id() && it != g_remotes.end() && !it->second.buf.empty()) {
            const auto& ls = it->second.buf.latest();
            line += std::string("  [") + modes::mode_name(ls.mode) + (other_world_label(p.id, ls) ? ", other world" : "") + "]";
        }
        H->overlay_text(x, y, 0xFFFFFFFF, line.c_str());
        wmax = std::max(wmax, utf8::overlay_width(line));
        g_status_drawn_right = std::max(g_status_drawn_right, x + utf8::overlay_width(line));
        y += 20;
    }
    g_status_block = {x, 70, x + wmax, y};
    pfind::set_status_rect(x, 70, x + wmax, y); // BUG-0949: the next pass keeps the labels off it
    notify::set_top_min(g_status_block_fix ? (float)y + 8 : 0.f);
    // notices are notification panels now (notify.cpp); the history is in the co-op window
}

// BUG-0400: `coop.npc gopos` (tests) puts a point under the terrain onto it
bool g_gopos_ground_fix = true;
COOP_FIX("tools.gopos_ground", "BUG-0400", g_gopos_ground_fix, "tests: coop.npc gopos puts the avatar onto the ground, never under it");
// BUG-0964: the ground above is GetHeightAt, the bare terrain: a nest mound or a rock stands above it (their collision
// is loaded near the camera). A creature put at the terrain's height under such a mound is inside its collision and
// Havok holds it in every direction (the attack key's walk-and-use and any walk do nothing until it is put higher;
// lab 20261112-142742: 7 of 7 such creatures walked after a 2 m lift, all of them 2.6-4.3 m from a nest's centre).
// The game's own surface (cPlanetModel_SurfacePoint: the terrain, then a ray cast down onto the collision) is the top.
static bool g_gopos_surface_fix = true;
COOP_FIX("tools.gopos_surface", "BUG-0964", g_gopos_surface_fix, "tests: coop.npc gopos puts the avatar onto the top of a nest mound or rock, never inside it");

// the point lifted onto the game's surface (a mound, a rock) when it is under it (BUG-0964); the lift in metres. The
// surface's ray cast hits creatures too (1 m from an animal it gives the animal's back), so the surface is the median of
// five: the point and four 1.2 m around it along the ground (an animal covers one or two of them, a mound all).
float gopos_onto_surface(net::Vec3& p)
{
    float r = drive::speed(p);
    if (!g_gopos_surface_fix || r < 1)
        return 0.f;
    net::Vec3 up{p.x / r, p.y / r, p.z / r};
    net::Vec3 e1 = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
    float l1 = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
    e1 = {e1.x / l1, e1.y / l1, e1.z / l1};
    net::Vec3 e2{up.y * e1.z - up.z * e1.y, up.z * e1.x - up.x * e1.z, up.x * e1.y - up.y * e1.x};
    float v[5];
    int n = 0;
    const float d = 1.2f, o[5][2] = {{0, 0}, {d, 0}, {-d, 0}, {0, d}, {0, -d}};
    for (auto& k : o) {
        net::Vec3 q{p.x + e1.x * k[0] + e2.x * k[1], p.y + e1.y * k[0] + e2.y * k[1], p.z + e1.z * k[0] + e2.z * k[1]};
        float qs = game::surface_radius(q), qr = drive::speed(q);
        if (qs > 1 && qr > 1)
            v[n++] = qs + (r - qr); // (the sample's height over its own radial line, put on the point's)
    }
    if (n < 3)
        return 0.f;
    std::sort(v, v + n);
    float s = v[n / 2];
    if (!(r < s + 0.05f) || s - r > 20.f) // (20 m: no surface found that far up)
        return 0.f;
    float k = (s + 0.05f) / r;
    p = {p.x * k, p.y * k, p.z * k};
    return s + 0.05f - r;
}

// BUG-0989: a guest that joins in Creature loads the host's save, so its creature starts where the save has the player's
// creature: on the host's creature (lab key frames: both avatars at one point, drawn inside each other, other poses on
// each game; the host's name tag over the guest's own creature for a moment). Once per arrival, when the host's
// creature's puppet first stands within kJoinApart of the guest's own creature (for 0.5 s, within 30 s of the arrival),
// the guest's creature is put kJoinAside m beside it on the game's surface; a guest that arrives elsewhere, or walks up
// to the host later, is left alone. `coop.fix core.join_apart off`: as before (the two in one spot).
static bool g_join_apart_fix = true;
COOP_FIX("core.join_apart", "BUG-0989", g_join_apart_fix,
         "creature: a joining guest's creature is put a few metres beside the host's (both started in one spot, drawn inside each other)");
constexpr float kJoinApart = 2.5f, kJoinAside = 5.f;
static uint32_t g_join_aparts;
static double g_join_apart_at = -1;
static float g_join_apart_from = -1;
static void join_apart_tick(double now)
{
    static void* av_seen;
    static double av_since, close_since;
    static bool decided;
    void* av = g_mode == modes::kCreature && g_sess.role() == net::Role::Client && g_sess.connected() ? game::avatar() : nullptr;
    if (av != av_seen) {
        av_seen = av;
        av_since = now;
        close_since = 0;
        decided = false;
    }
    if (!av || decided || !g_join_apart_fix || now - av_since > 30.0)
        return;
    auto it = g_remotes.find(0);
    if (it == g_remotes.end() || !it->second.puppet || !creature_puppet(it->second.puppet_adapter) ||
        !game::creature_listed(it->second.puppet))
        return;
    net::Vec3 a = game::position(av), b = game::position(it->second.puppet);
    float rb = drive::speed(b);
    if (rb < 1.f)
        return;
    if (net::dist(a, b) >= kJoinApart) {
        decided = true; // (it arrived elsewhere: nothing to do, now or later)
        return;
    }
    if (close_since == 0) {
        close_since = now;
        return;
    }
    if (now - close_since < 0.5)
        return;
    decided = true;
    // sideways on the ground: away from the host's creature when they are apart at all, else along the planet's tangent
    net::Vec3 up{b.x / rb, b.y / rb, b.z / rb}, d{a.x - b.x, a.y - b.y, a.z - b.z};
    float along = d.x * up.x + d.y * up.y + d.z * up.z;
    d = {d.x - up.x * along, d.y - up.y * along, d.z - up.z * along};
    float ld = drive::speed(d);
    if (ld < 0.2f) {
        d = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
        ld = drive::speed(d);
    }
    net::Vec3 p{b.x + d.x / ld * kJoinAside, b.y + d.y / ld * kJoinAside, b.z + d.z / ld * kJoinAside};
    float r = drive::speed(p), g = game::ground_radius(p);
    if (g > 1 && r > 1)
        p = {p.x * g / r, p.y * g / r, p.z * g / r};
    gopos_onto_surface(p);
    game::teleport(av, p, game::orientation(av));
    g_join_aparts++;
    g_join_apart_at = now;
    g_join_apart_from = net::dist(a, b);
    LOG("creature: this creature started on the host's (%.1f m apart): put %.0f m beside it (BUG-0989)", net::dist(a, b), kJoinAside);
}
std::string join_apart_status()
{
    char b[128];
    snprintf(b, sizeof b, "join_apart fix=%d moves=%u last=%.1f s ago from=%.2f m", (int)g_join_apart_fix, g_join_aparts,
             g_join_apart_at < 0 ? -1.0 : H->time_seconds() - g_join_apart_at, g_join_apart_from);
    return b;
}

// ---------------- UI ----------------
std::vector<coop_ui::PlayerRow> ui_players()
{
    std::vector<coop_ui::PlayerRow> v;
    bool host = g_sess.role() == net::Role::Host;
    for (auto& p : g_sess.players()) {
        coop_ui::PlayerRow r;
        r.id = p.id;
        r.name = p.name;
        r.host = p.id == 0;
        r.me = p.id == g_sess.my_id();
        r.ready = g_ready.count(p.id) && g_ready[p.id];
        r.color = pfind::color_index(p.id) >= 0 ? pfind::color_of(p.id) : 0; // F2
        if (r.me)
            r.stage = modes::mode_name(g_mode);
        else {
            auto it = g_remotes.find(p.id);
            if (it != g_remotes.end() && !it->second.buf.empty()) {
                const auto& ls = it->second.buf.latest();
                r.stage = modes::mode_name(ls.mode);
                if (other_world_label(p.id, ls) && g_adapter)
                    r.stage += ", other world";
            } else
                r.stage = "...";
        }
        if (!r.me)
            r.ping_ms = host || p.id == 0 ? g_sess.rtt_of(p.id) : (g_lobby_rtt.count(p.id) ? g_lobby_rtt[p.id] : -1);
        v.push_back(r);
    }
    return v;
}

std::string ui_session_line()
{
    char b[256];
    if (g_sess.role() == net::Role::Host)
        snprintf(b, sizeof b, "Hosting on UDP port %u  -  %d/%d players  -  %s", g_sess.port(), (int)g_sess.players().size(),
                 g_sess.max_players, autosave::saved_line().c_str()); // E2: when the host's game last saved
    else if (g_sess.connected())
        snprintf(b, sizeof b, "Connected to %s (%s)", g_sess.name_of(0).c_str(), g_sess.host_address().c_str());
    else
        snprintf(b, sizeof b, "Connecting to %s ...", g_sess.host_address().c_str());
    return b;
}

std::string ui_world_line()
{
    if (g_vote.active) {
        int ready = 0, n = 0;
        for (auto& p : g_sess.players()) {
            n++;
            ready += g_ready.count(p.id) && g_ready[p.id];
        }
        char b[200];
        snprintf(b, sizeof b, "%s - ready %d/%d - %d s", g_vote.text.c_str(), ready, n, g_vote.left);
        return b;
    }
    uint32_t got = 0, total = 0;
    if (g_sess.role() == net::Role::Client && world::progress(got, total) && total > 0) {
        char b[160];
        snprintf(b, sizeof b, "Receiving %s's world: %u%%", g_sess.name_of(0).c_str(),
                 (unsigned)((uint64_t)got * 100 / total));
        return b;
    }
    // the host is evolving in an editor: everybody gets the new body afterwards
    uint32_t host_mode = g_sess.role() == net::Role::Host ? g_mode
                         : (g_remotes.count(0) && !g_remotes[0].buf.empty() ? g_remotes[0].buf.latest().mode : 0);
    if (host_mode == modes::kEdit || host_mode == modes::kEditor) // BUG-1258: in Creature each its own creature
        return evolve_own_text(g_mode) ? g_sess.name_of(0) + " is in the editor"
                                       : g_sess.name_of(0) + " is evolving the species in the editor: everybody gets the new body";
    if (g_sess.role() == net::Role::Host && g_sess.players().size() > 1 && !(g_adapter && g_local))
        return "Load or start a game: the guests get your world automatically";
    if (g_sess.role() == net::Role::Client && g_sess.connected() && !g_adapter)
        return "Waiting for the host's world...";
    return "";
}

std::vector<std::pair<std::string, net::Vec3>> partners()
{
    std::vector<std::pair<std::string, net::Vec3>> v;
    double now = H->time_seconds();
    for (auto& [id, r] : g_remotes) {
        if (r.echo || r.buf.empty() || r.buf.age(now) > 3.0)
            continue;
        const auto& s = r.buf.latest();
        if ((s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD) && same_place(s))
            v.push_back({r.name, s.pos});
    }
    return v;
}

// the stage action of the co-op window: give the selected own units to a partner
// BUG-0075: the player picks the recipient among the other players (the co-op window's ">" next to
// Give, `coop.give next`); before, it was always the first other player. `coop.give fix 0`: the old one.
bool g_give_pick_fix = true;
COOP_FIX("give.pick", "BUG-0075", g_give_pick_fix, "civ: the player picks the recipient of Give (coop.give fix)");
static std::vector<uint8_t> player_ids()
{
    std::vector<uint8_t> ids;
    for (auto& p : g_sess.players())
        ids.push_back(p.id);
    return ids;
}
uint8_t give_target() { return net::give_target(player_ids(), g_sess.my_id(), g_give_pick, g_give_pick_fix); }
void give_target_cycle() { g_give_pick = net::give_target_next(player_ids(), g_sess.my_id(), g_give_pick); }

// BUG-1050: in Space "Give 10 to <player>" named coop's give target, but the spice went to the first other player of the
// session, and the ">" picker was hidden there. Now the spice goes to the player the button names, and the co-op window
// shows ">" in Space too (coop_ui.cpp). `coop.fix space.give_pick off`: the first other player, ">" hidden (0.7.13).
bool g_space_give_pick_fix = true;
COOP_FIX("space.give_pick", "BUG-1050", g_space_give_pick_fix,
         "space: Give 10 spice goes to the player the button names, and 3-4 players pick the recipient with >");
int give_selected_to(uint8_t to)
{
    if (to == 0xFF)
        return 0;
    if (g_mode == modes::kTribe)
        return tribe::give_selected(to);
    if (g_mode == modes::kCiv)
        return civ::give_selected(to);
    if (g_mode == modes::kSpace)
        return spacecoop::give_spice(10, g_space_give_pick_fix ? to : 0xFF);
    return 0;
}

// tests (chaos bot): what can be clicked right now: "kind id owner sx sy" per visible unit
std::string targets(int all)
{
    std::string out;
    char b[96];
    auto add = [&](const char* kind, int id, int owner, const net::Vec3& p) {
        float w[3] = {p.x, p.y, p.z}, sc[2];
        if (!H->world_to_screen(w, sc))
            return;
        snprintf(b, sizeof b, "%s %d %d %d %d\n", kind, id, owner, (int)sc[0], (int)sc[1]);
        out += b;
    };
    for (auto& [id, r] : g_remotes)
        if (void* pp = puppet_now(r))
            add("player", id, id, modes::entity_pos(r.puppet_adapter, pp));
    if (void* local = local_now())
        add("me", g_sess.my_id(), g_sess.my_id(), modes::entity_pos(g_adapter, local));
    struct U {
        std::string* out;
        decltype(add)* f;
    } u{&out, &add};
    tribe::for_each_owned(
        [](void* c, uint8_t owner, void* p) { (*((U*)p)->f)("member", tribe::id_of(c), owner, game::position(c)); },
        &u);
    civ::for_each_target(
        [](const char* kind, int id, int owner, const net::Vec3& pos, void* p) { (*((U*)p)->f)(kind, id, owner, pos); },
        &u);
    if (all) // partners off the screen: where they are projected (bots fly toward them)
        for (auto& [id, r] : g_remotes)
            if (void* pp = puppet_now(r)) {
                net::Vec3 p = modes::entity_pos(r.puppet_adapter, pp);
                float w[3] = {p.x, p.y, p.z}, sc[2] = {-1e9f, -1e9f};
                if (!H->world_to_screen(w, sc) && sc[0] > -1e8f) {
                    snprintf(b, sizeof b, "far player %d %d %d\n", id, (int)std::clamp(sc[0], -1e5f, 1e5f),
                             (int)std::clamp(sc[1], -1e5f, 1e5f));
                    out += b;
                }
            }
    if (all) { // every synchronized object on screen (game bots aim at NPCs, citizens, ships ...)
        net::Vec3 me{};
        if (void* local = local_now())
            me = modes::entity_pos(g_adapter, local);
        struct Seen {
            std::string key;
            float d;
            int x, y;
        };
        std::vector<Seen> seen;
        int sw = 0, sh = 0;
        H->screen_size(&sw, &sh);
        for (auto* f : {npc::sync_entries, tribe::sync_entries, civ::sync_entries, squadron::sync_entries,
                        cellsync::sync_entries, spacecoop::nav_entries})
            for (auto& e : f()) {
                float w[3] = {e.pos.x, e.pos.y, e.pos.z}, sc[2];
                if (H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh)
                    seen.push_back({e.key, net::dist(e.pos, me), (int)sc[0], (int)sc[1]});
            }
        if (all == 2) { // every game object on screen by noun id (huts, tools, plants, food: game bots)
            struct O {
                std::vector<Seen>* seen;
                net::Vec3 me;
                int sw, sh;
            } o{&seen, me, sw, sh};
            game::for_each_object(
                [](void* obj, uint32_t noun, void* u) {
                    auto* q = (O*)u;
                    net::Vec3 p;
                    float w[3], sc[2];
                    if (!game::object_alive(obj) || !game::object_position(obj, p))
                        return true;
                    w[0] = p.x, w[1] = p.y, w[2] = p.z;
                    if (H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < q->sw && sc[1] < q->sh) {
                        char k[16];
                        snprintf(k, sizeof k, "o%X", noun);
                        std::string key = tribe::object_key(obj);
                        q->seen->push_back({key.empty() ? std::string(k) : key, net::dist(p, q->me), (int)sc[0], (int)sc[1]});
                    }
                    return true;
                },
                &o);
            tribe::for_each_tree_pos( // planet trees are no game objects (fruit trees: gather)
                [](const net::Vec3& p, bool fruit, void* u) {
                    auto* q = (O*)u;
                    float w[3] = {p.x, p.y, p.z}, sc[2];
                    if (H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < q->sw && sc[1] < q->sh)
                        q->seen->push_back({fruit ? "fruittree" : "tree", net::dist(p, q->me), (int)sc[0], (int)sc[1]});
                },
                &o);
            // tribe centres also off screen (a bot pans the camera towards them): "tribe<i>[p] -1 x y"
            tribe::for_each_tribe_center(
                [](const std::string& key, const net::Vec3& p, void* u) {
                    auto* q = (O*)u;
                    // world_to_screen fills the point for anything in front of the camera, even off screen
                    float w[3] = {p.x, p.y, p.z}, sc[2] = {-1e9f, -1e9f};
                    H->world_to_screen(w, sc);
                    if (sc[0] > -1e8f) // listed first (distance -1): never cut off by the limit
                        q->seen->push_back({key, -1.f, (int)std::clamp(sc[0], -99999.f, 99999.f),
                                            (int)std::clamp(sc[1], -99999.f, 99999.f)});
                },
                &o);
        }
        // the nearest first, at most 80 (a console answer holds 8 KB; the tide pool has ~400 cells)
        std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.d < b.d; });
        for (size_t i = 0; i < seen.size() && i < (all == 2 ? 250u : 80u); i++) {
            snprintf(b, sizeof b, "obj %s %.1f %d %d\n", seen[i].key.c_str(), seen[i].d, seen[i].x, seen[i].y);
            out += b;
        }
    }
    return out;
}

// The host's "Ask the players for their reports" (lobby and F9, any time; owner 2026-11-08): F4 of 0.7.12
// (BUG-0767, sreport.h) asks every guest "send / don't send" without making the host's own report; the reports come
// over the session and go into one SporeCoop-session-report-<time>.zip. Returns the line the co-op window shows.
std::string ask_players_for_reports()
{
    if (g_sess.role() != net::Role::Host)
        return "Only the host can ask for the reports";
    LOG("ui: the host asks the players for their reports (%d players)", (int)g_sess.players().size());
    return sreport::ask("");
}

void init_ui()
{
    coop_ui::Api a;
    a.name = [] { return g_name; };
    a.set_name = [](const std::string& n) { g_name = utf8::player_name(n); };
    a.join = do_join;
    a.host = do_host;
    a.leave = do_leave;
    a.role = [] { return g_sess.role() == net::Role::Host ? 1 : g_sess.role() == net::Role::Client ? 2 : 0; };
    a.connected = [] { return g_sess.connected(); };
    a.session_line = ui_session_line;
    a.give = [](std::string& what, std::string& to) {
        uint8_t t = give_target();
        if (!g_sess.connected() || t == 0xFF)
            return 0;
        to = g_sess.name_of(t);
        int n = 0;
        if (g_mode == modes::kTribe) {
            n = tribe::own_selected();
            what = "member";
        } else if (g_mode == modes::kCiv) {
            bool city = false;
            n = civ::own_selected(&city);
            what = city ? "city" : "vehicle";
        } else if (g_mode == modes::kSpace) {
            std::string sto;
            n = spacecoop::spice(sto);
            if (n < 0)
                return 0;
            what = "spice";
        }
        return n;
    };
    a.jump = [](std::string& to) { return g_sess.connected() && spacecoop::can_jump(to); };
    a.update = autoupdate::ui;
    a.version_line = [] {
        std::string s = "SporeCoop " COOP_VERSION;
        if (host_has_diag() && H->build_name)
            s += std::string("  -  ") + H->build_name();
        return s;
    };
    a.report = [] {
        std::string p = make_report("from the co-op window");
        // F4 (BUG-0767): the host's report asks every guest for its report too (one session zip)
        if (!p.empty() && g_sess.role() == net::Role::Host && g_sess.players().size() > 1)
            sreport::ask(p);
        return p;
    };
    a.report_round = [] { return g_sess.role() == net::Role::Host && sreport::round_running(); };
    a.report_line = sreport::host_ui_line;
    a.report_prompt = sreport::guest_prompt;
    a.report_answer = sreport::answer;
    a.report_shot = [] { return support::report_shot("report", "\"Report a problem\" in the co-op window"); };
    a.report_shot_state = [] { return support::report_shot_state(); };
    a.update_do = autoupdate::start_update;
    a.ask_reports = ask_players_for_reports;
    a.host_address = [] {
        if (g_sess.role() == net::Role::Client)
            return g_sess.host_address();
        if (g_sess.role() != net::Role::Host)
            return std::string();
        std::string s;
        int n = 0;
        for (auto& l : local_addresses())
            if (n++ < 2) // (two fit the lobby's line)
                s += (s.empty() ? "" : ",  ") + l.ip + ":" + std::to_string(g_sess.port());
        return s;
    };
    a.jump_do = [] {
        H->run_outside_frame(
            [](void*) {
                std::string e = spacecoop::jump(0xFF);
                if (!e.empty()) { // only here: the partner does not need to read why we can't jump
                    feed("can't jump: " + e);
                    chat_log("* can't jump: " + e);
                }
            },
            nullptr);
    };
    a.give_do = [] { give_selected_to(give_target()); };
    a.give_cycle = [] { give_target_cycle(); };
    a.give_choices = [] { return (int)g_sess.players().size() - 1; };
    a.give_pick_space = [] { return g_space_give_pick_fix; };
    a.civ_role = [] { return (int)g_roles[g_sess.my_id()]; };
    a.civ_role_cycle = [](int d) { set_role((uint8_t)(((int)g_roles[g_sess.my_id()] + d + 4) % 4)); };
    a.creation = [](int& kind, std::string& name, int& own) {
        if (!g_sess.connected() || !(g_mode == modes::kCreature || g_mode == modes::kCell || g_mode == modes::kSpace ||
                                     creations::late_row(H->time_seconds()))) // BUG-1289: Tribe, Civ, the galaxy menu
            return false;
        creations::Kind k = g_mode == modes::kCell ? creations::kCell
                            : g_mode == modes::kSpace ? creations::kUfo : creations::kCreature;
        creations::Option o = creations::mine(k);
        kind = k;
        name = o.name;
        own = (int)creations::options(k).size();
        return true;
    };
    a.creation_cycle = [](int dir) {
        creations::Kind k = g_mode == modes::kCell ? creations::kCell : creations::kCreature;
        if (g_mode == modes::kSpace)
            return; // the ship is the one of the own empire
        creations::cycle(k, dir);
    };
    a.players = ui_players;
    a.color_next = [](std::string& line) { // F2: the lobby's ">": the next colour of the palette after the last one tried
        static int s_try = -1;
        int cur = pfind::color_index(g_sess.my_id());
        int idx = ((s_try >= 0 ? s_try : cur >= 0 ? cur : 0) + 1) % findx::kColors;
        if (idx == cur)
            idx = (idx + 1) % findx::kColors;
        s_try = idx;
        bool ru = coop_ui::russian();
        const auto& c = findx::kPalette[idx];
        std::string why;
        if (!pfind::request_color(idx, &why)) {
            line = std::string(ru ? c.ru : c.en) + (ru ? ": этот цвет у другого игрока" : ": another player has this colour");
            return false;
        }
        s_try = -1;
        line = g_sess.role() == net::Role::Client ? std::string(ru ? "Цвет запрошен у хоста: " : "Asked the host for ") + (ru ? c.ru : c.en)
                                                  : std::string(ru ? "Ваш цвет: " : "Your colour: ") + (ru ? c.ru : c.en);
        return true;
    };
    a.world_line = ui_world_line;
    a.ready = [] { return g_ready.count(g_sess.my_id()) && g_ready[g_sess.my_id()]; };
    a.set_ready = set_ready;
    a.share_world = [] {
        if (g_vote.active)
            finish_vote("the host started it");
        else
            share_all();
    };
    a.vote_active = [] { return g_vote.active; };
    a.one_creature = [] { return g_sess.role() == net::Role::Client && host_creature_mode(); };
    a.evolve_offer = [] { // BUG-0516
        return g_offer_editor && !g_vote.active ? std::max(1, (int)(g_offer_until - H->time_seconds())) : 0;
    };
    a.share_state = [] { return !(g_adapter && g_local) ? 1 : g_sess.players().size() > 1 ? 0 : 2; };
    a.say = [](const std::string& t) {
        if (support::chat_command(t)) // "/bug <text>" (docs/REPORTING.md#reporting-support)
            return;
        if (g_sess.connected())
            g_sess.send_chat(t);
    };
    a.chat = [] { return std::vector<std::string>(g_chat.begin(), g_chat.end()); };
    a.chat_colors = [] { return std::vector<uint32_t>(g_chat_col.begin(), g_chat_col.end()); };
    a.lan_games = [] {
        std::vector<coop_ui::LanGame> v;
        for (auto& g : g_disc.games(net::now_ms()))
            v.push_back({g.info.name, g.host.str(), g.info.info, g.info.players, g.info.max_players});
        return v;
    };
    a.lan_scan = lan_scan;
    a.local_addresses = [] {
        std::vector<std::string> v;
        for (auto& l : local_addresses())
            v.push_back(l.ip);
        return v;
    };
    a.mode = [] { return game::mode_id(); };
    a.default_port = default_port();
    coop_ui::init(H, M, std::move(a));
}

void on_unload()
{
    support::on_unload();
    gamefixes::shutdown();
    mouse::shutdown(); // its message hook lives in this DLL
    keepalive_stop();
    if (int n = adapters::shutdown())
        LOG("%d address lookup thread(s) still running: the old image stays loaded until they end", n);
    if (int n = autoupdate::shutdown()) // their image stays mapped until they end (BUG-0060)
        LOG("%d update thread(s) still running: the old image stays loaded until they end", n);
    notify::shutdown();
    coop_ui::shutdown();
    lan_scan(false);
    g_sess.leave("mod unload");
    rejoin::shutdown(net::g_fix.join_seen); // a game closed in a session keeps its token for that host (BUG-1135)
    end_session(EndPath::Unload, true);
}

// ---- support diagnostics (diag_coop.h, docs/REPORTING.md#reporting-support) ----
// BUG-0380: the sync-check leaves out a Tribe / Civ pointer that moved within kCursorStill s
constexpr double kCursorStill = 1.0;

int g_ini_auto_load = 1, g_ini_share_npcs = 1, g_ini_update = 1, g_ini_sync = 30;

void support_init()
{
    g_ini_auto_load = H->config_int(M, "coop", "auto_load_world", 1);
    g_ini_share_npcs = H->config_int(M, "coop", "share_npcs", 1);
    g_ini_update = H->config_int(M, "update", "check", 1);
    g_ini_sync = H->config_int(M, "coop", "sync_check", 30);
    support::Probe p;
    p.sess = &g_sess;
    p.feed = feed;
    p.chat_log = [](const std::string& t) { chat_log(t); };
    p.my_name = [] { return g_name; };
    p.entries = sync_all;
    p.stands = [] { return (game_busy() & busy::kPaused) != 0; }; // BUG-1339
    p.players_out = [] { // dead, respawning or not in a world: hidden on purpose (drive_puppet's rules)
        std::vector<uint8_t> v;
        double now = H->time_seconds();
        net::PlayerState own = local_state();
        // BUG-1338 (diag.override_out): while a test makes this game's player states report another mode
        // (`coop.hoststate mode`), the partners hide its creature as the rule for that mode says (the menu: gone) while
        // this game still draws and lists it: its own row is not compared then (lab 20261119-214823 away_stages: `p: 1
        // missing on the guest (p0)` in the override's 10 s). Off: compared.
        if (g_override_out_fix && g_state_mode_override && now < g_state_mode_until)
            v.push_back(g_sess.my_id());
        // BUG-0380: a Tribe / Civ player is a pointer (the planet point under the mouse, moved by the
        // mouse and by the camera); a partner's copy is ~0.1 s + half a round trip behind it, so a
        // moving pointer was "apart" (median 23 m in campaigns, sync-check `p apart`). Left out while
        // it moved in the last second on this machine; a pointer that stands still is compared.
        if (!local_now() || (own.flags & net::PF_DEAD) || !(own.flags & net::PF_IN_WORLD) ||
            modes::rts_marker_moving(g_adapter, local_now(), kCursorStill))
            v.push_back(g_sess.my_id());
        for (auto& [id, r] : g_remotes) {
            if (r.echo)
                continue;
            if (r.buf.empty() || r.buf.age(now) >= 5.0 || (r.buf.latest().flags & net::PF_DEAD) ||
                !(r.buf.latest().flags & net::PF_IN_WORLD) ||
                modes::rts_marker_moving(r.puppet_adapter, puppet_now(r), kCursorStill))
                v.push_back(id);
        }
        return v;
    };
    p.player_speeds = [] {
        std::vector<std::pair<uint8_t, float>> v;
        if (local_now())
            v.push_back({g_sess.my_id(), drive::speed(local_state().vel)});
        for (auto& [id, r] : g_remotes)
            if (!r.echo && !r.buf.empty())
                v.push_back({id, drive::speed(r.buf.latest().vel)});
        return v;
    };
    p.own_position = [](net::Vec3& v) {
        if (!local_now())
            return false;
        v = local_state().pos;
        return true;
    };
    p.partner_states = [] { // BUG-0758
        std::vector<support::Probe::StatePos> v;
        for (auto& [id, r] : g_remotes)
            if (!r.echo && !r.buf.empty())
                v.push_back({id, r.buf.latest().time_ms, r.buf.latest().pos});
        return v;
    };
    p.mode = [] { return g_mode; };
    p.place = [] { return g_world; };
    p.playing = [] { return g_local != nullptr; };
    p.stage_end = [] { return tribe::stage_end(); };
    p.since_share = [](uint8_t peer) { return world::since_share(peer); };
    p.dna = [](float& v) {
        if (!game::avatar() || g_mode != modes::kCreature)
            return false;
        v = game::dna();
        return true;
    };
    p.dna_own = [](uint8_t id, float& v) { return g_mode == modes::kCreature && dna_own_expect(id, v); }; // BUG-1310
    p.bar = [](float& v) { // BUG-1310: the party's evolution bar (compared where a guest has its own DNA)
        if (!game::avatar() || g_mode != modes::kCreature)
            return false;
        v = game::evolution_bar();
        return std::isfinite(v);
    };
    p.cell_food = [](int& v) { return g_mode == modes::kCell && cellsync::food_total(v); };
    p.cell_sent = [](uint8_t id) { return g_mode == modes::kCell ? cellsync::sent_cap(id) : (size_t)0; };
    p.cell_scale = [] { return g_mode == modes::kCell ? cellsync::scale_now() : 0.f; };
    p.over_ground = [](const net::Vec3& v) { // BUG-1216: the planet stages only
        if (g_mode != modes::kCreature && g_mode != modes::kTribe && g_mode != modes::kCiv)
            return -1e9f; // (net::diag::kEntryNoAgl)
        const float r = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), g = r > 1.f ? game::ground_radius(v) : 0.f;
        return g > 0 ? r - g : -1e9f;
    };
    p.vote = [](uint32_t& editor) {
        editor = g_vote.editor;
        return g_vote.active;
    };
    p.activity = [] {
        std::string a = modes::mode_name(g_mode);
        uint32_t got, total;
        if (world::progress(got, total))
            a += ", receiving a world " + std::to_string(got >> 10) + "/" + std::to_string(total >> 10) + " KB";
        if (world::save_busy())
            a += ", saving";
        if (world::load_queued() || g_mode == modes::kLoad)
            a += ", loading";
        if (g_mode == modes::kEditor || g_mode == modes::kEdit)
            a += ", editor";
        if (g_vote.active)
            a += ", vote";
        if (world::cinematic_playing())
            a += ", cinematic";
        if (!g_local)
            a += ", nothing controlled";
        return a;
    };
    p.settings = [] { // what differs from the defaults (coop.ini, and what the window / console changed)
        std::string s;
        auto add = [&](const char* k, int v) { s += (s.empty() ? "" : " ") + std::string(k) + "=" + std::to_string(v); };
        if (!g_share_world)
            add("share_world", 0);
        if (!g_save_on_share)
            add("save_before_share", 0);
        if (g_ini_auto_load != 1)
            add("auto_load_world", g_ini_auto_load);
        if (g_ini_share_npcs != 1)
            add("share_npcs", g_ini_share_npcs);
        if (g_ini_update != 1)
            add("update.check", g_ini_update);
        if (g_ini_sync != 30)
            add("sync_check", g_ini_sync);
        std::string ss = settings::diff(); // the session settings (dna_pace, npcs_target_partners, teamplay, ...)
        if (!ss.empty())
            s += (s.empty() ? "" : " ") + ss;
        return s;
    };
    p.describe = [](const std::string& key) { // BUG-0381: the object's line of coop.npc list / coop.tribe
        std::string all = key[0] == 'n' ? npc::list() : key[0] == 't' ? tribe::list() : std::string();
        std::string want = "#" + key.substr(1) + " ";
        size_t at = 0;
        while ((at = all.find(want, at)) != std::string::npos) {
            if (at >= 2 && all[at - 1] == ' ' && all[at - 2] == ' ') { // "  #<id> " at a line's start
                size_t end = all.find('\n', at);
                return all.substr(at, end == std::string::npos ? std::string::npos : end - at);
            }
            at += want.size();
        }
        return std::string();
    };
    support::init(H, M, p);
    memcensus::init(H, M); // BUG-0631: coop.memcensus (tests)
    H->subscribe(M, SK_EV_SHUTDOWN, [](void*, int, void*) { support::on_shutdown(); }, nullptr);
    H->command_add(M, "coop.diag",
                   "coop.diag [status | sync now|on|off|<s>|force shift|drop|ghost|dna|off | summary | mark <text> | "
                   "detail 0|1|2 | mem | reserve <MB> (tests: a planted leak) | summarysub <folder>|- | summarywrite] - support diagnostics (docs/REPORTING.md#reporting-support)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", support::command(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.bug", "coop.bug <text> - mark a problem in every player's log (like /bug in the co-op chat)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string t;
                       for (int i = 1; i < argc; i++)
                           t += (i > 1 ? " " : "") + std::string(argv[i]);
                       support::mark(t);
                       SAY("marked for the report");
                       return 0;
                   },
                   nullptr, SK_CMD_MAIN_THREAD);
}

SkModInfo g_info = {SK_ABI_VERSION, "coop", COOP_VERSION, on_unload};

// The start of the mod (B2 of 0.7.15): sporekit_mod_main runs these phases in this order, with the console commands
// (coop_cmds.cpp register_commands_*) between them where they were; docs/01-architecture.md#arch-start-and-frame.

// sporekit_mod_main, phase: coop.ini: the player name, the world share switches, the support trace
static void init_config()
{
    char nm[256]; // 24 characters of up to 4 bytes each (BUG-0142: 64 bytes cut a CJK name)
    H->config_str(M, "coop", "name", "Player", nm, sizeof nm);
    g_name = utf8::player_name(nm);
    if (g_name.empty())
        g_name = "Player";
    g_share_world = H->config_int(M, "coop", "share_world", 1) != 0;
    g_save_on_share = H->config_int(M, "coop", "save_before_share", 1) != 0;
    trace::init(H, M);
    trace::g_detail = std::max(0, std::min(2, H->config_int(M, "coop", "log_detail", 1)));
}

// sporekit_mod_main, phase: the world transfer (world.cpp), the shared resources, the game's animation and DNA listeners
static void init_world_share()
{
    world::init(H, M, feed);
    world::set_playing_probe([] { return g_local != nullptr; });
    world::set_place_probe([] { return g_world; });
    world::set_save_blocker(save_blocker);
    world::set_share_failed(share_failed); // BUG-0231
    // species of the host's own creations: sent ahead of its world (creations.h)
    world::set_share_hook([](uint8_t peer, const std::vector<const std::vector<uint8_t>*>& files) {
        creations::world_species_out(peer, files);
    });
    shared::init(H, M);
    shared::set_feed([](const std::string& t) { feed(t); }); // BUG-0696
    world::set_auto_load(H->config_int(M, "coop", "auto_load_world", 1) != 0);
    game::set_anim_listener(on_anim);
    game::set_own_behaviour_anim_listener(on_own_behaviour_anim); // BUG-1253
    game::set_dna_listener(on_dna);
}

// sporekit_mod_main, phase: the modules around the players' creatures and the host's world (npc ... social), each with its context
static void init_player_modules()
{
    npc::Ctx ctx;
    ctx.sess = &g_sess;
    ctx.puppet_of = [](uint8_t id) -> void* {
        auto it = g_remotes.find(id);
        return it == g_remotes.end() ? nullptr : it->second.puppet;
    };
    ctx.puppet_live = [](uint8_t id) { return creature_puppet_live(id); };
    ctx.player_pos = [](uint8_t id, net::Vec3& pos) {
        auto it = g_remotes.find(id);
        if (it == g_remotes.end() || it->second.buf.empty())
            return false;
        const auto& st = it->second.buf.latest();
        if (!(st.flags & net::PF_IN_WORLD) || it->second.buf.age(H->time_seconds()) > 3.0)
            return false;
        pos = st.pos;
        return true;
    };
    ctx.player_dead = [](uint8_t id) {
        auto it = g_remotes.find(id);
        return it != g_remotes.end() && !it->second.buf.empty() && (it->second.buf.latest().flags & net::PF_DEAD);
    };
    ctx.feed = feed;
    // its own NPCs stay while it plays another world (BUG-0130), also until the host's first state
    // says where the host is (a join from inside a game of its own)
    ctx.host_away = [] { return host_absent_for(Absent::Npc); }; // BUG-0130, BUG-0231
    ctx.host_mode = []() -> uint32_t { // BUG-1174 (npc.editor_hold)
        auto it = g_remotes.find(0);
        return it != g_remotes.end() && !it->second.buf.empty() ? it->second.buf.latest().mode : 0u;
    };
    ctx.in_place = [](uint8_t id) {
        if (!g_place_fix)
            return true;
        auto it = g_remotes.find(id);
        return it != g_remotes.end() && !it->second.buf.empty() && same_place(it->second.buf.latest());
    };
    ctx.stage_center = [](net::Vec3& p) { return tribe::center(p); };
    ctx.host_here = [] { return host_here(); }; // BUG-0412
    npc::init(H, M, ctx);
    { // BUG-0373 / BUG-0374 / BUG-0375: creature health between players
        creature_hp::Ctx hc;
        hc.puppet_of = [](uint8_t id) -> void* {
            auto it = g_remotes.find(id);
            return it == g_remotes.end() ? nullptr : it->second.puppet;
        };
        hc.is_host = [] { return g_sess.role() == net::Role::Host; };
        creature_hp::init(H, M, hc);
    }
    { // P1 of 0.7.7 (BUG-0418): the game's player rules carried to the player's own creature
        player_rules::Ctx pc;
        pc.sess = &g_sess;
        pc.puppet_of = [](uint8_t id) -> void* {
            auto it = g_remotes.find(id);
            return it == g_remotes.end() ? nullptr : it->second.puppet;
        };
        pc.puppet_live = [](uint8_t id) { return creature_puppet_live(id); };
        pc.airborne = [](uint8_t id) {
            auto it = g_remotes.find(id);
            return it != g_remotes.end() && !it->second.buf.empty() && (it->second.buf.latest().flags & net::PF_JUMPING);
        };
        player_rules::init(H, M, pc);
    }
    { // BUG-0600: the fight's effects, sprints and voices of the host's members / NPCs on a guest's mirrors
        mirror_fx::Ctx mc;
        mc.sess = &g_sess;
        mirror_fx::init(H, M, mc);
    }
    { // BUG-0451: the creatures' visual effects on the partner's puppet
        effects::Ctx ec;
        ec.sess = &g_sess;
        ec.puppet_of = [](uint8_t id) -> void* {
            auto it = g_remotes.find(id);
            return it == g_remotes.end() ? nullptr : it->second.puppet;
        };
        ec.puppet_live = [](uint8_t id) { return creature_puppet_live(id); };
        ec.all_puppets = [] {
            std::vector<std::pair<uint8_t, void*>> v;
            for (auto& [id, r] : g_remotes)
                v.push_back({id, r.puppet});
            return v;
        };
        ec.near_local = [](const net::Vec3& p) {
            void* av = game::avatar();
            if (av) {
                net::Vec3 a = game::position(av);
                return net::dist(a, p) < 1.8f;
            }
            if (g_adapter && g_adapter->local) {
                void* l = g_adapter->local();
                if (l && net::dist(modes::entity_pos(g_adapter, l), p) < 1.8f)
                    return true;
            }
            // allocation-free: this runs from inside the game's effect transform setter, also in
            // the UI skinner's render pipeline (a vector here faulted in Skinner::StartRender)
            if (g_mode == modes::kTribe)
                return tribe::near_member(p, 1.8f);
            if (g_mode == modes::kCiv)
                return civ::near_vehicle(p, 1.8f);
            return false;
        };
        ec.world = [] { return g_world; };
        ec.here = [](uint8_t id) {
            auto it = g_remotes.find(id);
            if (it == g_remotes.end() || it->second.echo || it->second.buf.empty())
                return false;
            const auto& ls = it->second.buf.latest();
            return same_place(ls) && (ls.flags & net::PF_IN_WORLD) != 0;
        };
        ec.anyone_here = [] {
            for (auto& [id, r] : g_remotes) {
                if (r.echo || r.buf.empty())
                    continue;
                const auto& ls = r.buf.latest();
                if (same_place(ls) && (ls.flags & net::PF_IN_WORLD))
                    return true;
            }
            return false;
        };
        ec.local_pos = [] {
            void* av = game::avatar();
            if (av)
                return game::position(av);
            if (g_adapter && g_adapter->local) {
                void* l = g_adapter->local();
                if (l)
                    return modes::entity_pos(g_adapter, l);
            }
            return net::Vec3{};
        };
        effects::init(H, M, ec);
    }
    { // BUG-0384: eggs at the nests
        nest::Ctx nc;
        nc.sess = &g_sess;
        nc.player_pos = ctx.player_pos;
        nc.in_place = ctx.in_place;
        nc.host_away = ctx.host_away;
        nc.stage_center = ctx.stage_center;
        nest::init(H, M, nc);
    }
    { // BUG-0419 G2: replicated fields (the modules register their kinds in their init)
        fields::Ctx fc;
        fc.sess = &g_sess;
        fc.in_place = ctx.in_place;
        fields::init(H, M, fc);
    }
    { // W1: host-owned world state
        wsync::Ctx wc;
        wc.sess = &g_sess;
        wc.playing = [] {
            const modes::Adapter* a = modes::for_mode(game::mode_id());
            return a && a->local() != nullptr;
        };
        wc.space_sky = [] { // BUG-0712
            if (g_mode != modes::kSpace || !(g_world & 0x40000000u))
                return false;
            if (g_sess.role() != net::Role::Client)
                return true;
            auto it = g_remotes.find(0);
            return it != g_remotes.end() && !it->second.buf.empty() && same_place(it->second.buf.latest());
        };
        wsync::init(H, M, wc);
    }
    { // BUG-0472, BUG-0459: world objects owned by the host
        wobj::Ctx wc;
        wc.sess = &g_sess;
        wc.player_pos = ctx.player_pos;
        wc.in_place = ctx.in_place;
        wc.host_away = ctx.host_away;
        wc.stage_center = ctx.stage_center;
        wc.civ_center = [](net::Vec3& p) { return civ::center(p); };
        // BUG-0490: Space planets (modes place PlanetID | 0x40000000)
        wc.space_on_planet = [] { return g_mode == modes::kSpace && (g_world & 0x40000000u) != 0; };
        wc.space_partner = [](uint8_t id, net::Vec3& pos) {
            auto it = g_remotes.find(id);
            if (it == g_remotes.end() || it->second.echo || it->second.buf.empty() || !(g_world & 0x40000000u))
                return false;
            const net::PlayerState& s = it->second.buf.latest();
            if (s.mode != modes::kSpace || s.world != g_world || it->second.buf.age(H->time_seconds()) > 3.0 ||
                !(s.flags & net::PF_IN_WORLD))
                return false;
            pos = s.pos;
            return true;
        };
        wobj::init(H, M, wc);
    }
    { // W2 (BUG-0564, BUG-0567): the whole planet re-aligned with the host's after transitions
        realign::Ctx rc;
        rc.sess = &g_sess;
        rc.in_place = ctx.in_place;
        rc.host_away = ctx.host_away;
        realign::init(H, M, rc);
    }
    audio::init(H, M); // BUG-0568: the game's audio commands for tests (coop.audio)
    { // BUG-0401: the party's collected creature parts
        parts::Ctx pc;
        pc.sess = &g_sess;
        pc.items = [] { return collected_parts(); };
        pc.guest_in_host_world = [] { return host_world_now() == host_world::HostWorld::Same; };
        pc.world = [] { return g_world; };
        pc.feed = feed;
        pc.cell_parts = [] { return cellsync::part_states(); }; // BUG-0467
        parts::init(H, M, pc);
    }
    savestrip::init(H, M); // BUG-0020: our objects never go into a save
    views::init(H, M);     // BUG-0280: the world lives around every player
    npc::set_enabled(H->config_int(M, "coop", "share_npcs", 1) != 0);
    { // F3 of 0.7.12: the session settings (coop.ini dna_pace, npcs_target_partners, teamplay are their defaults)
        settings::Ctx sc;
        sc.sess = &g_sess;
        sc.playing = [] { return stage_mode(g_mode) && g_local != nullptr; };
        sc.world_name = [] { return modes::current_world_name(); };
        sc.rivals = [] { return spaceext::rivals() || pfind::g_test_rivals; }; // (tests: coop.find rivals 1)
        sc.feed = feed;
        settings::init(H, M, sc);
        store::Ctx stc; // E1 (BUG-1149): the per-player store on the host's disk
        stc.sess = &g_sess;
        stc.feed = feed;
        stc.notice = feed; // the host only (the hint names a console command of the host)
        store::init(H, M, stc);
        autosave::init(H, M, &g_sess, feed); // E2 (BUG-0864)
        g_sess.join_closed = [] { return !settings::on(settings::kLateJoin) && settings::started(); };
    }
    { // F2 of 0.7.12: player colours, arrows to partners, partners on the minimap
        pfind::Ctx fc;
        fc.sess = &g_sess;
        fc.feed = feed;
        pfind::init(H, M, fc);
    portrait::init(H, M, [] { return g_mode == modes::kCreature; }); // BUG-0937
    }
    { // F4 of 0.7.12 (BUG-0767): one report for the session
        sreport::Ctx rc;
        rc.sess = &g_sess;
        rc.make_report = [](const char* why) { return make_report(why); };
        rc.shot = [] { return support::report_shot("report", "the host asked for the session report"); };
        rc.shot_state = [] { return support::report_shot_state(); };
        rc.feed = feed;
        rc.chat_log = [](const std::string& s) { chat_log(s); }; // (F2 gave chat_log a colour argument)
        rc.open_window = [] { coop_ui::open_lobby(); };
        sreport::init(H, M, rc);
    }
    social::Ctx sc;
    sc.sess = &g_sess;
    sc.feed = feed;
    sc.notice = [](const std::string& t) { notice(t, -1); };
    sc.puppet_of = [](uint8_t id) -> void* {
        auto it = g_remotes.find(id);
        return it == g_remotes.end() ? nullptr : it->second.puppet;
    };
    sc.blocked = [] { return g_menu_up.load() || coop_ui::is_open() || g_text_up.load(); };
    sc.pace = dna_pace_gain; // BUG-0510
    sc.elsewhere = [] { return guest_elsewhere(); }; // BUG-0420
    social::init(H, M, sc);
    game::set_damage_filter(npc::damage_filter);
}

// sporekit_mod_main, phase: the support log, the session callbacks, the frame / fault / overlay subscriptions, the keepalive thread
static void init_session()
{
    support_init();
    setup_callbacks();
    H->subscribe(M, SK_EV_FRAME, on_frame, nullptr);
    if (host_has_diag() && H->diag_context) {
        H->subscribe(M, SK_EV_FAULT, on_fault, nullptr);
        H->diag_context(M, diag_context_fn, nullptr);
        H->log(M, SK_INFO, "SporeCoop %s on SporeKit %s, game %s", COOP_VERSION, H->kit_version(), H->build_name());
    }
    keepalive_start();
    H->subscribe(M, SK_EV_OVERLAY, on_overlay, nullptr);
}

// sporekit_mod_main, phase: teamplay and the game hooks of the creature stage and its editors (vote.cpp, puppets.cpp)
static void init_creature_hooks()
{
    teamplay::Ctx tc;
    tc.partners = partners;
    tc.notice = [](const std::string& t) { notice(t, -1); };
    tc.my_name = [] { return g_name; };
    teamplay::init(H, M, std::move(tc));
    if (uintptr_t a = H->sym("Simulator::CreatureSelectionUI"))
        H->hook_create(M, (void*)a, (void*)h_SelectUI, (void**)&o_SelectUI);
    a_AbilityModeCore = H->sym("Simulator::cCreatureGameData::GetAbilityMode"); // BUG-0913
    if (uintptr_t a = H->sym("SporeCoop::Havok_GroupFilter_IsCollisionEnabled"))
        H->hook_create(M, (void*)a, (void*)h_CollEnabled, (void**)&o_CollEnabled);
    if (uintptr_t a = H->sym("SporeCoop::CreatureHoverFilter"))
        H->hook_create(M, (void*)a, (void*)h_HoverFilter, (void**)&o_HoverFilter);
    *(int32_t*)(g_blank_e84 + 0x32C) = -1;
    if (uintptr_t a = H->sym("Simulator::CreatureStageEventHandler"))
        H->hook_create(M, (void*)a, (void*)h_StageMsg, (void**)&o_StageMsg);
    if (uintptr_t a = H->sym("Editors::EditorRequest::Submit"))
        H->hook_create(M, (void*)a, (void*)h_Submit, (void**)&o_Submit);
    if (uintptr_t a = H->sym("SporeCoop::CellStage_ToCreature")) // BUG-0315
        H->hook_create(M, (void*)a, (void*)h_CellToCreature, (void**)&o_CellToCreature);
    if (uintptr_t a = H->sym("SporeCoop::CellStage_End")) // BUG-1269
        H->hook_create(M, (void*)a, (void*)h_CellStageEnd, (void**)&o_CellStageEnd);
    if (uintptr_t a = H->sym("SporeCoop::CreatureGame_HandleMessage")) // BUG-0466
        H->hook_create(M, (void*)a, (void*)h_CreatureGameMsg, (void**)&o_CreatureGameMsg);
    if (uintptr_t a = H->sym("SporeCoop::Simulator_HandleMessage"))
        H->hook_create(M, (void*)a, (void*)h_SimMsg, (void**)&o_SimMsg);
    if (uintptr_t a = H->sym("Editors::cEditor::GetBlockingErrors"))
        H->hook_create(M, (void*)a, (void*)h_BlockingErrors, (void**)&o_BlockingErrors);
}

// sporekit_mod_main, phase: Tribe, the world events, the notifications, the players' creations and the puppets' models
static void init_tribe_and_creations()
{
    tribe::Ctx trc;
    trc.sess = &g_sess;
    trc.feed = feed;
    trc.notice = [](const std::string& t) { notice(t, -1); };
    trc.host_away = [] { return host_absent_for(Absent::Tribe); }; // BUG-0151, BUG-0231
    tribe::init(H, M, std::move(trc));
    drama::init(H, M, &g_sess); // BUG-0486
    cocoon::init(H, M);           // WP-K (BUG-1303)
    notify::init(H, M, [] { return coop_ui::text_font(); });
    creations::Ctx crc;
    crc.sess = &g_sess;
    crc.feed = feed;
    crc.notice = [](const std::string& t) { notice(t, -1); };
    crc.name_of = [](uint8_t id) { return g_sess.name_of(id); };
    crc.cell_rebuilt = [] { cellsync::join_partner(); };
    crc.appearance_changed = [](uint8_t id) { // rebuild that player's puppet with its own look
        auto it = g_remotes.find(id);
        if (it != g_remotes.end()) {
            it->second.relook = it->second.puppet != nullptr; // BUG-0923: built again in a frame or two, not a new arrival
            despawn(it->second);
        }
    };
    creations::init(H, M, std::move(crc));
    species::init(H, M, &g_sess); // WP-0 (BUG-1270)
    civedit::init(H, M, &g_sess); // WP-C3 (BUG-1275)
    civ_arrival_init();           // WP-X1 (BUG-1276)
    modes::g_species_for_player = [](uint8_t id) { return creations::player_species(id); };
    modes::g_model_for_player = [](uint8_t id, int kind, uint32_t key[3]) {
        creations::Key k = creations::player_key(id, (creations::Kind)kind);
        key[0] = k.instance; key[1] = k.type; key[2] = k.group;
        return k.instance != 0;
    };
}

// sporekit_mod_main, phase: the Cell stage (cellsync.cpp)
static void init_cell()
{
    cellsync::Ctx cc;
    cc.sess = &g_sess;
    cc.partners = partners;
    cc.notice = [](const std::string& t) { notice(t, -1); };
    cc.food_cause = [](int from, int before, int amount) { // Cell (dna_ledger.h)
        LOG("dna: food %d -> %d cause=meal:%d%+d", before, before + amount, from, amount);
        dna_note({dna_ledger::Cause::Meal, (uint8_t)from, (float)amount}, g_sess.connected() ? g_sess.my_id() : 0);
    };
    cc.my_name = [] { return g_name; };
    cc.pace_meal = dna_pace_meal; // BUG-0510
    cc.pace_announce = dna_pace_announce;
    cc.pace_heard = dna_pace_heard;
    cc.puppet_owner = [](void* cell) -> int {
        for (auto& [id, r] : g_remotes)
            if (r.puppet == cell && !r.echo)
                return id;
        return -1;
    };
    cc.puppets = [] {
        std::vector<cellsync::Ctx::Puppet> v;
        if (g_mode != modes::kCell)
            return v;
        for (auto& [id, r] : g_remotes)
            if (r.puppet && !r.echo)
                v.push_back({id, r.name, r.puppet});
        return v;
    };
    cc.host_away = [] { return g_host_away; }; // BUG-0205
    cellsync::init(H, M, std::move(cc));
}

// sporekit_mod_main, phase: Civ, the census, the squadron, Space, the start-up switches, the game fixes, the updater
static void init_civ_and_space()
{
    civ::Ctx cvc;
    cvc.sess = &g_sess;
    cvc.feed = feed;
    cvc.notice = [](const std::string& t) { notice(t, -1); };
    cvc.role_of = [](uint8_t id) { return g_roles.count(id) ? g_roles[id] : (uint8_t)0; };
    cvc.vote = [](const std::string& t, const std::string& d, std::function<void()> a) {
        return start_action_vote(t, d, std::move(a));
    };
    cvc.beacon = add_beacon;
    cvc.space_place = [] { return g_mode == modes::kSpace ? g_world : 0u; }; // BUG-0751
    cvc.host_away = [] { return host_absent_for(Absent::Civ); }; // BUG-0151, BUG-0231
    civ::init(H, M, std::move(cvc));
    census::Ctx csc;
    csc.sess = &g_sess;
    csc.place = [](uint32_t& m, uint32_t& w) {
        m = g_mode;
        w = g_world;
    };
    csc.self_pos = [](net::Vec3& p) {
        if (!local_now())
            return false;
        p = local_state().pos;
        return true;
    };
    csc.puppet_owner = [](void* c) -> int {
        for (auto& [id, r] : g_remotes)
            if (r.puppet == c && !r.echo)
                return id;
        return -1;
    };
    csc.wallets = [](uint32_t mode, int host_spent) { // BUG-1310
        std::string o;
        char b[96];
        for (auto& p : g_sess.players()) {
            float v = -1;
            int sp = -1;
            bool cr = mode == modes::kCreature && dna_own_expect(p.id, v);
            bool ce = mode == modes::kCell && dna_own_expect_cell(p.id, host_spent, sp);
            if (p.id == 0 || (!cr && !ce))
                continue;
            snprintf(b, sizeof b, "st wallet id=%u dna=%.1f spent=%d\n", p.id, cr ? v : -1.f, ce ? sp : -1);
            o += b;
        }
        return o;
    };
    census::init(H, M, std::move(csc));
    squadron::Ctx sqc;
    sqc.sess = &g_sess;
    sqc.with_host = [] {
        auto it = g_remotes.find(0);
        return it != g_remotes.end() && !it->second.buf.empty() && same_place(it->second.buf.latest());
    };
    sqc.puppet_owner = [](void* u) -> int {
        for (auto& [id, r] : g_remotes)
            if (r.puppet == u && !r.echo)
                return id;
        return -1;
    };
    sqc.puppet_of = [](uint8_t id) -> void* {
        auto it = g_remotes.find(id);
        return it == g_remotes.end() ? nullptr : it->second.puppet;
    };
    squadron::init(H, M, std::move(sqc));
    spacecoop::Ctx spc;
    spc.sess = &g_sess;
    spc.notice = [](const std::string& t) { // every machine words its own messages (MSG_SPACE)
        feed(t);
        chat_log("* " + t);
    };
    spc.my_place = [] { return g_mode == modes::kSpace ? g_world : 0u; };
    spc.partners = [] {
        std::vector<spacecoop::Partner> v;
        for (auto& [id, r] : g_remotes) {
            if (r.echo || r.buf.empty())
                continue;
            const net::PlayerState& s = r.buf.latest();
            if (s.mode != modes::kSpace)
                continue;
            bool playing = r.buf.age(H->time_seconds()) <= 3.0 && (s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD);
            v.push_back({id, r.name, s.world, s.pos, r.puppet, playing});
        }
        return v;
    };
    spc.beacon = add_beacon;
    spacecoop::init(H, M, std::move(spc));
    apply_startup_off(); // tests: switches that must be off before the game starts (fixes_off.txt)
    gamefixes::init(H, M);
    creationprops::init(H, M); // BUG-1328
    autoupdate::set_feed(feed);
    autoupdate::init(H, M, COOP_VERSION);
}
} // namespace coop_core

extern "C" SK_EXPORT const SkModInfo* sporekit_mod_main(const SkHost* host, SkMod* self)
{
    H = host;
    M = self;
    g_host_for_modes = host;
    g_mod_for_modes = self;
    if (!game::init(H, M)) {
        H->log(M, SK_ERROR, "required game symbols missing - coop disabled");
        return nullptr;
    }
    init_config();
    init_world_share();
    init_player_modules();
    init_session();
    register_commands_core();
    coop_register_debug_commands(H, M);
    init_creature_hooks();
    register_commands_editor();
    init_tribe_and_creations();
    register_commands_civ();
    init_cell();
    register_commands_cell();
    init_civ_and_space();
    register_commands_stages();
    init_ui();
    adapters::init(H, M);
    register_commands_ui();
    H->cheat_register("coop", "coop.",
                      "coop host [port] | join <ip[:port]> | leave | status | name <nick> | say <text>");
    LOG("coop ready (name '%s')", g_name.c_str());
    return &g_info;
}
