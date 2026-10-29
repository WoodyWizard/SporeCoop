// SporeCoop mod: creature-stage co-op ("pack" mode). See docs/03-gameplay.md.
// the release version (tools/package.sh names the zip after it)
#ifndef COOP_VERSION
#define COOP_VERSION "0.7.3.0"
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
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

#include "coop_ui.h"
#include "driver.h"
#include "game.h"
#include "modes.h"
#include "stage_epoch.h"
#include "mouse.h"
#include "shared.h"
#include "sui.h"
#include "teamplay.h"
#include "tribe.h"
#include "cellsync.h"
#include "census.h"
#include "civ.h"
#include "autoupdate.h"
#include "gamefixes.h"
#include "spacecoop.h"
#include "squadron.h"
#include "npc.h"
#include "rejoin.h"
#include "savestrip.h"
#include "cell_logic.h"
#include "social.h"
#include "world.h"
#include "net/discovery.h"
#include "net/fixes.h"
#include "net/give_target.h"
#include "net/interp.h"
#include "net/lobby.h"
#include "net/session.h"
#include "net/utf8.h"
#include "creations.h"
#include "notify.h"
#include "notify_text.h"
#include "syncdbg.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "share_wait.h"
#include "dna_route.h"
#include "trace.h"

static const SkHost* H;
static SkMod* M;
extern const SkHost* g_host_for_modes; // modes.cpp
extern SkMod* g_mod_for_modes;

void coop_register_debug_commands(const SkHost* host, SkMod* self); // debug.cpp

#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)
#define SAY(...) H->out_printf(out, __VA_ARGS__)

namespace {

struct Remote {
    uint8_t id = 0;
    std::string name;
    net::SnapshotBuffer buf;
    void* puppet = nullptr;
    drive::State drv;
    const modes::Adapter* puppet_adapter = nullptr; // adapter that created `puppet`
    uint32_t puppet_world = 0; // the place `puppet` was made in
    bool echo = false; // local loopback test remote
    uint64_t world_sent = 0; // host: (mode << 32 | save crc) last sent to this guest
    double other_mode_since = -1; // host: since when the guest plays another mode
    bool joined_phase = false; // host: the guest played the host's mode after the last world send
    bool needs_world = false;  // host: joined while the host was not playing; send the world later
    double cine_wait_since = 0; // host: since when this guest's share waits for a cinematic (BUG-0232)
};

net::Session g_sess;
std::map<uint8_t, Remote> g_remotes;
std::string g_name = "Player";
double g_last_send = 0;
void* g_last_avatar = nullptr;
std::deque<std::pair<double, std::string>> g_feed;
bool g_overlay = true;
bool g_share_world = true; // host sends its save to joining guests
bool g_anim_log = false;
double g_last_time_sync = 0;
float g_dna_sent = -1;      // host: last broadcast total
double g_dna_at = 0;        // host: when it was sent
uint32_t g_dna_rx = 0, g_dna_fwd = 0;

// DNA earned locally: the host's total is authoritative
extern bool g_host_away;
extern float g_solo_dna;
extern uint32_t g_solo_mode, g_mode;
// BUG-0151: a goal card's reward a guest earns while the host is away is kept like any other DNA
// (social::goal_dna sent it to a host that drops it); the host keeps a guest's MSG_DNA_ADD that
// arrives while it has no creature (editor, load) and pays it when it has one again.
// `coop.corefix away_dna 0`: the old behaviour (tests).
bool g_away_dna_fix = true;
COOP_FIX("core.away_dna", "BUG-0151", g_away_dna_fix, "core: DNA a guest earns while the host is away is kept");
float g_dna_owed = 0;     // host: guests' DNA that arrived without an avatar
uint32_t g_dna_owed_paid; // tests
bool on_dna(float delta)
{
    if (g_away_dna_fix && g_sess.role() == net::Role::Client && g_sess.connected() && delta > 0 && g_host_away) {
        if (g_solo_dna <= 0)
            g_solo_mode = g_mode; // the stage it is earned in (BUG-0233)
        g_solo_dna += delta; // nobody to send it to: kept here, given to the pack on the host's return
        return true;
    }
    if (social::goal_dna(delta))
        return false; // a goal card's reward: the host pays it into the shared pool once
    if (g_sess.role() == net::Role::Client && g_sess.connected() && delta > 0 && g_host_away) {
        g_solo_dna += delta; // nobody to send it to: kept here, given to the pack on the host's return
        return true;
    }
    if (g_sess.role() == net::Role::Client && g_sess.connected() && delta > 0) {
        net::Writer w;
        w.f32(delta);
        g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
        g_dna_fwd++;
        return false; // applied when the host's total comes back
    }
    return true;
}
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
std::map<uint8_t, bool> g_ready;   // by player id (the host keeps the truth)
std::map<uint8_t, uint8_t> g_roles; // civilization roles by player id (the host keeps the truth)
// roles end with their player and session, the lobby table keeps them with their ids (BUG-0064,
// BUG-0079); `coop.role fix 0` = the code before (tests)
bool g_role_fix = true;
COOP_FIX("role.fix", "BUG-0064 BUG-0079", g_role_fix, "civ: roles end with their player and session (coop.role fix)");
std::map<uint8_t, int> g_lobby_rtt; // guest: host-measured rtt of every player
bool g_lobby_dirty = false;
double g_last_lobby = 0;
net::Discovery g_disc;
bool g_disc_on = false;
int g_lan_fake_n = 0;           // coop.lanfake: games on the network that answer (tests, BUG-0183)
uint32_t g_lan_fake_until = 0;

void chat_log(const std::string& s)
{
    g_chat.push_back(s);
    while (g_chat.size() > 50)
        g_chat.pop_front();
}

// an event everybody sees (feed + lobby chat): guests send it to the host, the host relays it
void notice(const std::string& text, int from = -1)
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

struct LocalAddr {
    std::string ip;
    uint32_t broadcast; // network byte order
};
std::vector<LocalAddr> local_addresses()
{
    static std::vector<LocalAddr> cache;
    static DWORD at = 0;
    if (!cache.empty() && GetTickCount() - at < 10000)
        return cache;
    at = GetTickCount();
    cache.clear();
    ULONG len = 16384;
    std::vector<uint8_t> buf(len);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, aa, &len) != NO_ERROR)
        return cache;
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sin = (sockaddr_in*)u->Address.lpSockaddr;
            if (!sin || sin->sin_family != AF_INET)
                continue;
            uint32_t ip = sin->sin_addr.s_addr;
            const uint8_t* b = (const uint8_t*)&ip;
            if (b[0] == 127 || (b[0] == 169 && b[1] == 254))
                continue;
            int plen = u->OnLinkPrefixLength;
            uint32_t mask = plen <= 0 ? 0 : htonl(plen >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> plen));
            char s[32];
            snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            cache.push_back({s, (ip & mask) | ~mask});
        }
    }
    return cache;
}

