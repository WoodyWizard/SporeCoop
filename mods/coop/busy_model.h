// C3 of docs/plan-0.7.15.md (audit STG-4; classes "host away" and "pause"): one model of what the host's game is busy
// with, as every guest sees it, in place of the copies each stage keeps (tribe.cpp / civ.cpp local_play and
// g_away_orders, npc.cpp's shadow g_host_away, cellsync's own pool rule, Tribe's MSG_NPC_ANIM kinds 20 / 21 and their
// waits, the two HeldCommands wirings).
//
// Owner decision 1 (2026-11-14): the host's EXPLICIT pause (P, the pause menu, the Sporepedia, a tutorial box) makes
// the guests wait in every stage, with the line "<host> paused the game"; not in Rivals mode; a guest's own pause
// stops nobody; the host away (main menu, a load) and the host in an editor stay states of their own (the guests
// play on alone, as before). Decision 2: an editor visit is no world change (world_id.h).
//
// Parts:
//   1. Host side, classify(): the host's state from what its game shows this frame (busy.h's readings plus the
//      pause reasons of cGameTimeManager, docs/re/host-centric.md#host-centric-pause-reasons): Away (main menu,
//      loading), Editor, Paused (an explicit pause: the pause menu reason 0x4BF38A4, P 0x4BF38A8, the UI-modal
//      reason 0x4BF38A7 while a modal window of the game is up, the Sporepedia's 0x4BF38A6 while no editor is
//      coming (My Collections uses the pause menu's reason); in Cell, which sets no reason, the pause menu's
//      window), None. A stand of the game's own (a caption's
//      cinematic pause 0x4BF38A5 / 0x600C4AE, a stage entry, an editor coming 0x4BF38A6) is not Paused; it is
//      reported as the Caption bit for the stages whose row waits on it (Tribe, BUG-1086's behaviour).
//   2. Host side, Announcer: the state "once" (on every change, debounced: a pause counts after kPauseHold, as
//      BUG-1086's 1 s; and to every guest that joins) as one message (MSG_HOST_BUSY, wire spec below and in
//      docs/01-architecture.md#arch-busy-model). Encode / decode are here (portable, golden-byte tested).
//   3. Guest side, HostView: the host's state as this guest uses it: the message when the host sends it, else the
//      0.7.14 fallback (the host's PlayerState mode: menu / editor / load as coop.cpp host_out_of_stage; Tribe's
//      kind 21 for the pause). The same waits as coop.cpp host_away_tick (menu 1.5 s, editor / load 4 s, back 1 s)
//      so a passing state between two places is not "away". step() returns the edges (NPC destroys its proxies on
//      the edge into Away, the notes are said on edges).
//   4. Per-stage policy rows: what a guest of each stage does in each host state (Wait: its game stands with a
//      note; Local: its own game runs the shared objects (local_play); On: nothing changes) and whether the host
//      holds a guest's commands (HeldPump).
//   5. HeldPump: one pump for the held commands (net/held_cmds.h) with the settle of tribe.cpp / civ.cpp tick (the
//      stage back for kSettle s, the same world) and the guest's "your order waits" note throttle (civ say_host_away).
// Portable (no Windows, no SporeKit): tests/core_tests.cpp checks every stage row, Rivals, the codec and the pump.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "busy.h"
#include "net/held_cmds.h"
#include "stage_epoch.h"

