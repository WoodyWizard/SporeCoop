// SporeCoop mod: creature-stage co-op ("pack" mode). See docs/03-gameplay.md.
// the release version (tools/package.sh names the zip after it)
#ifndef COOP_VERSION
#define COOP_VERSION "0.7.14.1-lineages66-dev"
#endif

#include <set>
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
#include "adventure.h"
#include "creature_rescue.h"
#include "coop_ui.h"
#include "founders.h"
#include "slotfs.h"
#include "lineages_graphics.h"
#include "driver.h"
#include "game.h"
#include "dna_sync.h"
#include "modes.h"
#include "stage_epoch.h"
#include "mouse.h"
#include "fov.h"
#include "editor_cfg.h"
#include "editor_depth.h"
#include "shared.h"
#include "sui.h"
#include "teamplay.h"
#include "evolve.h"
#include "tribe.h"
#include "tribe_late.h"
#include "drama.h"
#include "identity.h"
#include "bans.h"
#include "galaxy_client.h"
#include "era_visitors.h"
#include "galaxy_menu.h"
#include "party.h"
#include "parties_net.h"
#include "net/galaxy.h"
#include "home_nests.h"
#include "cellsync.h"
#include "audio.h"
#include "census.h"
#include "civ.h"
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
#include "social.h"
#include "party_bar.h"
#include "world.h"
#include "net/validate.h"
#include "find.h"
#include "net/discovery.h"
#include "net/fixes.h"
#include "net/give_target.h"
#include "net/interp.h"
#include "net/lobby.h"
#include "net/session.h"
#include "net/utf8.h"
#include "creations.h"
#include "lineage_cells.h"
#include "library.h"
#include "partner_marker.h"
#include "ring_timer.h"
#include "progress_debt.h"
#include "mem_refresh.h"
#include "guest_pack.h"
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
#include "sreport.h"
#include "space_ext.h"
#include "host_world.h"
#include "own_world.h"
#include "diag_coop.h"
#include "trace.h"
#include "view_centres.h"

static const SkHost* H;
static SkMod* M;
extern const SkHost* g_host_for_modes; // modes.cpp
extern SkMod* g_mod_for_modes;

void coop_register_debug_commands(const SkHost* host, SkMod* self); // debug.cpp

#define LOG(...) H->log(M, SK_INFO, __VA_ARGS__)
#define SAY(...) H->out_printf(out, __VA_ARGS__)

namespace {
static void invite_prompt_tick(); // 6J (with the right-click menu, below)
static void social_tick(double now); // 6L A (below)

struct Remote {
    uint8_t id = 0;
    std::string name;
    net::SnapshotBuffer buf;
    void* puppet = nullptr;
    drive::State drv;
    const modes::Adapter* puppet_adapter = nullptr; // adapter that created `puppet`
    uint32_t puppet_world = 0; // the place `puppet` was made in
    bool echo = false; // local loopback test remote
    bool silent_said = false; // BUG-0513: "is not responding" was told (core.partner_silent)
    // BUG-9141: this partner's knockout as seen here (Creature / Adventure revive ring): since when its state says
    // downed, and since when this player stands within reach of it (-1: not)
    double down_since = -1, near_since = -1;
    evolve::Cocoon cocoon; // BUG-9160: this partner's cocoon while it is in an editor of its own
    uint64_t world_sent = 0; // host: (mode << 32 | save crc) last sent to this guest
    double other_mode_since = -1; // host: since when the guest plays another mode
    bool joined_phase = false; // host: the guest played the host's mode after the last world send
    bool needs_world = false;  // host: joined while the host was not playing; send the world later
    double cine_wait_since = 0; // host: since when this guest's share waits for a cinematic (BUG-0232)
    // host, BUG-0508 (world.evolve_in_place): after the host's editor this guest was in the same world and
    // stage, so it may take the new body without the whole world; evo_check_until: until then its avatar
    // must have left the host's pre-editor species (Creature), or it gets the world after all
    bool evo_inplace = false;
    double evo_check_until = 0;
    // host, BUG-0231 (core.share_retry): a failed share is retried; dropped with the entry at the
    // session end (g_remotes.clear())
    uint32_t join_serial = 0; // this guest's join (the share's tag: an id reused by a newcomer is not it)
    bool share_at_join = false; // the last share was the join's (a retry waits for needs_world)
    int share_tries = 0;        // retries in this phase (share_try_key)
    uint64_t share_try_key = 0;
    double retry_at = 0;        // no share for this guest before
    // the invariant monitor (docs/06-testing.md#testing-monitor): the puppet read back from the game at
    // the start of the mod's frame (before any driving: where the game drew it) on a frame that writes
    // the trace lines; drawn_at = that frame's time, -1: not read
    net::PlayerState drawn_st;
    double drawn_at = -1;
    // coop.track (BUG-0414): the partner's animations played on its puppet (MSG_ANIM), the last one's id
    uint32_t anims = 0, anim_last = 0;
    // BUG-0923 (notify.appeared_once): the shown puppet was taken away to be built again with the partner's new
    // look (creations' appearance_changed): the puppet made next is the same appearance, not told again
    bool relook = false;
};

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
float g_dna_bar_sent = NAN;
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
// BUG-0231 (GitHub #165, dna_route.h row 2): DNA a guest earns while it plays another world of
// the host's stage stays in that world (0.7.3.0 sent it to the host's world). Off: forwarded.
bool g_world_dna_fix = true;
COOP_FIX("core.world_dna", "BUG-0231", g_world_dna_fix, "core: DNA a guest earns in its own world stays in that world");
uint32_t g_dna_own = 0; // tests: gains kept in the guest's own world (row 2)
// BUG-0466 (class G6, BUG-0423): a guest's own DNA spend (its own creature editor of a vote: the
// editor takes the price from the game's GetEvolutionPoints when it closes) was never sent: the guest
// forwards only gains (on_dna), so the host's next total (sent on a change and every 2 s) put the
// guest's DNA back to what it had before the editor (free DNA, and the host's total never paid for
// the guest's parts). Now a guest samples its own DNA while it plays the host's world: a drop it
// made itself (not a host total, not a load) is a spend; it is kept across the editor (the stage
// epoch: an editor and its Done keep it, a load does not), held while the host is away, sent once
// to the host as a negative MSG_DNA_ADD (an older host ignores it: d > 0 only) and taken off the
// party's total there (never below 0). Off: only gains go to the host (0.7.3.6).
bool g_dna_spend_fix = true;
COOP_FIX("core.dna_spend", "BUG-0466", g_dna_spend_fix, "core: the DNA a guest spends in its own editor leaves the party's total and is not given back");
// BUG-9162 (owner decision 2F, "Everyone gets their own DNA."): in Creature each player's spendable DNA (the editor's
// budget, the HUD's number) is their own: their own gains, their kills (a guest's kill is paid on the host's game, which
// sends it on: MSG_DNA_OWN), their own editor's spending. The progress bar (cPlayer +0x10F0) stays the party's: every
// gain still reaches the host, which adds it to the bar only, and sends the bar as before. Off: one shared pool (0.7.8).
static bool g_personal_dna = true;
COOP_FIX("creature.personal_dna", "BUG-9162", g_personal_dna,
         "creature: every player spends their own DNA (the progress bar stays the party's)");
static uint32_t g_dna_own_sent, g_dna_own_got;
extern uint32_t g_mode;
static bool personal_dna() { return g_personal_dna && g_mode == modes::kCreature && g_sess.connected(); }
static float g_dna_seen = 0;           // guest: the game's DNA after the last sample / host total / own gain
static bool g_dna_seen_ok = false;
static uint32_t g_dna_seen_epoch = 0;  // the Creature stage epoch of g_dna_seen (a load starts a new one)
static float g_dna_spend = 0;          // guest: spent here, not sent yet (<= 0)
static uint32_t g_dna_spend_world = 0; // the world it was spent in
static float g_dna_spend_owed = 0;     // host: guests' spends that arrived while it had no creature (<= 0)
static uint32_t g_dna_spends_sent = 0, g_dna_spends_got = 0; // tests
void feed_kind(const std::string& s, const char* kind);
// the DNA cause ledger and its notices (dna_ledger.h; player request 2026-11-01, BUG-0351)
static std::vector<dna_ledger::Part> g_dna_out; // host: the causes since the last total sent
static dna_ledger::Notices g_dna_notices;
static uint32_t g_dna_noticed;
static void dna_note(const dna_ledger::Part& p, uint8_t me)
{
    if (dna_ledger::worth_a_notice(p, me))
        g_dna_notices.add(p.cause, p.player, p.amount, H->time_seconds());
}
// host: a gain applied here now (before the game adds it)
static void dna_host_gain(const dna_ledger::Part& p, float before)
{
    LOG("dna: %.0f -> %.0f cause=%s", before, before + p.amount, dna_ledger::causes_text({p}).c_str());
    if (!g_sess.connected())
        return;
    bool merged = false;
    for (auto& o : g_dna_out)
        if (o.cause == p.cause && o.player == p.player) {
            o.amount += p.amount;
            merged = true;
        }
    if (!merged && g_dna_out.size() < 16)
        g_dna_out.push_back(p);
    dna_note(p, 0);
}
static void dna_notices_tick(double now)
{
    for (auto& p : g_dna_notices.due(now)) {
        bool mine = g_sess.connected() && p.player == g_sess.my_id();
        feed_kind(dna_ledger::notice_text(p, g_sess.name_of(p.player), mine), "good");
        g_dna_noticed++;
    }
}

bool guest_elsewhere();
bool guest_space_other();
// BUG-0510 (dna_pace.h): every gain entering the party's shared DNA / food is scaled by 1/N, N = the
// players playing the host's world now, so a party evolves at the single-player pace. The host
// decides (coop.ini [coop] dna_pace, 0 = the full sum) and announces the divisor with its totals
// (MSG_DNA's and MSG_CELL_FOOD's optional tail); each gain is scaled once, where its game made it.
// Off (`coop.fix core.dna_pace off`): every gain whole, the sum of all players (0.7.3.6).
bool g_dna_pace_fix = true;
COOP_FIX("core.dna_pace", "BUG-0510", g_dna_pace_fix, "core: the party's DNA grows at the single-player pace (each gain / N players)");
static int g_pace_heard = 1;          // guest: the host's divisor (1 until a total with one, or from an older host)
static uint32_t g_paced = 0;          // tests: gains scaled here
static float g_paced_in = 0, g_paced_out = 0; // tests: their sum before / after
extern uint32_t g_world;
// host: the players playing its world now (itself and the guests in its stage and world)
static int pace_playing()
{
    int n = 1;
    if (!g_world)
        return n;
    for (auto& [id, r] : g_remotes) {
        if (r.echo || r.buf.empty())
            continue;
        const auto& ls = r.buf.latest();
        if (ls.mode == g_mode && ls.world == g_world)
            n++;
    }
    return n;
}
// the divisor the host announces
int dna_pace_announce()
{
    return dna_pace::divisor(g_dna_pace_fix && settings::on(settings::kDnaPace), pace_playing());
}
// the divisor for a gain of this machine's game now (1: whole)
static int pace_div()
{
    if (!g_dna_pace_fix || dna_pace::g_bypass || !g_sess.connected())
        return 1;
    // Lineages (0.7.8 merge): a campaign's own workload (seats x difficulty, world::rules) already scales
    // its gains (Creature activity awards, Cell meals through CellCredit). Upstream's 1/N on top made a
    // two-seat campaign earn a quarter of its tuned pace. Inside such a campaign its rules decide.
    // BUG-9266: on a galaxy planet each party's own size scales its gains (on_dna): never divided by all the players
    if (world::party_difficulty() || world::rules_for_current().workload_percent() != 100)
        return 1;
    if (g_sess.role() == net::Role::Host)
        return dna_pace_announce();
    if (guest_elsewhere() || g_host_away)
        return 1; // its own world / playing alone meanwhile: not the party's total
    return std::max(1, g_pace_heard);
}
float dna_pace_gain(float amount)
{
    int div = pace_div();
    float out = dna_pace::share(amount, div);
    if (out != amount) {
        g_paced++;
        g_paced_in += amount;
        g_paced_out += out;
        trace::line("dna", "pace 1/%d: +%.1f -> +%.1f", div, amount, out);
    }
    return out;
}
static dna_pace::Carry g_pace_carry;
int dna_pace_meal(int amount) // Cell: an own meal's food points
{
    int div = pace_div();
    int out = g_pace_carry.take(amount, div);
    if (out != amount) {
        g_paced++;
        g_paced_in += (float)amount;
        g_paced_out += (float)out;
        trace::line("dna", "pace 1/%d: meal +%d -> +%d", div, amount, out);
    }
    return out;
}
void dna_pace_heard(int div) { g_pace_heard = std::clamp(div, 1, 255); }

bool on_dna(float& delta, bool activity_reward)
{
    // Lineages campaign: a Creature activity award is scaled by the campaign's workload. Such a gain
    // is not paced again by BUG-0510 below (that would scale it twice).
    bool campaign_scaled = false;
    if (activity_reward && delta > 0 && g_sess.connected() && game::in_creature_stage()) {
        const auto work = world::rules_for_me().workload_percent(); // (BUG-9266: this player's party)
        if (work != 100) {
            float raw = delta;
            delta = float(double(delta) * 100.0 / work);
            campaign_scaled = true;
            trace::ev("dna","scaled","Creature award %.3f at %u%% workload -> %.3f",raw,work,delta);
        }
    }
    bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    auto g = dna_route::guest_gain(guest, delta > 0, g_world_dna_fix && guest_elsewhere(), g_away_dna_fix && g_host_away);
    if (g == dna_route::Gain::Own) {
        g_dna_own++;
        trace::ev("dna", "own", "+%.0f kept in this world (the host plays another one)", delta);
        return true;
    }
    if (g == dna_route::Gain::Solo) {
        if (g_solo_dna <= 0)
            g_solo_mode = g_mode; // the stage it is earned in (BUG-0233)
        g_solo_dna += delta; // nobody to send it to: kept here, given to the pack on the host's return
        g_dna_seen += delta;  // BUG-0466: applied here now, not a spend
        return true;
    }
    if (social::goal_dna(delta))
        return false; // a goal card's reward: the host pays it into the shared pool once
    if (g == dna_route::Gain::Local) {
        if (g_sess.role() == net::Role::Host && delta > 0 && g_mode == modes::kCreature && !campaign_scaled)
            delta = dna_pace_gain(delta); // BUG-0510: the host's own gain (after the goal card: those stay whole)
        if (g_sess.role() == net::Role::Host && delta > 0 && g_mode == modes::kCreature) {
            delta = progress_debt::absorb(delta); // BUG-9119: a progress debt is repaid first
            if (delta <= 0)
                return false; // all of it went to the debt: no DNA, the bar stays
        }
        if (g_sess.role() == net::Role::Host && delta != 0 && g_mode == modes::kCreature) {
            const auto part = dna_ledger::g_credit.gain(delta, H->time_seconds());
            dna_host_gain(part, game::dna());
            if (personal_dna() && delta > 0 && part.cause == dna_ledger::Cause::Hunt && part.player != g_sess.my_id() &&
                net::is_player(g_sess, part.player)) {
                // BUG-9162: a guest's kill: the party's bar here, the DNA to that guest
                game::dna_sync_raw(game::dna(), game::evolution_bar() + delta);
                net::Writer w;
                w.f32(delta);
                g_sess.send_reliable_to(part.player, net::MSG_DNA_OWN, w);
                g_dna_own_sent++;
                return false;
            }
        }
        return true;
    }
    if (g_host_away) { // core.away_dna off: the old order (a goal card's reward went to the host)
        g_solo_dna += delta;
        return true;
    }
    if (personal_dna()) { // BUG-9162: its own DNA here; the host adds it to the party's bar only
        if (delta > 0) {
            net::Writer w;
            w.f32(campaign_scaled ? delta : dna_pace_gain(delta));
            g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
            g_dna_fwd++;
        }
        g_dna_seen += delta; // not a spend (BUG-0466's sampling)
        return true;
    }
    net::Writer w;
    w.f32(campaign_scaled ? delta : dna_pace_gain(delta)); // BUG-0510: its share, with the host's divisor (the host adds it whole)
    g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
    g_dna_fwd++;
    return false; // applied when the host's total comes back
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

void chat_log(const std::string& s, uint32_t argb = 0)
{
    g_chat.push_back(s);
    g_chat_col.push_back(argb);
    while (g_chat.size() > 50)
        g_chat.pop_front();
    while (g_chat_col.size() > g_chat.size())
        g_chat_col.pop_front();
}

// an event everybody sees (feed + lobby chat): guests send it to the host, the host relays it
void notice(const std::string& text_in, int from = -1)
{
    // BUG-9239 (seen 2026-11-16: "Host-NoShare- evolves its own creature"): the -NoShare- tag never shows in a notice
    const std::string text = net::g_fix.shown_names ? utf8::shown_name(text_in) : text_in;
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
using LocalAddr = adapters::Addr;
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
    return a && a == modes::for_mode(modes::kCreature);
}

void despawn(Remote& r)
{
    evolve::drop(r.cocoon, true); // BUG-9160: its sac goes with the puppet
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
double g_puppet_hide_until = 0; // tests (coop.puppet respawn <s>): partners' puppets stay away until then (BUG-0453)
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

// BUG-1055: every module's session and world state registered in lifecycle.h is reset on every session end and on
// every new world (the stage counters moved), and `coop.lifecycle held` lists what a reset left. Off: each module's
// own on_session_end / on_world_change only (0.7.13).
static bool g_lifecycle_fix = true;
COOP_FIX("core.lifecycle_reset", "BUG-1055", g_lifecycle_fix, "core: the session and world state every module registered (lifecycle.h) is reset on every session end and world change");
void forget_puppets() // world changed or the local player left it (editor, load)
{
    const modes::Adapter* creature = modes::for_mode(modes::kCreature);
    for (auto& [id, r] : g_remotes) {
        if (r.puppet) {
            game::puppet_unregister(r.puppet);
            if (g_lifecycle_fix)
                creature_hp::forget_puppet(r.puppet); // BUG-1055: no reported health kept for this address
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

void* puppet_now(const Remote& r);
// BUG-0426 class (L1 of 0.7.13): player id's Creature puppet, only while it is the live object made for it
void* creature_puppet_live(uint8_t id)
{
    auto it = g_remotes.find(id);
    if (it == g_remotes.end() || !it->second.puppet || !creature_puppet(it->second.puppet_adapter))
        return nullptr;
    void* p = puppet_now(it->second);
    return p && game::creature_listed(p) ? p : nullptr;
}

// ---- BUG-0913: a partner's creature does not block the way nor take the attack click while friendly fire is off ----
// The second player to attack an animal the partner fights stopped 2 m short behind the partner's creature (its
// puppet here, held at the partner's place) and its attack key did nothing; a click on the animal picked the partner
// in front of it (the attack key then does nothing: status 12). Owner's decision 2026-11-11 (docs/plan-0.7.13.md):
// while friendly fire is off and outside Rivals, in Creature,
// (a) Havok's collision filter (hkGroupFilter::isCollisionEnabled 0x10C3F90, asked when two bodies' boxes start to
//     overlap) says no for the pair of our avatar and a partner's puppet: we walk through it
//     (docs/re/creature.md#creature-partner-pass);
// (b) the Creature mode's hover filter 0xD39070 (cdecl bool(HoverHit*), asked for every object under the cursor
//     ray) refuses a partner's puppet in the combat stance: the hover and the click take the object behind it.
// With friendly fire on or in Rivals nothing changes. Accepted: players' creatures can stand inside each other.
// Off: 0.7.12.
static bool g_partner_pass_fix = true;
COOP_FIX("creature.partner_pass", "BUG-0913", g_partner_pass_fix, "creature: with friendly fire off a player walks through a partner's creature and a click goes to the animal behind it");
// what the hooks read (they may run inside Havok's step): written once a frame by partner_pass_tick
static void* volatile g_pass_avatar;
static void* volatile g_pass_puppets[net::kMaxPlayers]; // (6E B: one per player)
static volatile bool g_pass_on;
static uint32_t g_pass_pairs, g_pass_hover_skipped, g_pass_seen_own; // tests (coop.status)
static uintptr_t a_AbilityModeCore;
static void partner_pass_tick()
{
    bool on = g_partner_pass_fix && g_mode == modes::kCreature && g_sess.connected() &&
              !settings::on(settings::kFriendlyFire) && !(spaceext::rivals() || pfind::g_test_rivals);
    size_t n = 0;
    if (on)
        for (auto& [id, r] : g_remotes)
            if (n < (size_t)net::kMaxPlayers && r.puppet && !r.echo)
                if (void* p = creature_puppet_live(id))
                    g_pass_puppets[n++] = p;
    for (size_t i = n; i < (size_t)net::kMaxPlayers; i++)
        g_pass_puppets[i] = nullptr;
    g_pass_avatar = on ? game::avatar() : nullptr;
    g_pass_on = on && n > 0;
}
// o is the creature (its base pointer, as the casts give it) or its spatial part (+0xC0)
static bool pass_is(void* who, void* o) { return who && o && (o == who || o == (char*)who + 0xC0); }
static bool pass_partner(void* o)
{
    for (void* p : g_pass_puppets) {
        if (!p)
            break; // (filled from the front)
        if (pass_is(p, o))
            return true;
    }
    return false;
}
// the game object of a Havok collidable: the root's owner (broadphase type 1 = an entity), its userData (+0xC)
static void* collidable_object(uint8_t* c)
{
    if (!c)
        return nullptr;
    while (*(uint8_t**)(c + 0xC))
        c = *(uint8_t**)(c + 0xC);
    if (c[0x18] != 1)
        return nullptr;
    uint8_t* entity = c + *(int32_t*)(c + 0x10);
    return *(void**)(entity + 0xC);
}
// tests (BUG-0913 on rel/0.7.13): the pairs of a non-entity collidable (broadphase type 2, a phantom: hkpWorldObject,
// its userData at +0xC as an entity's) with the avatar or a partner's puppet, which the rule above lets through
static uint32_t g_pass_phantom_pairs, g_pass_phantom_own, g_pass_phantom_partner;
static void* collidable_phantom(uint8_t* c)
{
    if (!c)
        return nullptr;
    while (*(uint8_t**)(c + 0xC))
        c = *(uint8_t**)(c + 0xC);
    if (c[0x18] != 2)
        return nullptr;
    uint8_t* wo = c + *(int32_t*)(c + 0x10);
    return *(void**)(wo + 0xC);
}
using CollEnabled_t = uint8_t*(SK_THISCALL*)(void*, uint8_t*, uint8_t*, uint8_t*);
static CollEnabled_t o_CollEnabled;
static uint8_t* SK_THISCALL h_CollEnabled(void* self, uint8_t* out, uint8_t* a, uint8_t* b)
{
    if (g_pass_on && out && a && b) {
        void* av = g_pass_avatar;
        void *oa = collidable_object(a), *ob = collidable_object(b);
        bool own_a = pass_is(av, oa), own_b = pass_is(av, ob);
        g_pass_seen_own += own_a || own_b;
        if (void* pa = !oa ? collidable_phantom(a) : nullptr; pa || (!ob && collidable_phantom(b))) {
            void* ph = pa ? pa : collidable_phantom(b);
            void* other = pa ? ob : oa;
            g_pass_phantom_pairs++;
            g_pass_phantom_own += (pass_is(av, ph) && pass_partner(other)) || (pass_partner(ph) && pass_is(av, other));
            g_pass_phantom_partner += pass_partner(ph) || pass_partner(other);
        }
        if ((own_a && pass_partner(ob)) || (own_b && pass_partner(oa))) {
            g_pass_pairs++;
            *out = 0;
            return out;
        }
    }
    return o_CollEnabled(self, out, a, b);
}
using HoverFilter_t = bool(SK_CDECL*)(void**);
static HoverFilter_t o_HoverFilter;
static bool SK_CDECL h_HoverFilter(void** hit)
{
    if (g_pass_on && hit && hit[0] && a_AbilityModeCore && skg::cdecl_call<int>(a_AbilityModeCore) != 0) {
        void* owner = *(void**)((uint8_t*)hit[0] + 0x64);
        void* obj = owner ? skg::vcall<void*>(owner, 0xC / 4, 0x17F243Bu) : nullptr; // the hover filter's own cast
        if (pass_partner(obj)) {
            g_pass_hover_skipped++;
            return false; // the next object along the ray (the animal behind the partner)
        }
    }
    return o_HoverFilter(hit);
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

// every synchronized object here (coop.sync, the support sync-check): the players (p<id>: our own
// creature, the partners' puppets) and the stage modules' objects
std::vector<syncdbg::Entry> sync_all()
{
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
    for (auto* f : {npc::sync_entries, tribe::sync_entries, civ::sync_entries, squadron::sync_entries,
                    cellsync::sync_entries})
        for (auto& e : f())
            v.push_back(e);
    return v;
}

// the editor open now (its EditorRequest +0xC, e.g. 96B24187 the spaceship editor), 0 = none: the editor game mode
// is the mode manager's entry with id 0xDBDBA1 (+0x14..+0x18, entries of 0x18 bytes), its request at +0x1CC
// (as coop.editor reads it). The Party panel names a partner's editor by it (2026-11-13).
static uint32_t open_editor_id()
{
    static uintptr_t get = H->sym("App::IGameModeManager::Get");
    void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!mm || !skg::readable((char*)mm + 0x14, 8)) return 0;
    auto* b = skg::field<uint32_t*>(mm, 0x14);
    auto* e = skg::field<uint32_t*>(mm, 0x18);
    void* ed = nullptr;
    for (uint32_t* it = b; it && it < e && e - b < 6 * 64 && skg::readable(it, 8); it += 6)
        if (it[1] == 0xDBDBA1u) ed = (void*)(uintptr_t)it[0];
    void* req = ed && skg::readable((char*)ed + 0x1CC, 4) ? skg::field<void*>(ed, 0x1CC) : nullptr;
    return req && skg::readable(req, 0x10) ? skg::field<uint32_t>(req, 0xC) : 0;
}

static bool g_paused_cover = true;
COOP_FIX("ui.paused_cover", "BUG-9227", g_paused_cover, "ui: guests see a 'Paused by host' cover while the host's game is paused");
// Decision 6T (alpha; owner 2026-11-18: "I think pausing should be disabled when we move to the MMO / Alpha build"): in a
// session nothing pauses the world for everyone. A player's own pause (P 0x4BF38A8, the pause menu 0x4BF38A4 while the
// menu is up) is released as soon as the game counts it, so the world runs on behind the menu; the game's own stops
// (editors, captions, cinematics, its boxes) stay. The host sends no "paused" state, so a guest never stands for the
// host and sees no "Paused by host" cover (BUG-9227 / BUG-9260 stay for live). Off: pausing as on live.
static bool g_no_pause = true;
COOP_FIX("core.no_pause", "BUG-9262", g_no_pause, "alpha: in a session P and the pause menu do not stop the game; a guest never stands for the host");
static bool no_pause_session()
{
    return g_no_pause && g_sess.connected() && (g_sess.role() == net::Role::Client || g_sess.players().size() > 1);
}
bool stage_mode(uint32_t m);
net::PlayerState local_state()
{
    net::PlayerState st;
    st.mode = g_state_mode_override && H->time_seconds() < g_state_mode_until ? g_state_mode_override : g_mode;
    st.world = g_world;
    if (st.mode == modes::kEditor || st.mode == modes::kEdit) st.editor = open_editor_id();
    if (void* local = local_now())
        g_adapter->fill(local, st);
    if (st.mode == modes::kScenario && (st.flags & net::PF_IN_WORLD))
        st.gate = adventure::outgoing();
    if ((st.mode == modes::kCreature || st.mode == modes::kScenario) && creature_rescue::down())
        st.flags |= net::PF_DOWNED;
    { // BUG-9227: the host's game time stands for a second or more in a stage (its pause menu, a caption)
        static double s_paused_since = -1;
        const double now = H->time_seconds();
        const bool paused = g_paused_cover && !no_pause_session() && g_sess.role() == net::Role::Host && stage_mode(st.mode) &&
                            world::time_paused() && !world::cinematic_playing();
        s_paused_since = paused ? (s_paused_since < 0 ? now : s_paused_since) : -1;
        if (paused && now - s_paused_since >= 1.0)
            st.flags |= net::PF_PAUSED;
    }
    if (g_sess.role() == net::Role::Host && world::host_reloading())
        st.flags |= net::PF_RESYNC; // BUG-0332
    return st;
}

// remote `s` is visible here only when both play the same mode in the same world
bool same_place(const net::PlayerState& s) { return s.mode == g_mode && s.world == g_world && g_world != 0; }

// BUG-9260 (owner 2026-11-18: "when Host was paused in Live, the connected player was able to move still, even though
// 'host paused' screen was up"): BUG-9227 only drew the cover; the guest's own game ran on under it. While the host's
// state says paused (PF_PAUSED, the same place, fresh) the guest's game time stands too, with one pause of its own
// (the UI-modal reason the game's own boxes use, 0x4BF38A7: counted, so the guest's own pauses stay as they are), and
// is resumed when the host resumes, leaves the place, goes quiet or the session ends. Off: the cover only.
static bool g_follow_pause = true;
COOP_FIX("core.follow_host_pause", "BUG-9260", g_follow_pause, "ui: a guest's game pauses while the host's game is paused (not only the cover)");
constexpr uint32_t kHostPauseReason = 0x4BF38A7u;
static bool g_host_pause_held;
static uint32_t g_host_pauses;
static void host_pause_tick(double now)
{
    bool want = false;
    if (g_follow_pause && g_paused_cover && g_sess.role() == net::Role::Client && g_sess.connected()) {
        auto it = g_remotes.find(0);
        if (it != g_remotes.end() && !it->second.buf.empty()) {
            const auto& hs = it->second.buf.latest();
            want = (hs.flags & net::PF_PAUSED) && same_place(hs) && it->second.buf.age(now) < 3.0 && stage_mode(g_mode);
        }
    }
    if (want == g_host_pause_held)
        return;
    world::time_pause(kHostPauseReason, want);
    g_host_pause_held = want;
    if (want)
        g_host_pauses++;
    LOG("pause: the host's game is %s: this game %s (BUG-9260)", want ? "paused" : "running again", want ? "pauses" : "resumes");
}
static void host_pause_release() // the session ended, the world changed: never left paused by the host
{
    if (!g_host_pause_held)
        return;
    world::time_pause(kHostPauseReason, false);
    g_host_pause_held = false;
    LOG("pause: the host's pause released (the session or the place ended, BUG-9260)");
}

// decision 6T (above): every 0.2 s in a session's stage, the player's own pause reasons are resumed; a reason the game
// resumes later (the menu closed) would count under 0 and swallow its next pause, so it is paused back to 0 (the
// BUG-1101 net's way)
extern std::atomic<bool> g_menu_up; // the pause menu or the co-op window is up (menu_check_task)
constexpr uint32_t kPauseKey = 0x4BF38A8u, kPauseMenu = 0x4BF38A4u;
static std::map<uint32_t, int> g_np_released;
static uint32_t g_np_releases;
static double g_np_checked;
static void no_pause_tick(double now)
{
    if (now - g_np_checked < 0.2)
        return;
    g_np_checked = now;
    const auto rs = world::pause_reasons();
    auto count = [&](uint32_t r) {
        for (auto& [k, c] : rs)
            if (k == r)
                return c;
        return 0;
    };
    for (auto it = g_np_released.begin(); it != g_np_released.end();) { // the game's own Resume came: back to 0
        const int c = count(it->first), k = c < 0 ? std::min(-c, it->second) : 0;
        for (int i = 0; i < k; i++)
            world::time_pause(it->first, true);
        it->second -= k;
        it = it->second > 0 ? std::next(it) : g_np_released.erase(it);
    }
    if (!no_pause_session() || !stage_mode(g_mode))
        return;
    const bool menu = g_menu_up && !coop_ui::is_open(); // the pause menu (not the stage card, the same reason)
    for (uint32_t r : {kPauseKey, kPauseMenu}) {
        const int c = count(r);
        if (c <= 0 || (r == kPauseMenu && !menu))
            continue;
        for (int i = 0; i < c; i++)
            world::time_pause(r, false);
        g_np_released[r] += c;
        if (g_np_releases++ < 20)
            LOG("pause: %s in a session: the world runs on (decision 6T, BUG-9262)", r == kPauseKey ? "P" : "the pause menu");
    }
}

// ---- coop.track (BUG-0360): what this player sees of its partners, frame by frame. Tests sample the
// partner's puppet (its game position, where its model is drawn, its game speed, its animation, the
// interpolated target) and the own avatar every frame into a ring and read it back with `dump`.
struct TrackSample {
    float t;
    uint8_t who; // remote id, 255 = the own avatar
    uint16_t flags; // state flags of the target (PF_*: 16 bits since PF_RESYNC), the own avatar: 0
    uint16_t tp;   // the puppet's teleports so far (drive::State)
    net::Vec3 p, d, tgt; // p: now (after the driving), d: drawn this frame (before it)
    float gv, rv; // the game's speed of the creature, the remote's reported speed
    uint32_t anim;
    int16_t gait = -1, gait2 = -1; // the animation's gait state (game::gait_state)
    int8_t moving = -1, speed_state = -1;
    int8_t vis = 0; // world_to_screen of its position answered (in front of the camera)
    float sx = 0, sy = 0; // its screen position then
    // BUG-0366: the height over this game's ground (cPlanetModel::GetHeightAt) of the drawn position
    // and of the target (the partner's own position, at its own game's height)
    float hd = 0, ht = 0;
    uint16_t id = 0;      // Tribe (BUG-0365): the member id (who = its owner), 0 in Creature
    // BUG-0414: the partner's animations played on the puppet so far (MSG_ANIM) and the last one's id
    uint32_t ranims = 0, ranim = 0;
    // BUG-0436: its size on the observer's screen, pixels per metre (a point 1 m over it projected), 0 when not known:
    // the game animates a creature far from its camera without its walk gait
    float ppm = 0;
    // BUG-0434: the physics' simple-path inputs (PhysicsManager_Update 0xB4C5D5): the drawn flag +0xF90, the LOD +0xEAC
    int8_t f90 = -1, lod = -1;
    // BUG-1093: the puppet: how far past its newest state the target ran (SnapshotBuffer::last_extrap, s) and that
    // state's age (s past its usual arrival); the own avatar: 0 and the time since its last state was sent (s)
    float ex = 0, age = 0;
    float knee = 0, hz = 0, ow = 0; // BUG-1093: the puppet's fall knee, its median horizon less g_one_way, g_one_way (s)
};
static std::deque<TrackSample> g_track;
// tests (BUG-0367): `coop.track lift <m>`: this game takes the partners' Creature states <m> further from
// the planet's centre (negative: under the ground), as a partner's position off this game's ground
// would be (an extrapolated fall, a ground that differs)
static float g_track_lift = 0;
// tests (BUG-0394): `coop.track novel 1`: this game takes the partners' Creature states without their
// velocity, as a partner carried by an animation reports none (B4212A07: 3-4.6 m/s at velocity 0)
static bool g_track_novel = false;
static void track_lift(net::PlayerState& s, bool novel = true)
{
    if (novel && g_track_novel && g_mode == modes::kCreature)
        s.vel = {0, 0, 0};
    float r = std::sqrt(s.pos.x * s.pos.x + s.pos.y * s.pos.y + s.pos.z * s.pos.z);
    if (g_track_lift == 0 || g_mode != modes::kCreature || r < 1.f)
        return;
    float k = (r + g_track_lift) / r;
    s.pos = {s.pos.x * k, s.pos.y * k, s.pos.z * k};
}
static bool g_track_on = false;
uint32_t g_own_anims = 0, g_own_anim_last = 0; // coop.track (BUG-0414): the own avatar's animations (on_anim)
static double g_track_t0 = 0;
constexpr size_t kTrackMax = 60000;

static TrackSample& track_one(uint8_t who, void* c, const net::PlayerState* s, uint16_t tp, double now,
                              const net::Vec3* drawn = nullptr)
{
    TrackSample x{};
    x.t = (float)(now - g_track_t0);
    x.who = who;
    x.tp = tp;
    x.p = game::position(c);
    game::drawn_state(c, nullptr, &x.anim);
    x.d = drawn ? *drawn : x.p; // the puppet: where the game drew it this frame (before our driving)
    int g = -1, g2 = -1, mv = -1, ss = -1;
    if (game::gait_state(c, &g, &g2, &mv, &ss)) {
        x.gait = (int16_t)g;
        x.gait2 = (int16_t)g2;
        x.moving = (int8_t)mv;
        x.speed_state = (int8_t)ss;
    }
    x.gv = drive::speed(game::velocity(c));
    float w[3] = {x.p.x, x.p.y, x.p.z}, sc[2] = {0, 0};
    x.vis = H->world_to_screen(w, sc) ? 1 : 0;
    x.sx = sc[0];
    x.sy = sc[1];
    {
        float r = std::sqrt(x.p.x * x.p.x + x.p.y * x.p.y + x.p.z * x.p.z), sc2[2] = {0, 0};
        float w2[3] = {x.p.x + x.p.x / r, x.p.y + x.p.y / r, x.p.z + x.p.z / r};
        if (x.vis && r > 1.f && H->world_to_screen(w2, sc2))
            x.ppm = std::sqrt((sc2[0] - sc[0]) * (sc2[0] - sc[0]) + (sc2[1] - sc[1]) * (sc2[1] - sc[1]));
    }
    if (s) {
        x.tgt = s->pos;
        x.rv = drive::speed(s->vel);
        x.flags = s->flags;
    }
    auto over = [](const net::Vec3& v) {
        float g = game::ground_radius(v);
        return g > 0 ? std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) - g : 0.f;
    };
    x.hd = over(x.d);
    x.ht = s ? over(x.tgt) : x.hd;
    if (game::noun_id(c) == 0x18EB45Eu) { // a creature
        x.f90 = (int8_t)*((uint8_t*)c + 0xF90);
        x.lod = (int8_t)*(int32_t*)((uint8_t*)c + 0xEAC);
    }
    while (g_track.size() >= kTrackMax)
        g_track.pop_front();
    g_track.push_back(x);
    return g_track.back();
}

void track_tick(double now)
{
    if (g_track_on && g_mode == modes::kTribe) { // BUG-0365: the player tribe's members (mirrors on guests)
        tribe::for_each_tracked(
            [](const tribe::TrackMember& m, void* u) {
                net::PlayerState s{};
                if (m.tgt) {
                    s.pos = *m.tgt;
                    s.flags = net::PF_IN_WORLD | (m.rv > 0.3f ? net::PF_MOVING : 0);
                }
                TrackSample& x = track_one(m.owner, m.c, m.tgt ? &s : nullptr, m.teleports, *(double*)u, m.drawn);
                x.id = m.id;
                x.rv = m.rv;
                if (!m.tgt) { // the host's own member: its own state is the reference
                    x.tgt = x.p;
                    x.flags = net::PF_IN_WORLD | (m.rv > 0.3f ? net::PF_MOVING : 0);
                }
            },
            &now);
        return;
    }
    if (!g_track_on || g_mode != modes::kCreature || !g_local)
        return;
    {
        TrackSample& x = track_one(255, g_local, nullptr, 0, now);
        x.ranims = g_own_anims; // the own avatar: its animations sent (MSG_ANIM)
        x.ranim = g_own_anim_last;
        x.age = (float)(now - g_last_send); // (with net.send_phase: since the last state was due)
    }
    for (auto& [id, r] : g_remotes) {
        void* pp = puppet_now(r);
        if (!pp || !creature_puppet(r.puppet_adapter) || !game::creature_listed(pp))
            continue;
        net::PlayerState s;
        if (r.buf.sample(now, s) && (track_lift(s, false), true)) // (rv: the speed the partner reported)
        {
            TrackSample& x = track_one((uint8_t)id, pp, &s, (uint16_t)r.drv.teleports, now, g_puppet_hold ? nullptr : &r.drv.drawn);
            x.ranims = r.anims;
            x.ranim = r.anim_last;
            x.ex = (float)r.buf.last_extrap;
            x.age = (float)r.buf.age(now);
            x.knee = (float)r.buf.fall_knee();
            x.hz = (float)r.buf.horizon_med();
            x.ow = (float)net::g_one_way;
        }
    }
}

int c_track(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "count";
    if (!strcmp(a, "start")) {
        g_track.clear();
        g_track_on = true;
        g_track_t0 = H->time_seconds();
    } else if (!strcmp(a, "stop"))
        g_track_on = false;
    else if (!strcmp(a, "lift")) {
        if (argc > 2)
            g_track_lift = (float)atof(argv[2]);
        SAY("lift %.2f\n", g_track_lift);
        return 0;
    } else if (!strcmp(a, "air")) { // tests (BUG-0376): the puppet in the air 0 placed + not grounded, 1 placed, 2 teleported, 3 walked, 4 own fall speed
        if (argc > 2)
            modes::g_puppet_air_variant = atoi(argv[2]);
        SAY("air %d\n%s", modes::g_puppet_air_variant, game::air_status().c_str());
        return 0;
    } else if (!strcmp(a, "novel")) {
        if (argc > 2)
            g_track_novel = atoi(argv[2]) != 0;
        SAY("novel %d\n", (int)g_track_novel);
        return 0;
    }
    else if (!strcmp(a, "dump")) {
        // dump [from] [n]: t who flags tp px py pz dx dy dz tx ty tz gv rv anim gait gait2 moving speed_state vis sx sy
        // hd ht (BUG-0366: heights over the ground) id (BUG-0365: the Tribe member id, 0 in Creature) ranims ranim
        // (BUG-0414: the partner's animations played on the puppet so far, the last one) ppm (BUG-0436: pixels per metre on
        // the screen) f90 lod ex age knee hz ow (BUG-1093: the target's extrapolation, its newest state's age, the
        // extrapolation's fall knee, the median horizon less the one-way latency, the one-way latency); new columns go at the
        // end (parsers take >= the columns they know)
        size_t from = argc > 2 ? (size_t)atol(argv[2]) : 0, n = argc > 3 ? (size_t)atol(argv[3]) : 2000;
        for (size_t i = from; i < g_track.size() && i < from + n; i++) {
            const auto& x = g_track[i];
            SAY("%.4f %u %u %u %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.2f %.2f %08X %d %d %d %d %d %.0f %.0f %.3f %.3f %u %u %08X %.1f %d %d %.3f %.3f %.3f %.3f %.3f\n", x.t,
                x.who, x.flags, x.tp, x.p.x, x.p.y, x.p.z, x.d.x, x.d.y, x.d.z, x.tgt.x, x.tgt.y, x.tgt.z, x.gv, x.rv,
                x.anim, x.gait, x.gait2, x.moving, x.speed_state, x.vis, x.sx, x.sy, x.hd, x.ht, (unsigned)x.id, x.ranims, x.ranim, x.ppm, x.f90, x.lod, x.ex, x.age, x.knee, x.hz, x.ow);
        }
        return 0;
    }
    SAY("track %s samples=%u\n", g_track_on ? "on" : "off", (unsigned)g_track.size());
    return 0;
}

// ---- coop.bodyrec (BUG-0425, M1): every synchronized body (sync_all: players, NPCs, Tribe members, Civ vehicles,
// ships, cells) as this game drew it (read at the start of the mod's frame, before any message or driving), with the
// absolute time (H->time_seconds: QueryPerformanceCounter, one clock for every game on one PC). A test records on
// the host and on a guest and compares the guest's copy with the host's body at the same moment
// (tools/tests/body_trail.py), the game's own positions on both machines, not our bookkeeping.
struct BodyRow {
    double t;
    char key[8];
    net::Vec3 p;
};
static std::deque<BodyRow> g_bodyrec;
static bool g_bodyrec_on = false, g_bodyrec_diag = false;
static std::string g_bodyrec_kinds; // `coop.bodyrec start kinds <chars>`: only keys starting with one of them (Cell: p)
static constexpr size_t kBodyRecMax = 400000;

static void bodyrec_tick(double now)
{
    if (!g_bodyrec_on || !g_sess.connected())
        return;
    for (auto& e : sync_all()) {
        if (!g_bodyrec_kinds.empty() && (e.key.empty() || g_bodyrec_kinds.find(e.key[0]) == std::string::npos))
            continue;
        BodyRow r{};
        r.t = now;
        snprintf(r.key, sizeof r.key, "%s", e.key.c_str());
        r.p = e.pos;
        g_bodyrec.push_back(r);
    }
    if (g_bodyrec_diag) { // (K3 diagnostics: the driving's own targets, `coop.bodyrec start diag`)
        auto row = [](const char* key, const net::Vec3& p, void* u) {
            BodyRow r{};
            r.t = *(double*)u;
            snprintf(r.key, sizeof r.key, "%s", key);
            r.p = p;
            g_bodyrec.push_back(r);
        };
        npc::for_each_diag(row, &now); // q<id>: the NPC proxies' targets (BUG-0363)
        tribe::for_each_diag(row, &now);
        civ::for_each_diag(row, &now);
    }
    while (g_bodyrec.size() > kBodyRecMax)
        g_bodyrec.pop_front();
}

int c_bodyrec(void*, int argc, const char* const* argv, SkOut* out)
{
    const char* a = argc > 1 ? argv[1] : "count";
    if (!strcmp(a, "start")) {
        g_bodyrec.clear();
        g_bodyrec_on = true;
        g_bodyrec_diag = argc > 2 && !strcmp(argv[2], "diag");
        g_bodyrec_kinds = argc > 3 && !strcmp(argv[2], "kinds") ? argv[3] : ""; // tests (BUG-0887): a Cell pool is hundreds of rows a frame
    } else if (!strcmp(a, "stop"))
        g_bodyrec_on = false;
    else if (!strcmp(a, "dump")) { // dump [from] [n]: t key x y z (t: absolute s)
        size_t from = argc > 2 ? (size_t)atol(argv[2]) : 0, n = argc > 3 ? (size_t)atol(argv[3]) : 2000;
        for (size_t i = from; i < g_bodyrec.size() && i < from + n; i++) {
            const auto& x = g_bodyrec[i];
            SAY("%.4f %s %.3f %.3f %.3f\n", x.t, x.key, x.p.x, x.p.y, x.p.z);
        }
        return 0;
    } else if (!strcmp(a, "since")) { // since <t> [n] [step]: rows after absolute time t, one frame per step s
        // (BUG-0609: the divergence detector reads the rows of its last period, the ring index moves as it drops rows)
        double t0 = argc > 2 ? atof(argv[2]) : 0.0, step = argc > 4 ? atof(argv[4]) : 0.0;
        size_t n = argc > 3 ? (size_t)atol(argv[3]) : 2000, said = 0;
        auto it = std::upper_bound(g_bodyrec.begin(), g_bodyrec.end(), t0,
                                   [](double t, const BodyRow& r) { return t < r.t; });
        double kept = -1e300, last = -1e300;
        for (; it != g_bodyrec.end() && said < n; ++it) {
            if (it->t != last) { // a new frame: keep it when step s passed since the last kept one
                if (it->t < kept + step) {
                    double ft = it->t;
                    while (it != g_bodyrec.end() && it->t == ft)
                        ++it;
                    if (it == g_bodyrec.end())
                        break;
                    --it;
                    continue;
                }
                kept = last = it->t;
            }
            SAY("%.4f %s %.3f %.3f %.3f\n", it->t, it->key, it->p.x, it->p.y, it->p.z);
            said++;
        }
        return 0;
    } else if (!strcmp(a, "on")) // on: record without clearing (the detector; a test's `start` clears)
        g_bodyrec_on = true;
    SAY("bodyrec %s samples=%u now=%.4f\n", g_bodyrec_on ? "on" : "off", (unsigned)g_bodyrec.size(), H->time_seconds());
    return 0;
}

// BUG-0513: a partner whose game stops sending (minimized, alt-tabbed full screen, a stall of 17-38 s) was hidden
// after 5 s without a word ("the guest is bugged into the ground"). With core.partner_silent the others are told
// once that the partner is not responding and when it plays again. Off: 0.7.3.6.
static bool g_partner_silent_fix = true;
COOP_FIX("core.partner_silent", "BUG-0513", g_partner_silent_fix, "core: a partner whose game stopped answering (minimized, frozen) is announced, and when it is back");
uint32_t g_silent_notices = 0; // tests
// BUG-0923: a partner's creation record arrives 0.3-0.4 s after its first puppet (at a join) or later (a new body):
// the puppet is rebuilt with that look, and each making told "<name> appeared in the world" (twice at every join on
// the host). Off: every puppet made is told (0.7.11)
static bool g_appeared_once_fix = true;
COOP_FIX("notify.appeared_once", "BUG-0923", g_appeared_once_fix, "core: a partner's puppet rebuilt with its new look is not told as 'appeared in the world' again");
uint32_t g_appeared_told = 0, g_appeared_quiet = 0; // coop.status

static void silent_tick(Remote& r, double now)
{
    if (!g_partner_silent_fix || r.echo || r.buf.empty())
        return;
    double age = r.buf.age(now);
    if (!r.silent_said && age > 5.0 && g_sess.connected()) {
        r.silent_said = true;
        g_silent_notices++;
        feed(r.name + " is not responding (game minimized or frozen?)");
    } else if (r.silent_said && age < 1.0) {
        r.silent_said = false;
        feed(r.name + " is responding again");
    }
}

// BUG-1064: in Cell a partner's pose is in its own world scale (BUG-0519); a level change rescales each world over
// ~3.5 s at its own moment, so the host's view centre for a partner (the tiles kept round it, the cull's active area,
// h_TileActive) sat off the partner's place then (by the two scales' ratio): the host's game culled the cells round
// the partner that its own view keeps (a level down: 37 % of the partner's area against 20 % of the host's own,
// lab 20261113-033642-2). Now the centre is in the host's scale, where the partner's cell is drawn.
// Old (`coop.fix cell.view_centre_scale off`): the pose as it came.
static bool g_fix_view_centre_scale = true;
COOP_FIX("cell.view_centre_scale", "BUG-1064", g_fix_view_centre_scale, "cell: the host keeps the world round a partner at the partner's place in the host's world scale while a level change rescales either world");
static net::Vec3 view_pos(const net::PlayerState& s)
{
    if (!g_fix_view_centre_scale || g_mode != modes::kCell || !(s.scale > 0))
        return s.pos;
    float own = cellsync::scale_now();
    if (!(own > 0) || !std::isfinite(own))
        return s.pos;
    float k = s.scale / own;
    return {s.pos.x * k, s.pos.y * k, s.pos.z * k};
}

void drive_puppet(Remote& r, double now)
{
    net::PlayerState s;
    r.buf.jump_speed = g_mode == modes::kCreature ? modes::puppet_jump_speed() : 0.f; // BUG-0499
    r.buf.interp_fall = g_mode == modes::kCreature; // BUG-0680 (drive.puppet_fall: the Creature puppets' stops)
    if (!r.buf.sample(now, s))
        return;
    silent_tick(r, now);
    // BUG-9160 (owner decision 2D): a partner in an editor of its own stays where it stood, in the game's cocoon
    if (evolve::cocoon_on() && g_mode == modes::kCreature && g_adapter && g_local && r.puppet &&
        r.puppet_adapter == g_adapter && r.puppet_world == g_world && s.world == g_world && r.buf.age(now) < 5.0 &&
        (s.mode == modes::kEdit || s.mode == modes::kEditor) && !(s.flags & net::PF_DEAD)) {
        if (g_sess.role() == net::Role::Host) views::note(r.id, game::position(r.puppet), g_mode, now);
        evolve::puppet_editing(r.id, r.puppet, r.cocoon, now);
        return;
    }
    // dead players are hidden until they respawn
    bool visible = g_adapter && g_local && (s.flags & net::PF_IN_WORLD) && !(s.flags & net::PF_DEAD) &&
                   r.buf.age(now) < 5.0 && (r.echo || same_place(s)) && now >= g_puppet_hide_until;
    // BUG-0280: the host keeps its world simulated around every partner it shows in its own place
    if (visible && g_sess.role() == net::Role::Host)
        views::note(r.id, view_pos(s), g_mode, now);
    else
        views::forget(r.id);
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
        if (!visible) {
            r.relook = false; // gone from our place: its next puppet is a real appearance
            return;
        }
    }
    if (!g_adapter->spawn) // Adventure draws the peer from its network position.
        return;
    if (!r.puppet && g_mode == modes::kScenario && !modes::adventure_body_ready())
        return; // BUG-9113: the intro plays: the body comes with the mission (not "failed to show" each try)
    if (!r.puppet) {
        r.puppet = g_adapter->spawn(s, g_local);
        if (!r.puppet) {
            feed("failed to show " + r.name);
            return;
        }
        r.puppet_adapter = g_adapter;
        r.puppet_world = g_world;
        r.drv = drive::State{};
        bool again = g_appeared_once_fix && r.relook; // BUG-0923: the same partner, rebuilt with its new look
        r.relook = false;
        if (again)
            g_appeared_quiet++;
        else {
            g_appeared_told++;
            feed(r.name + " appeared in the world");
        }
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
    track_lift(s);
    if (r.cocoon.state) { // BUG-9160: back from its editor: out of the cocoon once its new body is there
        evolve::puppet_back(r.id, r.puppet, r.cocoon, now);
        if (r.cocoon.state) return; // still in the cocoon where it stood
    }
    // BUG-9206: the partner's own size (a hatchling, a new body), out of the cocoon only (the cocoon shrinks it)
    if (modes::g_size_sync && g_mode == modes::kCreature && s.captain_scale > 0 &&
        std::fabs(game::scale(r.puppet) - s.captain_scale) > 0.02f * s.captain_scale)
        game::set_scale(r.puppet, s.captain_scale);
    modes::g_drive_extrap = r.buf.last_extrap; // BUG-0388 diagnostics (coop.fleet track)
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
// BUG-0829: the DNA a guest earned while the host was away is in its game already (the bar moved) and goes to the host
// at the away end. A host total sent before the host added it (the 2 s resend, a change of the host's own) arrived after
// it: the guest's DNA went down to it (SetEvolutionPoints, the bar stays) and up again with the next total
// (AddEvolutionPoints: the bar moved a second time, the guest's bar ahead of the host's by that DNA). Now, after the
// away end, a total without the host's "earned by this player" cause is not applied until one with it came (at most
// kSoloAwait s). `coop.fix core.solo_await off`: every total applied (0.7.11).
static bool g_solo_await_fix = true;
COOP_FIX("core.solo_await", "BUG-0829", g_solo_await_fix, "creature: DNA a guest earned while the host was away moves its evolution bar once (older host totals are not applied over it)");
static float g_solo_await = 0;          // the solo DNA sent at the away end, its total not seen yet
static double g_solo_await_until = 0;
static constexpr double kSoloAwait = 5.0;
static uint32_t g_solo_stale_totals = 0; // tests: host totals older than the solo DNA, not applied
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
// BUG-0205: Cell too since cellsync plays the guest's own pool while the host is away (pool.away).
constexpr uint32_t kAwayStages[] = {modes::kCreature, modes::kTribe, modes::kCiv, modes::kCell};
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
    if (!g_away_editor_fix || g_own_editor_due)
        return 0;
    if (g_away_stages_fix && !dna_route::away_stage(g_mode, kAwayStages, (int)std::size(kAwayStages)))
        return 0; // BUG-0233
    if (g_mode == modes::kCell && !cellsync::away_enabled())
        return 0; // BUG-0205 off: as in 0.7.3.1
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
dna_sync::Progress g_dna_pending_progress;
float g_dna_pending = -1; // the host's DNA total that arrived while we were elsewhere
// unknown: what to say before the host's first state arrived (just joined).
// The 0.7.3.0 predicate: kept for the off side of core.world_gate (the gate) only; every module
// asks host_world_now() / guest_elsewhere() / host_absent_for() below.
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

// BUG-0789: an adventure (kScenario) lights its planet itself: the time-of-day object's orientation is computed from
// the clock AND the captain's position (fork RE, docs/re/adventure.md#adventure-ga-fork), so the host's is wrong for
// another captain. The host sends no MSG_WORLD_TIME from an adventure and a guest in one applies none (the wsync sky
// row is not in play in kScenario either). `coop.fix wsync.adventure_sky off` (with wsync.tod off): the legacy
// packet in an adventure as before.
static bool g_adventure_sky_fix = true;
COOP_FIX("wsync.adventure_sky", "BUG-0789", g_adventure_sky_fix, "wsync: in an adventure each game keeps its own sky (no host time of day sent or applied)");
static bool adventure_sky_own(uint32_t mode) { return g_adventure_sky_fix && mode == modes::kScenario; }
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
static bool g_vote_world_fix = true;
COOP_FIX("core.vote_world", "BUG-0420", g_vote_world_fix, "core: a guest in another world gets no vote screen and no editor from the host's vote");
static uint32_t g_vote_elsewhere;
// BUG-0422: on a leave and a join every module registered in player_life.h forgets the id (ids are reused). Off: the
// five modules of 0.7.10 only.
static bool g_player_forget_fix = true;
COOP_FIX("core.player_forget", "BUG-0422", g_player_forget_fix, "core: a player who joins under a departed player's id gets nothing of that player's state");
static lifecycle::WorldWatch g_lifecycle_world;
static void lifecycle_world_tick() // once a frame, after game::stage_epoch_tick
{
    uint32_t sum = 0;
    for (uint32_t m : {modes::kCell, modes::kCreature, modes::kTribe, modes::kCiv, modes::kSpace})
        sum += game::stage_epoch(m);
    if (g_lifecycle_world.changed(sum))
        lifecycle::reset(lifecycle::kWorld, g_lifecycle_fix);
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
static uint32_t g_host_save = 0;       // guest: the host's save heard or seen after WELCOME (0: none yet)
static uint32_t g_world_announced = 0; // host: the world id last sent as MSG_HOST_WORLD
static uint32_t g_host_save_heard = 0; // tests: MSG_HOST_WORLD taken
static uint32_t host_save_known()
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
} // namespace (the anonymous one: own_world is called from the other modules)
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
namespace {

// a message of the host's world state a guest elsewhere must not apply (MSG_RES is held inside
// shared.cpp instead: res.elsewhere). BUG-0420: one class per message type (host_world.h msg_class, a
// build error on an unlisted MsgType); a type this build does not know is dropped too (default deny).
// `coop.fix core.msg_classes off`: unknown types pass as in 0.7.10.
static bool g_msg_classes_fix = true;
COOP_FIX("core.msg_classes", "BUG-0420", g_msg_classes_fix, "core: a guest elsewhere drops every host message of an unknown type (default deny)");
static bool world_state_msg(uint8_t type)
{
    // Lineages world-state messages (host_world.h msg_class must classify them too): the Civ rows with
    // core.world_gate, the Tribe / Cell rows always
    if (type == net::MSG_CIV_TURRETS || type == net::MSG_CIV_ECONOMY || type == net::MSG_CIV_INFLUENCE)
        return g_world_gate_fix;
    if (type == net::MSG_CELL_COMBAT || type == net::MSG_TRIBE_BUILDINGS || type == net::MSG_TRIBE_EQUIPMENT ||
        type == net::MSG_TRIBE_TOOLS || type == net::MSG_TRIBE_RELATIONSHIPS || type == net::MSG_TRIBE_FOOD ||
        type == net::MSG_TRIBE_PERFORMANCE || type == net::MSG_TRIBE_COMBAT)
        return true;
    return host_world::world_state_msg(type, g_msg_classes_fix, g_world_gate_fix);
}

// the gate: does this machine drop the host's message? (0.7.3.0 with core.world_gate off: the old
// predicate, "unknown" counted only for the DNA total)
static bool gate_drops(uint8_t type)
{
    if (!world_state_msg(type))
        return false;
    return g_world_gate_fix ? guest_elsewhere() : host_elsewhere(type == net::MSG_DNA);
}

// BUG-0466: the guest's own spends (see g_dna_spend_fix)
static void dna_spend_sample()
{
    if (!g_dna_spend_fix || g_sess.role() != net::Role::Client || !g_sess.connected()) {
        g_dna_seen_ok = false;
        g_dna_spend = 0;
        return;
    }
    if (game::mode_id() != modes::kCreature)
        return; // an editor, a load: the last value is kept (an editor's price shows when it closes;
                // a world held for the editor's end may load before the avatar is back: no avatar asked)
    uint32_t epoch = game::stage_epoch(modes::kCreature);
    if (guest_elsewhere() || !g_dna_seen_ok || epoch != g_dna_seen_epoch) {
        g_dna_seen = game::dna(); // another world or a load: its value is not a change of ours
        g_dna_seen_ok = !guest_elsewhere();
        g_dna_seen_epoch = epoch;
        return;
    }
    float cur = game::dna(), d = cur - g_dna_seen;
    if (d < -0.5f) {
        if (g_dna_spend == 0)
            g_dna_spend_world = g_stage_world; // (an editor has no world of its own)
        g_dna_spend += d;
        LOG("dna: spent %.0f here (%.0f -> %.0f): goes to the host", -d, g_dna_seen, cur);
    }
    g_dna_seen = cur;
}

// a spend the mod made for the game (the own editor's price): counted at once, not left to the
// sample (a host total can arrive before the next tick: it would undo it)
static void dna_spend_note(float d)
{
    if (!g_dna_spend_fix || g_sess.role() != net::Role::Client || !g_sess.connected() || d >= 0)
        return;
    if (g_dna_spend == 0)
        g_dna_spend_world = g_stage_world; // (an editor has no world of its own)
    g_dna_spend += d;
    if (g_dna_seen_ok)
        g_dna_seen += d;
    LOG("dna: spent %.0f here (the own editor's price): goes to the host", -d);
}

// the game's DNA was set by the mod (a host total): the next sample starts from it
static void dna_spend_resync()
{
    if (g_dna_seen_ok)
        g_dna_seen = game::dna();
}

// BUG-9165: a guest's own DNA (BUG-9162) lives in its game's player; every load of the host's world (a share at a
// stage change, a new home nest, a rejoin) brings the host's budget from the host's save. The guest keeps its own: the
// last value seen in this world (and SporeCoop\worlds.ini [dna], for the next session) is put back once the avatar
// stands in a newly loaded copy of the same world (game::stage_epoch(kCreature) changed; an editor visit keeps the
// epoch, so a spend in the own editor stays spent). The bar is the host's (MSG_DNA) and is kept. Off: the host's budget.
static bool g_keep_own_dna = true;
COOP_FIX("creature.keep_own_dna", "BUG-9165", g_keep_own_dna,
         "creature: a guest's own DNA is put back after the host's world is loaded again (the save holds the host's)");
unsigned game_busy();
bool mode_switch_pending();
// BUG-9223 (owner play test 2026-11-15: "BigTexas went into editor with 14 DNA, added an Eye for 5 DNA, then went back
// ingame, still said 14 DNA"): the put-back took any new Creature epoch for a load, and the own editor of the mating call
// (with its cocoon) raises the epoch too: the DNA from before the editor came back, the spend was free. On: put back only
// after a load of a received copy of the host's world (world::shared_loads), once that copy is open. Off: by the epoch.
static bool g_own_dna_load_fix = true;
COOP_FIX("creature.own_dna_after_load", "BUG-9223", g_own_dna_load_fix,
         "creature: a guest's own DNA is put back only after a load of the host's world (an editor's spend stays spent)");
static uint32_t g_own_dna_loads, g_own_dna_wait_epoch;
static bool g_own_dna_pending;
static float g_own_dna = -1;
static uint32_t g_own_dna_world, g_own_dna_epoch, g_own_dna_restores;
static double g_own_dna_written_at = -100;
static float g_own_dna_written = -1;
static void own_dna_tick(double now)
{
    // recorded while busy too (the cocoon, a cinematic after the own editor): only the put-back waits for a standing avatar
    if (!g_keep_own_dna || !personal_dna() || g_sess.role() != net::Role::Client || !g_world || !game::avatar())
        return;
    const uint32_t epoch = game::stage_epoch(modes::kCreature);
    if (g_own_dna_world != g_world) { // first sight of this world: the note of an earlier session, if any
        g_own_dna = -1;
        const std::string note = creations::world_note("dna", g_world);
        if (!note.empty())
            g_own_dna = (float)atof(note.c_str());
        g_own_dna_world = g_world;
        g_own_dna_epoch = 0; // a loaded world: put the note back
        g_own_dna_written = g_own_dna;
    }
    if (g_own_dna_load_fix) {
        if (world::shared_loads() != g_own_dna_loads) { // a copy of the host's world is loading: put back once it is open
            g_own_dna_loads = world::shared_loads();
            g_own_dna_pending = true;
            g_own_dna_wait_epoch = epoch;
        }
        if (g_own_dna_epoch == 0 && !g_own_dna_pending) { // the first sight of this world (its note)
            g_own_dna_pending = true;
            g_own_dna_wait_epoch = 0xFFFFFFFFu;
        }
        g_own_dna_epoch = epoch;
        if (g_own_dna_pending) {
            if ((g_own_dna_wait_epoch != 0xFFFFFFFFu && epoch == g_own_dna_wait_epoch) || game_busy() || mode_switch_pending())
                return; // the loaded copy is not open yet: nothing recorded from it until the own DNA is back
            if (g_own_dna >= 0 && std::fabs(game::dna() - g_own_dna) > 0.5f) {
                LOG("dna: own DNA %.0f put back after a load of the world (the save had %.0f, BUG-9165)", g_own_dna, game::dna());
                game::dna_sync_raw(g_own_dna, game::evolution_bar());
                g_dna_seen = g_own_dna;
                ++g_own_dna_restores;
            }
            g_own_dna_pending = false;
        }
    } else if (epoch != g_own_dna_epoch) {
        if (game_busy() || mode_switch_pending())
            return; // a new copy of the world: nothing recorded from it until the own DNA is back
        if (g_own_dna >= 0 && std::fabs(game::dna() - g_own_dna) > 0.5f) {
            LOG("dna: own DNA %.0f put back after a load of the world (the save had %.0f, BUG-9165)", g_own_dna, game::dna());
            game::dna_sync_raw(g_own_dna, game::evolution_bar());
            g_dna_seen = g_own_dna;
            ++g_own_dna_restores;
        }
        g_own_dna_epoch = epoch;
    }
    g_own_dna = game::dna();
    if (std::fabs(g_own_dna - g_own_dna_written) > 0.5f && now - g_own_dna_written_at > 5) {
        char b[32];
        snprintf(b, sizeof b, "%.0f", g_own_dna);
        creations::world_note_set("dna", g_world, b);
        g_own_dna_written = g_own_dna;
        g_own_dna_written_at = now;
    }
}

static void dna_spend_tick(double now)
{
    static double last_send = -100;
    if (personal_dna()) { g_dna_spend = 0; return; } // BUG-9162: a spend is the player's own
    dna_spend_sample();
    if (g_dna_spend > -0.5f)
        return;
    if (g_dna_spend_world != g_world && g_mode == modes::kCreature && game::avatar()) {
        LOG("dna: a spend of %.0f made in world %08X dropped (now %08X)", -g_dna_spend, g_dna_spend_world, g_world);
        g_dna_spend = 0; // that world is gone (another save): its DNA has no total to come off
        return;
    }
    if (g_host_away || !host_here() || guest_elsewhere() || now - last_send < 0.25)
        return; // held: the host has no creature now (its own editor) or plays elsewhere
    net::Writer w;
    w.f32(g_dna_spend);
    g_sess.send_reliable_to(0, net::MSG_DNA_ADD, w);
    g_dna_spends_sent++;
    last_send = now;
    g_dna_spend = 0;
}

void host_away_tick(double now)
{
    if (stage_mode(g_mode) && g_world)
        g_stage_world = g_world;
    if (g_dna_owed > 0 && g_sess.role() == net::Role::Host) {
        auto o = dna_route::owed(g_away_dna_stage_fix, g_mode, stage_mode(g_mode), game::avatar() != nullptr, g_world,
                                 g_dna_owed_world, modes::kCreature);
        if (o == dna_route::Owed::Pay) {
            float left = progress_debt::absorb(g_dna_owed); // BUG-9119: a progress debt first
            if (left > 0) {
                dna_host_gain({dna_ledger::Cause::Owed, 0, left}, game::dna());
                game::dna_add_raw(left);
            }
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
        game::dna_sync_raw(g_dna_pending, g_dna_pending_progress.valid(g_world) ? g_dna_pending_progress.earned : NAN);
        g_dna_pending = -1;
        g_dna_pending_progress = {};
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
            // BUG-0332: the host's one reload of the new Tribe world (tribe.host_reload)
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
    bool arrival = false; // host (BUG-0315): the first Creature editor after a passed switch vote: a guest that arrived is ready
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
CALL_GUARD("coop.vote_bypass", g_vote_bypass); // BUG-1047
bool g_vote_opened_ui = false; // the vote opened the co-op window: it closes again afterwards
double g_vote_last_send = 0;
using Submit_t = bool(SK_CDECL*)(void*);
Submit_t o_Submit;
void broadcast_vote();
void set_ready(bool on);

std::function<void()> g_vote_action; // a vote for something else than an editor (a superweapon ...)
std::string g_vote_done = "evolution starts";

// BUG-0315 (docs/09-coop-design.md 9.3): the host's click on History's check at the end of the Cell stage started the
// Creature stage at once (0xE62440, SporeCoop::CellStage_ToCreature) and the guests were pulled after it; only the first
// Creature editor waited for a vote. On: in a session the host's switch waits for a vote ("wants to move on to the
// Creature stage"); when it passes, the game's own switch runs; the first Creature editor's vote that follows counts
// every guest that has arrived in the host's Creature world as ready (they voted already). A guest's own click on the
// check in the host's Cell world does not take it to Creature alone: during the vote it is its "I'm ready". Off: as
// in 0.7.10. A session setting later (docs/plan-0.7.12.md F3).
bool g_switch_vote_fix = true;
COOP_FIX("core.switch_vote", "BUG-0315", g_switch_vote_fix, "core: the host's Cell -> Creature switch (History's check) waits for a vote of everybody");
double g_switch_passed_at = -1000; // host: the switch vote passed (the arrival vote follows within 300 s)
unsigned g_switch_votes, g_switch_held, g_switch_guest_held, g_switch_arrivals; // tests

// BUG-0516: the host's evolution in the Creature stage (the mating call's request, tag +0x90 0x64AA168) waited for
// every guest's ready (the vote, at most 60 s) although no world reload follows it since BUG-0508. On: the host's
// editor opens at once (no vote); every guest in the host's world gets an offer for 90 s: the co-op window opens with
// "Evolve now" (the ready button), which opens the guest's own editor when its stage has settled (own_editor_tick, as
// after a vote); a guest that does not take it plays on (the host in an editor counts as away, BUG-0205) and can evolve
// at its nest later. Stage starts (the first Creature editor, Creature -> Tribe), Cell and the design stages keep the
// vote. Off: the vote as in 0.7.10. A session setting later (docs/plan-0.7.12.md F3).
bool g_editor_now_fix = true;
// F5 (BUG-0769): the session setting creature.mode = host (everybody plays the host's creature) is in force here
bool host_creature_mode() { return settings::get(settings::kCreatureMode) == 1; }
uint32_t g_one_evolutions = 0, g_one_refused = 0; // its host evolutions / refused guest editor requests (coop.status)
// BUG-0920: an editor request the mod took and will never submit (evo_flag_tick clears the game's flag at once)
bool g_evo_flag_refused = false;
COOP_FIX("core.editor_now", "BUG-0516", g_editor_now_fix, "core: the host's Creature evolution editor opens at once; each guest gets an Evolve now offer instead of the ready vote");
// BUG-1085: core.editor_now keyed on the request's tag +0x90 == 0x64AA168, taken for the mating call's. It is the
// stage-end editor's (CreatureGame_HandleMessage 0xD42E50 on 0x4471C60 with +0xDE set, which only the Timeline's evolve
// button sets: Creature -> Tribe, docs/re/creature.md#creature-mating-call); the mating editor is 0xD3C650 with tag
// 0x12191CA (also the tag of the first Creature editor after Cell, 0xFD4902BD, which BUG-0315's arrival vote owns). So
// Creature -> Tribe went on without the vote (the guests got a pointless "Evolve now") and every mating evolution waited
// for it, the reverse of docs/09-coop-design.md 9.3. On: the mating request (0x12191CA, not the landfall editor, not
// right after the switch vote) opens at once with offers; the stage-end request (0x64AA168) is a stage start: the host's
// waits for the vote "wants to move on to the Tribe stage", a guest's in the host's Creature world is refused (its I'm
// ready during that vote) and never takes that guest to Tribe alone. Off: as 0.7.13 (keyed on 0x64AA168, a guest's
// stage-end request reaches its game).
static bool g_editor_tag_fix = true;
COOP_FIX("core.editor_now_tag", "BUG-1085", g_editor_tag_fix, "core: Creature: the mating evolution opens at once with offers, the stage-end editor (Creature -> Tribe) waits for the vote and a guest cannot start it alone");
constexpr uint32_t kTagMating = 0x12191CAu, kTagStageEnd = 0x64AA168u, kEditorLandfall = 0xFD4902BDu;
unsigned g_stage_end_votes, g_stage_end_guest_refused; // tests (coop.vote)
// Lineages: set while submit_evolution_request submits (BUG-9163 migration, BUG-9164 join, BUG-9175 mating): its
// request carries 0x64AA168 and is an evolution, not the stage's end (h_Submit's BUG-1085 gate)
static bool g_lineages_evo_req = false;
CALL_GUARD("core.lineages_evo_req", g_lineages_evo_req);
constexpr uint8_t kVoteOffer = 1; // MSG_VOTE flags: an evolve offer, not a vote (sent with active = 0: older guests ignore it)
constexpr double kOfferSeconds = 90.0;
uint32_t g_offer_editor = 0;   // guest: the editor of the host's offer (0 = none)
double g_offer_until = 0;
bool g_offer_opened_ui = false;
unsigned g_offers_sent, g_offers_got, g_offers_taken, g_offers_expired; // tests
extern double g_own_editor_due_at, g_own_editor_due_first;
void broadcast_offer(uint32_t editor, const std::string& text);
void offer_end(const char* why);

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
    if (g_vote.mode == modes::kTribe && g_vote.editor == 0x156276D1) tribe::begin_outfit_vote();
    // BUG-0315: the first Creature editor right after a passed switch vote: everybody voted for it already
    g_vote.arrival = g_switch_vote_fix && settings::on(settings::kSwitchVote) && req && g_vote.mode == modes::kCreature &&
                     H->time_seconds() - g_switch_passed_at < 300.0;
    if (g_vote.arrival)
        g_switch_passed_at = -1000;
    trace::line("vote", "start '%s' editor=%s %s players=%d%s", text.c_str(), trace::hex(g_vote.editor).c_str(),
                modes::mode_name(g_vote.mode), (int)g_sess.players().size(),
                g_vote.arrival ? " (after the switch vote: a guest that arrived is ready)" : "");
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
    if (id == 0x60B4123u)
        social::g_stage_msg_win++; // BUG-0179 (tests)
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
static bool g_own_editor_charge_due = false; // BUG-0466: the own editor opened; its result charges once
// BUG-0811: a refused Submit cleared g_own_editor but left the price due (the next editor result without the evolution
// tag was charged as the own editor's), and the session end reset neither (the next editor of a later session waived the
// part-list error). The game's own Submit (0x5A92C0) always returns 1: only the mod's hook refuses (its test capture),
// so this is hardening. `coop.fix creations.own_editor_flags off`: as 0.7.11.
static bool g_own_editor_flags_fix = true;
COOP_FIX("creations.own_editor_flags", "BUG-0811", g_own_editor_flags_fix, "creature: a refused own editor request or the session end leaves no own-editor price or part waiver for a later editor");
using BlockingErrors_t = uint32_t*(SK_THISCALL*)(void*, uint32_t*, void*, void*, int);
BlockingErrors_t o_BlockingErrors;
unsigned g_part_list_waivers; // tests: how often the part-list error was waived
uint32_t* SK_THISCALL h_BlockingErrors(void* ed, uint32_t* out, void* res, void* model, int flag)
{
    uint32_t* r = o_BlockingErrors(ed, out, res, model, flag);
    void* req = ed && out && (out[0] & 0x20u) ? skg::field<void*>(ed,0x1CC) : nullptr;
    const bool own_captain = req && skg::readable(req,0x20) &&
        skg::field<uint32_t>(req,0xC) == 0xA56567F7 &&
        creations::personal_captain_outfit(skg::field<uint32_t>(req,0x10));
    if ((g_own_editor || own_captain) && out && (out[0] & 0x20u)) {
        out[0] &= ~0x20u;
        g_part_list_waivers++;
    }
    return r;
}

// BUG-0368: the creature stage's own evolution request (0xD42F2C) gives the editor the parts the
// species has collected (cPlayer +0x10E8, a cCollectableItems, at request +0x94); the palette shows a
// part locked only with that object (0x5C6820: null = every part available). The guest's own editor
// had none, so it offered every part. Off: the request without it (0.7.3.3).
bool g_own_editor_items_fix = true;
COOP_FIX("creations.own_editor_items", "BUG-0368", g_own_editor_items_fix, "creations: a guest's own creature editor locks the parts the species has not collected");
unsigned g_own_editor_items_set; // tests: own editor requests that got the collected parts
bool g_tribal_outfitter_fix = true;
COOP_FIX("tribe.own_outfitter", "BUG-9043", g_tribal_outfitter_fix,
         "tribe: open the guest species outfitter with the shared native clothing unlocks");
constexpr uint32_t kTribalOutfitter = 0x156276D1;

bool own_tribal_outfitter(uint32_t editor)
{
    return g_tribal_outfitter_fix && editor == kTribalOutfitter &&
           game::mode_id() == modes::kTribe && tribe::village_orders();
}

// the cPlayer's collected creature parts (cCollectableItems), or null
void* collected_parts()
{
    uintptr_t nouns = H->sym("Simulator::cGameNounManager::Get"), getp = H->sym("SporeCoop::cGameNounManager_GetPlayer");
    void* nm = nouns ? skg::cdecl_call<void*>(nouns) : nullptr;
    void* pl = nm && getp ? skg::thiscall<void*>(getp, nm) : nullptr;
    if (!pl || !skg::readable((char*)pl + 0x10E8, 4))
        return nullptr;
    return skg::field<void*>(pl, 0x10E8);
}

// BUG-0720 (class BUG-0431): every EditorRequest the mod builds comes from build_editor_request(), the
// fields the creature stage's own evolution request sets (CreatureGame_HandleMessage 0xD42E50, message
// 0x4471C60, docs/re/creations.md#creations-editor-request-builder): +0xC the editor, +0x10..+0x18 the
// species, +0x94 the collected parts (BUG-0368), the flags +0x34/+0x36/+0x37/+0x38/+0x39/+0x3B = 0 and
// +0x3C/+0x3D = 1 (the constructor 0x5A9140 leaves +0x39, +0x3B at 1 and +0x3C at 0), +0x70 the four
// consequence traits cPlayer +0x10FC..+0x1108, and +0x90 the evolution tag only for kEvolve. What the game
// does and the mod leaves out on purpose (the allow-list of the test, coop.editreq diff): +0x90 for the
// own editor (its price is charged by BUG-0466; with the tag the creature game applies an evolution of the
// world's species), the message 0x5D02A72 after Submit and the byte 0x1686AF0. Before (the switch off):
// open_own_editor and coop.vote editor set only +0xC, the key and +0x94.
static bool g_editor_request_fix = true;
COOP_FIX("creations.editor_request", "BUG-0720", g_editor_request_fix, "creature: the editor requests the mod builds (a guest's own editor, the test commands) carry the fields of the game's own evolution request (flags, consequence traits)");
enum class EditKind { Own, Edit, Evolve };
// BUG-0756: the guest's own editor keeps the constructor's +0x39 = 1, +0x3B = 1, +0x3C = 0 (the edit-a-creation editor of
// 0.7.10: its red cross and the Save New / Replace / Don't Save dialog); the game's evolution values (0, 0, 1) gave
// it the evolution editor's UI, with no way to leave without the edit. cEditor::OnEnter (0x58FB8D..0x58FBEF) passes
// +0x39 to 0x5DCDC0 (windows 0x682102A/B hidden when 1, 0x55FDCA8 / 0x47BC958 enabled) and +0x3B to 0x5DCC80
// (window 0x90439D7C, the red cross, visible); +0x3C goes to 0x5BF1E0 with the species key. The rest of BUG-0720
// (the other flags, the consequence traits, +0x94) stays the game's. `coop.fix creations.own_editor_cancel off`:
// the evolution values (no cross). docs/re/creations.md#creations-own-editor-cancel
static bool g_own_editor_cancel_fix = true;
COOP_FIX("creations.own_editor_cancel", "BUG-0756", g_own_editor_cancel_fix, "creature: a guest's own editor has its red cross and the save dialog (the request's +0x39 / +0x3B / +0x3C as the constructor's)");
static unsigned g_own_keep_ctor = 0; // tests (coop.editreq keep): own-editor bytes left at the constructor's value (1 +0x39, 2 +0x3B, 4 +0x3C, 16 vec70)
static void* the_player()
{
    uintptr_t nouns = H->sym("Simulator::cGameNounManager::Get"), getp = H->sym("SporeCoop::cGameNounManager_GetPlayer");
    void* nm = nouns ? skg::cdecl_call<void*>(nouns) : nullptr;
    void* pl = nm && getp ? skg::thiscall<void*>(getp, nm) : nullptr;
    return pl && skg::readable((char*)pl + 0x10E8, 0x24) ? pl : nullptr;
}
// a new request (one reference held by the caller, as the game's new + ctor give it), or null
static void* build_editor_request(EditKind kind, uint32_t editor_id, const game::Key& k)
{
    uintptr_t ctor = H->sym("EditorRequest_ctor"), alloc = H->sym("GeneralAllocator::new_");
    if (!ctor || !alloc)
        return nullptr;
    // the game's operator new (GeneralAllocator::new_: size, name, 0, 0, 0, 0), as at 0xD42F1E
    void* req = skg::cdecl_call<void*>(alloc, (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
    if (!req)
        return nullptr;
    skg::thiscall<void*>(ctor, req);
    skg::field<uint32_t>(req, 0xC) = editor_id ? editor_id : 0x465C50BAu; // CreatureEditorExtraLarge
    skg::field<uint32_t>(req, 0x10) = k.instance;
    skg::field<uint32_t>(req, 0x14) = k.type;
    skg::field<uint32_t>(req, 0x18) = k.group;
    bool creature = game::mode_id() == modes::kCreature;
    uintptr_t assign = H->sym("SporeCoop::IntrusivePtr_Assign"), push = H->sym("SporeCoop::VectorU32_PushBack");
    void* pl = the_player();
    if (creature && (kind == EditKind::Evolve || (kind == EditKind::Own && g_own_editor_items_fix))) {
        // the native request's intrusive pointer assignment (AddRef the new, Release the old)
        if (void* items = collected_parts(); items && assign) {
            skg::thiscall<void*>(assign, (char*)req + 0x94, items);
            if (kind == EditKind::Own)
                g_own_editor_items_set++;
        }
    }
    if (kind == EditKind::Evolve)
        skg::field<uint32_t>(req, 0x90) = 0x64AA168u;
    if (creature && (g_editor_request_fix || kind == EditKind::Evolve)) {
        for (int o : {0x34, 0x36, 0x37, 0x38, 0x39, 0x3B})
            skg::field<uint8_t>(req, o) = 0;
        skg::field<uint8_t>(req, 0x3C) = 1;
        skg::field<uint8_t>(req, 0x3D) = 1;
        if (kind == EditKind::Own) {
            unsigned keep = g_own_keep_ctor | (g_own_editor_cancel_fix ? 7u : 0u);
            if (keep & 1) skg::field<uint8_t>(req, 0x39) = 1;
            if (keep & 2) skg::field<uint8_t>(req, 0x3B) = 1;
            if (keep & 4) skg::field<uint8_t>(req, 0x3C) = 0;
        }
        for (int o = 0x10FC; pl && push && o <= 0x1108 && !(kind == EditKind::Own && (g_own_keep_ctor & 16)); o += 4) {
            uint32_t v = skg::field<uint32_t>(pl, o);
            skg::thiscall<void*>(push, (char*)req + 0x70, &v);
        }
    }
    return req;
}

// the creature editor for the own creature (EditorRequest + Submit, like ModAPI mods do); BUG-9043 (Lineages): in
// Tribe the guest species outfitter, with TribalGame's clothing collection
bool open_own_editor(uint32_t editor_id)
{
    uintptr_t submit = H->sym("Editors::EditorRequest::Submit");
    void* av = game::avatar();
    bool tribal = own_tribal_outfitter(editor_id);
    game::Key k;
    std::wstring outfit_name;
    void* tribal_items = nullptr;
    if (tribal) {
        auto own = creations::mine(creations::kCreature);
        k = {own.key.instance, own.key.type, own.key.group};
        outfit_name = slotfs::widen(own.name);
        uintptr_t get = H->sym("SporeCoop::TribalGame_Get");
        void* tg = get ? skg::cdecl_call<void*>(get) : nullptr;
        if (tg && skg::readable((char*)tg + 0x19C, 4)) tribal_items = skg::field<void*>(tg, 0x19C);
        if (!k.instance || !tribal_items || !H->sym("SporeCoop::IntrusivePtr_Assign") ||
            !H->sym("SporeCoop::UIntVector_PushBack") || !H->sym("wstring_copy") || outfit_name.empty()) return false;
    } else if (av) k = game::species_key(av);
    if (!submit || !k.instance)
        return false;
    void* req = build_editor_request(EditKind::Own, editor_id, k);
    if (!req)
        return false;
    if (tribal) {
        // Native planner D13280: outfits keep their species and use TribalGame's
        // clothing collection, not the Creature-stage body-part collection.
        for (size_t off : {0x34u, 0x36u, 0x37u, 0x38u, 0x39u, 0x3Du, 0x64u})
            skg::field<uint8_t>(req, off) = 0;
        skg::field<uint8_t>(req, 0x35) = 1;
        skg::field<uint8_t>(req, 0x3C) = 1;
        skg::field<uint8_t>(req, 0x6D) = 1;
        skg::thiscall<void>(H->sym("wstring_copy"), (char*)req + 0x40,
                            outfit_name.data(), outfit_name.data() + outfit_name.size());
        skg::thiscall<void*>(H->sym("SporeCoop::IntrusivePtr_Assign"), (char*)req + 0x94, tribal_items);
        void* player = the_player();
        if (player && skg::readable((char*)player + 0x10FC, 20))
            for (size_t off = 0x10FC; off <= 0x110C; off += 4)
                skg::thiscall<void>(H->sym("SporeCoop::UIntVector_PushBack"), (char*)req + 0x70, (char*)player + off);
    }
    bool r{};
    {
        callguard::Hold vote_bypass(g_vote_bypass);
        g_own_editor = true; // the editor loads the creature after Submit, on a later frame
        if (tribal) creations::begin_tribal_outfit();
        g_own_editor_charge_due = true; // BUG-0466: its result charges the price
        r = skg::cdecl_call<bool>(submit, req);
    }
    if (!r) {
        g_own_editor = false;
        if (g_own_editor_flags_fix)
            g_own_editor_charge_due = false; // BUG-0811: no editor, no price
        if (tribal) tribe::finish_own_outfit(false);
    }
    return r;
}

// BUG-9146: the Creature / Adventure knockout and revive with three or four players (any standing player revives);
// off: a duo only, as before (with more players a lethal hit was a native death)
static bool g_rescue_party_fix = true;
COOP_FIX("creature.rescue_party", "BUG-9146", g_rescue_party_fix,
         "creature / adventure: a downed player can be revived by any partner with three or four players (off: a duo only)");

// BUG-9135 roadmap phase 4 (decision 4): a late joiner in Tribe without a creature of their own makes a new one in the
// creature editor (CreatureEditorExtraLarge 0x465C50BA, no creation key), opened from the Co-op window. Back in this
// Tribe world the newest own creation it saved is their creature (creations::begin_new_creature) and the host founds
// their village with it. Not yet run in a game (the editor without a creation key from Tribe, the host's checkpoints
// while a guest edits): off by default, docs/re/tribe.md#tribe-late-joiners.
// BUG-9159 (owner decision 2M, 2026-11-12): on; tribe.late_editor_first opens it by itself on joining.
static bool g_late_create_editor = true;
COOP_FIX("tribe.late_create_editor", "BUG-9135", g_late_create_editor,
         "tribe: a partner without a creature of their own can make a new one in the creature editor");
unsigned game_busy();
bool mode_switch_pending();
// BUG-9170 (decision 1P / 14, Civ: "1A: the creature editor first"): a Civ late joiner without a creature of their own
// (civ::late_prompt 1: no nation, no own creature; the host founds no capital for it, LateWhy::NoOwnCreature) goes into
// the creature editor 15 s after the world, as in Tribe; what it saves is its creature (creations adopts it in Civ too)
// and the host founds its capital, behind the loading screen (civ::late_prompt 2). Off: the Co-op window's choice only.
static bool g_civ_editor_first = true;
COOP_FIX("civ.late_editor_first", "BUG-9170", g_civ_editor_first,
         "civ: a late joiner without a creature of their own goes into the creature editor first; their capital follows");
static bool late_create_offered()
{
    if (g_late_create_editor && g_sess.role() == net::Role::Client && g_mode == modes::kCiv && g_civ_editor_first)
        return civ::late_prompt() == 1;
    return g_late_create_editor && g_sess.role() == net::Role::Client && g_mode == modes::kTribe &&
           (tribe::late_prompt() != 0 || tribe::late_needs_creature()); // BUG-9159: before the window's prompt too
}
// the creature editor with the base creature (CreatureEditorExtraLarge 0x465C50BA, no creation key); outside the frame
// decision 3Z B: the Creature stage's own start editor (the galaxy wizard's "Create Your Own", the first creature out
// of the sea: legs and hands only, a few parts, a small budget), as the game asks for it from the galaxy menu (the
// host's request logged 2026-11-14: editor 0xE46C381E, return mode +0x1C the galaxy menu, the tag +0x90 0x13C5A95E)
constexpr uint32_t kCreatureStartEditor = 0xE46C381Eu;
// The start editor's palette comes from its request's parts list (+0x94, a cCollectableItems the wizard makes): the
// diet card leaves its mouth unlocked there (owner 2026-11-15: "Clicking one of those unlocked the corresponding
// mouth"); with no list the editor offers no mouth at all. The list's part states (its state map +0x4D00: buckets
// +0x4D04, their count +0x4D08, nodes {u32 lo, u32 hi, u8 state, ..., next +0x10}; bit 0 unlocked, 4 seen; read live
// 2026-11-15: the herbivore card left 32AC9620 / FE534766 seen) go from the host to the guests (MSG_START_PARTS); a
// guest makes its own list the game's way (0x5BF2E0: new 0x6DAC, the constructor) with the same states.
struct StartPart {
    uint32_t lo, hi;
    uint8_t state;
};
static std::vector<StartPart> g_start_parts; // guest: the host's, received
// BUG-9263 (owner 2026-11-19: "We can't let them make a level 5 endgame creature upon joining any player"): a guest's
// first creature for a world it has no creature in (BUG-9208), and a guest's editor while the host makes a new world
// without the host's start list, opened the Creature Creator: 2,000 DNA and every part, kept as the guest's creature
// (BUG-9259). Now it is always the Creature stage's start editor: its 145 budget and its palette (legs, the three diet
// mouths, eyes, the cell-era parts), with the host's start list (3Z B, the host's diet) when there is one. A host in
// the Creator gives its guests no editor (the late path follows in its world). Off: the Creator as before.
static bool g_join_start_editor = true;
COOP_FIX("creature.join_start_editor", "BUG-9263", g_join_start_editor,
         "creature: a joining guest's first creature is made in the stage's start editor (145 DNA, its palette), never the 2,000 DNA Creator");
// BUG-9265 (the alpha's half of BUG-9263): a galaxy planet's new host without a creature of its own there
// (gx.hosted_stored) got the Creator too. It is the start editor as well, opened inside the running world: there it
// shows the world's DNA and the start palette, and its request must not name the galaxy menu as the mode to go back to
// (the wizard's value, right from the menu). Off: the Creator as before.
static bool g_host_start_editor = true;
COOP_FIX("creature.host_start_editor", "BUG-9265", g_host_start_editor,
         "creature: a galaxy planet's new host makes its first creature in the stage's start editor (its small palette), never the 2,000 DNA Creator");
static std::vector<StartPart> g_host_start_parts; // host: ours, while in the start editor
static std::set<uint8_t> g_host_start_sent;      // host: the players that have them
static bool g_test_unlock_start = false;
COOP_FIX("creature.test_unlock_start", "BUG-9207", g_test_unlock_start,
         "tests: the game's own new-world start editor gets every part of its parts list unlocked");
static double g_start_parts_at = -1;
static std::vector<StartPart> item_states(void* items)
{
    std::vector<StartPart> out;
    if (!items || !skg::readable((char*)items + 0x4D00, 0x10)) return out;
    void** buckets = skg::field<void**>(items, 0x4D04);
    const uint32_t n = skg::field<uint32_t>(items, 0x4D08);
    if (!buckets || n == 0 || n > 4096 || !skg::readable(buckets, n * 4)) return out;
    for (uint32_t i = 0; i < n && out.size() < 2000; i++)
        for (void* node = buckets[i]; node && skg::readable(node, 0x14) && out.size() < 2000;
             node = skg::field<void*>(node, 0x10))
            out.push_back({skg::field<uint32_t>(node, 0), skg::field<uint32_t>(node, 4), skg::field<uint8_t>(node, 8)});
    return out;
}
static void* make_items(const std::vector<StartPart>& states)
{
    uintptr_t ctor = H->sym("SporeCoop::CollectableItems_ctor"), unlock = H->sym("Simulator::cCollectableItems::UnlockPart"),
              lock = H->sym("Simulator::cCollectableItems::LockPart");
    if (!ctor || !unlock || !lock) return nullptr;
    void* items = skg::cdecl_call<void*>(H->sym("GeneralAllocator::new_"), (size_t)0x6DAC, "cCollectableItems", 0, 0, 0, 0);
    if (!items) return nullptr;
    skg::thiscall<void*>(ctor, items);
    for (const auto& p : states) {
        if (p.state & 1) skg::thiscall<bool>(unlock, items, p.lo, p.hi, 0);
        else skg::thiscall<bool>(lock, items, p.lo, p.hi);
    }
    // the exact state bytes (bit 2 "seen": a locked mouth of another diet shows its diet's silhouette, not a shield,
    // as on the host, 2026-11-15)
    void** buckets = skg::field<void**>(items, 0x4D04);
    const uint32_t n = skg::field<uint32_t>(items, 0x4D08);
    if (buckets && n && n <= 4096 && skg::readable(buckets, n * 4))
        for (uint32_t i = 0; i < n; i++)
            for (void* node = buckets[i]; node && skg::readable(node, 0x14); node = skg::field<void*>(node, 0x10))
                for (const auto& p : states)
                    if (p.lo == skg::field<uint32_t>(node, 0) && p.hi == skg::field<uint32_t>(node, 4))
                        skg::field<uint8_t>(node, 8) = p.state;
    return items;
}
static bool submit_new_creature_request(uint32_t editor = 0x465C50BAu, const std::vector<StartPart>* parts = nullptr)
{
    uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit");
    if (!ctor || !submit) return false;
    void* req = skg::cdecl_call<void*>(H->sym("GeneralAllocator::new_"), (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
    if (!req) return false;
    skg::thiscall<void*>(ctor, req);
    skg::field<uint32_t>(req, 0xC) = 0x465C50BAu; // CreatureEditorExtraLarge
    skg::field<uint32_t>(req, 0x10) = 0;          // no creation: a new creature
    skg::field<uint32_t>(req, 0x14) = 0;
    skg::field<uint32_t>(req, 0x18) = 0;
    if (editor == kCreatureStartEditor) {
        std::string hex; // (the constructor's defaults, against the game's own request)
        char b[12];
        for (int o = 0x1C; o < 0x9C; o += 4) {
            snprintf(b, sizeof b, "%08X ", skg::field<uint32_t>(req, o));
            hex += b;
        }
        LOG("start editor request: the constructor's +0x1C..: %s", hex.c_str());
        skg::field<uint32_t>(req, 0xC) = kCreatureStartEditor;
        if (game::mode_id() == modes::kMenu) // (BUG-9265: opened in a running world it goes back to that world)
            skg::field<uint32_t>(req, 0x1C) = modes::kMenu;
        skg::field<uint32_t>(req, 0x20) = 0x02140700u;
        skg::field<uint32_t>(req, 0x24) = 0x0C3645FFu;
        const uint8_t flags[12] = {0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0}; // +0x34..+0x3F as the game's
        for (int i = 0; i < 12; i++) skg::field<uint8_t>(req, 0x34 + i) = flags[i];
        skg::field<uint32_t>(req, 0x64) = 1;
        skg::field<uint32_t>(req, 0x6C) = 0x00010100u;
        skg::field<uint32_t>(req, 0x90) = 0x13C5A95Eu;
        uintptr_t assign = H->sym("SporeCoop::IntrusivePtr_Assign");
        if (parts && assign)
            if (void* items = make_items(*parts)) {
                skg::thiscall<void*>(assign, (char*)req + 0x94, items); // (the request holds it)
                int unlocked = 0;
                for (const auto& p : *parts) unlocked += p.state & 1;
                LOG("start editor: our parts list from %zu part states (%d unlocked) (3Z B)", parts->size(), unlocked);
            }
    }
    g_vote_bypass = true; // the player's own editor, no vote
    g_own_editor = true;  // the part-list error is waived as in the own editor (h_BlockingErrors)
    const bool r = skg::cdecl_call<bool>(submit, req);
    g_vote_bypass = false;
    if (!r) g_own_editor = false;
    return r;
}
// outside the frame (editors create D3D resources)
static std::string open_new_creature_editor()
{
    if (!late_create_offered()) return "A new creature can be made here only while you have no village of your own.";
    if (!H->sym("EditorRequest_ctor") || !H->sym("Editors::EditorRequest::Submit"))
        return "The creature editor is not available on this game build.";
    if (game_busy() || mode_switch_pending() || world::load_queued()) return "The game is busy. Try again in a moment.";
    creations::begin_new_creature();
    if (!submit_new_creature_request()) {
        creations::cancel_new_creature();
        return "The creature editor could not be opened.";
    }
    LOG("late joiner: the creature editor for a new creature was requested (tribe.late_create_editor)");
    if (g_mode == modes::kTribe)
        tribe::late_editor_opened(); // BUG-9159: the host holds the stand-in while they make it
    notice(g_name + " is making a creature of their own in the editor", -1);
    return g_mode == modes::kCiv ? "Make your creature, save it, then leave the editor: your nation's capital follows."
                                 : "Make your creature, save it, then leave the editor: it becomes your creature for this game.";
}

// the simulator's mode switcher has a switch pending (+0x28: a load, the quit to the menu, an editor)
bool mode_switch_pending()
{
    static uintptr_t mgr_ptr = H->sym("SporeCoop::SimulatorModeMgr_ptr");
    void* mgr = mgr_ptr ? *(void**)mgr_ptr : nullptr;
    return mgr && skg::field<uint8_t>(mgr, 0x28);
}

// The one busy gate (busy.h, BUG-0404): why the game is busy right now (busy::k* bits). Cheap,
// callable in the frame.
unsigned game_busy()
{
    uint32_t m = game::mode_id();
    return busy::bits(world::cinematic_playing(), coop_ui::input_lock_now(), world::time_paused(),
                      m == modes::kEditor || m == modes::kEdit,
                      m == modes::kLoad || mode_switch_pending() || world::load_queued());
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
// (BUG-0849: not in the end of Tribe: no share save can be made there, the vehicle editor must not wait 15 s for one)
static bool tribe_graduating();
bool share_save_due() { return (world::save_busy() || world::save_pending()) && !tribe_graduating(); }

static bool native_first_spacecraft(void* req)
{
    return req && game::mode_id() == modes::kCiv && skg::field<uint32_t>(req, 0xC) == 0x96B24187 &&
        skg::field<uint32_t>(req, 0x10) == 0 && skg::field<uint32_t>(req, 0x1C) == modes::kCiv &&
        skg::field<uint32_t>(req, 0x90) == 0x0116D51D;
}

static bool personal_cell_edit(void* req, uint32_t mode)
{
    if (!req || mode != modes::kCell) return false;
    const uint32_t editor = skg::field<uint32_t>(req, 0xC);
    // Native mating selects one of five editors from the Cell growth table.
    // The Creature landfall editor (0xFD4902BD) remains a shared transition.
    return editor == 0x1D2EC0A0 || (editor >= 0x1D2EC0A4 && editor <= 0x1D2EC0A7);
}

static bool submit_native_editor(void* req)
{
    const bool tell_cell_edit = g_sess.connected() && g_sess.players().size() > 1 &&
                                personal_cell_edit(req, game::mode_id());
    const bool personal_launch = g_sess.connected() && g_sess.role() == net::Role::Client &&
                                 native_first_spacecraft(req);
    const bool captain_outfit = req && game::mode_id() == modes::kSpace &&
        skg::field<uint32_t>(req,0xC) == 0xA56567F7 &&
        creations::personal_captain_outfit(skg::field<uint32_t>(req,0x10));
    bool restoring = false;
    if (personal_launch && !creations::prepare_training_ship(req,restoring)) return false;
    bool r = o_Submit(req);
    if (r && personal_launch && !restoring) creations::begin_personal_launch();
    if (r && captain_outfit) creations::begin_captain_outfit();
    if (r && tell_cell_edit) notice(g_name + " is editing their cell");
    return r;
}

// BUG-0560: the Cell stage's request (0xE84600) submits and then posts the editor's DNA budget (message 0x90A03FDF,
// +8 = DNAForFood(food) - spent); the editor's listener takes it only after it entered (cEditor::OnEnter registers
// it). A held request (the vote, a save, the avatar's new body) let the game post it at once with no editor
// listening: the editor opened with the config's start budget (100) and the game booked spent = budget - remaining
// on the way out, a negative spent that paid again on every visit. On: the budget is posted again after the held
// request's Submit, queued behind it as the game does. Off: as before.
bool g_editor_budget_fix = true;
COOP_FIX("cell.editor_budget", "BUG-0560", g_editor_budget_fix,
         "cell: an editor opened after the vote gets the stage's DNA budget, not the editor's start budget");
unsigned g_budgets_posted; // tests
void post_editor_budget(int budget)
{
    uintptr_t get = H->sym("App::IMessageManager::Get"), nw = H->sym("GeneralAllocator::new_");
    uintptr_t vt = H->sym("SporeCoop::EditorBudgetMsg_vtable");
    void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!mm || !nw || !vt)
        return;
    static const char kName[] = "SporeCoop";
    uint32_t* m = skg::cdecl_call<uint32_t*>(nw, 0x40u, kName, 0, 0, 0, 0);
    if (!m)
        return;
    memset(m, 0, 0x40);
    m[0] = (uint32_t)vt; // the game's refcounted message (0xE84600: base vtable, then this one; +4 refcount 0)
    m[0xC] = 0x90A03FDFu;
    m[2] = (uint32_t)budget;
    m[6] = 1;
    skg::vcall<void>(mm, 0x18 / 4, 0x90A03FDFu, (void*)m, (void*)nullptr, 0); // MessagePost (queued)
    g_budgets_posted++;
    LOG("cell: DNA budget %d posted again to the held editor (BUG-0560)", budget);
}

bool submit_now(void* req)
{
    const int budget = g_editor_budget_fix && game::mode_id() == modes::kCell ? cellsync::editor_budget() : -1;
    bool r{};
    {
        callguard::Hold vote_bypass(g_vote_bypass);
        coop_ui::editor_opening(); // 3X A
        r = submit_native_editor(req);
    }
    if (r)
        g_editor_submitted_at = H->time_seconds();
    if (r && budget >= 0)
        post_editor_budget(budget);
    return r;
}

// BUG-0404: an editor that opened while a cinematic of the stage played (the evolution's
// CRG_Fade_From_Black_Egg F4F96E43 right after the last editor, a caption, a brain upgrade) came up
// without its buttons, and the cinematic never ended in the editor mode: its input lock stayed on and
// the host could not leave the editor. The vote ends whenever the others are ready or its time is up,
// whatever the host's game plays. The held request now waits at the busy gate (busy::kEditorGate: a
// cinematic, the input lock, the game time paused) and for 3 s after the game came back from an
// editor (the evolution's cinematic starts then). A pause waits as long as it lasts; a cinematic at
// most 60 s, then it is ended natively (EndCinematic) right before the editor opens. Off: the editor
// opens at once (0.7.3.3).
bool g_editor_cine_fix = true;
COOP_FIX("core.editor_cinematic", "BUG-0404", g_editor_cine_fix, "core: a vote's editor waits at the busy gate (cinematic, input lock, pause)");
constexpr double kEditorCineWait = 60.0;

// BUG-0576: the game opens some editors from a paused game: at the end of Tribe the stage's listener 0xCD1D60 takes
// the end of TRG2CVG_PreEditor (and of TRG2CVG_ShowCityHall), pauses the game time (reason 0x4BF38A6), raises the
// input lock (cGameInputManager +0x110) and submits the City Hall editor 99E92F05; cGameModeManager's 0xB1DF60
// submits a request under the same pause and resumes after Submit. The pause and the lock are the request's own:
// the game lifts them in the editor. The busy gate held such a request until they ended, so the host's Tribe -> Civ
// change stood on a black screen for 60 s (input lock) or 300 s (pause only). On: the pause and the input lock the
// game had when it submitted the request do not hold it (the pause only while the game's editor pause reason
// 0x4BF38A6 is counted); a cinematic and any later pause or lock still do. Off: they held it (0.7.6).
bool g_editor_own_pause_fix = true;
COOP_FIX("core.editor_own_pause", "BUG-0576", g_editor_own_pause_fix,
         "core: a vote's editor the game requested from a paused game (end of Tribe) does not wait for that pause");
constexpr uint32_t kEditorPauseReason = 0x4BF38A6u;
unsigned g_req_own_busy = 0;     // busy::kPaused / kInputLock the game had when it submitted the held request
unsigned g_submits_own_pause;    // tests: held editor requests that went on under the game's own pause
bool g_own_told = false;         // the held request's own pause was logged
int editor_pause_count()
{
    uintptr_t get = H->sym("Simulator::cGameTimeManager::Get"), cnt = H->sym("Simulator::cGameTimeManager::GetPauseCount");
    void* tm = get ? skg::cdecl_call<void*>(get) : nullptr;
    return tm && cnt ? skg::thiscall<int>(cnt, tm, kEditorPauseReason) : -1;
}
// the busy bits that are the held request's own (masked out at the gate)
unsigned req_own_bits()
{
    if (!g_editor_own_pause_fix)
        return 0;
    unsigned own = g_req_own_busy & busy::kInputLock;
    if ((g_req_own_busy & busy::kPaused) && editor_pause_count() != 0) // -1: no symbol, trust the snapshot
        own |= busy::kPaused;
    return own;
}
double g_editor_left_at = -100;  // the game came back from an editor into a stage
busy::Wait g_submit_wait;        // the held request's wait at the busy gate
bool g_submit_busy_told = false; // the held request's wait was logged
bool g_submit_end_cine = false;  // the outside-frame submit ends the cinematic first
unsigned g_submits_after_cine;   // tests: editor requests that waited for a cinematic
unsigned g_submit_cines_ended;   // tests: cinematics ended because they outlasted the wait

void hold_submit(void* req)
{
    skg::vcall<int>(req, 1); // AddRef
    if (g_submit_later)
        skg::vcall<int>(g_submit_later, 2); // a newer request replaces it
    g_submit_later = req;
    g_submit_held_at = H->time_seconds();
    g_submit_wait = {};
    g_submit_busy_told = false;
    g_own_told = false;
    if (share_save_due()) {
        g_submits_held++;
        LOG("the editor waits until the world save is written");
    }
}

// the held request goes to the game once the save is written (at most 15 s later), outside the
// frame: editors create D3D resources
unsigned g_submits_after_swap; // tests (BUG-0318): editor requests that waited for the avatar's new body
bool pause_ui_up(); // UI traversal, outside-frame only
bool native_tribal_outfit(void* req)
{
    return req && own_tribal_outfitter(skg::field<uint32_t>(req, 0xC)) &&
        skg::field<uint32_t>(req, 0x90) == 0x0116DD1B;
}
bool g_tribal_graduation_editor = true;
COOP_FIX("tribe.graduation_editor", "BUG-9048", g_tribal_graduation_editor,
         "tribe: let the native graduation open its city hall editor through its own pause");
bool native_tribal_graduation(void* req)
{
    // Observed native History graduation request: blank city hall, return to
    // Tribe, community editor context. The transition itself owns the pause.
    return req && game::mode_id() == modes::kTribe &&
        skg::field<uint32_t>(req, 0xC) == 0x99E92F05 &&
        skg::field<uint32_t>(req, 0x10) == 0 &&
        skg::field<uint32_t>(req, 0x1C) == modes::kTribe &&
        skg::field<uint32_t>(req, 0x90) == 0x0116DD1B;
}
bool tribal_graduation_pending();
bool g_tribal_graduation_vehicle_types = true;
COOP_FIX("tribe.graduation_vehicle_types", "BUG-9060", g_tribal_graduation_vehicle_types,
         "tribe: accept military and religious first vehicles through the native graduation pause");
bool native_tribal_editor_pause(void* req)
{
    // The native construction movie then requests the first land vehicle.
    uint32_t editor = req ? skg::field<uint32_t>(req, 0xC) : 0;
    bool land_vehicle = editor == 0xF670AA43 || (g_tribal_graduation_vehicle_types &&
        (editor == 0x7D433FAD || editor == 0x9AD7D4AA));
    bool vehicle = req && game::mode_id() == modes::kTribe && tribal_graduation_pending() &&
        land_vehicle &&
        skg::field<uint32_t>(req, 0x10) == 0 &&
        skg::field<uint32_t>(req, 0x1C) == modes::kTribe &&
        skg::field<uint32_t>(req, 0x90) == 0x0116DD1B;
    return native_tribal_outfit(req) || (g_tribal_graduation_editor && (native_tribal_graduation(req) || vehicle));
}
static bool g_civ_launch_editor = true;
COOP_FIX("civ.launch_editor", "BUG-9070", g_civ_launch_editor,
         "civ: allow the first spacecraft editor through its own launch pause");
static bool native_editor_owns_pause(void* req)
{
    if (native_tribal_editor_pause(req)) return true;
    // Vantiegan native History request after reaching ten team cities. Keep
    // the actual pause menu, save and cinematic checks around submission.
    return g_civ_launch_editor && native_first_spacecraft(req) && civ::founder_space_ready();
}
// BUG-9050 (Lineages) and upstream's BUG-0849 (taken from it) share one switch, `coop.fix tribe.graduation_save`
// (registered once, with BUG-0849's g_graduation_fix below)
static bool g_graduation_fix = true;
static bool& g_tribal_graduation_save = g_graduation_fix;
uint32_t g_tribal_graduation_world;
double g_tribal_graduation_settled;
bool tribal_graduation_pending()
{
    return g_tribal_graduation_save && g_tribal_graduation_world &&
        g_tribal_graduation_world == modes::current_world();
}
void tribal_graduation_tick(double now)
{
    if (!g_tribal_graduation_world) return;
    uint32_t mode = game::mode_id();
    if (g_tribal_graduation_world != modes::current_world() ||
        (mode != modes::kTribe && mode != modes::kEditor && mode != modes::kEdit)) {
        g_tribal_graduation_world = 0;
        g_tribal_graduation_settled = 0;
        return;
    }
    // A native cancellation returns to playable Tribe. Graduation instead
    // continues through the cinematic and vehicle editor. Do not mistake
    // the brief gap before that cinematic starts for a cancellation.
    if (mode != modes::kTribe || game_busy() || g_vote.active || g_submit_later || g_submit_queued) {
        g_tribal_graduation_settled = 0;
    } else if (!g_tribal_graduation_settled) {
        g_tribal_graduation_settled = now;
    } else if (now - g_tribal_graduation_settled > 5) {
        g_tribal_graduation_world = 0;
        g_tribal_graduation_settled = 0;
    }
}
void submit_tick(double now)
{
    if (!g_submit_later || ((world::save_busy() || (world::save_pending() && !tribal_graduation_pending())) &&
                           now - g_submit_held_at < 15.0))
        return;
    if (creations::avatar_swap_settling(now) && now - g_submit_held_at < 90.0)
        return;
    bool end_cine = false;
    if (g_editor_cine_fix && stage_mode(game::mode_id())) { // BUG-0404: the busy gate
        unsigned all = game_busy() & busy::kEditorGate;
        unsigned b = all & ~req_own_bits();
        if (b != all && !g_own_told) {
            g_own_told = true;
            {
                LOG("the editor does not wait for the game's own %s (it requested the editor from it, BUG-0576; editor "
                    "pause count %d)", busy::names(all & ~b).c_str(), editor_pause_count());
            }
        }
        // Lineages (BUG-9048 / BUG-9060 / BUG-9070): the native planner intentionally pauses and locks the
        // simulation. Its own outfit request may leave that planner. Real pause/dialog UI is checked again
        // outside-frame before Submit, and cinematics still wait.
        if (native_editor_owns_pause(g_submit_later)) b &= ~(busy::kPaused | busy::kInputLock);
        bool expired = false;
        bool settling = now - g_editor_left_at < 3.0; // (the evolution's cinematic is about to start)
        bool was = g_submit_busy_told;
        if (busy::hold(g_submit_wait, b, now, kEditorCineWait, busy::kPaused, 300.0, &expired) || settling) {
            if (!was) {
                g_submits_after_cine++;
                LOG("the editor waits: the game is busy (%s)", busy::names(b).c_str());
                if (b)
                    feed(b & busy::kPaused ? "your editor opens when the game goes on"
                                           : "your editor opens when the cinematic is over");
            }
            g_submit_busy_told = true;
            return;
        }
        end_cine = expired && (b & busy::kCinematic);
        g_submit_busy_told = false;
    }
    void* req = g_submit_later;
    if (g_editor_cine_fix && req_own_bits() & (game_busy() & busy::kEditorGate))
        g_submits_own_pause++;
    g_submit_later = nullptr;
    g_submit_wait = {};
    g_submit_busy_told = false;
    if (!stage_mode(game::mode_id()) || mode_switch_pending()) { // the host left the stage meanwhile
        LOG("the held editor request is dropped: the game left the stage");
        skg::vcall<int>(req, 2);
        return;
    }
    if (now - g_submit_held_at > 0.05)
        LOG("the editor opens (held %.1f s: a world save or the avatar's new body)", now - g_submit_held_at);
    g_submit_queued = true; // no save from here until the game has the request (save_blocker)
    g_submit_end_cine = end_cine;
    H->run_outside_frame(
        [](void* r) {
            if (native_editor_owns_pause(r) && pause_ui_up()) {
                hold_submit(r);
                g_submit_queued = false;
                skg::vcall<int>(r, 2);
                return;
            }
            if (g_submit_end_cine && world::end_cinematic("before the held editor opens (BUG-0404)"))
                g_submit_cines_ended++;
            g_submit_end_cine = false;
            submit_now(r);
            g_submit_queued = false;
            skg::vcall<int>(r, 2); // Release
        },
        req);
}

// BUG-0849: the end of Tribe (the City Hall editor done, the vehicle editor to come, then Civ). The game takes
// the Tribe world down for Civ in that time (the player tribe has no food mat, every animal goes to (0 0 0),
// docs/re/tribe.md#tribe-civ-transition); a share save posted then (the return from the City Hall editor taken for
// an evolution, or a guest joining) was written while the village was dismantled: the fork's host faulted in the
// game's serializer 36 ms after the vehicle editor was asked for. No share and no share save from the City Hall
// editor's end to the Civ stage: the Civ stage is shared as a new stage. Off: as 0.7.12.
// (g_graduation_fix is defined above, shared with BUG-9050)
COOP_FIX("tribe.graduation_save", "BUG-0849", g_graduation_fix, "tribe: no world share or share save between the City Hall editor and the Civilization stage; share the completed Civilization entry, not intermediate graduation designs (BUG-9050)");
static unsigned g_graduation_refusals; // tests: share saves refused in the end of Tribe
static bool tribe_civ_editor(uint32_t id);
static uint32_t g_last_editor_req;
static bool tribe_graduating()
{
    return g_graduation_fix && game::mode_id() == modes::kTribe && (tribe::stage_end() || tribe_civ_editor(g_last_editor_req));
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
    if (mode == modes::kScenario)
        return "Finish or quit the adventure in its own menu before saving the campaign.";
    if (!stage_mode(mode))
        return "the game is not in a stage";
    if (mode == modes::kCreature && creature_rescue::pending())
        return "Wait for the pack to revive or respawn before saving.";
    if (mode == modes::kTribe && tribe::rescue_pending())
        return "Wait for the last villager to revive or respawn before saving.";
    if (mode == modes::kTribe && tribe::outfits_pending())
        return "Wait for your partner's outfit to finish saving.";
    if (mode == modes::kTribe && tribal_graduation_pending())
        return "Finish Civilization graduation before sharing the campaign.";
    if (g_submit_queued || H->time_seconds() - g_editor_submitted_at < 5.0)
        return "an editor is opening";
    if (mode_switch_pending())
        return "the game is switching modes";
    if (tribe_graduating()) { // BUG-0849
        if (!g_graduation_refusals++)
            LOG("no world save in the end of Tribe (City Hall built, Civilization next): the world is sent in Civ");
        return "the Tribe stage is ending (Civilization comes next): the world is sent in the Civilization stage";
    }
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

// BUG-0466 (observed, lab 20261103-141812 editor_sync_guest): the guest's own editor of a vote
// (open_own_editor: an EditorRequest without the evolution tag +0x90) showed the DNA budget and the
// price of the new parts (40 -> 15), but the game never charged it: the creature game's handler
// 0xD42E50 applies an editor's result (message 0x30C11C7, data +0xC the request's tag) only for the
// evolution tag 0x64AA168 (then the stage's return sets GetEvolutionPoints to the result's
// remaining DNA, +0x34, 0xD445AA). The guest kept its DNA and the host's total stayed the same:
// free parts. Now the result of the own editor (not cancelled, +0x44 == 0) charges the price
// through the game (the remaining DNA at +0x34), and core.dna_spend sends it to the host.
// `coop.fix creations.own_editor_dna off`: the own editor costs nothing (0.7.3.6).
static void dna_spend_note(float d);
bool g_own_editor_dna_fix = true;
COOP_FIX("creations.own_editor_dna", "BUG-0466", g_own_editor_dna_fix, "creature: the parts a guest adds in its own editor cost DNA (the editor's price is charged)");
static unsigned g_own_editor_charged = 0;
typedef bool(SK_THISCALL* CreatureGameMsg_t)(void*, uint32_t, void*);
CreatureGameMsg_t o_CreatureGameMsg;
static void* g_creature_game; // BUG-0720 tests: the creature game whose handler builds the evolution request
bool SK_THISCALL h_CreatureGameMsg(void* self, uint32_t id, void* data)
{
    g_creature_game = self;
    if (id == 0x30C11C7u && g_own_editor_charge_due && data && skg::readable(data, 0x48)) {
        uint32_t tag = skg::field<uint32_t>(data, 0xC);
        int left = skg::field<int32_t>(data, 0x34);
        uint8_t cancelled = skg::field<uint8_t>(data, 0x44);
        float now = game::dna();
        LOG("own editor result: tag %08X remaining DNA %d cancelled %u, DNA here %.0f%s", tag, left, (unsigned)cancelled, now,
            g_own_editor_dna_fix ? "" : " (creations.own_editor_dna off)");
        if (tag != 0x64AA168u && tag != 0x12191CAu) {
            g_own_editor_charge_due = false;
            if (g_own_editor_dna_fix && !cancelled && left >= 0 && left < now) {
                game::dna_add_raw((float)left - now); // the editor's price, as the evolution's return sets it
                g_own_editor_charged++;
                dna_spend_note((float)left - now); // a spend of ours: to the host (core.dna_spend)
                feed("your new parts cost " + std::to_string((int)(now - left)) + " DNA");
            }
        }
    }
    return o_CreatureGameMsg(self, id, data);
}

// BUG-0588: the editor of the last request (EditorRequest +0xC): the end of Tribe's City Hall and vehicle editors
static bool tribe_civ_editor(uint32_t id)
{
    return id == 0x99E92F05u || id == 0xF670AA43u || id == 0x9AD7D4AAu || id == 0x7CB21BADu;
}
// BUG-0720 tests (coop.editreq): a request captured at Submit instead of being submitted
struct ReqImage {
    bool have = false;
    uint8_t bytes[0x9C] = {};
    std::vector<uint32_t> vec70; // the +0x70 vector's contents
};
static bool g_editreq_capture;
static ReqImage g_editreq_game;
static ReqImage req_image(void* req)
{
    ReqImage im;
    if (!req || !skg::readable(req, 0x9C))
        return im;
    im.have = true;
    memcpy(im.bytes, req, 0x9C);
    uint32_t* b = skg::field<uint32_t*>(req, 0x70);
    uint32_t* e = skg::field<uint32_t*>(req, 0x74);
    for (uint32_t* q = b; b && e && e - b <= 64 && q < e; q++)
        im.vec70.push_back(*q);
    return im;
}
// BUG-9278 (step 4 part 3): the stage-end request held while the party moves into its crossing (galaxy_client.cpp)
static void* g_cross_req = nullptr;
static double g_cross_at = 0;
static unsigned g_cross_holds = 0, g_cross_sent = 0;
bool SK_CDECL h_Submit(void* req);
static void cross_tick(double now)
{
    if (!g_cross_req)
        return;
    if (galaxy::crossing_ready()) {
        void* r = g_cross_req;
        g_cross_req = nullptr;
        g_cross_sent++;
        LOG("evolution: the party is in its crossing: the stage's end goes on there (BUG-9278)");
        H->run_outside_frame(
            [](void* q) {
                h_Submit(q); // (again through the vote: the session is the party's now)
                skg::vcall<int>(q, 2);
            },
            r);
    } else if (!galaxy::in_crossing() && now - g_cross_at > 30) {
        skg::vcall<int>(g_cross_req, 2);
        g_cross_req = nullptr;
        feed("your party could not move on to the next stage: try again");
    }
}

bool SK_CDECL h_Submit(void* req)
{
    if (g_editreq_capture) { // tests: the game's own request, kept and not submitted
        g_editreq_game = req_image(req);
        g_editreq_capture = false;
        return false;
    }
    uint32_t mode = game::mode_id();
    // BUG-9278 (step 4 part 3): on a galaxy era the Creature stage's end moves the party (its leader asks) into its
    // own crossing first; there the co-op stage end runs for the party alone
    if (!g_vote_bypass && req && mode == modes::kCreature && !g_lineages_evo_req && skg::readable(req, 0x94) &&
        skg::field<uint32_t>(req, 0x90) == kTagStageEnd) {
        std::string why;
        const auto step = galaxy::stage_end_request(why);
        if (step == galaxy::kStageEndHold) {
            if (g_cross_req != req) {
                skg::vcall<int>(req, 1); // AddRef: sent again from cross_tick
                if (g_cross_req)
                    skg::vcall<int>(g_cross_req, 2);
                g_cross_req = req;
                g_cross_at = H->time_seconds();
                g_cross_holds++;
            }
            feed(why);
            return false;
        }
        if (step == galaxy::kStageEndRefuse) {
            feed(why);
            g_evo_flag_refused = true; // BUG-0920: the game sets its "editor pending" flag after this call
            return false;
        }
    }
    if (native_tribal_graduation(req)) {
        g_tribal_graduation_world = modes::current_world();
        g_tribal_graduation_settled = 0;
    }
    if (req && skg::readable(req, 0x10))
        g_last_editor_req = skg::field<uint32_t>(req, 0xC);
    if (req && mode == modes::kMenu && skg::readable(req, 0x9C)) { // 3Z B / ships: a new world's editor, as the game asks
        std::string hex;
        char b[12];
        for (int o = 0; o < 0x9C; o += 4) {
            snprintf(b, sizeof b, "%08X ", skg::field<uint32_t>(req, o));
            hex += b;
        }
        LOG("editor request from the galaxy menu: %s", hex.c_str());
        if (g_test_unlock_start && skg::field<uint32_t>(req, 0xC) == kCreatureStartEditor) { // tests (3Z B)
            void* items = skg::field<void*>(req, 0x94);
            for (const auto& p : item_states(items))
                skg::thiscall<bool>(H->sym("Simulator::cCollectableItems::UnlockPart"), items, p.lo, p.hi, 0);
            LOG("start editor: tests: the game's parts list unlocked (%zu)", item_states(items).size());
        }
        if (g_sess.role() == net::Role::Host && g_sess.connected() && skg::field<uint32_t>(req, 0xC) == kCreatureStartEditor) {
            const auto states = item_states(skg::field<void*>(req, 0x94)); // 3Z B: the guests' editors get the same
            net::Writer w;
            w.u16((uint16_t)states.size());
            int unlocked = 0;
            for (const auto& p : states) {
                w.u32(p.lo);
                w.u32(p.hi);
                w.u8(p.state);
                unlocked += p.state & 1;
            }
            g_host_start_parts = states; // (a guest joining while we are in this editor gets them too)
            g_host_start_sent.clear();
            for (auto& p : g_sess.players())
                if (p.id != g_sess.my_id()) {
                    g_sess.send_reliable_to(p.id, net::MSG_START_PARTS, w);
                    g_host_start_sent.insert(p.id);
                }
            LOG("start editor: our parts list's %zu part states (%d unlocked) to the guests (3Z B)", states.size(), unlocked);
        }
    }
    // The cell stage asks for its editor again while the mate is still next to the avatar
    // (0xE78D10 -> mating -> event 0x1E -> 0xE59800 -> Submit): one more request came 13 ms after
    // the vote had ended, before the held one reached the game, and started a second vote that
    // ran on while the host was in the editor (its result was dropped). A repeated personal edit
    // request can also send the same notice twice, so the same guard applies to both paths.
    if (!g_vote_bypass && req &&
        (g_submit_later || g_submit_queued || H->time_seconds() - g_editor_submitted_at < 5.0)) {
        g_submits_repeated++;
        return false;
    }
    if (!g_vote_bypass && req) // BUG-0576: what the game itself had paused or locked when it asked for the editor
        g_req_own_busy = game_busy() & (busy::kPaused | busy::kInputLock);
    if (req && mode == modes::kCell && g_sess.connected())
        cellsync::dna_note(g_vote_bypass ? "at the editor request (after the vote)" : "at the editor request");
    if (!g_vote_bypass && req && stage_mode(mode) && !personal_cell_edit(req, mode) &&
        g_sess.connected() && g_sess.players().size() > 1) {
        uint32_t tag = skg::readable(req, 0x94) ? skg::field<uint32_t>(req, 0x90) : 0u;
        uint32_t editor = skg::field<uint32_t>(req, 0xC);
        // BUG-1085: which Creature request this is (the stage-end editor or an evolution inside the stage)
        // (Lineages: our own evolution requests, submit_evolution_request, carry the stage-end tag: never the end)
        bool stage_end = g_editor_tag_fix && mode == modes::kCreature && tag == kTagStageEnd && !g_lineages_evo_req;
        bool in_stage = !g_editor_tag_fix ? tag == kTagStageEnd
                                          : tag == kTagMating && editor != kEditorLandfall &&
                                                !(g_switch_vote_fix && settings::on(settings::kSwitchVote) &&
                                                  H->time_seconds() - g_switch_passed_at < 300.0);
        if (mode == modes::kCreature)
            LOG("editor request in Creature: editor %08X tag %08X (%s)", editor, tag,
                stage_end ? "the stage's end" : in_stage ? "an evolution in the stage" : "other");
        // BUG-9160 (owner decision 2D): in Creature everyone evolves on their own, the host too (no vote); the end of
        // the stage keeps the vote (BUG-1085)
        const bool personal = mode == modes::kCreature && evolve::personal_editor() && !stage_end;
        bool now_ok = g_editor_now_fix && settings::on(settings::kEditorNow) && g_sess.role() == net::Role::Host &&
                      !g_vote.active && mode == modes::kCreature && in_stage;
        // F5 (BUG-0769): creature.mode host: one creature for everyone, only the host evolves it (no vote, no offer);
        // a guest's own editor request is refused (the game's nest / mating call of a guest)
        bool one = host_creature_mode() && (mode == modes::kCell || mode == modes::kCreature);
        if (one && g_sess.role() == net::Role::Host) {
            g_one_evolutions++;
            LOG("evolution: creature.mode host: the host's editor opens at once, every player gets the result");
            notice(g_name + " evolves the creature of everybody", -1);
        } else if (one) {
            g_one_refused++;
            LOG("evolution: creature.mode host: this guest's editor request is refused (the host evolves)");
            feed("the host evolves the creature for everybody (session setting: one creature for everyone)");
            g_evo_flag_refused = true; // BUG-0920: the game sets its "editor pending" flag after this call
            return false;
        } else if (personal && g_sess.role() == net::Role::Host) {
            notice(g_name + " evolves its own creature", -1); // BUG-9160 (a guest: the own evolution below)
        } else if (now_ok) { // BUG-0516: the host's evolution goes on now, the guests get an offer
            uint32_t id = skg::field<uint32_t>(req, 0xC);
            LOG("evolution: the host's editor opens at once, the others get an offer (core.editor_now)");
            broadcast_offer(id, g_name + " evolves now: press Evolve now (F9) to evolve yours too");
            notice(g_name + " evolves now (the others may evolve too)", -1);
        } else if (g_sess.role() == net::Role::Host) {
            if (!g_vote.active) {
                if (stage_end) { // BUG-1085: a stage start keeps the vote
                    g_stage_end_votes++;
                    LOG("evolution: the end of the Creature stage waits for the vote (BUG-1085)");
                    start_vote(req, g_name + " wants to move on to the Tribe stage");
                } else
                    start_vote(req, g_name + (design_stage(mode) ? " wants to edit a design (" : " wants to evolve (") +
                                        modes::mode_name(mode) + ")");
            }
            return false; // runs when the vote is over
        } else if (stage_end && host_world_now() == host_world::HostWorld::Same) {
            // BUG-1085 (docs/09-coop-design.md 9.3): a guest cannot change the stage alone; its press is its I'm ready
            g_stage_end_guest_refused++;
            g_evo_flag_refused = true; // BUG-0920: the game sets its "editor pending" flag after this call
            if (g_vote.active) {
                LOG("evolution: this guest's stage-end request is its I'm ready (BUG-1085)");
                set_ready(true);
            } else {
                LOG("evolution: this guest's stage-end request is not taken: the host moves everybody on (BUG-1085)");
                feed("the host moves everybody on to the Tribe stage (after a vote)");
            }
            return false;
        } else {
            offer_end("the own evolution"); // BUG-0516: this guest evolves on its own now
            notice(g_name + (design_stage(mode) ? " edits a design" : " evolves its own creature"), -1);
            set_ready(true);
        }
    }
    if (!g_vote_bypass && req && creations::avatar_swap_settling(H->time_seconds())) {
        hold_submit(req); // BUG-0318: after the new body is built (submit_tick)
        LOG("the editor waits until the avatar's new body is built");
        feed("your editor opens in a moment (your creature is still being built)");
        g_submits_after_swap++;
        return false;
    }
    if (!g_vote_bypass && req && g_sess.role() == net::Role::Host && share_save_due()) {
        hold_submit(req); // the game's own request (alone in the session): after the save
        return false;
    }
    coop_ui::editor_opening(); // 3X A: the editor's UI is built at 100 %
    bool r = submit_native_editor(req);
    if (r)
        g_editor_submitted_at = H->time_seconds();
    return r;
}

// BUG-0315: the Cell stage's switch to Creature (0xE62440: ends the cinematics, CreatureStart_FromCell, the mode
// switch to 0x1654C01, the fade effect), reached from cCellModeStrategy::HandleMessage 0x47C1D19 (History's check)
// and the cell event 0x1F (0xE7E130). cdecl void(void), globals only: safe to run later from outside the frame.
typedef void(SK_CDECL* CellToCreature_t)();
CellToCreature_t o_CellToCreature;
bool g_switch_go = false; // the vote passed: the next call is the game's own switch
void SK_CDECL h_CellToCreature()
{
    bool multi = g_sess.connected() && g_sess.players().size() > 1;
    if (!g_switch_vote_fix || !settings::on(settings::kSwitchVote) || g_switch_go || game::mode_id() != modes::kCell ||
        !multi) {
        g_switch_go = false;
        o_CellToCreature();
        return;
    }
    if (g_sess.role() == net::Role::Client) {
        if (host_world_now() != host_world::HostWorld::Same) { // the guest's own world: its own switch
            o_CellToCreature();
            return;
        }
        g_switch_guest_held++;
        if (g_vote.active) {
            LOG("the Creature stage: this guest's click on History's check is its I'm ready (BUG-0315)");
            set_ready(true);
        } else {
            LOG("the Creature stage: a guest's own switch is not taken, the host starts it for everybody (BUG-0315)");
            feed("the host moves everybody on to the Creature stage (after a vote)");
        }
        return;
    }
    g_switch_held++;
    LOG("the host's switch to the Creature stage (from %p, cinematic %d)", __builtin_return_address(0),
        (int)world::cinematic_playing());
    if (g_vote.active) {
        LOG("the Creature stage waits: a vote runs ('%s')", g_vote.text.c_str());
        feed("the Creature stage: wait until the vote is over, then click History's check again");
        return;
    }
    LOG("the Creature stage waits for a vote (BUG-0315, core.switch_vote)");
    g_switch_votes++;
    start_action_vote(g_name + " wants to move on to the Creature stage", "the Creature stage starts", [] {
        if (game::mode_id() != modes::kCell) {
            LOG("the Creature stage: the vote passed out of the Cell stage, nothing to start");
            return;
        }
        g_switch_passed_at = H->time_seconds();
        g_switch_go = true;
        LOG("the Creature stage starts after the vote (BUG-0315)");
        h_CellToCreature();
    });
}

// BUG-0404 (the busy gate): an action vote's result (a superweapon ...) ran the moment the vote
// ended, also during a cinematic, a pause or a load. It now waits at the gate (busy::kActionGate):
// a pause as long as it lasts, the rest at most 60 s; it is dropped when the stage was left. Off: at
// once (0.7.3.3).
bool g_vote_action_busy_fix = true;
COOP_FIX("core.vote_action_busy", "BUG-0404", g_vote_action_busy_fix, "core: an action vote's result waits at the busy gate");
std::function<void()> g_vote_action_due;
busy::Wait g_vote_action_wait;
bool g_vote_action_told = false;
uint32_t g_vote_action_mode = 0;
unsigned g_vote_actions_waited; // tests

void vote_action_tick(double now)
{
    if (!g_vote_action_due)
        return;
    unsigned all = game_busy();
    if (game::mode_id() != g_vote_action_mode && !(all & (busy::kLoading | busy::kEditor))) {
        LOG("the vote's action is dropped: the stage was left");
        g_vote_action_due = nullptr;
        return;
    }
    unsigned b = all & busy::kActionGate;
    if (busy::hold(g_vote_action_wait, b, now, 60.0, busy::kPaused)) {
        if (!g_vote_action_told) {
            g_vote_action_told = true;
            g_vote_actions_waited++;
            LOG("the vote's action waits: the game is busy (%s)", busy::names(b).c_str());
        }
        return;
    }
    static std::function<void()> run;
    run = std::move(g_vote_action_due);
    g_vote_action_due = nullptr;
    g_vote_action_wait = {};
    g_vote_action_told = false;
    H->run_outside_frame([](void*) { g_vote_actions_run++; if (run) run(); }, nullptr);
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
        if (g_vote_action_busy_fix) { // BUG-0404: vote_action_tick runs it at the busy gate
            g_vote_action_due = std::move(run);
            run = nullptr;
            g_vote_action_wait = {};
            g_vote_action_told = false;
            g_vote_action_mode = game::mode_id();
            vote_action_tick(H->time_seconds());
        } else
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
    w.u8(0); // flags (BUG-0516)
    g_sess.broadcast_reliable(net::MSG_VOTE, w);
}

// BUG-0516: the host evolves now; every guest may open its own editor of the same id
void broadcast_offer(uint32_t editor, const std::string& text)
{
    net::Writer w;
    w.u8(0); // not a vote: an older guest takes it as the end of no vote
    w.u8((uint8_t)kOfferSeconds);
    w.str8(text);
    w.u32(editor);
    w.u8(kVoteOffer);
    g_sess.broadcast_reliable(net::MSG_VOTE, w);
    g_offers_sent++;
    trace::line("vote", "offer sent: '%s' editor=%s", text.c_str(), trace::hex(editor).c_str());
}

// guest: the offer ends (taken, expired, the stage or the session left)
void offer_end(const char* why)
{
    if (!g_offer_editor)
        return;
    trace::line("vote", "offer of editor %s ends: %s", trace::hex(g_offer_editor).c_str(), why);
    g_offer_editor = 0;
    if (g_offer_opened_ui && !g_vote.active)
        coop_ui::open(false);
    g_offer_opened_ui = false;
    g_lobby_dirty = true;
}

// guest: "Evolve now": the own editor opens when the stage has settled (own_editor_tick)
void offer_take()
{
    uint32_t id = g_offer_editor;
    if (!id)
        return;
    g_offers_taken++;
    offer_end("taken");
    g_own_editor_due = id;
    g_own_editor_due_at = g_own_editor_due_first = H->time_seconds();
    feed("evolution: your editor opens in a moment");
}

void offer_tick(double now)
{
    if (!g_offer_editor)
        return;
    if (!g_sess.connected() || g_mode != modes::kCreature) {
        offer_end(!g_sess.connected() ? "the session ended" : "the stage was left");
        return;
    }
    if (now > g_offer_until) {
        g_offers_expired++;
        offer_end("time is up");
        feed("evolution: you can evolve your creature at your nest later");
    }
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
    tribe::cancel_outfit_vote();
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

// BUG-0801: a guest that plays another world of the host's stage ignores the host's vote (BUG-0702) and never sends
// MSG_READY: every vote of a 3-4 player session with one guest elsewhere ran into its 60 s timer. The vote counts
// only the players in the host's world now (own_world::peer_elsewhere: what that guest itself decides). Off: every
// player of the session (0.7.12).
static bool g_vote_present_fix = true;
COOP_FIX("core.vote_present", "BUG-0801", g_vote_present_fix, "core: a vote does not wait for a guest that plays another world");
static int g_vote_away_last = 0;
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
    if (g_vote.arrival) // BUG-0315: a guest that plays the host's Creature world now voted for it in Cell
        for (auto& [id, r] : g_remotes) {
            if (r.echo || r.buf.empty() || (g_ready.count(id) && g_ready[id]))
                continue;
            const auto& ls = r.buf.latest();
            if (same_place(ls) && (ls.flags & net::PF_IN_WORLD)) {
                g_ready[id] = true;
                g_lobby_dirty = true;
                g_switch_arrivals++;
                trace::line("vote", "player %u arrived in the Creature stage: ready (BUG-0315)", (unsigned)id);
            }
        }
    bool all = true;
    int away = 0;
    for (auto& p : g_sess.players()) {
        if (p.id != 0 && g_vote_present_fix && own_world::peer_elsewhere((uint8_t)p.id)) {
            away++; // BUG-0801: plays another world, ignores the vote (BUG-0702): not waited for
            continue;
        }
        all &= g_ready.count(p.id) && g_ready[p.id];
    }
    if (away != g_vote_away_last) {
        g_vote_away_last = away;
        trace::line("vote", "%d player(s) in another world: not waited for (BUG-0801)", away);
    }
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

static bool g_cell_editor_share_fix = true;
COOP_FIX("world.cell_editor_in_place", "BUG-9096", g_cell_editor_share_fix,
         "Cell editor returns update live creations without reloading an already joined partner");
// BUG-0508: after every host editor (Cell and Creature) every guest got the whole world again (a 3 MB
// save) and went Cell / Creature -> galaxy menu -> load, although the host's new creation (its bundle,
// creations.cpp) had already rebuilt its puppet. A guest of this version in the same world and stage
// now keeps playing: the bundle rebuilds the host's puppet and a guest that plays the world's species
// takes the host's new one in place (creations.cpp `follow`); in Creature the host still saves its own
// game once (its avatar takes the evolved species only with a save) but sends nothing. The world still
// goes when the evolution needs it (docs/re/creations.md#creations-evolve-in-place): a migration started
// (the brain grew, BUG-0019), the guest in another world or stage, an older guest, or (Creature) a guest
// whose avatar still plays the pre-editor species 30 s later.
// Off: every guest gets the world after every editor (0.7.3.6).
static bool g_evolve_inplace_fix = true;
COOP_FIX("world.evolve_in_place", "BUG-0508", g_evolve_inplace_fix,
         "world: after the host's editor a guest in the same world and stage takes the new body without loading the world again");
static uint32_t g_evo_key_before = 0; // host: its avatar's species when its editor opened (Creature)
static int g_evo_age_before = -1;     // host: its avatar's age (+0xB34) then
static bool g_evo_cine = false;       // host: a cinematic held a share since the last evolution
static unsigned g_evo_inplace = 0, g_evo_fallback = 0; // tests: guests kept in place, worlds sent after all
// the creature stage's own evolution request, as its message handler 0xD42E50 builds it when the avatar meets its mate
// (docs/re/creations.md#creations-evolution-request): +0x90 the evolution tag (the editor's result is applied to the
// species), +0x94 the collected parts, the flags, +0x70 cPlayer +0x10FC..+0x1108; through the hook (h_Submit). -1: not
// possible here, else Submit's result. Outside the frame. (Was coop.vote evolve's, BUG-0457.)
int submit_evolution_request()
{
    if (game::mode_id() != modes::kCreature) return -1;
    uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit"),
              assign = H->sym("SporeCoop::IntrusivePtr_Assign"), push = H->sym("SporeCoop::VectorU32_PushBack"),
              nouns = H->sym("Simulator::cGameNounManager::Get"), getp = H->sym("SporeCoop::cGameNounManager_GetPlayer");
    void* av = game::avatar();
    void* nm = nouns ? skg::cdecl_call<void*>(nouns) : nullptr;
    void* pl = nm && getp ? skg::thiscall<void*>(getp, nm) : nullptr;
    if (!ctor || !submit || !assign || !push || !av || !pl || !skg::readable((char*)pl + 0x10E8, 0x24)) return -1;
    void* req = skg::cdecl_call<void*>(H->sym("GeneralAllocator::new_"), (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
    if (!req) return -1;
    skg::thiscall<void*>(ctor, req);
    skg::field<uint32_t>(req, 0xC) = 0x465C50BAu;
    game::Key k = game::species_key(av);
    skg::field<uint32_t>(req, 0x10) = k.instance;
    skg::field<uint32_t>(req, 0x14) = k.type;
    skg::field<uint32_t>(req, 0x18) = k.group;
    skg::field<uint32_t>(req, 0x90) = 0x64AA168u;
    if (void* items = skg::field<void*>(pl, 0x10E8))
        skg::thiscall<void*>(assign, (char*)req + 0x94, items);
    for (int o : {0x34, 0x36, 0x37, 0x38, 0x39, 0x3B})
        skg::field<uint8_t>(req, o) = 0;
    skg::field<uint8_t>(req, 0x3C) = 1;
    skg::field<uint8_t>(req, 0x3D) = 1;
    for (int o = 0x10FC; o <= 0x1108; o += 4) {
        uint32_t v = skg::field<uint32_t>(pl, o);
        skg::thiscall<void*>(push, (char*)req + 0x70, &v);
    }
    bool r{};
    {
        callguard::Hold evo_req(g_lineages_evo_req); // an evolution, not the stage's end (BUG-1085 gate)
        r = skg::cdecl_call<bool>(submit, req);
    }
    if (uintptr_t f = H->sym("SporeCoop::CreatureEvolutionPending_flag")) // as 0xD43069
        *(uint8_t*)f = 1;
    return r ? 1 : 0;
}

// the migration state of this game's player (cPlayer +0x69C property 0x514A219, docs/re/creature.md:
// CreatureGame_AfterEditor): 1 = a migration started by this evolution (CRG_SpeciesMigrate), 2 = its goal
static int player_property(uint32_t id)
{
    static uintptr_t a_GetProperty = H->sym("SporeCoop::Player_GetProperty");
    uint8_t* nm = (uint8_t*)game::noun_manager();
    void* pl = nm && skg::readable(nm + 0x74, 4) ? *(void**)(nm + 0x74) : nullptr;
    return a_GetProperty && pl ? skg::thiscall<int>(a_GetProperty, pl, id) : -1;
}
static int host_migration() { return player_property(0x514A219u); }

// ---- BUG-9163 (owner decision 2G): the party migrates together ----
// "anyone starting the native migration warns every player in the world, 10 s later all go into the editor for final
// touches (downed or dead players too) and migrate together". A game whose own evolution started the migration (its
// player's property 0x514A219 goes 0 -> 1 after its editor, CRG_SpeciesMigrate) tells the others (MSG_MIGRATE: a guest
// to the host, the host to every guest); each other player gets a 10 s warning and then its own evolution editor (the
// creature stage's request, through the personal editor and the cocoon). The host's own editor ends in the host's
// migration, and the host's migration sends every guest the world (BUG-0508's path), so the party lands at the new nest
// together. A player who is down or dead then gets its editor once it stands again (30 s at most). Off: each game alone.
static bool g_migrate_together = true;
COOP_FIX("creature.migrate_together", "BUG-9163", g_migrate_together,
         "creature: a migration started by any player warns everyone; 10 s later everyone edits and the party migrates together");
static int g_mig_seen = -1;
static double g_mig_editor_at = -1, g_mig_editor_until = -1;
static uint32_t g_mig_started, g_mig_heard, g_mig_editors;
static std::string g_mig_by;
constexpr double kMigrateWarn = 10, kMigrateWait = 30;
// one migration is one round (2026-11-13, live: the MSI's migration warned everyone; each "final touches" editor then
// started that player's own migration and warned everyone again, a second round of editors). A game that was warned
// (or started one) ignores further warnings for kMigrateRound s, and a migration that its warned editor started is
// part of the round: nobody is told again.
constexpr double kMigrateRound = 300;
static double g_mig_round_until = -1;
static bool g_mig_told_editor;
static uint32_t g_mig_ignored;
// 6R A (BUG-9254, 2026-11-18): on a galaxy planet strangers share the world, so "everyone" is the party: a player's
// migration opens the editors of its own party only (each game knows its own party: the host passes every warning on
// and each game checks it). A direct session is one group, as before. Off (`coop.fix creature.migrate_party off`):
// every player of the planet migrates together.
static bool g_migrate_party = true;
COOP_FIX("creature.migrate_party", "BUG-9254", g_migrate_party,
         "creature: on a galaxy planet a migration takes only the migrating player's party into the editor, not strangers");
static bool migrates_with(uint8_t by)
{
    return !g_migrate_party || !parties::galaxy() || parties::same_party(by, g_sess.my_id());
}
static void migration_warned(uint8_t by, double now)
{
    g_mig_by = g_sess.name_of(by);
    g_mig_round_until = now + kMigrateRound;
    g_mig_editor_at = now + kMigrateWarn;
    g_mig_editor_until = now + kMigrateWarn + kMigrateWait;
    ++g_mig_heard;
    feed(g_mig_by + "'s species is migrating to a new nest. " +
         std::string(g_migrate_party && parties::galaxy() ? "Your party migrates together" : "Everyone migrates together") +
         ": your editor opens in 10 s for final touches.");
}
static void send_migrate(uint8_t by, int to /* -1: every guest */, uint8_t skip)
{
    net::Writer w;
    w.u8(1);
    w.u8(by);
    w.u32(g_world);
    if (to >= 0) {
        g_sess.send_reliable_to((uint8_t)to, net::MSG_MIGRATE, w);
        return;
    }
    for (auto& p : g_sess.players())
        if (p.id != g_sess.my_id() && p.id != skip) g_sess.send_reliable_to(p.id, net::MSG_MIGRATE, w);
}
// ---- BUG-9164 (owner decision 4 in Creature, as Tribe's 2M): everyone plays a creature of their own ----
// A guest in Creature that plays the world's (the host's) species, no creature of its own, gets its own evolution editor
// 12 s after its creature appeared (once per world): what it saves becomes its own creature (creations.cpp adopts the new
// creation; its own nest follows, home_nests), so the host's evolutions no longer change it (the owner, 2026-11-12: "became
// the new creature instead of their own"). Leaving the editor without a change keeps the shared species. Off: as before.
static bool g_join_editor = true;
COOP_FIX("creature.join_editor_first", "BUG-9164", g_join_editor,
         "creature: a guest without a creature of their own gets their own editor after joining; what they save is theirs");
static bool g_new_world_editor = true;
COOP_FIX("creature.new_world_editor", "BUG-9203", g_new_world_editor,
         "creature: a guest with an old co-op creature pick still gets its own editor in a world new to it");
static double g_join_since = -1;
static uint32_t g_join_done_world, g_join_editors;
static void join_editor_tick(double now)
{
    if (!g_join_editor || g_sess.role() != net::Role::Client || !g_sess.connected() || g_mode != modes::kCreature ||
        !g_local || !g_world) {
        g_join_since = -1;
        return;
    }
    if (g_join_since < 0) g_join_since = now;
    if (g_join_done_world == g_world || now - g_join_since < 12) return;
    uint32_t got = 0, total = 0; // not while the host's world is on its way (BUG-9170's lesson)
    if (world::progress(got, total) || world::load_queued()) { g_join_since = now; return; }
    if (game_busy() || mode_switch_pending() || creature_rescue::down() || game::is_dead(g_local)) return;
    g_join_done_world = g_world;
    // BUG-9203: an old co-op window pick (it holds everywhere) is no creature of this world yet: in a world new to it the
    // guest gets the editor too, starting from that pick (decision 3T A). Off: a pick skips the editor
    if (creations::plays_own_creature() && !(g_new_world_editor && creations::own_creature_new_here())) return;
    H->run_outside_frame([](void*) {
        const int r = submit_evolution_request();
        LOG("join: own editor for a creature of our own -> %d (BUG-9164)", r);
        if (r < 0) return; // 0: Submit said false, the editor opened all the same (2026-11-13)
        ++g_join_editors;
        feed("Make a creature of your own: what you save is your creature for this game (Done without a change keeps the "
             "current one).");
        notice(g_name + " is making a creature of their own", -1);
    }, nullptr);
}

// ---- BUG-9207 (decision 3U, owner 2026-11-14: "skip 3UA and send guests into the editor while the host is making the
// world while in their editor") ----
// A guest waiting in the galaxy menu while the host is in the creature editor it opened from its galaxy menu (a new
// world) gets the creature editor too, with the base creature (no creation key). The editor's check (its finish
// button, control 0x058A3350, found live 2026-11-14) is greyed until the host's world is here (it waits for the editor,
// world::load_queued) or the host is back in the menu. What the guest saves becomes its creature
// (creations::begin_first_creature), played in that world without another editor. Off: guests wait in the menu.
static bool g_pregame_editor = true;
COOP_FIX("creature.pregame_editor", "BUG-9207", g_pregame_editor,
         "creature: guests make their first creature while the host makes a new world; their check waits for the world");
static bool g_first_before_world = true;
COOP_FIX("creature.first_before_world", "BUG-9208", g_first_before_world,
         "creature: a guest joining a world where it has no creature makes its first one before the world loads");
constexpr uint32_t kEditorCheck = 0x058A3350u;
static bool g_pregame_open;        // this guest's editor is the pre-game one
static double g_first_result_wait = -1; // BUG-9209: left the pre-game editor, its result not read yet (since)
// BUG-9209 (MSI tour 2026-11-15): on one long-used profile the pre-game editor's name field never opened, every time;
// the in-world editor named fine there. The editor comes again once, then the world loads (the in-world editor follows)
static int g_first_retries = 0;
static uint32_t g_late_world = 0;       // BUG-9208: the host's world the late editor was opened for (once)
static bool g_first_retry = true;
COOP_FIX("creature.first_retry", "BUG-9209", g_first_retry,
         "creature: a guest who leaves the first creature's editor without saving gets it again, not the old creature");
static bool g_letterbox_wait = true;
COOP_FIX("creature.editor_after_intro", "BUG-9209", g_letterbox_wait,
         "creature: the guests' first-creature editor waits until the galaxy menu's intro bars are gone");
// the galaxy menu's fly-in shows Spore's cinematic bars (letterbox_top / letterbox_bottom); an editor opened under them
// kept the bars, and they took the clicks meant for the name field (BUG-9209, 2026-11-15)
// BUG-9252: a guest that left a stage for the galaxy menu (to load the host's world) found the bars up with no
// cinematic playing (the menu's intro was not started, BUG-0626) and they stayed: the held world waited for the editor,
// the editor for the bars, forever (the 120 s limit was checked after the bars). On: bars up for 5 s with no cinematic
// are stale and hidden, and the 120 s limit counts while the bars are waited for. Off: the old wait.
static bool g_stale_bars_fix = true;
COOP_FIX("creature.stale_intro_bars", "BUG-9252", g_stale_bars_fix,
         "creature: intro bars left up on the galaxy menu with no cinematic are hidden; the first-creature editor's 120 s limit counts while it waits for them");
static unsigned g_stale_bars_hidden;
static bool hide_stale_bars()
{
    sui::Win main = sui::main_window();
    bool any = false;
    for (const char* cap : {"letterbox_top", "letterbox_bottom"})
        if (sui::Win w = main ? sui::find_caption(main, cap) : nullptr; w && sui::visible(w)) {
            sui::set_visible(w, false);
            any = true;
        }
    return any;
}
static bool letterbox_shown() // (sui::Win)
{
    sui::Win main = sui::main_window();
    for (const char* cap : {"letterbox_top", "letterbox_bottom"}) {
        sui::Win w = main ? sui::find_caption(main, cap) : nullptr;
        if (!w || !sui::visible(w)) continue;
        const sui::Rect r = sui::screen_rect(w);
        if (r.h() > 4 && r.y2 > 0 && r.y1 < sui::area(main).y2) return true;
    }
    return false;
}
static bool g_pregame_start;       // 3Z B: it is the host's start editor with the host's parts list
static bool g_same_start_parts = true;
COOP_FIX("creature.same_start_parts", "BUG-9207", g_same_start_parts,
         "creature: guests get the host's new-world start editor with the host's parts (its diet's mouth); off: the Creator");
static bool g_pregame_check_off;   // we greyed its check
static uint32_t g_pregame_editors; // tests
static void pregame_editor_tick(double now)
{
    if (g_sess.role() == net::Role::Host && !g_host_start_parts.empty()) { // 3Z B: a guest joining during our editor
        const uint32_t m = game::mode_id();
        if (m != modes::kEditor && m != modes::kEdit) {
            g_host_start_parts.clear();
        } else {
            for (auto& p : g_sess.players())
                if (p.id != g_sess.my_id() && !g_host_start_sent.count(p.id)) {
                    net::Writer w;
                    w.u16((uint16_t)g_host_start_parts.size());
                    for (const auto& sp : g_host_start_parts) {
                        w.u32(sp.lo);
                        w.u32(sp.hi);
                        w.u8(sp.state);
                    }
                    g_sess.send_reliable_to(p.id, net::MSG_START_PARTS, w);
                    g_host_start_sent.insert(p.id);
                    LOG("start editor: our parts list to %s, who joined during it (3Z B)", g_sess.name_of(p.id).c_str());
                }
        }
    }
    static uint32_t s_host_before = 0; // the host's last mode outside an editor
    static bool s_done = false;        // opened (or tried) for this editor of the host's
    static double s_check_at = 0;
    const bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    const uint32_t host_mode = guest && g_remotes.count(0) && !g_remotes[0].buf.empty() ? g_remotes[0].buf.latest().mode : 0;
    const bool host_editing = host_mode == modes::kEditor || host_mode == modes::kEdit;
    if (!host_editing) {
        s_host_before = host_mode;
        s_done = false;
    }
    const uint32_t mode = game::mode_id();
    if (g_pregame_open && mode != modes::kEditor && mode != modes::kEdit && mode != modes::kLoad &&
        !mode_switch_pending()) {
        g_pregame_open = g_pregame_check_off = false; // left the editor
        // BUG-9209: the world waits until the editor's result is read (a creature saved, or none: then the late path
        // below opens the base creature's editor again instead of loading the world with the old creature)
        if (g_first_retry && creations::first_creature_pending()) {
            world::hold_for_editor(true);
            g_first_result_wait = now;
        } else
            world::hold_for_editor(false); // BUG-9208: the held world loads now
        return;
    }
    if (g_first_result_wait >= 0) {
        if (creations::first_creature_pending() && now - g_first_result_wait < 40) return;
        g_first_result_wait = -1;
        world::hold_for_editor(false); // a creature was saved (or 40 s): the late path decides with it
        const bool made = creations::first_creature_made();
        const bool again = !made && g_first_retries < 1;
        if (made) g_first_retries = 0;
        if (again) { // none: the base creature's editor again (the late path), not the old creature
            g_late_world = 0;
            ++g_first_retries;
        }
        LOG("pregame editor: its result read: %s (BUG-9209)",
            made ? "a creature" : again ? "none saved, the editor again" : "none saved again, the world loads (its editor follows there)");
    }
    if (g_pregame_open) { // the check waits for the host's world (or the host's return to the menu, or no host)
        if (now - s_check_at < 0.25) return;
        s_check_at = now;
        const bool wait = guest && host_editing && !world::load_queued();
        if (wait == g_pregame_check_off) return;
        if (sui::Win check = sui::find(sui::main_window(), kEditorCheck)) {
            sui::set_enabled(check, !wait);
            g_pregame_check_off = wait;
            LOG("pregame editor: the check %s (BUG-9207)", wait ? "waits for the host's world" : "works");
            if (!wait && world::load_queued())
                feed(g_sess.name_of(0) + "'s world is ready: press the check to join them.");
        }
        return;
    }
    // BUG-9208 (owner 2026-11-14: "It should bring up the starting base creature ... They shouldn't be sharing their
    // creatures or have family at their nest until they finish creating their first creature"): a guest joining from
    // the galaxy menu while the host plays Creature in a world where this guest has no creature of its own makes it
    // here first; the host's world (arriving or arrived) is held for the editor and loads when it is left, so no old
    // body, family, nest or cocoon appears in the world before. Once per world. Switch: creature.first_before_world.
    static double s_late_at = -1; // the world is held for the editor since (-1: not waiting)
    const uint32_t host_world = guest && g_remotes.count(0) && !g_remotes[0].buf.empty() ? g_remotes[0].buf.latest().world : 0;
    if (!g_pregame_editor || !guest || mode != modes::kMenu || g_local) {
        if (s_late_at >= 0) { s_late_at = -1; world::hold_for_editor(false); }
        return;
    }
    if (g_first_before_world && s_late_at < 0 && host_mode == modes::kCreature && host_world && host_world != g_late_world &&
        creations::needs_first_creature(host_world)) {
        g_late_world = host_world;
        s_late_at = now;
        world::hold_for_editor(true); // at once: the world arriving now waits for the editor
        LOG("pregame editor: no creature of our own in the host's world %08X: it waits for our first creature (BUG-9208)",
            host_world);
    }
    if ((game_busy() & busy::kEditorGate) || mode_switch_pending()) return; // (a held world counts as loading)
    static double s_bars_since = -1; // BUG-9252: the bars up with no cinematic since
    if (g_letterbox_wait && letterbox_shown()) { // BUG-9209: not under the galaxy intro's bars
        const bool cine = world::cinematic_playing() || world::menu_intro_playing();
        if (!g_stale_bars_fix || cine) {
            s_bars_since = -1;
            return;
        }
        if (s_bars_since < 0)
            s_bars_since = now;
        if (s_late_at >= 0 && now - s_late_at > 120) { // (the limit below, also while the bars are waited for)
            s_late_at = -1;
            s_bars_since = -1;
            world::hold_for_editor(false);
            LOG("pregame editor: no editor in 120 s (stale intro bars): the world is not held any more (BUG-9252)");
            return;
        }
        if (now - s_bars_since < 5)
            return;
        s_bars_since = -1;
        if (hide_stale_bars()) {
            ++g_stale_bars_hidden;
            LOG("pregame editor: the intro bars were up 5 s with no cinematic: hidden (BUG-9252)");
        }
        return; // (the editor opens on a later tick, without the bars)
    }
    s_bars_since = -1;
    uint32_t got = 0, total = 0;
    const bool receiving = world::progress(got, total);
    bool late = false;
    if (s_late_at >= 0) {
        // the editor opens once the world is here and held (opened while the world was written into the slot, its 3D
        // view stayed black, 2026-11-14)
        if (now - s_late_at > 120) { // no world came
            s_late_at = -1;
            world::hold_for_editor(false);
            LOG("pregame editor: no world in 120 s: not held any more (BUG-9208)");
            return;
        }
        if (receiving || !world::load_queued() || world::load_starting()) return;
        s_late_at = -1;
        late = true;
        // 3Z B: the host's start editor's parts known (a new world, the editor again after none was saved): the same
        g_pregame_start = g_same_start_parts && g_start_parts_at >= 0 && now - g_start_parts_at < 900;
    } else {
        const bool host_from_menu = s_host_before == modes::kMenu || (s_host_before == 0 && host_world == g_world);
        if (s_done || !host_editing || !host_from_menu || world::load_queued() || world::load_starting() || receiving) return;
        const uint32_t he = g_remotes[0].buf.latest().editor; // creatures only: the Creator, or the stage start editor (a new
        if (he != 0x465C50BAu && he != kCreatureStartEditor) return; // world from the galaxy wizard, 2026-11-14)
        if (g_join_start_editor && he != kCreatureStartEditor) return; // BUG-9263: not with the Creator (the late path)
        // 3Z B: the host's start editor: the same, with the host's parts list (sent at its request; a host without it,
        // an older one: the Creator after 4 s)
        static double s_waited = -1;
        if (s_waited < 0) s_waited = now;
        const bool parts = g_start_parts_at >= 0 && now - g_start_parts_at < 600;
        if (he == kCreatureStartEditor && !parts && now - s_waited < 4) return;
        s_waited = -1;
        g_pregame_start = he == kCreatureStartEditor && parts && g_same_start_parts;
        s_done = true;
    }
    H->run_outside_frame([](void* late) {
        if (!late && (world::load_starting() || game::mode_id() != modes::kMenu)) { // BUG-9240: a load began meanwhile
            LOG("pregame editor: not opened: the host's world is loading (BUG-9240)");
            return;
        }
        creations::begin_first_creature();
        const bool r = g_pregame_start       ? submit_new_creature_request(kCreatureStartEditor, &g_start_parts)
                       : g_join_start_editor ? submit_new_creature_request(kCreatureStartEditor) // BUG-9263
                                             : submit_new_creature_request();
        LOG("pregame editor: the creature editor with the base creature %s -> %d (BUG-%s)",
            late ? "before the host's world (no own creature there)" : "while the host makes a new world", (int)r,
            late ? "9208" : "9207");
        if (!r && game::mode_id() != modes::kEditor) {
            creations::cancel_first_creature();
            world::hold_for_editor(false);
            return;
        }
        g_pregame_open = true;
        ++g_pregame_editors;
        if (late)
            feed("Make your creature for " + g_sess.name_of(0) + "'s world: you join it with what you save.");
        else
            feed(g_sess.name_of(0) + " is making a new world: make your creature now. The check unlocks when the world is "
                                     "ready, and what you save is your creature there.");
        notice(g_name + " is making their creature", -1);
    }, late ? (void*)1 : nullptr);
}

// BUG-9170: the Civ late joiner's editor (tried every 3 s, 10 times a world, while the game is busy)
static uint32_t g_civ_editor_world, g_civ_editor_opened_world, g_civ_editors;
static int g_civ_editor_tries;
static double g_civ_editor_at = -1e9;
static void civ_editor_first_tick(double now)
{
    // not before the host's world is here: right after the join the guest was still in its own copy (the same world
    // name) and the editor opened before the host's world came (2026-11-13); 15 s connected, no transfer, no load
    static double s_connected_at = -1;
    if (!g_sess.connected()) s_connected_at = -1;
    else if (s_connected_at < 0) s_connected_at = now;
    uint32_t got = 0, total = 0;
    if (s_connected_at < 0 || now - s_connected_at < 15 || world::progress(got, total) || world::load_queued()) return;
    if (!g_civ_editor_first || g_mode != modes::kCiv || g_sess.role() != net::Role::Client || !g_sess.connected()) return;
    if (g_civ_editor_world != g_world) {
        g_civ_editor_world = g_world;
        g_civ_editor_tries = 0;
    }
    if (g_civ_editor_opened_world == g_world || g_civ_editor_tries >= 10 || now - g_civ_editor_at < 3) return;
    static double s_said = -100; // support log: why not (every 20 s while it waits, 10 times)
    static int s_says = 0;
    if ((civ::late_prompt() != 1 || game_busy() || mode_switch_pending()) && now - s_said > 20 && s_says < 10) {
        s_said = now;
        ++s_says;
        LOG("civ: editor first waits: late_prompt=%d own_creature=%d busy=%u switch=%d (BUG-9170)", civ::late_prompt(),
            (int)creations::plays_own_creature(), game_busy(), (int)mode_switch_pending());
    }
    if (civ::late_prompt() != 1 || game_busy() || mode_switch_pending()) return;
    g_civ_editor_at = now;
    ++g_civ_editor_tries;
    H->run_outside_frame([](void*) {
        if (!late_create_offered()) return;
        const std::string said = open_new_creature_editor();
        if (game::mode_id() == modes::kEditor || said.rfind("Make your creature", 0) == 0) {
            g_civ_editor_opened_world = g_world;
            ++g_civ_editors;
        }
        LOG("civ: a late joiner without a creature of their own: %s (BUG-9170)", said.c_str());
        feed(said);
    }, nullptr);
}

static void migration_tick(double now)
{
    // the migration starts on the return from the editor (CreatureGame_AfterEditor): the state seen before the editor is
    // kept through it, else the 0 -> 1 was never seen (2026-11-13: a guest's game at state 1, nobody told)
    // a load (another copy of the world, its saved state) starts over: game::stage_epoch moves with a load, not an editor
    static uint32_t seen_world, seen_epoch;
    const uint32_t epoch = game::stage_epoch(modes::kCreature);
    if (seen_world != g_world || seen_epoch != epoch) {
        seen_world = g_world;
        seen_epoch = epoch;
        g_mig_seen = -1;
    }
    if (g_mode == modes::kEditor || g_mode == modes::kEdit)
        return;
    if (!g_migrate_together || !g_sess.connected() || g_mode != modes::kCreature) {
        g_mig_seen = -1;
        if (g_mode != modes::kCreature) g_mig_editor_at = -1;
        return;
    }
    const int mig = host_migration();
    if (g_mig_seen == 0 && mig == 1 && (g_mig_told_editor || now < g_mig_round_until)) {
        ++g_mig_started;
        g_mig_editor_at = -1;
        g_mig_told_editor = false;
        LOG("migration: this game's migration is part of the party's round (by %s): nobody told again (BUG-9163)",
            g_mig_by.c_str());
    } else if (g_mig_seen == 0 && mig == 1) { // this game's own evolution started a migration
        ++g_mig_started;
        g_mig_editor_at = -1; // its own editor is done
        g_mig_round_until = now + kMigrateRound;
        g_mig_by = g_name;
        const bool party_only = g_migrate_party && parties::galaxy(); // BUG-9254: the party, not the planet
        if (party_only && parties::my_party().size() <= 1)
            LOG("migration: started by this game's evolution, no party here: nobody else migrates (BUG-9254)");
        else {
            if (g_sess.role() == net::Role::Host) {
                send_migrate(g_sess.my_id(), -1, 0xFF);
                if (!party_only) notice(g_name + "'s species is migrating to a new nest: everyone migrates together.", -1);
            } else
                send_migrate(g_sess.my_id(), 0, 0xFF);
            LOG("migration: started by this game's evolution: the others are told (BUG-9163)");
        }
    }
    if (mig >= 0) g_mig_seen = mig;
    if (g_mig_editor_at < 0 || now < g_mig_editor_at) return;
    if (now > g_mig_editor_until) { // never stood again in time
        LOG("migration: no editor for this player (busy, down or dead for %0.f s, BUG-9163)", kMigrateWait);
        g_mig_editor_at = -1;
        return;
    }
    if (!g_local || game::is_dead(g_local) || creature_rescue::down() || game_busy() || mode_switch_pending()) {
        g_mig_editor_at = now + 1;
        return;
    }
    g_mig_editor_at = -1;
    g_mig_told_editor = true;
    H->run_outside_frame([](void*) {
        const int r = submit_evolution_request();
        if (r >= 0) ++g_mig_editors; // 0: Submit said false, the editor opens all the same (BUG-9164)
        LOG("migration: own editor for the final touches -> %d (BUG-9163)", r);
    }, nullptr);
}
static int g_evo_migration_before = -1;
static uint32_t g_evo_seen_key = 0; // host: the avatar's species, age and migration in the Creature stage
static int g_evo_seen_age = -1, g_evo_seen_mig = -1;
static double g_evo_seen_at = 0;
static unsigned g_evo_saves = 0; // host: saves made for an evolution in place (tests)
static bool g_evo_save_due = false;
// BUG-0517: after an evolution editor the stage's return (0xD43DE0) reloads the avatar's species profile
// in place with the new species (0x4DFBC0: the same profile object, its key +0x504 the new one, the world's
// player species +0xA4 too) and calls vt 0xAC on the avatar and its own-species creatures, but their own
// species key (+0xB28) stays the pre-editor one until the game is saved: every reader of +0xB28 (the
// mod's trace and state, the relations of GetRelationship) saw the old species for minutes on a host that
// did not save (docs/re/creations.md#creations-evolve-key). The key of every creature whose profile is
// the avatar's and whose key is the pre-editor one is set to the profile's, once, within 60 s after the
// editor; then the host's save of BUG-0508 is not needed. Any role, also alone. Off: the save (0.7.4).
static bool g_evo_key_fix = true;
COOP_FIX("creature.evolve_key", "BUG-0517", g_evo_key_fix, "creature: after an evolution editor the own creature takes the new species at once (no save needed)");
static double g_evo_key_until = 0;   // watch after an editor (Creature)
static uint32_t g_evo_keys_set = 0;  // tests: creatures given the new key
static bool g_evo_key_done = false;  // this evolution's keys were set (the host's save is skipped)
static void evo_key_tick(double now);
static bool g_evo_save_test_off = false; // tests (BUG-0517): the host's in-place save is not made
static int host_avatar_age()
{
    void* av = game::mode_id() == modes::kCreature ? game::avatar() : nullptr;
    return av && skg::readable((uint8_t*)av + 0xB34, 4) ? *(int*)((uint8_t*)av + 0xB34) : -1;
}
static const std::string* peer_version(uint8_t id)
{
    for (auto& p : g_sess.players())
        if (p.id == id)
            return &p.version;
    return nullptr;
}
// host: may guest r take the host's evolution in place (same world, the host's stage or an editor)?
static bool evolve_inplace_ok(uint8_t id, const Remote& r, uint32_t stage)
{
    // (called from on_mode_change too, before g_adapter / g_world follow the new mode)
    const modes::Adapter* ad = modes::for_mode(stage);
    uint32_t world = ad ? modes::current_place(ad) : 0;
    if (!g_evolve_inplace_fix || r.echo || r.buf.empty() || !ad || ad->own_worlds)
        return false;
    const std::string* v = peer_version(id);
    if (!v || v->empty())
        return false; // an older SporeCoop: it does not follow the host's species in place
    const auto& ls = r.buf.latest();
    bool editor = ls.mode == modes::kEdit || ls.mode == modes::kEditor;
    return (ls.mode == stage || editor) && world && ls.world == world;
}

// BUG-0941: a host that loads another Space save from the galaxy menu sent no world to its connected guests: the
// stage is the same and in Space the state's world is the place (star, planet), so nothing told the guests the save
// was another one; they played on in their copy of the old save. The host's save file (world::current_game_key: the
// slot folder and the planet file) when it entered Space, and when it left Space for the menu, is compared with the
// one it loads into Space: another one is shared with everybody as a new stage is. `coop.fix space.load_follow off`:
// no share (0.7.12; BUG-0933 then holds the shared money on the guests).
static bool g_space_load_follow_fix = true;
COOP_FIX("space.load_follow", "BUG-0941", g_space_load_follow_fix,
         "space: guests follow the host into another Space save it loads");
static std::string g_space_key; // the host's Space save as last seen at a mode change
static uint32_t g_space_load_shares;

void on_mode_change(uint32_t from, uint32_t to)
{
    static uint32_t last_stage_world;
    if (from == modes::kTribe || from == modes::kCreature) creature_hp::clear_enemy_health();
    if (from == modes::kSpace) squadron::clear_enemy_stats();
    if (from == modes::kSpace && to == modes::kMenu) { // BUG-0941: the save it leaves (as written by the quit)
        std::string k = world::current_game_key();
        if (!k.empty())
            g_space_key = k;
    }
    if (to == modes::kLoad || to == modes::kMenu || (stage_mode(to) && to != modes::kTribe))
        g_last_editor_req = 0; // (BUG-0849: the end of Tribe is over; a Tribe game loaded later starts clean)
    if (to == modes::kMenu && stage_mode(from) && g_sess.role() == net::Role::Host && !g_remotes.empty())
        feed("the others play on alone until you load a game again (they get its world)");
    bool editor = to == modes::kEdit || to == modes::kEditor;
    if (editor && stage_mode(from)) {
        g_was_in_editor = true;
        // BUG-0508: what the evolution changes, as last seen in the stage (the avatar is gone by now)
        g_evo_key_before = from == modes::kCreature ? g_evo_seen_key : 0;
        g_evo_age_before = from == modes::kCreature ? g_evo_seen_age : -1;
        g_evo_migration_before = from == modes::kCreature ? g_evo_seen_mig : -1;
    }
    if (!editor && (from == modes::kEdit || from == modes::kEditor)) {
        g_own_editor = false;
        g_editor_left_at = H->time_seconds(); // BUG-0404: the evolution's cinematic starts now
    }
    if (!stage_mode(to))
        return;
    bool new_stage = g_last_stage && to != g_last_stage;
    bool evolved = g_was_in_editor && !new_stage;
    g_was_in_editor = false;
    if (evolved && to == modes::kCreature) { // BUG-0517: any role, also without a session
        g_evo_key_until = H->time_seconds() + 60.0;
        g_evo_key_done = false;
    }
    uint32_t last = g_last_stage;
    g_last_stage = to;
    const auto stage_world = modes::current_world();
    if (new_stage && stage_world && stage_world == last_stage_world && g_sess.role() == net::Role::Host)
        world::request_autosave();
    last_stage_world = stage_world;
    bool other_save = false; // BUG-0941
    if (to == modes::kSpace && from != modes::kEdit && from != modes::kEditor && from != modes::kScenario) {
        std::string k = world::current_game_key();
        other_save = g_space_load_follow_fix && last == modes::kSpace && !new_stage && !evolved && !k.empty() &&
                     !g_space_key.empty() && k != g_space_key;
        if (other_save)
            LOG("space: the host loaded another Space save (%s, was %s): it goes to everybody (BUG-0941)", k.c_str(),
                g_space_key.c_str());
        if (!k.empty())
            g_space_key = k;
    }
    if (to == modes::kTribe && tribal_graduation_pending()) return;
    if (g_sess.role() != net::Role::Host || !g_share_world || g_remotes.empty() || !last)
        return;
    if (!new_stage && !evolved && !other_save)
        return;
    if (other_save)
        g_space_load_shares++;
    if (evolved && tribe_graduating()) { // BUG-0849: no evolution; the Civ stage comes next and is shared then
        LOG("back from the editor %08X in the end of Tribe: no share (the Civilization stage is shared)", g_last_editor_req);
        notice(g_name + " made a design for the Civilization stage: it comes next", -1);
        return;
    }
    g_evo_cine = false;
    g_evo_save_due = evolved;
    for (auto& [id, r] : g_remotes)
        if (!r.echo) {
            // Cell has no nest migration or terrain editor. The creation stream
            // already updates the partner's body; keep a fresh, joined peer in
            // this pool instead of replacing its world for every editor visit.
            const bool cell_editor = g_cell_editor_share_fix && evolved && to == modes::kCell;
            if (cell_editor && r.joined_phase && !r.needs_world && !r.buf.empty() &&
                r.buf.age(H->time_seconds()) < 5.0) {
                const auto& peer = r.buf.latest();
                const uint32_t current = modes::current_world();
                if (current && peer.world == current && (peer.mode == to || peer.mode == modes::kEdit || peer.mode == modes::kEditor)) {
                    LOG("world: Cell editor return keeps player %u in the current pool; creation sync updates designs",id);
                    continue;
                }
            }
            r.needs_world = true; // sent by follow_phase as soon as the host controls something
            r.cine_wait_since = 0; // a new wait (BUG-0232)
            r.evo_inplace = evolved && evolve_inplace_ok(id, r, to); // BUG-0508: decided after the wait
            r.evo_check_until = 0;
        }
    g_share_due_at = H->time_seconds() + (evolved ? 3.0 : 0.0); // BUG-0019: its cinematic starts first
    if (new_stage && last == modes::kCreature && to == modes::kTribe)
        world::host_reload_after_share(); // BUG-0332: one ground for everybody (world.cpp tribe.host_reload)
    notice(new_stage            ? g_name + " entered the " + modes::mode_name(to) + " stage: everybody follows"
           : other_save         ? g_name + " loaded another game: everybody follows"
           : design_stage(to) ? g_name + " changed a design: everybody gets the new designs"
           // BUG-0588: the City Hall and vehicle editors of the end of Tribe are no evolution
           : to == modes::kTribe && (tribe::stage_end() || tribe_civ_editor(g_last_editor_req)) ? g_name + " made a design for the Civilization stage: it comes next"
                              : g_name + " evolved the species: everybody gets the new body",
           -1);
}

static void evo_key_tick(double now)
{
    if (!g_evo_key_fix || g_evo_key_until <= 0)
        return;
    if (now > g_evo_key_until) {
        g_evo_key_until = 0;
        return;
    }
    void* av = g_mode == modes::kCreature ? game::avatar() : nullptr;
    if (!av)
        return;
    uint8_t* prof = *(uint8_t**)((uint8_t*)av + 0xB20);
    if (!prof || IsBadReadPtr(prof + 0x504, 12))
        return;
    skg::ResourceKey nk = *(skg::ResourceKey*)(prof + 0x504);
    skg::ResourceKey ok = *(skg::ResourceKey*)((uint8_t*)av + game::kSpeciesKeyOff);
    if (!nk.instance || !memcmp(&nk, &ok, sizeof nk))
        return; // not applied yet (the stage's return), or nothing to do
    if (g_evo_key_before && ok.instance != g_evo_key_before)
        return; // the avatar is not the pre-editor species (a swap of the mod's): not ours to touch
    struct U {
        uint8_t* prof;
        skg::ResourceKey ok, nk;
        uint32_t n;
    } u{prof, ok, nk, 0};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            uint8_t* c = (uint8_t*)o;
            if (noun != 0x18EB45Eu || !game::object_alive(o) || IsBadReadPtr(c, 0xB34))
                return true;
            skg::ResourceKey* k = (skg::ResourceKey*)(c + game::kSpeciesKeyOff);
            if (*(uint8_t**)(c + 0xB20) == u->prof && !memcmp(k, &u->ok, sizeof u->ok)) {
                *k = u->nk;
                u->n++;
            }
            return true;
        },
        &u);
    g_evo_keys_set += u.n;
    g_evo_key_done = true;
    g_evo_key_until = 0;
    LOG("evolved: the avatar's species key %08X -> %08X (its profile's, %u creature(s), BUG-0517)", ok.instance, nk.instance,
        u.n);
}

// BUG-0231: a share that failed in world::tick ("cannot share the world: ...", after the save) was
// never retried in that phase: follow_phase / player_join had already set world_sent, so a guest
// in another world of the host's stage stayed there for good. Retried at most 3 times per phase
// and guest, 30 s apart; then the host is told to use coop.share. A "planet file missing" failure
// is not retried (world.cpp already saved 3 times). Off: as in 0.7.3.0.
bool g_share_retry_fix = true;
COOP_FIX("core.share_retry", "BUG-0231", g_share_retry_fix, "core: a failed world share is retried (3 times, 30 s apart)");
uint32_t g_join_serial = 0;   // host: counts joins (Remote::join_serial)
uint32_t g_share_retries = 0; // tests
constexpr int kShareRetries = 3;
constexpr double kShareRetryGap = 30.0;

void share_failed(uint8_t peer, uint32_t tag, bool planet_missing)
{
    if (!g_share_retry_fix || planet_missing || !tag || g_sess.role() != net::Role::Host)
        return;
    auto it = g_remotes.find(peer);
    if (it == g_remotes.end() || it->second.join_serial != tag)
        return; // the player left, or its id belongs to a newcomer by now (BUG-0166)
    Remote& r = it->second;
    uint64_t key = ((uint64_t)g_mode << 32) | modes::current_world();
    if (r.share_try_key != key) {
        r.share_try_key = key;
        r.share_tries = 0;
    }
    if (r.share_tries >= kShareRetries) {
        feed("cannot share the world with " + r.name + ": use coop.share");
        return;
    }
    r.share_tries++;
    g_share_retries++;
    r.retry_at = H->time_seconds() + kShareRetryGap;
    r.world_sent = 0; // follow_phase sends it again
    if (r.share_at_join) {
        r.needs_world = true;
        r.cine_wait_since = 0; // a new wait (BUG-0232)
    }
    LOG("share for %s failed: retry %d of %d in %.0f s", r.name.c_str(), r.share_tries, kShareRetries, kShareRetryGap);
}

// BUG-0382: after Civ -> Space every guest stayed in its Civ world for ~90 s (campaign_space, the
// monitor's stage_apart) and nobody said why: the host's share waits for the stage's cinematic
// (BUG-0019) and then for the host's ship to leave the home planet (the Space stage saves only off a
// planet, docs/07-modes.md 7.2). The host now tells everybody once per wait that the world goes out
// when its ship leaves the planet, and its trace line says why the share waits (`share_wait=planet`
// or `cinematic`, read by the monitor). Off: only the host's own feed line (0.7.3.3).
static bool g_share_wait_fix = true;
COOP_FIX("world.share_wait_notice", "BUG-0382", g_share_wait_fix,
         "world: everybody is told that the host's world goes out when its ship leaves the planet; the trace says why a share waits");
static double g_share_cine_held_at = -1e9; // host: a share was held for a cinematic (follow_phase)
static bool g_planet_wait_told;
// host: why a share waits now ("planet", "cinematic"), nullptr when none does
static const char* share_wait_reason()
{
    if (!g_share_wait_fix || g_sess.role() != net::Role::Host)
        return nullptr;
    if (world::share_waits_for_planet())
        return "planet";
    return H->time_seconds() - g_share_cine_held_at < 2.0 ? "cinematic" : nullptr;
}
static void share_wait_tick()
{
    bool planet = g_share_wait_fix && g_sess.role() == net::Role::Host && world::share_waits_for_planet();
    if (planet && !g_planet_wait_told)
        notice(g_name + "'s world goes to the others when " + g_name +
                   "'s ship leaves the planet (the Space stage saves only in a solar system or the galaxy)",
               -1);
    g_planet_wait_told = planet;
}

// The host decides when a phase ends: when the host plays a stage and a guest stays in another
// stage (or its editor) for a few seconds, the host world is sent again and the guest loads it.
void follow_phase(double now)
{
    if (g_sess.role() != net::Role::Host || !g_share_world || !g_adapter || g_adapter->own_worlds ||
        !stage_mode(g_mode))
        return;
    uint64_t key = ((uint64_t)g_mode << 32) | modes::current_world();
    if (g_mode == modes::kCreature && g_local && now - g_evo_seen_at > 0.5) { // BUG-0508
        g_evo_seen_at = now;
        g_evo_seen_key = game::species_instance(g_local);
        g_evo_seen_age = host_avatar_age();
        g_evo_seen_mig = host_migration();
    }
    for (auto& [id, r] : g_remotes) {
        if (r.echo)
            continue;
        if (r.retry_at && now < r.retry_at)
            continue; // BUG-0231: a failed share is retried 30 s later
        if (r.needs_world && g_local && world::offer_launch(g_sess,id,r.join_serial)) continue;
        if (r.needs_world && g_local) { // joined while the host was in a menu
            if (g_share_cine_fix) { // BUG-0019: after the evolution cinematic, at most 120 s
                double& since = g_share_cine_each_fix ? r.cine_wait_since : g_share_cine_since;
                // the busy gate's share mask (busy.h kShareGate: the cinematic, not a pause, BUG-0404)
                unsigned gate = game_busy() & busy::kShareGate;
                bool cine = gate != 0;
                bool waited = since != 0 && cine;
                if (share_wait::hold(since, cine, now, g_share_due_at)) {
                    if (cine) {
                        g_share_cine_held_at = now; // BUG-0382: trace share_wait=cinematic
                        g_evo_cine = true; // BUG-0508: this evolution plays a cinematic: the world goes
                    }
                    continue;
                }
                if (waited)
                    LOG("share: the game is busy for 120 s (%s), the world is sent anyway", busy::names(gate).c_str());
            } else
                g_share_cine_since = 0;
            r.cine_wait_since = 0;
            if (r.evo_inplace) { // BUG-0508: the evolution in place, unless it needs the world after all
                r.evo_inplace = false;
                // (every Creature evolution plays a cinematic after the editor, ~10 s, and the host's avatar
                // takes the new species only after it: lab 20261103-173315; the migration is what the
                // guest's game cannot get without the world, BUG-0019)
                int age = host_avatar_age(), mig = g_mode == modes::kCreature ? host_migration() : -1;
                const char* why = !evolve_inplace_ok(id, r, g_mode)                      ? "left the world or stage"
                                  : g_mode == modes::kCreature && mig < 0                  ? "the migration state is unknown"
                                  : mig > 0 && mig != g_evo_migration_before              ? "a migration started"
                                                                                         : nullptr;
                // (the age is no sign: the evolved avatar is a hatchling, age 0, for a few seconds after the
                // cinematic of every evolution, lab 20261103-173652)
                if (!why) {
                    // The host's own avatar takes the evolved species only with a save of the game: without
                    // the share's save it stayed the pre-editor species for minutes while the world's player
                    // species (cSpeciesManager +0xA4) was the new one (lab 20261103-173652). The save is made,
                    // only not sent (once per evolution, docs/re/creations.md#creations-evolve-in-place).
                    if (g_mode == modes::kCreature && g_evo_save_due && g_evo_key_fix && g_evo_key_done) {
                        g_evo_save_due = false; // BUG-0517: the avatar has its new species already
                        LOG("evolved in place: no save needed (the avatar's species key was set, BUG-0517)");
                    }
                    if (g_mode == modes::kCreature && g_evo_save_due && g_evo_save_test_off) {
                        g_evo_save_due = false;
                        LOG("evolved in place: the save is off for a test (coop.evokey nosave)");
                    }
                    if (g_mode == modes::kCreature && g_evo_save_due) {
                        g_evo_save_due = false;
                        std::string err;
                        if (world::save_game(err))
                            g_evo_saves++;
                        else
                            LOG("evolved in place: the save for the host's new body failed: %s", err.c_str());
                    }
                    r.needs_world = false;
                    r.world_sent = key;
                    r.joined_phase = true;
                    r.evo_check_until = g_mode == modes::kCreature && g_evo_key_before ? now + 30.0 : 0;
                    g_evo_inplace++;
                    LOG("evolved in place: %s keeps its world (host species %08X -> %08X, age %d -> %d, migration %d -> %d, cinematic %d)",
                        r.name.c_str(), g_evo_key_before,
                        g_mode == modes::kCreature && game::avatar() ? game::species_instance(game::avatar()) : 0,
                        g_evo_age_before, age, g_evo_migration_before, mig, (int)g_evo_cine);
                    feed(r.name + " gets your new body in place (no reload)");
                    continue;
                }
                LOG("evolved: %s gets the world (%s)", r.name.c_str(), why);
            }
            r.evo_check_until = 0;
            r.needs_world = false;
            r.world_sent = key;
            r.joined_phase = false;
            r.share_at_join = true; // (a retry of it waits for needs_world again)
            std::string err;
            if (world::share_with(id, err, g_save_on_share, r.join_serial))
                feed("sending the world to " + r.name);
            else {
                feed("cannot share the world: " + err);
                share_failed(id, r.join_serial, false);
            }
            continue;
        }
        if (r.buf.empty())
            continue;
        const auto& ls = r.buf.latest();
        // BUG-0508: a guest kept in place whose avatar still plays the host's pre-editor species: the
        // apply failed there (or its switch is off): it gets the world after all
        if (r.evo_check_until) {
            if (ls.mode == modes::kCreature && ls.species && ls.species != g_evo_key_before)
                r.evo_check_until = 0; // it follows (or plays a creature of its own)
            else if (now > r.evo_check_until && ls.mode == modes::kCreature) {
                r.evo_check_until = 0;
                g_evo_fallback++;
                LOG("evolved in place: %s still plays %08X after 30 s: sending the world", r.name.c_str(), ls.species);
                r.needs_world = true;
                r.cine_wait_since = 0;
                continue;
            }
        }
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
        r.share_at_join = false;
        std::string err;
        if (world::share_with(id, err, g_save_on_share, r.join_serial))
            feed((left_to_menu || other_world ? "back to the world: sending it to " : "next phase: sending the world to ") + r.name);
        else {
            feed("cannot share the world: " + err);
            share_failed(id, r.join_serial, false);
        }
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
            g_sess.lobby_scaling = world::rules_for_current().difficulty_percent;
            // BUG-0776: discovery queries from a subnet of this machine's adapters are answered too (a virtual LAN
            // on public numbers); the list comes from the background lookup (adapters.cpp, BUG-0286)
            static uint32_t bc_gen = ~0u;
            if (adapters::generation() != bc_gen || g_sess.lan_broadcasts.empty()) {
                bc_gen = adapters::generation();
                g_sess.lan_broadcasts.clear();
                for (auto& a : local_addresses())
                    if (a.broadcast != 0xFFFFFFFFu && a.broadcast != 0)
                        g_sess.lan_broadcasts.push_back(a.broadcast);
            }
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
void mate_hearts_tick(double now);

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

// The guest's own editor after the vote opens once its stage has settled. At the start of the
// Creature stage the world arrives a few seconds before the vote ends; an editor opened 4 s after
// that load came up without its interface (no parts, no buttons, cancel did nothing) and the guest
// was stuck in it (coop_editor_desync.py, 2026-10-24, also with 5 s of waiting). The planet showed
// 6-15 s after the avatar appeared (the screen stayed black before). It waits until the avatar has
// played for 15 s and no cinematic or caption is up, at most 90 s.
uint32_t g_own_editor_due = 0;
double g_own_editor_due_at = 0;
double g_own_editor_due_first = 0; // (BUG-0404: the 90 s may be pushed by pauses, not past 300 s)
bool g_own_editor_queued = false;
unsigned g_own_editor_waits; // tests: frames the own editor waited for the stage to settle
double g_own_editor_hold_until = 0; // tests (coop.vote hold <s>): the own editor waits until then
void* g_settle_avatar = nullptr;
double g_settle_since = 0;
bool pause_ui_up();

// BUG-0920: the game's "an editor of the stage is pending" flag left set with no editor coming. The creature
// stage's evolution request (its message handler 0xD42E50, the mating call) sets the byte 0x1686AF0 after
// EditorRequest::Submit whatever Submit answered, and only the next mode switch clears it (0xB5F19D). While it is
// set the frame function 0xB5EAE0 skips the stage's own drawing: the world and the whole game HUD are gone, the
// screen is black with only the mod's overlay, for good (60 s, input and F9 change nothing; the byte written back
// to 0 brings the picture back within a frame). A request the mod's Submit hook refuses and never submits later
// (creature.mode host: a guest's own evolution request, 0.7.12) left the guest like that; a held request that
// is dropped, or one the game's own Submit refuses, would too. On: when the flag is set in a stage and nothing
// will open an editor (no vote, no held or queued request, no own editor due, no mode switch pending, no request
// submitted in the last 15 s), it is cleared: at once after a refusal, else after 1 s of that state. Off: as before.
bool g_evo_flag_fix = true;
COOP_FIX("core.evo_flag_clear", "BUG-0920", g_evo_flag_fix,
         "core: an editor request that is refused or dropped does not leave the stage's screen black (the game's editor-pending flag is cleared)");
unsigned g_evo_flags_cleared = 0; // coop.status, tests
double g_evo_flag_idle_since = 0;
int evo_flag_now()
{
    uintptr_t f = H->sym("SporeCoop::CreatureEvolutionPending_flag");
    return f ? *(uint8_t*)f : -1;
}
void evo_flag_tick(double now)
{
    bool refused = g_evo_flag_refused;
    g_evo_flag_refused = false;
    uintptr_t f = g_evo_flag_fix ? H->sym("SporeCoop::CreatureEvolutionPending_flag") : 0;
    if (!f || !*(uint8_t*)f || !stage_mode(game::mode_id()) || g_vote.active || g_submit_later || g_submit_queued ||
        g_own_editor_due || g_own_editor_queued || mode_switch_pending() || now - g_editor_submitted_at < 15.0) {
        g_evo_flag_idle_since = 0;
        return;
    }
    if (!refused) { // not the mod's own refusal: only after the state held for a second
        if (!g_evo_flag_idle_since)
            g_evo_flag_idle_since = now;
        if (now - g_evo_flag_idle_since < 1.0)
            return;
    }
    *(uint8_t*)f = 0;
    g_evo_flag_idle_since = 0;
    g_evo_flags_cleared++;
    LOG("evolution: the game's editor-pending flag was left set with no editor coming (%s): cleared, the stage is drawn again (BUG-0920, %u)",
        refused ? "the request was refused" : "nothing pending for 1 s", g_evo_flags_cleared);
}

// BUG-0404 (the busy gate): the guest's own editor waited for a cinematic but not for a pause: due
// while the guest's game time stood (the pause menu, the Sporepedia, My Collections) it was dropped
// for good ("your editor did not open (pause menu)") or opened under the Sporepedia. It now waits at
// the gate (busy::kEditorGate) while the 90 s limit counts only the time the game was not paused, and
// a pause menu found by the outside-frame check makes it wait again instead of dropping it. Off: as
// in 0.7.3.3.
bool g_own_editor_busy_fix = true;
COOP_FIX("core.own_editor_busy", "BUG-0404", g_own_editor_busy_fix, "core: a guest's own editor after a vote waits through a pause");
double g_own_editor_last_tick = 0;

void own_editor_tick(double now)
{
    void* av = g_mode == modes::kCreature ? game::avatar() : nullptr;
    if (av != g_settle_avatar) {
        g_settle_avatar = av;
        g_settle_since = now;
    }
    double dt = g_own_editor_last_tick ? now - g_own_editor_last_tick : 0;
    g_own_editor_last_tick = now;
    if (!g_own_editor_due || g_own_editor_queued)
        return;
    if (host_creature_mode() && g_sess.role() == net::Role::Client) { // F5 (BUG-0769): the host evolves for everybody
        g_own_editor_due = 0;
        g_one_refused++;
        return;
    }
    unsigned b = g_own_editor_busy_fix ? game_busy() & busy::kEditorGate : 0u;
    if ((b & busy::kPaused) && dt > 0 && dt < 5.0)
        g_own_editor_due_at += dt; // a pause does not count against the 90 s (all in all 300 s)
    bool first_editor = g_own_editor_due == 0xFD4902BD && creations::landfall_pending();
    bool tribal = own_tribal_outfitter(g_own_editor_due);
    if (now - g_own_editor_due_at > 90.0 || now - g_own_editor_due_first > 300.0 ||
        (g_mode != modes::kCreature && stage_mode(g_mode) && !tribal && !(g_mode == modes::kCell && first_editor))) {
        LOG("own editor not opened: the stage did not settle (or was left)");
        feed("evolution: your editor did not open; evolve at your nest later");
        g_own_editor_due = 0;
        return;
    }
    if ((!tribal && (!av || !g_local || now - g_settle_since < 15.0)) || now < g_own_editor_hold_until ||
        creations::avatar_swap_settling(now) || // BUG-0318
        world::cinematic_playing() || b || mode_switch_pending() || world::load_queued()) { // a received world loads first (BUG-0157)
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
            if ((game::mode_id() != modes::kCreature && !own_tribal_outfitter(id)) || mode_switch_pending() || world::load_queued())
                return; // own_editor_tick decides again
            // an editor opened under the pause menu or its "save your game?" boxes: a click on
            // "Don't Save" right after left the guest in a black editor for good
            if (pause_ui_up() && g_own_editor_busy_fix)
                return; // BUG-0404: own_editor_tick waits for the pause (or a message box) to go
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
        guest_pack::reset(); // BUG-9129
        nest::on_world_change();
        fields::on_world_change();
        wobj::on_world_change();
        if (!tribe::editor_world_fix()) // BUG-0217: else tribe::world_check() decides
            tribe::on_world_change();
        party::on_world_change(); // roadmap 0b (every world change)
        home_nests::on_world_change();
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
    if (g_mode == modes::kScenario && !r.puppet)
        return "captain marker";
    return r.puppet ? "shown" : "hidden: not spawned (yet)";
}

// ---- the invariant monitor's fields (docs/06-testing.md#testing-monitor) ----
// The trace lines are written at the session clock's 10 s marks (every machine of a session in the same
// frame, within the clock's error of a few ms), so the lab compares a partner's puppet with that partner's
// own trace line of the same moment. Without a session clock: every 10 s of the local clock as before.
double g_trace_slot = -1e9, g_trace_slot_now = -1e9;
bool g_trace_due = false;

// the start of the mod's frame (after the stale-world check, before any message, module or puppet
// driving): is a trace due this frame, and if so each partner's puppet as the game drew it
static void tribe_air_watch(double now);
void monitor_frame_start(double now)
{
    bool ok = false;
    double st = support::session_time(&ok);
    g_trace_slot_now = ok ? std::floor(st / 10.0) : -1e9;
    g_trace_due = g_trace_at < -50 || (ok ? g_trace_slot_now != g_trace_slot : now - g_trace_at >= 10.0);
    tribe_air_watch(now);
    if (!g_trace_due || !trace::on() || !g_sess.connected())
        return;
    for (auto& [id, r] : g_remotes) {
        r.drawn_at = -1;
        void* p = r.echo ? nullptr : puppet_now(r);
        if (!p || (creature_puppet(r.puppet_adapter) && !game::creature_listed(p)))
            continue;
        r.drawn_st = net::PlayerState{};
        r.puppet_adapter->fill(p, r.drawn_st);
        r.drawn_at = now;
    }
}

// height over this game's terrain (cPlanetModel::GetHeightAt); false without a planet
static bool agl_of(const net::Vec3& p, float& h)
{
    float g = game::ground_radius(p);
    if (g <= 0)
        return false;
    h = drive::speed(p) - g;
    return std::isfinite(h);
}

// BUG-1088 (docs/re/tribe.md#tribe-member-swim): a member swimming is at the water's surface (its centre 0.6-1.9 m under
// it), over a sea bed that GetHeightAt gives as the ground: 3-28 m "over the ground" for as long as it swims, on every
// game (the guests' mirrors float on their own water) and also with no session (lab 20261113-175131, the host alone).
// The full campaign's ground gate failed on such a member (20261113-142203: #16 at radius 499.77, Cadian's water
// 499.7). Over water (the planet's water radius above the terrain there) a member's height counts over the water's
// surface, 0 at most for one in it: a member high over the water still counts. `coop.fix monitor.tribe_water off`:
// over the terrain (0.7.13).
static bool g_tribe_water_fix = true;
COOP_FIX("monitor.tribe_water", "BUG-1088", g_tribe_water_fix,
         "tests: a tribe member swimming counts as on the ground (its height over the water's surface, not the sea bed)");
static bool member_agl_of(const net::Vec3& p, float& h)
{
    if (!agl_of(p, h))
        return false;
    float w = g_tribe_water_fix ? game::water_radius() : 0.f, r = drive::speed(p);
    if (w > 1.f && r - h < w) // (the ground r - h is under the water's surface: over water)
        h = std::min(h, std::max(0.f, r - w));
    return true;
}

// Tribe: the lowest and highest of the player tribe's members of `owner` as this game draws them
// (host: its own members; guest: the mirrors, drawn before this frame's driving)
static bool tribe_agl(uint8_t owner, float& lo, float& hi)
{
    struct Acc {
        uint8_t owner;
        float lo = 1e9f, hi = -1e9f;
        int n = 0;
    } a{owner};
    tribe::for_each_tracked(
        [](const tribe::TrackMember& m, void* u) {
            auto* a = (Acc*)u;
            float h;
            if (m.owner != a->owner || !member_agl_of(m.drawn ? *m.drawn : game::position(m.c), h))
                return;
            a->lo = std::min(a->lo, h);
            a->hi = std::max(a->hi, h);
            a->n++;
        },
        &a);
    lo = a.lo;
    hi = a.hi;
    return a.n > 0;
}

// BUG-0945: which member is over the ground, for how long, and what it does there. The trace's agl_hi named no
// member and no duration: the monitor's "tribe members 5.62 m over the ground on 2 lines" of a campaign could not be
// told from its logs (a member hanging in the air, or members tossed by blows at two samples in a row: a hard blow
// throws a member 3.4-3.7 m up for about 2 s on the host, docs/re/tribe.md#tribe-member-toss). 4 times a second:
// every tracked member more than 3 m over the terrain has an air time (since when without a break, the highest
// point); a line when it starts (the node, the speed, the height over the game's own surface: a rock or a mound over
// the terrain is "on something") and one when it is down again. The trace gives the longest running air time of
// the owner's members as agl_held=<s> next to agl_hi: the monitor's rule is about a member that stays up.
struct AirTime {
    double since, last;
    float top;
    uint8_t owner;
};
static std::unordered_map<uint16_t, AirTime> g_air;
static constexpr float kAirOver = 3.f;
// BUG-1104: the monitor's ground rule found partner 1's members 1.26 m under the host's ground for 20 s in the 0.7.14
// final check's campaign and nothing named the member (lab 20261114-005006). The same watch: a member more than 0.5 m
// under (the rule's limit) is logged once with its node, speed, and its height over the game's surface and the water.
static std::unordered_map<uint16_t, AirTime> g_under;
static constexpr float kUnderAt = -0.5f;
static void tribe_air_watch(double now)
{
    static double next = 0;
    if (g_mode != modes::kTribe || !g_sess.connected()) {
        g_air.clear();
        g_under.clear();
        return;
    }
    if (now < next)
        return;
    next = now + 0.25;
    struct Acc {
        double now;
    } a{now};
    tribe::for_each_tracked(
        [](const tribe::TrackMember& m, void* u) {
            auto* a = (Acc*)u;
            net::Vec3 p = m.drawn ? *m.drawn : game::position(m.c);
            float h;
            if (!member_agl_of(p, h))
                return;
            if (h < kUnderAt) { // BUG-1104: name a member under the ground too (once per stay)
                auto u = g_under.find(m.id);
                if (u != g_under.end()) {
                    u->second.last = a->now;
                    u->second.top = std::min(u->second.top, h);
                    return;
                }
                g_under[m.id] = {a->now, a->now, h, m.owner};
                float sr = game::surface_radius(p), r = drive::speed(p), w = game::water_radius();
                net::Vec3 v = game::velocity(m.c);
                H->log(M, SK_INFO, "tribe: member #%u of player %u is %.2f m under the ground at (%.1f %.1f %.1f): %s speed=%.1f "
                       "(up %.1f) over the game's surface %.2f, over the water %.2f",
                       (unsigned)m.id, (unsigned)m.owner, -h, p.x, p.y, p.z, tribe::member_brief(m.c).c_str(), drive::speed(v),
                       r > 1 ? (v.x * p.x + v.y * p.y + v.z * p.z) / r : 0.f, sr > 1.f ? r - sr : -999.f, w > 1.f ? r - w : -999.f);
                return;
            }
            if (h <= kAirOver)
                return;
            auto it = g_air.find(m.id);
            if (it != g_air.end()) {
                it->second.last = a->now;
                it->second.top = std::max(it->second.top, h);
                return;
            }
            g_air[m.id] = {a->now, a->now, h, m.owner};
            float sr = game::surface_radius(p), r = drive::speed(p);
            net::Vec3 v = game::velocity(m.c);
            H->log(M, SK_INFO, "tribe: member #%u of player %u is %.2f m over the ground at (%.1f %.1f %.1f): %s speed=%.1f "
                   "(up %.1f) over the game's surface %.2f",
                   (unsigned)m.id, (unsigned)m.owner, h, p.x, p.y, p.z, tribe::member_brief(m.c).c_str(), drive::speed(v),
                   r > 1 ? (v.x * p.x + v.y * p.y + v.z * p.z) / r : 0.f, sr > 1.f ? r - sr : -999.f);
        },
        &a);
    for (auto it = g_under.begin(); it != g_under.end();) {
        if (it->second.last >= now) {
            ++it;
            continue;
        }
        H->log(M, SK_INFO, "tribe: member #%u of player %u is on the ground again (or gone) after %.1f s under it, lowest %.2f m",
               (unsigned)it->first, (unsigned)it->second.owner, it->second.last - it->second.since + 0.25, -it->second.top);
        it = g_under.erase(it);
    }
    for (auto it = g_air.begin(); it != g_air.end();) {
        if (it->second.last >= now) {
            ++it;
            continue;
        }
        H->log(M, SK_INFO, "tribe: member #%u of player %u is down again (or gone) after %.1f s over %.0f m, highest %.2f m",
               (unsigned)it->first, (unsigned)it->second.owner, it->second.last - it->second.since + 0.25, kAirOver,
               it->second.top);
        it = g_air.erase(it);
    }
}
// the longest running air time of the owner's members, 0: none is up
static double tribe_air_held(uint8_t owner, double now)
{
    double held = 0;
    for (auto& [id, at] : g_air)
        if (at.owner == owner)
            held = std::max(held, now - at.since + 0.25);
    return held;
}

// " agl=<m>" (Creature: the creature; Tribe: " agl=<lowest> agl_hi=<highest>" of the player's members)
static std::string agl_field(uint8_t owner, void* creature, const net::Vec3* at)
{
    char b[64];
    float lo, hi;
    if (g_mode == modes::kCreature && creature && at && agl_of(*at, lo)) {
        snprintf(b, sizeof b, " agl=%.2f", lo);
        return b;
    }
    if (g_mode == modes::kTribe && tribe_agl(owner, lo, hi)) {
        int n = snprintf(b, sizeof b, " agl=%.2f agl_hi=%.2f", lo, hi);
        if (hi > kAirOver) // BUG-0945: for how long a member has been up without a break (0.0: it came up just now)
            snprintf(b + n, sizeof b - n, " agl_held=%.1f", tribe_air_held(owner, H->time_seconds()));
        return b;
    }
    return "";
}

// BUG-0368 class: the unlocked creature parts of the cPlayer's collected parts (cCollectableItems,
// docs/re/creations.md#creations-own-editor-items): over the unlockable items (the map at +0x1490),
// the number unlocked and an order-free hash of their keys. `locked` gets the keys still locked.
static bool parts_census(int& unlocked, uint32_t& hash, int& unlockable, std::vector<std::pair<uint32_t, uint32_t>>* locked = nullptr)
{
    return parts::census(unlocked, hash, unlockable, locked); // BUG-0401: parts.cpp holds it
}

// the partner's speed as this game sees it: its reported speed, or (the RTS pointer reports none) the
// way its interpolated position went in the last 0.5 s
static float seen_speed(const Remote& r, double now)
{
    net::PlayerState a, b;
    if (!r.buf.sample(now, a) || !r.buf.sample(now - 0.5, b))
        return 0;
    float v = a.mode == b.mode && a.world == b.world ? net::dist(a.pos, b.pos) / 0.5f : 0.f;
    return std::max(v, drive::speed(a.vel));
}

// the monitor's fields at the end of the own trace line
static std::string monitor_own_fields(bool have)
{
    std::string out;
    if (!have)
        return out;
    net::Vec3 p = g_local && g_mode == modes::kCreature ? game::position(g_local) : net::Vec3{};
    out += agl_field(g_sess.my_id(), g_mode == modes::kCreature ? g_local : nullptr, &p);
    if (g_mode == modes::kCreature) {
        int n, all;
        uint32_t h;
        if (parts_census(n, h, all)) {
            char b[48];
            snprintf(b, sizeof b, " parts=%d:%08X", n, h);
            out += b;
        }
    }
    if (g_mode == modes::kTribe || g_mode == modes::kCiv) {
        // BUG-0416: how long the own pointer has stood still (its partner's copy is ~0.1 s + half a round
        // trip behind a pointer that just jumped: the monitor's drawn rule judges only a resting one)
        double still = modes::rts_marker_still(g_adapter, local_now());
        if (still >= 0) {
            char b[32];
            snprintf(b, sizeof b, " ptr_still=%.1f", std::min(still, 999.0));
            out += b;
        }
    }
    if (g_mode == modes::kCell) {
        // BUG-0681: the game's world scale its Cell positions (own pos=, the partners' drawn=) are in: during a
        // level change each game rescales at its own moment, the monitor's drawn rule compares in one scale
        float k = cellsync::scale_now();
        if (k > 0) {
            char b[32];
            snprintf(b, sizeof b, " wscale=%.6g", k);
            out += b;
        }
    }
    out += npc::monitor_fields();
    return out;
}

// the monitor's fields at the end of a partner's trace line
static std::string monitor_partner_fields(uint8_t id, const Remote& r, double now)
{
    std::string out;
    char b[160];
    bool drawn = r.drawn_at == now && strcmp(puppet_why(r, now), "shown") == 0;
    if (drawn) {
        const net::Vec3& d = r.drawn_st.pos;
        snprintf(b, sizeof b, " drawn=(%.1f %.1f %.1f) drawn_v=%.1f", d.x, d.y, d.z, seen_speed(r, now));
        out += b;
    }
    bool creature = drawn && creature_puppet(r.puppet_adapter) && g_mode == modes::kCreature;
    out += agl_field(id, creature ? r.puppet : nullptr, creature ? &r.drawn_st.pos : nullptr);
    if (creature && r.drawn_st.max_health > 0) { // read from the puppet (BUG-0373: the partner's health)
        snprintf(b, sizeof b, " puppet_hp=%.0f/%.0f", r.drawn_st.health, r.drawn_st.max_health);
        out += b;
    }
    return out;
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
    if (!g_sess.connected() || (!changed && !g_trace_due))
        return;
    g_trace_at = now;
    if (g_trace_due)
        g_trace_slot = g_trace_slot_now;
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
    if (const char* why = share_wait_reason()) // BUG-0382 (new fields go at the end: monitor.py)
        snprintf(b + strlen(b), sizeof b - strlen(b), " share_wait=%s", why);
    if (unsigned gb = game_busy()) // BUG-0404: the busy gate (census / sync checks skip a game that stands)
        snprintf(b + strlen(b), sizeof b - strlen(b), " busy=%s", busy::names(gb).c_str());
    snprintf(b + strlen(b), sizeof b - strlen(b), "%s", monitor_own_fields(have).c_str()); // (before version=)
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
        l += monitor_partner_fields(id, r, now);
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

// Tests: fix switches that act while the game starts (before a console can switch them, e.g. game.save_areas,
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

static void spectate_tick(); // BUG-9156 (below)
static void join_apart_tick(double now);
// ---------------- the frame profiler (BUG-9257, 2026-11-18: a guest's ~0.25 s pause every second) ----------------
// What of the mod's frame took long: on_frame is cut into its calls (a mark after each one), the session's messages
// by type. Once a minute the slowest ones go to the log ("prof: ..."), and a single call over 50 ms at once (12 lines
// a minute at most). A frame interval over 80 ms that our work did not fill counts as the game's own. Off:
// [diag] frame_prof=0. Costs two QueryPerformanceCounter calls a mark (about 110 a frame).
namespace fprof {
static bool g_on = true;
struct Sect {
    double sum = 0, max = 0;
    uint32_t over = 0; // calls over 10 ms
};
static std::map<const char*, Sect> g_sect;
static Sect g_msg[256];
static double g_t, g_frame_t0, g_last_begin, g_frame_work, g_next_report, g_freq;
static uint32_t g_long_frames, g_long_ours, g_spikes;
static double qpc()
{
    LARGE_INTEGER c;
    if (!g_freq) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_freq = (double)f.QuadPart;
    }
    QueryPerformanceCounter(&c);
    return c.QuadPart / g_freq;
}
static void add(Sect& s, double dt, const char* what, int type)
{
    s.sum += dt;
    if (dt > s.max) s.max = dt;
    if (dt > 0.010) s.over++;
    if (dt > 0.050 && g_spikes++ < 12) {
        if (what) LOG("prof: %s took %.0f ms (one frame, %s)", what, dt * 1000, modes::mode_name(game::mode_id()));
        else LOG("prof: message type %d took %.0f ms (one message, %s)", type, dt * 1000, modes::mode_name(game::mode_id()));
    }
}
static void begin()
{
    if (!g_on) return;
    const double t = qpc();
    if (g_last_begin > 0 && t - g_last_begin > 0.080) { // the last frame interval was long: ours, or the game's
        g_long_frames++;
        if (g_frame_work > 0.040) g_long_ours++;
    }
    g_last_begin = g_t = g_frame_t0 = t;
}
static void mark(const char* label)
{
    if (!g_on) return;
    const double t = qpc();
    add(g_sect[label], t - g_t, label, -1);
    g_t = t;
}
static void message(uint8_t type, double dt)
{
    if (g_on) add(g_msg[type], dt, nullptr, type);
}
static void end_frame()
{
    if (!g_on) return;
    const double t = qpc();
    g_frame_work = t - g_frame_t0;
    if (t < g_next_report) return;
    if (g_next_report > 0) {
        std::vector<std::pair<const char*, Sect>> v(g_sect.begin(), g_sect.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.max > b.second.max; });
        std::string line;
        for (size_t i = 0; i < v.size() && i < 8 && v[i].second.max > 0.004; i++) {
            char b[160];
            snprintf(b, sizeof b, "%s%s %.1f/%.0f/%u", line.empty() ? "" : ", ", v[i].first, v[i].second.max * 1000,
                     v[i].second.sum * 1000, v[i].second.over);
            line += b;
        }
        std::string msgs;
        for (int k = 0; k < 256; k++)
            if (g_msg[k].max > 0.004) {
                char b[64];
                snprintf(b, sizeof b, "%s%d %.1f/%.0f/%u", msgs.empty() ? "" : ", ", k, g_msg[k].max * 1000, g_msg[k].sum * 1000,
                         g_msg[k].over);
                msgs += b;
            }
        LOG("prof: this minute (%s): frames with an interval over 80 ms %u, of them filled by the mod's own work %u | "
            "slowest calls (max ms in one frame / ms in all / frames over 10 ms): %s | messages by type: %s",
            modes::mode_name(game::mode_id()), g_long_frames, g_long_ours, line.empty() ? "none over 4 ms" : line.c_str(),
            msgs.empty() ? "none over 4 ms" : msgs.c_str());
    }
    g_sect.clear();
    for (auto& m : g_msg) m = Sect{};
    g_long_frames = g_long_ours = g_spikes = 0;
    g_next_report = t + 60;
}
} // namespace fprof

void on_frame(void*, int, void*)
{
    fprof::begin(); // (the frame profiler: what of this frame took long, prof lines in the log)
    modes::CellScope cell_scope; // BUG-0271: no game code runs in here, the pool check holds for it
    fprof::mark("modes::CellScope cell_scope");
    game::LiveScope live_scope;  // BUG-0426: the object list of this frame (game::listed) holds in here
    fprof::mark("game::LiveScope live_scope");
    double now = H->time_seconds();
    g_last_frame = now;
    fprof::mark("g_last_frame = now");
    support::tick(now); // first: its frame timing (FPS, hitches) and the session clock
    fprof::mark("support::tick(now)");
    mem_refresh::tick(now); // decision 4S (BUG-9239): the memory box, a restart's steps
    fprof::mark("mem_refresh::tick(now)");
    {
        bool clock_ok = false;
        double st = support::session_time(&clock_ok);
        squadron::frame_start(st, clock_ok); // BUG-0378 tests: the ships as drawn, before any write of ours
    }
    diag_tick(now);
    fprof::mark("diag_tick(now)");
    notify::tick(now);
    fprof::mark("notify::tick(now)");
    gamefixes::frame(); // BUG-0506: once
    fprof::mark("gamefixes::frame()");
    game::world_epoch(); // BUG-0119: sees every frame outside the creature stage
    fprof::mark("game::world_epoch()");
    game::stage_epoch_tick(); // BUG-0217: before any message or module tick of this frame
    fprof::mark("game::stage_epoch_tick()");
    modes::world_name_tick(); // BUG-0258: the planet stays known through snapshot.spo
    fprof::mark("modes::world_name_tick()");
    modes::test_tick();       // BUG-0223 tests: a forced frame without the local entity
    fprof::mark("modes::test_tick()");
    tribe::world_check();     // BUG-0217: the Tribe world changed (not an editor visit)
    fprof::mark("tribe::world_check()");
    civ::world_check();       // BUG-0223: the Civ world changed (not a frame without the city anchor)
    fprof::mark("civ::world_check()");
    lifecycle_world_tick();   // BUG-1055: a new world resets every WORLD_STATE (lifecycle.h)
    fprof::mark("lifecycle_world_tick()");
    creations::tick(now);
    fprof::mark("creations::tick(now)");
    library::tick(now, !(game_busy() & (busy::kEditor | busy::kLoading | busy::kCinematic)));
    fprof::mark("library::tick(now, !(game_busy() & (busy::kEditor | busy");
    lineage_cells::tick(now, game::mode_id() == modes::kCell && !(game_busy() & (busy::kEditor | busy::kLoading))); // BUG-9111
    fprof::mark("lineage_cells::tick(now, game::mode_id() == modes::kCell");
    if (g_frame_fix) // BUG-0091: forget the puppets of a world the game tore down before any message uses them
        avatar_change_check();
    monitor_frame_start(now); // the puppets as drawn, before any driving (docs/06-testing.md#testing-monitor)
    fprof::mark("monitor_frame_start(now)");
    npc::monitor_tick(now);   // the proxies as drawn
    fprof::mark("npc::monitor_tick(now)");
    bodyrec_tick(now);        // coop.bodyrec (tests, BUG-0425): every body as drawn
    fprof::mark("bodyrec_tick(now)");
    modes::own_ground_tick(now); // BUG-0592: the own creature not left in the ground
    fprof::mark("modes::own_ground_tick(now)");
    // BUG-0231: the world a joining guest hears in WELCOME (before the host's first state); the
    // values of the last frame, as every message of this update sees them. Not switched: 0.7.3.0
    // guests never read it
    if (g_sess.role() == net::Role::Host)
        g_sess.world = host_world::welcome_world(g_mode, g_world, g_mode == modes::kSpace ? modes::current_world() : 0);
    host_save_tick(); // BUG-0933: the host says its save again when it changed; a guest follows it
    fprof::mark("host_save_tick()");
    g_sess.update();
    fprof::mark("g_sess.update()");
    partner_pass_tick(); // BUG-0913: what the collision and hover hooks may skip this frame
    fprof::mark("partner_pass_tick()");
    // BUG-9146: the knockout and revive with every partner here (before: only a duo had them; with three or four
    // players a lethal hit was a native death). Off (`coop.fix creature.rescue_party off`): a duo only.
    static std::vector<net::PlayerState> rescue_peers;
    rescue_peers.clear();
    fprof::mark("rescue_peers.clear()");
    const size_t players = g_sess.players().size();
    bool duo = g_local && g_sess.connected() && (g_rescue_party_fix ? players >= 2 : players == 2);
    if (duo && (g_mode == modes::kScenario || g_mode == modes::kCreature)) {
        for (const auto& [id, r] : g_remotes) {
            if (r.echo || id >= net::kMaxPlayers || r.buf.empty() || r.buf.age(now) > 2) continue;
            const auto& s = r.buf.latest();
            if (same_place(s) && (s.flags & net::PF_IN_WORLD))
                rescue_peers.push_back(s);
        }
    }
    if (g_local && g_sess.connected() && g_mode == modes::kScenario)
        adventure::tick();
    else
        adventure::reset();
    if (g_local && g_sess.connected() && (g_mode == modes::kCreature || g_mode == modes::kScenario) &&
        (duo || creature_rescue::continuing()))
        creature_rescue::tick(g_world ? g_world : 0xADE47u, g_local, rescue_peers, now); // BUG-9089: the captain too
    else
        creature_rescue::reset();
    spectate_tick(); // BUG-9156
    fprof::mark("spectate_tick()");
    lobby_tick(now);
    host_pause_tick(now); // BUG-9260
    no_pause_tick(now);   // decision 6T (alpha)
    fprof::mark("lobby_tick(now)");
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
    fprof::mark("vote_tick(now)");
    submit_tick(now);
    fprof::mark("submit_tick(now)");
    cross_tick(now); // BUG-9278
    tribal_graduation_tick(now);
    fprof::mark("tribal_graduation_tick(now)");
    own_editor_tick(now);
    fprof::mark("own_editor_tick(now)");
    evo_flag_tick(now); // BUG-0920
    fprof::mark("evo_flag_tick(now)");
    offer_tick(now); // BUG-0516
    fprof::mark("offer_tick(now)");
    vote_action_tick(now);
    fprof::mark("vote_action_tick(now)");
    coop_ui::tick();
    fprof::mark("coop_ui::tick()");
    if (build_id() == 0xDC04AEE5u) editor_depth::tick();
    fprof::mark("if (build_id() == 0xDC04AEE5u) editor_depth::tick()");
    menu_tick(now);
    fprof::mark("menu_tick(now)");
    mate_hearts_tick(now);
    fprof::mark("mate_hearts_tick(now)");
    if (g_sess.role() == net::Role::Host)
        world::tick(g_sess);
    else
        world::guest_tick(g_sess); // a received world waiting for the player to leave its editor
    savestrip::tick();
    fprof::mark("savestrip::tick()");
    npc::tick(now);
    fprof::mark("npc::tick(now)");
    player_rules::tick(now); // BUG-0558: the host's UFO on the guests
    fprof::mark("player_rules::tick(now)");
    nest::tick(now); // BUG-0384
    fprof::mark("nest::tick(now)");
    fields::tick(now); // BUG-0419 G2 (after the modules that make the mirrors)
    fprof::mark("fields::tick(now)");
    wsync::tick(now);  // W1: host-owned world state (time of day, relations, stages done, the Tribe goal)
    fprof::mark("wsync::tick(now)");
    wobj::tick(now); // BUG-0472, BUG-0459
    fprof::mark("wobj::tick(now)");
    realign::tick(now); // W2: the whole planet after transitions (BUG-0564)
    fprof::mark("realign::tick(now)");
    parts::tick(now); // BUG-0401
    fprof::mark("parts::tick(now)");
    effects::tick(now); // BUG-0451: sends the queued effects on the main thread
    fprof::mark("effects::tick(now)");
    social::tick(now);
    fprof::mark("social::tick(now)");
    if (g_sess.role() == net::Role::Host)
        party_bar::tick(g_sess); // BUG-9277: the planet's saved party bars
    evo_key_tick(now);
    fprof::mark("evo_key_tick(now)");
    creature_hp::tick();
    fprof::mark("creature_hp::tick()");
    shared::tick(g_sess, game::mode_id(), now, guest_elsewhere() || guest_space_other()); // BUG-0231, BUG-0709: held while elsewhere
    fprof::mark("shared::tick(g_sess, game::mode_id(), now, guest_elsewhe");
    if (g_sess.role() == net::Role::Host && game::avatar()) {
        float total = game::dna();
        float earned = game::evolution_bar();
        // also every 2 s: a guest drops it while it has no avatar (loading, editor), BUG-0128
        static uint32_t bars_seen = 0; // BUG-9277: a party's own bar moved
        if (total != g_dna_sent || (std::isfinite(earned) && earned != g_dna_bar_sent) ||
            party_bar::changed_since(bars_seen) || (cellsync::totals_resend_enabled() && now - g_dna_at > 2.0)) {
            bars_seen = party_bar::serial();
            g_dna_sent = total;
            g_dna_bar_sent = earned;
            g_dna_at = now;
            net::Writer w;
            w.f32(total);
            // (optional tail, older guests read the total only) why it changed since the last one
            w.u8((uint8_t)g_dna_out.size());
            for (auto& c : g_dna_out) {
                w.u8((uint8_t)c.cause);
                w.u8(c.player);
                w.f32(c.amount);
            }
            const uint8_t pace = (uint8_t)dna_pace_announce();
            g_dna_out.clear();
            for (auto& p : g_sess.players())
                if (p.id != 0) {
                    net::Writer wp = w; // BUG-9277: each guest its own party's bar
                    dna_sync::Progress{g_world, party_bar::bar_for(g_sess, p.id, earned)}.write(wp); // Lineages: the earned progress (tagged, optional)
                    // BUG-0510: the pace divisor, after the progress tail (a guest without it reads the progress only)
                    wp.u8(pace);
                    g_sess.send_reliable_to(p.id, net::MSG_DNA, wp);
                }
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

    uint32_t prev_mode = g_mode;
    g_mode = game::mode_id();
    fprof::mark("g_mode = game::mode_id()");
    // the effect hooks are installed once (game::init); this only gates their work to the played
    // stages (a hook removed at a stage change faulted the UI skinner's render thread, BUG-0451 review)
    game::set_world_relay(g_mode == modes::kCell || g_mode == modes::kCreature || g_mode == modes::kTribe
                          || g_mode == modes::kCiv || g_mode == modes::kSpace);
    if (g_mode != prev_mode)
        on_mode_change(prev_mode, g_mode);
    g_adapter = modes::for_mode(g_mode);
    fprof::mark("g_adapter = modes::for_mode(g_mode)");
    g_world = modes::current_place(g_adapter);
    fprof::mark("g_world = modes::current_place(g_adapter)");
    g_local = g_adapter ? g_adapter->local() : nullptr;
    fprof::mark("g_local = g_adapter ? g_adapter->local() : nullptr");
    settings::tick(); // F3: the host's world opened or closed (its stored settings)
    fprof::mark("settings::tick()");
    g_sess.max_players = settings::get(settings::kPlayerLimit); // F3 (BUG-0768): the host's join rules
    fprof::mark("g_sess.max_players = settings::get(settings::kPlayerLimi");
    if (galaxy::hosting()) // 6E B (alpha): a galaxy planet takes the galaxy's count, not the co-op setting
        g_sess.max_players = (decltype(g_sess.max_players))galaxy::planet_players();
    pfind::tick(H->time_seconds()); // F2: the partners on the minimap (outside the frame)
    fprof::mark("pfind::tick(H->time_seconds())");
    parties::tick(H->time_seconds()); // 6J
    fprof::mark("parties::tick");
    invite_prompt_tick();
    fprof::mark("invite_prompt_tick()");
    social_tick(H->time_seconds()); // 6L A
    fprof::mark("social_tick");
    portrait::tick(H->time_seconds()); // BUG-0937: the HUD portrait follows the avatar's species (outside the frame)
    fprof::mark("portrait::tick(H->time_seconds())");
    pfind::hud_tick(H->time_seconds()); // BUG-0949: the UI windows the labels keep off (outside the frame)
    fprof::mark("pfind::hud_tick(H->time_seconds())");
    sreport::tick();  // F4: the session report's round (host) and the guest's question / transfer
    fprof::mark("sreport::tick()");
    game::leave_probe_tick(g_local); // BUG-0264 tests: an avatar used while the stage is torn down
    fprof::mark("game::leave_probe_tick(g_local)");
    modes::cell_probe_tick();        // BUG-0271: the cell pool freed while the mode reads Cell
    fprof::mark("modes::cell_probe_tick()");
    void* av = game::avatar();
    fprof::mark("void* av = game::avatar()");
    avatar_change_check();
    fprof::mark("avatar_change_check()");
    join_apart_tick(now); // BUG-0989
    fprof::mark("join_apart_tick(now)");

    remove_strays(now);
    fprof::mark("remove_strays(now)");
    follow_phase(now);
    fprof::mark("follow_phase(now)");
    share_wait_tick();
    fprof::mark("share_wait_tick()");
    host_away_tick(now);
    fprof::mark("host_away_tick(now)");
    dna_spend_tick(now); // BUG-0466
    fprof::mark("dna_spend_tick(now)");
    own_dna_tick(now);   // BUG-9165
    fprof::mark("own_dna_tick(now)");
    dna_notices_tick(now);
    fprof::mark("dna_notices_tick(now)");
    progress_debt::tick(g_mode, g_world, now); // BUG-9119
    fprof::mark("progress_debt::tick(g_mode, g_world, now)");
    evolve::own_tick(game::mode_id(), g_local, game_busy() != 0, now); // BUG-9160: out of the own cocoon
    fprof::mark("evolve::own_tick(game::mode_id(), g_local, game_busy() !");
    evolve::egg_tick(game::mode_id(), now);                             // BUG-9161: an egg scene that never ends
    fprof::mark("evolve::egg_tick(game::mode_id(), now)");
    evolve::mating_tick(game::mode_id(), g_local == game::avatar() ? g_local : nullptr, // BUG-9175
                        g_sess.connected() && g_sess.role() != net::Role::Host, game_busy() != 0, now, submit_evolution_request);
    evolve::family_tick(game::mode_id(), g_local, game_busy() != 0, now); // BUG-9161: the family wears the own species
    fprof::mark("evolve::family_tick(game::mode_id(), g_local, game_busy(");
    migration_tick(now); // BUG-9163: the party migrates together
    fprof::mark("migration_tick(now)");
    join_editor_tick(now); // BUG-9164: a creature of one's own
    fprof::mark("join_editor_tick(now)");
    pregame_editor_tick(now); // BUG-9207: the first creature while the host makes a new world
    fprof::mark("pregame_editor_tick(now)");
    civ_editor_first_tick(now); // BUG-9170
    fprof::mark("civ_editor_first_tick(now)");
    guest_pack::tick(now);                      // BUG-9129
    fprof::mark("guest_pack::tick(now)");
    identity::tick(now);                        // BUG-9135
    fprof::mark("identity::tick(now)");
    bans::tick(now);                            // BUG-9251: a banned player whose identity arrived is removed
    fprof::mark("bans::tick(now)");
    galaxy::tick(now);                          // decision 5O A: the galaxy server (alpha)
    fprof::mark("galaxy::tick(now)");
    era_visitors::tick(now);                    // decision 5S A spike (alpha)
    fprof::mark("era_visitors::tick(now)");
    galaxy_menu::tick(now);                     // decision 5X B (alpha)
    fprof::mark("galaxy_menu::tick(now)");
    party::tick(now);                           // roadmap 0b
    fprof::mark("party::tick(now)");
    home_nests::host_tick(now);                 // roadmap phase 2
    fprof::mark("home_nests::host_tick(now)");
    tribe::tick(now);
    fprof::mark("tribe::tick(now)");
    bool walk_done = !gamefixes::leave_walk_armed();
    if (!walk_done)
        tribe::for_each_owned([](void* c, uint8_t, void* done) { // BUG-0310 tests (coop.leavewalk)
            if (!*(bool*)done)
                *(bool*)done = gamefixes::leave_walk_tick(c);
        }, &walk_done);
    cellsync::tick(now);
    fprof::mark("cellsync::tick(now)");
    civ::tick(now);
    fprof::mark("civ::tick(now)");
    civ::planner_probe_tick(); // BUG-0682 tests (off unless `coop.civ planner_probe on`)
    fprof::mark("civ::planner_probe_tick()");
    squadron::tick(now);
    fprof::mark("squadron::tick(now)");
    autoupdate::tick();
    fprof::mark("autoupdate::tick()");
    spacecoop::tick(now);
    fprof::mark("spacecoop::tick(now)");
    if (g_sess.connected() && g_mode == modes::kCreature)
        teamplay::tick(game::avatar(), now);

    // BUG-1093 (net.send_phase): every 0.05 s with the schedule's phase kept, a stop at once (net::state_due)
    float sp_now = 0;
    fprof::mark("float sp_now = 0");
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

    (void)av;
    // Scenario models and their act-owned metadata are created outside Present.
    static bool adventure_drive_pending=false;
    if (g_mode==modes::kScenario && !adventure_drive_pending) {
        adventure_drive_pending=true;
        H->run_outside_frame([](void*) {
            adventure_drive_pending=false;
            if (g_mode!=modes::kScenario || game::mode_id()!=g_mode ||
                !g_local || g_local!=game::avatar() || !g_sess.connected()) return;
            for (auto& [id,r]:g_remotes) drive_puppet(r,H->time_seconds());
        },nullptr);
    }
    for (auto& [id, r] : g_remotes) {
        if (!g_local) {
            if (r.puppet)
                forget_puppets();
            continue;
        }
        if (g_mode!=modes::kScenario) drive_puppet(r, now);
    }
    track_tick(now);
    fprof::mark("track_tick(now)");
    trace_tick(now);
    fprof::mark("trace_tick(now)");
    fprof::end_frame();
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
static bool g_partner_arrows = true;
COOP_FIX("ui.partner_arrows", "BUG-9097", g_partner_arrows, "visible direction arrows and distance for partners in the same world");
static unsigned g_partner_indicators;

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

// BUG-9121: off-screen arrows (the partner's, map marks) pointed down whenever the target was off the
// screen, as if it were behind the camera. Off: the old direction.
bool g_arrow_fix = true;
COOP_FIX("ui.offscreen_arrow", "BUG-9121", g_arrow_fix, "ui: arrows to off-screen partners and marks point at them (not down)");

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
        sc[0] = sc[1] = NAN;
        bool front = H->world_to_screen(w, sc) != 0;
        if (g_arrow_fix) // BUG-9121: off screen is not behind
            front = std::isfinite(sc[0]) && std::isfinite(sc[1]);
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
            const float seconds = tribe::revive_seconds(c);
            if (seconds > 0 && ring_timer::enabled()) { // BUG-9141: the draining ring above the downed villager's tag
                int sw = 0, sh = 0;
                H->screen_size(&sw, &sh);
                const float r = ring_timer::radius_for(sh);
                ring_timer::draw(H, sc[0], std::max(r + 4.f, sc[1] - 16.f - r), r,
                                 ring_timer::fraction(seconds, tribe::revive_total()), tribe::revive_progress(c),
                                 H->time_seconds());
            } else if (seconds > 0)
                n += " [REVIVE " + std::to_string((int)std::ceil(seconds)) + "s]";
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
bool g_text_fix = true; // coop.social textfix off: the old key poll (tests)
COOP_FIX("social.text", "BUG-0149", g_text_fix, "creature: ability keys are not taken while the cheat console is open");
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

static uintptr_t g_mate_classify;
static void* (SK_THISCALL* g_get_animal_target)(void*);
static float (*g_native_mate_response)(void*);

static float mate_response(void* creature)
{
    const float score = g_native_mate_response(creature);
    if (score > 0 || !g_sess.connected() || !game::in_creature_stage() ||
        !g_local || g_local != game::avatar() || creature == g_local ||
        game::is_dead(g_local) || game::is_dead(creature) || game::is_puppet(creature))
        return score;

    // Match the game's mating actions. Only help the nestmate the player has
    // actually selected, after the native input has started the approach.
    constexpr uint32_t mating = 0x02D852F0u, mating_at_nest = 0xD23FE7CAu;
    const auto controller = skg::field<void*>(g_local, 0xB4C);
    const auto action = controller ? skg::field<uint32_t>(controller, 0x1D8) : 0;
    if ((action != mating && action != mating_at_nest) || g_get_animal_target(g_local) != creature ||
        skg::cdecl_call<int>(g_mate_classify, creature) != 9)
        return score;

    const auto herd = game::herd_of(g_local);
    if (!herd || game::herd_of(creature) != herd)
        return score;
    const auto nest = game::herd_nest(herd);
    net::Vec3 center;
    if (!nest || !game::object_position(nest, center))
        return score;

    // Tolerate the player's collision edge and the selected mate's short
    // wander past the nest. Spore still positions both creatures.
    const float radius = skg::vcall<float>((uint8_t*)nest + 0x34, 0x74 / 4) +
                         skg::vcall<float>((uint8_t*)creature + 0xC0, 0x74 / 4);
    constexpr float mate_wander_margin = 5.f;
    const float mate_distance = net::dist(center, game::position(creature));
    const float avatar_distance = net::dist(center, game::position(g_local));
    constexpr float avatar_edge_margin = 0.25f;
    if (radius > 0 && avatar_distance < radius + avatar_edge_margin &&
        mate_distance < radius + mate_wander_margin &&
        (avatar_distance >= radius || mate_distance >= radius))
        return 1.f;
    return score;
}

static bool g_mate_retries = true;
COOP_FIX("creature.mate_retries", "BUG-9175", g_mate_retries, "creature: a stalled mating approach is restarted every 2.5 s, 5 times");
static bool (*g_native_mate_step)(void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t*);
static bool (SK_THISCALL* g_mate_position_reached)(void*);

// tests (BUG-9161): the last call of the native mating step for the own creature: its state words, when, whether
// each of the pair reached its spot, and the mate
static uint32_t g_mstep_state[4];
static double g_mstep_at = -1;
static int g_mstep_reached[2] = {-1, -1};
static void* g_mstep_mate;
static uint32_t g_mstep_calls;
static bool mate_step(void* avatar, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uint32_t* state)
{
    if (avatar == g_local && state && skg::readable(state, 16)) {
        memcpy(g_mstep_state, state, 16);
        g_mstep_at = H->time_seconds();
        ++g_mstep_calls;
        g_mstep_mate = g_get_animal_target ? g_get_animal_target(avatar) : nullptr;
        g_mstep_reached[0] = g_mate_position_reached ? (int)g_mate_position_reached((uint8_t*)avatar + 0xC0) : -1;
        g_mstep_reached[1] = g_mstep_mate && game::object_alive(g_mstep_mate) && g_mate_position_reached
                                 ? (int)g_mate_position_reached((uint8_t*)g_mstep_mate + 0xC0) : -1;
    }
    if (avatar == g_local && g_sess.connected()) {
        static double retry_at = 0;
        static unsigned retries = 0;
        if (state[0] != 2 || state[1] != 1) {
            retry_at = 0;
            if (state[0] != 2)
                retries = 0;
        } else {
            const double now = H->time_seconds();
            // BUG-9175 (owner play test 2026-11-14: "needed to spam click their mate"): one retry 4 s in did not move a
            // mate that stood still; the owner's clicks did. Now every 2.5 s, 5 times (`coop.fix creature.mate_retries
            // off`: twice, every 4 s), each logged with the pair's spots
            const double every = g_mate_retries ? 2.5 : 4.0;
            const unsigned most = g_mate_retries ? 5 : 2;
            if (!retry_at)
                retry_at = now + every;
            if (now >= retry_at && retries < most) {
                ++retries;
                retry_at = now + every;
                void* mate = g_get_animal_target(avatar);
                if (mate && !game::is_dead(mate) && !game::is_puppet(mate) &&
                    game::herd_of(mate) == game::herd_of(avatar) &&
                    (!g_mate_position_reached((uint8_t*)avatar + 0xC0) ||
                     !g_mate_position_reached((uint8_t*)mate + 0xC0))) {
                    // The mate already responded, but its roaming can replace
                    // the approach goal. Ask Spore to stage the pair again.
                    trace::line("creature", "restarting the paired mating approach (retry %u): state %d %d, at its spot: "
                                "avatar %d, mate %d, %.1f m apart", retries, (int)state[0], (int)state[1],
                                (int)g_mate_position_reached((uint8_t*)avatar + 0xC0),
                                (int)g_mate_position_reached((uint8_t*)mate + 0xC0),
                                net::dist(game::position(avatar), game::position(mate)));
                    state[1] = 0;
                }
            }
        }
    }
    return g_native_mate_step(avatar, a2, a3, a4, a5, state);
}

// BUG-9174: the Creature game's mate keeper (0xD52270, every frame) keeps the nestmate offered as mate in its slot
// (+0x38, an intrusive pointer), marked +0xB58 bit 0x10 (CreatureClickAction 9: the mate icon) and its controller
// +0x5FC bit 0x4000; it offers none while the avatar's +0x1678 > 0 and clears one through 0xD4B180. It picks from the
// avatar's own family; a guest's family are copies of the host's members (npc.cpp), never picked: no mate icon and no
// hearts on a guest (owner, 2026-11-12 and 2026-11-13). Now a guest's empty slot gets the nearest grown copy of its own
// species in its own nest herd. Off: no mate offered on a guest (the click still mates, BUG-0177).
static bool g_guest_mate_fix = true;
COOP_FIX("creature.guest_mate_offer", "BUG-9174", g_guest_mate_fix,
         "creature: a guest's nest offers a copy of its family as mate (the mate icon and hearts, as on the host)");
static void (SK_THISCALL* g_native_mate_keeper)(void*, uint32_t, uint32_t);
static uintptr_t g_ptr_assign;   // SporeCoop::IntrusivePtr_Assign
static void* g_mate_keeper;      // tests: the keeper (this) seen last
static uint32_t g_mate_offers;   // tests: copies put in the slot
static uint32_t g_keeper_calls, g_keeper_lends; // tests (BUG-9181): keeper runs on a guest, mates lent to the own AI

static bool own_family_copy(void* c, void* herd, void* profile)
{
    return c && c != g_local && npc::is_proxy(c) && game::object_alive(c) && !game::is_dead(c) &&
           game::herd_of(c) == herd && game::species_profile(c) == profile &&
           skg::field<int32_t>(c, 0xB34) == 1 && skg::field<void*>(c, 0xB4C); // grown (0xC0B8D0), a controller (0xD4B180)
}

static void SK_THISCALL mate_keeper(void* self, uint32_t a, uint32_t b)
{
    g_native_mate_keeper(self, a, b);
    g_mate_keeper = self;
    ++g_keeper_calls;
    if (!g_guest_mate_fix || !g_ptr_assign || !self || !g_sess.connected() || g_sess.role() == net::Role::Host ||
        !game::in_creature_stage() || !g_local || g_local != game::avatar() || (game_busy() & (busy::kLoading | busy::kEditor)))
        return;
    void** slot = (void**)((uint8_t*)self + 0x38);
    // BUG-9181: the mate offered (a copy of the host's member) and the one the player is mating with run their own AI
    // here (the sync leaves them alone): the game's pairing needs the mate's answer, its walk and its dance. Before,
    // the pairing stalled (BUG-9175) and the mod went to the cocoon at once ("rather than the normal mating", owner).
    if (g_guest_mate_fix) {
        if (*slot && npc::is_proxy(*slot) && npc::lend_local_ai(*slot, 2.0)) ++g_keeper_lends;
        if (void* ctl = skg::field<void*>(g_local, 0xB4C)) {
            const uint32_t a = skg::field<uint32_t>(ctl, 0x1D8);
            if (a == 0x02D852F0u || a == 0xD23FE7CAu)
                if (void* t = g_get_animal_target ? g_get_animal_target(g_local) : nullptr; t && npc::is_proxy(t))
                    npc::lend_local_ai(t, 2.0);
        }
    }
    // the game offers one itself, or none now: its own gates (0xD52695..0xD526FE: the avatar grown, +0xB5E clear,
    // +0x1678 <= 0); an offer past them was cleared again every frame (a hatchling after its editor: 29 offers in 15 s)
    if (*slot || skg::field<int32_t>(g_local, 0xB34) != 1 || skg::field<uint8_t>(g_local, 0xB5E) ||
        skg::field<int32_t>(g_local, 0x1678) > 0)
        return;
    static double next = 0;
    const double now = H->time_seconds();
    if (now < next)
        return;
    next = now + 0.5;
    // an offer the game cleared at once is one its own check refuses (0xD4B2A0: its mate before, a busy one, ...): that
    // copy is left out for a while (the TITAN guest 2026-11-13: the same copy offered 190 times in 95 s, another one kept)
    static uint32_t last = 0, refused = 0;
    static double last_at = 0, refused_until = 0;
    if (last && now - last_at < 2.0) {
        refused = last;
        refused_until = now + 30;
    }
    last = 0;
    struct Pick {
        void *herd, *profile;
        uint32_t skip;
        void* best = nullptr;
        net::Vec3 nest{};
        float d = 1e9f; // the nearest to the nest, wherever it roams (a lone member 73 m out was never offered)
    } p{game::herd_of(g_local), game::species_profile(g_local), now < refused_until ? refused : 0};
    void* nest = p.herd ? game::herd_nest(p.herd) : nullptr;
    if (!nest || !p.profile || !game::object_position(nest, p.nest))
        return;
    game::for_each_object([](void* c, uint32_t noun, void* u) {
        auto& p = *static_cast<Pick*>(u);
        if (noun == 0x18EB45Eu && own_family_copy(c, p.herd, p.profile) && game::object_serial(c) != p.skip) {
            const float d = net::dist(p.nest, game::position(c));
            if (d < p.d) {
                p.d = d;
                p.best = c;
            }
        }
        return true;
    }, &p);
    if (!p.best)
        return;
    last = game::object_serial(p.best);
    last_at = now;
    skg::thiscall<void>(g_ptr_assign, slot, p.best); // AddRef, as the game's own offer (0xD529FF)
    skg::field<uint32_t>(p.best, 0xB58) |= 0x10;
    skg::field<uint32_t>(skg::field<void*>(p.best, 0xB4C), 0x5FC) |= 0x4000;
    if (g_mate_offers++ < 10)
        trace::line("creature", "mate offered: own family copy %p, %.1f m from the nest (BUG-9174)", p.best, p.d);
}

static void* (SK_THISCALL* g_get_creature_effect)(void*, uint32_t);
static void* (SK_THISCALL* g_start_creature_effect)(void*, bool, uint32_t, uint32_t);

void mate_hearts_tick(double now)
{
    static double next_scan = 0;
    static uint32_t epoch = 0;
    static std::vector<uint32_t> restored;
    if (!g_mate_classify || !g_get_creature_effect || !g_start_creature_effect ||
        !g_sess.connected() || !game::in_creature_stage()) {
        next_scan = 0;
        restored.clear();
        return;
    }
    if (const auto current = game::stage_epoch(modes::kCreature); current != epoch) {
        epoch = current;
        restored.clear();
        next_scan = 0;
    }
    if (!g_local || game_busy() || now < next_scan)
        return;
    next_scan = now + 1.0;

    struct LocalCreature {
        void* herd;
        net::Vec3 position;
        std::vector<uint32_t> still_eligible;
    } local{game::herd_of(g_local), game::position(g_local), {}};
    if (!local.herd || !game::herd_nest(local.herd)) {
        restored.clear();
        return;
    }
    game::for_each_object([](void* creature, uint32_t noun, void* ctx) {
        auto& local = *static_cast<LocalCreature*>(ctx);
        // (BUG-9174: a guest's own family copy offered as mate gets the hearts too)
        if (noun != 0x18EB45Eu || creature == g_local ||
            (game::is_puppet(creature) && !(g_guest_mate_fix && game::species_profile(creature) == game::species_profile(g_local))) ||
            !game::object_alive(creature) || game::is_dead(creature) || game::herd_of(creature) != local.herd ||
            skg::cdecl_call<int>(g_mate_classify, creature) != 9)
            return true;

        const auto serial = game::object_serial(creature);
        if (std::find(restored.begin(), restored.end(), serial) != restored.end()) {
            local.still_eligible.push_back(serial);
            return true;
        }

        // BUG-0177: restore a missing native cue once per eligibility window.
        // The effect can leave the pool before all its particles have faded.
        constexpr uint32_t mating_hearts = 0x9005E011u;
        if (net::dist(local.position, game::position(creature)) < 40.f &&
            !g_get_creature_effect(creature, mating_hearts) &&
            g_start_creature_effect(creature, true, mating_hearts, mating_hearts))
            local.still_eligible.push_back(serial);
        return true;
    }, &local);
    restored.swap(local.still_eligible);
}

// tests (BUG-9160, the mating call): the own herd's members, the game's mate class (9: may answer the call), the hearts,
// the distance and where each is on screen (to click it)
std::string mates_report()
{
    if (!g_local || !g_mate_classify || !g_get_creature_effect) return "no own creature here\n";
    // BUG-9172: in Cell (or in a load) g_local is no creature: its "herd" field held a float and the nest read faulted
    if (!game::in_creature_stage() || g_local != game::avatar() || (game_busy() & (busy::kLoading | busy::kEditor)))
        return "not in Creature with an own creature (or loading, or in an editor)\n";
    struct Ctx {
        void* herd;
        net::Vec3 me;
        std::string out;
    } c{game::herd_of(g_local), game::position(g_local), {}};
    void* nest = c.herd ? game::herd_nest(c.herd) : nullptr;
    net::Vec3 np{};
    const bool has_nest = nest && game::object_position(nest, np);
    char line[200];
    snprintf(line, sizeof line, "herd %p nest %p%s (%.1f m, at %.1f %.1f %.1f) action %08X target %p\n", c.herd, nest,
             has_nest ? "" : " (none)", has_nest ? net::dist(np, c.me) : -1.f, np.x, np.y, np.z,
             skg::field<void*>(g_local, 0xB4C) ? skg::field<uint32_t>(skg::field<void*>(g_local, 0xB4C), 0x1D8) : 0u,
             g_get_animal_target(g_local));
    c.out = line;
    snprintf(line, sizeof line, "mate step: calls %u, last %.1f s ago, state %u %u %u %u, reached avatar %d mate %d (%p)\n",
             g_mstep_calls, g_mstep_at < 0 ? -1.0 : H->time_seconds() - g_mstep_at, g_mstep_state[0], g_mstep_state[1],
             g_mstep_state[2], g_mstep_state[3], g_mstep_reached[0], g_mstep_reached[1], g_mstep_mate);
    c.out += line;
    snprintf(line, sizeof line, "own species %p/%08X, herd species %p\n", game::species_profile(g_local),
             game::species_key(g_local).instance, c.herd ? game::herd_species(c.herd) : nullptr);
    c.out += line;
    snprintf(line, sizeof line, "mate keeper %p slot %p wait %d, copies offered %u (BUG-9174), calls %u lends %u\n", g_mate_keeper,
             g_mate_keeper ? skg::field<void*>(g_mate_keeper, 0x38) : nullptr, skg::field<int32_t>(g_local, 0x1678), g_mate_offers,
             g_keeper_calls, g_keeper_lends);
    c.out += line;
    game::for_each_object([](void* creature, uint32_t noun, void* ctx) {
        auto& c = *static_cast<Ctx*>(ctx);
        if (noun != 0x18EB45Eu || creature == g_local || !game::object_alive(creature) || game::herd_of(creature) != c.herd)
            return true; // (copies of the host's members count: a guest's family, BUG-9169)
        const net::Vec3 p = game::position(creature);
        float w[3] = {p.x, p.y, p.z}, sc[2] = {-1, -1};
        const bool on = H->world_to_screen(w, sc) != 0;
        char line[240], lent[48] = "";
        if (npc::is_proxy(creature))
            snprintf(lent, sizeof lent, " (copy%s, lent %.1f)", game::is_puppet(creature) ? " puppet" : "", npc::lent_for(creature));
        snprintf(line, sizeof line, "  %p%s class %d hearts %d dead %d %.1f m screen %s%.0f %.0f species %p/%08X\n", creature,
                 lent,
                 skg::cdecl_call<int>(g_mate_classify, creature), g_get_creature_effect(creature, 0x9005E011u) ? 1 : 0,
                 (int)game::is_dead(creature), net::dist(c.me, p), on ? "" : "(behind) ", sc[0], sc[1],
                 game::species_profile(creature), game::species_key(creature).instance);
        c.out += line;
        return true;
    }, &c);
    return c.out;
}

// ---- BUG-9156: spectating while downed (owner decision 1W: "Tab works, but also a left and right arrow next to the
// name you're spectating") ----
// A downed player (Creature, Adventure) watches the nearest partner: the camera is moved by the partner's offset from
// the own creature after the game's camera update (fov::set_camera_offset), "SPECTATING <name>" with < > buttons under
// the revive ring, Tab or the buttons switch partners, a red edge round the screen. `coop.fix creature.spectate off`.
static bool g_spectate_fix = true;
COOP_FIX("creature.spectate", "BUG-9156", g_spectate_fix,
         "creature / adventure: a downed player watches a partner (SPECTATING, Tab, < >) with a red screen edge");
static uint8_t g_spec_id = 0xFF;
static uint32_t g_spec_clicks;
static bool g_spec_tab;
static int g_spec_btn[3][4]; // the < and > buttons and GIVE UP on screen: x0, y0, x1, y1
static uint32_t g_spec_switches;
static int g_spec_step; // a native < > or GIVE UP press (coop_ui::set_downed_hud), consumed by spectate_tick
static void on_downed_button(int code)
{
    if (g_mode == modes::kCell) { // BUG-9171: the Cell knockout in the same box
        if (code == 0) cellsync::give_up();
        else cellsync::spectate_step(code);
        return;
    }
    if (code == 0) creature_rescue::give_up_now();
    else g_spec_step = code;
}

static bool spectating()
{
    return g_spectate_fix && g_local && g_sess.connected() && creature_rescue::down() &&
           (g_mode == modes::kCreature || g_mode == modes::kScenario);
}

struct SpecTarget { uint8_t id; net::Vec3 pos; bool down; float dist; };
static std::vector<SpecTarget> spec_targets()
{
    std::vector<SpecTarget> out;
    const net::Vec3 me = modes::entity_pos(g_adapter, g_local);
    for (const auto& [id, r] : g_remotes) {
        if (r.echo || id >= net::kMaxPlayers || r.buf.empty() || !r.puppet || !r.puppet_adapter) continue;
        const auto& s = r.buf.latest();
        if (!same_place(s) || !(s.flags & net::PF_IN_WORLD) || (s.flags & net::PF_DEAD)) continue;
        const net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
        out.push_back({id, p, (s.flags & net::PF_DOWNED) != 0, net::dist(p, me)});
    }
    std::sort(out.begin(), out.end(), [](const SpecTarget& a, const SpecTarget& b) {
        return a.down != b.down ? !a.down : a.dist < b.dist; // standing partners first, the nearest first
    });
    return out;
}

static void spectate_tick()
{
    if (!spectating()) { g_spec_id = 0xFF; return; }
    auto targets = spec_targets();
    if (targets.empty()) { // no partner to watch: only GIVE UP is there
        g_spec_id = 0xFF;
        mouse::install(H->game_window());
        if (mouse::clicks() != g_spec_clicks) {
            g_spec_clicks = mouse::clicks();
            int x, y;
            mouse::last_click(x, y);
            if (x >= g_spec_btn[2][0] && x <= g_spec_btn[2][2] && y >= g_spec_btn[2][1] && y <= g_spec_btn[2][3])
                creature_rescue::give_up_now();
        }
        return;
    }
    // the order of the switch is the players' order, not the distance (stable while they move)
    std::vector<uint8_t> ids;
    for (const auto& t : targets) ids.push_back(t.id);
    std::sort(ids.begin(), ids.end());
    auto at = std::find(ids.begin(), ids.end(), g_spec_id);
    if (at == ids.end()) { g_spec_id = targets.front().id; at = std::find(ids.begin(), ids.end(), g_spec_id); }
    int step = g_spec_step;
    g_spec_step = 0;
    HWND gw = H->game_window ? (HWND)H->game_window() : nullptr;
    const bool tab = gw && GetForegroundWindow() == gw && (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    if (tab && !g_spec_tab) step = 1;
    g_spec_tab = tab;
    mouse::install(H->game_window());
    if (mouse::clicks() != g_spec_clicks) {
        g_spec_clicks = mouse::clicks();
        int x, y;
        mouse::last_click(x, y);
        for (int b = 0; b < 3; ++b)
            if (x >= g_spec_btn[b][0] && x <= g_spec_btn[b][2] && y >= g_spec_btn[b][1] && y <= g_spec_btn[b][3]) {
                if (b == 2) creature_rescue::give_up_now(); // GIVE UP (holding G still works)
                else step = b == 0 ? -1 : 1;
            }
    }
    if (step && ids.size() > 1) {
        const int n = (int)ids.size();
        const int i = (int)(at - ids.begin());
        g_spec_id = ids[(size_t)(((i + step) % n + n) % n)];
        ++g_spec_switches;
    }
}

// fov's camera target: the followed partner's puppet (null: the own creature)
static void* spectate_target()
{
    if (!spectating() || g_spec_id == 0xFF) return nullptr;
    auto it = g_remotes.find(g_spec_id);
    if (it == g_remotes.end() || !it->second.puppet || !it->second.puppet_adapter ||
        !creature_puppet(it->second.puppet_adapter) || !game::creature_listed(it->second.puppet)) return nullptr;
    return it->second.puppet;
}

// the owner's mock-up (2026-11-12): over the ability bar, a dark box "SPECTATING" with "< name >", a red GIVE UP button
// under it and the revive ring under that, the red screen edge round it all
static void bold(int x, int y, uint32_t color, const char* text)
{
    H->overlay_text(x + 1, y + 1, 0xFF000000, text);
    H->overlay_text(x, y, color, text);
    H->overlay_text(x + 1, y, color, text);
}
// BUG-9261 (owner 2026-11-18: the loading screens "aren't centered and seem to be low resolution"): the covers' title and
// step line grow with the screen (SporeKit 0.3.18's sized text: 22 and 18 px at 1080p, 29 and 24 at 1440p) and are
// centred by their measured width. Off, or an older SporeKit: the fixed 18 px font centred by the 9 px per letter guess.
static bool g_cover_text = true;
COOP_FIX("ui.cover_text_scale", "BUG-9261", g_cover_text,
         "ui: the loading and pause covers' text grows with the screen and is centred by its measured width");
static bool kit_sized_text()
{
    return H->size >= offsetof(SkHost, overlay_text_px) + sizeof(void*) && H->overlay_text_px;
}
// title (white) at y, step (gold) under it, both centred on cx; returns nothing, draws in SK_EV_OVERLAY only
static void cover_lines(float cx, float y, int sh, const char* title, const std::string& step)
{
    if (!g_cover_text || !kit_sized_text()) {
        bold((int)(cx - text_width(title) / 2.f), (int)y, 0xFFFFFFFFu, title);
        H->overlay_text((int)(cx - text_width(step) / 2.f), (int)y + 22, 0xFFF0C040u, step.c_str());
        return;
    }
    const float k = std::max(1.f, sh / 1080.f);
    const int tp = std::min(64, (int)std::lround(22.f * k)), sp = std::min(54, (int)std::lround(18.f * k));
    const int tw = H->overlay_text_px(0, 0, 0, title, tp, 1), stw = H->overlay_text_px(0, 0, 0, step.c_str(), sp, 1);
    H->overlay_text_px((int)std::lround(cx - tw / 2.f), (int)y, 0xFFFFFFFFu, title, tp, 0);
    H->overlay_text_px((int)std::lround(cx - stw / 2.f), (int)std::lround(y + tp + 4.f * k), 0xFFF0C040u, step.c_str(), sp, 0);
}
static void draw_downed_hud(int sw, int sh)
{
    std::memset(g_spec_btn, 0, sizeof g_spec_btn);
    ring_timer::vignette(H, sw, sh, 0.85f);
    const float cx = sw / 2.f;
    const float ring_r = std::max(18.f, sh * 0.036f);
    const float ring_y = sh * 0.815f;
    // the owner (2026-11-12): the button as wide as the box, the same gap between box, button and ring
    const float gap = 8.f;
    const float btn_h = 36.f; // the Co-op window's button height
    const float btn_y1 = ring_y - ring_r - 3.f - gap, btn_y0 = btn_y1 - btn_h; // the ring's dark disc: radius + 3
    const bool cell = g_mode == modes::kCell; // BUG-9171
    const std::string cell_name = cell ? cellsync::spectated() : std::string();
    const bool box = cell ? !cell_name.empty() : spectating() && g_spec_id != 0xFF;
    const float box_h = 74.f; // 10 + the title row 24 + the arrow row 30 + 10 (coop_ui dh_apply)
    const std::string name = !box ? std::string() : cell ? cell_name : g_sess.name_of(g_spec_id);
    // wide enough for the name on one line between the arrows (the owner's quality check 2026-11-13: "CreatureHost"
    // broke in two over the title): about 11 px a character of the Co-op window's font, the two arrows 60 each
    size_t chars = 0;
    for (unsigned char c : name) chars += (c & 0xC0) != 0x80;
    const float w = std::min(sw * 0.6f, std::max(std::max(220.f, sw * 0.17f), chars * 11.f + 16.f + 2 * 60.f));
    const sui::Rect give{cx - w / 2, btn_y0, cx + w / 2, btn_y1};
    const sui::Rect box_area{cx - w / 2, btn_y0 - gap - box_h, cx + w / 2, btn_y0 - gap};
    // the Co-op window's own look (owner 2026-11-12: "ensure consistency"); drawn by the mod only without it
    const bool native = coop_ui::set_downed_hud(true, box, name, give, box_area, on_downed_button);
    if (!native) {
        if (box) {
            ring_timer::panel(H, box_area.x1, box_area.y1, box_area.x2, box_area.y2, 10.f, 0xE0141820u, 0xFFB8BCC4u);
            const char* title = "SPECTATING";
            bold((int)(cx - text_width(title) / 2.f), (int)(box_area.y1 + 6), 0xFFFFFFFFu, title);
            const float ny = box_area.y1 + box_h * 0.62f;
            H->overlay_text((int)(cx - text_width(name) / 2.f), (int)(ny - 6), 0xFFD8DCE0u, name.c_str());
            const float ax[2] = {box_area.x1 + 16.f, box_area.x2 - 16.f};
            for (int b = 0; b < 2; ++b) {
                ring_timer::arrow(H, ax[b], ny, 7.f, b == 0, 0xFFFFD040u);
                g_spec_btn[b][0] = (int)(ax[b] - 14); g_spec_btn[b][1] = (int)(ny - 14);
                g_spec_btn[b][2] = (int)(ax[b] + 14); g_spec_btn[b][3] = (int)(ny + 14);
            }
        }
        ring_timer::panel(H, give.x1, give.y1, give.x2, give.y2, btn_h / 2, 0xF0B01E1Eu, 0xFF3A0808u);
        const char* text = "GIVE UP";
        bold((int)(cx - text_width(text) / 2.f), (int)(btn_y0 + btn_h / 2 - 7), 0xFFFFFFFFu, text);
        g_spec_btn[2][0] = (int)give.x1; g_spec_btn[2][1] = (int)give.y1;
        g_spec_btn[2][2] = (int)give.x2; g_spec_btn[2][3] = (int)give.y2;
    }
    if (ring_timer::enabled())
        ring_timer::draw(H, cx, ring_y, ring_r, cell ? cellsync::own_left() : creature_rescue::own_left(),
                         cell ? cellsync::own_revive() : creature_rescue::own_revive(), H->time_seconds());
}

static bool g_reload_cover = true;
COOP_FIX("ui.reload_cover", "BUG-9195", g_reload_cover,
         "ui: the loading screen covers a world reload that comes in the middle of a stage (the galaxy menu and the load)");
static bool g_editor_cover = true;
COOP_FIX("ui.editor_cover", "BUG-9205", g_editor_cover, "ui: the loading screen covers the black after the Creature editor (until the egg scene shows)");
static bool g_join_cover = true;
COOP_FIX("ui.join_cover", "BUG-9202", g_join_cover,
         "ui: the loading screen covers a guest's join (the galaxy menu while the host's world comes, and its load)");
// BUG-9274 (Live audit 2026-11-19, MSI pair): a guest with no creature of its own joined a Creature host from inside
// its own Cell game: the reload cover (BUG-9195) started in the stage and stayed over the first-creature editor
// (BUG-9263) until its 120 s limit ("Getting the latest world" over the editor). A cover ends when an editor is open;
// the join cover takes over by itself once the editor is left for the load. Off: the cover stays (its time limit).
static bool g_cover_editor_end = true;
COOP_FIX("ui.cover_ends_in_editor", "BUG-9274", g_cover_editor_end,
         "ui: a loading screen never stays over an open editor (a joiner's first-creature editor)");
// ---- the Party panel (the owner's mockup 2026-11-13, coop_ui::set_party_hud) ----
// decision 3E C: a partner's arrow is a slim chevron in the HUD's blue with a white edge and a name plate
// (images/partner_arrow_mockup_C.png); off: the green arrow and the name line
static bool g_partner_chevron = true;
COOP_FIX("ui.partner_chevron", "BUG-9191", g_partner_chevron,
         "ui: a partner's arrow is the HUD-blue chevron with a name plate (decision 3E C)");
static bool g_party_panel = true;
COOP_FIX("ui.party_panel", "BUG-9191", g_party_panel,
         "ui: the Party panel (name plates, creature cards, health) instead of the CO-OP player list");
static const char* editor_name(uint32_t editor)
{
    switch (editor) {
    case 0x96B24187u: return "Ship Editor";
    case 0x99E92F05u: return "Building Editor";
    case 0x7D433FADu: return "Vehicle Editor";
    case 0x156276D1u: return "Outfitter";
    case 0x465C50BAu: return "Creature Editor";
    default: return "Editor";
    }
}
static std::string party_place(uint32_t mode, uint32_t editor)
{
    if (mode == modes::kEditor || mode == modes::kEdit) return editor_name(editor);
    return mode ? modes::mode_name(mode) : "";
}
// the UI scale setting (Settings, under Borderless; lineages-graphics.ini ui_scale), read again every 2 s
static float ui_scale()
{
    static double next = 0;
    static float value = 1.f;
    const double now = H->time_seconds();
    if (now >= next) {
        next = now + 2;
        value = lineages_graphics::read_ui_scale(slotfs::widen(H->mods_dir()) + L"\\" + lineages_graphics::file_name) / 100.f;
    }
    return value;
}
void open_sporepedia_of(uint8_t player); // below (the portrait's click)
static bool g_party_editor_hide = true;
COOP_FIX("ui.party_editor_hide", "BUG-9228", g_party_editor_hide, "ui: the Party cards are hidden while an editor is open");
// BUG-9275 (Live audit 2026-11-19, Space on the MSI pair): a guest with no creature of its own plays the host's (its
// captain in Space, its species elsewhere) and sends no picture: its Party card was empty on every screen. Its card
// shows the host's creature's picture while it plays a stage (not in an editor: it may be making its own). Off: empty.
static bool g_party_host_pic = true;
COOP_FIX("ui.party_host_picture", "BUG-9275", g_party_host_pic,
         "ui: the Party card of a player without a creature of their own shows the host's creature, which they play");
static bool update_party_panel(bool labels)
{
    std::vector<coop_ui::PartyRow> rows;
    const bool by_party = parties::active(); // 6J: the own party's cards only (6K: on a galaxy, the server's party)
    for (auto& p : g_sess.players()) {
        if (by_party && !parties::same_party(g_sess.my_id(), p.id))
            continue;
        coop_ui::PartyRow r;
        r.player = p.id;
        r.name = p.name;
        r.me = p.id == g_sess.my_id();
        net::PlayerState st;
        bool have = false;
        if (r.me) {
            st = local_state();
            have = true;
        } else if (auto it = g_remotes.find(p.id); it != g_remotes.end() && !it->second.buf.empty()) {
            st = it->second.buf.latest();
            have = true;
        }
        if (have) {
            r.place = party_place(st.mode, st.editor);
            if (st.max_health > 0 && std::isfinite(st.health)) r.health = std::clamp(st.health / st.max_health, 0.f, 1.f);
            r.energy = st.energy;
            r.stage = st.mode;
        }
        // the creature's picture, sent by its owner (creations.cpp kPortrait) under its shared key
        r.portrait = creations::portrait_of(p.id);
        if (!r.portrait && g_party_host_pic && p.id != 0 && have && stage_mode(st.mode))
            r.portrait = creations::portrait_of(0); // BUG-9275
        r.portrait_group = creations::portrait_group();
        rows.push_back(std::move(r));
    }
    (void)labels;
    // not over the Sporepedia, the pause menu or a caption: the game time stands then (busy kPaused / kInputLock)
    // BUG-9228 (owner play test 2026-11-15: "Party is covering the next page button in the editor for painting"): not in
    // an editor (its pages' buttons sit at the bottom right where the cards go). `coop.fix ui.party_editor_hide off`.
    const bool editor = g_party_editor_hide && (g_mode == modes::kEditor || g_mode == modes::kEdit);
    const bool show = !(game_busy() & (busy::kPaused | busy::kInputLock)) && !editor;
    return coop_ui::set_party_hud(show, rows, ui_scale(), open_sporepedia_of);
}

// a portrait click: that player's creature in the game's own Sporepedia (BUG-9191). The game's Sporepedia browser
// launch (SP::cSPUIAssetBrowser::AdvancedLaunch, ModAPI Sporepedia::ShopperRequest::Show, cdecl(request&)) with the
// creature as the request's current selection (+0x18, ModAPI's currentSelection, the dev name mInitialKey); the
// request (0x48 bytes) made by the game's own constructor (thiscall, the GlobalTemplate shopper) and taken down by its
// destructor. Prologues checked at runtime 2026-11-13 (Show 0xD6A900: sub esp,40 ... [esp+54]; the constructor
// 0xD64AC0 writes the SDK's defaults). Outside the frame (UI).
static uint32_t g_sporepedia_opened, g_sporepedia_failed;
static std::vector<std::pair<uint32_t, uint8_t>> g_sporepedia_bytes; // tests: request bytes (offset, value) to set
void open_sporepedia_of(uint8_t player)
{
    creations::Key k = player == g_sess.my_id() ? creations::mine(creations::kCreature).key
                                                 : creations::player_key(player, creations::kCreature);
    if (!k.instance) // the host that plays its world's species: the creature of its picture (that species here)
        if (uint32_t inst = creations::portrait_of(player)) k = {inst, 0x2B978C46u, 0x40626200u};
    H->log(M, SK_INFO, "ui: Party portrait of player %u clicked: creature %08X/%08X/%08X", player, k.instance, k.type, k.group);
    if (!k.instance) {
        feed(g_sess.name_of(player) + "'s creature is not here yet.");
        return;
    }
    static uint32_t key[3];
    key[0] = k.instance;
    key[1] = k.type ? k.type : 0x2B978C46u;
    key[2] = k.group ? k.group : 0x40626200u;
    H->run_outside_frame([](void*) {
        static uintptr_t ctor = H->sym("SporepediaRequest_ctor"), dtor = H->sym("SporepediaRequest_dtor"),
                         show = H->sym("Sporepedia::ShopperRequest::Show");
        if (!ctor || !dtor || !show) {
            ++g_sporepedia_failed;
            return;
        }
        alignas(8) uint8_t req[0x60] = {};
        skg::thiscall<void*>(ctor, req);
        memcpy(req + 0x18, key, sizeof key);
        // the large card of that creation, not only the grid: AdvancedLaunch opens the card when +0x24 is set, a key is
        // given and +0x13 is clear (the constructor sets +0x13 = 1: the grid; spore-decomp, BUG-9191)
        req[0x13] = 0;
        for (auto [off, v] : g_sporepedia_bytes)
            if (off < 0x48) req[off] = v;
        skg::cdecl_call<void>(show, (void*)req);
        skg::thiscall<void>(dtor, req);
        ++g_sporepedia_opened;
        H->log(M, SK_INFO, "ui: the Sporepedia asked to show %08X/%08X/%08X", key[0], key[1], key[2]);
    }, nullptr);
}

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

// 6J: where each partner showed in the last overlay pass (client pixels): its name plate or tag, its creature's point
// and a reach around it (0: not on the screen), and when
struct PickSpot {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, cx = 0, cy = 0, reach = 0;
    double at = -1;
};
static std::map<uint8_t, PickSpot> g_pick;

void on_overlay(void*, int, void*)
{
    static bool s_downed_parts;
    const bool cell_down = g_spectate_fix && g_mode == modes::kCell && cellsync::own_down(); // BUG-9171
    const bool downed_now = (g_spectate_fix && creature_rescue::down() && (g_mode == modes::kCreature || g_mode == modes::kScenario)) ||
                            cell_down;
    if (s_downed_parts && !downed_now) coop_ui::set_downed_hud(false, false, "", {}, {}, nullptr); // BUG-9156: up again
    if (!cell_down) cellsync::set_box_shown(false);
    s_downed_parts = downed_now;
    if (build_id() == 0xDC04AEE5u) editor_depth::draw(); // BUG-9103
    coop_ui::draw_tooltip(); // BUG-9107
    modes::CellScope cell_scope; // BUG-0271
    if (!g_menu_up)
        progress_debt::draw(); // BUG-9119: the red stretch on the progress bar
    if (!g_overlay)
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    // BUG-9195 (the owner 2026-11-13: "Maybe instead of that [the notice], we can do our loading screen to overlay
    // it?"): a world from the host arriving while this guest plays a stage (its own new nest, a party wipe, a reload)
    // took it through the galaxy menu and a load in plain sight. The loading screen covers it from the first byte until
    // it plays the stage again (2 s settled; 120 s at most). `coop.fix ui.reload_cover off`: the galaxy menu shows.
    // BUG-9202: the join itself (the galaxy menu while the host saves and sends, then a black load) had no loading
    // screen: the same screen covers it, from the moment the host plays a stage while this guest is in the menu or
    // loading, until it plays the stage (2 s settled; 180 s at most; not while the Co-op window is open, so a stuck
    // join can still be left). `coop.fix ui.join_cover off`: the galaxy menu and the black load show.
    // BUG-9205 (owner play test 2026-11-14: "When Titan finished evolving, they were stuck on a black screen for a while"):
    // the return from the Creature editor is the game's own black while it builds the new body, then its fade and the egg
    // scene (12-22 s at 10 fps). The same screen covers the black only: from the step out of the editor until the frame is
    // no longer dark (0.5 s), so the egg scene shows. `coop.fix ui.editor_cover off`: the black shows.
    {
        static double s_since = -1, s_back_at = -1;
        static bool s_nest = false, s_join = false, s_editor = false;
        static uint32_t s_prev_mode = 0;
        const double now = H->time_seconds();
        float got = 0;
        const bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
        const bool arriving = guest && world::incoming(got);
        const bool in_stage = g_mode == modes::kCreature || g_mode == modes::kCell || g_mode == modes::kTribe ||
                              g_mode == modes::kCiv || g_mode == modes::kSpace;
        const bool in_menu = g_mode == modes::kMenu || g_mode == modes::kLoad;
        bool host_plays = false;
        if (auto it = g_remotes.find(0); it != g_remotes.end() && !it->second.buf.empty())
            host_plays = stage_mode(it->second.buf.latest().mode);
        if (g_reload_cover && s_since < 0 && arriving && in_stage && !world::local_intro_running()) {
            s_since = now;
            s_back_at = -1;
            s_join = s_editor = false;
            s_nest = g_mode == modes::kCreature; // in Creature mid-play: the own nest's world (BUG-9167) as a rule
        }
        if (g_join_cover && s_since < 0 && guest && in_menu && (arriving || host_plays) && !coop_ui::is_open()) {
            s_since = now;
            s_back_at = -1;
            s_join = true;
            s_nest = false;
            s_editor = false;
        }
        const bool from_editor = (s_prev_mode == modes::kEdit || s_prev_mode == modes::kEditor) && g_mode == modes::kCreature;
        if (g_mode) s_prev_mode = g_mode;
        if (g_editor_cover && s_since < 0 && from_editor && g_sess.connected()) {
            s_since = now;
            s_back_at = -1;
            s_join = s_nest = false;
            s_editor = true;
        }
        if (s_since >= 0) {
            // back in the stage: the player's own creature / tribe / city is there and no cinematic runs (the game is in
            // the stage mode while its own black loading and the entry fade still run, ~10 s on the MSI, 2026-11-14)
            static double s_stage_at = -1, s_cine_s = 0, s_last = 0;
            if (!in_stage) s_stage_at = -1;
            else if (s_stage_at < 0) s_stage_at = now;
            const bool cine = in_stage && world::cinematic_playing();
            if (cine) s_cine_s += std::min(0.5, now - s_last);
            s_last = now;
            // and the game's frame under the screen is no longer black (a dark place passes after 60 s of it; the
            // Tribe join showed 30 s of black in the stage mode on the MSI, 2026-11-14)
            static double s_ready_at = -1;
            const bool ready = s_editor ? in_stage && g_local // (the egg scene after it shows)
                                        : in_stage && !arriving && (!world::load_queued() || world::local_intro_running()) &&
                                              g_local && !cine;
            if (!ready) s_ready_at = -1;
            else if (s_ready_at < 0) s_ready_at = now;
            const float luma = ring_timer::scene_luma(now);
            const bool dark = luma >= 0 && luma < 16.f && now - s_ready_at < 60;
            if (ready && !dark) {
                if (s_back_at < 0) s_back_at = now;
            } else
                s_back_at = -1;
            const char* why = (s_back_at >= 0 && now - s_back_at > (s_editor ? 0.5 : 2) && now - s_since > (s_editor ? 1 : 4)) ? "playing"
                              : now - s_since > (s_join ? 180 : s_editor ? 60 : 120)    ? "time limit"
                              : !g_sess.connected()                                     ? "disconnected"
                              : s_join && coop_ui::is_open()                            ? "co-op window"
                              : g_cover_editor_end && (g_mode == modes::kEdit || g_mode == modes::kEditor) ? "an editor" // BUG-9274
                              : s_join && !in_menu && !in_stage                         ? "other mode"
                              : s_editor && g_mode != modes::kCreature                  ? "other mode"
                                                                                        : nullptr;
            if (why) {
                H->log(M, SK_INFO, "ui: the %s loading screen ended (%s) after %.1f s, %.1f s of it in the stage, %.1f s "
                       "cinematic, %.1f s ready but black, brightness %.0f (BUG-9202)", s_join ? "join" : s_editor ? "editor return" : "reload", why,
                       now - s_since, s_stage_at >= 0 ? now - s_stage_at : 0.0, s_cine_s,
                       s_ready_at >= 0 && s_back_at >= 0 ? s_back_at - s_ready_at : -1.0, luma);
                s_since = -1;
                s_stage_at = -1;
                s_cine_s = 0;
                s_ready_at = -1; // BUG-9205: the next cover's black is counted from its own start
            }
        }
        if (s_since >= 0 && sw > 0) {
            // the join: 0..0.1 while the host saves, the transfer to 0.7, then the load
            static double s_recv_end = -1;
            if (arriving || s_recv_end < 0 && (in_stage || g_mode == modes::kLoad)) s_recv_end = now;
            if (now - s_since < 0.05) s_recv_end = -1; // a new cover
            const bool saving = s_join && !arriving && s_recv_end < 0;
            const float p = s_join ? (arriving ? 0.1f + 0.6f * got
                                     : saving ? std::min(0.1f, (float)(now - s_since) * 0.01f)
                                              : std::min(0.95f, 0.7f + (float)(now - s_recv_end) * 0.02f))
                           : arriving ? 0.6f * got : std::min(0.95f, 0.6f + (float)(now - s_since) * 0.01f);
            // only forward: a second world during the same join (a Tribe joiner's new village) restarts the share
            static float s_shown = 0;
            if (now - s_since < 0.05) s_shown = 0;
            s_shown = std::max(s_shown, p);
            const float cx = sw / 2.f, cy = sh * 0.45f, r = std::max(22.f, sh * 0.05f);
            ring_timer::loading(H, sw, sh, cx, cy, r, s_shown, now);
            const std::string host = g_sess.name_of(0);
            const std::string title = s_join ? "Joining " + host : s_editor ? "Evolving" : s_nest ? "Bringing your nest" : "Getting the latest world";
            const std::string step = s_editor ? "Building your new body..."
                                     : arriving ? "Receiving " + host + "'s world..."
                                     : in_stage ? "Final touches..."
                                     : saving ? host + " is saving the world..."
                                     : "Loading...";
            cover_lines(cx, cy + r + 18.f * std::max(1.f, sh / 1080.f), sh, title.c_str(), step);
            return; // nothing else over the loading screen
        }
    }
    // BUG-9227 (owner play test 2026-11-15: "When host pauses, it should add the loading overlay without the loading icon
    // saying paused by host"): while the host's state says its game time stands (PF_PAUSED) and this guest plays the same
    // stage of the same world, the loading screen's cover without its ring and "Paused by host".
    if (g_paused_cover && g_sess.role() == net::Role::Client && g_sess.connected() && sw > 0) {
        auto it = g_remotes.find(0);
        if (it != g_remotes.end() && !it->second.buf.empty()) {
            const auto& hs = it->second.buf.latest();
            if ((hs.flags & net::PF_PAUSED) && same_place(hs) && it->second.buf.age(H->time_seconds()) < 3.0) {
                const float cx = sw / 2.f, cy = sh * 0.45f;
                ring_timer::loading(H, sw, sh, cx, cy, 0.f, 0.f, H->time_seconds());
                const std::string step = g_sess.name_of(0) + " paused the game";
                cover_lines(cx, cy - 10.f * std::max(1.f, sh / 1080.f), sh, "Paused by host", step);
                return; // nothing else over the cover
            }
        }
    }
    // BUG-9159 (owner decision 1P A): a late joiner whose own creature is chosen (after the editor) waits for their
    // village: the frame blurred, a ring and short steps until it is there (90 s at most)
    {
        static double s_since = -1;
        const bool civ_wait = g_mode == modes::kCiv && g_civ_editor_first && civ::late_prompt() == 2 &&
                              g_civ_editor_opened_world == g_world; // BUG-9170: its capital is on its way
        const bool waiting = ((g_mode == modes::kTribe && tribe::late_prompt() == 2 /* VillageComing */) || civ_wait) &&
                             sw > 0 && !coop_ui::is_open() && !g_menu_up;
        const double now = H->time_seconds();
        if (!waiting) s_since = -1;
        else if (s_since < 0) s_since = now;
        if (waiting && now - s_since < 90) {
            const float p = tribe_late::loading_progress(now - s_since);
            const float cx = sw / 2.f, cy = sh * 0.45f, r = std::max(22.f, sh * 0.05f);
            ring_timer::loading(H, sw, sh, cx, cy, r, p, now);
            const std::string host = g_sess.name_of(0);
            const std::string steps[4] = {"Catching up to " + host + "...", "Matching tools...", "Buildings are rising...",
                                          "Final touches..."};
            const char* title = civ_wait ? "Founding your capital" : "Founding your village";
            cover_lines(cx, cy + r + 18.f * std::max(1.f, sh / 1080.f), sh, title, steps[tribe_late::loading_step(p)]);
            return; // nothing else over the loading screen
        }
    }
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
    // BUG-9127: the Creature / Adventure knockout was invisible (its text only in a console report):
    // the downed player's countdown and the partner's "revive" line, centred under the top bar
    if (creature_rescue::ui_shown() && labels && (g_mode == modes::kCreature || g_mode == modes::kScenario) && sw > 0) {
        const std::string line = creature_rescue::ui_line(); // BUG-9141: with the ring only "Hold G to give up"
        // BUG-9141: this downed player's own ring, large in the middle under the top bar, its hint under it
        int line_y = (int)(sh * 0.16f);
        const bool downed_hud = g_spectate_fix && creature_rescue::down(); // BUG-9156: the owner's layout at the bottom
        creature_rescue::set_give_up_button(downed_hud);
        if (!downed_hud) coop_ui::set_downed_hud(false, false, "", {}, {}, nullptr);
        if (downed_hud) draw_downed_hud(sw, sh);
        else if (ring_timer::enabled() && creature_rescue::down()) {
            const float r = ring_timer::radius_for(sh) * 1.4f;
            ring_timer::draw(H, sw / 2.f, sh * 0.16f + r, r, creature_rescue::own_left(), creature_rescue::own_revive(),
                             H->time_seconds());
            line_y = (int)(sh * 0.16f + 2.f * r + 10.f);
        }
        if (!line.empty() && !downed_hud) {
            const bool own = creature_rescue::down();
            const int x = sw / 2 - text_width(line) / 2, y = line_y;
            H->overlay_text(x + 1, y + 1, 0xFF000000, line.c_str());
            H->overlay_text(x, y, own ? 0xFFFF6060 : 0xFFFFD040, line.c_str());
        }
    }
    if (cell_down && labels && sw > 0) { // BUG-9171: the Cell knockout in the Creature's downed box (1W: < > and GIVE UP)
        draw_downed_hud(sw, sh);
        cellsync::set_box_shown(true);
    } else if (cell_down) {
        cellsync::set_box_shown(false);
    }
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
        if (labels && !g_partner_arrows) // Lineages: our partner arrows (BUG-9097 / 9191) replace upstream's (no doubles)
            pfind::draw_arrows(ts, has_me ? me : nullptr);
        pfind::set_map_targets(ts, g_mode);
    }
    // name tags above remote players
    // (Lineages: the partner arrows below stay with all.name_tags off, as upstream's arrows did; only the tags go)
    void* av = labels ? g_local : nullptr;
    if (g_tag_self && av && tags) {
        net::Vec3 p = modes::entity_pos(g_adapter, av);
        float sc[2] = {0, 0}, w[3] = {p.x, p.y, p.z};
        int ok = H->world_to_screen(w, sc);
        g_dbg_sc[0] = sc[0]; g_dbg_sc[1] = sc[1]; g_dbg_sc[2] = (float)ok;
        if (ok)
            H->overlay_text((int)sc[0] - 4, (int)sc[1] - 10, 0xFFFF4040, "+");
    }
    g_partner_indicators = 0;
    for (auto& [id, r] : g_remotes) {
        if (!av)
            continue;
        if (g_mode == modes::kCell && cellsync::partner_down(id))
            continue; // the red rescue indicator already identifies this partner
        const double label_now = H->time_seconds();
        if (!r.echo && (r.buf.empty() || r.buf.age(label_now) >= 5.0 || !same_place(r.buf.latest())))
            continue;
        net::Vec3 p;
        float h = 3.f;
        if (g_mode == modes::kScenario) {
            net::PlayerState s;
            double now = H->time_seconds();
            if (!r.buf.sample(now, s) || r.buf.age(now) >= 5.0 || !same_place(s) ||
                !(s.flags & net::PF_IN_WORLD) || (s.flags & net::PF_DEAD))
                continue;
            p = s.pos;
        } else {
            if (!r.puppet || !r.puppet_adapter)
                continue;
            auto* ad = r.puppet_adapter;
            if (g_tribe_tag_fix && ad->mode == modes::kTribe) // BUG-0907: the pointer has nothing to label
                continue;
            p = modes::entity_pos(ad, r.puppet);
            if (pfind::g_placed_fix && !find_placed(p, 0.f)) // BUG-0939: a pointer never put on the planet has no name
                continue;
            if (ad->tag_height)
                h = ad->tag_height(r.puppet);
        }
        float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        float up = len > 1 ? h / len : 0; // above the head, along the planet normal
        float w[3] = {p.x + p.x * up, p.y + p.y * up, p.z + p.z * up}, sc[2];
        if (g_adapter->flat) {
            w[0] = p.x;
            w[1] = p.y + h; // cell world: screen up is +y on the plane
            w[2] = p.z;
        }
        // BUG-9121: world_to_screen returns 0 both behind the camera and off the screen (outside 1.2x the
        // view); only behind leaves the point unwritten. Off screen it is still the direction to point.
        sc[0] = sc[1] = NAN;
        H->world_to_screen(w, sc);
        const bool front = g_arrow_fix ? std::isfinite(sc[0]) && std::isfinite(sc[1]) : H->world_to_screen(w, sc) != 0;
        // (BUG-9191: not over the Sporepedia, the pause menu or a caption: the game time stands then, as the Party panel)
        // (BUG-9239: nor over the memory box: the marker is drawn over the UI)
        if (g_partner_arrows && sw > 0 && sh > 0 && !(g_partner_chevron && (game_busy() & (busy::kPaused | busy::kInputLock))) &&
            !mem_refresh::box_up()) {
            const float size = std::clamp(sh / 24.f,30.f,60.f);
            const bool visible = front && sc[0] >= 40 && sc[0] < sw-40 && sc[1] >= 70 && sc[1] < sh-70;
            float x = 0, y = 0, dx = 0, dy = 1;
            if (visible) { x = sc[0]; y = sc[1]-size; }
            else {
                dx = front ? sc[0]-sw*.5f : 0.f;
                dy = front ? sc[1]-sh*.5f : 1.f;
                if (!cell_logic::edge_point(sw*.5f,sh*.5f,dx,dy,110,85,1,.5f,x,y)) continue;
            }
            // BUG-9127: a downed partner's arrow is red (the way to them while the countdown runs)
            const bool partner_down = creature_rescue::ui_shown() && !r.buf.empty() && (r.buf.latest().flags & net::PF_DOWNED);
            const float distance = net::dist(modes::entity_pos(g_adapter,av),p);
            if (!std::isfinite(distance)) continue;
            std::string tag = r.name.substr(0,utf8::cut_bytes(r.name,32)) + "  " + std::to_string(int(std::min(distance,999999.f))) + " m";
            int tx = std::clamp(int(x)-text_width(tag)/2,8,std::max(8,sw-text_width(tag)-8));
            int ty = std::clamp(visible ? int(y-size*.7f)-18 : int(y+size*.7f),8,sh-26);
            // decision 3E C (BUG-9191): the HUD-blue chevron and a name plate (the name, the distance under it)
            std::string name_line, dist_line;
            float px1 = 0, py1 = 0, px2 = 0, py2 = 0;
            const float cs = std::clamp(sh / 19.f, 30.f, 60.f) * ui_scale();
            if (g_partner_chevron) {
                name_line = r.name.substr(0, utf8::cut_bytes(r.name, 32));
                dist_line = std::to_string(int(std::min(distance, 999999.f))) + " m";
                // (centred by the letters' real widths: the flat estimate put the name left of the middle, owner 2026-11-17)
                const float w = (float)std::max(utf8::overlay_text_px(name_line), utf8::overlay_text_px(dist_line)) + 20, h = 38;
                float cx = x, cy = y - cs * .55f - h * .5f - 4;
                if (!visible) { // beside the chevron, towards the middle of the screen
                    const float l = std::hypot(dx, dy) > 0 ? std::hypot(dx, dy) : 1;
                    cx = x - dx / l * (cs * .6f + w * .5f + 6);
                    cy = y - dy / l * (cs * .6f + h * .5f + 6);
                }
                px1 = std::clamp(cx - w * .5f, 4.f, std::max(4.f, sw - w - 4));
                py1 = std::clamp(cy - h * .5f, 4.f, std::max(4.f, sh - h - 4));
                px2 = px1 + w;
                py2 = py1 + h;
                ty = (int)py1;
                tx = (int)px1;
            } else
                partner_marker::draw(H,x,y,dx,dy,size,partner_down ? 0xFFFF5050 : 0xFF70FFB0);
            // BUG-9141: a downed partner's ring (Creature / Adventure): its time from when this game first saw it down,
            // a revive from when this player stood within reach (the host decides; this is what it looks like here)
            if (!partner_down) r.down_since = r.near_since = -1;
            else if (ring_timer::enabled() && (g_mode == modes::kCreature || g_mode == modes::kScenario)) {
                if (r.down_since < 0) r.down_since = label_now;
                if (distance <= creature_rescue::reach_m() && !creature_rescue::down()) {
                    if (r.near_since < 0) r.near_since = label_now;
                } else
                    r.near_since = -1;
                const double win = creature_rescue::window_seconds();
                const float rr = ring_timer::radius_for(sh) * (visible ? 1.f : 0.7f);
                const float ry = visible ? std::max(rr + 4.f, (float)ty - rr - 6.f)
                                         : std::clamp((float)ty + 26.f + rr, rr + 4.f, sh - rr - 4.f);
                const float revive = r.near_since < 0 ? 0.f
                    : (float)std::clamp((label_now - r.near_since) / creature_rescue::hold_seconds(), 0.0, 1.0);
                ring_timer::draw(H, std::clamp(x, rr + 4.f, sw - rr - 4.f), ry, rr,
                                 ring_timer::fraction(win - (label_now - r.down_since), win), revive, label_now);
            }
            // owner 2026-11-18 (the Social Menu "looking very wonky ... when the locator arrows and names are above the
            // menu"): the overlay draws over every window, so a marker that would cross the open panel or menu is not
            // drawn this frame
            const bool covered = coop_ui::ui_covers(std::min(px1, x - 30), std::min(py1, y - 30), std::max(px2, x + 30),
                                                    std::max(py2, y + 30));
            if (covered) {
            } else if (g_partner_chevron) {
                partner_marker::chevron(H, x, y, dx, dy, cs, partner_down ? 0xFFE85050 : 0xFF46AAE6);
                partner_marker::plate(H, px1, py1, px2, py2, 17, 0xA5282828, 0xC8EBF0F5);
                const int nx = (int)((px1 + px2) * .5f) - utf8::overlay_text_px(name_line) / 2;
                const int dxl = (int)((px1 + px2) * .5f) - utf8::overlay_text_px(dist_line) / 2;
                H->overlay_text(nx + 1, (int)py1 + 5, 0xFF000000, name_line.c_str());
                H->overlay_text(nx, (int)py1 + 4, 0xFFFFFFFF, name_line.c_str());
                H->overlay_text(dxl, (int)py1 + 19, 0xFFD4DCE2, dist_line.c_str());
            } else {
                H->overlay_text(tx+1,ty+1,0xFF000000,tag.c_str());
                H->overlay_text(tx,ty,0xFFB0FFD0,tag.c_str());
            }
            ++g_partner_indicators;
            // 6J: where this partner shows (its name plate, its creature on the screen) for the right-click menu
            g_pick[id] = {g_partner_chevron ? px1 : (float)tx, g_partner_chevron ? py1 : (float)ty,
                          g_partner_chevron ? px2 : (float)(tx + text_width(tag)), g_partner_chevron ? py2 : (float)(ty + 16),
                          x, y, visible ? std::max(30.f, size) : 0.f, label_now};
        } else if (tags && front && !(g_partner_chevron && (game_busy() & (busy::kPaused | busy::kInputLock)))) {
            std::string tag = r.name;
            float at[3] = {p.x, p.y, p.z};
            pfind::tag("player", id, tag, sc[0], sc[1], at, 0); // F2 colour, BUG-0949
            g_tags_drawn++;
            g_pick[id] = {sc[0] - 60, sc[1] - 26, sc[0] + 60, sc[1] + 4, sc[0], sc[1], 40.f, H->time_seconds()}; // 6J
        }
    }
    if (av && tags) // tests (BUG-0949): the made-up partners' tags, at their points
        for (const pfind::Target& g : pfind::ghosts()) {
            float w[3] = {g.pos[0], g.pos[1], g.pos[2]}, sc[2];
            if (H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh)
                pfind::tag("player", g.id, g.name, sc[0], sc[1], w, 0);
        }
    pfind::labels_flush(); // BUG-0949: the tags and arrows of this pass, apart and off the HUD
    std::string head;
    if (g_sess.role() == net::Role::Host)
        head = coop_ui::demo_text("CO-OP host :" + std::to_string(g_sess.port())) + "  " + std::to_string(g_sess.players().size()) +
               "/" + std::to_string(std::min<int>(g_sess.seat_limit, g_sess.max_players)); // Lineages slots, all.max_players
    else if (g_sess.role() == net::Role::Client)
        head = std::string("CO-OP guest  ") + (g_sess.connected() ? "connected" : "connecting...");
    else if (g_echo)
        head = "CO-OP echo test";
    // BUG-0155: the block is not drawn over the co-op window or a menu (the lobby shows the same),
    // and the notification panels start below it; `coop.corefix status_block 0` = the old way
    // (g_menu_up is only refreshed in a game: in the main menu the window's own flag counts)
    bool covered = coop_ui::is_open() || (!labels && g_local);
    if (head.empty() || (covered && g_status_block_fix)) {
        coop_ui::set_party_hud(false, {}, ui_scale(), nullptr);
        g_status_block = {0, 0, 0, 0};
        pfind::set_status_rect(0, 0, 0, 0);
        notify::set_top_min(0);
        return;
    }
    // the Party panel (the owner's mockup 2026-11-13) in place of the header line and the player list; the stage's own
    // lines (village food, performance, teamplay) stay at the top right. Without the panel's images: the text list.
    const bool party = g_party_panel && g_sess.connected() && update_party_panel(labels);
    int x = sw > 0 ? sw - 480 : 900, y = 70, wmax = party ? 0 : utf8::overlay_width(head);
    if (!party) {
        H->overlay_text(x, y, 0xFFFFD040, head.c_str());
        y += 22;
    }
    if (g_mode == modes::kTribe && g_sess.connected()) {
        std::string food = tribe::village_food_line();
        if (!food.empty()) {
            H->overlay_text(x, y, 0xFF8CF08C, food.c_str());
            wmax = std::max(wmax, utf8::overlay_width(food));
            y += 20;
        }
        std::string performance = tribe::performance_line();
        if (!performance.empty()) {
            H->overlay_text(x, y, 0xFFFFD040, performance.c_str());
            wmax = std::max(wmax, utf8::overlay_width(performance));
            y += 20;
        }
    }
    // owner 2026-11-17: "Can we get rid of the top right text now, or is it needed?": the pack line is a developer's
    // line (the partner's distance, rescue, joint hits): off unless [coop] pack_line=1 (or `coop.teamplay`)
    static const bool pack_line = H->config_int(M, "coop", "pack_line", 0) != 0;
    if (pack_line && g_sess.connected() && g_mode == modes::kCreature) {
        std::string t = teamplay::status();
        if (!t.empty()) {
            H->overlay_text(x, y, 0xFF8CF08C, t.c_str());
            y += 20;
        }
    }
    for (auto& p : party ? std::vector<net::PlayerInfo>{} : g_sess.players()) {
        std::string line = (p.id == g_sess.my_id() ? "> " : "  ") + p.name;
        auto it = g_remotes.find(p.id);
        if (p.id != g_sess.my_id() && it != g_remotes.end() && !it->second.buf.empty()) {
            const auto& ls = it->second.buf.latest();
            line += std::string("  [") + modes::mode_name(ls.mode) + (other_world_label(p.id, ls) ? ", other world" : "") + "]";
        }
        H->overlay_text(x, y, 0xFFFFFFFF, line.c_str());
        wmax = std::max(wmax, utf8::overlay_width(line));
        y += 20;
    }
    g_status_block = {x, 70, x + wmax, y};
    pfind::set_status_rect(x, 70, x + wmax, y); // BUG-0949: the next pass keeps the labels off it
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
    g_own_anims++;
    g_own_anim_last = anim;
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

static bool g_anim_creature_only_fix = true;
COOP_FIX("core.anim_creature_only", "BUG-0796", g_anim_creature_only_fix, "creature: a partner's animation is never played on its cell or its Tribe marker around a stage switch (the game read a creature's animation data there)");
uint32_t g_anims_not_creature = 0;
void on_remote_anim(const net::AnimEvent& ev)
{
    g_anims_rx++;
    if (g_anim_log)
        LOG("remote %u anim 0x%08X", ev.id, ev.anim);
    auto it = g_remotes.find(ev.id);
    if (it == g_remotes.end() || !it->second.puppet)
        return;
    // BUG-0796: a partner's animation is a creature's (cCreatureBase::PlayAnimation reads the animated creature at
    // +0xB54): around a stage switch the puppet can be a Cell cell (a pool slot) or a Tribe marker (a heap object of
    // 40 bytes), and the reliable MSG_ANIM still comes. Dropped unless the puppet is a live Creature one.
    // Old (`coop.fix core.anim_creature_only off`): only a creature puppet was checked, any other one was played on.
    if (g_anim_creature_only_fix && !creature_puppet(it->second.puppet_adapter)) {
        g_anims_not_creature++;
        return;
    }
    if (creature_puppet(it->second.puppet_adapter) && !game::creature_listed(it->second.puppet)) {
        if (g_frame_fix) { // freed by a teardown the mod has not seen yet (BUG-0091)
            g_anims_dropped++;
            return;
        }
        g_anims_unlisted++;
    }
    game::play_animation(it->second.puppet, ev.anim, ev.block, ev.flags & 1);
    g_anims_played++;
    it->second.anims++;
    it->second.anim_last = ev.anim;
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
// BUG-0770: the session settings of the host and the session report's round / question / upload end on every session
// end (a Leave, a new host or join, the unload), not only when the network drops it (cb.disconnected)
bool g_session_state_end_fix = true;
bool g_settings_in = true; // not a lab switch: tests (coop.corefix settings_in 0) drop the host's settings tables
COOP_FIX("core.session_state_end", "BUG-0770", g_session_state_end_fix, "core: the host's settings table and the session report end with every session");
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
    pfind::on_session_end(); // F2: the colour table was the session's
    support::on_session_end(path == EndPath::Leave ? "left" : path == EndPath::Unload ? "mod unload"
                            : path == EndPath::Restart ? "a new session" : "disconnected");
    if (g_session_end_fix || path == EndPath::Disconnect) {
        npc::on_session_end();
        identity::on_session_end(); // BUG-9135
        party::on_session_end();    // roadmap 0b
        parties::on_session_end();  // 6J
        nest::on_session_end(); // BUG-0384: our egg mirrors
        fields::on_session_end();
        wobj::on_session_end();
        realign::on_session_end();
        parts::on_session_end(); // BUG-0401
        effects::on_session_end(); // BUG-0451
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
        nest::on_session_end(); // BUG-0384: our egg mirrors
        fields::on_session_end();
        wobj::on_session_end();
        realign::on_session_end();
        parts::on_session_end(); // BUG-0401
        effects::on_session_end(); // BUG-0451
        social::on_session_end();
        if (cell)
            cellsync::on_session_end();
        spacecoop::on_session_end();
    } else if (path == EndPath::Unload) {
        npc::on_session_end();
        nest::on_session_end(); // BUG-0384: our egg mirrors
        fields::on_session_end();
        wobj::on_session_end();
        realign::on_session_end();
        parts::on_session_end(); // BUG-0401
        effects::on_session_end(); // BUG-0451
        social::on_session_end();
    }
    if (g_session_end_fix || path != EndPath::Restart) {
        for (auto& [id, r] : g_remotes)
            despawn(r);
        g_remotes.clear();
        views::clear();
    }
    if (g_vote_scope_fix)
        drop_vote("the session ended");
    g_vote_action_due = nullptr; // BUG-0404: a result still waiting at the busy gate ends with its session
    g_puppet_hold = false; // tests (coop.puppet hold, BUG-0251)
    g_puppet_hide_until = 0; // tests (coop.puppet respawn <s>, BUG-0453)
    world::reset_shares();
    adventure::reset();
    creature_rescue::reset();
    progress_debt::reset(); // BUG-9119
    guest_pack::reset();    // BUG-9129
    shared::on_session_end();
    library::on_session_end();
    wsync::on_session_end();
    world::on_session_end(); // the guest's queued loads of the host's world (BUG-0074)
    g_dna_pending = -1;
    g_dna_pending_progress = {};
    if (g_host_away_fix) { // BUG-0085
        g_host_away = false;
        g_host_away_since = -1;
        g_solo_dna = 0;
        g_solo_mode = 0;
    }
    g_dna_owed = 0;
    g_dna_owed_world = 0;
    g_solo_await = 0; // BUG-0829
    g_pace_heard = 1; // BUG-0510
    g_pace_carry.reset();
    g_dna_spend_owed = 0; // BUG-0466
    if (g_own_editor_flags_fix && !(game::mode_id() == modes::kEdit || game::mode_id() == modes::kEditor)) {
        g_own_editor = false; // BUG-0811: (in an editor its result still comes, and charges, as in a game of one)
        g_own_editor_charge_due = false;
    }
    g_dna_out.clear();
    g_dna_notices.clear();
    g_state_mode_override = 0; // tests
    if (g_own_editor_end_fix)
        g_own_editor_due = 0; // BUG-0234 (a task already queued reads it first and returns)
    if (g_give_pick_reset_fix)
        g_give_pick = net::kNoGiveTarget; // BUG-0234: a new session starts with no pick
    if (g_share_cine_each_fix) { // BUG-0232: no wait of an old session shortens a later one
        g_share_cine_since = 0;
        g_share_due_at = 0;
    }
    if (g_session_state_end_fix) { // BUG-0770 (state only: safe inside g_sess.update())
        settings::on_disconnect();
        sreport::on_session_end();
    }
    lifecycle::reset(lifecycle::kSession, g_lifecycle_fix); // BUG-1055: every SESSION_STATE (mod state only)
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

// decision 6H A (alpha): the players' states copied by the galaxy server. Off (`coop.fix net.fan_states off`): the host
// sends each guest every other player's state itself (the square of the players)
static bool g_fix_fan_states = true;
COOP_FIX("net.fan_states", "6H A", g_fix_fan_states,
         "net: on a galaxy planet the host sends each player's state once and the galaxy server copies it to the others");

// 6J: a session player's name ("" none)
static std::string player_name_of(uint8_t id)
{
    for (auto& p : g_sess.players())
        if (p.id == id)
            return p.name;
    return "";
}

// 6J: a whisper: one line to one player. A guest's goes through the host (guests reach only the host); the host passes
// it on with the sender's id. Shown in the chat as "<from> whispers: ..." / "To <name>: ..." (only the two see it).
static std::vector<std::string> g_whispers_in; // (tests: coop.whisper)
static void show_whisper(uint8_t from, const std::string& text)
{
    const std::string line = utf8::shown_name(player_name_of(from)) + " whispers: " + text;
    g_whispers_in.push_back(line);
    if (g_whispers_in.size() > 20)
        g_whispers_in.erase(g_whispers_in.begin());
    chat_log(line);
    feed(line);
}
static void on_whisper(uint8_t from, net::Reader& r)
{
    const uint8_t to = r.u8();
    const std::string text = utf8::clean_text(r.str16(), 240);
    if (!r.ok() || text.empty())
        return;
    if (g_sess.role() == net::Role::Host) {
        if (to == g_sess.my_id()) {
            show_whisper(from, text);
            return;
        }
        net::Writer w; // pass it on: the receiver learns who sent it
        w.u8(from);
        w.str16(text);
        g_sess.send_reliable_to(to, net::MSG_WHISPER, w);
        return;
    }
    if (from == 0) // from the host: u8 = the sender
        show_whisper(to, text);
}
static std::string whisper(uint8_t to, const std::string& raw)
{
    const std::string text = utf8::clean_text(raw, 240);
    if (text.empty())
        return "Nothing to whisper.";
    if (!g_sess.connected())
        return "Not in a session.";
    if (to == g_sess.my_id() || player_name_of(to).empty())
        return "No such player.";
    net::Writer w;
    if (g_sess.role() == net::Role::Host) {
        w.u8(g_sess.my_id());
        w.str16(text);
        g_sess.send_reliable_to(to, net::MSG_WHISPER, w);
    } else {
        w.u8(to);
        w.str16(text);
        g_sess.send_reliable_to(0, net::MSG_WHISPER, w);
    }
    chat_log("To " + utf8::shown_name(player_name_of(to)) + ": " + text);
    return "";
}

// 6F B / 6H A: one unreliable message the galaxy server copies to these guests (false: not hosting through a galaxy)
static bool galaxy_fan_out(const std::vector<uint8_t>& ids, uint8_t type, const net::Writer& w)
{
    if (!galaxy::hosting() || (type != net::MSG_CELL_STATE_C && !g_fix_fan_states))
        return false; // (net.fan_states off: the player states and the Tribe messages one to each, 6I A)
    std::vector<uint16_t> slots;
    for (uint8_t id : ids) {
        const uint16_t slot = galaxy::slot_of(g_sess.peer_endpoint(id));
        if (!slot)
            return false;
        slots.push_back(slot);
    }
    return galaxy::fan_out(slots, type, w);
}

void setup_callbacks()
{
    net::g_spawn_thread = spawn_pinned;
    g_sess.build = build_id();
    g_sess.personal_launch = true;
    // the versions every peer logs on a join (CAP_VERSION): mod, SporeKit, game build and exe hash
    g_sess.version = std::string("coop " COOP_VERSION ", SporeKit ") + H->kit_version() + ", " + H->build_name() + " " +
                     std::string(H->exe_sha256()).substr(0, 8);
    g_sess.cb.log = [](const std::string& s) { LOG("net: %s", s.c_str()); };
    g_sess.cb.fan_out = galaxy_fan_out; // 6H A: the player states copied by the galaxy server
    g_sess.cb.connected = [] {
        parties::on_session_start(); // 6J (the host's book follows in MSG_PARTY)
        support::on_session_start();
        support::on_connected();
        pfind::on_connected(); // F2: the colour this player picked goes to the host
        feed("connected to host");
        coop_ui::session_event(true, ""); // BUG-0104: "Connecting to ..." ends here
        coop_ui::remember_server(g_sess.host_address(), g_sess.name_of(0));
    };
    g_sess.cb.disconnected = [](const std::string& why) {
        host_pause_release(); // BUG-9260
        support::on_disconnected(why);
        rejoin::clear(net::g_fix.join_seen); // the session ended (BUG-0235); kept for this host and name (BUG-1001)
        settings::on_disconnect();
        sreport::on_disconnect();
        // BUG-9251: the host's own words when it refused or removed this player ("The host removed you from the game."),
        // without "disconnected: rejected: " in front; other ends keep their line
        const bool refused = why.rfind("rejected: ", 0) == 0;
        feed(refused ? why.substr(10) : "disconnected: " + why);
        coop_ui::session_event(false, why);
        chat_log("* disconnected: " + why);
        g_ready.clear();
        g_lobby_rtt.clear();
        end_session(EndPath::Disconnect, true);
    };
    g_sess.cb.player_join = [](const net::PlayerInfo& p) {
        parties::on_player_join(p.id); // 6J
        world::campaign_guest_joined(g_sess);
        support::on_player_join(p);
        settings::on_player_join(p.id); // F3: the host's session settings first
        pfind::on_player_join(p.id);    // F2: the host gives the newcomer a colour, everybody gets the table
        shared::on_player_join(g_sess.role());
        g_world_announced = 0; // BUG-0933: the newcomer's WELCOME may have named no world (the host in an editor)
        wsync::on_player_join();
        creations::on_player_join(p.id);
        npc::on_player_leave(p.id); // a newcomer under a reused id knows no NPCs yet (BUG-0125)
        if (g_player_forget_fix && p.id != g_sess.my_id())
            player_life::forget_all(p.id); // BUG-0422: nothing of an earlier player of this id
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
        r.name = net::g_fix.shown_names ? utf8::shown_name(p.name) : p.name; // BUG-9222: name tags without the tag
        r.join_serial = ++g_join_serial; // BUG-0231: the tag of this guest's shares
        r.share_tries = 0;
        r.share_try_key = 0;
        r.retry_at = 0;
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
        feed_kind(utf8::shown_name(p.name) + " joined the pack", "join"); // a name like "Lost" is not danger (BUG-0129)
        chat_log("* " + utf8::shown_name(p.name) + " joined");
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
            if (world::can_resume_world(g_sess.peer_resume_world(p.id))) {
                r.world_sent = ((uint64_t)g_mode << 32) | modes::current_world();
                r.joined_phase = true;
                trace::line("world", "reusing the verified Cell world for player %u", p.id);
                return;
            }
            if (galaxy::accept_warm(g_sess.peer_resume_world(p.id))) { // decision 6B C (alpha): it kept this world
                r.world_sent = ((uint64_t)g_mode << 32) | modes::current_world();
                r.joined_phase = true;
                LOG("galaxy: %s has this world loaded (the hand-over, 6B C, or the galaxy's stored copy, 6I A): no world sent",
                    p.name.c_str());
                return;
            }
            bool save = g_save_on_share && g_adapter; // only while playing, not from menus
            r.world_sent = ((uint64_t)g_mode << 32) | modes::current_world();
            r.joined_phase = false;
            r.share_at_join = true;
            if (world::offer_launch(g_sess,p.id,r.join_serial)) {
                r.needs_world = true;
                return;
            }
            if (!world::share_with(p.id, err, save, r.join_serial)) {
                feed("cannot share the world: " + err);
                share_failed(p.id, r.join_serial, false);
            } else {
                feed("sending the world to " + p.name);
                galaxy::upload_soon(20); // 6I A: the share saved anew: the galaxy's copy follows (the next joiners use it)
            }
        }
    };
    g_sess.cb.custom = [](uint8_t type, uint8_t from, net::Reader& r) {
        if (type == net::MSG_PARTY) { // 6J
            parties::on_message(from, r);
            return;
        }
        if (type == net::MSG_WHISPER) { // 6J
            on_whisper(from, r);
            return;
        }
        if (type == net::MSG_ADVENTURE) {
            adventure::message(from,r);
            return;
        }
        if (type == net::MSG_ADVENTURE_ACTORS) { // BUG-9114
            adventure::actors_message(from, r);
            return;
        }
        if (type == net::MSG_CREATURE_RESCUE) {
            creature_rescue::message(from, r);
            return;
        }
        if (type == net::MSG_IDENTITY) { // BUG-9135
            identity::on_message(from, r);
            return;
        }
        if (type == net::MSG_DRAMA) { // BUG-9131
            drama::on_message(from, r);
            return;
        }
        if (type == net::MSG_PROGRESS_DEBT) { // BUG-9119
            progress_debt::message(from, r);
            return;
        }
        if (type == net::MSG_HOST_RESTART) { // BUG-9239: the host restarts for its memory: rejoin by ourselves
            if (from == 0 && g_sess.role() == net::Role::Client) mem_refresh::host_restarting(r.u16());
            return;
        }
        if (type == net::MSG_DIAG) { // support diagnostics: clock, marks, sync-check (diag_coop.h)
            support::on_message(from, r);
            return;
        }
        if (type == net::MSG_ANIM) {
            net::AnimEvent ev;
            if (ev.read(r))
                on_remote_anim(ev);
            return;
        }
        if (type == net::MSG_SETTINGS) { // F3: the host's session settings (Session class: wherever we play)
            if (!g_settings_in)
                return; // tests (coop.corefix settings_in 0): a host older than 0.7.12 sends no table
            settings::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_COLORS) { // F2: the player colours (Session class)
            pfind::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_REPORT) { // F4: the session report (BUG-0767)
            sreport::on_message(from, r);
            return;
        }
        if (type == net::MSG_HOST_WORLD) { // BUG-0933: the host's save now (Session class)
            uint32_t w = r.u32();
            if (from == 0 && r.ok() && w != 0 && g_sess.role() == net::Role::Client) {
                g_host_save = w;
                g_host_save_heard++;
            }
            return;
        }
        if (type == net::MSG_EFFECT) { // BUG-0451: a partner's creature made an effect
            effects::on_message(type, from, r);
            return;
        }
        // (also before the host's first state when the host named no world in WELCOME: one Civ
        // cycle applied in that window bound the guest's own vehicles for good, BUG-0231; with
        // Welcome::world a guest in the host's world is Same from WELCOME on and loses nothing)
        if (from == 0 && type != net::MSG_DNA && gate_drops(type)) {
            g_place_dropped++;
            return;
        }
        if (type == net::MSG_DNA && from == 0 && g_sess.role() == net::Role::Client) {
            float total = r.f32(); // (row 6, elsewhere, is the gate above: kept as g_dna_pending)
            std::vector<dna_ledger::Part> parts; // the host's causes (an older host sends none)
            if (r.ok() && r.left() >= 1) {
                unsigned n = r.u8();
                if (r.left() < n * 6u) return;
                for (unsigned i = 0; i < n; i++) {
                    uint8_t c = r.u8(), pl = r.u8();
                    float a = r.f32();
                    if (c >= 1 && c <= 4 && std::isfinite(a) && std::fabs(a) < 100000)
                        parts.push_back({(dna_ledger::Cause)c, pl, a});
                }
            }
            auto progress = dna_sync::Progress::read(r); // Lineages: optional, tagged
            // BUG-0510: the host's pace divisor, after the progress tail (an older host sends none: the full sum)
            dna_pace_heard(r.ok() && r.left() >= 1 ? r.u8() : 1);
            if (!r.ok() || !std::isfinite(total) || total < 0) return;
            if (gate_drops(type)) {
                g_dna_pending = total;
                g_dna_pending_progress = progress;
                g_place_dropped++;
                return;
            }
            if (personal_dna() && game::avatar()) { // BUG-9162: the party's bar; the DNA here is this player's own
                if (progress.valid(g_world) && party_bar::on())
                    game::set_bar(progress.earned); // BUG-9277: its party's own bar (lower than the host's too)
                else if (progress.valid(g_world))
                    game::dna_sync_raw(game::dna(), progress.earned);
                g_dna_seen = game::dna();
                g_dna_seen_ok = true;
                g_dna_rx++;
                return;
            }
            if (r.ok() && g_solo_await > 0) { // BUG-0829
                bool paid = false;
                for (auto& p : parts)
                    paid = paid || (p.cause == dna_ledger::Cause::Earned && p.player == g_sess.my_id() &&
                                    p.amount >= g_solo_await * 0.5f);
                if (paid || H->time_seconds() > g_solo_await_until)
                    g_solo_await = 0;
                else if (!g_host_away && dna_route::host_total(false, false, game::avatar() != nullptr) == dna_route::Total::Apply) {
                    g_solo_stale_totals++;
                    LOG("dna: host total %.0f before it added the %.0f DNA earned here while it was away: not applied (BUG-0829)",
                        total, g_solo_await);
                    for (auto& p : parts)
                        dna_note(p, g_sess.my_id());
                    return;
                }
            }
            if (dna_route::host_total(false, g_host_away, game::avatar() != nullptr) == dna_route::Total::Apply) {
                dna_spend_sample(); // BUG-0466: a spend since the last tick is ours, not undone by this total
                if (g_dna_spend_fix)
                    total = std::max(0.f, total + g_dna_spend); // a spend not sent yet stays off
                float before = game::dna(), d = total - before;
                if (d != 0) {
                    LOG("dna: %.0f -> %.0f cause=%s", before, total, dna_ledger::causes_text(parts).c_str());

                    for (auto& p : parts)
                        dna_note(p, g_sess.my_id());
                }
                game::dna_sync_raw(total, progress.valid(g_world) ? progress.earned : NAN);
                dna_spend_resync(); // BUG-0466
                g_dna_rx++;
            }
            return;
        }
        if (type == net::MSG_START_PARTS && from == 0 && g_sess.role() == net::Role::Client) { // 3Z B
            const uint16_t n = r.u16();
            std::vector<StartPart> parts;
            for (uint16_t i = 0; i < n && r.ok() && i < 2000; i++) {
                StartPart p{};
                p.lo = r.u32();
                p.hi = r.u32();
                p.state = r.u8();
                parts.push_back(p);
            }
            if (!r.ok()) return;
            g_start_parts = std::move(parts);
            g_start_parts_at = H->time_seconds();
            LOG("start editor: the host's parts list, %zu part states (3Z B)", g_start_parts.size());
            return;
        }
        if (type == net::MSG_MIGRATE) { // BUG-9163: a player's evolution started the migration
            const uint8_t kind = r.u8(), by = r.u8();
            const uint32_t world = r.u32();
            if (!r.ok() || kind != 1 || !g_migrate_together) return;
            // (2026-11-18 review: on a galaxy planet the host passes a guest's warning on first, whatever it does itself:
            // in its editor or within its own round it returned below and that guest's party never heard)
            const bool pass = g_sess.role() == net::Role::Host && from != 0 && by == from;
            const bool pass_first = pass && g_migrate_party && parties::galaxy();
            if (pass_first) send_migrate(by, -1, by);
            if (g_mode != modes::kCreature || world != g_world) return;
            const double now = H->time_seconds();
            if (now < g_mig_round_until) { // this round is under way already (warned, or started here)
                ++g_mig_ignored;
                LOG("migration: %s's migration is part of the round of %s (BUG-9163)", g_sess.name_of(by).c_str(),
                    g_mig_by.c_str());
                return;
            }
            const bool mine = migrates_with(by); // (BUG-9254: on a galaxy planet, this game's party only)
            if (!mine)
                LOG("migration: %s's migration is not this game's party's: no editor here (BUG-9254)",
                    g_sess.name_of(by).c_str());
            if (pass) { // a guest's: everyone, the host too
                if (!pass_first) send_migrate(by, -1, by); // (each guest checks its own party)
                if (mine) migration_warned(by, now);
            } else if (g_sess.role() == net::Role::Client && from == 0 && by != g_sess.my_id() && mine)
                migration_warned(by, now);
            return;
        }
        if (type == net::MSG_DNA_OWN && from == 0 && g_sess.role() == net::Role::Client) { // BUG-9162: our kill's DNA
            const float d = r.f32();
            if (r.ok() && d > 0 && d < 10000 && personal_dna() && game::avatar()) {
                game::dna_sync_raw(game::dna() + d, game::evolution_bar());
                g_dna_seen += d;
                g_dna_own_got++;
                feed("+" + std::to_string((int)d) + " DNA from your hunt");
            }
            return;
        }
        if (type == net::MSG_DNA_ADD && g_sess.role() == net::Role::Host) {
            float d = r.f32();
            auto add = dna_route::host_add(game::avatar() != nullptr, g_away_dna_fix);
            if (r.ok() && personal_dna() && d < 0)
                return; // BUG-9162: a guest's own spend is its own
            if (r.ok() && d > 0 && d < 10000 && add == dna_route::Add::Pay && personal_dna() &&
                party_bar::credit(g_sess, from, d)) { // BUG-9277: another party's gain fills its own bar
                feed(g_sess.name_of(from) + " earned " + std::to_string((int)d) + " DNA");
            } else if (r.ok() && d > 0 && d < 10000 && add == dna_route::Add::Pay) {
                float left = progress_debt::absorb(d); // BUG-9119: a progress debt is repaid first
                if (left > 0) {
                    dna_host_gain({dna_ledger::Cause::Earned, from, left}, game::dna());
                    if (personal_dna())
                        game::dna_sync_raw(game::dna(), game::evolution_bar() + left); // BUG-9162: the bar only
                    else
                        game::dna_add_raw(left);
                }
                feed(g_sess.name_of(from) + " earned " + std::to_string((int)d) + " DNA" +
                     (left < d ? " (repays the progress debt)" : ""));
            } else if (g_dna_spend_fix && r.ok() && d < 0 && d > -100000) { // BUG-0466: a guest's own spend
                g_dna_spends_got++;
                if (add == dna_route::Add::Pay) {
                    float dd = std::max(d, -game::dna()); // the total never goes below 0
                    dna_host_gain({dna_ledger::Cause::Earned, from, dd}, game::dna());
                    game::dna_add_raw(dd);
                } else
                    g_dna_spend_owed += d; // in an editor or a load: taken off when our creature is back
            } else if (r.ok() && d > 0 && d < 10000 && add == dna_route::Add::Owe && g_dna_owed < 100000) {
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
            if (r.ok() && game::avatar() && !wsync::owns_time_of_day() && // BUG-0579: else the wsync row
                !adventure_sky_own(game::mode_id()))                       // BUG-0789
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
            uint8_t flags = r.left() >= 1 ? r.u8() : 0; // BUG-0516 (older hosts send none)
            if (flags & kVoteOffer) { // the host evolves now: this guest may follow with its own editor
                if (!editor || (g_vote_world_fix && guest_elsewhere()) || g_mode != modes::kCreature || !g_local ||
                    g_own_editor_due || host_creature_mode()) { // F5: the host evolves for everybody
                    trace::line("vote", "offer '%s' not taken: %s", text.c_str(),
                                g_mode != modes::kCreature || !g_local ? "not playing the Creature stage" : "another world or an editor due");
                    return;
                }
                g_offers_got++;
                g_offer_editor = editor;
                g_offer_until = H->time_seconds() + (left > 0 ? left : kOfferSeconds);
                trace::line("vote", "offer by the host: '%s' editor=%s", text.c_str(), trace::hex(editor).c_str());
                feed(text);
                if (!coop_ui::is_open()) {
                    g_offer_opened_ui = true;
                    coop_ui::open(true);
                }
                g_lobby_dirty = true;
                return;
            }
            // BUG-0420: a guest that plays another world of the host's stage takes no part in the host's vote
            // (no vote screen, no evolution editor of the host's world); a vote it saw start here still ends
            if (g_vote_world_fix && !g_vote.active && guest_elsewhere()) {
                g_vote_elsewhere++;
                if (on)
                    trace::line("vote", "by the host: '%s' not taken, this guest plays another world", text.c_str());
                return;
            }
            bool started = on && !g_vote.active, ended = !on && g_vote.active;
            if (on || (g_vote_drop_editor_fix && ended && has_editor))
                g_vote.editor = editor; // a cancelled vote ends with 0 (BUG-0234)
            // the evolution starts: everybody evolves its own creature in its own editor (own_editor_tick)
            bool landfall = g_vote.editor == 0xFD4902BD && creations::landfall_pending();
            if (ended && has_editor && editor == 0x96B24187 && civ::founder_space_ready()) {
                world::begin_local_space_intro();
                feed("Open Civilization History and click its checkmark to design your ship and start your flight tutorial.");
            }
            // F5 (BUG-0769): creature.mode host: no own editor, the host's evolution comes as the new species
            if (ended && g_vote.editor &&
                ((g_mode == modes::kCreature && g_local && !(g_vote_world_fix && guest_elsewhere()) && !host_creature_mode()) ||
                 landfall || own_tribal_outfitter(g_vote.editor))) {
                g_own_editor_due = g_vote.editor;
                g_own_editor_due_at = g_own_editor_due_first = H->time_seconds();
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
            if (net::g_fix.shown_names) t = utf8::shown_name(t); // (from an older host: BUG-9239)
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
        if ((type >= net::MSG_CIV_STATE && type <= net::MSG_CIV_BUILD) || type == net::MSG_CIV_ALARM || type == net::MSG_CIV_TURRETS || type == net::MSG_CIV_ECONOMY || type == net::MSG_CIV_INFLUENCE) {
            civ::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_CAMPAIGN_RULES) {
            world::receive_campaign_rules(g_sess,from,r);
            return;
        }
        if (type == net::MSG_SPACE_LAUNCH) {
            world::on_launch_message(g_sess,from,r);
            return;
        }
        if (type == net::MSG_LIBRARY) {
            library::on_message(from, r);
            return;
        }
        if (type == net::MSG_CREATION) {
            creations::on_message(type, from, r);
            return;
        }
        if ((type >= net::MSG_CELL_STATE && type <= net::MSG_CELL_FOOD) || type == net::MSG_CELL_KO ||
            type == net::MSG_CELL_COMBAT || type == net::MSG_CELL_BITTEN || type == net::MSG_CELL_STATE_C) {
            cellsync::on_message(type, from, r);
            return;
        }
        if (type == net::MSG_TRIBE_STATE || type == net::MSG_TRIBE_CMD || type == net::MSG_TRIBE_BUILDINGS ||
            type == net::MSG_TRIBE_EQUIPMENT || type == net::MSG_TRIBE_TOOLS || type == net::MSG_TRIBE_RELATIONSHIPS || type == net::MSG_TRIBE_FOOD || type == net::MSG_TRIBE_PERFORMANCE || type == net::MSG_TRIBE_OUTFIT || type == net::MSG_TRIBE_COMBAT ||
            type == net::MSG_TRIBE_LATE || type == net::MSG_TRIBE_VILLAGES) {
            tribe::on_message(type, from, r);
            return;
        }
        switch (type) {
        case net::MSG_NPC_SPAWN: case net::MSG_NPC_DESPAWN: case net::MSG_NPC_STATE: case net::MSG_NPC_STATE_C:
        case net::MSG_DAMAGE_REQ: case net::MSG_DAMAGE: case net::MSG_NPC_RESYNC: case net::MSG_NPC_ANIM:
        case net::MSG_NPC_MEAT:
            npc::on_message(type, from, r);
            return;
        case net::MSG_SPECIES: case net::MSG_SOCIAL:
            social::on_message(type, from, r);
            return;
        case net::MSG_NEST: // BUG-0384
            nest::on_message(type, from, r);
            return;
        case net::MSG_FIELDS: // BUG-0419 G2: replicated fields of mirrored objects
            fields::on_message(type, from, r);
            return;
        case net::MSG_WSTATE: // W1: host-owned world state
            wsync::on_message(type, from, r);
            return;
        case net::MSG_PARTS: // BUG-0401
            parts::on_message(type, from, r);
            return;
        case net::MSG_WOBJ: // BUG-0472, BUG-0459
            wobj::on_message(type, from, r);
            return;
        case net::MSG_PLAYER_RULE: // P1 of 0.7.7: stun (BUG-0554)
            player_rules::on_message(type, from, r);
            return;
        case net::MSG_MIRROR_FX: // BUG-0600: the host's creature effects, sprints and voices on the mirrors
            mirror_fx::on_message(type, from, r);
            return;
        }
        if (g_sess.role() == net::Role::Client && from == 0)
            world::on_message(type, r, g_sess.name_of(0));
    };
    { // BUG-9257: each message's time (the frame profiler)
        auto inner = g_sess.cb.custom;
        g_sess.cb.custom = [inner](uint8_t type, uint8_t from, net::Reader& r) {
            const double t0 = fprof::qpc();
            inner(type, from, r);
            fprof::message(type, fprof::qpc() - t0);
        };
    }
    g_sess.cb.old_peer = [](uint8_t, const std::string& name) { // BUG-0219: once per player and join
        // the log line (net: ... older SporeCoop) says what is lost: long chat cut, no species table
        feed_kind(name + " has an older SporeCoop: update both games to the latest", "info");
    };
    g_sess.cb.player_leave = [](uint8_t id, uint8_t reason) {
        parties::on_player_leave(id); // 6J
        identity::on_player_left(id); // BUG-9135
        creature_rescue::on_player_leave(id); // old debt is detached from this reusable seat
        guest_pack::on_player_leave(id);      // BUG-9129
        trace::line("session", "player %u '%s' left: %s", id, g_remotes.count(id) ? g_remotes[id].name.c_str() : "?",
                    reason == 1 ? "timed out (no packets)" : "left or replaced by its rejoin");
        support::on_player_leave(id, reason); // its link statistics (diag_stats.cpp)
        sreport::on_player_leave(id);         // BUG-0767: its report will not come
        if (g_role_fix)
            g_roles.erase(id); // ids are reused: the next newcomer starts with no role (BUG-0064)
        shared::on_player_leave(id);
        creations::on_player_leave(id); // BUG-0076: the next player of this id is not shown as this one
        library::on_player_leave(id);
        npc::on_player_leave(id);       // BUG-0125
        views::forget(id);              // BUG-0280
        pfind::on_player_leave(id);     // F2: its colour is free again
        if (g_player_forget_fix)
            player_life::forget_all(id); // BUG-0422: every module's state of this id (player_life.h)
        g_give_pick = net::give_pick_after_leave(g_give_pick, id, g_give_pick_reset_fix); // BUG-0234
        if (g_ready.erase(id) | g_lobby_rtt.erase(id)) // BUG-0772 guard: also for a leaver without a Remote
            g_lobby_dirty = true;
        auto it = g_remotes.find(id);
        if (it == g_remotes.end())
            return;
        feed_kind(utf8::shown_name(it->second.name) + " left", "leave");
        chat_log("* " + utf8::shown_name(it->second.name) + " left");
        g_ready.erase(id);
        g_lobby_dirty = true;
        cellsync::on_player_leave(id);
        despawn(it->second);
        g_remotes.erase(it);
    };
    g_sess.cb.player_state = [](const net::PlayerState& s) {
        if (game::pose_bad(s.pos, s.rot, s.vel)) // BUG-0614: never into the buffer (its velocity is written raw)
            return;
        auto& r = g_remotes[s.id];
        r.id = s.id;
        if (r.name.empty())
            r.name = g_sess.name_of(s.id);
        r.buf.push(s, H->time_seconds());
    };
    g_sess.cb.chat = [](uint8_t id, const std::string& t) {
        feed_kind("<" + g_sess.name_of(id) + "> " + t, "chat"); // never guessed from the words (BUG-0129)
        chat_log(g_sess.name_of(id) + ": " + t, pfind::color_of(id)); // F2: in the writer's colour
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
    g_chat_col.clear();
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

bool do_host(uint16_t port, unsigned seats, unsigned scaling, std::string& err, const std::string& password = {},
             unsigned seat_floor = 0)
{
    if (!creations::campaign_content(COOP_VERSION, g_sess.campaign_profile, err))
        return false;
    end_open_session();
    reset_lobby();
    if (scaling > founders::kMaxDifficultyPercent) {
        err = "Difficulty Scaling must be between 0 and 400%.";
        return false;
    }
    founders::CampaignRules requested{uint8_t(seats),uint16_t(scaling)};
    requested.model = 1; // BUG-9100: new campaigns scale every extra with the difficulty
    int health = H->config_int(M,"campaign","enemy_health_per_extra_founder",requested.extra_health_percent);
    int damage = H->config_int(M,"campaign","enemy_damage_per_extra_founder",requested.extra_damage_percent);
    if (health < 0 || health > UINT16_MAX || damage < 0 || damage > UINT16_MAX) {
        err = "Enemy health and damage bonuses must be nonnegative whole percentages up to 65535.";
        return false;
    }
    int hunger = H->config_int(M,"campaign","hunger_basis_points_per_extra_founder",requested.extra_hunger_basis_points);
    if (hunger < 0 || hunger > UINT16_MAX) {
        err = "Hunger scaling must be between 0 and 65535 basis points per extra founder.";
        return false;
    }
    requested.extra_hunger_basis_points = uint16_t(hunger);
    int takeover = H->config_int(M,"campaign","takeover_per_extra_founder",requested.extra_takeover_percent);
    if (takeover < 0 || takeover > UINT16_MAX) {
        err = "Takeover resistance must be between 0 and 65535 percent per extra founder.";
        return false;
    }
    requested.extra_takeover_percent = uint16_t(takeover);
    requested.extra_health_percent = uint16_t(health);
    requested.extra_damage_percent = uint16_t(damage);
    if (!world::setup_campaign(g_sess,requested,err,seat_floor)) return false;
    if (!g_sess.host(port, g_name, err, password))
        return false;
    support::on_session_start();
    pfind::on_session_start(); // F2: the host takes its colour
    parties::on_session_start(); // 6J: the host's party
    feed("hosting on port " + std::to_string(g_sess.port()));
    chat_log("* hosting on UDP port " + std::to_string(g_sess.port()));
    g_lobby_dirty = true;
    return true;
}

bool do_join(const std::string& addr, std::string& err, const std::string& password = {}, uint64_t token = 0)
{
    if (!creations::campaign_content(COOP_VERSION, g_sess.campaign_profile, err))
        return false;
    end_open_session();
    reset_lobby();
    g_sess.resume_world = world::resume_world();
    if (uint64_t warm = galaxy::warm_join_token()) // decision 6B C (alpha): the galaxy's warm hand-over
        g_sess.resume_world = warm;
    g_sess.rejoin_token = rejoin::token_for_join(H->instance_id(), addr, g_name); // BUG-0235, BUG-1001
    if (token) g_sess.rejoin_token = token; // BUG-9239: the same player after a memory restart
    if (!g_sess.join(addr, g_name, err, password))
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
    if (galaxy::hosting()) { // decision 6A A (alpha): a galaxy planet's host saves it first, then leaves (galaxy_client)
        H->log(M, SK_INFO, "%s", galaxy::leave().c_str());
        return;
    }
    g_sess.leave("user");
    // a clean leave: the next join is a new player (BUG-0235), except a join back to this host under this name, which
    // keeps the token: the host takes it back while it takes no new players (BUG-1001, off with net.join_seen)
    rejoin::clear(net::g_fix.join_seen);
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
    if (g_offer_editor && !g_vote.active && g_sess.role() == net::Role::Client) { // BUG-0516: "Evolve now"
        offer_take();
        return;
    }
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
int c_seats(void*, int argc, const char* const* argv, SkOut* out)
{
    const bool now = argc == 3 && !strcmp(argv[2], "now");
    char* end = nullptr;
    const unsigned long seats = (argc == 2 || now) ? strtoul(argv[1], &end, 10) : 0;
    if ((argc != 2 && !now) || !argv[1][0] || *end || seats < 1 || seats > founders::kSeatCeiling) { // (6E B: any count)
        SAY("Usage: coop.seats <1-%u> [now]. Without now: from the next stage (the current size cancels a queued change); "
            "now: more seats in this stage at once.", founders::kSeatCeiling);
        return 1;
    }
    std::string result;
    const bool ok = now ? world::raise_seats_now(g_sess,unsigned(seats),result) : world::change_seats(g_sess,unsigned(seats),result);
    SAY("%s",result.c_str());
    return ok ? 0 : 1;
}

int c_host(void*, int argc, const char* const* argv, SkOut* out)
{
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : default_port();
    if (argc > 2)
        g_name = utf8::player_name(argv[2]);
    std::string err;
    unsigned seats = 2, scaling = 100;
    if (argc > 3) {
        char* end = nullptr;
        unsigned long n = strtoul(argv[3],&end,10);
        if (!*argv[3] || *end || n < 1 || n > founders::kSeatCeiling) { SAY("Reserve 1 to %u founders.", founders::kSeatCeiling); return 1; }
        seats = unsigned(n);
    }
    if (argc > 4) {
        char* end = nullptr;
        unsigned long n = strtoul(argv[4],&end,10);
        if (!*argv[4] || *end || n > founders::kMaxDifficultyPercent) { SAY("Use a whole scaling percentage from 0 to 400."); return 1; }
        scaling = unsigned(n);
    }
    if (!do_host(port,seats,scaling,err)) {
        SAY("host failed: %s", err.c_str());
        // BUG-0302: in the game log too (a lab start whose host never hosted left no trace there)
        H->log(M, SK_WARN, "net: host failed on UDP port %u: %s", port, err.c_str());
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

int c_founders(void*, int, const char* const*, SkOut* out)
{
    founders::Roster roster;
    const auto planet = world::current_game_key();
    if (founders::read(planet, roster) || founders::read(planet, roster, founders::Stage::Civilization)) {
        SAY("%s", founders::describe(roster).c_str());
        return 0;
    }
    SAY("No readable founding roster for this planet.");
    return 1;
}

// The campaign rules in effect here and every value derived from them (BUG-9100, tests).
int c_rules(void*, int, const char* const*, SkOut* out)
{
    const auto r = world::rules_for_current();
    SAY("seats=%u difficulty=%u%% model=%u progress=%u%% enemy_hp=%u%% enemy_dmg=%u%% hunger=%.4f "
        "takeover=%u%% raid_extra=%u civ_interval_ms=%u rival_target(5)=%u\n",
        r.seats, r.difficulty_percent, r.model, r.workload_percent(), r.enemy_health_percent(),
        r.enemy_damage_percent(), r.hunger_factor(), r.takeover_percent(), r.extra_total(1),
        r.civ_military_interval_ms(), r.rival_target(5));
    return 0;
}

int c_status(void*, int, const char* const*, SkOut* out)
{
    SAY("%s%sbusy: %s\n", g_sess.status().c_str(), world::status().c_str(), busy::names(game_busy()).c_str());
    if (g_sess.role() != net::Role::None)
        SAY("campaign profile: %s\n", g_sess.campaign_profile.c_str());
    for (auto& pl : g_sess.players()) // CAP_VERSION: each player's mod, SporeKit and game build
        SAY("version %u '%s': %s\n", pl.id, pl.name.c_str(), pl.version.empty() ? "unknown (pre-0.7.4)" : pl.version.c_str());
    SAY("remote animations: received %u, played %u\n", g_anims_rx, g_anims_played);
    SAY("appeared notices: told %u, puppet rebuilds not told %u (BUG-0923)\n", g_appeared_told, g_appeared_quiet);
    SAY("%s", effects::status().c_str()); // BUG-0451
    SAY("%s", npc::status().c_str());
    SAY("%s", social::status().c_str());
    SAY("%s", tribe::status().c_str());
    SAY("%s", cellsync::status().c_str());
    SAY("%s", notify::status().c_str());
    SAY("dna notices: shown=%u gathering=%u\n", g_dna_noticed, (unsigned)g_dna_notices.open());
    SAY("status block (%d %d %d %d)\n", g_status_block[0], g_status_block[1], g_status_block[2], g_status_block[3]);
    SAY("%s", creations::status().c_str());
    SAY("%s", civ::status().c_str());
    SAY("%s", squadron::status().c_str());
    SAY("%s", spacecoop::status().c_str());
    SAY("%s", gamefixes::status().c_str());
    SAY("dna=%.1f (rx %u totals, forwarded %u gains) solo=%.1f owed=%.1f owed_paid=%u away=%d\n", game::dna(), g_dna_rx,
        g_dna_fwd, g_solo_dna, g_dna_owed, g_dna_owed_paid, (int)g_host_away);
    SAY("dna spend: unsent=%.1f sent=%u got=%u owed=%.1f seen=%.1f/%d\n", g_dna_spend, g_dna_spends_sent, g_dna_spends_got,
        g_dna_spend_owed, g_dna_seen, (int)g_dna_seen_ok); // BUG-0466
    SAY("dna pace: div=%d fix=%d setting=%d heard=%d paced=%u in=%.1f out=%.1f\n", pace_div(), (int)g_dna_pace_fix, settings::get(settings::kDnaPace),
        g_pace_heard, g_paced, g_paced_in, g_paced_out); // BUG-0510
    for (size_t i = g_chat.size() > 6 ? g_chat.size() - 6 : 0; i < g_chat.size(); i++)
        SAY("chat: %s\n", g_chat[i].c_str());
    void* av = g_local;
    SAY("mode=0x%08X local=%p overlay-self=(%.1f %.1f ok=%.0f)\n", game::mode_id(), av, g_dbg_sc[0], g_dbg_sc[1], g_dbg_sc[2]);
    SAY("host_away=%d (times %u) solo_dna=%.1f solo_stale_totals=%u\n", (int)g_host_away, g_host_away_count, g_solo_dna,
        g_solo_stale_totals);
    SAY("host elsewhere=%d, its world's messages dropped %u, dna pending %.1f, own-world dna gains %u, welcome world %08X, share retries %u\n",
        (int)(g_world_gate_fix ? guest_elsewhere() : host_elsewhere()), g_place_dropped, g_dna_pending, g_dna_own,
        g_sess.welcome_world(), g_share_retries); // BUG-0130, BUG-0231
    SAY("host save: known %08X (welcome %08X, followed %08X, heard %u), mine %08X, space_other=%d, announced %08X\n", host_save_known(),
        g_sess.welcome_world(), g_host_save, g_host_save_heard, g_mode == modes::kSpace ? modes::current_world() : 0,
        (int)guest_space_other(), g_world_announced); // BUG-0933
    SAY("own world: mode %08X world %08X elsewhere=%d space_other=%d host_left=%d\n", g_mode, g_world,
        (int)own_world::elsewhere(), (int)own_world::space_other(), (int)own_world::host_left()); // O1 (BUG-0427)
    SAY("space save: %s, load shares %u\n", g_space_key.empty() ? "-" : g_space_key.c_str(), g_space_load_shares); // BUG-0941
    SAY("evolve in place: kept %u, sent after all %u, saves %u\n", g_evo_inplace, g_evo_fallback, g_evo_saves); // BUG-0508
    SAY("away dna: solo_mode=%08X solo_dropped=%u owed_world=%08X owed_dropped=%u\n", g_solo_mode, g_solo_dropped,
        g_dna_owed_world, g_owed_dropped); // BUG-0233
    SAY("strays: pending=%u removed=%u\n", (unsigned)g_strays.size(), g_strays_removed);
    SAY("labels: menu_up=%d window=%d hidden_frames=%u cinematic_frames=%u name_tags=%u\n", (int)g_menu_up.load(),
        (int)coop_ui::is_open(), g_menu_hidden, g_cine_hidden, g_tags_drawn);
    for (auto& p : g_sess.players()) // tests (BUG-0809): the datagrams sent to each peer so far
        if (net::Connection* c = p.id != g_sess.my_id() ? g_sess.conn_to(p.id) : nullptr)
            SAY("net to %u: out_bytes=%llu out_packets=%u\n", (unsigned)p.id, (unsigned long long)c->stats.bytes_sent,
                c->stats.packets_sent);
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
                SAY("   drive err=%.2f speed=%.2f nudges=%u kept=%u teleports=%u walk_goals=%u jumps=%u airs=%u stop_holds=%u\n", r.drv.last_err,
                    r.drv.last_speed, r.drv.nudges, r.drv.nudges_kept, r.drv.teleports, r.drv.goals, r.drv.jumps, r.drv.airs, r.drv.stop_holds);
        }
        // tests (BUG-0507): the puppet the mod holds is a live creature in the game's own object list
        if (r.puppet && creature_puppet(r.puppet_adapter))
            SAY("puppet_listed %u %d gone_by_game=%u\n", id, (int)game::creature_listed(r.puppet), modes::g_puppets_gone_alive);
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
    if (argc > 1 && !strcmp(argv[1], "reload")) { // tests (BUG-0897): [coop] name of coop.ini, as at the start
        char nm[256];
        H->config_str(M, "coop", "name", "Player", nm, sizeof nm);
        g_name = utf8::player_name(nm);
        if (g_name.empty())
            g_name = "Player";
    } else if (argc > 1)
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
    if (bans::chat_command(t)) { // BUG-9251: "/kick", "/ban", "/unban", "/bans" as typed in the chat (the answer is in the feed)
        SAY("chat command handled");
        return 0;
    }
    if (support::chat_command(t)) { // "/bug <text>": a mark in every player's log, not a chat line
        SAY("marked for the report");
        return 0;
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
        SAY(" age=%d hatchling_waiting=%d scale=%.3f radius=%.2f", *(int*)((uint8_t*)av + 0xB34),
            (int)game::hatchling_waiting(av), game::scale(av), game::bounding_radius(av));
    if (g_adapter->mode == modes::kScenario) {
        int act, state, completed;
        if (modes::adventure_progress(act, state, completed))
            SAY("\nact_index=%d play_state=%d goals_completed=%d", act, state, completed);
        SAY("\nscale=%.3f radius=%.2f", game::scale(av), game::bounding_radius(av));
        for (const auto& [id, r] : g_remotes) {
            SAY("\npartner %u: %s", (unsigned)id, puppet_why(r, H->time_seconds()));
            if (r.puppet && game::object_alive(r.puppet)) // tests: the partner's body against the own captain
                SAY(" scale=%.3f radius=%.2f hp=%.1f/%.1f side=%d (captain %d)", game::scale(r.puppet),
                    game::bounding_radius(r.puppet), game::health(r.puppet), game::max_health(r.puppet),
                    *(int*)((uint8_t*)r.puppet + 0x30), *(int*)((uint8_t*)av + 0x30)); // BUG-9200: the political ids
        }
    }
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
    if (argc >= 3 && (!strcmp(argv[1], "block") || !strcmp(argv[1], "away"))) { // tests: BUG-0691 / BUG-0692
        SAY("%s", shared::test_knob(argv[1], atof(argv[2]), argc >= 5 ? argv[3] : "", argc >= 5 ? atof(argv[4]) : 0).c_str());
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

int c_transfer(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "paced")) g_sess.paced_world_transfer = true;
        else if (!strcmp(argv[1], "normal")) g_sess.paced_world_transfer = false;
        else return 1;
    }
    SAY("world transfer: %s", g_sess.paced_world_transfer ? "paced" : "normal");
    return 0;
}

int c_share(void*, int argc, const char* const* argv, SkOut* out)
{
    if (g_sess.role() != net::Role::Host) {
        SAY("only the host shares its world");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "due")) { // tests (BUG-0404): a share due as after an evolution (the gate path)
        int k = 0;
        for (auto& [id, r] : g_remotes)
            if (!r.echo) {
                r.needs_world = true;
                r.cine_wait_since = 0;
                k++;
            }
        g_share_due_at = H->time_seconds();
        SAY("share due for %d player(s), the game is busy: %s", k, busy::names(game_busy()).c_str());
        return 0;
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

// BUG-0400: `coop.npc gopos` (tests) puts a point under the terrain onto it
static bool g_gopos_ground_fix = true;
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
static float gopos_onto_surface(net::Vec3& p)
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
static std::string join_apart_status()
{
    char b[128];
    snprintf(b, sizeof b, "join_apart fix=%d moves=%u last=%.1f s ago from=%.2f m", (int)g_join_apart_fix, g_join_aparts,
             g_join_apart_at < 0 ? -1.0 : H->time_seconds() - g_join_apart_at, g_join_apart_from);
    return b;
}

int c_npc(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "animquiet")) { // guest (tests, BUG-0464)
        npc::set_anim_quiet((float)atof(argv[2]));
        SAY("ok\n");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "joinapart")) { // tests (BUG-0989)
        SAY("%s\n", join_apart_status().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "anims")) { // BUG-0464: the behaviour animation relay's counters
        SAY("%s", npc::anims_status().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "cur")) { // BUG-0464 (tests): each NPC's current animation
        SAY("%s", npc::current_anims().c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "untune")) { // guest (tests, BUG-0462): proxy <id> as made in a herd of ours
        SAY("%s", npc::debug_untune((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "gamekill")) { // guest (tests, BUG-0850): the game destroys proxy <id>, then it goes
        SAY("%s", npc::debug_gamekill((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    // tests (BUG-0426 class, L1): reusehunt <hex address | p<proxy id> | puppet> [frames] [flags]: hunt for a new creature
    // at the address (npc.cpp ReuseHunt); reusehunt alone: its status
    // guest (tests, BUG-0805): handlerkill <id> damage|prompt [anim hex]: the game destroys proxy <id>'s creature
    // (DestroyInstance, no bookkeeping: the proxy still names it, as before the next npc tick), then in the same call a
    // host message about that proxy is handled: damage = the host says the guest was hit by it (MSG_DAMAGE),
    // prompt = its social prompt (MSG_SOCIAL K_PROMPT)
    if (argc > 3 && !strcmp(argv[1], "handlerkill")) {
        uint16_t id = (uint16_t)atoi(argv[2]);
        void* c = npc::debug_proxy_creature(id);
        if (!c || !game::creature_listed(c) || !g_sess.connected() || g_sess.role() != net::Role::Client) {
            SAY("no live proxy %u (a guest's)\n", id);
            return 1;
        }
        uint32_t d0 = npc::handler_dropped();
        game::destroy_untracked(c);
        net::Writer w;
        if (!strcmp(argv[3], "damage")) {
            net::Damage d;
            d.player = g_sess.my_id();
            d.damage = 0.1f;
            d.attacker = id;
            d.write(w);
            net::Reader r(w.data.data(), w.data.size());
            npc::on_message(net::MSG_DAMAGE, 0, r);
        } else {
            w.u8(5); // K_PROMPT
            w.u16(id);
            w.u8(0);
            w.u8(1);
            w.u32(0);
            w.u32(0);
            w.u32(argc > 4 ? (uint32_t)strtoul(argv[4], nullptr, 16) : 0x0F3A8D11u);
            net::Reader r(w.data.data(), w.data.size());
            social::on_message(net::MSG_SOCIAL, 0, r);
        }
        SAY("handlerkill: proxy %u's creature %p destroyed by the game, %s handled; calls dropped %u (BUG-0805)\n", id, c,
            argv[3], npc::handler_dropped() - d0);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "settle")) { // guest (tests, BUG-0842): settle <s>
        SAY("%s", npc::debug_settle(atof(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "chunk")) { // tests (BUG-1099): chunk p<proxy id> | <hex address>
        void* t = argv[2][0] == 'p' ? npc::debug_proxy_creature((uint16_t)atoi(argv[2] + 1))
                                    : (void*)strtoul(argv[2], nullptr, 16);
        SAY("%s", npc::debug_chunk(t).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "reusehunt")) {
        if (argc < 3) {
            SAY("%s", npc::debug_reuse_status().c_str());
            return 0;
        }
        void* t = nullptr;
        if (argv[2][0] == 'p' && argv[2][1] != 'u')
            t = npc::debug_proxy_creature((uint16_t)atoi(argv[2] + 1));
        else if (!strcmp(argv[2], "puppet")) {
            for (auto& [id, r] : g_remotes)
                if (r.puppet && creature_puppet(r.puppet_adapter))
                    t = r.puppet;
        } else
            t = (void*)strtoul(argv[2], nullptr, 16);
        if (!t) {
            SAY("no such object");
            return 1;
        }
        const char* fl = argc > 4 ? argv[4] : "";
        if (strchr(fl, 'H')) { // H: t is a herd; the tries are herds of the avatar's species (BUG-0843)
            bool hl = game::listed(t, game::kHerdNoun);
            net::Vec3 hat = game::avatar() ? game::position(game::avatar()) : net::Vec3{};
            if (fl[0] == 'x' && hl)
                game::destroy_untracked(t);
            SAY("%s%s", fl[0] == 'x' && hl ? "herd destroyed by the game; " : hl ? "" : "(not a listed herd) ",
                npc::debug_reuse_hunt(t, argc > 3 ? atoi(argv[3]) : 120, nullptr, 0, 1, nullptr, &hat, true).c_str());
            return 0;
        }
        bool listed = game::creature_listed(t);
        void* sp = listed && !strchr(fl, 'a') ? game::species_profile(t) : nullptr; // a: of the avatar's species
        void* herd = listed && strchr(fl, 'h') ? game::herd_of(t) : nullptr; // h: in the herd the target was in
        net::Vec3 at = listed ? game::position(t) : net::Vec3{};
        // x: the game destroys it first (DestroyInstance, no bookkeeping of ours); n: the first tries run now, in this
        // call (no frame of ours between); b<N>: N creatures per tick (the first tick after the destroy is in the frame
        // the game frees it in)
        bool x = fl[0] == 'x';
        if (x && listed)
            game::destroy_untracked(t);
        int now_tries = strchr(fl, 'n') ? (argc > 5 ? atoi(argv[5]) : 40) : 0;
        const char* bb = strchr(fl, 'b');
        int burst = bb ? std::max(1, atoi(bb + 1)) : 1;
        // k: the misses stay alive until the hunt ends (their blocks do not go back first); e<N>: a try every N frames
        // f<N>: once the target's heap chunk is free, at most N more tries (BUG-0825); w<N>: no try before it is free, the round ends
        // when it is not free after N frames (default 60, BUG-1099)
        const char* ee = strchr(fl, 'e');
        const char* ff = strchr(fl, 'f');
        SAY("%s%s", x ? "destroyed by the game; " : "",
            npc::debug_reuse_hunt(t, argc > 3 ? atoi(argv[3]) : 120, sp, now_tries, burst, herd, &at, false,
                                  strchr(fl, 'r') != nullptr, // r: Create with its reuse flag
                                  strchr(fl, 'k') != nullptr, ee ? atoi(ee + 1) : 1).c_str());
        if (const char* ww = strchr(fl, 'w'))
            npc::debug_reuse_free_first(atoi(ww + 1));
        if (ff)
            npc::debug_reuse_wait_free(atoi(ff + 1) > 0 ? atoi(ff + 1) : 20);
        SAY("%s", npc::debug_reuse_status().c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "forget")) { // guest (tests, BUG-0487): forget <id>
        SAY("%s", npc::debug_forget((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "resync")) {
        SAY("%s", npc::resync_status().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "list")) {
        SAY("%s", npc::status().c_str());
        // out_printf keeps 8 KB per call: a guest's list with 40 proxies is longer (BUG-0305)
        std::string l = npc::list();
        for (size_t at = 0; at < l.size();) {
            size_t end = at + 6000 < l.size() ? l.rfind('\n', at + 6000) : l.size() - 1;
            if (end == std::string::npos || end < at)
                end = std::min(l.size(), at + 6000) - 1;
            SAY("%s", l.substr(at, end + 1 - at).c_str());
            at = end + 1;
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "marker")) { // tests (BUG-0280): pin the Tribe / Civ pointer (x y z | off)
        bool pin = argc > 4;
        net::Vec3 p{};
        if (pin)
            p = {(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        modes::test_pin_marker(pin, p);
        SAY("pointer %s (%.1f %.1f %.1f)", pin ? "pinned" : "follows the mouse", p.x, p.y, p.z);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "herdnulls")) { // tests (BUG-0256)
        SAY("%s", game::herd_nulls_status().c_str());
        return 0;
    }
    if (argc > 5 && !strcmp(argv[1], "dash")) { // host (tests, BUG-0305): dash <id> <m> <m/s> <s> [w] [mate id] [m beside]
        SAY("%s", npc::debug_dash((uint16_t)atoi(argv[2]), (float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]),
                                  argc > 6 ? (float)atof(argv[6]) : 0.f, argc > 7 ? (uint16_t)atoi(argv[7]) : 0,
                                  argc > 8 ? (float)atof(argv[8]) : 0.f).c_str());
        return 0;
    }
    if (argc > 7 && !strcmp(argv[1], "dashto")) { // host (tests, BUG-0440): dashto <id> <x> <y> <z> <m/s> <s> [vel factor]
        net::Vec3 to{(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])};
        SAY("%s", npc::debug_dash((uint16_t)atoi(argv[2]), 0.f, (float)atof(argv[6]), (float)atof(argv[7]), 0.f, 0, 0.f, &to,
                                  argc > 8 ? (float)atof(argv[8]) : 1.f).c_str());
        return 0;
    }
    if (argc > 5 && !strcmp(argv[1], "objnear")) { // tests (BUG-0440): objnear <x> <y> <z> <r>: the objects (nouns) within r m
        net::Vec3 p{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        SAY("%s", game::objects_near(p, (float)atof(argv[5]), nullptr, 40).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "bhv")) { // host (tests, BUG-0440): bhv <id> <anim hex>
        SAY("%s", npc::debug_bhv((uint16_t)atoi(argv[2]), (uint32_t)strtoul(argv[3], nullptr, 16)).c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "pushback")) { // guest (tests, BUG-0440): pushback <id> <m/s> <s>
        SAY("%s", npc::debug_pushback((uint16_t)atoi(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "raised")) { // tests (BUG-0440): raised <r> [x y z]
        net::Vec3 at{argc > 5 ? (float)atof(argv[3]) : 0.f, argc > 5 ? (float)atof(argv[4]) : 0.f, argc > 5 ? (float)atof(argv[5]) : 0.f};
        SAY("%s", npc::debug_raised((float)atof(argv[2]), argc > 5 ? &at : nullptr).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "nanvel")) { // host (tests, BUG-0894): nanvel <s> [id|-1] [air]
        SAY("%s", npc::debug_nanvel((float)atof(argv[2]), argc > 3 ? atoi(argv[3]) : -1, argc > 4 && !strcmp(argv[4], "air")).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "rxkeep")) { // guest (tests, BUG-0611): rxkeep <n> <s>
        SAY("%s", npc::debug_rxkeep(atoi(argv[2]), (float)atof(argv[3])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "pool")) { // host (tests, BUG-0339): pool <id>
        SAY("%s", npc::debug_pool((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "lift")) { // host (tests, BUG-0550): lift <id> <m> <s>
        SAY("%s", npc::debug_lift((uint16_t)atoi(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "sink")) { // guest (tests, BUG-0399): sink <id> <m> [mark]
        SAY("%s", npc::debug_sink((uint16_t)atoi(argv[2]), (float)atof(argv[3]), argc > 4 && atoi(argv[4]) != 0).c_str());
        return 0;
    }
    if (argc > 7 && !strcmp(argv[1], "pwalk")) { // guest (tests, BUG-0281): pwalk <id> x y z <m/s> <s> [mode bits]
        SAY("%s", npc::debug_pwalk((uint16_t)atoi(argv[2]), {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])},
                                   (float)atof(argv[6]), (float)atof(argv[7]), argc > 8 ? atoi(argv[8]) : 0).c_str());
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
        // BUG-0400: tests shift a partner's position along a world axis (team: host x + 1.5 m), which is
        // a chord of the planet: up to 0.5 m nearer its centre, often under the terrain, and the game
        // does not lift a grounded creature placed under it (docs/re/creature.md#creature-puppet-ground).
        // A point under the ground goes onto it (`coop.fix tools.gopos_ground off`: as given, 0.7.3.3).
        net::Vec3 p{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        float r = drive::speed(p), g = game::ground_radius(p);
        bool lifted = g_gopos_ground_fix && g > 1 && r > 1 && r < g;
        if (lifted)
            p = {p.x * g / r, p.y * g / r, p.z * g / r};
        float up = gopos_onto_surface(p);
        game::teleport(av, p, game::orientation(av));
        if (up > 0.f)
            SAY("avatar moved (onto the surface: %.2f m up, a mound or a rock)", up);
        else
            SAY("avatar moved%s", lifted ? " (onto the ground)" : "");
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
        // BUG-0400 as for gopos: on a slope the point d m aside can be under the terrain (pack_target: the host's
        // own creature 0.6-0.7 m under it for 10 s+, the monitor's ground rule), so it goes onto the ground
        float qr = drive::speed(q), qg = game::ground_radius(q);
        if (g_gopos_ground_fix && qg > 1 && qr > 1 && qr < qg + 0.3f)
            q = {q.x * (qg + 0.3f) / qr, q.y * (qg + 0.3f) / qr, q.z * (qg + 0.3f) / qr};
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
    if (argc > 2 && !strcmp(argv[1], "kill")) { // host (tests, BUG-1100): kill <id>: the NPC's object marked dead
        SAY("%s", npc::debug_kill((uint16_t)atoi(argv[2])).c_str());
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
    if (argc > 2 && !strcmp(argv[1], "aifreeze")) { // host (tests, BUG-9212): aifreeze <hex species | off>
        game::set_ai_freeze(strcmp(argv[2], "off") ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0u);
        SAY("ai freeze %s", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "aifrozen")) {
        SAY("ai frozen ticks %u", game::ai_frozen_ticks());
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
    if (argc > 5 && !strcmp(argv[1], "place")) { // host (tests, BUG-0815): NPC #id put at x y z (the game's teleport)
        void* c = npc::host_npc((uint16_t)atoi(argv[2]));
        if (c)
            game::teleport(c, {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])}, game::orientation(c));
        SAY(c ? "npc placed" : "no such npc");
        return c ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "hosthit")) { // host avatar hits host NPC #id
        bool ok = npc::debug_host_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]), argc > 4 ? (float)atof(argv[4]) : 0.f);
        SAY(ok ? "host hit applied" : "no such npc");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "spawn")) { // host: an NPC of crt species <hex> next to the avatar
        void* c = npc::debug_spawn((uint32_t)strtoul(argv[2], nullptr, 16));
        SAY(c ? "spawned %p" : "unknown species or no avatar", c);
        return c ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "ghost")) { // guest, tests (BUG-0931): proxy #id names memory that is no creature
        SAY("%s", npc::debug_ghost((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "recruit")) { // host: NPC #id joins the pack (AddMember)
        bool ok = npc::debug_recruit((uint16_t)atoi(argv[2]));
        SAY(ok ? "joined the pack" : "no such npc or it cannot join");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "hit")) {
        bool ok = npc::debug_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]));
        SAY(ok ? "hit sent through the damage filter" : "no such spawned proxy (a guest) or NPC (the host)");
        return ok ? 0 : 1;
    }
    if (argc > 1)
        npc::set_enabled(strcmp(argv[1], "off") != 0);
    SAY("npc sync %s\n%s", npc::enabled() ? "on" : "off", npc::status().c_str());
    return 0;
}

// BUG-0422: coop.players held <id>: the per-player state the modules still keep for a player id (player_life.h)
static int c_players(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "held")) {
        std::string h = player_life::held((uint8_t)atoi(argv[2]));
        SAY("%s", h.empty() ? "nothing held\n" : h.c_str());
        return 0;
    }
    SAY("modules with per-player state: %zu\n", player_life::table().size());
    return 0;
}

// BUG-1055: coop.lifecycle [held]: the session / world state the modules registered (lifecycle.h); `held` prints what
// is held now and what the last reset of each scope left ("left session: none" is what a test wants)
static int c_lifecycle(void*, int argc, const char* const* argv, SkOut* out)
{
    auto lines = [](const std::string& s) {
        std::string o;
        for (char c : s)
            o += c == '\n' ? ',' : c;
        if (!o.empty() && o.back() == ',')
            o.pop_back();
        return o.empty() ? std::string("none") : o;
    };
    if (argc > 1 && !strcmp(argv[1], "held")) {
        SAY("now: %s\n", lines(lifecycle::held()).c_str());
        SAY("left session: %s (resets %u)\n", lines(lifecycle::left(lifecycle::kSession)).c_str(),
            lifecycle::resets(lifecycle::kSession));
        SAY("left world: %s (resets %u)\n", lines(lifecycle::left(lifecycle::kWorld)).c_str(),
            lifecycle::resets(lifecycle::kWorld));
        return 0;
    }
    SAY("lifecycle: fix=%d entries=%zu\n", (int)g_lifecycle_fix, lifecycle::table().size());
    for (auto& e : lifecycle::table())
        SAY("  %s %s%s%s held=%zu\n", e.name, (e.scope & lifecycle::kSession) ? "session" : "",
            e.scope == (lifecycle::kSession | lifecycle::kWorld) ? "+" : "", (e.scope & lifecycle::kWorld) ? "world" : "",
            e.held());
    return 0;
}

// BUG-1047: coop.guard [unwind <name> [n] | stick <name>]: the modules' call guards (call_guard.h). `unwind`: the next
// n holds of that guard end as a fault the game caught ends them (the guard stays set); `stick`: set it now. The frame's
// swap guard tick clears a guard left set (`coop.fix core.call_guard_reset off`: it stays set).
static int c_guard(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "unwind")) {
        bool ok = callguard::unwind(argv[2], argc > 3 ? (uint32_t)atoi(argv[3]) : 1u);
        SAY(ok ? "unwind %s armed\n" : "no guard %s\n", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "stick")) {
        callguard::Entry* e = callguard::find(argv[2]);
        if (!e) {
            SAY("no guard %s\n", argv[2]);
            return 0;
        }
        if (e->flag)
            *e->flag = true;
        else
            *e->depth = 1;
        SAY("%s set\n", argv[2]);
        return 0;
    }
    SAY("guards: entries=%zu cleared=%u last=%s\n", callguard::table().size(), callguard::state().cleared,
        callguard::state().last.empty() ? "none" : callguard::state().last.c_str());
    SAY("%s", callguard::status().c_str());
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
    if (argc > 3 && !strcmp(argv[1], "answer")) { // tests (BUG-0525): answer mirror <id> with the ability of type <hex>
        bool ok = social::debug_answer((uint16_t)atoi(argv[2]), strtoul(argv[3], nullptr, 16));
        SAY(ok ? "answer sent" : "no answer (not a guest, no such proxy or ability)");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "abil")) { // tests (BUG-0525): abilities of NPC <id> (host) / proxy (guest), -1 the avatar
        SAY("%s", social::abilities_dump(atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "games")) { // tests (BUG-0831): the host's social games and their prompts
        SAY("%s", social::games_dump().c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "meter_null")) { // tests (BUG-1058): the next n meter tasks find no meter window
        social::debug_meter_null((unsigned)atoi(argv[2]));
        SAY("the next %s meter tasks find no meter window\n", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "cards")) { // tests (BUG-9244): the species goal cards
        SAY("%s", social::cards_dump().c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "meterfill")) { // tests (BUG-9230): meterfill <done> <rounds> [close]
        social::debug_meter_fill(atoi(argv[2]), atoi(argv[3]), argc > 4 && !strcmp(argv[4], "close"));
        SAY("meter fill queued");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "meter")) { // tests (BUG-0179): the game's social meter on this screen
        SAY("%s", social::meter_dump().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "rel")) {
        SAY("%s", social::relations_dump().c_str());
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
    if (argc > 4 && !strcmp(argv[1], "pevent")) { // tests (6V A): pevent <npc id> <event 0..5> <player>, as that player's
        bool ok = social::debug_event_by((uint16_t)atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
        SAY(ok ? "event applied" : "no such living NPC here (the host) or event out of 0..7");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "event")) { // tests (BUG-0168): event <hex crt> <n>, the game's ApplyEvent
        bool ok = social::debug_event(strtoul(argv[2], nullptr, 16), atoi(argv[3]));
        SAY(ok ? "event applied" : "no relationship manager or event out of 6..7 (0..5 need a creature: pevent)");
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
    if (argc > 2 && !strcmp(argv[1], "post")) { // tests (BUG-0994): post <hex creature>: the avatar's social click on it
        SAY("%s", social::debug_post((void*)strtoul(argv[2], nullptr, 16)).c_str());
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
    // tests (BUG-0796): animrx <player id> <hex anim> [n]: as if player id's MSG_ANIM arrived n times (default 1), whatever
    // stage this game shows that player in (a cell in Cell, a marker in Tribe)
    if (argc > 3 && !strcmp(argv[1], "animrx")) {
        net::AnimEvent ev;
        ev.id = (uint8_t)atoi(argv[2]);
        ev.anim = (uint32_t)strtoul(argv[3], nullptr, 16);
        int n = argc > 4 ? std::max(1, atoi(argv[4])) : 1;
        uint32_t played0 = g_anims_played;
        for (int i = 0; i < n; i++)
            on_remote_anim(ev);
        auto it = g_remotes.find(ev.id);
        SAY("animrx: %d delivered to player %u (puppet %p, %s): played=%u not_creature=%u\n", n, ev.id,
            it != g_remotes.end() ? it->second.puppet : nullptr,
            it != g_remotes.end() && it->second.puppet_adapter ? it->second.puppet_adapter->name : "-",
            g_anims_played - played0, g_anims_not_creature);
        return 0;
    }
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
// ---------------- decision 6J: friends, the players' right-click menu, the party invite prompt ----------------
// Friends: this profile's list of player identities (identity::key: "id:<u64>", or "name:<name>" for an older game)
// in <coop dir>\friends.txt, one "key<TAB>name" a line.
static std::map<std::string, std::string> g_friends;
static bool g_friends_loaded;
static std::wstring friends_file() { return creations::coop_dir() + L"\\friends.txt"; }
static void friends_load()
{
    if (g_friends_loaded)
        return;
    g_friends_loaded = true;
    FILE* f = _wfopen(friends_file().c_str(), L"rb");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
            l.pop_back();
        const size_t tab = l.find('\t');
        if (tab != std::string::npos && tab > 0)
            g_friends[l.substr(0, tab)] = l.substr(tab + 1);
    }
    fclose(f);
}
static void friends_save()
{
    const std::wstring path = friends_file(), tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f)
        return;
    for (auto& [k, n] : g_friends)
        fprintf(f, "%s\t%s\n", k.c_str(), n.c_str());
    fclose(f);
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
static bool is_friend(uint8_t id)
{
    friends_load();
    return g_friends.count(identity::key(id)) != 0;
}
static void set_friend(uint8_t id, bool on)
{
    friends_load();
    const std::string k = identity::key(id), n = utf8::shown_name(player_name_of(id));
    if (on)
        g_friends[k] = n;
    else
        g_friends.erase(k);
    friends_save();
    feed(on ? n + " is on your friends list." : n + " is off your friends list.");
    g_lobby_dirty = true;
}

enum MenuCode { kMenuView = 1, kMenuInvite, kMenuLead, kMenuWhisper, kMenuFriend, kMenuUnfriend, kMenuLeave, kMenuAccept,
                kMenuDecline, kMenuKick };
static int g_menu_player = -1; // the menu's player in this session (-1: not here; -2: the invite prompt)
// 6L A: the menu's player as the Social Menu knows it too (a galaxy player who is not in this session)
static uint64_t g_menu_identity;
static std::string g_menu_name;
// 2026-11-18 review: a whisper started from a menu or the Social Menu goes to the player picked there. The chat line
// only carries "/w <name> ", and the first player of that name got it (two "Player", "Bob" and "Bob-NoShare-")
static struct {
    std::string name;
    int sid = -1;
    uint64_t ident = 0;
    double until = 0;
} g_whisper_pick;
static void whisper_to(const std::string& name, int sid, uint64_t ident)
{
    if (!g_sess.connected()) { // (the chat line is the lobby's: no session, no line to type in)
        feed("Whispers are typed in the Co-op window's chat: it is there once you play with others.");
        return;
    }
    g_whisper_pick = {name, sid, ident, H->time_seconds() + 300};
    coop_ui::chat_prefill("/w " + name + " ");
}
static int g_prompted_from = -1; // the invite the prompt shows (-1 none)
static bool g_swallow_rup;
static uint32_t g_menus_opened;
static void open_sporepedia_of(uint8_t player);
static std::string friend_key(int sid, uint64_t ident)
{
    if (ident) {
        char b[24];
        snprintf(b, sizeof b, "id:%016llX", (unsigned long long)ident);
        return b;
    }
    return sid >= 0 ? identity::key((uint8_t)sid) : "";
}
static void set_friend_key(const std::string& k, const std::string& name, bool on)
{
    if (k.empty())
        return;
    friends_load();
    if (on)
        g_friends[k] = name;
    else
        g_friends.erase(k);
    friends_save();
    feed(on ? name + " is on your friends list." : name + " is off your friends list.");
    g_lobby_dirty = true;
}
static bool in_my_galaxy_party(uint64_t ident)
{
    if (!ident)
        return false;
    for (auto& m : parties::members())
        if (m.identity == ident)
            return true;
    return false;
}
static void on_menu_pick(int code)
{
    const int id = g_menu_player;
    const uint64_t ident = g_menu_identity;
    const bool gx = parties::galaxy() && ident;
    std::string r;
    switch (code) {
    case kMenuView: if (id >= 0) open_sporepedia_of((uint8_t)id); break;
    case kMenuInvite:
        r = gx ? parties::invite_identity(ident) : id >= 0 ? parties::invite((uint8_t)id) : "That player is not reachable from here.";
        break;
    case kMenuLead: r = gx ? parties::lead_identity(ident) : id >= 0 ? parties::make_leader((uint8_t)id) : ""; break;
    case kMenuWhisper: whisper_to(g_menu_name, id, ident); break;
    case kMenuFriend: set_friend_key(friend_key(id, ident), g_menu_name, true); break;
    case kMenuUnfriend: set_friend_key(friend_key(id, ident), g_menu_name, false); break;
    case kMenuLeave: r = parties::leave(); break;
    case kMenuKick: r = gx ? parties::kick_identity(ident) : id >= 0 ? parties::kick((uint8_t)id) : ""; break;
    case kMenuAccept: r = parties::answer(true); g_prompted_from = -1; break;
    case kMenuDecline: r = parties::answer(false); g_prompted_from = -1; break;
    default: break;
    }
    if (!r.empty())
        feed(r);
}
// the menu of a player (sid: in this session, -1 not; ident: its identity, 0 unknown) at the client pixel (x, y)
static void open_menu_for(int sid, uint64_t ident, const std::string& name, float x, float y)
{
    std::vector<coop_ui::MenuItem> items;
    const uint8_t me = g_sess.my_id();
    const bool gx = parties::galaxy();
    const bool self = (sid >= 0 && sid == me && g_sess.connected()) || (ident && ident == identity::own());
    const size_t party_n = gx ? parties::members().size() : parties::my_party().size();
    const bool same = self || (gx ? in_my_galaxy_party(ident) : sid >= 0 && parties::same_party(me, (uint8_t)sid));
    // our button colours (owner 2026-11-18): gold joins, red leaves or removes, blue the rest (all with white text)
    if (sid >= 0)
        items.push_back({"View Creature", kMenuView, coop_ui::kMenuBlue});
    if (self) {
        if (party_n > 1)
            items.push_back({"Leave Party", kMenuLeave, coop_ui::kMenuRed});
    } else {
        if (same) {
            if (parties::i_lead()) {
                items.push_back({"Make Leader", kMenuLead, coop_ui::kMenuBlue});
                items.push_back({"Remove from Party", kMenuKick, coop_ui::kMenuRed});
            }
        } else
            items.push_back({"Invite to Party", kMenuInvite, coop_ui::kMenuGold});
        items.push_back({"Whisper", kMenuWhisper, coop_ui::kMenuBlue});
        friends_load();
        items.push_back(g_friends.count(friend_key(sid, ident))
                            ? coop_ui::MenuItem{"Remove Friend", kMenuUnfriend, coop_ui::kMenuGrey}
                            : coop_ui::MenuItem{"Add Friend", kMenuFriend, coop_ui::kMenuBlue});
    }
    if (items.empty())
        return;
    bool leads = false;
    if (gx) {
        for (auto& m : parties::members())
            leads = leads || (m.leader && m.identity == ident);
    } else if (sid >= 0)
        leads = parties::leader_of((uint8_t)sid) == sid;
    std::string title = name;
    if (leads && same && party_n > 1)
        title += " (leader)";
    g_menu_player = sid;
    g_menu_identity = ident;
    g_menu_name = name;
    g_menus_opened++;
    coop_ui::show_player_menu(x, y, title, items, on_menu_pick);
}
static void open_player_menu(uint8_t id, float x, float y)
{
    const uint64_t ident = id == g_sess.my_id() ? identity::own() : identity::known(id);
    open_menu_for(id, ident, utf8::shown_name(player_name_of(id)), x, y);
}

// ---------------- 6L A: the Social Menu's rows (coop_ui.cpp draws them) ----------------
static const char* stage_label(uint32_t m)
{
    switch (m) {
    case modes::kCell: return "Cell";
    case modes::kCreature: return "Creature";
    case modes::kTribe: return "Tribe";
    case modes::kCiv: return "Civilization";
    case modes::kSpace: return "Space";
    default: return "";
    }
}
static int session_id_of(uint64_t ident)
{
    if (!ident || !g_sess.connected())
        return -1;
    for (auto& p : g_sess.players())
        if ((p.id == g_sess.my_id() ? identity::own() : identity::known(p.id)) == ident)
            return p.id;
    return -1;
}
static uint32_t session_stage_of(int sid)
{
    if (sid < 0)
        return 0;
    if (sid == g_sess.my_id())
        return g_mode;
    auto it = g_remotes.find((uint8_t)sid);
    return it == g_remotes.end() || it->second.buf.empty() ? 0 : it->second.buf.latest().mode;
}
static std::string where_text(const std::string& planet, uint32_t stage, bool online)
{
    if (!online)
        return "Offline";
    const std::string st = stage_label(stage);
    if (planet.empty())
        return st.empty() ? "Galaxy menu" : st;
    return st.empty() ? planet : planet + "  -  " + st;
}
static void on_social_action(int action, const coop_ui::SocialRow& row, float x, float y)
{
    std::string r;
    switch (action) {
    case coop_ui::kSocLeave: r = parties::leave(); break;
    case coop_ui::kSocWhisper: whisper_to(row.name, row.session_id, row.identity); break;
    case coop_ui::kSocInvite:
        r = parties::galaxy() && row.identity ? parties::invite_identity(row.identity)
            : row.session_id >= 0              ? parties::invite((uint8_t)row.session_id)
                                               : "That player is not reachable from here.";
        break;
    case coop_ui::kSocMenu: open_menu_for(row.session_id, row.identity, row.name, x, y); break;
    default: break;
    }
    if (!r.empty())
        feed(r);
}
static void social_tick(double now)
{
    static double s_next = 0, s_ask = 0;
    if (now < s_next)
        return;
    s_next = now + 0.5;
    coop_ui::SocialState st;
    const bool gx = parties::galaxy();
    st.galaxy = gx;
    st.stage = g_mode;
    const bool in_stage = g_mode >= modes::kCell && g_mode <= modes::kSpace;
    st.button = in_stage && (g_sess.connected() || galaxy::welcomed()) && !(game_busy() & (busy::kPaused | busy::kInputLock));
    if (coop_ui::social_open() && gx && now >= s_ask) {
        s_ask = now + 3;
        parties::ask_online();
    }
    std::set<uint64_t> listed;
    const uint64_t own = identity::own();
    // the party
    if (gx) {
        for (auto& m : parties::members()) {
            coop_ui::SocialRow r;
            r.name = utf8::shown_name(m.name);
            r.identity = m.identity;
            r.session_id = session_id_of(m.identity);
            r.me = m.me || (own && m.identity == own);
            r.leader = m.leader;
            const uint32_t stg = r.session_id >= 0 ? session_stage_of(r.session_id) : m.stage;
            r.where = where_text(m.planet, stg, true);
            r.section = 0;
            listed.insert(m.identity);
            st.rows.push_back(r);
        }
    } else {
        for (uint8_t id : parties::my_party()) {
            coop_ui::SocialRow r;
            r.session_id = id;
            r.identity = id == g_sess.my_id() ? own : identity::known(id);
            r.name = utf8::shown_name(id == g_sess.my_id() || !g_sess.connected() ? g_name : player_name_of(id));
            r.me = id == g_sess.my_id();
            r.leader = parties::leader_of(id) == id;
            r.where = where_text(id == g_sess.my_id() ? modes::current_world_name() : "", session_stage_of(id), true);
            r.section = 0;
            if (r.identity) listed.insert(r.identity);
            st.rows.push_back(r);
        }
    }
    st.party_n = (int)st.rows.size();
    for (auto& r : st.rows)
        r.buddy = !r.me && g_friends.count(friend_key(r.session_id, r.identity));
    // friends: online (the galaxy's players, this session's) first, then offline
    friends_load();
    std::vector<coop_ui::SocialRow> on, off;
    const auto online = parties::online();
    for (auto& [key, fname] : g_friends) {
        coop_ui::SocialRow r;
        r.name = fname;
        r.section = 1;
        r.buddy = true;
        uint64_t ident = 0;
        if (key.rfind("id:", 0) == 0)
            ident = strtoull(key.c_str() + 3, nullptr, 16);
        if (ident && listed.count(ident))
            continue; // (in the party: listed there)
        r.identity = ident;
        bool found = false;
        for (auto& o : online)
            if (ident && o.identity == ident) {
                r.name = utf8::shown_name(o.name);
                r.where = where_text(o.planet, 0, true);
                found = true;
            }
        r.session_id = session_id_of(ident);
        if (r.session_id >= 0) {
            r.where = where_text(modes::current_world_name(), session_stage_of(r.session_id), true);
            found = true;
        }
        r.online = found;
        if (!found)
            r.where = "Offline";
        if (ident)
            listed.insert(ident);
        (found ? on : off).push_back(r);
    }
    st.friends_online = (int)on.size();
    for (auto& r : on) st.rows.push_back(r);
    for (auto& r : off) st.rows.push_back(r);
    // the galaxy's other players
    if (gx) {
        int shown = 0;
        for (auto& o : online) {
            if (!o.identity || listed.count(o.identity) || o.identity == own)
                continue;
            st.online_n++;
            if (shown >= 5)
                continue;
            coop_ui::SocialRow r;
            r.name = utf8::shown_name(o.name);
            r.identity = o.identity;
            r.session_id = session_id_of(o.identity);
            r.where = where_text(o.planet, r.session_id >= 0 ? session_stage_of(r.session_id) : 0, true);
            r.section = 2;
            st.rows.push_back(r);
            shown++;
        }
    }
    coop_ui::set_social(st, on_social_action);
}

// the invite prompt: the same panel in the upper middle of the screen, Accept / Decline
static void invite_prompt_tick()
{
    const int from = parties::pending_invite();
    const bool shown = coop_ui::player_menu_open() && g_menu_player == -2;
    if (from < 0) {
        if (shown)
            coop_ui::hide_player_menu();
        g_prompted_from = -1;
        return;
    }
    // (2026-11-18 review: another player menu replaced the prompt and it never came back: shown again once that menu
    // is closed, as long as the invite is pending)
    if (from == g_prompted_from && shown)
        return;
    if (coop_ui::player_menu_open() && !shown)
        return;
    g_prompted_from = from;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    g_menu_player = -2; // (the prompt)
    coop_ui::show_player_menu(sw * 0.5f - 100, sh * 0.32f, utf8::shown_name(parties::pending_name()) + " invites you",
                              {{"Join Party", kMenuAccept, coop_ui::kMenuGold}, {"Decline", kMenuDecline, coop_ui::kMenuRed}},
                              on_menu_pick);
}
// every left / right press in the game window (mouse.cpp, before the UI scale remap)
static uint32_t g_menu_presses; // (tests)
static bool player_menu_input(unsigned msg, int x, int y)
{
    if (msg == WM_RBUTTONDOWN)
        g_menu_presses++;
    if (msg == WM_LBUTTONDOWN) { // on the menu: its item (here, also under the game's input lock); beside it: it closes
        if (coop_ui::player_menu_press((float)x, (float)y))
            return true;
        if (coop_ui::player_menu_open() && g_menu_player != -2)
            coop_ui::hide_player_menu();
        return coop_ui::social_press((float)x, (float)y, false); // 6L A: the Social button and panel
    }
    if (msg == WM_RBUTTONUP) {
        const bool swallow = g_swallow_rup;
        g_swallow_rup = false;
        return swallow;
    }
    if (coop_ui::player_menu_open() && g_menu_player != -2) {
        coop_ui::hide_player_menu();
        if (coop_ui::point_in_player_menu((float)x, (float)y))
            return g_swallow_rup = true;
    }
    if (coop_ui::social_press((float)x, (float)y, true)) // 6L A: a right-click on a Social Menu row
        return g_swallow_rup = true;
    if (!g_sess.connected())
        return false;
    int id = coop_ui::player_at((float)x, (float)y); // a lobby row, a Party card
    if (id < 0) { // a name plate or tag, then a partner's creature on the screen
        const double now = H->time_seconds();
        float best = 1e9f;
        for (auto& [pid, p] : g_pick) {
            if (now - p.at > 0.5)
                continue;
            if (x >= p.x1 && x < p.x2 && y >= p.y1 && y < p.y2) {
                id = pid;
                break;
            }
            const float d = std::hypot(x - p.cx, y - p.cy);
            if (p.reach > 0 && d < p.reach && d < best) {
                best = d;
                id = pid;
            }
        }
    }
    if (id < 0 || player_name_of((uint8_t)id).empty())
        return false;
    open_player_menu((uint8_t)id, (float)x, (float)y);
    return g_swallow_rup = true;
}

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
        r.buddy = !r.me && is_friend(p.id);               // 6J
        r.party = parties::same_party(g_sess.my_id(), p.id);
        r.leads = parties::leader_of(p.id) == p.id && parties::my_party().size() > 1 && r.party;
        v.push_back(r);
    }
    return v;
}

std::string ui_session_line()
{
    char b[256];
    if (g_sess.role() == net::Role::Host)
        snprintf(b, sizeof b, "Hosting on UDP port %u  -  %d/%d players", g_sess.port(), (int)g_sess.players().size(),
                 std::min<int>(g_sess.seat_limit, g_sess.max_players)); // Lineages slots, all.max_players
    else if (g_sess.connected())
        snprintf(b, sizeof b, "Connected to %s (%s)", utf8::head(utf8::shown_name(g_sess.name_of(0)), 16).c_str(),
                 g_sess.host_address().c_str()); // BUG-9222 / 9221: no tag, short enough for one line
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
    if ((host_mode == modes::kEdit || host_mode == modes::kEditor) && g_sess.role() == net::Role::Client && !g_local)
        return g_sess.name_of(0) + " is making a new world: it comes to you when they finish"; // BUG-9203: not evolving
    if (host_mode == modes::kEdit || host_mode == modes::kEditor)
        return g_sess.name_of(0) + " is evolving the species in the editor: everybody gets the new body";
    if (g_sess.role() == net::Role::Host && g_mode == modes::kTribe) // BUG-9135 phase 4: a partner without a village
        if (std::string line = tribe::founding_wait_line(); !line.empty()) return line;
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
    if (argc > 2 && !strcmp(argv[1], "hit") && local_now() && (g_mode == modes::kCreature || g_mode == modes::kScenario)) { // tests: a hit through
        game::hit(local_now(), (float)atof(argv[2]), 0, nullptr); // the damage filter (the pack rescue applies)
        SAY("hit applied, hp %.1f", game::health(local_now()));
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "pvp") && local_now() && g_mode == modes::kCreature) { // tests (F3 friendly fire):
        auto it = g_remotes.find((uint8_t)atoi(argv[2]));                                  // our creature hits a partner's
        if (it == g_remotes.end() || !puppet_now(it->second)) {                             // puppet through the game's
            SAY("no puppet of player %s", argv[2]);                                         // TakeDamage (the filter)
            return 1;
        }
        game::hit(it->second.puppet, (float)atof(argv[3]), 0, local_now());
        SAY("hit on player %s's creature sent through the damage filter", argv[2]);
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
    a.join = [](const std::string& addr, std::string& err, const std::string& pw) { return do_join(addr, err, pw); };
    a.host = [](uint16_t port, unsigned seats, unsigned scaling, std::string& err, const std::string& pw) {
        return do_host(port, seats, scaling, err, pw);
    };
    a.queue_seats = [](unsigned seats, std::string& result) { return world::set_seats(g_sess,seats,result); };
    a.leave = do_leave;
    a.role = [] { return g_sess.role() == net::Role::Host ? 1 : g_sess.role() == net::Role::Client ? 2 : 0; };
    a.connected = [] { return g_sess.connected(); };
    a.session_line = ui_session_line;
    a.seat_limit = [] { return g_sess.seat_limit; };
    a.invite = [] {
        std::string addresses;
        for (const auto& adapter : local_addresses()) {
            if (!addresses.empty()) addresses += ", ";
            addresses += adapter.ip + ':' + std::to_string(g_sess.port());
        }
        return addresses;
    };
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
        // BUG-9135 phase 4: in Tribe for a partner without a village of their own (tribe.late_prompt), in Civ without
        // a nation of their own (civ::late_prompt)
        const bool late = (g_mode == modes::kTribe && tribe::late_prompt() != 0) || (g_mode == modes::kCiv && civ::late_prompt() != 0);
        if (!g_sess.connected() || !(g_mode == modes::kCreature || g_mode == modes::kCell || g_mode == modes::kSpace || late))
            return false;
        creations::Kind k = g_mode == modes::kCell ? creations::kCell
                            : g_mode == modes::kSpace ? creations::kUfo : creations::kCreature;
        creations::Option o = creations::mine(k);
        kind = k;
        name = o.name;
        own = (int)creations::options(k).size();
        return true;
    };
    a.late_prompt = [] { return g_mode == modes::kTribe ? tribe::late_prompt() : g_mode == modes::kCiv ? civ::late_prompt() : 0; };
    a.create_creature_offered = late_create_offered;
    a.create_creature = [] {
        H->run_outside_frame([](void*) {
            const std::string said = open_new_creature_editor();
            feed(said);
        }, nullptr);
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
            line = std::string(ru ? c.ru : c.en) + (ru ? ": этот цвет у другого игрока" : ": another player has this color");
            return false;
        }
        s_try = -1;
        line = g_sess.role() == net::Role::Client ? std::string(ru ? "Цвет запрошен у хоста: " : "Asked the host for ") + (ru ? c.ru : c.en)
                                                  : std::string(ru ? "Ваш цвет: " : "Your color: ") + (ru ? c.ru : c.en);
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
    a.paced_transfer = [] { return g_sess.paced_world_transfer; };
    a.set_paced_transfer = [](bool on) { g_sess.paced_world_transfer = on; };
    a.foreign_creations = [] { return library::foreign_creations(); }; // BUG-9112
    a.set_foreign_creations = [](bool on) { library::set_foreign_creations(on); };
    a.lineage_cells = [] { return lineage_cells::enabled(); }; // BUG-9111
    a.set_lineage_cells = [](bool on) { lineage_cells::set_enabled(on); };
    a.one_creature = [] { return g_sess.role() == net::Role::Client && host_creature_mode(); };
    a.evolve_offer = [] { // BUG-0516
        return g_offer_editor && !g_vote.active ? std::max(1, (int)(g_offer_until - H->time_seconds())) : 0;
    };
    a.share_state = [] { return !(g_adapter && g_local) ? 1 : g_sess.players().size() > 1 ? 0 : 2; };
    a.say = [](const std::string& t) {
        if (support::chat_command(t)) // "/bug <text>" (docs/REPORTING.md#reporting-support)
            return;
        if (t.rfind("/w ", 0) == 0) { // 6J: "/w <name> <text>": the longest player name the line starts with
            const std::string rest = t.substr(3);
            int to = -1;
            size_t best = 0;
            for (auto& p : g_sess.players()) {
                const std::string n = utf8::shown_name(p.name);
                if (!n.empty() && n.size() > best && rest.rfind(n, 0) == 0 && (rest.size() == n.size() || rest[n.size()] == ' ')) {
                    to = p.id;
                    best = n.size();
                }
            }
            uint64_t gto = 0; // 6L A: a player of the galaxy (any planet, the galaxy menu)
            bool picked = false; // (the player picked in a menu, when the line still names it)
            if (const auto& w = g_whisper_pick; !w.name.empty() && H->time_seconds() < w.until &&
                                                 rest.rfind(w.name, 0) == 0 && (rest.size() == w.name.size() || rest[w.name.size()] == ' ')) {
                const bool here = w.sid >= 0 && [&] {
                    for (auto& p : g_sess.players())
                        if (p.id == w.sid) return utf8::shown_name(p.name) == w.name;
                    return false;
                }();
                if (here || w.ident) {
                    to = here ? w.sid : -1;
                    gto = here ? 0 : w.ident;
                    best = w.name.size();
                    picked = true;
                }
            }
            if (parties::galaxy() && !picked)
                for (auto& o : parties::online()) {
                    const std::string n = utf8::shown_name(o.name);
                    if (!n.empty() && n.size() > best && rest.rfind(n, 0) == 0 && (rest.size() == n.size() || rest[n.size()] == ' ')) {
                        gto = o.identity;
                        to = -1;
                        best = n.size();
                    }
                }
            const std::string text = to < 0 && !gto ? "" : rest.substr(std::min(rest.size(), best + 1));
            std::string r;
            if (gto) {
                if (text.empty()) r = "Nothing to whisper.";
                else if (!galaxy::whisper(gto, text)) r = "Not connected to the galaxy server.";
                else chat_log("To " + rest.substr(0, best) + ": " + text);
            } else
                r = to < 0 ? "Whisper to whom? /w <name> <text>" : whisper((uint8_t)to, text);
            if (!r.empty())
                feed(r);
            return;
        }
        if (bans::chat_command(t)) // BUG-9251: "/kick", "/ban", "/unban", "/bans" (the host)
            return;
        if (g_sess.connected())
            g_sess.send_chat(t);
    };
    a.chat = [] { return std::vector<std::string>(g_chat.begin(), g_chat.end()); };
    a.chat_colors = [] { return std::vector<uint32_t>(g_chat_col.begin(), g_chat_col.end()); };
    a.lan_games = [] {
        std::vector<coop_ui::LanGame> v;
        for (auto& g : g_disc.games(net::now_ms()))
            v.push_back({g.info.name, g.host.str(), g.info.info, g.info.players, g.info.max_players,
                         g.info.scaling, g.info.settings});
        return v;
    };
    a.lan_scan = lan_scan;
    a.galaxy_info = [](std::string& name, std::string& line, bool& connected) { // decision 5Y A (alpha)
        connected = galaxy::online();
        name = connected ? galaxy::galaxy_name() : "Galaxy Server";
        if (connected) {
            const auto ps = galaxy::planets_known();
            unsigned players = 0;
            for (auto& p : ps)
                players += p.players;
            line = std::to_string(ps.size()) + (ps.size() == 1 ? " planet, " : " planets, ") + std::to_string(players) +
                   (players == 1 ? " player" : " players") + " online";
        } else
            line = coop_ui::load_ui_str("galaxy_address").empty() ? "Online worlds  -  port 47900"
                                                                  : "Online worlds  -  select, then Join";
        return true;
    };
    a.galaxy_disconnect = [] { H->log(M, SK_INFO, "%s", galaxy::disconnect().c_str()); };
    a.galaxy_connect = [](const std::string& addr, std::string& err) {
        const std::string r = galaxy::connect(addr);
        if (r.rfind("Connecting", 0) == 0)
            return true;
        err = r;
        return false;
    };
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
    if (argc > 2 && !strcmp(argv[1], "lang")) { // tests: the layout of the texts that change in Russian
        coop_ui::set_russian(!strcmp(argv[2], "ru"));
        SAY("ui lang %s (the texts set from now on)", coop_ui::russian() ? "ru" : "en");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "first")) { // tests: the window as at the first start (the name step)
        coop_ui::first_start();
        SAY("ui: back to the first start (the name step)");
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[1], "nudge") || !strcmp(argv[1], "hide"))) { // tests: ui_golden's twin
        bool hide = !strcmp(argv[1], "hide");
        SAY("%s", coop_ui::test_nudge(argv[2], argc > 3 && !hide ? (float)atof(argv[3]) : 0.f,
                                      argc > 4 && !hide ? (float)atof(argv[4]) : 0.f, hide,
                                      argc > 5 && !hide ? (float)atof(argv[5]) : 0.f).c_str()); // [dw]: BUG-1065's twin
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "color")) { // tests / layout shots: a colour for players without one
        coop_ui::set_test_color((uint32_t)strtoul(argv[2], nullptr, 16));
        SAY("ui: test colour %s", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "demo")) { // README pictures: documentation address and port
        if (argc > 4)
            coop_ui::set_demo(argv[2], argv[3], argv[4]);
        else
            coop_ui::set_demo("", "", "");
        SAY("ui: demo %s", argc > 4 ? argv[2] : "off");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "widgets")) { // tests: the visible widgets for layout checks
        SAY("%s", coop_ui::widgets_state().c_str());
        return 0;
    }
    if (argc > 1)
        coop_ui::open(atoi(argv[1]) != 0);
    SAY("%s", coop_ui::debug_state().c_str());
    return 0;
}

void on_unload()
{
    support::on_unload();
    gamefixes::shutdown();
    fov::shutdown();
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
    rejoin::shutdown();
    end_session(EndPath::Unload, true);
}

// ---- support diagnostics (diag_coop.h, docs/REPORTING.md#reporting-support) ----
// BUG-0380: the sync-check leaves out a Tribe / Civ pointer that moved within kCursorStill s
static bool g_cursor_check_fix = true;
COOP_FIX("diag.rts_cursor", "BUG-0380", g_cursor_check_fix,
         "sync-check: a Tribe / Civ player's pointer is compared only after it stood still for a second");
constexpr double kCursorStill = 1.0;

int g_ini_auto_load = 1, g_ini_share_npcs = 1, g_ini_update = 1, g_ini_sync = 30;

void support_init()
{
    g_ini_auto_load = H->config_int(M, "coop", "auto_load_world", 1);
    fprof::g_on = H->config_int(M, "diag", "frame_prof", 1) != 0; // BUG-9257: the frame profiler's prof lines
    g_ini_share_npcs = H->config_int(M, "coop", "share_npcs", 1);
    g_ini_update = H->config_int(M, "update", "check", 1);
    g_ini_sync = H->config_int(M, "coop", "sync_check", 30);
    support::Probe p;
    p.sess = &g_sess;
    p.feed = feed;
    p.chat_log = [](const std::string& t) { chat_log(t); };
    p.my_name = [] { return utf8::shown_name(g_name); }; // BUG-9222: shown without the -NoShare- tag
    p.entries = sync_all;
    p.players_out = [] { // dead, respawning or not in a world: hidden on purpose (drive_puppet's rules)
        std::vector<uint8_t> v;
        double now = H->time_seconds();
        net::PlayerState own = local_state();
        // BUG-0380: a Tribe / Civ player is a pointer (the planet point under the mouse, moved by the
        // mouse and by the camera); a partner's copy is ~0.1 s + half a round trip behind it, so a
        // moving pointer was "apart" (median 23 m in campaigns, sync-check `p apart`). Left out while
        // it moved in the last second on this machine; a pointer that stands still is compared.
        bool cursor = g_cursor_check_fix;
        if (!local_now() || (own.flags & net::PF_DEAD) || !(own.flags & net::PF_IN_WORLD) ||
            (cursor && modes::rts_marker_moving(g_adapter, local_now(), kCursorStill)))
            v.push_back(g_sess.my_id());
        for (auto& [id, r] : g_remotes) {
            if (r.echo)
                continue;
            if (r.buf.empty() || r.buf.age(now) >= 5.0 || (r.buf.latest().flags & net::PF_DEAD) ||
                !(r.buf.latest().flags & net::PF_IN_WORLD) ||
                (cursor && modes::rts_marker_moving(r.puppet_adapter, puppet_now(r), kCursorStill)))
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
        // BUG-9162: with personal DNA each player's budget is their own; the party's progress bar is what must agree
        v = personal_dna() ? game::evolution_bar() : game::dna();
        return true;
    };
    p.cell_food = [](int& v) { return g_mode == modes::kCell && cellsync::food_total(v); };
    p.cell_scale = [] { return g_mode == modes::kCell ? cellsync::scale_now() : 0.f; };
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

int c_framefix(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1)
        g_frame_fix = atoi(argv[1]) != 0;
    SAY("frame order fix %s: calls on a puppet the game no longer lists: anims dropped=%u made=%u, despawns "
        "dropped=%u made=%u; anims for a puppet that is no creature dropped=%u (BUG-0796)\n",
        g_frame_fix ? "on" : "off", g_anims_dropped, g_anims_unlisted, g_despawns_dropped, g_despawns_unlisted,
        g_anims_not_creature);
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
                {"settings_in", &g_settings_in}}; // (away_goal: retired in Lineages, social.cpp has no switch)
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
    g_sess.paced_world_transfer = H->config_int(M, "coop", "paced_world_transfer", 0) != 0;
    trace::init(H, M);
    trace::g_detail = std::max(0, std::min(2, H->config_int(M, "coop", "log_detail", 1)));
    world::init(H, M, feed);
    if (build_id() == 0xDC04AEE5u) {
        editor_cfg::init(H, M);
        if (!editor_depth::init(H, M)) LOG("editor depth handle: symbols missing, off");
    }
    if (build_id() == 0xDC04AEE5u)
        fov::set_camera_target(spectate_target); // BUG-9156
        fov::init(H, M, [] {
            // Creature can pause its simulation without taking away the follow camera.
            return (game_busy() & ~busy::kPaused) == 0 && !coop_ui::is_open();
        }, feed);
    g_mate_classify = H->sym("SporeCoop::CreatureClickAction");
    g_get_animal_target = reinterpret_cast<decltype(g_get_animal_target)>(
        H->sym("Simulator::cCreatureBase::GetAnimalTarget"));
    if (const auto response = H->sym("SporeCoop::CreatureMateResponse"); response && g_mate_classify && g_get_animal_target)
        H->hook_create(M, (void*)response, (void*)mate_response, (void**)&g_native_mate_response);
    g_mate_position_reached = reinterpret_cast<decltype(g_mate_position_reached)>(
        H->sym("SporeCoop::CreatureMatePositionReached"));
    if (const auto step = H->sym("SporeCoop::CreatureMateStep"); step && g_get_animal_target && g_mate_position_reached)
        H->hook_create(M, (void*)step, (void*)mate_step, (void**)&g_native_mate_step);
    g_ptr_assign = H->sym("SporeCoop::IntrusivePtr_Assign");
    if (const auto keeper = H->sym("SporeCoop::CreatureMateKeeper"); keeper && g_ptr_assign) // BUG-9174
        H->hook_create(M, (void*)keeper, (void*)mate_keeper, (void**)&g_native_mate_keeper);
    g_get_creature_effect = reinterpret_cast<decltype(g_get_creature_effect)>(
        H->sym("Simulator::cCreatureBase::GetEffectFromPools"));
    g_start_creature_effect = reinterpret_cast<decltype(g_start_creature_effect)>(
        H->sym("Simulator::cCreatureBase::StartOrStopEffectFromPool2"));
    world::set_playing_probe([] { return g_local != nullptr; });
    world::set_place_probe([] { return g_world; });
    world::set_host_stage_probe([] {
        auto host = g_remotes.find(0);
        return host != g_remotes.end() && !host->second.buf.empty() ? host->second.buf.latest().mode : 0u;
    });
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
    game::set_dna_listener(on_dna);
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
    ctx.player_downed = [](uint8_t id) {
        auto it = g_remotes.find(id);
        return it != g_remotes.end() && !it->second.buf.empty() && (it->second.buf.latest().flags & net::PF_DOWNED);
    };
    ctx.feed = feed;
    // its own NPCs stay while it plays another world (BUG-0130), also until the host's first state
    // says where the host is (a join from inside a game of its own)
    ctx.host_away = [] { return host_absent_for(Absent::Npc); }; // BUG-0130, BUG-0231
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
        hc.in_session = [] { return g_sess.connected(); };
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
        pc.feed = [](const std::string& t) { feed(net::g_fix.shown_names ? utf8::shown_name(t) : t); }; // decision 5FA
        player_rules::init(H, M, pc);
        guest_pack::Ctx gc; // BUG-9129
        gc.sess = &g_sess;
        gc.puppet_of = pc.puppet_of;
        gc.notice = [](const std::string& t) { notice(t, -1); };
        gc.world = [] { return g_world; };
        guest_pack::init(H, M, gc);
        H->command_add(M, "coop.gpack", "coop.gpack [summon <guest> | recruit <guest> <npc id>] - the guests' packs the host runs (BUG-9129); summon / recruit: tests (host)",
                       [](void*, int argc, const char* const* argv, SkOut* out) {
                           if (argc > 2 && !strcmp(argv[1], "watch")) { // tests (decision 5CD): who hits NPC <id>
                               npc::g_damage_watch = npc::host_npc((uint16_t)atoi(argv[2]));
                               H->out_printf(out, "watching %p\n", npc::g_damage_watch);
                           }
                           if (argc > 2 && !strcmp(argv[1], "hostorder")) { // tests (decision 5CD): the same for the host's own
                               void* t = npc::host_npc((uint16_t)atoi(argv[2]));       // creature (its click: target, command)
                               if (!t) // (a guest: its own creature's command at proxy <id>, decision 5EA)
                                   t = npc::proxy((uint16_t)atoi(argv[2]));
                               guest_pack::host_command(t);
                               H->out_printf(out, "host order at %p\n", t);
                           }
                           if (argc > 3 && !strcmp(argv[1], "order")) { // tests (decision 5CD): the guest's attack command as it
                               void* t = npc::host_npc((uint16_t)atoi(argv[3])); // arrives from its game (K_PACKCMD 0x10)
                               guest_pack::pack_command((uint8_t)atoi(argv[2]), 0x10u, 0u, 3.4028235e38f, 0u, 0u, t);
                               H->out_printf(out, "order sent to %p\n", t);
                           }
                           if (argc > 2 && !strcmp(argv[1], "summonlimit")) // decision 5FA: the party's summons out at once
                               player_rules::set_summon_limit(atoi(argv[2]));
                           if (argc > 2 && !strcmp(argv[1], "baruse")) // tests (5FA): press the avatar's ability <hex type> on the bar
                               H->out_printf(out, "bar use: %s\n", player_rules::debug_bar_use((uint32_t)strtoul(argv[2], nullptr, 16)) ? "pressed" : "no such ability");
                           if (argc > 2 && !strcmp(argv[1], "ctx")) // tests (decision 5CD)
                               guest_pack::set_ctx_test(atoi(argv[2]) != 0);
                           if (argc > 2 && !strcmp(argv[1], "follow")) // tests (decision 5CD): the follow stimulus by hand
                               guest_pack::set_follow_test(atoi(argv[2]) != 0);
                           if (argc > 1 && !strcmp(argv[1], "callers")) { // tests (decision 5CD)
                               H->out_printf(out, "%s", guest_pack::callers(argc > 2 && !strcmp(argv[2], "start")).c_str());
                               return 0;
                           }
                           if (argc > 2 && !strcmp(argv[1], "herd"))
                               guest_pack::set_herd_fix(atoi(argv[2]) != 0);
                           if (argc > 2 && !strcmp(argv[1], "summon"))
                               H->out_printf(out, "summoned %d\n", guest_pack::summon((uint8_t)atoi(argv[2])));
                           else if (argc > 3 && !strcmp(argv[1], "recruit"))
                               H->out_printf(out, "recruit %d\n", (int)guest_pack::recruit((uint8_t)atoi(argv[2]), npc::host_npc((uint16_t)atoi(argv[3]))));
                           H->out_printf(out, "%s", guest_pack::status().c_str());
                           H->out_printf(out, "%s", player_rules::summon_lock_status().c_str()); // decision 5FA
                           return 0;
                       }, nullptr, SK_CMD_MAIN_THREAD);
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
    party_bar::init(H, M); // BUG-9277
    home_nests::Ctx hc; // roadmap phase 2
    hc.sess = &g_sess;
    hc.notice = [](const std::string& t) { notice(t, -1); };
    hc.world = [] { return g_world; };
    home_nests::init(H, M, hc);
    game::set_damage_filter(npc::damage_filter);
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
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.seats", "coop.seats <1-4> [now] - reserve seats for the next stage or next Space load; now: more seats in this stage at once", c_seats, nullptr, MT);
    H->command_add(M, "coop.host", "coop.host [port] [name] [new-game seats 1-4] [scaling percent] - host a session", c_host, nullptr, MT);
    H->command_add(M, "coop.join", "coop.join <ip[:port]> [name] - join a host", c_join, nullptr, MT);
    H->command_add(M, "coop.leave", "leave the session", c_leave, nullptr, MT);
    // BUG-9251 (decision 5I): kick and ban, as the host's chat commands /kick /ban /unban /bans
    H->command_add(M, "coop.kick", "coop.kick <player id|name> - the host removes a player from the game",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       H->out_printf(out, "%s\n", argc > 1 ? [&] { static std::string r; r = bans::kick(argv[1], false); if (r.empty()) r = "done (the notice is in every feed)"; return r.c_str(); }() : "coop.kick <player>");
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.ban", "coop.ban <player id|name> - the host removes a player and keeps them out of its games",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       H->out_printf(out, "%s\n", argc > 1 ? [&] { static std::string r; r = bans::kick(argv[1], true); if (r.empty()) r = "done (the notice is in every feed)"; return r.c_str(); }() : "coop.ban <player>");
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.unban", "coop.unban <name|list number|identity> - let a banned player in again",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       H->out_printf(out, "%s\n", argc > 1 ? bans::unban(argv[1]).c_str() : "coop.unban <name>");
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.visit", "coop.visit spawn <hex instance> <hex type> <hex group> [drive|stand] [lift m] [axes 0|1|2] [prop|ufo] | clear | status - another era's model in this Creature game (alpha spike, decision 5S)",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       std::string r;
                       const std::string sub = argc > 1 ? argv[1] : "status";
                       if (sub == "spawn" && argc > 4)
                           r = era_visitors::spawn((uint32_t)strtoul(argv[2], nullptr, 16), (uint32_t)strtoul(argv[3], nullptr, 16),
                                                   (uint32_t)strtoul(argv[4], nullptr, 16), argc > 5 && !strcmp(argv[5], "drive"),
                                                   argc > 6 ? (float)atof(argv[6]) : 0.f, argc > 7 ? atoi(argv[7]) : 0,
                                                   !(argc > 8 && !strcmp(argv[8], "ufo")));
                       else if (sub == "clear")
                           r = era_visitors::clear();
                       else
                           r = era_visitors::status();
                       H->out_printf(out, "%s\n", r.c_str());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.gmenu", "coop.gmenu [status | on | off | rebuild] - the galaxy menu shows the galaxy server's planets (alpha, decision 5X B)",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       H->out_printf(out, "%s\n", galaxy_menu::command(argc, argv).c_str());
                       return 0;
                   }, nullptr, MT);
    // 6J: parties of 4 and whispers (the right-click menu calls the same)
    H->command_add(M, "coop.party", "coop.party [status | invite | kick | lead <player> | yes | no | leave | online | ginvite | gkick <name>] - parties of 4 (6J, 6K); <player>: id or name",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       const std::string sub = argc > 1 ? argv[1] : "status";
                       std::string arg;
                       for (int i = 2; i < argc; i++)
                           arg += (i > 2 ? " " : "") + std::string(argv[i]);
                       auto who = [&]() -> int {
                           if (!arg.empty() && arg.find_first_not_of("0123456789") == std::string::npos)
                               return atoi(arg.c_str());
                           for (auto& p : g_sess.players())
                               if (utf8::shown_name(p.name) == arg || p.name == arg)
                                   return p.id;
                           return -1;
                       };
                       std::string r;
                       if (sub == "invite" || sub == "lead") {
                           const int id = who();
                           if (id < 0)
                               r = "No such player: " + arg;
                           else
                               r = sub == "invite" ? parties::invite((uint8_t)id) : parties::make_leader((uint8_t)id);
                       } else if (sub == "rclick" || sub == "lclick") { // tests: a real press through the window's queue
                           int x = 0, y = 0;
                           if (sscanf(arg.c_str(), "%d %d", &x, &y) == 2) {
                               HWND w = (HWND)H->game_window();
                               const bool right = sub == "rclick";
                               PostMessageW(w, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(x, y));
                               PostMessageW(w, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                               r = "posted";
                           } else
                               r = "coop.party rclick <x> <y>";
                       } else if (sub == "menu") {
                           int x = 0, y = 0;
                           sscanf(arg.c_str(), "%d %d", &x, &y);
                           r = "menus opened " + std::to_string(g_menus_opened) + ", presses " + std::to_string(g_menu_presses) +
                               ", open " + std::to_string((int)coop_ui::player_menu_open()) + ", player " +
                               std::to_string(g_menu_player) + ", at (" + arg + "): " +
                               std::to_string(coop_ui::player_at((float)x, (float)y)) + " | " + coop_ui::player_at_debug();
                           for (auto& [pid, p] : g_pick) {
                               char b[160];
                               snprintf(b, sizeof b, " | pick %u plate (%.0f %.0f %.0f %.0f) at (%.0f %.0f) reach %.0f age %.1f", pid,
                                        p.x1, p.y1, p.x2, p.y2, p.cx, p.cy, p.reach, H->time_seconds() - p.at);
                               r += b;
                           }
                       } else if (sub == "kick") {
                           const int id = who();
                           r = id >= 0 ? parties::kick((uint8_t)id) : "No such player: " + arg;
                       } else if (sub == "online") { // 6K: the galaxy's players (asked now; the list as it last came)
                           parties::ask_online();
                           for (auto& o : parties::online())
                               r += std::to_string(o.slot) + " '" + o.name + "' " + (o.planet.empty() ? "[menu]" : "[" + o.planet + "]") +
                                    " party " + std::to_string(o.party) + "\n";
                           if (r.empty()) r = "(asked the galaxy server: ask again)";
                       } else if (sub == "ginvite" || sub == "gkick") { // 6K: a galaxy player by name (the galaxy menu)
                           uint64_t id = 0;
                           for (auto& o : parties::online())
                               if (o.name == arg || utf8::shown_name(o.name) == arg) id = o.identity;
                           r = !id ? "Not online (coop.party online first): " + arg
                               : sub == "ginvite" ? parties::invite_identity(id) : parties::kick_identity(id);
                       } else if (sub == "yes" || sub == "no")
                           r = parties::answer(sub == "yes");
                       else if (sub == "leave")
                           r = parties::leave();
                       else
                           r = parties::status();
                       SAY("%s", r.empty() ? "sent" : r.c_str());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.whisper", "coop.whisper <player id> <text> - a line only that player sees (6J)",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       if (argc < 3) {
                           std::string s;
                           for (auto& l : g_whispers_in)
                               s += l + "\n";
                           SAY("%s", s.empty() ? "usage: coop.whisper <player id> <text>" : s.c_str());
                           return 0;
                       }
                       std::string text;
                       for (int i = 2; i < argc; i++)
                           text += (i > 2 ? " " : "") + std::string(argv[i]);
                       const std::string r = whisper((uint8_t)atoi(argv[1]), text);
                       SAY("%s", r.empty() ? "sent" : r.c_str());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.galaxy", "coop.galaxy connect <address> | list | enter <planet> | leave | save | every <s> | disconnect | status - the galaxy server (alpha, decisions 5O, 5Q)",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       std::string r;
                       const std::string sub = argc > 1 ? argv[1] : "status";
                       std::string arg;
                       for (int i = 2; i < argc; i++)
                           arg += (i > 2 ? " " : "") + std::string(argv[i]);
                       if (sub == "connect") r = arg.empty() ? "coop.galaxy connect <address>" : galaxy::connect(arg);
                       else if (sub == "list") r = galaxy::list();
                       else if (sub == "enter") r = galaxy::enter(arg);
                       else if (sub == "leave") r = galaxy::leave();
                       else if (sub == "save") r = galaxy::save();
                       else if (sub == "every") { galaxy::set_upload_every(atof(arg.c_str())); r = "upload every " + arg + " s"; }
                       else if (sub == "disconnect") r = galaxy::disconnect();
                       else r = galaxy::status();
                       H->out_printf(out, "%s\n", r.c_str());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.bans", "coop.bans - the host's ban list",
                   [](void*, int, const char* const*, SkOut* out) -> int {
                       H->out_printf(out, "%s\n%s\n", bans::list().c_str(), bans::status().c_str());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.sreport", "coop.sreport [status] | ask | answer yes|no | test none|big|overrun|badname|junk - the session report (F4, BUG-0767): the host asks every guest for its report",
                   sreport::command, nullptr, MT);
    H->command_add(M, "coop.settings", "coop.settings [list] | get <key> | set <key> on|off|default|<n> | force <key> <v> (tests) | stored - the session settings (the host decides; session ones before the start)",
                   settings::command, nullptr, MT);
    H->command_add(M, "coop.find", "coop.find [colors | color <name|0..7> | project x y z | labels] - F2: the arrows to the partners of the last frame, the player colours, pick a colour",
                   pfind::command, nullptr, MT);
    H->command_add(M, "coop.live", "coop.live - tests (BUG-0426): the object list of this frame (game::listed: walks, hits, destroys seen)",
                   [](void*, int, const char* const*, SkOut* out) {
                       SAY("%s\n", game::live_status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.framefix", "coop.framefix [0|1] - forget a torn-down world's puppets before the frame's messages (tests: 0 = the old order, BUG-0091)",
                   c_framefix, nullptr, MT);
    H->command_add(M, "coop.puppet", "coop.puppet hold on|off | respawn [s] | gamekill - tests: the remote players' puppets are not moved (a seeded failure for coop_mode.py, BUG-0251); respawn: removed now, shown again next frame or after s seconds (BUG-0453); gamekill: the game destroys the Creature puppets behind the mod's back (BUG-0507)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 2 && !strcmp(argv[1], "speedset")) { // tests (BUG-0788): SetDesiredSpeed on each Creature
                           for (auto& [id, r] : g_remotes) { // puppet now, and +0x1E0 read right after it (same frame)
                               if (!r.puppet || !creature_puppet(r.puppet_adapter) || !game::creature_listed(r.puppet))
                                   continue;
                               uint8_t* sp = (uint8_t*)r.puppet + game::kSpatialOff;
                               float before = *(float*)(sp + 0x1E0);
                               game::set_speed(r.puppet, (float)atof(argv[2]));
                               SAY("puppet %u desired %.2f -> %.2f (standard %.2f)\n", id, before, *(float*)(sp + 0x1E0),
                                   *(float*)(sp + 0x1E4));
                           }
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "speed")) { // tests (BUG-0788): the game's speeds of each Creature puppet
                           for (auto& [id, r] : g_remotes) {
                               if (!r.puppet || !creature_puppet(r.puppet_adapter) || !game::creature_listed(r.puppet))
                                   continue;
                               uint8_t* sp = (uint8_t*)r.puppet + game::kSpatialOff; // cLocomotiveObject
                               net::Vec3 v = game::velocity(r.puppet);
                               SAY("puppet %u desired=%.2f standard=%.2f vel=%.2f want=%.2f err=%.2f kinematic=%d\n", id,
                                   *(float*)(sp + 0x1E0), *(float*)(sp + 0x1E4), drive::speed(v), r.drv.last_want_speed,
                                   r.drv.last_err, (int)r.drv.kinematic);
                           }
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "hp")) { // tests (BUG-0697): the game's health of each ship puppet
                           for (auto& [id, r] : g_remotes)
                               SAY("puppet %u %s\n", id, modes::space_puppet_hp(r.puppet).c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "respawn")) { // tests (BUG-0453): every partner's puppet
                           int n = 0; // goes now (as on a death or a hide) and is shown again next frame or in <s> s
                           g_puppet_hide_until = argc > 2 ? H->time_seconds() + atof(argv[2]) : 0;
                           for (auto& [id, r] : g_remotes)
                               if (r.puppet) {
                                   despawn(r);
                                   n++;
                               }
                           SAY("%d puppet(s) removed\n", n);
                           return 0;
                       }
                       // tests (BUG-0795): gamekillfx <pool> <hex id> <key> [n]: the game destroys every partner's Creature
                       // puppet (as gamekill) and in the same call that partner's effect message arrives n times
                       if (argc > 4 && !strcmp(argv[1], "gamekillfx")) {
                           int n = argc > 5 ? std::max(1, atoi(argv[5])) : 1, k = 0;
                           for (auto& [id, r] : g_remotes)
                               if (r.puppet && creature_puppet(r.puppet_adapter) && game::creature_listed(r.puppet)) {
                                   net::Vec3 p = game::position(r.puppet);
                                   game::destroy_untracked(r.puppet);
                                   for (int i = 0; i < n; i++)
                                       effects::inject(id, id, 0, (uint8_t)atoi(argv[2]), (uint32_t)strtoul(argv[3], nullptr, 16),
                                                       (uint32_t)strtoul(argv[4], nullptr, 0), p, net::kNoAgl);
                                   SAY("puppet %u (%p) destroyed by the game, %d effect message(s) of it handled\n", id, r.puppet, n);
                                   k++;
                               }
                           SAY("%s", effects::status().c_str());
                           return k ? 0 : 1;
                       }
                       if (argc > 1 && !strcmp(argv[1], "gamekill")) { // tests (BUG-0507): the game destroys
                           int n = 0; // every partner's Creature puppet behind the mod's back (as it did on a
                           for (auto& [id, r] : g_remotes) // player's PC): the mod must notice and show it again
                               if (r.puppet && creature_puppet(r.puppet_adapter)) {
                                   game::destroy_untracked(r.puppet);
                                   n++;
                               }
                           SAY("%d puppet(s) destroyed by the game\n", n);
                           return 0;
                       }
                       if (argc > 1 && (!strcmp(argv[1], "agl") || !strcmp(argv[1], "sink"))) {
                           // tests (BUG-0683): agl: each Creature puppet's height over this game's terrain; sink <depth>
                           // [side]: each is put <side> m sideways and <depth> m under its terrain there (teleport), as
                           // the game's separation of creatures stacked on the nest pressed one in
                           bool sink = !strcmp(argv[1], "sink") && argc > 2;
                           float depth = sink ? (float)atof(argv[2]) : 0, side = sink && argc > 3 ? (float)atof(argv[3]) : 0;
                           for (auto& [id, r] : g_remotes) {
                               if (!r.puppet || !creature_puppet(r.puppet_adapter) || !game::creature_listed(r.puppet))
                                   continue;
                               net::Vec3 q = game::position(r.puppet);
                               float rr = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
                               if (sink && rr > 1.f) {
                                   // a direction across the planet's normal
                                   net::Vec3 n{q.x / rr, q.y / rr, q.z / rr}, ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
                                   net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
                                   float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
                                   net::Vec3 w{q.x + side * u.x / ul, q.y + side * u.y / ul, q.z + side * u.z / ul};
                                   float wl = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z), g = game::ground_radius(w);
                                   if (g > 0) {
                                       float k = (g - depth) / wl;
                                       game::teleport(r.puppet, {w.x * k, w.y * k, w.z * k}, game::orientation(r.puppet));
                                   }
                                   q = game::position(r.puppet);
                                   rr = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
                               }
                               float g = rr > 1.f ? game::ground_radius(q) : 0.f;
                               SAY("puppet %u agl=%.2f pos=(%.2f %.2f %.2f) ungrounded=%u\n", id, g > 0 ? rr - g : 0.f, q.x, q.y, q.z,
                                   r.drv.ungrounded);
                           }
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "hold"))
                           g_puppet_hold = !strcmp(argv[2], "on") || !strcmp(argv[2], "1");
                       SAY("puppet hold %s\n", g_puppet_hold ? "on" : "off");
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.stalltest", "coop.stalltest <s> - tests (BUG-0687): the main thread runs a message loop of its own for s seconds (max 20): no frames, the keepalive runs",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       double secs = std::min(argc > 1 ? atof(argv[1]) : 5.0, 20.0), t0 = H->time_seconds();
                       MSG m;
                       while (H->time_seconds() - t0 < secs) {
                           while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                               TranslateMessage(&m);
                               DispatchMessageW(&m);
                           }
                           Sleep(10);
                       }
                       SAY("stalled %.1f s\n", H->time_seconds() - t0);
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME); // (inside a frame the outside-frame tasks, the keepalive too, wait for its end)
    H->command_add(M, "coop.sessionfix", "coop.sessionfix [0|1] - one cleanup for every session end (tests: 0 = the old per-path lists, BUG-0033)",
                   c_sessionfix, nullptr, MT);
    H->command_add(M, "coop.lanfake", "coop.lanfake <n> [seconds] - n made-up games on the network answer (192.0.2.x, tests, BUG-0183)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       g_lan_fake_n = argc > 1 ? std::clamp(atoi(argv[1]), 0, 64) : 0;
                       g_lan_fake_until = net::now_ms() + (uint32_t)(argc > 2 ? atoi(argv[2]) : 60) * 1000u;
                       SAY("fake games %d, listed %zu, kept %zu\n", g_lan_fake_n, g_disc.games(net::now_ms()).size(),
                           g_disc.stored());
                       for (auto& g : g_disc.games(net::now_ms())) // the rows (BUG-0210 test)
                           SAY("  game %s %s id=%08X%s\n", g.info.name.c_str(), g.host.str().c_str(), g.info.host_id,
                               g.info.has_id ? "" : " (none)");
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
    H->command_add(M, "coop.founders", "show the two species registered on this saved planet", c_founders, nullptr, MT);
    H->command_add(M, "coop.rules", "the campaign rules in effect and their derived difficulty values", c_rules, nullptr, MT);
    H->command_add(M, "coop.effects",
                   "coop.effects [probe <hex key> | map | log on|off | load <n> [period_ms] [hex id]] - creature effects "
                   "relayed to the partners' puppets (BUG-0451); probe: the game's own effect pool of a key on the avatar "
                   "and every puppet; load: n effects per period at the local player (the frame-time measurement)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "log")) {
                           SAY("effects log %s\n", effects::set_log(argc > 2 && (!strcmp(argv[2], "on") || !strcmp(argv[2], "1"))) ? "on" : "off");
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "inject")) {
                           // tests: a crafted MSG_EFFECT as if peer `from` sent it (validation)
                           if (argc < 8) {
                               SAY("inject <from> <player> <stop> <pool> <id hex> <key> [x y z [agl]]");
                               return 1;
                           }
                           net::Vec3 p{};
                           if (argc > 10) {
                               p.x = (float)atof(argv[8]);
                               p.y = (float)atof(argv[9]);
                               p.z = (float)atof(argv[10]);
                           }
                           effects::inject((uint8_t)strtoul(argv[2], nullptr, 0), (uint8_t)strtoul(argv[3], nullptr, 0),
                                           (uint8_t)strtoul(argv[4], nullptr, 0), (uint8_t)strtoul(argv[5], nullptr, 0),
                                           (uint32_t)strtoul(argv[6], nullptr, 16), (uint32_t)strtoul(argv[7], nullptr, 0), p,
                                           argc > 11 ? (float)atof(argv[11]) : net::kNoAgl); // BUG-0608: agl
                           SAY("%s", effects::status().c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "load")) {
                           uint32_t n = argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 10) : 0;
                           uint32_t ms = argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 10) : 1000;
                           uint32_t id = argc > 4 ? (uint32_t)strtoul(argv[4], nullptr, 16) : 0;
                           effects::set_load(n, ms, id);
                           SAY("%s", effects::status().c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "watch")) { // tests (BUG-0465): watch <hex id> | watch
                           SAY("%s", effects::watch(argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0).c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "probe")) {
                           SAY("%s", effects::probe((uint32_t)strtoul(argv[2], nullptr, 16)).c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "map")) {
                           SAY("%s", effects::map_dump().c_str());
                           return 0;
                       }
                       SAY("%s", effects::status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.track", "coop.track start|stop|count|dump [from] [n]|lift <m>|novel 0|1 - every frame: the partners' puppets and the own avatar; in Tribe the player tribe's members (tests, BUG-0360, BUG-0365, BUG-0366)",
                   c_track, nullptr, MT);
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
    H->command_add(M, "coop.players", "coop.players [held <id>] - the per-player state modules keep (BUG-0422)", c_players, nullptr, MT);
    H->command_add(M, "coop.guard", "coop.guard [unwind <name> [n] | stick <name>] - the call guards of the modules and what the frame reset cleared (BUG-1047)", c_guard, nullptr, MT);
    H->command_add(M, "coop.lifecycle", "coop.lifecycle [held] - the session / world state modules keep and what the last resets left (BUG-1055)", c_lifecycle, nullptr, MT);
    H->command_add(M, "coop.social", "coop.social [species | log | act <npc id> [slot] | goto <npc id>] - creature social games", c_social, nullptr, MT);
    H->command_add(M, "coop.fields", "coop.fields - replicated state fields of mirrored objects per kind (BUG-0419 G2, fields.h)",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       // tests / RE: `coop.fields scale <hex object> [value]` reads (and sets) an object's scale
                       if (argc > 2 && !strcmp(argv[1], "scale")) {
                           void* o = (void*)(uintptr_t)strtoul(argv[2], nullptr, 16);
                           if (!o || IsBadReadPtr(o, 0x40) || !game::object_alive(o)) {
                               SAY("no object %s\n", argv[2]);
                               return 0;
                           }
                           if (argc > 3)
                               game::set_object_scale(o, (float)atof(argv[3]));
                           SAY("scale %p %.3f\n", o, game::object_scale(o));
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "list")) {
                           std::string l = fields::list(argv[2]);
                           for (size_t at = 0; at < l.size(); at += 6000) // out_printf keeps 8 KB per call
                               SAY("%s", l.substr(at, 6000).c_str());
                           return 0;
                       }
                       SAY("%s", fields::status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.npc", "coop.npc [on|off|list|herds|hit <id> <dmg>|hosthit <id> <dmg> [push]|recruit <id>|spawn <hex>|cinedefer on|off] - host-authoritative NPC sync", c_npc, nullptr, MT);
    H->command_add(M, "coop.res", "coop.res [[joinsim] add <food|money> <n> | fix reset|floor [0|1]] - shared resources", c_res, nullptr, MT);
    H->command_add(M, "coop.ping", "coop.ping - show everybody the place under your mouse / where you are", c_ping,
                   nullptr, MT);
    H->command_add(M, "coop.transfer", "coop.transfer [normal|paced] - world transfer pacing for this run", c_transfer, nullptr, MT);
    H->command_add(M, "coop.share", "coop.share [player id|due] - (re)send the host world (save) to guests (due: tests, as after an evolution)", c_share,
                   nullptr, MT);
    H->command_add(M, "coop.perf", "coop.perf readable [extra <n>] - tests (BUG-0791): readable() calls per frame since the last ask, their cost (us per call); extra: n more VirtualQuery per call",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       static LONG s_calls;
                       static uint64_t s_frame;
                       static double s_t;
                       if (argc > 3 && !strcmp(argv[2], "extra"))
                           skg::readable_extra = atoi(argv[3]);
                       LONG c = skg::readable_calls;
                       uint64_t f = H->frame_index();
                       double t = H->time_seconds();
                       double frames = (double)(f - s_frame), secs = t - s_t;
                       // the cost of one call on a heap block and a stack address (the main thread's own pointers)
                       static int s_heap[16];
                       LARGE_INTEGER q0, q1, qf;
                       QueryPerformanceFrequency(&qf);
                       QueryPerformanceCounter(&q0);
                       int ok = 0;
                       for (int i = 0; i < 2000; i++)
                           ok += skg::readable(&s_heap[i & 15], 4);
                       QueryPerformanceCounter(&q1);
                       double us = (double)(q1.QuadPart - q0.QuadPart) * 1e6 / (double)qf.QuadPart / 2000.0;
                       SAY("readable calls=%ld since=%ld frames=%.0f secs=%.1f per_frame=%.1f fps=%.1f us_per_call=%.3f extra=%d ok=%d\n",
                           (long)c, (long)(c - s_calls), frames, secs, frames > 0 ? (c - s_calls) / frames : 0.0,
                           secs > 0 ? frames / secs : 0.0, us, skg::readable_extra, ok);
                       s_calls = skg::readable_calls; // (the 2000 calls of the benchmark not counted for the next ask)
                       s_frame = f;
                       s_t = t;
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.adventure", "coop.adventure [act <n> | place <hex> | kill <class> | hit <class> [damage] | actors [radius m]] - tests (BUG-0845): the adventure's play mode (state, act, resource key, place id); act: the game's JumpToAct; place: one place id for every adventure (0 off); Lineages: the shared mission's counters and the rescue status (BUG-9089, BUG-9114)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 2 && !strcmp(argv[1], "kill")) { // tests: a kill in this game only (BUG-9089)
                           H->out_printf(out, "%s", adventure::debug_kill((uint32_t)strtoul(argv[2], nullptr, 10)).c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "hit")) { // tests: this captain's hit (a guest's goes to the host)
                           H->out_printf(out, "%s", adventure::debug_hit((uint32_t)strtoul(argv[2], nullptr, 10),
                                                                        argc > 3 ? (float)atof(argv[3]) : 10.f).c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "actors")) { // tests: scenario creatures near this captain (BUG-9114)
                           H->out_printf(out, "%s", adventure::actors(argc > 2 ? (float)atof(argv[2]) : 120.f,
                                                                      argc > 3 ? atoi(argv[3]) : -1).c_str());
                           return 0;
                       }
                       static uintptr_t smp = H->sym("App::cScenarioMode::ptr"), jump = H->sym("SporeCoop::ScenarioPlay_JumpToAct");
                       void* sm = smp && game::mode_id() == modes::kScenario ? *(void**)smp : nullptr;
                       void* pm = sm && skg::readable(sm, 0x7C) ? skg::field<void*>(sm, 0x78) : nullptr; // cScenarioPlayMode
                       if (argc > 2 && !strcmp(argv[1], "place") && !pm) { // (also before the adventure starts)
                           modes::adventure_force_place((uint32_t)strtoul(argv[2], nullptr, 16));
                           SAY("adventure place forced to %s\n", argv[2]);
                           return 0;
                       }
                       if (!pm || !skg::readable(pm, 0xBC)) {
                           SAY("no adventure\n%s\n%s", adventure::status().c_str(), creature_rescue::status().c_str());
                           return argc > 1 ? 1 : 0; // (the bare command: the Lineages status, also outside an adventure)
                       }
                       uint32_t ki = 0, kg = 0;
                       modes::adventure_key(ki, kg);
                       SAY("adventure playmode=%p state=%d act=%d key=%08X/%08X place=%08X\n", pm, skg::field<int>(pm, 0x90),
                           skg::field<int>(pm, 0xB8), ki, kg, modes::current_place(modes::for_mode(modes::kScenario)));
                       SAY("%s\n%s\n", adventure::status().c_str(), creature_rescue::status().c_str()); // Lineages
                       if (argc > 2 && !strcmp(argv[1], "place")) { // tests: one place id for every adventure (0: off)
                           modes::adventure_force_place((uint32_t)strtoul(argv[2], nullptr, 16));
                           SAY("adventure place forced to %s\n", argv[2]);
                       }
                       if (argc > 2 && !strcmp(argv[1], "act") && jump) {
                           static int s_act;
                           s_act = atoi(argv[2]);
                           // (the act's change rebuilds the cast, the goals' UI and the camera: outside the frame)
                           H->run_outside_frame(
                               [](void*) {
                                   void* sm2 = game::mode_id() == modes::kScenario ? *(void**)smp : nullptr;
                                   void* pm2 = sm2 ? skg::field<void*>(sm2, 0x78) : nullptr;
                                   if (pm2)
                                       skg::thiscall<void>(jump, pm2, s_act);
                               },
                               nullptr);
                           SAY("jump to act %d queued", s_act);
                       }
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.awaytest", "coop.awaytest 1|0 - tests (BUG-1084, a guest): the host counts as in an editor (away after 4 s) until 0",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1)
                           g_away_test = atoi(argv[1]) != 0;
                       SAY("awaytest=%d host_away=%d", (int)g_away_test, (int)g_host_away);
                       return 0;
                   },
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
    H->command_add(M, "coop.partnerpass", "coop.partnerpass - tests (BUG-0913): the partners' creatures we walk and click through",
                   [](void*, int, const char* const*, SkOut* out) {
                       int n = 0;
                       for (void* p : g_pass_puppets)
                           n += p != nullptr;
                       SAY("fix=%d on=%d puppets=%d pairs_skipped=%u own_pairs_seen=%u hover_skipped=%u ff=%d "
                           "phantom_pairs=%u/%u/%u",
                           (int)g_partner_pass_fix, (int)g_pass_on, n, g_pass_pairs, g_pass_seen_own, g_pass_hover_skipped,
                           (int)settings::on(settings::kFriendlyFire), g_pass_phantom_pairs, g_pass_phantom_own,
                           g_pass_phantom_partner);
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
    tc.my_name = [] { return utf8::shown_name(g_name); }; // BUG-9222: shown without the -NoShare- tag
    teamplay::init(H, M, std::move(tc));
    creature_rescue::init(H, &g_sess, notice);
    evolve::init(H, M); // BUG-9160
    H->command_add(M, "coop.evolve", "coop.evolve [mates | anim <hex> [hold] | release | fx <name>.. | fxset <scale> <up> <rows>] - personal evolving and the cocoon (BUG-9160; the rest: tests)",
                   [](void*, int argc, const char* const* argv, SkOut* o) {
                       std::string out;
                       if (argc > 1 && !strcmp(argv[1], "mates")) { // tests: the own herd and its mates on screen
                           out = mates_report();
                           H->out_printf(o, "%s", out.c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "migrate")) { // tests (BUG-9163): as if this game's evolution started a migration
                           if (g_sess.role() == net::Role::Host) send_migrate(g_sess.my_id(), -1, 0xFF);
                           else send_migrate(g_sess.my_id(), 0, 0xFF);
                           char b[160];
                           snprintf(b, sizeof b, "migration announced; this game's state %d, started %u heard %u editors %u\n",
                                    host_migration(), g_mig_started, g_mig_heard, g_mig_editors);
                           H->out_printf(o, "%s", b);
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "migration")) { // tests (BUG-9163)
                           char b[280];
                           snprintf(b, sizeof b, "migration: state %d (last threshold %d, bar %.0f), started %u heard %u ignored %u editors %u, round %.0f s, editor in %.0f s (by %s)\n",
                                    host_migration(), player_property(0x54FB6BFu), game::evolution_bar(), g_mig_started, g_mig_heard,
                                    g_mig_ignored, g_mig_editors, g_mig_round_until < 0 ? -1.0 : g_mig_round_until - H->time_seconds(),
                                    g_mig_editor_at < 0 ? -1.0 : g_mig_editor_at - H->time_seconds(), g_mig_by.c_str());
                           H->out_printf(o, "%s", b);
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "partners")) { // tests: each partner's puppet, cocoon and screen spot
                           for (auto& [id, r] : g_remotes) {
                               char b[260];
                               net::Vec3 p = r.puppet ? game::position(r.puppet) : net::Vec3{};
                               float w[3] = {p.x, p.y, p.z}, sc[2] = {-1, -1};
                               const bool on = r.puppet && H->world_to_screen(w, sc);
                               uint32_t anim = 0;
                               if (r.puppet) game::drawn_state(r.puppet, nullptr, &anim);
                               snprintf(b, sizeof b, "%u '%s' puppet %p radius %.2f mode %08X cocoon %d fx %d anim %08X held %d at (%.1f %.1f %.1f) screen %s%.0f %.0f\n",
                                        id, r.name.c_str(), r.puppet, r.puppet ? game::bounding_radius(r.puppet) : 0.f,
                                        r.buf.empty() ? 0u : r.buf.latest().mode, r.cocoon.state,
                                        r.cocoon.fx && game::effect_active(r.cocoon.fx) ? 1 : 0, anim,
                                        r.puppet ? (int)game::pose_held(r.puppet) : 0, p.x, p.y, p.z, on ? "" : "(off) ", sc[0], sc[1]);
                               out += b;
                           }
                           H->out_printf(o, "%s", out.empty() ? "no partners\n" : out.c_str());
                           return 0;
                       }
                       int r = evolve::command(argc, argv, out);
                       H->out_printf(o, "%s", out.c_str());
                       return r;
                   }, nullptr, SK_CMD_MAIN_THREAD);
    progress_debt::init(H, &g_sess, notice); // BUG-9119
    { // decision 4S (BUG-9239): the memory box and the restart
        mem_refresh::Api api{};
        api.role = [] { return g_sess.role() == net::Role::Host ? 1 : g_sess.role() == net::Role::Client ? 2 : 0; };
        api.connected = [] { return g_sess.role() != net::Role::None && g_sess.connected(); };
        api.host_address = [] { return g_sess.host_address(); };
        api.password = [] { return g_sess.password(); };
        api.port = [] { return g_sess.port(); };
        api.seats = [] { return (unsigned)g_sess.seat_limit; };
        api.name = [] { return g_name; };
        api.token = [] { return g_sess.rejoin_token; };
        api.join = [](const std::string& addr, const std::string& name, const std::string& pw, uint64_t token,
                      std::string& err) {
            if (!name.empty()) g_name = utf8::player_name(name);
            return do_join(addr, err, pw, token);
        };
        api.host = [](uint16_t port, unsigned seats, const std::string& name, const std::string& pw, std::string& err) {
            if (!name.empty()) g_name = utf8::player_name(name);
            return do_host(port ? port : default_port(), seats >= 1 && seats <= founders::kSeatCeiling ? seats : 2, 100, err, pw);
        };
        api.leave = [] {
            if (g_sess.role() == net::Role::Client) do_leave();
            else if (g_sess.role() == net::Role::Host) end_open_session();
        };
        api.announce = [](uint16_t secs) {
            net::Writer w;
            w.u16(secs);
            g_sess.broadcast_reliable(net::MSG_HOST_RESTART, w);
            chat_log("* the host restarts Spore for its memory: the partners rejoin by themselves");
        };
        api.feed = [](const std::string& s) { feed(s); };
        mem_refresh::init(H, M, api);
    }
    H->command_add(M, "coop.debt", "coop.debt [add <n> | clear] - the party's progress debt (BUG-9119): what is owed, the bar; add / clear: tests (host)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string text;
                       int rc = progress_debt::command(argc, argv, text);
                       H->out_printf(out, "%s", text.c_str());
                       return rc;
                   }, nullptr, SK_CMD_MAIN_THREAD);
    adventure::init(H, M, &g_sess);
    H->command_add(M, "coop.homes", "coop.homes [make <seat>] - every player's own nest (roadmap phase 2, BUG-9135)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", home_nests::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.identity", "coop.identity - this profile's persistent player identity and the ones the host knows (BUG-9135)",
                   [](void*, int, const char* const*, SkOut* out) {
                       SAY("%s%s", identity::status().c_str(), party::status().c_str());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.giveup", "coop.giveup [down] - a downed player gives up now, as holding G does; down: a lethal hit on the own creature through the game's damage path (BUG-9118, tests)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "down")) {
                           void* av = game::avatar();
                           if (!av) { H->out_printf(out, "no creature\n"); return 1; }
                           game::hit(av, game::health(av) + 50.f, 0, nullptr);
                           H->out_printf(out, "hit: hp now %.1f, down=%d\n", game::health(av), (int)creature_rescue::down());
                           return 0;
                       }
                       creature_rescue::give_up_now();
                       const bool cell = cellsync::give_up();
                       tribe::give_up_now();
                       spacecoop::give_up_now();
                       H->out_printf(out, "give up sent (creature down=%d cell down=%d)\n", (int)creature_rescue::down(), (int)cell);
                       return 0;
                   },
                   nullptr, MT);
    adventure::set_body_lookup([](uint8_t player) -> void* { // BUG-9114: a partner's body in this game
        auto it = g_remotes.find(player);
        return it != g_remotes.end() && it->second.puppet && game::object_alive(it->second.puppet) ? it->second.puppet : nullptr;
    });
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
    if (uintptr_t a = H->sym("SporeCoop::CreatureGame_HandleMessage")) // BUG-0466
        H->hook_create(M, (void*)a, (void*)h_CreatureGameMsg, (void**)&o_CreatureGameMsg);
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
    H->command_add(M, "coop.newcreature", "coop.newcreature - tests (BUG-9207): the creature editor with the base creature, "
                   "here (from the galaxy menu: as a new world's first editor)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::vector<StartPart> parts; // (lo:state, the part type 0x40626000)
                       for (int i = 2; i < argc; i++) {
                           const char* c = strchr(argv[i], ':');
                           parts.push_back({(uint32_t)strtoul(argv[i], nullptr, 16), 0x40626000u,
                                            (uint8_t)(c ? strtoul(c + 1, nullptr, 16) : 5)});
                       }
                       const bool r = submit_new_creature_request(argc > 1 ? (uint32_t)strtoul(argv[1], nullptr, 16) : 0x465C50BAu,
                                                                  argc > 2 ? &parts : nullptr);
                       SAY("new creature editor -> %d\n", (int)r);
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    if (H->instance_id() > 1)
        H->command_add(M, "coop.autosave-test", "Queue the normal milestone autosave in a QA session",
                       [](void*, int, const char* const*, SkOut*) {
                           if (g_sess.role() != net::Role::Host) return 1;
                           world::request_autosave();
                           return 0;
                       }, nullptr, MT);
    H->command_add(M, "coop.sporepedia", "coop.sporepedia <player> [off=val ...] - tests (BUG-9191): the Sporepedia on that player's creature, request bytes set",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc < 2) return 1;
                       g_sporepedia_bytes.clear();
                       for (int i = 2; i < argc; ++i) {
                           unsigned off = 0, v = 0;
                           if (sscanf(argv[i], "%x=%x", &off, &v) == 2) g_sporepedia_bytes.push_back({off, (uint8_t)v});
                       }
                       open_sporepedia_of((uint8_t)atoi(argv[1]));
                       H->out_printf(out, "asked (%zu bytes set)", g_sporepedia_bytes.size());
                       return 0;
                   }, nullptr, 0);
    H->command_add(M, "coop.editor", "coop.editor - tests (BUG-0368): the open editor's request and the parts its palette shows locked",
                   [](void*, int, const char* const*, SkOut* out) {
                       // the editor game mode: the mode manager's list (+0x14..+0x18, entries of 0x18 bytes
                       // {mode, id, name}) holds it under id 0xDBDBA1 (0x57F3E0); its request at +0x1CC
                       // (OnEnter reads +0x94 there and gives it to the palette)
                       void* mm = skg::cdecl_call<void*>(H->sym("App::IGameModeManager::Get"));
                       void* ed = nullptr;
                       if (mm && skg::readable((char*)mm + 0x14, 8)) {
                           auto* b = skg::field<uint32_t*>(mm, 0x14);
                           auto* e = skg::field<uint32_t*>(mm, 0x18);
                           for (uint32_t* it = b; it && it < e && e - b < 6 * 64 && skg::readable(it, 8); it += 6)
                               if (it[1] == 0xDBDBA1u)
                                   ed = (void*)(uintptr_t)it[0];
                       }
                       void* req = ed && skg::readable((char*)ed + 0x1CC, 4) ? skg::field<void*>(ed, 0x1CC) : nullptr;
                       if (!req || !skg::readable(req, 0x9C)) {
                           SAY("no editor request (mode %p)\n", ed);
                           return 1;
                       }
                       void* items = skg::field<void*>(req, 0x94);
                       void* mine = collected_parts();
                       // the palette's rule (0x5C6820): available = no items object, or the item unlocked
                       // (IsUnlocked); counted over the unlockable items of the collected parts (parts::census:
                       // the map at +0x1490 and, BUG-0441, the item states)
                       int unlockable = 0, locked = 0, avail = 0;
                       uint32_t avail_hash = 0;
                       if (items && parts::census(avail, avail_hash, unlockable, nullptr, nullptr, items))
                           locked = unlockable - avail;
                       else if (!items && mine && parts::census(avail, avail_hash, unlockable, nullptr, nullptr, mine)) {
                           avail = unlockable; // no items: every part offered
                           avail_hash = 0;
                       }
                       SAY("editor %08X key %08X request %p items %p collected %p unlockable %d locked %d (own editor items set %u) "
                           "available=%d:%08X\n",
                           skg::field<uint32_t>(req, 0xC), skg::field<uint32_t>(req, 0x10), req, items, mine, unlockable,
                           locked, g_own_editor_items_set, avail, avail_hash);
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.parts", "coop.parts [unlock <n>|find <level>|keys|states|cell [collect <i>]] - the collected creature parts as the trace's parts= field counts them; "
                   "unlock <n>: unlock n locked ones in this game only (tests: a player's unlock the other games miss); "
                   "find <level>: the game's own 'a part was found' (a skeleton's reward, BUG-0401); keys: the unlocked unlockable keys; states: every item state (BUG-0441)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "cell")) { // BUG-0467: cell [collect <i>]
                           std::vector<std::string> a;
                           for (int i = 2; i < argc; i++)
                               a.push_back(argv[i]);
                           SAY("%s", parts::cell_debug(a).c_str());
                           return 0;
                       }
                       std::vector<std::pair<uint32_t, uint32_t>> locked;
                       int n = 0, all = 0, done = 0;
                       uint32_t h = 0;
                       if (!parts_census(n, h, all, &locked)) {
                           SAY("no collected parts (no cPlayer)\n");
                           return 1;
                       }
                       if (argc > 1 && (!strcmp(argv[1], "keys") || !strcmp(argv[1], "states"))) {
                           SAY("%s", parts::dump(argv[1]).c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "unlock")) {
                           // cCollectableItems::UnlockPart(key, cost 0), as when the player finds the part
                           uintptr_t unlock = H->sym("Simulator::cCollectableItems::UnlockPart");
                           void* items = collected_parts();
                           for (int i = 0; unlock && items && i < atoi(argv[2]) && i < (int)locked.size(); i++)
                               done += skg::thiscall<bool>(unlock, items, locked[i].first, locked[i].second, 0) ? 1 : 0;
                           parts_census(n, h, all);
                       } else if (argc > 2 && !strcmp(argv[1], "find")) {
                           int before = n, after = parts::find_part(atoi(argv[2]));
                           done = after >= 0 ? after - before : 0;
                           parts_census(n, h, all);
                       }
                       SAY("parts=%d:%08X unlockable=%d unlocked_now=%d\n", n, h, all, done);
                       SAY("sync: %s\n", parts::status().c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME); // find shows the game's goal card
    H->command_add(M, "coop.migratetest", "coop.migratetest - tests (BUG-9254): tell the others this game's species migrates (MSG_MIGRATE, as its evolution does), whatever its party",
                   [](void*, int, const char* const*, SkOut* out) {
                       if (!g_sess.connected()) { SAY("not in a session\n"); return 1; }
                       g_mig_by = g_name;
                       g_mig_round_until = H->time_seconds() + kMigrateRound; // (this game's own round)
                       send_migrate(g_sess.my_id(), g_sess.role() == net::Role::Host ? -1 : 0, 0xFF);
                       SAY("MSG_MIGRATE sent as %s (galaxy parties %d, my party %zu)\n", g_name.c_str(),
                           (int)parties::galaxy(), parties::my_party().size());
                       return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.editreq", "coop.editreq diff - tests (BUG-0720): the creature stage's own evolution request (its handler run once, nothing submitted) against the ones the mod builds",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       void* av = game::avatar();
                       uintptr_t flag = H->sym("SporeCoop::CreatureEvolutionPending_flag");
                       if (argc > 2 && !strcmp(argv[1], "keep")) { // exploration: which own-editor bytes give its cross
                           g_own_keep_ctor = (unsigned)strtoul(argv[2], nullptr, 0);
                           SAY("own editor keeps the constructor's bytes: mask %u\n", g_own_keep_ctor);
                           return 0;
                       }
                       if (argc < 2 || strcmp(argv[1], "diff") || game::mode_id() != modes::kCreature || !av || !g_creature_game ||
                           !o_CreatureGameMsg || !flag) {
                           SAY("not possible here (creature=%d creature_game=%p)\n", (int)(game::mode_id() == modes::kCreature), g_creature_game);
                           return 1;
                       }
                       // the game's builder: message 0x4471C60 with +0xDA set (the avatar met its mate), Submit captured
                       uint8_t& da = skg::field<uint8_t>(g_creature_game, 0xDA);
                       uint8_t da0 = da, f0 = *(uint8_t*)flag;
                       da = 1;
                       g_editreq_game = {};
                       g_editreq_capture = true;
                       o_CreatureGameMsg(g_creature_game, 0x4471C60u, nullptr);
                       g_editreq_capture = false;
                       da = da0;
                       *(uint8_t*)flag = f0; // its "evolution pending" byte back (nothing was submitted)
                       if (!g_editreq_game.have) {
                           SAY("the game built no request\n");
                           return 1;
                       }
                       // fields that differ by nature: vtables +0/+4, refcount +8, the strings' pointers +0x40..+0x5C,
                       // the vector's pointers and allocator +0x70..+0x83 (its contents are compared), and the bytes
                       // the constructor never writes (padding after its byte fields: +0x21..+0x23, +0x3E/+0x3F, +0x67;
                       // the allocator's leftovers, seen different in the lab 20261108-035619)
                       auto skip = [](int o) {
                           return o < 0xC || (o >= 0x21 && o <= 0x23) || o == 0x3E || o == 0x3F || (o >= 0x40 && o < 0x60) || o == 0x67 ||
                                  (o >= 0x70 && o < 0x84);
                       };
                       for (EditKind kind : {EditKind::Own, EditKind::Evolve}) {
                           void* req = build_editor_request(kind, 0, game::species_key(av));
                           ReqImage ours = req_image(req);
                           if (req) // the caller's reference: freed
                               skg::thiscall<void>(skg::field<uintptr_t>(skg::field<void*>(req, 0), 8), req);
                           std::string diff, allowed;
                           char b[64];
                           for (int o = 0; o < 0x9C && ours.have; o++) {
                               if (skip(o) || ours.bytes[o] == g_editreq_game.bytes[o])
                                   continue;
                               int field = o & ~3;
                               // the own editor has no evolution tag (BUG-0466) and keeps the constructor's +0x39 / +0x3B / +0x3C
                               // (its red cross and save dialog, BUG-0756)
                               bool allow = kind == EditKind::Own && (field == 0x90 || o == 0x39 || o == 0x3B || o == 0x3C);
                               snprintf(b, sizeof b, " +0x%02X(%02X/%02X)", o, ours.bytes[o], g_editreq_game.bytes[o]);
                               (allow ? allowed : diff) += b;
                           }
                           if (ours.vec70 != g_editreq_game.vec70) {
                               snprintf(b, sizeof b, " vec70(%u/%u)", (unsigned)ours.vec70.size(), (unsigned)g_editreq_game.vec70.size());
                               diff += b;
                           }
                           SAY("editreq %s: diff=%s allowed=%s (vec70 %u, items %08X/%08X)\n", kind == EditKind::Own ? "own" : "evolve",
                               diff.empty() ? " none" : diff.c_str(), allowed.empty() ? " none" : allowed.c_str(),
                               (unsigned)g_editreq_game.vec70.size(), *(uint32_t*)(ours.bytes + 0x94), *(uint32_t*)(g_editreq_game.bytes + 0x94));
                       }
                       SAY("editreq fix=%d; the game's: post 0x5D02A72 and byte 0x1686AF0 (allowed: the evolution flow's, not the own editor's)\n",
                           (int)g_editor_request_fix);
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
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
                           void* req = build_editor_request(EditKind::Edit, argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0,
                                                            game::species_key(av)); // BUG-0720
                           bool r = req && skg::cdecl_call<bool>(submit, req);
                           SAY("editor request submitted -> %d\n", (int)r);
                       } else if (argc > 1 && (!strcmp(argv[1], "evolve") || !strcmp(argv[1], "stageend")) &&
                                  game::mode_id() == modes::kCreature) {
                           // tests (BUG-0457): the creature stage's own evolution request, as its message handler
                           // 0xD42E50 builds it when the avatar meets its mate (docs/re/creations.md#creations-evolution-request):
                           // +0x90 the evolution tag (the editor's result is applied to the species), +0x94 the
                           // collected parts, the flags, +0x70 cPlayer +0x10FC..+0x1108; through the vote (the hook)
                           bool end = !strcmp(argv[1], "stageend");
                           if (!end) { // Lineages: our evolution request (the one BUG-9163 / 9164 / 9175 submit)
                               int r = submit_evolution_request();
                               if (r < 0) {
                                   SAY("not possible here");
                                   return 1;
                               }
                               SAY("evolution request submitted -> %d\n", (int)r);
                               return 0;
                           }
                           // BUG-1085: `stageend` carries the stage-end editor's tag (0xD42E50 on 0x4471C60, Creature -> Tribe)
                           uintptr_t submit = H->sym("Editors::EditorRequest::Submit");
                           void* av = game::avatar();
                           void* req = submit && av ? build_editor_request(EditKind::Evolve, 0, game::species_key(av)) : nullptr;
                           if (!req) {
                               SAY("not possible here");
                               return 1;
                           }
                           skg::field<uint32_t>(req, 0x90) = kTagStageEnd;
                           bool r = skg::cdecl_call<bool>(submit, req);
                           if (uintptr_t f = H->sym("SporeCoop::CreatureEvolutionPending_flag")) // as 0xD43069
                               *(uint8_t*)f = 1;
                           SAY("stage-end request submitted -> %d\n", (int)r);
                       } else if (argc > 1 && !strcmp(argv[1], "mate") && game::mode_id() == modes::kCreature) {
                           // tests (BUG-0920): the game's own evolution request, not one built by the mod: its message
                           // handler 0xD42E50 run with message 0x4471C60 and +0xDA set (the avatar met its mate, as
                           // coop.editreq diff does), the request goes through the Submit hook like a player's mating
                           // call; the game then posts 0x5D02A72 and sets its editor-pending flag itself. BUG-1085: this
                           // builder is the stage-end editor's (tag 0x64AA168, Creature -> Tribe), not the mating call's
                           if (!game::avatar() || !g_creature_game || !o_CreatureGameMsg) {
                               SAY("not possible here");
                               return 1;
                           }
                           uint8_t& da = skg::field<uint8_t>(g_creature_game, 0xDA);
                           uint8_t da0 = da;
                           da = 1;
                           o_CreatureGameMsg(g_creature_game, 0x4471C60u, nullptr);
                           da = da0;
                           SAY("the game's own evolution request ran (editor-pending flag %d)\n", evo_flag_now());
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
                           "actions run %u, votes dropped %u\neditor held for a cinematic %u, cinematics ended %u, "
                           "editor held now %d, actions waited %u, action due %d\n"
                           "editor under the game's own pause %u (own busy %s, editor pause count %d)\n"
                           "votes of the host not taken elsewhere %u\n"
                           "switch votes %u (host clicks held %u, guest clicks held %u, arrivals ready %u)\n"
                           "evolve offers sent %u got %u taken %u expired %u, offer now %d\n"
                           "editor-pending flag %d, cleared %u (BUG-0920)\n"
                           "stage-end votes %u, guest stage-end requests refused %u (BUG-1085)",
                           g_vote.active ? "active" : "none", g_vote.text.c_str(), g_vote.left, (int)g_own_editor,
                           g_part_list_waivers, g_submits_held, g_quits_delayed, g_submits_repeated,
                           g_own_editor_due ? 1 : 0, g_own_editor_waits, g_vote_actions_run, g_votes_dropped,
                           g_submits_after_cine, g_submit_cines_ended, g_submit_later ? 1 : 0, g_vote_actions_waited,
                           g_vote_action_due ? 1 : 0, g_submits_own_pause, busy::names(g_req_own_busy).c_str(),
                           editor_pause_count(), g_vote_elsewhere, g_switch_votes, g_switch_held, g_switch_guest_held,
                           g_switch_arrivals, g_offers_sent, g_offers_got, g_offers_taken, g_offers_expired,
                           g_offer_editor ? 1 : 0, evo_flag_now(), g_evo_flags_cleared, g_stage_end_votes,
                           g_stage_end_guest_refused);
                       if (g_submit_later)
                           SAY("\nheld native editor=%08X model=%08X return=%08X context=%08X busy=%s",
                               skg::field<uint32_t>(g_submit_later, 0xC), skg::field<uint32_t>(g_submit_later, 0x10),
                               skg::field<uint32_t>(g_submit_later, 0x1C), skg::field<uint32_t>(g_submit_later, 0x90),
                               busy::names(game_busy()).c_str());
                       return 0;
                   },
                   // outside the frame: `editor` submits an editor request, which the game does
                   // from its update, never inside Present (BUG-0096)
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    tribe::Ctx trc;
    trc.sess = &g_sess;
    trc.feed = feed;
    trc.notice = [](const std::string& t) { notice(t, -1); };
    trc.host_away = [] { return host_absent_for(Absent::Tribe); }; // BUG-0151, BUG-0231
    trc.here = [](uint8_t player) { // BUG-9135 phase 4
        auto it = g_remotes.find(player);
        return it != g_remotes.end() && !it->second.buf.empty() && same_place(it->second.buf.latest());
    };
    trc.open_creature_editor = [] { // BUG-9159: the editor first for a late joiner without a creature
        H->run_outside_frame([](void*) {
            if (!late_create_offered()) return;
            const std::string said = open_new_creature_editor();
            feed(said);
        }, nullptr);
    };
    tribe::init(H, M, std::move(trc));
    drama::init(H, M, &g_sess); // BUG-0486
    identity::init(H, M, &g_sess); // BUG-9135
    { // BUG-9251 (decision 5I): kick and ban
        bans::Ctx bc;
        bc.sess = &g_sess;
        bc.feed = [](const std::string& t) { feed(t); };
        bc.notice = [](const std::string& t) { notice(t, -1); };
        bans::init(H, M, bc);
    }
    { // decision 5O A (alpha): the galaxy server: one address, planets hosted by players, their traffic relayed
        galaxy::Ctx gx;
        gx.sess = &g_sess;
        gx.feed = [](const std::string& t) { feed(t); };
        gx.name = [] { return g_name; };
        gx.identity = [] { return identity::own(); };
        gx.key = [] { return identity::galaxy_key(); }; // 6S
        gx.version = COOP_VERSION;
        // 6E B: a planet takes the galaxy's player count (16 by default; difficulty stays one party's, founders.h)
        gx.host = [](std::string& err) { return do_host(default_port(), galaxy::planet_players(), 100, err, {}, galaxy::planet_players()); };
        gx.join = [](const std::string& addr, std::string& err) { return do_join(addr, err); };
        gx.ready = [] { return g_local != nullptr && !world::load_queued() && !world::load_starting(); };
        gx.promote = [] { npc::promote_now(); };
        gx.party_in = [](uint8_t type, net::Reader& r) { // 6K, 6L A
            if (type == net::galaxy::WHISPER) {
                r.u64();
                const std::string from = utf8::shown_name(r.str8()), text = utf8::clean_text(r.str16(), 240);
                if (r.ok() && !text.empty()) {
                    const std::string line = from + " whispers: " + text;
                    g_whispers_in.push_back(line);
                    chat_log(line);
                    feed(line);
                }
                return;
            }
            parties::on_galaxy(type, r);
        };
        gx.fan_in = [](uint8_t type, net::Reader& r) { // 6F B / 6H A: the host's messages copied by the galaxy server
            if (type == net::MSG_PLAYER_STATE)
                g_sess.host_fan_in(type, r);
            else if ((type == net::MSG_CELL_STATE_C || type == net::MSG_TRIBE_STATE || type == net::MSG_TRIBE_VILLAGES ||
                      type == net::MSG_NPC_ANIM ||
                      type == net::MSG_TRIBE_BUILDINGS || type == net::MSG_TRIBE_TOOLS ||
                      type == net::MSG_TRIBE_RELATIONSHIPS || type == net::MSG_TRIBE_EQUIPMENT) &&
                     g_sess.cb.custom) // (6I A: the host's unreliable messages the same for every guest)
                g_sess.cb.custom(type, 0, r);
        };
        gx.hosted_stored = []() -> bool { // owner test 2026-11-17: a new host makes its own creature first
            if (!creations::host_needs_first_creature())
                return true;
            // not under a death screen, a cinematic or a busy game (the editor opened there stayed black, 2026-11-17)
            void* av = game::avatar();
            if (!av || game::health(av) <= 0 || world::cinematic_playing() || game_busy() || mode_switch_pending())
                return false;
            H->run_outside_frame([](void*) {
                if (!creations::host_needs_first_creature())
                    return;
                creations::host_first_creature_begin();
                const bool r = g_host_start_editor ? submit_new_creature_request(kCreatureStartEditor) // BUG-9265
                                                   : submit_new_creature_request();
                LOG("galaxy: the new host's first-creature editor (%s) -> %d", g_host_start_editor ? "the start editor" : "the Creator", (int)r);
                if (r)
                    feed("Make your creature for this planet: you play it when you save it.");
            }, nullptr);
            return true;
        };
        galaxy::init(H, M, gx);
        mouse::set_close_request(galaxy::close_request); // decision 6A A (alpha): Alt+F4 / the X save the planet first
    }
    era_visitors::init(H, M); // decision 5S A spike (alpha)
    galaxy_menu::init(H, M);  // decision 5X B (alpha)
    party::init(H, M, &g_sess);    // roadmap 0b
    {
        parties::Ctx pc; // 6J
        pc.sess = &g_sess;
        pc.stage_of = [](uint8_t id) -> uint32_t { // (a menu, an editor, a load: no stage, never refused for it)
            uint32_t m = 0;
            if (id == g_sess.my_id())
                m = g_mode;
            else if (auto it = g_remotes.find(id); it != g_remotes.end() && !it->second.buf.empty())
                m = it->second.buf.latest().mode;
            return m >= modes::kCell && m <= modes::kSpace ? m : 0;
        };
        pc.galaxy = [] { return galaxy::hosting(); };
        pc.name_of = [](uint8_t id) { return utf8::shown_name(player_name_of(id)); };
        pc.feed = [](const std::string& t) { feed(t); };
        pc.changed = [] { g_lobby_dirty = true; };
        // 6K: the galaxy server keeps the parties, but not of a direct session played while connected to one (2026-11-18
        // review: that session got no Party cards, "not here" invites and migrations alone)
        pc.galaxy_welcomed = [] { return galaxy::welcomed() && (g_sess.role() == net::Role::None || galaxy::relayed()); };
        pc.galaxy_slot = [] { return galaxy::my_slot(); };
        pc.galaxy_send = [](uint8_t kind, uint64_t other, uint8_t flag, uint32_t stage) {
            return galaxy::party_send(kind, other, flag, stage);
        };
        pc.galaxy_who = [] { galaxy::ask_players(); };
        pc.identity_of = [](uint8_t id) -> uint64_t {
            return id == g_sess.my_id() || !g_sess.connected() ? identity::own() : identity::known(id);
        };
        parties::init(H, M, std::move(pc));
    }
    mouse::set_player_menu_input(player_menu_input); // 6J
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
    library::init(H, M, &g_sess);
    lineage_cells::init(H, M, &g_sess); // BUG-9111
    H->command_add(M, "coop.lineagecells", "coop.lineagecells [on|off] - Lineages cells: player cells as Cell NPC species (host setting)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::vector<std::string> a(argv + 1, argv + argc);
                       H->out_printf(out, "%s", lineage_cells::debug(a).c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
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
    H->command_add(M, "coop.library", "coop.library [status|list|scan|onward on|off|import <PNG>] - persistent shared library",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", library::debug(std::vector<std::string>(argv+1,argv+argc)).c_str());
                       return 0;
                   }, nullptr, SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.partner_markers", "coop.partner_markers - current drawn partner indicators",
                   [](void*, int, const char* const*, SkOut* out) {
                       SAY("markers=%u enabled=%d",g_partner_indicators,int(g_partner_arrows)); return 0;
                   }, nullptr, MT);
    H->command_add(M, "coop.creations", "coop.creations [list | next|prev <creature|cell|ufo> | choose <kind> <hex>]",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", creations::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.bodyrec", "coop.bodyrec start|stop|count|dump [from] [n] - every frame: every synchronized body as drawn, absolute time (tests, BUG-0425)",
                   c_bodyrec, nullptr, MT);
    H->command_add(M, "coop.sync", "coop.sync - pose of every synchronized object (key x y z qx qy qz qw), tests",
                   [](void*, int, const char* const*, SkOut* out) {
                       std::vector<syncdbg::Entry> v = sync_all();
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
    cc.feed = [](const std::string& t) { feed(t); }; // BUG-9123
    cc.sess = &g_sess;
    cc.partners = partners;
    cc.notice = [](const std::string& t) { notice(t, -1); };
    cc.food_cause = [](int from, int before, int amount) { // Cell (dna_ledger.h)
        LOG("dna: food %d -> %d cause=meal:%d%+d", before, before + amount, from, amount);
        dna_note({dna_ledger::Cause::Meal, (uint8_t)from, (float)amount}, g_sess.connected() ? g_sess.my_id() : 0);
    };
    cc.my_name = [] { return utf8::shown_name(g_name); }; // BUG-9222: shown without the -NoShare- tag
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
    cc.peer_in_pool = [](uint8_t id) {
        auto it = g_remotes.find(id);
        if (it == g_remotes.end() || it->second.buf.empty() ||
            it->second.buf.age(H->time_seconds()) >= 5.0) return false;
        const auto& state = it->second.buf.latest();
        return g_mode == modes::kCell && same_place(state) && (state.flags & net::PF_IN_WORLD);
    };
    cc.host_away = [] { return g_host_away; }; // BUG-0205
    cc.fan_out = galaxy_fan_out; // 6F B
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
    sqc.place_of = [](uint8_t id) -> uint32_t { // BUG-9197: the leader per place
        if (id == g_sess.my_id()) return g_mode == modes::kSpace ? g_world : 0u;
        auto it = g_remotes.find(id);
        if (it == g_remotes.end() || it->second.buf.empty()) return 0;
        const auto& s = it->second.buf.latest();
        return s.mode == modes::kSpace ? s.world : 0u;
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
    autoupdate::set_feed(feed);
    autoupdate::init(H, M, COOP_VERSION);
    H->command_add(M, "coop.update", "coop.update [check | apply | url <u> | testdelay <ms> | applydelay <ms>] - SporeCoop updates from GitHub releases",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string val; // the rest: "fake <version>" (Lineages), "url <u>", "testdelay <ms>"
                       for (int i = 2; i < argc; ++i) val += std::string(i > 2 ? " " : "") + argv[i];
                       SAY("%s", autoupdate::command(argc > 1 ? argv[1] : "", val).c_str());
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
                       const auto report = civ::status() + civ::list();
                       for (size_t at = 0; at < report.size(); at += 6000)
                           SAY("%s", report.substr(at, 6000).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.drama", "coop.drama [start <kind> | mask <hex> | mate | target [<tribe obj hex>]] - the game's world events (BUG-0486, BUG-1078)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", drama::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
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
                       // BUG-9061: out_printf keeps only 8 KB per call. A full
                       // campaign exceeds that, especially with guest drive rows.
                       std::string report = tribe::status() + tribe::list();
                       for (size_t at = 0; at < report.size(); at += 6000)
                           SAY("%s", report.substr(at, 6000).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(
        M, "coop.evokey", "coop.evokey [nosave|save] - BUG-0517 tests: keys set after an editor; the host's in-place save off/on",
        [](void*, int argc, const char* const* argv, SkOut* out) -> int {
            if (argc > 1)
                g_evo_save_test_off = !strcmp(argv[1], "nosave");
            SAY("evokey: keys_set=%u done=%d watching=%d save_off=%d", g_evo_keys_set, (int)g_evo_key_done,
                (int)(g_evo_key_until > 0), (int)g_evo_save_test_off);
            return 0;
        },
        nullptr, SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.team", "coop.team [on|off] - co-op mechanics (joint hunt, rescue, pack regen)", c_team,
                   nullptr, MT);
    init_ui();
    adapters::init(H, M);
    H->command_add(M, "coop.adapters", "coop.adapters [stress N|fault] - local address lookup (BUG-0286)",
                   [](void*, int argc, const char* const* argv, SkOut* out) { return adapters::command(argc, argv, out); },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.uistate", "coop.uistate [0|1|log|lang en|ru|first|color <argb>|nudge <control> <dx> <dy>|hide <control>|nudge off|demo <addr> <port> <shown port>|demo off|widgets] - co-op window state (open/close it; the rest: tests, README pictures)", c_uistate, nullptr,
                   SK_CMD_OUTSIDE_FRAME);
    H->cheat_register("coop", "coop.",
                      "coop host [port] | join <ip[:port]> | leave | status | name <nick> | say <text>");
    LOG("coop ready (name '%s')", g_name.c_str());
    return &g_info;
}