void lan_scan(bool on)
{
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

// BUG-0091: a creature puppet can be freed by the game before the mod notices (a world teardown
// between two frames: the messages queued meanwhile are handled first). Animations and despawns
// only touch a creature puppet that is still in the game's object list. `coop.framefix 0` restores
// the old order and the unchecked calls (tests).
bool g_frame_fix = true;
COOP_FIX_UNSAFE("core.frame", "BUG-0091", g_frame_fix, "core: animations and despawns only touch a puppet the game still lists (coop.framefix)");
// calls that met a puppet the game no longer lists: dropped (fix on) / made anyway (framefix 0)
uint32_t g_anims_dropped = 0, g_anims_unlisted = 0, g_despawns_dropped = 0, g_despawns_unlisted = 0;
bool creature_puppet(const modes::Adapter* a)
{
    return a && (a == modes::for_mode(modes::kCreature) || a == modes::for_mode(modes::kScenario));
}

void despawn(Remote& r)
{
    if (r.puppet && r.puppet_adapter) {
        if (creature_puppet(r.puppet_adapter) && !game::creature_listed(r.puppet)) {
            (g_frame_fix ? g_despawns_dropped : g_despawns_unlisted)++;
            LOG("puppet of %s is gone already (%p): forgotten%s", r.name.c_str(), r.puppet,
                g_frame_fix ? "" : ", destroyed anyway (framefix 0)");
            if (g_frame_fix)
                game::puppet_unregister(r.puppet);
            else
                r.puppet_adapter->despawn(r.puppet);
        } else
            r.puppet_adapter->despawn(r.puppet);
    }
    r.puppet = nullptr;
    r.puppet_adapter = nullptr;
}

uint32_t g_mode = 0, g_world = 0;
bool g_save_on_share = true; // host saves before sending the world, so guests get the current state
const modes::Adapter* g_adapter = nullptr;
bool g_puppet_hold = false; // tests (coop.puppet hold): remote puppets are not driven (BUG-0251), never saved
void* g_local = nullptr; // entity the local player controls (adapter specific)

// Creature puppets the game did not destroy when we forgot them: the creature stage world stays
// through the editor (the local avatar is gone meanwhile), so the old puppet stood on as an
// unregistered creature, an NPC with its own AI in the nestless herd made for it. Its behaviour
// tree then searched land around that herd's home, which could lie deep inside the planet, and
// read past the planet's cube map (host crash 0xB88BEA, 2026-10-25; docs/re/creature.md 10).
// Such puppets are removed once the player is back in the same world.
struct Stray {
    void* c;
    void* herd;
    uint32_t species;
    uint32_t world;
    double since;
};
std::vector<Stray> g_strays;
uint32_t g_strays_removed = 0;
bool g_strays_fix = true; // tests: off = left-over puppets stay (the bug of BUG-0007, coop_editor_strays.py)
COOP_FIX("core.strays", "BUG-0007", g_strays_fix, "creature: a partner's puppet left over after an editor is removed");

void forget_puppets() // world changed or the local player left it (editor, load)
{
    const modes::Adapter* creature = modes::for_mode(modes::kCreature);
    for (auto& [id, r] : g_remotes) {
        if (r.puppet) {
            game::puppet_unregister(r.puppet);
            if (r.puppet_adapter == creature && game::creature_listed(r.puppet))
                g_strays.push_back({r.puppet, game::herd_of(r.puppet), game::species_instance(r.puppet), r.puppet_world,
                                    H->time_seconds()});
        }
        r.puppet = nullptr;
        r.puppet_adapter = nullptr;
    }
}

void remove_strays(double now)
{
    if (g_strays.empty() || g_mode != modes::kCreature || !g_local)
        return;
    if (!g_strays_fix) {
        g_strays.clear();
        return;
    }
    for (auto& s : g_strays) {
        // the same creature (pointer still listed, same herd), the same world, not ours again, not
        // destroyed already (npc.cpp remove_saved_strays meets it too: untuned, in a herd like ours)
        if (s.world == g_world && now - s.since < 3600 && game::creature_listed(s.c) && !game::is_destroyed(s.c) &&
            game::herd_of(s.c) == s.herd &&
            game::species_instance(s.c) == s.species && !game::is_puppet(s.c) && s.c != game::avatar()) {
            LOG("a puppet left over from before (%p, herd %p) removed", s.c, s.herd);
            game::destroy_creature(s.c);
            g_strays_removed++;
        }
    }
    g_strays.clear();
}


// tests (coop.hoststate mode <hex> <seconds>, BUG-0233): the mode our states report, for a while
uint32_t g_state_mode_override = 0;
double g_state_mode_until = 0;

// BUG-0271: what our last frame found may be gone by now. Console commands and network callbacks
// also run between frames, even from the game's message pump inside a stage teardown (the host's
// coop.status / coop.avatar read a freed cell while the game went Cell -> Creature, with the mode
// already Creature). The local entity counts only while the mode is still the frame's, the stage is
// not being left and the adapter still finds it; a puppet only while the mode is the frame's and its
// adapter still sees it alive. Inside on_frame these always agree. `coop.fix core.stale_local off`.
static bool frame_world_now()
{
    return game::mode_id() == g_mode && !game::leaving_stage();
}

static uint32_t g_local_refused, g_puppet_refused; // coop.cellgone (tests)

void* local_now()
{
    if (!g_adapter || !g_local || !modes::stale_fix)
        return g_local;
    if (frame_world_now() && g_adapter->local() == g_local)
        return g_local;
    g_local_refused++;
    return nullptr;
}

void* puppet_now(const Remote& r)
{
    if (!r.puppet || !r.puppet_adapter || !modes::stale_fix)
        return r.puppet;
    if (!frame_world_now() || (r.puppet_adapter->alive && !r.puppet_adapter->alive(r.puppet))) {
        g_puppet_refused++;
        return nullptr;
    }
    return r.puppet;
}

net::PlayerState local_state()
{
    net::PlayerState st;
    st.mode = g_state_mode_override && H->time_seconds() < g_state_mode_until ? g_state_mode_override : g_mode;
    st.world = g_world;
    if (void* local = local_now())
        g_adapter->fill(local, st);
    return st;
}

// remote `s` is visible here only when both play the same mode in the same world
bool same_place(const net::PlayerState& s) { return s.mode == g_mode && s.world == g_world && g_world != 0; }

void drive_puppet(Remote& r, double now)
{
    net::PlayerState s;
    if (!r.buf.sample(now, s))
        return;
    // dead players are hidden until they respawn
    bool visible = g_adapter && g_local && (s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD) &&
                   r.buf.age(now) < 5.0 && (r.echo || same_place(s));
    if (r.puppet && !visible) // why a partner disappears (docs/stability/cell.md)
        LOG("%s hidden: flags=0x%X age=%.1f same_place=%d", r.name.c_str(), (unsigned)s.flags, r.buf.age(now),
            (int)same_place(s));
    if (r.puppet && r.puppet_adapter && r.puppet_adapter->alive && !r.puppet_adapter->alive(r.puppet)) {
        LOG("puppet of %s removed by the game itself (mode %08X, place %08X): shown again", r.name.c_str(), g_mode,
            g_world);
        r.puppet = nullptr; // removed by the game itself: show it again
        r.puppet_adapter = nullptr;
    }
    if (!visible || (r.puppet && r.puppet_adapter != g_adapter)) {
        despawn(r);
        if (!visible)
            return;
    }
    if (!r.puppet) {
        r.puppet = g_adapter->spawn(s, g_local);
        if (!r.puppet) {
            feed("failed to show " + r.name);
            return;
        }
        r.puppet_adapter = g_adapter;
        r.puppet_world = g_world;
        r.drv = drive::State{};
        feed(r.name + " appeared in the world");
        if (g_mode == modes::kCreature) // which body the partner got here and why (support trace)
            trace::line("body", "puppet of %u '%s': species %08X here; its creation here %s, its own game reports %08X%s",
                        r.id, r.name.c_str(), game::species_instance(r.puppet),
                        trace::hex(creations::player_key(r.id, creations::kCreature).instance).c_str(), s.species,
                        creations::player_key(r.id, creations::kCreature).instance ? "" : " (none received: built from our species)");
        else if (g_mode == modes::kCell)
            trace::line("body", "puppet of %u '%s': cell model %08X here; its creation here %s", r.id, r.name.c_str(),
                        *(uint32_t*)((uint8_t*)r.puppet + 0xFC),
                        trace::hex(creations::player_key(r.id, creations::kCell).instance).c_str());
    }
    if (!g_puppet_hold)
        g_adapter->drive(r.puppet, s, r.drv, now);
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
// opens up to ~20 s after the host's). `coop.corefix away_editor 0`: only the main menu (tests).
bool g_away_editor_fix = true;
COOP_FIX("core.away_editor", "BUG-0205", g_away_editor_fix, "core: the host in an editor or a load for 4 s counts as away");
extern uint32_t g_own_editor_due;
// BUG-0233: an editor or a load of the host counts as away only in the stages whose modules play
// on locally meanwhile (npc in Creature, tribe / civ local_play); in Cell and Space the guest got
// "you play on alone" / "is back" for every editor visit of the host. The main menu counts in every
// stage (BUG-0151). Off: every stage (0.7.3.0).
constexpr uint32_t kAwayStages[] = {modes::kCreature, modes::kTribe, modes::kCiv};
bool g_away_stages_fix = true;
COOP_FIX("core.away_stages", "BUG-0233", g_away_stages_fix, "core: a host editor or load counts as away only in Creature, Tribe, Civ");
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
char host_out_of_stage()
{
    if (g_sess.role() != net::Role::Client || !g_sess.connected() || !stage_mode(g_mode) || !g_local)
        return 0;
    auto it = g_remotes.find(0);
    if (it == g_remotes.end() || it->second.buf.empty())
        return 0;
    uint32_t m = it->second.buf.latest().mode;
    if (m == modes::kMenu)
        return 'm';
    if (!g_away_editor_fix || g_own_editor_due)
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
// `coop.corefix same_place 0` = the old behaviour (tests).
bool g_place_fix = true;
COOP_FIX("core.same_place", "BUG-0130", g_place_fix, "core: nothing of the host's world is applied to a guest in another world");
uint32_t g_place_dropped = 0;
float g_dna_pending = -1; // the host's DNA total that arrived while we were elsewhere
// unknown: what to say before the host's first state arrived (just joined)
bool host_elsewhere(bool unknown = false)
{
    if (!g_place_fix || g_sess.role() != net::Role::Client || !stage_mode(g_mode))
        return false;
    auto it = g_remotes.find(0);
    if (it == g_remotes.end() || it->second.buf.empty())
        return unknown; // not known yet
    const auto& ls = it->second.buf.latest();
    return ls.mode == g_mode && !same_place(ls);
}

// BUG-0231: a guest in another world of the host's Civ or Tribe stage applied the host's vehicle,
// city and alarm state to its own planet (bound and drove its own vehicles as mirrors, killed the
// rest, founded cities up to the host's count) and its Tribe/Civ AI and orders stood still
// (`local_play` followed only the host's main menu / editor). The Civ messages are dropped like the
// rest of the host's world, and Tribe and Civ play on locally while the guest is elsewhere.
// Off: as in 0.7.3.0.
bool g_world_gate_fix = true;
COOP_FIX("core.world_gate", "BUG-0231", g_world_gate_fix, "core: a guest in another world applies no Civ state and plays its tribe / civ itself");

// a message of the host's world state a guest elsewhere must not apply
static bool world_state_msg(uint8_t type)
{
    if (g_world_gate_fix && (type == net::MSG_CIV_STATE || type == net::MSG_CIV_BUILD || type == net::MSG_CIV_ALARM))
        return true;
    return type == net::MSG_DNA || type == net::MSG_WORLD_TIME ||
           (type >= net::MSG_CELL_STATE && type <= net::MSG_CELL_FOOD) || type == net::MSG_CELL_KO ||
           type == net::MSG_CELL_BITTEN || type == net::MSG_TRIBE_STATE || type == net::MSG_TRIBE_CMD ||
           type == net::MSG_SPECIES || type == net::MSG_SOCIAL;
}

void host_away_tick(double now)
{
    if (stage_mode(g_mode) && g_world)
        g_stage_world = g_world;
    if (g_dna_owed > 0 && g_sess.role() == net::Role::Host) {
        auto o = dna_route::owed(g_away_dna_stage_fix, g_mode, stage_mode(g_mode), game::avatar() != nullptr, g_world,
                                 g_dna_owed_world, modes::kCreature);
        if (o == dna_route::Owed::Pay) {
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
    if (g_dna_pending >= 0 && g_sess.role() == net::Role::Client && host_here() && game::avatar() && !g_host_away) {
        float d = g_dna_pending - game::dna(); // the host's total, once we play its world
        if (d != 0)
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
    bool change = g_host_away ? host_here() || !g_sess.connected() || !stage_mode(g_mode) : why != 0;
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

// Civilization and Space editors change designs (buildings, vehicles, ships, the anthem), not the
// species: the vote and the notices say so (until 2026-10-23 a house design "evolved the species")
bool design_stage(uint32_t m) { return m == modes::kCiv || m == modes::kSpace; }
void add_beacon(const net::Vec3& p, const std::string& text, double seconds, uint32_t argb);
bool start_action_vote(const std::string& text, const std::string& done, std::function<void()> action);

// Phase events of the host. The species is shared: when the host comes back from an editor
// (evolution inside a stage) or starts a new stage, every guest gets the new world at once.
uint32_t g_last_stage = 0;  // last stage the local player played
bool g_was_in_editor = false;

// ---- evolution vote: the host's editor request (next stage or evolution inside a stage) waits
// until every player is ready (or 60 s / "Start now"), then the game goes on ----
struct Vote {
    bool active = false;
    void* request = nullptr; // Editors::EditorRequest*, one reference held
    double t0 = 0;
    std::string text;
    int left = 0;
    uint32_t editor = 0; // the editor the host opens (EditorRequest+0xC); guests open the same for their own creature
    uint32_t mode = 0;   // host: the stage and world the vote belongs to (BUG-0043)
    uint32_t world = 0;
};
Vote g_vote;
// BUG-0043: a vote ends with its stage, its world and its session (its action or editor request is
// dropped, not run later on a freed civilization or in another save). `coop.corefix vote_scope 0`
// restores the old behaviour (tests).
bool g_vote_scope_fix = true;
COOP_FIX_UNSAFE("core.vote_scope", "BUG-0043", g_vote_scope_fix, "core: a vote ends with its stage, world and session");
// BUG-0234: a guest takes the editor id also from the message that ends a vote: a cancelled vote
// (drop_vote) ends with 0 and opens no own editor; a passed one (finish_vote) ends with its id.
// Off: the id only from "on" messages, every ended vote opened the guest's editor (0.7.3.0).
bool g_vote_drop_editor_fix = true;
COOP_FIX("core.vote_drop_editor", "BUG-0234", g_vote_drop_editor_fix, "core: a cancelled vote opens no guest editor");
// BUG-0234: a guest's own editor still due from a vote is forgotten when the session ends (it
// opened offline after a Leave or a lost host)
bool g_own_editor_end_fix = true;
COOP_FIX("core.own_editor_end", "BUG-0234", g_own_editor_end_fix, "core: a guest's pending own editor ends with the session");
unsigned g_vote_actions_run; // tests: action votes whose action ran
unsigned g_votes_dropped;    // tests: votes ended without running their result
bool g_vote_bypass = false;
bool g_vote_opened_ui = false; // the vote opened the co-op window: it closes again afterwards
double g_vote_last_send = 0;
using Submit_t = bool(SK_CDECL*)(void*);
Submit_t o_Submit;
void broadcast_vote();
void set_ready(bool on);

std::function<void()> g_vote_action; // a vote for something else than an editor (a superweapon ...)
std::string g_vote_done = "evolution starts";

// a vote whose result runs `action` (outside the frame) when everybody is ready or time is up
bool start_action_vote(const std::string& text, const std::string& done, std::function<void()> action)
{
    if (g_vote.active)
        return false;
    g_vote = Vote{true, nullptr, H->time_seconds(), text, 60, 0u, game::mode_id(), modes::current_world()};
    g_vote_action = std::move(action);
    g_vote_done = done;
    trace::line("vote", "start (action) '%s' players=%d", text.c_str(), (int)g_sess.players().size());
    for (auto& p : g_sess.players())
        g_ready[p.id] = p.id == 0;
    g_lobby_dirty = true;
    broadcast_vote();
    notice(text + ": press I'm ready (F9)", -1);
    g_vote_opened_ui = !coop_ui::is_open();
    coop_ui::open(true);
    return true;
}

void start_vote(void* req, const std::string& text)
{
    g_vote_action = nullptr;
    g_vote_done = design_stage(game::mode_id()) ? "the editor opens" : "evolution starts";
    if (req)
        skg::vcall<int>(req, 1); // AddRef: kept until the vote ends
    g_vote = Vote{true, req, H->time_seconds(), text, 60, req ? skg::field<uint32_t>(req, 0xC) : 0u,
                  game::mode_id(), modes::current_world()};
    trace::line("vote", "start '%s' editor=%s %s players=%d", text.c_str(), trace::hex(g_vote.editor).c_str(),
                modes::mode_name(g_vote.mode), (int)g_sess.players().size());
    for (auto& p : g_sess.players())
        g_ready[p.id] = p.id == 0;
    g_lobby_dirty = true;
    broadcast_vote();
    notice(text + ": press I'm ready (F9)", -1);
    g_vote_opened_ui = !coop_ui::is_open();
    coop_ui::open(true);
}

// Selecting a creature in the creature stage: CreatureSelectionUI reads (+0xE84)->+0x32C only for
// an adult with +0xB58 mask 0x1 set and mask 0x2 clear (GOG 2024 0xB6C9C3..0xB6CA11: `test al,2`
// skips the read, `test al,1` gates it; then vt 0xD0 / 0xD4 false). Our puppets and proxies have no
// +0xE84 object (the game crashed there), so mask 0x1 is hidden for the call. Mask 0x1 is not the
// pack (that is 0x100); docs/re/creature.md#creature-selection-gate (BUG-0118).
typedef void(SK_CDECL* SelectUI_t)(void*, void*, void*, uint32_t, float);
SelectUI_t o_SelectUI;
unsigned g_select_guards; // selections that needed the guard (tests)
void SK_CDECL h_SelectUI(void* sel, void* a2, void* a3, uint32_t a4, float a5)
{
    uint32_t* flags = nullptr;
    void* obj = sel && !IsBadReadPtr(sel, 8) ? *(void**)((uint8_t*)sel + 4) : nullptr;
    void* cr = obj ? skg::vcall<void*>(obj, 0xC / 4, 0xD0036E08u) : nullptr; // cCreatureAnimal
    if (cr && !IsBadReadPtr((uint8_t*)cr + 0xE84, 4) && *(void**)((uint8_t*)cr + 0xE84) == nullptr) {
        flags = (uint32_t*)((uint8_t*)cr + 0xB58);
        if (!(*flags & 1))
            flags = nullptr;
    }
    size_t mark = game::swap_depth();
    if (flags) {
        game::swap_open(game::swap_undo_or32, flags, 1u, "selection flag"); // BUG-0110
        *flags &= ~1u;
        g_select_guards++;
    }
    o_SelectUI(sel, a2, a3, a4, a5);
    if (flags) {
        *flags |= 1u;
        game::swap_close(mark);
    }
}

// The creature stage's avatar-event handler (a vtable method, 0xD47FC0) reads (+0xE84)->+0x32C of
// the creature an event is about (msg+4: the attacked creature of 0xD335362C, the impressed one of
// 0x60B4123) when it is an adult of another species, without a null check (0xD48634). Our puppets
// and NPC proxies have no +0xE84 object: the guest faulted there while it fought proxies
// (play_creature.py, 2026-10-23). The host of a player's report faulted there 3 times on a
// creature that was none of ours (a puppet brought back by a save, npc.cpp remove_saved_strays), so the
// creature of the message itself is checked too, whatever it is. During the handler these get a
// blank object whose +0x32C is -1 ("none"); a creature with tuning is never touched.
typedef bool(SK_THISCALL* StageMsg_t)(void*, uint32_t, void*);
StageMsg_t o_StageMsg;
alignas(16) uint8_t g_blank_e84[0x1000];
unsigned g_stage_msg_guards; // handler calls that needed the blank object (tests)
unsigned g_stage_msg_other;  // ... for a creature of the message that was none of ours (tests)
bool SK_THISCALL h_StageMsg(void* self, uint32_t id, void* msg)
{
    void* patched[257];
    int n = 0;
    if (g_mode == modes::kCreature)
        for (void* c : game::puppets()) {
            void** e84 = (void**)((uint8_t*)c + 0xE84);
            if (n < 256 && !IsBadReadPtr(e84, 4) && *e84 == nullptr && game::noun_id(c) == 0x18EB45Eu) {
                *e84 = g_blank_e84;
                patched[n++] = c;
            }
        }
    // the two messages whose msg+4 is the creature read at 0xD48634 (cast with noun 0x18EB45E, 0xAC8010)
    if ((id == 0xD335362Cu || id == 0x60B4123u) && msg && !IsBadReadPtr(msg, 8)) {
        void* c = ((void**)msg)[1];
        void** e84 = c ? (void**)((uint8_t*)c + 0xE84) : nullptr;
        if (c && !IsBadReadPtr(c, 0xE88) && !IsBadReadPtr(*(void**)c, 0x24) && game::noun_id(c) == 0x18EB45Eu &&
            *e84 == nullptr) {
            *e84 = g_blank_e84;
            patched[n++] = c;
            g_stage_msg_other++;
        }
    }
    if (n)
        g_stage_msg_guards++;
    size_t mark = game::swap_depth(); // BUG-0110: a fault in the handler leaves no blank +0xE84 behind
    for (int i = 0; i < n; i++)
        game::swap_open(game::swap_undo_ptr_if, (uint8_t*)patched[i] + 0xE84, (uintptr_t)g_blank_e84, "blank +0xE84");
    size_t own = game::swap_depth() - mark;
    bool r = o_StageMsg(self, id, msg);
    game::swap_close(mark, own);
    for (int i = 0; i < n; i++) { // the handler may have destroyed nothing of ours: restore only our pointer
        void** e84 = (void**)((uint8_t*)patched[i] + 0xE84);
        if (!IsBadReadPtr(e84, 4) && *e84 == g_blank_e84)
            *e84 = nullptr;
    }
    return r;
}

// The own creature usually comes from the Creature Creator (My Creations) and may hold parts that
// are not in the stage editor's part list. The editor then marks that error (bit 5) as blocking
// when it loads the creature, and "done" only shows the error and stays in the editor (cancel
// works). In the own editor such parts are allowed: the bit is dropped from the blocking set
// (docs/re/creations.md 7).
bool g_own_editor = false; // the own editor was requested and the player has not left it yet
using BlockingErrors_t = uint32_t*(SK_THISCALL*)(void*, uint32_t*, void*, void*, int);
BlockingErrors_t o_BlockingErrors;
unsigned g_part_list_waivers; // tests: how often the part-list error was waived
uint32_t* SK_THISCALL h_BlockingErrors(void* ed, uint32_t* out, void* res, void* model, int flag)
{
    uint32_t* r = o_BlockingErrors(ed, out, res, model, flag);
    if (g_own_editor && out && (out[0] & 0x20u)) {
        out[0] &= ~0x20u;
        g_part_list_waivers++;
    }
    return r;
}

// the creature editor for the own creature (EditorRequest + Submit, like ModAPI mods do)
bool open_own_editor(uint32_t editor_id)
{
    uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit");
    void* av = game::avatar();
    if (!ctor || !submit || !av)
        return false;
    // the game's operator new (GeneralAllocator::new_: size, name, 0, 0, 0, 0), as at 0xD42F1E (GOG 2016)
    void* req = skg::cdecl_call<void*>(H->sym("GeneralAllocator::new_"), (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
    skg::thiscall<void*>(ctor, req);
    skg::field<uint32_t>(req, 0xC) = editor_id ? editor_id : 0x465C50BAu; // CreatureEditorExtraLarge
    game::Key k = game::species_key(av);
    skg::field<uint32_t>(req, 0x10) = k.instance;
    skg::field<uint32_t>(req, 0x14) = k.type;
    skg::field<uint32_t>(req, 0x18) = k.group;
    g_vote_bypass = true;
    g_own_editor = true; // the editor loads the creature after Submit, on a later frame
    bool r = skg::cdecl_call<bool>(submit, req);
    g_vote_bypass = false;
    if (!r)
        g_own_editor = false;
    return r;
}

// the simulator's mode switcher has a switch pending (+0x28: a load, the quit to the menu, an editor)
bool mode_switch_pending()
{
    static uintptr_t mgr_ptr = H->sym("SporeCoop::SimulatorModeMgr_ptr");
    void* mgr = mgr_ptr ? *(void**)mgr_ptr : nullptr;
    return mgr && skg::field<uint8_t>(mgr, 0x28);
}

// An editor request that waits until the host's save for a world share is written (a save made
// while the editor opens does not load again: world::save_game). One reference held.
void* g_submit_later = nullptr;
double g_submit_held_at = 0;
double g_editor_submitted_at = -100; // the last editor request that reached the game
bool g_submit_queued = false;        // released, the outside-frame task has not submitted it yet
unsigned g_submits_held; // tests: editor requests that waited for a save
unsigned g_submits_repeated; // tests: editor requests dropped as repeats of one that is opening

// a save for a world share is under way or due (a share that waits for its save: the editor lets
// it save first; a share that cannot save now holds the editor at most 15 s, submit_tick)
bool share_save_due() { return world::save_busy() || world::save_pending(); }

bool submit_now(void* req)
{
    g_vote_bypass = true;
    bool r = o_Submit(req);
    g_vote_bypass = false;
    if (r)
        g_editor_submitted_at = H->time_seconds();
    return r;
}

void hold_submit(void* req)
{
    skg::vcall<int>(req, 1); // AddRef
    if (g_submit_later)
        skg::vcall<int>(g_submit_later, 2); // a newer request replaces it
    g_submit_later = req;
    g_submit_held_at = H->time_seconds();
    if (share_save_due()) {
        g_submits_held++;
        LOG("the editor waits until the world save is written");
    }
}

// the held request goes to the game once the save is written (at most 15 s later), outside the
// frame: editors create D3D resources
void submit_tick(double now)
{
    if (!g_submit_later || (share_save_due() && now - g_submit_held_at < 15.0))
        return;
    void* req = g_submit_later;
    g_submit_later = nullptr;
    if (!stage_mode(game::mode_id()) || mode_switch_pending()) { // the host left the stage meanwhile
        LOG("the held editor request is dropped: the game left the stage");
        skg::vcall<int>(req, 2);
        return;
    }
    if (now - g_submit_held_at > 0.05)
        LOG("the save is written: the editor opens (%.1f s later)", now - g_submit_held_at);
    g_submit_queued = true; // no save from here until the game has the request (save_blocker)
    H->run_outside_frame(
        [](void* r) {
            submit_now(r);
            g_submit_queued = false;
            skg::vcall<int>(r, 2); // Release
        },
        req);
}

// why the game must not save now (world::save_game)
const char* save_blocker()
{
    uint32_t mode = game::mode_id();
    if (mode == modes::kEditor || mode == modes::kEdit)
        return "an editor is open (the world is sent when the host is back in the stage)";
    if (mode == modes::kLoad)
        return "the game is loading";
    // a save in the galaxy menu (a share that waited there after the host left its stage) wrote a
    // slot that faulted the host's own next load (0xBA7F87, cStarManager) and stayed in LoadGame
    if (!stage_mode(mode) && mode != modes::kScenario)
        return "the game is not in a stage";
    if (g_submit_queued || H->time_seconds() - g_editor_submitted_at < 5.0)
        return "an editor is opening";
    if (mode_switch_pending())
        return "the game is switching modes";
    return nullptr;
}

// The quit message 0x4715068 (save and leave the stage for the galaxy menu; coop.load and received
// worlds use it, docs/re/space.md 8.2) saves the game itself. Handled while our save for a world
// share was still queued or being written (a guest joined as the host left), the two saves broke
// the host's slot: its next load faulted at 0xBA7F87 (data 0x1188, from 0xB27DA6) and stayed in
// LoadGame. Like the game does while a mode switch is pending, the message is posted again until
// our save is written.
using SimMsg_t = int(SK_THISCALL*)(void*, uint32_t, void*);
SimMsg_t o_SimMsg;
unsigned g_quits_delayed; // tests: quit messages that waited for our save
double g_quit_delay_logged = 0;
int SK_THISCALL h_SimMsg(void* self, uint32_t id, void* data)
{
    if (id == 0x4715068u && world::save_busy()) {
        uintptr_t get = H->sym("App::IMessageManager::Get");
        if (void* mm = get ? skg::cdecl_call<void*>(get) : nullptr) {
            double now = H->time_seconds();
            if (now - g_quit_delay_logged > 5.0) {
                g_quit_delay_logged = now;
                g_quits_delayed++;
                LOG("leaving the stage waits until the world save is written");
            }
            skg::vcall<void>(mm, 0x18 / 4, 0x4715068u, (void*)nullptr, (void*)nullptr, 0); // MessagePost
            return 1;
        }
    }
    if (id == 0x4715068u) // BUG-0264: the game leaves the stage now (any source of the quit)
        game::stage_leaving("quit message handled");
    return o_SimMsg(self, id, data);
}

bool SK_CDECL h_Submit(void* req)
{
    uint32_t mode = game::mode_id();
    // The cell stage asks for its editor again while the mate is still next to the avatar
    // (0xE78D10 -> mating -> event 0x1E -> 0xE59800 -> Submit): one more request came 13 ms after
    // the vote had ended, before the held one reached the game, and started a second vote that
    // ran on while the host was in the editor (its result was dropped). One editor per vote. A
    // guest's own request reaches the game at once; its repeat only told everybody twice.
    if (!g_vote_bypass && req &&
        (g_submit_later || g_submit_queued || H->time_seconds() - g_editor_submitted_at < 5.0)) {
        g_submits_repeated++;
        return false;
    }
    if (!g_vote_bypass && req && stage_mode(mode) && g_sess.connected() && g_sess.players().size() > 1) {
        if (g_sess.role() == net::Role::Host) {
            if (!g_vote.active)
                start_vote(req, g_name + (design_stage(mode) ? " wants to edit a design (" : " wants to evolve (") +
                                    modes::mode_name(mode) + ")");
            return false; // runs when the vote is over
        }
        notice(g_name + (design_stage(mode) ? " edits a design" : " evolves its own creature"), -1);
        set_ready(true);
    }
    if (!g_vote_bypass && req && g_sess.role() == net::Role::Host && share_save_due()) {
        hold_submit(req); // the game's own request (alone in the session): after the save
        return false;
    }
    bool r = o_Submit(req);
    if (r)
        g_editor_submitted_at = H->time_seconds();
    return r;
}

void finish_vote(const char* why)
{
    if (!g_vote.active)
        return;
    void* req = g_vote.request;
    g_vote.active = false;
    {
        int ready = 0, n = 0;
        for (auto& p : g_sess.players()) {
            n++;
            ready += g_ready.count(p.id) && g_ready[p.id];
        }
        trace::line("vote", "end: %s, ready %d/%d after %.1f s, editor=%s", why, ready, n, H->time_seconds() - g_vote.t0,
                    trace::hex(g_vote.editor).c_str());
    }
    broadcast_vote();
    if (g_vote_opened_ui)
        coop_ui::open(false);
    g_vote_opened_ui = false;
    notice(g_vote_done + " (" + why + ")", -1);
    if (g_vote_action) {
        static std::function<void()> run;
        run = std::move(g_vote_action);
        g_vote_action = nullptr;
        H->run_outside_frame([](void*) { g_vote_actions_run++; run(); }, nullptr);
    }
    if (req) { // the editor opens from submit_tick (outside the frame) once no world save is written
        hold_submit(req);
        skg::vcall<int>(req, 2); // the vote's reference: hold_submit took its own
        submit_tick(H->time_seconds());
    }
}

void broadcast_vote()
{
    if (g_sess.role() != net::Role::Host)
        return;
    net::Writer w;
    w.u8(g_vote.active ? 1 : 0);
    w.u8((uint8_t)std::max(0, g_vote.left));
    w.str8(g_vote.text);
    w.u32(g_vote.editor);
    g_sess.broadcast_reliable(net::MSG_VOTE, w);
}

// the vote ends without its result: the editor request is released, the action is not run
void drop_vote(const char* why)
{
    if (!g_vote.active)
        return;
    LOG("the vote '%s' is dropped: %s", g_vote.text.c_str(), why);
    g_votes_dropped++;
    void* req = g_vote.request;
    g_vote = Vote{};
    g_vote_action = nullptr;
    // on purpose with editor = 0 (g_vote was reset): the guests open no own editor for a cancelled
    // vote (BUG-0234); finish_vote broadcasts with the id. Do not keep the id here.
    broadcast_vote();
    if (g_vote_opened_ui)
        coop_ui::open(false);
    g_vote_opened_ui = false;
    if (req)
        skg::vcall<int>(req, 2); // the vote's reference
}

void vote_tick(double now)
{
    if (!g_vote.active || g_sess.role() != net::Role::Host)
        return;
    if (g_vote_scope_fix) {
        uint32_t w = modes::current_world();
        if (game::mode_id() != g_vote.mode || (g_vote.world && w && w != g_vote.world)) {
            notice(g_vote.text + ": cancelled (the stage was left)", -1);
            drop_vote("the stage was left");
            return;
        }
    }
    g_vote.left = (int)(60 - (now - g_vote.t0));
    bool all = true;
    for (auto& p : g_sess.players())
        all &= g_ready.count(p.id) && g_ready[p.id];
    if (all)
        finish_vote("everybody is ready");
    else if (g_vote.left <= 0)
        finish_vote("time is up");
    else if (g_sess.players().size() <= 1)
        finish_vote("alone");
    else if (now - g_vote_last_send > 1.0) {
        g_vote_last_send = now;
        broadcast_vote();
    }
}

// BUG-0019: the world sent after an evolution was saved while the host's game played the
// cinematic of the evolution (CRG_SpeciesMigrate, a caption waiting for OK): the creature in it is
// still the hatchling (age 0) and the migration not started, and a guest's game that loads it never
// grows up nor migrates. Such a share waits until the cinematic had 3 s to start and none plays (at
// most 120 s). `coop.npc sharecine off`: at once, as before (tests).
bool g_share_cine_fix = true;
COOP_FIX("npc.sharecine", "BUG-0019", g_share_cine_fix, "world: a share waits for the evolution cinematic to end");
double g_share_cine_since = 0, g_share_due_at = 0;
// BUG-0232: the cinematic wait counts per guest (Remote::cine_wait_since), restarted whenever the
// guest is marked as needing the world, and the timers end with the session. Off: one global timer
// reset only when a world goes out (0.7.3.0).
bool g_share_cine_each_fix = true;
COOP_FIX("npc.sharecine_each", "BUG-0232", g_share_cine_each_fix, "world: the wait for the evolution cinematic counts per guest");

void on_mode_change(uint32_t from, uint32_t to)
{
    if (to == modes::kMenu && stage_mode(from) && g_sess.role() == net::Role::Host && !g_remotes.empty())
        feed("the others play on alone until you load a game again (they get its world)");
    bool editor = to == modes::kEdit || to == modes::kEditor;
    if (editor && stage_mode(from))
        g_was_in_editor = true;
    if (!editor && (from == modes::kEdit || from == modes::kEditor))
        g_own_editor = false;
    if (!stage_mode(to))
        return;
    bool new_stage = g_last_stage && to != g_last_stage;
    bool evolved = g_was_in_editor && !new_stage;
    g_was_in_editor = false;
    uint32_t last = g_last_stage;
    g_last_stage = to;
    if (g_sess.role() != net::Role::Host || !g_share_world || g_remotes.empty() || !last)
        return;
    if (!new_stage && !evolved)
        return;
    for (auto& [id, r] : g_remotes)
        if (!r.echo) {
            r.needs_world = true; // sent by follow_phase as soon as the host controls something
            r.cine_wait_since = 0; // a new wait (BUG-0232)
        }
    g_share_due_at = H->time_seconds() + (evolved ? 3.0 : 0.0); // BUG-0019: its cinematic starts first
    notice(new_stage            ? g_name + " entered the " + modes::mode_name(to) + " stage: everybody follows"
           : design_stage(to) ? g_name + " changed a design: everybody gets the new designs"
                              : g_name + " evolved the species: everybody gets the new body",
           -1);
}

// The host decides when a phase ends: when the host plays a stage and a guest stays in another
// stage (or its editor) for a few seconds, the host world is sent again and the guest loads it.
void follow_phase(double now)
{
    if (g_sess.role() != net::Role::Host || !g_share_world || !g_adapter || g_adapter->own_worlds ||
        !stage_mode(g_mode))
        return;
    uint64_t key = ((uint64_t)g_mode << 32) | modes::current_world();
    for (auto& [id, r] : g_remotes) {
        if (r.echo)
            continue;
        if (r.needs_world && g_local) { // joined while the host was in a menu
            if (g_share_cine_fix) { // BUG-0019: after the evolution cinematic, at most 120 s
                double& since = g_share_cine_each_fix ? r.cine_wait_since : g_share_cine_since;
                bool cine = world::cinematic_playing();
                bool waited = since != 0 && cine;
                if (share_wait::hold(since, cine, now, g_share_due_at))
                    continue;
                if (waited)
                    LOG("share: a cinematic plays for 120 s, the world is sent anyway");
            } else
                g_share_cine_since = 0;
            r.cine_wait_since = 0;
            r.needs_world = false;
            r.world_sent = key;
            r.joined_phase = false;
            std::string err;
            if (world::share_with(id, err, g_save_on_share))
                feed("sending the world to " + r.name);
            else
                feed("cannot share the world: " + err);
            continue;
        }
        if (r.buf.empty())
            continue;
        const auto& ls = r.buf.latest();
        // a guest in an editor is left alone (it may be evolving inside the same stage); a guest
        // that plays another stage than the host is pulled back into the host's stage
        bool behind = ls.mode != g_mode && stage_mode(ls.mode);
        // a guest that played this phase and went to the main menu (pause menu "Exit to Main
        // Menu") while staying in the session waited for the host's world forever (the lobby said
        // "Waiting for the host's world...", play_creature.py 2026-10-23): it gets the world again
        bool left_to_menu = ls.mode == modes::kMenu && r.joined_phase;
        if (left_to_menu)
            behind = true;
        // the host went to the main menu and loaded another planet of the same stage: the guest
        // stays on the old planet (same mode, another world) and nothing sent the new one
        bool other_world = !g_adapter->place && ls.mode == g_mode && ls.world && g_world && ls.world != g_world;
        if (other_world)
            behind = true;
        if (!behind) {
            r.other_mode_since = -1;
            if (ls.mode == g_mode)
                r.joined_phase = true;
            continue;
        }
        if (r.other_mode_since < 0)
            r.other_mode_since = now;
        // once per phase, or again when the guest had followed and then left the phase
        if (now - r.other_mode_since < 5.0 || (r.world_sent == key && !r.joined_phase))
            continue;
        r.world_sent = key;
        r.joined_phase = false;
        std::string err;
        if (world::share_with(id, err, g_save_on_share))
            feed((left_to_menu || other_world ? "back to the world: sending it to " : "next phase: sending the world to ") + r.name);
        else
            feed("cannot share the world: " + err);
    }
}

void send_lobby()
{
    net::Writer w;
    auto pl = g_sess.players();
    std::vector<net::LobbyEntry> t;
    for (auto& p : pl) {
        net::LobbyEntry e;
        e.id = p.id;
        e.ready = g_ready[p.id];
        e.rtt = p.id == 0 ? 0 : g_sess.rtt_of(p.id);
        e.role = g_roles[p.id];
        t.push_back(e);
    }
    net::write_lobby(w, t, g_role_fix);
    for (auto& p : pl)
        if (p.id != 0)
            g_sess.send_reliable_to(p.id, net::MSG_LOBBY, w);
}

void lobby_tick(double now)
{
    if (g_sess.role() == net::Role::Host) {
        if (g_lobby_dirty || now - g_last_lobby > 1.0) {
            g_lobby_dirty = false;
            g_last_lobby = now;
            send_lobby();
            const char* mode = modes::mode_name(g_mode);
            std::string world = modes::current_world_name();
            if (world.size() > 4 && world.substr(world.size() - 4) == ".spo")
                world.resize(world.size() - 4);
            g_sess.lobby_info = std::string(mode) + (world.empty() || !g_adapter ? "" : " - " + world);
        }
    }
    if (g_disc_on) {
        uint32_t t = net::now_ms();
        if (g_lan_fake_n && (int32_t)(t - g_lan_fake_until) < 0) // coop.lanfake (tests)
            for (int i = 0; i < g_lan_fake_n; i++) {
                net::DiscoverReply rep;
                rep.port = net::kDefaultPort;
                rep.name = "fake" + std::to_string(i + 1);
                rep.players = 1;
                rep.max_players = 4;
                rep.info = "test";
                g_disc.on_reply({0x000200C0u | (uint32_t)(i + 1) << 24, 0}, rep, t); // 192.0.2.<i+1> (TEST-NET-1)
            }
        g_disc.update(t);
    }
}

// Network keepalive without frames: a modal game dialog (e.g. "game saved") can stop the frames
// while the window still pumps messages; the session and the world transfer then keep running
// from the window procedure, so guests do not time out.
std::atomic<double> g_last_frame{0};
std::atomic<bool> g_keepalive_run{false}, g_keepalive_queued{false};
// BUG-0107: a joinable std::thread in a global aborted the game at exit (~thread() calls
// std::terminate when the process ends through ExitProcess without cSporeApp::Shutdown, so
// on_unload never joined it). The thread is held through a pointer whose object is only destroyed
// after a join; at process exit nothing is destroyed. `coop.corefix keepalive_ptr 0`: the old
// global object (tests: coop.exittest then aborts).
bool g_keepalive_ptr_fix = true;
COOP_FIX_UNSAFE("core.keepalive_ptr", "BUG-0107", g_keepalive_ptr_fix, "core: the keepalive thread cannot abort the game at exit");
std::thread* g_keepalive = nullptr;
std::thread g_keepalive_old; // the old way, only with the fix off

void keepalive_task(void*)
{
    g_keepalive_queued = false;
    if (H->time_seconds() - g_last_frame < 0.5 || g_sess.role() == net::Role::None)
        return;
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

// tests (BUG-0107): end the process the way the game's own fault handler does, through ExitProcess
// without cSporeApp::Shutdown. An abort on the way out (std::terminate from a static destructor)
// writes "abort" into mods\logs\coop_exittest.txt.
char g_exittest_path[MAX_PATH];
void exittest_abort(int)
{
    if (FILE* f = fopen(g_exittest_path, "a")) {
        fputs("abort\n", f);
        fclose(f);
    }
    _exit(3);
}

int c_exittest(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "old")) { // the old global std::thread, started now
        keepalive_stop();
        g_keepalive_ptr_fix = false;
        keepalive_start();
    }
    snprintf(g_exittest_path, sizeof g_exittest_path, "%s\\logs\\coop_exittest.txt", H->mods_dir());
    if (FILE* f = fopen(g_exittest_path, "w")) {
        fputs("exit 0\n", f);
        fclose(f);
    }
    signal(SIGABRT, exittest_abort);
    LOG("coop.exittest: ExitProcess(0) without Shutdown (keepalive %s)", g_keepalive ? "pointer" : "global object");
    ExitProcess(0);
}

void menu_tick(double now);

// ---- diagnostics (SporeKit 0.3.4+: crash / problem reports, docs/02-modkit.md 2.7) ----
// the co-op state for a report, built on the frame (a crash report must not read game objects):
// two buffers, the report reads the last complete one
static std::string g_diag_buf[2];
static std::atomic<int> g_diag_cur{0};
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
    for (auto& [id, r] : g_remotes)
        s += "remote " + std::to_string(id) + " '" + r.name + "' puppet " + (r.puppet ? "yes" : "no") + "\n";
    size_t first = g_chat.size() > 8 ? g_chat.size() - 8 : 0;
    for (size_t i = first; i < g_chat.size(); i++)
        s += "chat: " + g_chat[i] + "\n";
    int next = 1 - g_diag_cur.load();
    g_diag_buf[next] = std::move(s);
    g_diag_cur = next;
}

static const char* diag_context_fn(void*) { return g_diag_buf[g_diag_cur.load()].c_str(); }

// a fault inside SporeKit or coop.dll was caught (SK_EV_FAULT): tell the player once per run where
// the report is, a mod bug is behind it
static void on_fault(void*, int, void* arg)
{
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
    char path[MAX_PATH * 2] = {0};
    H->diag_report(reason, path, sizeof path);
    return path;
}

// The guest's own editor after the vote opens once its stage has settled. At the start of the
// Creature stage the world arrives a few seconds before the vote ends; an editor opened 4 s after
// that load came up without its interface (no parts, no buttons, cancel did nothing) and the guest
// was stuck in it (coop_editor_desync.py, 2026-10-24, also with 5 s of waiting). The planet showed
// 6-15 s after the avatar appeared (the screen stayed black before). It waits until the avatar has
// played for 15 s and no cinematic or caption is up, at most 90 s.
uint32_t g_own_editor_due = 0;
double g_own_editor_due_at = 0;
bool g_own_editor_queued = false;
unsigned g_own_editor_waits; // tests: frames the own editor waited for the stage to settle
double g_own_editor_hold_until = 0; // tests (coop.vote hold <s>): the own editor waits until then
void* g_settle_avatar = nullptr;
double g_settle_since = 0;
bool pause_ui_up();

void own_editor_tick(double now)
{
    void* av = g_mode == modes::kCreature ? game::avatar() : nullptr;
    if (av != g_settle_avatar) {
        g_settle_avatar = av;
        g_settle_since = now;
    }
    if (!g_own_editor_due || g_own_editor_queued)
        return;
    if (now - g_own_editor_due_at > 90.0 || (g_mode != modes::kCreature && stage_mode(g_mode))) {
        LOG("own editor not opened: the stage did not settle (or was left)");
        feed("evolution: your editor did not open; evolve at your nest later");
        g_own_editor_due = 0;
        return;
    }
    if (!av || !g_local || now - g_settle_since < 15.0 || now < g_own_editor_hold_until ||
        world::cinematic_playing() || mode_switch_pending() || world::load_queued()) { // a received world loads first (BUG-0157)
        g_own_editor_waits++;
        return;
    }
    g_own_editor_queued = true;
    H->run_outside_frame( // editors create D3D resources: outside the frame
        [](void*) {
            g_own_editor_queued = false;
            uint32_t id = g_own_editor_due;
            if (!id)
                return;
            // the player may have left the stage since (pause menu "Exit to Main Menu")
            if (game::mode_id() != modes::kCreature || mode_switch_pending() || world::load_queued())
                return; // own_editor_tick decides again
            // an editor opened under the pause menu or its "save your game?" boxes: a click on
            // "Don't Save" right after left the guest in a black editor for good
            if (pause_ui_up()) {
                LOG("own editor not opened: the pause menu is open");
                feed("evolution: your editor did not open (pause menu); evolve at your nest later");
                g_own_editor_due = 0;
                return;
            }
            g_own_editor_due = 0;
            bool ok = open_own_editor(id);
            LOG("own editor %s (%.1f s after the vote)", ok ? "opened" : "not opened",
                H->time_seconds() - g_own_editor_due_at);
        },
        nullptr);
}

// the local player's entity changed (a load, the editor, the stage left): the puppets and proxies
// belong to the old world
static_assert(modes::kTribe == stage_epoch::kTribe && modes::kCiv == stage_epoch::kCiv &&
                  modes::kEditor == stage_epoch::kEditor && modes::kEdit == stage_epoch::kEdit &&
                  modes::kSpace == stage_epoch::kSpace && modes::kScenario == stage_epoch::kScenario &&
                  modes::kCell == stage_epoch::kCell && modes::kCreature == stage_epoch::kCreature,
              "stage_epoch.h mode ids");
void avatar_change_check()
{
    const modes::Adapter* a = modes::for_mode(game::mode_id());
    void* local = a ? a->local() : nullptr;
    if (local == g_last_avatar)
        return;
    if (g_last_avatar) {
        forget_puppets();
        npc::on_world_change();
        if (!tribe::editor_world_fix()) // BUG-0217: else tribe::world_check() decides
            tribe::on_world_change();
        if (!civ::world_epoch_fix()) // BUG-0223: else civ::world_check() decides
            civ::on_world_change(); // BUG-0059
    }
    g_last_avatar = local;
}

// ---- support trace (trace.h, docs/REPORTING.md#reporting-reading-logs) ----
double g_trace_at = -100, g_trace_body_at = -100, g_trace_stage_at = -100, g_trace_ver_at = -100;
uint32_t g_trace_mode = 0, g_trace_world = 0, g_trace_own_body = 0;
double g_trace_since = 0; // when the mode / world of the last "stage:" line began
bool g_trace_dead = false;
std::string g_trace_body_line;
std::map<uint8_t, uint8_t> g_trace_rdead; // partner id -> PF_DEAD of its last state seen here
std::map<uint8_t, uint32_t> g_trace_rspecies; // partner id -> the species its Creature state reported

const char* role_str()
{
    return g_sess.role() == net::Role::Host ? "host" : g_sess.role() == net::Role::Client ? (g_sess.connected() ? "guest" : "joining")
                                                                                          : "offline";
}

// why a partner's puppet is shown or not (the same tests as drive_puppet)
const char* puppet_why(const Remote& r, double now)
{
    if (r.buf.empty())
        return "hidden: no state received";
    const auto& s = r.buf.latest();
    if (!g_adapter || !g_local)
        return "hidden: no own avatar here";
    if (!(s.flags & net::PF_IN_WORLD))
        return "hidden: partner not in a world";
    if (s.flags & net::PF_DEAD)
        return "hidden: partner dead";
    if (r.buf.age(now) >= 5.0)
        return "hidden: state older than 5 s";
    if (s.mode != g_mode)
        return "hidden: partner in another mode";
    if (s.world != g_world || !g_world)
        return "hidden: partner in another world";
    return r.puppet ? "shown" : "hidden: not spawned (yet)";
}

// the first line of a module's status()
std::string first_line(const std::string& s) { return s.substr(0, s.find('\n')); }

void trace_tick(double now)
{
    if (!trace::on())
        return;
    trace::tick(now);
    net::PlayerState st = local_state();
    bool have = g_local && local_now();
    // own avatar: death and its body (Creature: species; Cell: the cell model)
    bool dead = have && (st.flags & net::PF_DEAD);
    if (have && dead != g_trace_dead)
        trace::line("ko", "own avatar %s (hp %.0f/%.0f, %s)", dead ? "dead / knocked out" : "alive again", st.health,
                    st.max_health, modes::mode_name(g_mode));
    g_trace_dead = dead;
    uint32_t body = !have ? 0 : g_mode == modes::kCreature ? st.species : g_mode == modes::kCell ? cellsync::own_model() : 0;
    if (body && body != g_trace_own_body && g_sess.connected()) {
        trace::line("body", "own avatar %s %s -> %08X: %s", g_mode == modes::kCreature ? "species" : "cell model",
                    trace::hex(g_trace_own_body).c_str(), body,
                    creations::trace_line(g_mode == modes::kCreature ? creations::kCreature : creations::kCell).c_str());
        g_trace_body_at = now;
    }
    if (body)
        g_trace_own_body = body;
    for (auto& [id, r] : g_remotes) {
        if (r.echo || r.buf.empty())
            continue;
        uint8_t d = r.buf.latest().flags & net::PF_DEAD;
        auto it = g_trace_rdead.find(id);
        if (it != g_trace_rdead.end() && it->second != d)
            trace::line("ko", "player %u '%s' %s (state of its own game)", id, r.name.c_str(), d ? "dead / knocked out" : "alive again");
        g_trace_rdead[id] = d;
        // the body a partner's own game reports (Creature): a change after its editor or its own creation
        const auto& ls = r.buf.latest();
        if (ls.mode == modes::kCreature && ls.species && g_trace_rspecies[id] != ls.species) {
            void* pp = puppet_now(r);
            trace::line("body", "player %u '%s' reports species %s -> %08X; its creation here %s, its puppet here %s", id,
                        r.name.c_str(), trace::hex(g_trace_rspecies[id]).c_str(), ls.species,
                        trace::hex(creations::player_key(id, creations::kCreature).instance).c_str(),
                        pp && g_mode == modes::kCreature ? trace::hex(game::species_instance(pp)).c_str() : "-");
            g_trace_rspecies[id] = ls.species;
        }
    }
    bool changed = g_mode != g_trace_mode || g_world != g_trace_world;
    if (changed) {
        trace::line("stage", "%s %s -> %s %s '%s' (%.1f s in the last)", modes::mode_name(g_trace_mode),
                    trace::hex(g_trace_world).c_str(), modes::mode_name(g_mode), trace::hex(g_world).c_str(),
                    modes::current_world_name().c_str(), g_trace_since > 0 ? now - g_trace_since : 0.0);
        g_trace_mode = g_mode;
        g_trace_world = g_world;
        g_trace_since = now;
        g_trace_body_at = g_trace_stage_at = -100; // the stage lines with the next trace
    }
    if (!g_sess.connected() || (!changed && now - g_trace_at < 10.0))
        return;
    g_trace_at = now;
    char b[700];
    std::string res = shared::brief();
    snprintf(b, sizeof b,
             "%s id=%u players=%d %s world=%s '%s' avatar=%s pos=(%.1f %.1f %.1f) hp=%.0f/%.0f species=%s flags=0x%X "
             "dna=%.0f%s%s host_away=%d vote=%s input_lock=%d editor_pending=%s",
             role_str(), g_sess.my_id(), (int)g_sess.players().size(), modes::mode_name(g_mode), trace::hex(g_world).c_str(),
             modes::current_world_name().c_str(), have ? "yes" : "no", st.pos.x, st.pos.y, st.pos.z, st.health,
             st.max_health, g_mode == modes::kCell ? "-" : trace::hex(st.species).c_str(), (unsigned)st.flags,
             game::avatar() ? game::dna() : -1.f, res.empty() ? "" : " ", res.c_str(), (int)g_host_away,
             g_vote.active ? "on" : "off", coop_ui::input_lock_now(), trace::hex(g_own_editor_due).c_str());
    trace::line("trace", "%s%s%s", b, now - g_trace_ver_at > 60.0 ? " version=" : "",
                now - g_trace_ver_at > 60.0 ? g_sess.version.c_str() : "");
    float host_d = -1;
    for (auto& [id, r] : g_remotes) {
        if (r.echo)
            continue;
        net::Session::Link lk;
        bool link = g_sess.link_of(id, lk);
        int rtt = link ? lk.rtt_ms : g_lobby_rtt.count(id) ? g_lobby_rtt[id] : -1;
        if (r.buf.empty()) {
            trace::line("trace", "  partner %u '%s': no state yet, rtt=%d", id, r.name.c_str(), rtt);
            continue;
        }
        const auto& s = r.buf.latest();
        float d = net::dist(s.pos, st.pos);
        bool here = same_place(s);
        if (id == 0 && here)
            host_d = d;
        snprintf(b, sizeof b,
                 "  partner %u '%s' %s world=%s d=%s same_place=%d puppet=%s hp=%.0f/%.0f species=%s flags=0x%X "
                 "state_age=%.1f s rtt=%d ms",
                 id, r.name.c_str(), modes::mode_name(s.mode), trace::hex(s.world).c_str(),
                 here ? std::to_string((int)d).c_str() : "-", (int)here, puppet_why(r, now), s.health, s.max_health,
                 s.mode == modes::kCell ? "-" : trace::hex(s.species).c_str(), (unsigned)s.flags, r.buf.age(now), rtt);
        std::string l = b;
        if (now - g_trace_ver_at > 60.0) // the versions once a minute (they do not change in a session)
            for (auto& pl : g_sess.players())
                if (pl.id == id)
                    l += " version=" + (pl.version.empty() ? std::string("unknown (pre-0.7.4)") : pl.version);
        if (link) {
            snprintf(b, sizeof b, " resends=%u/%u silent=%u ms pending=%u", lk.resends, lk.sent, lk.silent_ms, lk.pending);
            l += b;
        }
        trace::line("trace", "%s", l.c_str());
    }
    if (now - g_trace_ver_at > 60.0)
        g_trace_ver_at = now;
    if (!have)
        return;
    if (g_mode == modes::kCell) {
        std::string c = cellsync::trace_line();
        if (g_sess.role() == net::Role::Client) {
            // the host's game populates only about one screen around its own cell (BUG-0174)
            snprintf(b, sizeof b, " | host d=%s%s", host_d < 0 ? "-" : std::to_string((int)host_d).c_str(),
                     host_d < 0 ? " (not here)" : host_d > 40 ? " (outside the host's populated area: little or no food)" : "");
            c += b;
        }
        trace::line("cell", "%s", c.c_str());
    } else if (g_mode == modes::kCreature) {
        std::string c = npc::trace_line(st.pos, 60.f);
        net::Vec3 nest;
        if (game::nest_position(g_local, nest)) {
            snprintf(b, sizeof b, ", nest d=%.0f m", net::dist(nest, st.pos));
            c += b;
        } else
            c += ", no nest";
        trace::line("creature", "%s", c.c_str());
    }
    bool body_stage = g_mode == modes::kCreature || g_mode == modes::kCell;
    if (body_stage) {
        std::string bl = creations::trace_line(g_mode == modes::kCreature ? creations::kCreature : creations::kCell);
        if (bl != g_trace_body_line || now - g_trace_body_at > 60.0) {
            trace::line("body", "%s", bl.c_str());
            g_trace_body_line = bl;
            g_trace_body_at = now;
        }
    }
    if (now - g_trace_stage_at > 30.0) {
        g_trace_stage_at = now;
        if (g_mode == modes::kTribe)
            trace::line("tribe", "%s", first_line(tribe::status()).c_str());
        else if (g_mode == modes::kCiv)
            trace::line("civ", "%s", first_line(civ::status()).c_str());
        else if (g_mode == modes::kSpace) {
            trace::line("space", "%s", first_line(squadron::status()).c_str());
            trace::line("space", "%s", first_line(spacecoop::status()).c_str());
        }
    }
}

void on_frame(void*, int, void*)
{
    modes::CellScope cell_scope; // BUG-0271: no game code runs in here, the pool check holds for it
    double now = H->time_seconds();
    g_last_frame = now;
    diag_tick(now);
    notify::tick(now);
    game::world_epoch(); // BUG-0119: sees every frame outside the creature stage
    game::stage_epoch_tick(); // BUG-0217: before any message or module tick of this frame
    modes::world_name_tick(); // BUG-0258: the planet stays known through snapshot.spo
    modes::test_tick();       // BUG-0223 tests: a forced frame without the local entity
    tribe::world_check();     // BUG-0217: the Tribe world changed (not an editor visit)
    civ::world_check();       // BUG-0223: the Civ world changed (not a frame without the city anchor)
    creations::tick(now);
    if (g_frame_fix) // BUG-0091: forget the puppets of a world the game tore down before any message uses them
        avatar_change_check();
    g_sess.update();
    lobby_tick(now);
    vote_tick(now);
    submit_tick(now);
    own_editor_tick(now);
    coop_ui::tick();
    menu_tick(now);
    if (g_sess.role() == net::Role::Host)
        world::tick(g_sess);
    else
        world::guest_tick(); // a received world waiting for the player to leave its editor
    savestrip::tick();
    npc::tick(now);
    social::tick(now);
    shared::tick(g_sess, game::mode_id(), now);
    if (g_sess.role() == net::Role::Host && game::avatar()) {
        float total = game::dna();
        // also every 2 s: a guest drops it while it has no avatar (loading, editor), BUG-0128
        if (total != g_dna_sent || (cellsync::totals_resend_enabled() && now - g_dna_at > 2.0)) {
            g_dna_sent = total;
            g_dna_at = now;
            net::Writer w;
            w.f32(total);
            for (auto& p : g_sess.players())
                if (p.id != 0)
                    g_sess.send_reliable_to(p.id, net::MSG_DNA, w);
        }
    }
    if (g_sess.role() == net::Role::Host && now - g_last_time_sync > 1.0) {
        g_last_time_sync = now;
        net::Quat q;
        if (game::time_of_day(q)) {
            net::Writer w;
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            for (auto& p : g_sess.players())
                if (p.id != 0)
                    g_sess.send_unreliable_to(p.id, net::MSG_WORLD_TIME, w);
        }
    }

    uint32_t prev_mode = g_mode;
    g_mode = game::mode_id();
    if (g_mode != prev_mode)
        on_mode_change(prev_mode, g_mode);
    g_adapter = modes::for_mode(g_mode);
    g_world = modes::current_place(g_adapter);
    g_local = g_adapter ? g_adapter->local() : nullptr;
    game::leave_probe_tick(g_local); // BUG-0264 tests: an avatar used while the stage is torn down
    modes::cell_probe_tick();        // BUG-0271: the cell pool freed while the mode reads Cell
    void* av = game::avatar();
    avatar_change_check();

    remove_strays(now);
    follow_phase(now);
    host_away_tick(now);
    tribe::tick(now);
    cellsync::tick(now);
    civ::tick(now);
    squadron::tick(now);
    autoupdate::tick();
    spacecoop::tick(now);
    if (g_sess.connected() && g_mode == modes::kCreature)
        teamplay::tick(game::avatar(), now);

    if (now - g_last_send >= 0.05) {
        g_last_send = now;
        net::PlayerState st = local_state();
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

    (void)av;
    for (auto& [id, r] : g_remotes) {
        if (!g_local) {
            if (r.puppet)
                forget_puppets();
            continue;
        }
        drive_puppet(r, now);
    }
    trace_tick(now);
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
            if (tribe::is_chief(c))
                n = "\u2605 " + n + "'s chief";
            H->overlay_text((int)sc[0] - text_width(n) / 2, (int)sc[1] - 10,
                            owner == g_sess.my_id() ? 0xFFFFD040 : 0xFF80C8FF, n.c_str());
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
            H->overlay_text((int)sc[0] - text_width(n) / 2, (int)sc[1] - 10,
                            owner == g_sess.my_id() ? 0xFFFFD040 : 0xFF80C8FF, n.c_str());
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
bool g_text_fix = true; // coop.social textfix off: the old key poll (tests)
COOP_FIX("social.text", "BUG-0149", g_text_fix, "creature: ability keys are not taken while the cheat console is open");
std::atomic<bool> g_menu_check_queued{false};
double g_menu_checked = 0;
uint32_t g_menu_hidden = 0; // frames whose labels were hidden (coop.status)

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
    if (!up)
        if (sui::Win root = sui::main_window())
            up = shown_chain(sui::find(root, kPauseMenuId));
    g_menu_up = up;
    sui::Win root = sui::main_window();
    g_text_up = g_text_fix && root && shown_chain(sui::find(root, kCheatConsoleId));
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
    if (labels) {
        draw_marks();
        cellsync::draw_overlay();
        spacecoop::draw_overlay();
        draw_owner_tags();
        draw_civ_tags();
    }
    // name tags above remote players
    void* av = labels ? g_local : nullptr;
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
        net::Vec3 p = modes::entity_pos(ad, r.puppet);
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
            H->overlay_text((int)sc[0] - text_width(tag) / 2, (int)sc[1] - 10, 0xFF80FF80, tag.c_str());
        }
    }
    std::string head;
    if (g_sess.role() == net::Role::Host)
        head = "CO-OP host :" + std::to_string(g_sess.port()) + "  " + std::to_string(g_sess.players().size()) +
               "/" + std::to_string(net::kMaxPlayers);
    else if (g_sess.role() == net::Role::Client)
        head = std::string("CO-OP guest  ") + (g_sess.connected() ? "connected" : "connecting...");
    else if (g_echo)
        head = "CO-OP echo test";
    // BUG-0155: the block is not drawn over the co-op window or a menu (the lobby shows the same),
    // and the notification panels start below it; `coop.corefix status_block 0` = the old way
    // (g_menu_up is only refreshed in a game: in the main menu the window's own flag counts)
    bool covered = coop_ui::is_open() || (!labels && g_local);
    if (head.empty() || (covered && g_status_block_fix)) {
        g_status_block = {0, 0, 0, 0};
        notify::set_top_min(0);
        return;
    }
    int x = sw > 0 ? sw - 480 : 900, y = 70, wmax = utf8::overlay_width(head);
    H->overlay_text(x, y, 0xFFFFD040, head.c_str());
    y += 22;
    if (g_sess.connected() && g_mode == modes::kCreature) {
        std::string t = teamplay::status();
        if (!t.empty()) {
            H->overlay_text(x, y, 0xFF8CF08C, t.c_str());
            y += 20;
        }
    }
    for (auto& p : g_sess.players()) {
        std::string line = (p.id == g_sess.my_id() ? "> " : "  ") + p.name;
        auto it = g_remotes.find(p.id);
        if (p.id != g_sess.my_id() && it != g_remotes.end() && !it->second.buf.empty()) {
            const auto& ls = it->second.buf.latest();
            line += std::string("  [") + modes::mode_name(ls.mode) + (same_place(ls) || ls.mode != g_mode ? "" : ", other world") + "]";
        }
        H->overlay_text(x, y, 0xFFFFFFFF, line.c_str());
        wmax = std::max(wmax, utf8::overlay_width(line));
        y += 20;
    }
    g_status_block = {x, 70, x + wmax, y};
    notify::set_top_min(g_status_block_fix ? (float)y + 8 : 0.f);
    // notices are notification panels now (notify.cpp); the history is in the co-op window
}

void on_anim(void* creature, uint32_t anim, int block, bool disable_blend, bool to_target)
{
    void* av = game::avatar();
    if (!av || creature != av)
        return;
    if (g_anim_log)
        LOG("avatar anim 0x%08X block=%d%s%s", anim, block, disable_blend ? " noblend" : "", to_target ? " (to)" : "");
    net::AnimEvent ev;
    ev.id = g_sess.my_id();
    ev.anim = anim;
    ev.block = block;
    ev.flags = (disable_blend ? 1 : 0) | (to_target ? 2 : 0);
    if (g_sess.connected()) {
        net::Writer w;
        ev.write(w);
        g_sess.send_event(net::MSG_ANIM, w);
    }
    if (g_echo) { // loopback test: replay on the echo puppet
        auto it = g_remotes.find(200);
        if (it != g_remotes.end() && it->second.puppet)
            game::play_animation(it->second.puppet, anim, block, disable_blend);
    }
}

uint32_t g_anims_rx = 0, g_anims_played = 0;

void on_remote_anim(const net::AnimEvent& ev)
{
    g_anims_rx++;
    if (g_anim_log)
        LOG("remote %u anim 0x%08X", ev.id, ev.anim);
    auto it = g_remotes.find(ev.id);
    if (it == g_remotes.end() || !it->second.puppet)
        return;
    if (creature_puppet(it->second.puppet_adapter) && !game::creature_listed(it->second.puppet)) {
        if (g_frame_fix) { // freed by a teardown the mod has not seen yet (BUG-0091)
            g_anims_dropped++;
            return;
        }
        g_anims_unlisted++;
    }
    game::play_animation(it->second.puppet, ev.anim, ev.block, ev.flags & 1);
    g_anims_played++;
}

// Every way a session ends runs the same cleanup (BUG-0033): the host or the network ending it
// (cb.disconnected), the player's Leave (do_leave), a new host/join while a session is open
// (Session::host/join only close the socket) and the mod unload. Before, each path cleaned its own
// subset of the stage modules: after a Leave the Space mirror ships flew on with their own AI, the
// tribe and civ tables and the creations of the others stayed, and a new session inherited the old
// remotes. `coop.sessionfix 0` restores the old per-path lists (tests).
bool g_session_end_fix = true;
bool g_id_reuse_fix = true; // coop.corefix id_reuse 0: a joining player inherits an old entry of its id
COOP_FIX("session.end", "BUG-0033", g_session_end_fix, "core: every session end runs the same cleanup (coop.sessionfix)");
COOP_FIX("core.id_reuse", "BUG-0076", g_id_reuse_fix, "core: a joining player does not inherit an old entry of its id");
COOP_FIX("core.unique_names", "BUG-0141", g_sess.unique_names, "net: players with the same name get unique names");
COOP_FIX("core.rejoin_name", "BUG-0209", g_sess.rejoin_by_name, "net: a rejoin after a crash replaces the silent entry of its name");
COOP_FIX("core.text_utf8", "BUG-0164", utf8::g_cut_fix, "net: texts are cut between characters, not bytes");
COOP_FIX("core.notify_text", "BUG-0129", notify_text::g_fix, "ui: notification lines wrap by characters, the panel kind is not guessed");
// the Give recipient picked with ">" (civ); BUG-0234: cleared when its player leaves and at the
// session end (it pointed at whoever got that id next)
uint8_t g_give_pick = net::kNoGiveTarget;
bool g_give_pick_reset_fix = true;
COOP_FIX("give.pick_reset", "BUG-0234", g_give_pick_reset_fix, "civ: the Give pick ends with its player and the session");
// The session-end structure (BUG-0214, GitHub #148; docs/06-testing.md#testing-fix-registry):
//  block 1, the module calls: with session.end on every path calls every module; off, a path calls
//    only what it called before BUG-0033 (Disconnect always called them all);
//  block 2, the remotes: despawned and cleared, except on Restart with session.end off (the "new
//    session inherits the old remotes" part of BUG-0033);
//  block 3, always: every reset of coop.cpp state, on every path and with every session.end
//    setting, each gated only by its own switch. It may run inside g_sess.update() (Disconnect,
//    BUG-0029): mod state only, no game calls, no sends, no waiting; idempotent.
// A new coop.cpp reset goes into block 3; a module's reset into its own on_session_end().
enum class EndPath { Leave, Unload, Restart, Disconnect };

void end_session(EndPath path, bool cell)
{
    if (g_session_end_fix || path == EndPath::Disconnect) {
        npc::on_session_end();
        social::on_session_end();
        tribe::on_session_end();
        if (cell)
            cellsync::on_session_end();
        creations::on_session_end();
        civ::on_session_end();
        squadron::on_session_end();
        spacecoop::on_session_end();
    } else if (path == EndPath::Leave) { // before BUG-0033 (tests)
        npc::on_session_end();
        social::on_session_end();
        if (cell)
            cellsync::on_session_end();
        spacecoop::on_session_end();
    } else if (path == EndPath::Unload) {
        npc::on_session_end();
        social::on_session_end();
    }
    if (g_session_end_fix || path != EndPath::Restart) {
        for (auto& [id, r] : g_remotes)
            despawn(r);
        g_remotes.clear();
    }
    if (g_vote_scope_fix)
        drop_vote("the session ended");
    g_puppet_hold = false; // tests (coop.puppet hold, BUG-0251)
    world::reset_shares();
    shared::on_session_end();
    world::on_session_end(); // the guest's queued loads of the host's world (BUG-0074)
    g_dna_pending = -1;
    if (g_host_away_fix) { // BUG-0085
        g_host_away = false;
        g_host_away_since = -1;
        g_solo_dna = 0;
        g_solo_mode = 0;
    }
    g_dna_owed = 0;
    g_dna_owed_world = 0;
    g_state_mode_override = 0; // tests
    if (g_own_editor_end_fix)
        g_own_editor_due = 0; // BUG-0234 (a task already queued reads it first and returns)
    if (g_give_pick_reset_fix)
        g_give_pick = net::kNoGiveTarget; // BUG-0234: a new session starts with no pick
    if (g_share_cine_each_fix) { // BUG-0232: no wait of an old session shortens a later one
        g_share_cine_since = 0;
        g_share_due_at = 0;
    }
}

// BUG-0204: the host name lookup of a join runs on a thread that holds a reference to coop.dll,
// so a hot reload during a slow lookup does not unmap the code under it (as BUG-0060)
struct PinnedStart {
    void (*fn)(void*);
    void* arg;
    HMODULE self;
};

DWORD WINAPI pinned_main(void* p)
{
    PinnedStart s = *static_cast<PinnedStart*>(p);
    delete static_cast<PinnedStart*>(p);
    s.fn(s.arg);
    FreeLibraryAndExitThread(s.self, 0);
}

bool spawn_pinned(void (*fn)(void*), void* arg)
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(void*)&spawn_pinned, &self))
        return false;
    auto* s = new PinnedStart{fn, arg, self};
    HANDLE h = CreateThread(nullptr, 0, pinned_main, s, 0, nullptr);
    if (!h) {
        delete s;
        FreeLibrary(self);
        return false;
    }
    CloseHandle(h);
    return true;
}

// coop.slowdns (tests, BUG-0204): a name server that answers late
std::atomic<int> g_slow_dns_ms{0};
net::LookupFn g_real_lookup = nullptr;

bool slow_lookup(const std::string& host, uint32_t& ip)
{
    if (int ms = g_slow_dns_ms.load())
        Sleep((DWORD)ms);
    return g_real_lookup(host, ip);
}

void setup_callbacks()
{
    net::g_spawn_thread = spawn_pinned;
    g_sess.build = build_id();
    // the versions every peer logs on a join (CAP_VERSION): mod, SporeKit, game build and exe hash
    g_sess.version = std::string("coop " COOP_VERSION ", SporeKit ") + H->kit_version() + ", " + H->build_name() + " " +
                     std::string(H->exe_sha256()).substr(0, 8);
    g_sess.cb.log = [](const std::string& s) { LOG("net: %s", s.c_str()); };
    g_sess.cb.connected = [] {
        feed("connected to host");
        coop_ui::session_event(true, ""); // BUG-0104: "Connecting to ..." ends here
    };
    g_sess.cb.disconnected = [](const std::string& why) {
        rejoin::clear(); // the session ended (BUG-0235)
        feed("disconnected: " + why);
        coop_ui::session_event(false, why);
        chat_log("* disconnected: " + why);
        g_ready.clear();
        g_lobby_rtt.clear();
        end_session(EndPath::Disconnect, true);
    };
    g_sess.cb.player_join = [](const net::PlayerInfo& p) {
        shared::on_player_join(g_sess.role());
        creations::on_player_join(p.id);
        npc::on_player_leave(p.id); // a newcomer under a reused id knows no NPCs yet (BUG-0125)
        g_dna_sent = -1; // resend the DNA total to the newcomer
        auto old = g_remotes.find(p.id);
        if (g_id_reuse_fix && old != g_remotes.end() && !old->second.echo) {
            // BUG-0109 / BUG-0076: an entry of an earlier player under this id (a ghost made by a
            // late state): its puppet and its buffer are not the newcomer's (states of the new
            // player's clock older than the ghost's newest were all dropped as old)
            despawn(old->second);
            g_remotes.erase(old);
        }
        auto& r = g_remotes[p.id];
        r.id = p.id;
        r.name = p.name;
        trace::line("session", "player %u '%s' joined (we are %s, %s, world %s): %s", p.id, p.name.c_str(), role_str(),
                    modes::mode_name(g_mode), trace::hex(g_world).c_str(),
                    p.version.empty() ? "version unknown (pre-0.7.4)" : p.version.c_str());
        if (!p.version.empty() && p.version != g_sess.version) {
            // the same exe build is checked by the protocol; another mod or SporeKit version is allowed
            // but behaves differently: a support report needs to see it at once
            H->log(M, SK_WARN, "versions differ: player %u '%s' runs %s, this game %s", p.id, p.name.c_str(),
                   p.version.c_str(), g_sess.version.c_str());
            if (p.version.substr(0, p.version.find(',')) != g_sess.version.substr(0, g_sess.version.find(',')))
                feed_kind(p.name + " has another SporeCoop version (" +
                              (p.version.rfind("coop ", 0) == 0 ? p.version.substr(5, p.version.find(',') - 5) : p.version) +
                              "): update both games to the same one", "info");
        }
        feed_kind(p.name + " joined the pack", "join"); // a name like "Lost" is not danger (BUG-0129)
        chat_log("* " + p.name + " joined");
        g_lobby_dirty = true;
        bool own_worlds = g_adapter && g_adapter->own_worlds;
        bool playing = g_adapter && g_local && stage_mode(g_mode);
        if (g_sess.role() == net::Role::Host && g_share_world && !own_worlds && !playing) {
            // in menus or loading screens there is no world to send yet (the current save name
            // is a stale snapshot): send it when the host starts playing
            r.needs_world = true;
            r.cine_wait_since = 0;
            feed(p.name + " waits for your world: load or start a game");
        } else if (g_sess.role() == net::Role::Host && g_share_world && !own_worlds) {
            std::string err;
            bool save = g_save_on_share && g_adapter; // only while playing, not from menus
            r.world_sent = ((uint64_t)g_mode << 32) | modes::current_world();
            r.joined_phase = false;
            if (!world::share_with(p.id, err, save))
                feed("cannot share the world: " + err);
            else
                feed("sending the world to " + p.name);
        }
    };
    g_sess.cb.custom = [](uint8_t type, uint8_t from, net::Reader& r) {
        if (type == net::MSG_ANIM) {
            net::AnimEvent ev;
            if (ev.read(r))
                on_remote_anim(ev);
            return;
        }
        // (the DNA total also before the host's first state: it is applied once we play its world)
        // (the Civ state also before it: one cycle applied in that window bound the guest's own
        // vehicles for good, BUG-0231; a guest in the host's world gets the next cycle)
        bool civ_msg = type == net::MSG_CIV_STATE || type == net::MSG_CIV_BUILD || type == net::MSG_CIV_ALARM;
        if (from == 0 && world_state_msg(type) && host_elsewhere(type == net::MSG_DNA || (g_world_gate_fix && civ_msg))) {
            if (type == net::MSG_DNA) {
                float total = r.f32();
                if (r.ok())
                    g_dna_pending = total;
            }
            g_place_dropped++;
            return;
        }
        if (type == net::MSG_DNA && g_sess.role() == net::Role::Client) {
            float total = r.f32();
            if (r.ok() && game::avatar() && !g_host_away) {
                float d = total - game::dna();
                if (d != 0) {
                    trace::ev("dna", "total", "host total %.0f -> %+.0f here", total, d);
                    game::dna_add_raw(d); // through the game so the UI/progress updates
                }
                g_dna_rx++;
            }
            return;
        }
        if (type == net::MSG_DNA_ADD && g_sess.role() == net::Role::Host) {
            float d = r.f32();
            if (r.ok() && d > 0 && d < 10000 && game::avatar()) {
                game::dna_add_raw(d);
                feed(g_sess.name_of(from) + " earned " + std::to_string((int)d) + " DNA");
            } else if (r.ok() && d > 0 && d < 10000 && g_away_dna_fix && g_dna_owed < 100000) {
                trace::line("dna", "from %u +%.0f kept as owed (no own creature now)", from, d);
                if (g_dna_owed <= 0)
                    g_dna_owed_world = g_stage_world; // BUG-0233: paid only into this world
                g_dna_owed += d; // in an editor or a load: paid when our creature is back
            }
            return;
        }
        if (type == net::MSG_WORLD_TIME && g_sess.role() == net::Role::Client) {
            net::Quat q;
            q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
            if (r.ok() && game::avatar())
                game::set_time_of_day(q);
            return;
        }
        if (type == net::MSG_VOTE && g_sess.role() == net::Role::Client) {
            bool on = r.u8() != 0;
            int left = r.u8();
            std::string text = r.str8();
            if (!r.ok())
                return;
            bool has_editor = r.left() >= 4;
            uint32_t editor = has_editor ? r.u32() : 0;
            bool started = on && !g_vote.active, ended = !on && g_vote.active;
            if (on || (g_vote_drop_editor_fix && ended && has_editor))
                g_vote.editor = editor; // a cancelled vote ends with 0 (BUG-0234)
            // the evolution starts: everybody evolves its own creature in its own editor (own_editor_tick)
            if (ended && g_vote.editor && g_mode == modes::kCreature && g_local) {
                g_own_editor_due = g_vote.editor;
                g_own_editor_due_at = H->time_seconds();
            }
            g_vote.active = on;
            g_vote.left = left;
            g_vote.text = text;
            if (started || ended)
                trace::line("vote", "%s by the host: '%s' editor=%s%s", started ? "started" : "ended", text.c_str(),
                            trace::hex(editor).c_str(),
                            ended && g_own_editor_due ? ": the own editor opens when the stage has settled" : "");
            if (started) {
                g_ready[g_sess.my_id()] = false;
                g_vote_opened_ui = !coop_ui::is_open();
                coop_ui::open(true);
            }
            if (ended && g_vote_opened_ui) {
                coop_ui::open(false);
                g_vote_opened_ui = false;
            }
            return;
        }
        if (type == net::MSG_NOTICE) {
            std::string t = r.str8();
            if (!r.ok())
                return;
            if (g_sess.role() == net::Role::Host)
                notice(t, from); // relay to the other guests
            else {
                feed(t);
                chat_log("* " + t);
            }
            return;
        }
        if (type == net::MSG_ROLE && g_sess.role() == net::Role::Host) {
            uint8_t role = r.u8();
            if (r.ok() && role < 4) {
                g_roles[from] = role;
                g_lobby_dirty = true;
            }
            return;
        }
        if (type == net::MSG_READY && g_sess.role() == net::Role::Host) {
            uint8_t on = r.u8();
            if (r.ok()) {
                g_ready[from] = on != 0;
                g_lobby_dirty = true;
                trace::line("vote", "player %u ready=%d received (vote %s)", from, (int)on, g_vote.active ? "on" : "off");
            }
            return;
        }
        if (type == net::MSG_LOBBY && g_sess.role() == net::Role::Client) {
            std::vector<net::LobbyEntry> t;
            bool has_roles = false;
            if (net::read_lobby(r, t, has_roles, g_role_fix)) {
                std::map<uint8_t, bool> ready;
                std::map<uint8_t, int> rtt;
                for (auto& e : t) {
                    ready[e.id] = e.ready;
                    rtt[e.id] = e.rtt;
                }
                bool mine = g_ready[g_sess.my_id()];
                g_ready = ready;
                g_ready[g_sess.my_id()] = mine; // our own flag is ours until the host confirms it
                g_lobby_rtt = rtt;
                if (has_roles) {
                    uint8_t my_role = g_roles[g_sess.my_id()];
                    if (g_role_fix)
                        g_roles.clear(); // players that left are gone from the table
                    for (auto& e : t)
                        g_roles[e.id] = e.role;
                    g_roles[g_sess.my_id()] = my_role;
                }
            }
            return;
        }
        if (type == net::MSG_RES || type == net::MSG_RES_ADD) {
            shared::on_message(g_sess, type, from, r);
            return;
        }
        if (type == net::MSG_PING_MARK) { // u8 origin, vec3, u32 mode
            uint8_t origin = r.u8();
            net::Vec3 p;
            p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
            uint32_t mode = r.u32();
            if (!r.ok())
                return;
            if (g_sess.role() == net::Role::Host) { // relay to the other guests
                origin = from;
                net::Writer w;
                w.u8(origin);
                w.f32(p.x); w.f32(p.y); w.f32(p.z); w.u32(mode);
                g_sess.broadcast_reliable(net::MSG_PING_MARK, w, from);
            }
            add_mark(origin, p, mode);
            return;
        }
        if (type == net::MSG_SPACE) {
            spacecoop::on_message(type, from, r);
            return;
        }
        if (type >= net::MSG_UFO_STATE && type <= net::MSG_UFO_DAMAGE) {
            squadron::on_message(type, from, r);
            return;
        }
        if ((type >= net::MSG_CIV_STATE && type <= net::MSG_CIV_BUILD) || type == net::MSG_CIV_ALARM) {
            civ::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_CREATION) {
            creations::on_message(type, from, r);
            return;
        }
        if ((type >= net::MSG_CELL_STATE && type <= net::MSG_CELL_FOOD) || type == net::MSG_CELL_KO ||
            type == net::MSG_CELL_BITTEN) {
            cellsync::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_TRIBE_STATE || type == net::MSG_TRIBE_CMD) {
            tribe::on_message(type, from, r);
            return;
        }
        switch (type) {
        case net::MSG_NPC_SPAWN: case net::MSG_NPC_DESPAWN: case net::MSG_NPC_STATE:
        case net::MSG_DAMAGE_REQ: case net::MSG_DAMAGE:
            npc::on_message(type, from, r);
            return;
        case net::MSG_SPECIES: case net::MSG_SOCIAL:
            social::on_message(type, from, r);
            return;
        }
        if (g_sess.role() == net::Role::Client && from == 0)
            world::on_message(type, r, g_sess.name_of(0));
    };
    g_sess.cb.old_peer = [](uint8_t, const std::string& name) { // BUG-0219: once per player and join
        // the log line (net: ... older SporeCoop) says what is lost: long chat cut, no species table
        feed_kind(name + " has an older SporeCoop: update both games to the latest", "info");
    };
    g_sess.cb.player_leave = [](uint8_t id, uint8_t reason) {
        trace::line("session", "player %u '%s' left: %s", id, g_remotes.count(id) ? g_remotes[id].name.c_str() : "?",
                    reason == 1 ? "timed out (no packets)" : "left or replaced by its rejoin");
        if (g_role_fix)
            g_roles.erase(id); // ids are reused: the next newcomer starts with no role (BUG-0064)
        shared::on_player_leave(id);
        creations::on_player_leave(id); // BUG-0076: the next player of this id is not shown as this one
        npc::on_player_leave(id);       // BUG-0125
        g_give_pick = net::give_pick_after_leave(g_give_pick, id, g_give_pick_reset_fix); // BUG-0234
        auto it = g_remotes.find(id);
        if (it == g_remotes.end())
            return;
        feed_kind(it->second.name + " left", "leave");
        chat_log("* " + it->second.name + " left");
        g_ready.erase(id);
        g_lobby_dirty = true;
        cellsync::on_player_leave(id);
        despawn(it->second);
        g_remotes.erase(it);
    };
    g_sess.cb.player_state = [](const net::PlayerState& s) {
        auto& r = g_remotes[s.id];
        r.id = s.id;
        if (r.name.empty())
            r.name = g_sess.name_of(s.id);
        r.buf.push(s, H->time_seconds());
    };
    g_sess.cb.chat = [](uint8_t id, const std::string& t) {
        feed_kind("<" + g_sess.name_of(id) + "> " + t, "chat"); // never guessed from the words (BUG-0129)
        chat_log(g_sess.name_of(id) + ": " + t);
    };
}

// ---------------- actions (commands and UI) ----------------
uint16_t default_port() { return (uint16_t)(net::kDefaultPort + H->instance_id() - 1); }

void reset_lobby()
{
    if (g_role_fix)
        g_roles.clear(); // a new session starts with no roles (BUG-0064)
    g_ready.clear();
    g_lobby_rtt.clear();
    g_chat.clear();
}

// a host/join while a session is open: Session::host/join close the old one without cleanup
void end_open_session()
{
    if (g_sess.role() != net::Role::None) {
        if (g_session_end_fix) // off: Session::host / join close the old session themselves
            g_sess.leave("restart");
        end_session(EndPath::Restart, true);
    }
}

bool do_host(uint16_t port, std::string& err)
{
    end_open_session();
    reset_lobby();
    if (!g_sess.host(port, g_name, err))
        return false;
    feed("hosting on port " + std::to_string(g_sess.port()));
    chat_log("* hosting on UDP port " + std::to_string(g_sess.port()));
    g_lobby_dirty = true;
    return true;
}

bool do_join(const std::string& addr, std::string& err)
{
    end_open_session();
    reset_lobby();
    g_sess.rejoin_token = rejoin::token_for_join(H->instance_id()); // BUG-0235
    if (!g_sess.join(addr, g_name, err))
        return false;
    { // the chat log goes into problem reports: no public address there
        net::Endpoint ep;
        std::string e, name;
        bool numeric = net::Endpoint::parse_numeric(addr, net::kDefaultPort, ep, e, &name);
        chat_log("* joining " + (numeric ? ep.masked() : std::string("a host name")));
    }
    return true;
}

void do_leave()
{
    g_sess.leave("user");
    rejoin::clear(); // a clean leave: the next join is a new player (BUG-0235)
    end_session(EndPath::Leave, cellsync::voluntary_leave_cleanup_enabled());
    reset_lobby();
}

const char* role_name(uint8_t r)
{
    return r == 1 ? "military" : r == 2 ? "economic" : r == 3 ? "religious" : "none";
}

void set_role(uint8_t role)
{
    g_roles[g_sess.my_id()] = role;
    if (g_sess.role() == net::Role::Client) {
        net::Writer w;
        w.u8(role);
        g_sess.send_reliable_to(0, net::MSG_ROLE, w);
    } else
        g_lobby_dirty = true;
    notice(g_name + " governs as " + std::string(role_name(role)) + " now", -1);
}

void set_ready(bool on)
{
    trace::line("vote", "own ready=%d set (vote %s)", (int)on, g_vote.active ? "on" : "off");
    g_ready[g_sess.my_id()] = on;
    if (g_sess.role() == net::Role::Client) {
        net::Writer w;
        w.u8(on ? 1 : 0);
        g_sess.send_reliable_to(0, net::MSG_READY, w);
    } else
        g_lobby_dirty = true;
}

int share_all()
{
    int n = 0;
    if (!(g_adapter && g_local)) { // not playing: no world yet, send it once the host plays
        for (auto& [id, r] : g_remotes)
            if (!r.echo) {
                r.needs_world = true;
                r.cine_wait_since = 0; // BUG-0232
            }
        feed("the world goes to everyone when you load or start a game");
        return 0;
    }
    for (auto& p : g_sess.players()) {
        std::string err;
        if (p.id != 0 && world::share_with(p.id, err, g_save_on_share && g_adapter))
            n++;
        else if (p.id != 0)
            feed("cannot share the world: " + err);
    }
    return n;
}

// ---------------- commands ----------------
int c_host(void*, int argc, const char* const* argv, SkOut* out)
{
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : default_port();
    if (argc > 2)
        g_name = utf8::player_name(argv[2]);
    std::string err;
    if (!do_host(port, err)) {
        SAY("host failed: %s", err.c_str());
        return 1;
    }
    SAY("hosting on UDP %u as '%s'", g_sess.port(), g_name.c_str());
    return 0;
}

int c_join(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 2) {
        SAY("usage: coop.join <ip[:port]> [name]");
        return 1;
    }
    if (argc > 2)
        g_name = utf8::player_name(argv[2]);
    std::string err;
    if (!do_join(argv[1], err)) {
        SAY("join failed: %s", err.c_str());
        return 1;
    }
    SAY("joining %s as '%s'", argv[1], g_name.c_str());
    return 0;
}

int c_leave(void*, int, const char* const*, SkOut* out)
{
    do_leave();
    SAY("left session");
    return 0;
}

int c_status(void*, int, const char* const*, SkOut* out)
{
    SAY("%s%s", g_sess.status().c_str(), world::status().c_str());
    for (auto& pl : g_sess.players()) // CAP_VERSION: each player's mod, SporeKit and game build
        SAY("version %u '%s': %s\n", pl.id, pl.name.c_str(), pl.version.empty() ? "unknown (pre-0.7.4)" : pl.version.c_str());
    SAY("remote animations: received %u, played %u\n", g_anims_rx, g_anims_played);
    SAY("%s", npc::status().c_str());
    SAY("%s", social::status().c_str());
    SAY("%s", tribe::status().c_str());
    SAY("%s", cellsync::status().c_str());
    SAY("%s", notify::status().c_str());
    SAY("status block (%d %d %d %d)\n", g_status_block[0], g_status_block[1], g_status_block[2], g_status_block[3]);
    SAY("%s", creations::status().c_str());
    SAY("%s", civ::status().c_str());
    SAY("%s", squadron::status().c_str());
    SAY("%s", spacecoop::status().c_str());
    SAY("%s", gamefixes::status().c_str());
    SAY("dna=%.1f (rx %u totals, forwarded %u gains) solo=%.1f owed=%.1f owed_paid=%u away=%d\n", game::dna(), g_dna_rx,
        g_dna_fwd, g_solo_dna, g_dna_owed, g_dna_owed_paid, (int)g_host_away);
    for (size_t i = g_chat.size() > 6 ? g_chat.size() - 6 : 0; i < g_chat.size(); i++)
        SAY("chat: %s\n", g_chat[i].c_str());
    void* av = g_local;
    SAY("mode=0x%08X local=%p overlay-self=(%.1f %.1f ok=%.0f)\n", game::mode_id(), av, g_dbg_sc[0], g_dbg_sc[1], g_dbg_sc[2]);
    SAY("host_away=%d (times %u) solo_dna=%.1f\n", (int)g_host_away, g_host_away_count, g_solo_dna);
    SAY("host elsewhere=%d, its world's messages dropped %u, dna pending %.1f\n", (int)host_elsewhere(), g_place_dropped,
        g_dna_pending); // BUG-0130
    SAY("away dna: solo_mode=%08X solo_dropped=%u owed_world=%08X owed_dropped=%u\n", g_solo_mode, g_solo_dropped,
        g_dna_owed_world, g_owed_dropped); // BUG-0233
    SAY("strays: pending=%u removed=%u\n", (unsigned)g_strays.size(), g_strays_removed);
    SAY("labels: menu_up=%d window=%d hidden_frames=%u\n", (int)g_menu_up.load(), (int)coop_ui::is_open(), g_menu_hidden);
    double now = H->time_seconds();
    for (auto& [id, r] : g_remotes) {
        net::PlayerState s;
        bool have = r.buf.sample(now, s);
        SAY("remote %u '%s' puppet=%p state=%s age=%.2fs pos=(%.1f %.1f %.1f)\n", id, r.name.c_str(),
            r.puppet, have ? "yes" : "no", r.buf.age(now), s.pos.x, s.pos.y, s.pos.z);
        // tests parse the "puppet pos" line as the one right after the "remote" line: keep it there
        // BUG-0271: a cell puppet of a pool the game freed (the stage is being left) is not read
        if (puppet_now(r)) {
            net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
            float rot_err = -1;
            if (g_mode == modes::kCell) { // facing error in degrees
                net::Quat q;
                modes::mat_to_quat((float*)((uint8_t*)r.puppet + 0x5C), q);
                float dot = std::fabs(q.x * s.rot.x + q.y * s.rot.y + q.z * s.rot.z + q.w * s.rot.w);
                rot_err = 2.f * std::acos(std::min(1.f, dot)) * 57.2958f;
            }
            SAY("   puppet pos=(%.1f %.1f %.1f) err=%.2f rot_err=%.1f species=%08X\n", p.x, p.y, p.z,
                net::dist(p, s.pos), rot_err,
                g_mode == modes::kCreature ? game::species_key(r.puppet).instance
                : g_mode == modes::kCell   ? *(uint32_t*)((uint8_t*)r.puppet + 0xFC) // cell model
                                           : 0u);
            if (g_mode == modes::kCreature)
                SAY("   drive err=%.2f speed=%.2f nudges=%u kept=%u teleports=%u\n", r.drv.last_err,
                    r.drv.last_speed, r.drv.nudges, r.drv.nudges_kept, r.drv.teleports);
        }
        // tests: both players are in the same world and place right now
        SAY("together %u %d\n", id, !r.buf.empty() && r.buf.age(now) < 3.0 && same_place(r.buf.latest()) ? 1 : 0);
    }
    return 0;
}

// BUG-0164: capped like the co-op window (24 characters); in a session the others keep the name the
// session started with, so a change is refused there (`coop.ui` / the window: before host / join)
int c_name(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && g_sess.role() != net::Role::None) {
        SAY("name: %s (cannot change it in a session; leave first)", g_name.c_str());
        return 1;
    }
    if (argc > 1)
        g_name = utf8::player_name(argv[1]);
    SAY("name: %s", g_name.c_str());
    return 0;
}

int c_say(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string t;
    for (int i = 1; i < argc; i++) {
        if (i > 1) t += ' ';
        t += argv[i];
    }
    if (!g_sess.connected()) {
        SAY("not connected");
        return 1;
    }
    g_sess.send_chat(t);
    SAY("sent");
    return 0;
}

int c_avatar(void*, int, const char* const*, SkOut* out)
{
    void* av = local_now(); // BUG-0271
    if (!av) {
        SAY("nothing controlled (mode 0x%08X %s, adapter %s)", game::mode_id(), modes::mode_name(game::mode_id()),
            g_adapter ? g_adapter->name : "none");
        return 1;
    }
    auto st = local_state();
    SAY("%s: local=%p place=0x%08X world '%s'\n", g_adapter->name, av, g_world, modes::current_world_name().c_str());
    SAY("species=%p key=0x%08X\npos=(%.2f %.2f %.2f) |r|=%.1f\nrot=(%.3f %.3f %.3f %.3f)\n"
        "vel=(%.2f %.2f %.2f)\nhp=%.1f/%.1f flags=0x%02X",
        g_adapter->mode == modes::kCreature || g_adapter->mode == modes::kScenario ? game::species_profile(av) : nullptr,
        st.species, st.pos.x, st.pos.y, st.pos.z,
        std::sqrt(st.pos.x * st.pos.x + st.pos.y * st.pos.y + st.pos.z * st.pos.z), st.rot.x, st.rot.y,
        st.rot.z, st.rot.w, st.vel.x, st.vel.y, st.vel.z, st.health, st.max_health, st.flags);
    if (g_adapter->mode != modes::kSpace && g_adapter->mode != modes::kCell) {
        float g = game::ground_radius(st.pos); // tests: the avatar's height above the terrain
        float r = std::sqrt(st.pos.x * st.pos.x + st.pos.y * st.pos.y + st.pos.z * st.pos.z);
        if (g > 0)
            SAY("\nground=%.2f above=%.2f", g, r - g);
    }

    if (g_adapter->mode == modes::kCreature) // age +0xB34 (0 hatchling, 1 adult), the hatchling waiting for its family
        SAY(" age=%d hatchling_waiting=%d", *(int*)((uint8_t*)av + 0xB34), (int)game::hatchling_waiting(av));
    SAY("\ngame_name '%s'", modes::raw_world_name().c_str()); // BUG-0258: CurrentGameName as is
    return 0;
}

int c_echo(void*, int argc, const char* const* argv, SkOut* out)
{
    g_echo = argc < 2 || strcmp(argv[1], "off") != 0;
    if (argc > 2)
        g_echo_offset = (float)atof(argv[2]);
    if (argc > 3)
        g_echo_delay = atof(argv[3]);
    if (!g_echo) {
        auto it = g_remotes.find(200);
        if (it != g_remotes.end()) {
            despawn(it->second);
            g_remotes.erase(it);
        }
        g_echo_q.clear();
    }
    SAY("echo %s offset=%.1f delay=%.2f", g_echo ? "on" : "off", g_echo_offset, g_echo_delay);
    return 0;
}

int c_res(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc >= 3 && !strcmp(argv[1], "fix")) {
        SAY("%s", shared::fix(argv[2], argc >= 4 ? atoi(argv[3]) : -1).c_str());
        return 0;
    }
    // joinsim (tests, BUG-0220): what a join of another player runs, then the change, in one step
    bool joinsim = argc >= 5 && !strcmp(argv[1], "joinsim");
    if (joinsim)
        shared::on_player_join(g_sess.role());
    if ((argc >= 4 && !strcmp(argv[1], "add")) || (joinsim && !strcmp(argv[2], "add"))) {
        std::string err;
        if (!shared::debug_add(argv[2 + joinsim], atof(argv[3 + joinsim]), err)) {
            SAY("%s", err.c_str());
            return 1;
        }
    }
    SAY("%s", shared::status().c_str());
    return 0;
}