namespace busy_model {

// ---------------------------------------------------------------- 1. the host's state
enum class State : uint8_t { None = 0, Paused = 1, Editor = 2, Away = 3 };
inline const char* state_name(State s)
{
    switch (s) {
    case State::None: return "none";
    case State::Paused: return "paused";
    case State::Editor: return "editor";
    case State::Away: return "away";
    }
    return "?";
}

// the pause reasons of cGameTimeManager (docs/re/tribe.md#tribe-game-time-pauses, #tribe-caption-pause-leak and
// docs/re/host-centric.md#host-centric-pause-reasons)
constexpr uint32_t kReasonMenu = 0x4BF38A4;     // the pause menu (Esc), F1 / F2 / Tab windows; the end-of-Tribe card
constexpr uint32_t kReasonKeyP = 0x4BF38A8;     // P
constexpr uint32_t kReasonModal = 0x4BF38A7;    // a UI modal, a tutorial / hint box, a stage's entry, Tribe's wait
constexpr uint32_t kReasonEditor = 0x4BF38A6;   // an editor is coming (City Hall, the launch) AND the Sporepedia
                                                // (lane 4, 2026-11-15: the Creature HUD's Sporepedia button, flags 1)
constexpr uint32_t kReasonCine1 = 0x4BF38A5;    // a cinematic script's pause (slot 1)
constexpr uint32_t kReasonCine2 = 0x600C4AE;    // a cinematic script's pause (slot 2, -pauseGraphics)

// detail bits of the state (the message carries them; the rows read some)
enum : uint16_t {
    kWhyMenu = 1u << 0,     // Away: the main menu
    kWhyLoad = 1u << 1,     // Away: a load or a mode switch
    kWhyPauseMenu = 1u << 2, // Paused: the pause menu reason
    kWhyKeyP = 1u << 3,     // Paused: P
    kWhyModal = 1u << 4,    // Paused: a modal / tutorial box of the game (its reason with a modal window up)
    kWhyCaption = 1u << 5,  // the game stands on its own (a caption, a cinematic pause): not Paused, Tribe waits
    kWhyStill = 1u << 6,    // the game time stands for any reason (busy.h kPaused)
    kWhySporepedia = 1u << 7, // Paused: the Sporepedia (reason 0x4BF38A6 while no editor is coming)
    // BUG-9304 (PR #216 by IltonBJSilva): the host's main thread is about to stand for long (an editor's first load:
    // 30-60 s on a busy PC), told just before; its guests wait up to kLongWaitMs for its packets, not the session's 30 s
    kWhyLongWait = 1u << 8,
};

// what the host's game shows this frame (the game glue fills it: game::mode_id, busy.h readings, world::pause_reasons,
// coop_ui::game_modal_up)
struct Reading {
    uint32_t mode = 0;
    bool menu = false;       // modes::kMenu (the galaxy menu)
    bool loading = false;    // kLoad, a mode switch pending or a load queued (busy.h kLoading)
    bool editor = false;     // kEdit / kEditor
    bool time_stands = false; // cGameTimeManager +0x48 bit 0
    bool cinematic = false;  // cCinematicManager +0x2C
    bool modal_window = false; // a modal window of the game (UTFWin +0x82C)
    bool editor_coming = false; // an editor request is on its way (the vote's / the guest's own editor, BUG-0920's
                                // pending flag; a planner): its 0x4BF38A6 is not the Sporepedia
    bool pause_menu_window = false; // the pause menu ("Options", control 0x0615B50D) is shown: the Cell stage's only
                                    // sign (its pause menu sets no cGameTimeManager reason; SimUpdate stops)
    bool saving = false; // the host's game writes a save of the mod's (a world share's, an autosave; world::save_busy
                         // or its "game saved" box up): its stand is no pause and no caption (BUG-1217)
    std::vector<std::pair<uint32_t, int>> reasons; // the pause reasons counted now (> 0)
};

struct Classified {
    State state = State::None;
    uint16_t why = 0;
};

inline int count_of(const Reading& r, uint32_t reason)
{
    for (auto& [k, n] : r.reasons)
        if (k == reason)
            return n;
    return 0;
}

inline Classified classify(const Reading& r)
{
    Classified c;
    if (r.menu) {
        c.state = State::Away;
        c.why = kWhyMenu;
        return c;
    }
    if (r.loading) {
        c.state = State::Away;
        c.why = kWhyLoad;
        return c;
    }
    if (r.editor) {
        c.state = State::Editor;
        return c;
    }
    if (r.pause_menu_window) // (Cell: no time manager pause; the other stages count its reason below too)
        c.why |= kWhyPauseMenu;
    if (!r.time_stands) {
        if (c.why)
            c.state = State::Paused;
        return c;
    }
    c.why |= kWhyStill;
    if (count_of(r, kReasonMenu) > 0)
        c.why |= kWhyPauseMenu;
    if (count_of(r, kReasonKeyP) > 0)
        c.why |= kWhyKeyP;
    if (count_of(r, kReasonModal) > 0 && r.modal_window && !r.cinematic)
        c.why |= kWhyModal;
    if (count_of(r, kReasonEditor) > 0 && !r.editor_coming)
        c.why |= kWhySporepedia;
    if (r.saving) {
        // BUG-1217: a save of the mod's stands the game for 1-2 s (the write, then its "game saved" box): told as a
        // caption stand it made every Tribe guest stop for a moment with two cards on each join and autosave. Only
        // the host's explicit pause (the pause menu, P, the Sporepedia) still counts while it saves; the box is no
        // tutorial box.
        c.why &= kWhyPauseMenu | kWhyKeyP | kWhySporepedia;
        if (c.why)
            c.state = State::Paused;
        return c;
    }
    if (c.why & (kWhyPauseMenu | kWhyKeyP | kWhyModal | kWhySporepedia))
        c.state = State::Paused;
    else
        c.why |= kWhyCaption; // the game's own stand (a caption, an entry, an editor coming)
    return c;
}

// ---------------------------------------------------------------- 2. the message
// MSG_HOST_BUSY (type number: the manifest's, proposed 70), host -> every guest, reliable, Session class (wherever the
// guest plays). Little endian, 14 bytes in version 1; a reader takes a longer payload (a newer host's tail) and drops
// a shorter one:
//   u8 version (1) | u8 state (State) | u16 why (kWhy* bits) | u32 mode (the host's mode id) | u32 world (the host's
//   save crc, modes::current_world; 0 unknown) | u16 seq (+1 per message of this host's session; an older seq than
//   the last one taken is dropped)
struct Msg {
    uint8_t version = 1;
    State state = State::None;
    uint16_t why = 0;
    uint32_t mode = 0;
    uint32_t world = 0;
    uint16_t seq = 0;
};
constexpr size_t kMsgSize = 14;

inline std::vector<uint8_t> encode(const Msg& m)
{
    std::vector<uint8_t> b;
    auto u8 = [&](uint8_t v) { b.push_back(v); };
    auto u16 = [&](uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); };
    auto u32 = [&](uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); };
    u8(m.version);
    u8((uint8_t)m.state);
    u16(m.why);
    u32(m.mode);
    u32(m.world);
    u16(m.seq);
    return b;
}
// false: too short, version 0, or a state this build does not know (dropped, counted by the caller)
inline bool decode(const uint8_t* p, size_t n, Msg& m)
{
    if (!p || n < kMsgSize || p[0] == 0 || p[1] > (uint8_t)State::Away)
        return false;
    auto u16 = [&](size_t o) { return (uint16_t)(p[o] | p[o + 1] << 8); };
    auto u32 = [&](size_t o) { return (uint32_t)u16(o) | (uint32_t)u16(o + 2) << 16; };
    m.version = p[0];
    m.state = (State)p[1];
    m.why = u16(2);
    m.mode = u32(4);
    m.world = u32(8);
    m.seq = u16(12);
    return true;
}

