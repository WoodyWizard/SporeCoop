// C3 of 0.7.15 (BUG-1142, audit STG-4; owner decision 1): the host's busy state for every guest, on top of the
// portable busy_model.h (docs/01-architecture.md#arch-busy-model).
//
// Host: busy_model::classify over this frame's readings (the mode, busy.h's gate, the pause reasons of
// cGameTimeManager, a modal window, the pause menu's window in Cell, an editor on its way), busy_model::Announcer
// tells every guest once per change (MSG_HOST_BUSY, reliable; and the state to a guest that joins).
// Guest: busy_model::HostView (the host's message, else the 0.7.14 fallback), the stage's row (busy_model::act):
// the host's EXPLICIT pause (P, the pause menu, the Sporepedia, a tutorial box) makes this game wait with the line
// "<host> paused the game", in every stage, while it plays the host's world; not in Rivals mode; a guest's pause
// stops nobody (only the host sends the message, HostView takes id 0 only). The host away / in an editor stays its
// own state (coop.cpp host_away_tick, the modules' local play): this file changes nothing there.
// The wait: one pause of ours on the game's time (cGameTimeManager::Pause with the UI-modal reason 0x4BF38A7, as
// Tribe's waits do, BUG-0960 / BUG-1086); in Cell, which is no Simulator stage and has no time manager pause, the
// cell step (SimUpdate) does not run while it holds (modes.cpp, as the game's own pause menu stops it).
// Switches: `core.host_busy` (the host's message and the guest's view), per stage `cell.busy_model`,
// `npc.busy_model` (Creature), `tribe.busy_model`, `civ.busy_model`, `space.busy_model` (the wait in that stage).
#include "lifecycle.h"
#include "coop_state.h"
#include "busy_model.h"
#include "host_busy.h"
#include "net/manifest.h"
#include "stage_epoch.h"