// coop.ping: mark the place under the mouse (RTS stages) or where you stand, for everybody
int c_ping(void*, int, const char* const*, SkOut* out)
{
    if (!g_local || g_sess.role() == net::Role::None) {
        SAY("play and join a session first");
        return 1;
    }
    net::PlayerState st = local_state();
    net::Writer w;
    w.u8(g_sess.my_id());
    w.f32(st.pos.x); w.f32(st.pos.y); w.f32(st.pos.z); w.u32(g_mode);
    if (g_sess.role() == net::Role::Host)
        g_sess.broadcast_reliable(net::MSG_PING_MARK, w);
    else
        g_sess.send_reliable_to(0, net::MSG_PING_MARK, w);
    add_mark(g_sess.my_id(), st.pos, g_mode);
    SAY("pinged (%.1f %.1f %.1f)", st.pos.x, st.pos.y, st.pos.z);
    return 0;
}

int c_share(void*, int argc, const char* const* argv, SkOut* out)
{
    if (g_sess.role() != net::Role::Host) {
        SAY("only the host shares its world");
        return 1;
    }
    int n = 0;
    for (auto& p : g_sess.players()) {
        if (p.id == 0 || (argc > 1 && atoi(argv[1]) != p.id))
            continue;
        std::string err;
        if (world::share_with(p.id, err))
            n++;
        else
            SAY("player %u: %s\n", p.id, err.c_str());
    }
    SAY("sharing the world with %d player(s)", n);
    return 0;
}