// host: the state once (on a change, debounced) and again to every new guest
struct Announcer {
    static constexpr double kPauseHold = 1.0; // s an explicit pause must stand before it is told (BUG-1086)
    Classified sent;        // the last state told
    bool told = false;      // something was told in this session
    double pending_since = 0; // the classified state differs from `sent` since (0: it does not)
    Classified pending;
    uint16_t seq = 0;
    uint32_t sends = 0;

    // this frame's classified state; true when a message is due now (then *out is it)
    bool step(double now, const Classified& c, uint32_t mode, uint32_t world, Msg* out)
    {
        bool same = c.state == sent.state && c.why == sent.why;
        if (told && same) {
            pending_since = 0;
            return false;
        }
        if (!(c.state == pending.state && c.why == pending.why) || !pending_since) {
            pending = c;
            pending_since = now;
        }
        // a pause (and a caption stand) counts after kPauseHold; the end of one, and every other state, at once (the
        // guests' HostView keeps the away waits of 0.7.14)
        bool hold = (c.state == State::Paused || (c.why & kWhyCaption)) && now - pending_since < kPauseHold;
        if (hold)
            return false;
        // a short flicker of the caption bit alone is not worth a message when nothing else changed
        sent = c;
        told = true;
        pending_since = 0;
        if (out)
            *out = Msg{1, c.state, c.why, mode, world, ++seq};
        sends++;
        return true;
    }
    // BUG-9304: told now, without the debounce (the host's main thread stands right after it): the state as sent
    Msg announce(const Classified& c, uint32_t mode, uint32_t world)
    {
        sent = c;
        told = true;
        pending_since = 0;
        sends++;
        return Msg{1, c.state, c.why, mode, world, ++seq};
    }
    // the message for a guest that just joined (the state as last told)
    Msg current(uint32_t mode, uint32_t world) const { return Msg{1, sent.state, sent.why, mode, world, seq}; }
    // a new session: the next state is told whatever it is
    void reset() { *this = Announcer{}; }
};