namespace coop_core {

static bool g_host_busy_fix = true;
COOP_FIX("core.host_busy", "BUG-1142", g_host_busy_fix,
         "core: the host tells its guests once when its game is paused, away or in an editor (MSG_HOST_BUSY)");
static bool g_cell_fix = true, g_creature_fix = true, g_tribe_fix = true, g_civ_fix = true, g_space_fix = true;
COOP_FIX("cell.busy_model", "BUG-1142", g_cell_fix, "cell: a guest's game waits while the host's game is paused (its pause menu)");
COOP_FIX("npc.busy_model", "BUG-1142", g_creature_fix,
         "creature: a guest's game waits while the host's game is paused (pause menu, P, Sporepedia, a tutorial box)");
COOP_FIX("tribe.busy_model", "BUG-1142", g_tribe_fix,
         "tribe: a guest's game waits for the host's pause through the one host-busy model (not Tribe's own kind 21)");
COOP_FIX("civ.busy_model", "BUG-1142", g_civ_fix,
         "civ: a guest's game waits while the host's game is paused (pause menu, P, Sporepedia, a tutorial box)");
COOP_FIX("space.busy_model", "BUG-1142", g_space_fix,
         "space: a guest's game waits while the host's game is paused (not in Rivals mode)");

// BUG-1217: the host's own save (a world share's for a joining guest, an autosave) stands its game 1-2 s; told as a
// caption stand, every Tribe guest stopped for 0.1 s with "<host>'s game stands" / "goes on" on each join and
// autosave. Off: the save's stand is classified like any other stand (0.7.15 before the fix).
static bool g_save_fix = true;
COOP_FIX("core.host_busy_save", "BUG-1217", g_save_fix,
         "core: the host's own save (a world share's, an autosave) is no stand its guests are told about or wait for");

// BUG-1419 (ported from PR #216 by IltonBJSilva): an editor's first load stood the host's main thread 30-60 s on a busy
// PC (the City Hall editor's palettes, the spaceship editor's property files); its guests heard nothing for 30 s and
// their sessions ended ("connection to host lost (timeout)"), the host went on alone. The host now tells its guests
// just before every editor request of its own reaches the game (host_busy_long_wait from vote.cpp: Editor +
// kWhyLongWait, sent at once), and a guest waits up to busy_model::kLongWaitMs for the host's packets while the host's
// last told state is such a stand (an editor, a load). Off: the guest's timeout stays 30 s and nothing is told before
// the editor (0.7.16). The other direction (a guest standing in its own editor) is BUG-1420: the host keeps 30 s.
static bool g_long_wait_fix = true;
COOP_FIX("core.editor_long_wait", "BUG-1419", g_long_wait_fix,
         "core: the host tells its guests before an editor loads, and a guest waits longer for a host in an editor or a load");
static uint32_t g_long_waits_told;
static uint32_t g_normal_timeout_ms; // the session's own timeout (read once)

static busy_model::Announcer g_ann;
static busy_model::HostView g_view;
static busy_model::Edge g_edge;
static busy_model::Classified g_host_now; // host: this frame's classified state (coop.status)
static uint32_t g_sent, g_rx, g_rx_dropped, g_waits, g_resumes;
static bool g_ann_session;          // host: the Announcer belongs to the running session
constexpr uint32_t kWaitReason = busy_model::kReasonModal; // (counted: balanced with the game's own)
enum class Hold : uint8_t { None, Time, Cell };
static Hold g_hold = Hold::None;
static double g_hold_since; // (coop.status: how long this game waits)

static bool stage_on(int stage)
{
    switch (stage) {
    case 0: return g_cell_fix;
    case 1: return g_creature_fix;
    case 2: return g_tribe_fix;
    case 3: return g_civ_fix;
    case 4: return g_space_fix;
    }
    return false;
}

static void* time_manager()
{
    static uintptr_t get = H->sym("Simulator::cGameTimeManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// the wait of this game on (true) or off; false: it could not be made (no time manager)
static bool hold(bool on, int stage, const char* why)
{
    if (on == (g_hold != Hold::None))
        return true;
    if (!on) {
        if (g_hold == Hold::Cell)
            modes::set_cell_hold(false);
        else if (void* tm = time_manager()) {
            static uintptr_t resume = H->sym("Simulator::cGameTimeManager::Resume");
            if (resume)
                skg::thiscall<int>(resume, tm, kWaitReason);
        }
        g_hold = Hold::None;
        g_resumes++;
        LOG("host busy: this game goes on (%s, BUG-1142)", why);
        return true;
    }
    if (stage == 0) {
        modes::set_cell_hold(true);
        g_hold = Hold::Cell;
    } else {
        static uintptr_t pause = H->sym("Simulator::cGameTimeManager::Pause");
        void* tm = time_manager();
        if (!tm || !pause)
            return false;
        skg::thiscall<int>(pause, tm, kWaitReason);
        g_hold = Hold::Time;
    }
    g_hold_since = H->time_seconds();
    g_waits++;
    LOG("host busy: this game waits for the host (%s, %s, BUG-1142)", g_hold == Hold::Cell ? "the cell step stands" : "time paused",
        why);
    return true;
}

static uint32_t host_world_crc() { return g_mode == modes::kSpace || stage_mode(g_mode) ? modes::current_world() : 0; }

static std::atomic<bool> g_pause_menu_seen{false};

static busy_model::Reading reading(double now)
{
    busy_model::Reading r;
    r.mode = game::mode_id();
    unsigned b = game_busy();
    r.menu = r.mode == modes::kMenu;
    r.loading = (b & busy::kLoading) != 0;
    r.editor = (b & busy::kEditor) != 0;
    r.time_stands = (b & busy::kPaused) != 0;
    r.cinematic = (b & busy::kCinematic) != 0;
    r.modal_window = coop_ui::game_modal_up();
    r.editor_coming = editor_coming(now);
    r.pause_menu_window = r.mode == modes::kCell && g_pause_menu_seen.load();
    r.saving = g_save_fix && (world::save_busy() || world::saved_box_up());
    if (r.time_stands)
        r.reasons = world::pause_reasons();
    return r;
}

static void send_to(uint8_t id, const busy_model::Msg& m)
{
    std::vector<uint8_t> b = busy_model::encode(m);
    net::Writer w;
    for (uint8_t c : b)
        w.u8(c);
    if (id == 0xFF)
        g_sess.broadcast_reliable(net::MSG_HOST_BUSY, w);
    else
        g_sess.send_reliable_to(id, net::MSG_HOST_BUSY, w);
    g_sent++;
}

// host, every frame (also without guests: a guest that joins gets the state as it is)
static void host_tick(double now)
{
    if (!g_host_busy_fix || g_sess.role() != net::Role::Host) {
        if (g_ann_session)
            g_ann.reset();
        g_ann_session = false;
        return;
    }
    g_ann_session = true;
    g_host_now = busy_model::classify(reading(now));
    busy_model::Msg m;
    if (g_ann.step(now, g_host_now, game::mode_id(), host_world_crc(), &m)) {
        LOG("host busy: %s (why %04X) told the guests (seq %u, BUG-1142)", busy_model::state_name(m.state), m.why, m.seq);
        if (g_sess.players().size() > 1)
            send_to(0xFF, m);
    }
}

// the guest plays the host's world (same stage, not elsewhere): only then does the host's pause hold it
static bool in_host_world()
{
    if (!g_view.heard || g_view.last.mode != g_mode)
        return false;
    return g_mode == modes::kSpace ? !guest_space_other() : !guest_elsewhere();
}

static void guest_tick(double now)
{
    if (!g_normal_timeout_ms)
        g_normal_timeout_ms = g_sess.timeout_ms;
    bool guest = g_sess.role() == net::Role::Client && g_sess.connected();
    // BUG-1419: a host in an editor or a load may stand long: wait longer for its packets (a guest only: the host's
    // timeout for its guests stays the session's, BUG-1420)
    g_sess.timeout_ms = guest && g_host_busy_fix && g_long_wait_fix
                            ? busy_model::guest_timeout_ms(g_view.heard, g_view.last, g_normal_timeout_ms)
                            : g_normal_timeout_ms;
    if (!guest || !g_host_busy_fix) {
        if (g_hold != Hold::None)
            hold(false, -1, guest ? "core.host_busy off" : "the session ended");
        if (g_view.heard || g_view.taken)
            g_view.reset();
        g_edge = {};
        return;
    }
    busy_model::Fallback fb;
    fb.out_of_stage = host_out_of_stage();
    busy_model::Edge e = g_view.step(now, fb);
    g_edge = e;
    int stage = stage_epoch::index_of(g_mode);
    busy_model::Act a = busy_model::act(stage, e, spaceext::rivals());
    bool want = a == busy_model::Act::Wait && stage_on(stage) && in_host_world() && !game::leaving_stage();
    std::string host = g_sess.name_of(0);
    if (want && g_hold == Hold::None) {
        if (hold(true, stage, busy_model::state_name(e.to)))
            feed(e.to == busy_model::State::Paused ? host + " paused the game: yours waits until " + host + " goes on"
                                                    : host + "'s game stands: yours waits until it goes on");
    } else if (!want && g_hold != Hold::None) {
        hold(false, stage, busy_model::state_name(e.to));
        if (e.to == busy_model::State::None)
            feed(host + " (host) goes on: so does your game");
    }
}

void host_busy_tick(double now)
{
    host_tick(now);
    guest_tick(now);
}

// BUG-1419, host: the game's main thread is about to stand for long (an editor is asked for): the guests are told now,
// and the message leaves before the stand
void host_busy_long_wait(const char* what)
{
    if (!g_host_busy_fix || !g_long_wait_fix || g_sess.role() != net::Role::Host)
        return;
    busy_model::Msg m = g_ann.announce({busy_model::State::Editor, (uint16_t)busy_model::kWhyLongWait}, game::mode_id(),
                                       host_world_crc());
    g_long_waits_told++;
    if (g_sess.players().size() > 1) {
        send_to(0xFF, m);
        g_sess.flush_now();
    }
    LOG("host busy: %s: the guests wait up to %u s for this game (seq %u, BUG-1419)", what, busy_model::kLongWaitMs / 1000,
        m.seq);
}

// the outside-frame menu check (coop.cpp menu_check_task, 5 times a second): the pause menu's window is shown
void host_busy_pause_menu(bool shown) { g_pause_menu_seen = shown; }

// host: a guest joined: the state as last told (after its WELCOME)
void host_busy_join(uint8_t id)
{
    if (!g_host_busy_fix || g_sess.role() != net::Role::Host || !g_ann.told)
        return;
    send_to(id, g_ann.current(game::mode_id(), host_world_crc()));
}

// guest: MSG_HOST_BUSY (Session class: taken wherever this game plays)
void host_busy_message(uint8_t from, net::Reader& r)
{
    if (!g_host_busy_fix || g_sess.role() != net::Role::Client)
        return;
    size_t n = r.left();
    if (g_view.on_message(from, r.cur(), n)) {
        g_rx++;
        LOG("host busy: the host is %s (why %04X, mode %08X, seq %u)", busy_model::state_name(g_view.last.state), g_view.last.why,
            g_view.last.mode, g_view.last.seq);
    } else
        g_rx_dropped++;
}

std::string host_busy_status()
{
    char b[420];
    snprintf(b, sizeof b,
             "host busy: fix=%d role=%s now=%s why=%04X told=%s sent=%u | view heard=%d host=%s why=%04X settled=%s rx=%u "
             "dropped=%u | wait=%s waits=%u resumes=%u | long_wait fix=%d told=%u timeout=%u ms\n",
             (int)g_host_busy_fix, g_sess.role() == net::Role::Host ? "host" : "guest", busy_model::state_name(g_host_now.state),
             g_host_now.why, busy_model::state_name(g_ann.sent.state), g_sent, (int)g_view.heard,
             busy_model::state_name(g_view.last.state), g_view.last.why, busy_model::state_name(g_edge.to), g_rx, g_rx_dropped,
             g_hold == Hold::None ? "none" : g_hold == Hold::Cell ? "cell" : "time", g_waits, g_resumes, (int)g_long_wait_fix,
             g_long_waits_told, g_sess.timeout_ms);
    if (g_hold != Hold::None) {
        size_t l = strlen(b);
        if (l > 0 && l + 40 < sizeof b)
            snprintf(b + l - 1, sizeof b - l + 1, " for %.0f s\n", H->time_seconds() - g_hold_since);
    }
    return b;
}

} // namespace coop_core

// for the stage modules (host_busy.h)
namespace host_busy {
bool waiting() { return coop_core::g_hold != coop_core::Hold::None; }
bool takes_tribe_pause() { return coop_core::g_host_busy_fix && coop_core::g_tribe_fix && coop_core::g_view.heard; }
} // namespace host_busy

// the session's end: what the host told and the guest heard was that session's
SESSION_STATE("core.host_busy", nullptr, [] { coop_core::g_view.reset(); coop_core::g_ann.reset(); coop_core::g_edge = {}; },
              []() -> size_t { return coop_core::g_view.heard ? 1 : 0; });