int c_npc(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "list")) {
        SAY("%s%s", npc::status().c_str(), npc::list().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "herds")) {
        SAY("%s", game::herds_dump().c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "gopos")) { // tests: the avatar to a point (x y z)
        void* av = game::avatar();
        if (!av) {
            SAY("no avatar");
            return 1;
        }
        game::teleport(av, {(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])}, game::orientation(av));
        SAY("avatar moved");
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[1], "goherd") || !strcmp(argv[1], "gonpc"))) { // tests: the avatar d m
                                                                                  // from a herd's home / host NPC
        void* h = (void*)(uintptr_t)strtoul(argv[2], nullptr, 16);
        void* av = game::avatar();
        void* npc = argv[1][2] == 'n' ? npc::host_npc((uint16_t)atoi(argv[2])) : nullptr; // gonpc <id>
        if (!av || (!npc && (!h || IsBadReadPtr(h, 0x40) || game::noun_id(h) != game::kHerdNoun))) {
            SAY("no avatar or not a herd / host npc");
            return 1;
        }
        net::Vec3 p;
        if (npc)
            p = game::position(npc);
        else {
            auto* home = (float*)((uint8_t*)h + 0x34);
            p = {home[0], home[1], home[2]};
        }
        float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), d = argc > 3 ? (float)atof(argv[3]) : 12.f;
        net::Vec3 up = r > 1 ? net::Vec3{p.x / r, p.y / r, p.z / r} : net::Vec3{0, 0, 1};
        // d metres to the side (any direction perpendicular to up) and a little above the ground
        net::Vec3 side = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
        float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        net::Vec3 q{p.x + side.x / sl * d + up.x * 2, p.y + side.y / sl * d + up.y * 2, p.z + side.z / sl * d + up.z * 2};
        game::teleport(av, q, game::orientation(av));
        SAY("avatar at (%.1f %.1f %.1f)", q.x, q.y, q.z);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "epic")) { // host (tests): epic <player id, 0 = host> [dist] [archetype hex] [species hex]
        void* c = npc::debug_spawn_epic((uint8_t)atoi(argv[2]), argc > 3 ? (float)atof(argv[3]) : 8.f,
                                        argc > 4 ? (uint32_t)strtoul(argv[4], nullptr, 16) : 0x95511253u,
                                        argc > 5 ? (uint32_t)strtoul(argv[5], nullptr, 16) : 0u);
        SAY(c ? "epic %p" : "no epic (not the host, no such player or no NPC species near)", c);
        return c ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "partners")) { // tests: NPCs target partners' puppets on/off
        npc::set_partner_targets(strcmp(argv[2], "off") != 0);
        SAY("npcs target partners %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "cinedefer")) { // tests: off = remove creatures at once (before 0.7.2.4)
        game::set_destroy_defer(strcmp(argv[2], "off") != 0);
        SAY("creatures removed during a cinematic: %s", strcmp(argv[2], "off") ? "after it" : "at once");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "strays")) { // tests
        npc::set_strays(strcmp(argv[2], "off") != 0);
        SAY("stray removal %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "killfix")) { // tests: off = the guest's kill list survives skipped frames (BUG-0082)
        npc::set_kill_clear(strcmp(argv[2], "off") != 0);
        SAY("kill list cleared every frame: %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "reuse")) { // host (tests): reuse <id>: a dead NPC's object alive again (BUG-0044)
        SAY("%s", npc::debug_reuse((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[1], "reusefix") || !strcmp(argv[1], "idfix"))) { // tests (BUG-0044 / BUG-0023)
        bool on = strcmp(argv[2], "off") != 0;
        (argv[1][0] == 'r' ? npc::set_reuse_fix : npc::set_id_fix)(on);
        SAY("%s %s", argv[1], on ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "deferpuppet")) { // tests: off = a deferred puppet loses its puppet state at once (BUG-0066)
        game::set_defer_keeps_puppet(strcmp(argv[2], "off") != 0);
        SAY("deferred puppets stay puppets: %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "swapguard")) { // tests: off = no undo of a swap left open (BUG-0110)
        game::set_swap_guard(strcmp(argv[2], "off") != 0);
        SAY("swap guard %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "sharecine")) { // tests: off = the world goes during a cinematic (BUG-0019)
        g_share_cine_fix = strcmp(argv[2], "off") != 0;
        SAY("sharecine %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "deathfix")) { // tests: off = a killed proxy stays alive at 0 health (BUG-0015, BUG-0148)
        npc::set_death_fix(strcmp(argv[2], "off") != 0);
        SAY("deathfix %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "stallfix")) { // tests: off = the old proxy driving (BUG-0169)
        npc::set_stall_fix(strcmp(argv[2], "off") != 0);
        SAY("stallfix %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "knownfix")) { // tests: off = a leaving guest's known NPCs stay (BUG-0125)
        npc::set_known_fix(strcmp(argv[2], "off") != 0);
        SAY("knownfix %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "worldfix")) { // tests: off = worlds told apart by the avatar address only (BUG-0119)
        game::set_world_epoch_fix(strcmp(argv[2], "off") != 0);
        SAY("world counter %s (now %u)", argv[2], game::world_epoch());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "swapfault")) { // tests: the next swapped NPC tick skips its restore
        if (argc > 2 && !strcmp(argv[2], "ledger")) { // the other detours (BUG-0110)
            game::debug_swap_ledger_fault();
            SAY("the next detour that changes game state around a call leaves like a fault");
            return 0;
        }
        game::debug_swap_fault();
        SAY("the next NPC tick with a partner as the player leaves like a fault");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "nextid")) { // host (tests): the next net id tried (BUG-0023)
        npc::debug_next_id((uint16_t)atoi(argv[2]));
        SAY("next npc id %s", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "killhold")) { // tests: the avatar into the guest's kill list
        npc::debug_kill_hold();
        SAY("held");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "hatch")) { // tests
        npc::set_hatch_help(strcmp(argv[2], "off") != 0);
        SAY("hatch help %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "attackmsg")) { // tests: host avatar "attacks" host NPC #id
        bool ok = npc::debug_attack_msg((uint16_t)atoi(argv[2]));
        SAY(ok ? "attack message sent" : "no such npc");
        return ok ? 0 : 1;
    }
    if (argc > 1 && !strcmp(argv[1], "archless")) { // tests: herds like ours (no archetype)
        SAY("%s", npc::herds(argc > 2 && !strcmp(argv[2], "all")).c_str());
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "untuned") || !strcmp(argv[1], "creatures"))) { // tests
        SAY("%s", npc::creatures(argv[1][0] == 'u', argc > 2 ? strtoul(argv[2], nullptr, 16) : 0).c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "hurt")) {
        bool ok = npc::debug_hurt((uint8_t)atoi(argv[2]), (uint16_t)atoi(argv[3]), (float)atof(argv[4]));
        SAY(ok ? "puppet hit through the damage filter" : "no such npc/puppet");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "hosthit")) { // host avatar hits host NPC #id
        bool ok = npc::debug_host_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]));
        SAY(ok ? "host hit applied" : "no such npc");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "spawn")) { // host: an NPC of crt species <hex> next to the avatar
        void* c = npc::debug_spawn((uint32_t)strtoul(argv[2], nullptr, 16));
        SAY(c ? "spawned %p" : "unknown species or no avatar", c);
        return c ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "recruit")) { // host: NPC #id joins the pack (AddMember)
        bool ok = npc::debug_recruit((uint16_t)atoi(argv[2]));
        SAY(ok ? "joined the pack" : "no such npc or it cannot join");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "hit")) {
        bool ok = npc::debug_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]));
        SAY(ok ? "hit sent through the damage filter" : "no such spawned proxy");
        return ok ? 0 : 1;
    }
    if (argc > 1)
        npc::set_enabled(strcmp(argv[1], "off") != 0);
    SAY("npc sync %s\n%s", npc::enabled() ? "on" : "off", npc::status().c_str());
    return 0;
}