// BUG-9304 (PR #216 by IltonBJSilva): how long a guest waits for the host's packets (net::Session::timeout_ms) while the
// host's last told state is a long stand of its game: an editor coming or open (its first load reads hundreds of
// property files), or a load; else the session's own timeout. 120 s: over twice the 51.8 s seen, while a host that
// really died in its editor is noticed within two minutes (the PR had 180 s)
constexpr uint32_t kLongWaitMs = 120000;
inline uint32_t guest_timeout_ms(bool heard, const Msg& last, uint32_t normal)
{
    if (!heard)
        return normal;
    bool long_stand = (last.why & kWhyLongWait) || last.state == State::Editor ||
                      (last.state == State::Away && (last.why & kWhyLoad));
    return long_stand && kLongWaitMs > normal ? kLongWaitMs : normal;
}

// ---------------------------------------------------------------- 3. the guest's view
// the 0.7.14 fallback: the host's PlayerState mode as coop.cpp host_out_of_stage reads it ('m' menu, 'e' editor,
// 'l' load, 0 in a stage), and Tribe's kind 21 / 20 ("the host's game stands")
struct Fallback {
    char out_of_stage = 0;
    bool tribe_pause = false; // MSG_NPC_ANIM kind 21 up (a 0.7.14 host in Tribe)
};

struct Edge {
    State from = State::None, to = State::None;
    uint16_t why = 0;
    bool changed() const { return from != to; }
};

struct HostView {
    // the 0.7.14 waits of coop.cpp host_away_tick: a passing state between two places is not away
    static constexpr double kMenuWait = 1.5, kOtherWait = 4.0, kBackWait = 1.0;
    bool heard = false; // the host sends MSG_HOST_BUSY (a 0.7.15 host)
    Msg last;
    uint32_t taken = 0, dropped = 0;
    State settled = State::None;
    uint16_t settled_why = 0;
    double change_since = -1;

    // a MSG_HOST_BUSY from `from`: only the host's (id 0) is taken (a guest's pause stops nobody)
    bool on_message(uint8_t from, const uint8_t* p, size_t n)
    {
        Msg m;
        if (from != 0 || !decode(p, n, m) || (heard && (uint16_t)(m.seq - last.seq) >= 0x8000)) {
            dropped++;
            return false;
        }
        heard = true;
        last = m;
        taken++;
        return true;
    }
    // what the host's state is this frame before the waits
    Classified raw(const Fallback& fb) const
    {
        if (heard)
            return {last.state, last.why};
        Classified c;
        if (fb.out_of_stage == 'm')
            c = {State::Away, kWhyMenu};
        else if (fb.out_of_stage == 'l')
            c = {State::Away, kWhyLoad};
        else if (fb.out_of_stage == 'e')
            c = {State::Editor, 0};
        else if (fb.tribe_pause)
            c = {State::Paused, 0};
        return c;
    }
    // one frame: the settled state and its edge (Away / Editor after their waits, back after kBackWait; Paused and
    // its end at once: the host debounced it)
    Edge step(double now, const Fallback& fb)
    {
        Classified c = raw(fb);
        Edge e{settled, settled, settled_why};
        if (c.state == settled) {
            change_since = -1;
            settled_why = c.why;
            e.why = c.why;
            return e;
        }
        double wait = 0;
        bool away_like = [](State s) { return s == State::Away || s == State::Editor; }(settled);
        if (c.state == State::Away || c.state == State::Editor)
            wait = (c.why & kWhyMenu) ? kMenuWait : kOtherWait;
        else if (away_like)
            wait = kBackWait;
        if (change_since < 0)
            change_since = now;
        if (now - change_since < wait)
            return e;
        change_since = -1;
        settled = c.state;
        settled_why = c.why;
        e.to = c.state;
        e.why = c.why;
        return e;
    }
    void reset() { *this = HostView{}; }
};

// ---------------------------------------------------------------- 4. the stage rows
enum class Act : uint8_t {
    On,    // nothing changes: the host's states drive this stage as usual
    Wait,  // the guest's game stands (a pause of ours) with the note, until the host goes on
    Local, // the guest's own game runs the shared objects (tribe / civ local_play, npc away locals, cell's own pool)
};
inline const char* act_name(Act a) { return a == Act::On ? "on" : a == Act::Wait ? "wait" : "local"; }

struct Row {
    int stage;          // stage_epoch::index_of (0 Cell .. 4 Space)
    const char* name;
    Act paused;         // the host's explicit pause (decision 1: Wait everywhere, On in Rivals mode)
    Act caption;        // the host's game stands on its own (a caption): Tribe waits (BUG-1086), the others play on
    Act editor;         // the host in an editor (decision 2: host away, not a world change)
    Act away;           // the host in the main menu or loading
    bool hold_cmds;     // the host holds a guest's commands while its stage is not running (BUG-0693)
    bool say_cmd_waits; // the guest is told that its command waits (civ say_host_away)
    double settle;      // s the host's stage runs again before held commands run (net::HeldCommands::kSettle)
};