int c_social(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "textfix")) { // tests: off = ability keys also while the cheat console is open (BUG-0149)
        g_text_fix = strcmp(argv[2], "off") != 0;
        SAY("textfix %s", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "keys")) { // tests: whether the ability keys are taken now
        SAY("text_up=%d menu_up=%d", (int)g_text_up.load(), (int)g_menu_up.load());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "species")) {
        SAY("%s", social::species_dump().c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "goal")) { // tests: goal <hex crt> <outcome 5|6> <dna> (guest -> host, BUG-0205)
        bool ok = social::debug_send_goal(strtoul(argv[2], nullptr, 16), atoi(argv[3]), (float)atof(argv[4]));
        SAY(ok ? "goal sent" : "not a connected guest");
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "record")) { // tests: record <hex crt> <relation> <impressed>
        bool ok = social::debug_set_record(strtoul(argv[2], nullptr, 16), atoi(argv[3]), (uint32_t)atoi(argv[4]));
        SAY(ok ? "record set" : "no relationship manager");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "goto")) {
        bool ok = social::debug_goto((uint16_t)atoi(argv[2]));
        SAY(ok ? "next to it" : "no such spawned proxy");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "objs")) {
        SAY("%s", social::debug_objects(strtoul(argv[2], nullptr, 16), strtoul(argv[3], nullptr, 16)).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "log")) {
        social::debug_social(0, -1);
        SAY("toggled");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "act")) {
        bool ok = social::debug_social((uint16_t)atoi(argv[2]), argc > 3 ? atoi(argv[3]) : 0);
        SAY(ok ? "social action sent" : "no action (no such proxy, too far, no social ability or cooling down)");
        return ok ? 0 : 1;
    }
    SAY("%s", social::status().c_str());
    return 0;
}

int c_overlay(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "animsend")) { // tests: as if the avatar played animation <hex> (BUG-0091)
        void* av = game::avatar();
        if (av)
            on_anim(av, (uint32_t)strtoul(argv[2], nullptr, 16), 0, false, false);
        SAY(av ? "animation sent" : "no avatar");
        return av ? 0 : 1;
    }
    if (argc > 1 && !strcmp(argv[1], "anims")) {
        g_anim_log = !g_anim_log;
        SAY("avatar animation log %s", g_anim_log ? "on" : "off");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "self")) {
        g_tag_self = !g_tag_self;
        SAY("self tag %s", g_tag_self ? "on" : "off");
        return 0;
    }
    g_overlay = argc < 2 || strcmp(argv[1], "off") != 0;
    SAY("overlay %s", g_overlay ? "on" : "off");
    return 0;
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
        if (r.me)
            r.stage = modes::mode_name(g_mode);
        else {
            auto it = g_remotes.find(p.id);
            if (it != g_remotes.end() && !it->second.buf.empty()) {
                const auto& ls = it->second.buf.latest();
                r.stage = modes::mode_name(ls.mode);
                if (ls.mode == g_mode && !same_place(ls) && g_adapter)
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
        snprintf(b, sizeof b, "Hosting on UDP port %u  -  %d/%d players", g_sess.port(), (int)g_sess.players().size(),
                 net::kMaxPlayers);
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
    if (host_mode == modes::kEdit || host_mode == modes::kEditor)
        return g_sess.name_of(0) + " is evolving the species in the editor: everybody gets the new body";
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

int c_team(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "point")) { // tests: where the first partner is on the screen
        for (auto& [id, r] : g_remotes)
            if (puppet_now(r) && !r.echo) {
                net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
                float w[3] = {p.x, p.y, p.z}, sc[2] = {0, 0};
                int ok = H->world_to_screen(w, sc);
                uint8_t* cr = (uint8_t*)r.puppet;
                SAY("point %d %d %d flags=%08X e84=%p guards=%u msg_guards=%u msg_other=%u", ok, (int)sc[0], (int)sc[1],
                    g_mode == modes::kCreature ? *(uint32_t*)(cr + 0xB58) : 0u,
                    g_mode == modes::kCreature ? *(void**)(cr + 0xE84) : nullptr, g_select_guards, g_stage_msg_guards,
                    g_stage_msg_other);
                return 0;
            }
        SAY("no partner");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "die") && local_now() && g_mode == modes::kCreature) { // tests: a lethal hit
        game::apply_damage(local_now(), 1e6f, 0, nullptr); // the game's damage path (death, caption, respawn)
        SAY("lethal hit applied");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "hit") && local_now() && g_mode == modes::kCreature) { // tests: a hit through
        game::hit(local_now(), (float)atof(argv[2]), 0, nullptr); // the damage filter (the pack rescue applies)
        SAY("hit applied, hp %.1f", game::health(local_now()));
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "hp") && local_now() && g_mode == modes::kCreature) { // tests
        game::set_health(local_now(), (float)atof(argv[2]));
        SAY("hp set");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "come")) { // tests: our creature next to the first partner
        for (auto& [id, r] : g_remotes)
            if (puppet_now(r) && local_now() && g_mode == modes::kCreature) {
                net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
                net::Vec3 me = modes::entity_pos(g_adapter, local_now());
                float d = net::dist(p, me);
                // 3 m from the partner, on our side of it
                net::Vec3 at = d > 0.1f ? net::Vec3{p.x + (me.x - p.x) * 3.f / d, p.y + (me.y - p.y) * 3.f / d,
                                                    p.z + (me.z - p.z) * 3.f / d}
                                        : p;
                game::teleport(local_now(), at, net::Quat{0, 0, 0, 1});
                SAY("moved next to %s (%.1f m away before)", r.name.c_str(), d);
                return 0;
            }
        SAY("no partner");
        return 1;
    }
    if (argc > 1)
        teamplay::set_enabled(strcmp(argv[1], "off") != 0);
    SAY("teamplay %s\n%s", teamplay::enabled() ? "on" : "off", teamplay::status().c_str());
    return 0;
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