// today's behaviour plus decision 1 (Paused: Wait in every stage). Cell / Creature: npc and cellsync play locally
// while the host is away or in an editor (BUG-0130 / BUG-0205 / BUG-0233); Tribe / Civ: local_play (BUG-0151); Space:
// its own modules (space_ext away) keep their rules, the row changes nothing there but the pause.
inline const Row* rows()
{
    static const Row r[stage_epoch::kStages] = {
        {0, "cell", Act::Wait, Act::On, Act::Local, Act::Local, false, false, 0},
        {1, "creature", Act::Wait, Act::On, Act::Local, Act::Local, false, false, 0},
        {2, "tribe", Act::Wait, Act::Wait, Act::Local, Act::Local, true, false, net::HeldCommands::kSettle},
        {3, "civ", Act::Wait, Act::On, Act::Local, Act::Local, true, true, net::HeldCommands::kSettle},
        {4, "space", Act::Wait, Act::On, Act::On, Act::On, false, false, 0},
    };
    return r;
}
inline const Row* row_of(int stage) { return stage >= 0 && stage < stage_epoch::kStages ? &rows()[stage] : nullptr; }

// what a guest in `stage` does now; `rivals`: the Rivals mode is on (the players are rivals: nobody waits for the
// host's pause, decision 1)
inline Act act(int stage, const Edge& host, bool rivals)
{
    const Row* r = row_of(stage);
    if (!r)
        return Act::On;
    switch (host.to) {
    case State::Paused: return rivals ? Act::On : r->paused;
    case State::Editor: return r->editor;
    case State::Away: return r->away;
    case State::None: return (host.why & kWhyCaption) && !rivals ? r->caption : Act::On;
    }
    return Act::On;
}

// the guest's note on an edge ("" none). The away texts are coop.cpp host_away_tick's.
inline std::string note(const Edge& e, const std::string& host, Act now)
{
    if (!e.changed())
        return "";
    if (e.to == State::Paused && now == Act::Wait)
        return host + " paused the game: yours waits until " + host + " goes on";
    if (e.from == State::Paused && e.to == State::None)
        return host + " (host) goes on: so does your game";
    if (e.to == State::Editor)
        return host + " (host) is in an editor: you play on alone until " + host + " is back";
    if (e.to == State::Away)
        return host + " (host) " + ((e.why & kWhyLoad) ? "is loading" : "left for the main menu") + ": you play on alone until " +
               host + " is back";
    if ((e.from == State::Editor || e.from == State::Away) && e.to == State::None)
        return host + " (host) is back";
    return "";
}

// ---------------------------------------------------------------- 5. the held commands
struct HeldPump {
    net::HeldCommands q;
    double active_at = -1, active_since = 0;
    double said_at = -100; // guest: the last "your command waits" note
    static constexpr double kSayEvery = 5.0;

    // host: a guest's command came while its stage cannot run it: held (false: the row does not hold, drop it)
    bool hold(const Row& r, uint8_t from, const uint8_t* p, size_t n, uint32_t world, double now)
    {
        if (!r.hold_cmds || !n)
            return false;
        q.push(from, p, n, world, now);
        return true;
    }
    // host, every tick: `active` (the stage runs now); the held commands of this world run once the stage ran
    // r.settle s again (a load, an editor: its world settles first). Returns how many ran.
    template <class Run>
    size_t tick(const Row& r, bool active, uint32_t world, double now, Run&& run)
    {
        if (!active)
            return 0;
        if (active_at < 0 || now - active_at > 1.0) // the stage just came back
            active_since = now;
        active_at = now;
        if (q.q.empty() || now - active_since <= r.settle)
            return 0;
        uint32_t before = q.run;
        q.drain(world, now, run);
        return q.run - before;
    }
    // guest: say "your command waits" now? (at most every kSayEvery s; only rows that say it)
    bool say_due(const Row& r, double now)
    {
        if (!r.say_cmd_waits || now - said_at < kSayEvery)
            return false;
        said_at = now;
        return true;
    }
    void forget(uint8_t from) { q.forget(from); }
    void clear() { q.clear(); }
    size_t held_of(uint8_t from) const
    {
        size_t n = 0;
        for (auto& c : q.q)
            n += c.from == from;
        return n;
    }
};

} // namespace busy_model