int give_selected_to(uint8_t to)
{
    if (to == 0xFF)
        return 0;
    if (g_mode == modes::kTribe)
        return tribe::give_selected(to);
    if (g_mode == modes::kCiv)
        return civ::give_selected(to);
    if (g_mode == modes::kSpace)
        return spacecoop::give_spice(10);
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
    a.report = [] { return make_report("from the co-op window"); };
    a.update_do = autoupdate::start_update;
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
    a.civ_role = [] { return (int)g_roles[g_sess.my_id()]; };
    a.civ_role_cycle = [](int d) { set_role((uint8_t)(((int)g_roles[g_sess.my_id()] + d + 4) % 4)); };
    a.creation = [](int& kind, std::string& name, int& own) {
        if (!g_sess.connected() || !(g_mode == modes::kCreature || g_mode == modes::kCell || g_mode == modes::kSpace))
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
    a.say = [](const std::string& t) {
        if (g_sess.connected())
            g_sess.send_chat(t);
    };
    a.chat = [] { return std::vector<std::string>(g_chat.begin(), g_chat.end()); };
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

int c_uistate(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "log")) {
        coop_ui::g_log_msgs = !coop_ui::g_log_msgs;
        SAY("ui message log %s", coop_ui::g_log_msgs ? "on" : "off");
        return 0;
    }
    if (argc > 1)
        coop_ui::open(atoi(argv[1]) != 0);
    SAY("%s", coop_ui::debug_state().c_str());
    return 0;
}

void on_unload()
{
    gamefixes::shutdown();
    mouse::shutdown(); // its message hook lives in this DLL
    keepalive_stop();
    if (int n = autoupdate::shutdown()) // their image stays mapped until they end (BUG-0060)
        LOG("%d update thread(s) still running: the old image stays loaded until they end", n);
    notify::shutdown();
    coop_ui::shutdown();
    lan_scan(false);
    g_sess.leave("mod unload");
    rejoin::shutdown();
    end_session(EndPath::Unload, true);
}

int c_framefix(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1)
        g_frame_fix = atoi(argv[1]) != 0;
    SAY("frame order fix %s: calls on a puppet the game no longer lists: anims dropped=%u made=%u, despawns "
        "dropped=%u made=%u\n",
        g_frame_fix ? "on" : "off", g_anims_dropped, g_anims_unlisted, g_despawns_dropped, g_despawns_unlisted);
    return 0;
}

// the core's fix switches (tests: 0 = the old behaviour, so a test can show the bug)
int c_corefix(void*, int argc, const char* const* argv, SkOut* out)
{
    struct Sw {
        const char* name;
        bool* on;
    } sw[] = {{"vote_scope", &g_vote_scope_fix}, {"keepalive_ptr", &g_keepalive_ptr_fix},
                {"share_reset", &world::g_share_reset_fix}, {"menu_drop", &coop_ui::g_menu_drop_fix},
                {"update_pin", &autoupdate::g_pin_fix}, {"big_reliable", &net::g_fix.big_reliable},
                {"state_roster", &net::g_fix.state_roster}, {"offset_window", &net::g_fix.offset_window},
                {"sender_time", &net::g_fix.sender_time}, {"id_reuse", &g_id_reuse_fix},
                {"creations_leave", &creations::g_leave_fix},
                {"same_place", &g_place_fix}, {"unique_names", &g_sess.unique_names}, {"rejoin_name", &g_sess.rejoin_by_name},
                {"first_ack", &net::g_fix.first_ack}, {"time_wrap", &net::g_fix.time_wrap},
                {"lan_prune", &net::g_fix.lan_prune}, {"addr_check", &net::g_fix.addr_check},
                {"async_resolve", &net::g_fix.async_resolve},
                {"host_away_reset", &g_host_away_fix}, {"ui_status", &coop_ui::g_status_fix},
                {"lan_pages", &coop_ui::g_lan_pages_fix},
                {"status_block", &g_status_block_fix}, {"window_recenter", &coop_ui::g_recenter_fix},
                {"text_utf8", &utf8::g_cut_fix}, {"notify_text", &notify_text::g_fix},
                {"away_dna", &g_away_dna_fix}, {"away_editor", &g_away_editor_fix},
                {"away_goal", &social::g_goal_owe_fix}};
    for (auto& x : sw)
        if (argc > 2 && !strcmp(argv[1], x.name))
            *x.on = atoi(argv[2]) != 0;
    for (auto& x : sw)
        SAY("%s %d\n", x.name, *x.on ? 1 : 0);
    return 0;
}

// coop.fix: every fix switch of the mod in one place (fixes.h, BUG-0116)
int c_fix(void*, int argc, const char* const* argv, SkOut* out)
{
    bool ok;
    std::string r = fix::command(std::vector<std::string>(argv + 1, argv + argc), &ok);
    if (ok && argc > 2)
        H->log(M, SK_WARN, "coop.fix %s %s", argv[1], argv[2]);
    for (size_t a = 0, b; a < r.size(); a = b + 1) { // a line per call: out_printf takes 8 KB at most
        b = r.find('\n', a);
        b = b == std::string::npos ? r.size() : b;
        SAY("%s\n", r.substr(a, b - a).c_str());
    }
    return ok ? 0 : 1;
}

int c_sessionfix(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1)
        g_session_end_fix = atoi(argv[1]) != 0;
    SAY("session end fix %s\n", g_session_end_fix ? "on" : "off");
    return 0;
}

SkModInfo g_info = {SK_ABI_VERSION, "coop", COOP_VERSION, on_unload};

} // namespace

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
    char nm[256]; // 24 characters of up to 4 bytes each (BUG-0142: 64 bytes cut a CJK name)
    H->config_str(M, "coop", "name", "Player", nm, sizeof nm);
    g_name = utf8::player_name(nm);
    if (g_name.empty())
        g_name = "Player";
    g_share_world = H->config_int(M, "coop", "share_world", 1) != 0;
    g_save_on_share = H->config_int(M, "coop", "save_before_share", 1) != 0;
    trace::init(H, M);
    trace::g_detail = std::max(0, std::min(2, H->config_int(M, "coop", "log_detail", 1)));
    world::init(H, M, feed);
    world::set_playing_probe([] { return g_local != nullptr; });
    world::set_save_blocker(save_blocker);
    // species of the host's own creations: sent ahead of its world (creations.h)
    world::set_share_hook([](uint8_t peer, const std::vector<const std::vector<uint8_t>*>& files) {
        creations::world_species_out(peer, files);
    });
    shared::init(H, M);
    world::set_auto_load(H->config_int(M, "coop", "auto_load_world", 1) != 0);
    game::set_anim_listener(on_anim);
    game::set_dna_listener(on_dna);
    npc::Ctx ctx;
    ctx.sess = &g_sess;
    ctx.puppet_of = [](uint8_t id) -> void* {
        auto it = g_remotes.find(id);
        return it == g_remotes.end() ? nullptr : it->second.puppet;
    };
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
    ctx.host_away = [] { return g_host_away || host_elsewhere(true); };
    ctx.in_place = [](uint8_t id) {
        if (!g_place_fix)
            return true;
        auto it = g_remotes.find(id);
        return it != g_remotes.end() && !it->second.buf.empty() && same_place(it->second.buf.latest());
    };
    ctx.stage_center = [](net::Vec3& p) { return tribe::center(p); };
    npc::init(H, M, ctx);
    savestrip::init(H, M); // BUG-0020: our objects never go into a save
    npc::set_enabled(H->config_int(M, "coop", "share_npcs", 1) != 0);
    npc::set_partner_targets(H->config_int(M, "coop", "npcs_target_partners", 1) != 0);
    social::Ctx sc;
    sc.sess = &g_sess;
    sc.feed = feed;
    sc.notice = [](const std::string& t) { notice(t, -1); };
    sc.puppet_of = [](uint8_t id) -> void* {
        auto it = g_remotes.find(id);
        return it == g_remotes.end() ? nullptr : it->second.puppet;
    };
    sc.blocked = [] { return g_menu_up.load() || coop_ui::is_open() || g_text_up.load(); };
    social::init(H, M, sc);
    game::set_damage_filter(npc::damage_filter);
    setup_callbacks();
    H->subscribe(M, SK_EV_FRAME, on_frame, nullptr);
    if (host_has_diag() && H->diag_context) {
        H->subscribe(M, SK_EV_FAULT, on_fault, nullptr);
        H->diag_context(M, diag_context_fn, nullptr);
        H->log(M, SK_INFO, "SporeCoop %s on SporeKit %s, game %s", COOP_VERSION, H->kit_version(), H->build_name());
    }
    keepalive_start();
    H->subscribe(M, SK_EV_OVERLAY, on_overlay, nullptr);
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.host", "coop.host [port] [name] - host a session", c_host, nullptr, MT);
    H->command_add(M, "coop.join", "coop.join <ip[:port]> [name] - join a host", c_join, nullptr, MT);
    H->command_add(M, "coop.leave", "leave the session", c_leave, nullptr, MT);
    H->command_add(M, "coop.framefix", "coop.framefix [0|1] - forget a torn-down world's puppets before the frame's messages (tests: 0 = the old order, BUG-0091)",
                   c_framefix, nullptr, MT);
    H->command_add(M, "coop.puppet", "coop.puppet hold on|off - tests: the remote players' puppets are not moved (a seeded failure for coop_mode.py, BUG-0251)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 2 && !strcmp(argv[1], "hold"))
                           g_puppet_hold = !strcmp(argv[2], "on") || !strcmp(argv[2], "1");
                       SAY("puppet hold %s\n", g_puppet_hold ? "on" : "off");
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.sessionfix", "coop.sessionfix [0|1] - one cleanup for every session end (tests: 0 = the old per-path lists, BUG-0033)",
                   c_sessionfix, nullptr, MT);
    H->command_add(M, "coop.lanfake", "coop.lanfake <n> [seconds] - n made-up games on the network answer (192.0.2.x, tests, BUG-0183)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       g_lan_fake_n = argc > 1 ? std::clamp(atoi(argv[1]), 0, 64) : 0;
                       g_lan_fake_until = net::now_ms() + (uint32_t)(argc > 2 ? atoi(argv[2]) : 60) * 1000u;
                       SAY("fake games %d, listed %zu, kept %zu\n", g_lan_fake_n, g_disc.games(net::now_ms()).size(),
                           g_disc.stored());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.slowdns", "coop.slowdns <ms> - every host name lookup waits ms first, like a slow name server (tests, BUG-0204)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       g_slow_dns_ms = argc > 1 ? std::clamp(atoi(argv[1]), 0, 60000) : 0;
                       if (net::g_lookup != slow_lookup) {
                           g_real_lookup = net::g_lookup;
                           net::g_lookup = slow_lookup;
                       }
                       SAY("name lookups wait %d ms\n", g_slow_dns_ms.load());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.corefix", "coop.corefix [name 0|1] - the core's fix switches (tests: 0 = the old behaviour)",
                   c_corefix, nullptr, MT);
    H->command_add(M, "coop.fix", "coop.fix [list] | <name> [on|off] | all on|off - every fix switch (tests: off = the code before the fix)",
                   c_fix, nullptr, MT);
    H->command_add(M, "coop.exittest", "coop.exittest [old] - end the game with ExitProcess(0) without Shutdown (tests, BUG-0107)",
                   c_exittest, nullptr, MT);
    H->command_add(M, "coop.targets",
                   "coop.targets [all|objs] - clickable units on screen: kind id owner x y; all: also every synced "
                   "object: obj key distance x y; objs: also every game object: obj o<noun> distance x y (tests)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", targets(argc < 2 ? 0 : !strcmp(argv[1], "all") ? 1 : !strcmp(argv[1], "objs") ? 2 : 0).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.status", "session, players and puppets", c_status, nullptr, MT);
    H->command_add(M, "coop.log", "coop.log detail [0|1|2] - support logging: 0 = the old lines only, 1 = state trace + events "
                   "(default), 2 = verbose; coop.log trace - write the trace lines now",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc >= 3 && !strcmp(argv[1], "detail"))
                           trace::g_detail = std::max(0, std::min(2, atoi(argv[2])));
                       else if (argc >= 2 && !strcmp(argv[1], "trace"))
                           g_trace_at = g_trace_body_at = g_trace_stage_at = -100; // the next frame writes them
                       SAY("log_detail=%d\n", trace::g_detail);
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.name", "coop.name <nick>", c_name, nullptr, MT);
    H->command_add(M, "coop.say", "coop.say <text> - chat", c_say, nullptr, MT);
    H->command_add(M, "coop.avatar", "print local avatar state (planet stages: ground = terrain radius under it, above = height over it)", c_avatar, nullptr, MT);
    H->command_add(M, "coop.echo", "coop.echo [on|off] [offset] [delay] - loopback puppet test", c_echo, nullptr, MT);
    H->command_add(M, "coop.overlay", "coop.overlay on|off|self", c_overlay, nullptr, MT);
    H->command_add(M, "coop.social", "coop.social [species | log | act <npc id> [slot] | goto <npc id>] - creature social games", c_social, nullptr, MT);
    H->command_add(M, "coop.npc", "coop.npc [on|off|list|herds|hit <id> <dmg>|hosthit <id> <dmg>|recruit <id>|spawn <hex>|cinedefer on|off] - host-authoritative NPC sync", c_npc, nullptr, MT);
    H->command_add(M, "coop.res", "coop.res [[joinsim] add <food|money> <n> | fix reset|floor [0|1]] - shared resources", c_res, nullptr, MT);
    H->command_add(M, "coop.ping", "coop.ping - show everybody the place under your mouse / where you are", c_ping,
                   nullptr, MT);
    H->command_add(M, "coop.share", "coop.share [player id] - (re)send the host world (save) to guests", c_share,
                   nullptr, MT);
    H->command_add(M, "coop.leaving", "coop.leaving - tests (BUG-0264): the avatar while the game leaves a stage for the galaxy menu",
                   [](void*, int, const char* const*, SkOut* out) {
                       const auto& l = game::leave_stats();
                       SAY("leaving=%d windows=%u frames=%u raw_gone=%u used_gone=%u refused=%u bad_vtable=%u stage_gone=%u",
                           (int)game::leaving_stage(), l.windows, l.frames, l.raw_gone, l.used_gone, l.refused, l.bad_vtable,
                           l.stage_gone);
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.cellgone", "coop.cellgone - tests (BUG-0271): cell reads refused once the game freed the cell pool",
                   [](void*, int, const char* const*, SkOut* out) {
                       const auto& c = modes::cell_gone_stats();
                       SAY("fix=%d checks=%u outside=%u pool_gone=%u refused=%u windows=%u frames_gone=%u local_refused=%u "
                           "puppet_refused=%u",
                           (int)modes::stale_fix, c.checks, c.outside, c.pool_gone, c.refused, c.windows, c.frames_gone,
                           g_local_refused, g_puppet_refused);
                       return 0;
                   },
                   nullptr, SK_CMD_ANY_THREAD);
    coop_register_debug_commands(H, M);
    teamplay::Ctx tc;
    tc.partners = partners;
    tc.notice = [](const std::string& t) { notice(t, -1); };
    tc.my_name = [] { return g_name; };
    teamplay::init(H, M, std::move(tc));
    if (uintptr_t a = H->sym("Simulator::CreatureSelectionUI"))
        H->hook_create(M, (void*)a, (void*)h_SelectUI, (void**)&o_SelectUI);
    *(int32_t*)(g_blank_e84 + 0x32C) = -1;
    if (uintptr_t a = H->sym("Simulator::CreatureStageEventHandler"))
        H->hook_create(M, (void*)a, (void*)h_StageMsg, (void**)&o_StageMsg);
    if (uintptr_t a = H->sym("Editors::EditorRequest::Submit"))
        H->hook_create(M, (void*)a, (void*)h_Submit, (void**)&o_Submit);
    if (uintptr_t a = H->sym("SporeCoop::Simulator_HandleMessage"))
        H->hook_create(M, (void*)a, (void*)h_SimMsg, (void**)&o_SimMsg);
    if (uintptr_t a = H->sym("Editors::cEditor::GetBlockingErrors"))
        H->hook_create(M, (void*)a, (void*)h_BlockingErrors, (void**)&o_BlockingErrors);
    H->command_add(M, "coop.hoststate", "coop.hoststate mode <hex> <seconds> - tests: our player states report this mode",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 3 && !strcmp(argv[1], "mode")) {
                           g_state_mode_override = (uint32_t)strtoul(argv[2], nullptr, 16);
                           g_state_mode_until = H->time_seconds() + atof(argv[3]);
                       }
                       SAY("state mode override %08X for %.1f s\n", g_state_mode_override,
                           std::max(0.0, g_state_mode_until - H->time_seconds()));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.vote", "coop.vote [test|go] - evolution vote (test: a vote without an editor)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "test") && g_sess.role() == net::Role::Host && !g_vote.active) {
                           start_vote(nullptr, g_name + " wants to evolve (test)");
                       } else if (argc > 1 && !strcmp(argv[1], "editor")) {
                           // a request through the vote (the hook), like the game's own editor entry
                           uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit");
                           void* av = game::avatar();
                           if (!av && game::mode_id() == modes::kTribe) // tests: a member's species (BUG-0217)
                               tribe::for_each_owned([](void* c, uint8_t, void* u) { if (!*(void**)u) *(void**)u = c; }, &av);
                           if (!ctor || !submit || !av) {
                               SAY("not possible here");
                               return 1;
                           }
                           void* req = skg::cdecl_call<void*>(H->sym("GeneralAllocator::new_"), (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
                           skg::thiscall<void*>(ctor, req);
                           skg::field<uint32_t>(req, 0xC) = argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0x465C50BAu;
                           game::Key k = game::species_key(av);
                           skg::field<uint32_t>(req, 0x10) = k.instance;
                           skg::field<uint32_t>(req, 0x14) = k.type;
                           skg::field<uint32_t>(req, 0x18) = k.group;
                           bool r = skg::cdecl_call<bool>(submit, req);
                           SAY("editor request submitted -> %d\n", (int)r);
                       } else if (argc > 1 && !strcmp(argv[1], "action") && g_sess.role() == net::Role::Host) {
                           // an action vote like the superweapon's (tests, BUG-0043)
                           start_action_vote(g_name + " wants to test an action", "the test action runs",
                                             [] { LOG("the test vote action ran"); });
                       } else if (argc > 1 && !strcmp(argv[1], "go"))
                           finish_vote("the host started it");
                       else if (argc > 2 && !strcmp(argv[1], "hold")) // tests (BUG-0234): a due own editor waits
                           g_own_editor_hold_until = H->time_seconds() + atof(argv[2]);
                       SAY("vote %s: %s (%d s)\nown editor %d, part list waived %u, editor held for a save %u, "
                           "quit held for a save %u, repeated requests dropped %u, own editor due %d (waited %u frames)\n"
                           "actions run %u, votes dropped %u",
                           g_vote.active ? "active" : "none", g_vote.text.c_str(), g_vote.left, (int)g_own_editor,
                           g_part_list_waivers, g_submits_held, g_quits_delayed, g_submits_repeated,
                           g_own_editor_due ? 1 : 0, g_own_editor_waits, g_vote_actions_run, g_votes_dropped);
                       return 0;
                   },
                   // outside the frame: `editor` submits an editor request, which the game does
                   // from its update, never inside Present (BUG-0096)
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    tribe::Ctx trc;
    trc.sess = &g_sess;
    trc.feed = feed;
    trc.notice = [](const std::string& t) { notice(t, -1); };
    trc.host_away = [] { return g_host_away || (g_world_gate_fix && host_elsewhere()); }; // BUG-0231
    tribe::init(H, M, std::move(trc));
    notify::init(H, M, [] { return coop_ui::text_font(); });
    creations::Ctx crc;
    crc.sess = &g_sess;
    crc.feed = feed;
    crc.name_of = [](uint8_t id) { return g_sess.name_of(id); };
    crc.cell_rebuilt = [] { cellsync::join_partner(); };
    crc.appearance_changed = [](uint8_t id) { // rebuild that player's puppet with its own look
        auto it = g_remotes.find(id);
        if (it != g_remotes.end())
            despawn(it->second);
    };
    creations::init(H, M, std::move(crc));
    modes::g_species_for_player = [](uint8_t id) { return creations::player_species(id); };
    modes::g_model_for_player = [](uint8_t id, int kind, uint32_t key[3]) {
        creations::Key k = creations::player_key(id, (creations::Kind)kind);
        key[0] = k.instance; key[1] = k.type; key[2] = k.group;
        return k.instance != 0;
    };
    H->command_add(M, "coop.role", "coop.role [0-3 | fix 0|1] - civilization role: 0 none, 1 military, 2 economic, 3 religious",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 2 && !strcmp(argv[1], "fix")) { // tests: 0 = before BUG-0064 / BUG-0079
                           g_role_fix = atoi(argv[2]) != 0;
                           SAY("role fix %s\n", g_role_fix ? "on" : "off");
                           return 0;
                       }
                       if (argc > 1)
                           set_role((uint8_t)(atoi(argv[1]) & 3));
                       for (auto& p : g_sess.players())
                           SAY("role %u %s %s\n", p.id, p.name.c_str(), role_name(g_roles[p.id]));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.give", "coop.give [player id | next | target | fix 0|1] - give the selected own tribe members / vehicles (or the opened city); next: the next recipient",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "next")) { // the co-op window's ">" (BUG-0075)
                           give_target_cycle();
                           SAY("give target: %u %s", give_target(), g_sess.name_of(give_target()).c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "fix")) {
                           g_give_pick_fix = atoi(argv[2]) != 0;
                           SAY("give pick fix %d", (int)g_give_pick_fix);
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "target")) {
                           SAY("give target: %u %s", give_target(), g_sess.name_of(give_target()).c_str());
                           return 0;
                       }
                       uint8_t to = argc > 1 ? (uint8_t)atoi(argv[1]) : give_target();
                       SAY("given: %d", give_selected_to(to));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.creations", "coop.creations [list | next|prev <creature|cell|ufo> | choose <kind> <hex>]",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", creations::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.sync", "coop.sync - pose of every synchronized object (key x y z qx qy qz qw), tests",
                   [](void*, int, const char* const*, SkOut* out) {
                       std::vector<syncdbg::Entry> v;
                       auto player = [&](uint8_t id, const modes::Adapter* a, void* e) {
                           if (!a || !e)
                               return;
                           net::PlayerState st;
                           a->fill(e, st);
                           v.push_back({"p" + std::to_string(id), st.pos, st.rot});
                       };
                       if (g_sess.connected()) {
                           player(g_sess.my_id(), g_adapter, local_now()); // BUG-0271
                           for (auto& [id, r] : g_remotes)
                               if (!r.echo)
                                   player(id, r.puppet_adapter, puppet_now(r));
                       }
                       for (auto* f : {npc::sync_entries, tribe::sync_entries, civ::sync_entries,
                                       squadron::sync_entries, cellsync::sync_entries})
                           for (auto& e : f())
                               v.push_back(e);
                       for (auto& e : v) // one line per call: the console buffer of a call is 8 KB
                           SAY("%s", syncdbg::format({e}).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.notify", "coop.notify <text> - show a co-op notification (game event log)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string t;
                       for (int i = 1; i < argc; i++)
                           t += (i > 1 ? " " : "") + std::string(argv[i]);
                       feed(t);
                       SAY("%s", notify::status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    cellsync::Ctx cc;
    cc.sess = &g_sess;
    cc.partners = partners;
    cc.notice = [](const std::string& t) { notice(t, -1); };
    cc.my_name = [] { return g_name; };
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
    cellsync::init(H, M, std::move(cc));
    H->command_add(M, "coop.pool", "coop.pool [bite | food <n> | hp <n> | hurt <player> <dmg>] - cell co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", cellsync::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    civ::Ctx cvc;
    cvc.sess = &g_sess;
    cvc.feed = feed;
    cvc.notice = [](const std::string& t) { notice(t, -1); };
    cvc.role_of = [](uint8_t id) { return g_roles.count(id) ? g_roles[id] : (uint8_t)0; };
    cvc.vote = [](const std::string& t, const std::string& d, std::function<void()> a) {
        return start_action_vote(t, d, std::move(a));
    };
    cvc.beacon = add_beacon;
    cvc.host_away = [] { return g_host_away || (g_world_gate_fix && host_elsewhere()); }; // BUG-0231
    civ::init(H, M, std::move(cvc));
    census::Ctx csc;
    csc.sess = &g_sess;
    csc.place = [](uint32_t& m, uint32_t& w) {
        m = g_mode;
        w = g_world;
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
    gamefixes::init(H, M);
    autoupdate::set_feed(feed);
    autoupdate::init(H, M, COOP_VERSION);
    H->command_add(M, "coop.update", "coop.update [check | apply] - SporeCoop updates from GitHub releases",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", autoupdate::command(argc > 1 ? argv[1] : "").c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.space",
                   "coop.space [hit <dmg> | hp | jump [id] | give <n> | spice [add <n>] | terra <a> <w> <t> | planet | empire [colony <PlanetID> [spice] | contact <StarID>]]"
                   " - space co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", spacecoop::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.fleet", "coop.fleet [list | shoot <id> <dmg> | hurt <ship id> <player> [dmg] | spawn <type>] - space co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", squadron::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.civ",
                   "coop.civ [select <id> <0|1> | move <id> <x> <y> <z> | buy <city> <loco> | build <city> <kind>]",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1) {
                           SAY("%s", civ::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                           return 0;
                       }
                       SAY("%s%s", civ::status().c_str(), civ::list().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.tribe",
                   "coop.tribe [select <id> <0|1> | walk <x> <y> <z> | act <action> <target id>] - tribe co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1) {
                           SAY("%s", tribe::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                           return 0;
                       }
                       SAY("%s%s", tribe::status().c_str(), tribe::list().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.team", "coop.team [on|off] - co-op mechanics (joint hunt, rescue, pack regen)", c_team,
                   nullptr, MT);
    init_ui();
    H->command_add(M, "coop.uistate", "coop.uistate [0|1] - co-op window state (open/close it)", c_uistate, nullptr,
                   SK_CMD_OUTSIDE_FRAME);
    H->cheat_register("coop", "coop.",
                      "coop host [port] | join <ip[:port]> | leave | status | name <nick> | say <text>");
    LOG("coop ready (name '%s')", g_name.c_str());
    return &g_info;
}
