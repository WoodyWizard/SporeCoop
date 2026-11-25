// Co-op Galactic Adventures (WP-A of 0.7.99, BUG-1474 / BUG-0793; design inspired by SporeCoop Lineages PR #196 by
// PandizeLive and SporeCoopGA by WoodyWizard, adapted to SporeCoop's verified hooks, LiveRef, CallGuard, Lifecycle,
// and family-minor gates).
//
// E-A (adventure.shared):
//   Hooks ScenarioCreditGoal (0xF1ACF0), ScenarioPlay_SetCurrentAct (0xF1EE70), ScenarioPlay_SetState (0xF1A9C0).
//   Host maintains a monotonic run journal (goal credits, act changes, host win/loss outcome, non-creature scenario
//   object deaths), broadcasts entries on MSG_ADVENTURE (79, creature 1.4), and replays entries on late join / rejoin.
//   Guest forwards non-defeat goal credits to host (defeat credits of named objects stay native), holds its own local
//   SetState(6) while host plays, and replays host commits in strict sequence order.
//
// E-B (adventure.host_actors, adventure.hit_forward, adventure.guest_extras, adventure.body_side):
//   Host streams live scenario creatures at 10 Hz (delta + 1 Hz full refresh) keyed by host serial (+0x24) and
//   (marker +0x18 hash, class +0x1C). Guest keeps its native scenario objects (avoiding the 0xC0714E act-setup fault
//   and preserving class-targeted defeat goals), pairs each host actor with the nearest unpaired local creature of the
//   same (marker, class) within 12 m, suppresses ONLY NPCTickAI on paired creatures while the stream flows, drives
//   them with the puppet/kinematic driver, mirrors host Die, forwards guest captain hits to host, and destroys
//   unpaired guest leftover creatures after 5 s. On host, partner captain puppets receive the captain's political ID
//   (+0x30) each tick so host enemy AI (npc.cpp ai_target) attacks partner captains too.
#include "adventure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "adventure_logic.h"
#include "call_guard.h"
#include "driver.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "net/interp.h"
#include "net/protocol.h"
#include "settings.h"
#include "sporekit_game.hpp"

namespace adventure {

// ---------------- COOP_FIX switches ----------------
static bool g_shared_fix = true;
COOP_FIX("adventure.shared", "BUG-0793 BUG-1474", g_shared_fix,
         "adventure: shared Galactic Adventures run journal (goals, acts, win/loss outcome, late-join replay)");

static bool g_host_actors_fix = true;
COOP_FIX("adventure.host_actors", "BUG-1474", g_host_actors_fix,
         "adventure: host simulates adventure creatures; guest pairs by (marker, class) within 12 m, skips NPCTickAI on paired creatures, drives them, and mirrors Die");

static bool g_hit_forward_fix = true;
COOP_FIX("adventure.hit_forward", "BUG-1474", g_hit_forward_fix,
         "adventure: guest captain hits on paired adventure enemies forward to the host for authoritative damage and defeat-goal credit");

static bool g_guest_extras_fix = true;
COOP_FIX("adventure.guest_extras", "BUG-1474", g_guest_extras_fix,
         "adventure: guest removes unpaired leftover adventure creatures after 5 s while the host actor stream runs");

static bool g_body_side_fix = true;
COOP_FIX("adventure.body_side", "BUG-1474", g_body_side_fix,
         "adventure: host sets partner captain puppet political ID (+0x30) so adventure enemies attack partner captains too");

// ---------------- Constants & Game Layout ----------------
constexpr uint32_t kCreatureNoun = 0x018EB45Eu;
constexpr uint32_t kBuildingNoun = 0x070703B3u;
constexpr uint32_t kBuildingType = 0x070704DBu;
constexpr uintptr_t kBuildingCombatOff = 0x120;
constexpr uintptr_t kScenarioMarkerOff = 0x18; // cGameData +0x18: cScenarioMarker*
constexpr uintptr_t kScenarioClassOff = 0x1C;  // cGameData +0x1C: int32 scenario class (-1 = none)
constexpr uintptr_t kGoalStride = 0x1AC;       // cScenarioPlayMode +0x64/+0x68 goal vector stride
constexpr double kActorSendPeriod = 0.1;       // 10 Hz host actor stream
constexpr double kActorFullRefresh = 1.0;      // 1 Hz full refresh for late join / UDP loss recovery
constexpr double kHelloPeriod = 1.0;           // 1 Hz guest hello while syncing
constexpr double kBuildingPollPeriod = 0.25;   // 4 Hz non-creature scenario object death poll

// ---------------- Hook Trampolines & Call Guards ----------------
typedef void(SK_THISCALL* CreditGoal_t)(void* play, void* goal, void* spatial1, void* spatial2);
typedef void(SK_THISCALL* SetCurrentAct_t)(void* play, int act, bool skip_setup);
typedef void(SK_THISCALL* SetState_t)(void* play, int state);

static CreditGoal_t o_CreditGoal;
static SetCurrentAct_t o_SetCurrentAct;
static SetState_t o_SetState;
static uintptr_t a_JumpToAct;

static bool g_adv_applying = false;
CALL_GUARD("adventure.applying", g_adv_applying);
static int g_in_set_act = 0;
CALL_GUARD_DEPTH("adventure.set_act", g_in_set_act);
static bool g_in_damage_mirror = false;
CALL_GUARD("adventure.damage_mirror", g_in_damage_mirror);

// ---------------- Module State ----------------
static const SkHost* H;
static SkMod* M;
static Ctx C;

static uint32_t g_run_nonce = 0;
static uint32_t g_visitor_nonce = 0;
static void* g_last_playmode = nullptr;
static int32_t g_last_completed = -1;

// E-A journals
static HostJournal g_host_journal;
static GuestJournal g_guest_journal;
static std::unordered_map<uint8_t, uint32_t> g_host_visitors; // peer id -> visitor_id welcomed for current run
static double g_last_hello_sent = -1e9;
static int32_t g_jump_queued_act = -1;
static double g_jump_queued_at = -1e9;

// Non-creature building death tracking (E-A)
struct BuildingWatch {
    game::LiveRef ref;
    uint32_t obj_id = 0;
    float hp = 0.f;
    float max_hp = 0.f;
    bool hurt = false;
    bool dead_counted = false;
};
static std::unordered_map<uint32_t, BuildingWatch> g_buildings; // local serial -> watch
static std::unordered_map<uint32_t, uint8_t> g_local_bldg_deaths; // obj_id -> local deaths seen
static std::unordered_map<uint32_t, uint8_t> g_mirrored_bldg_deaths; // obj_id -> deaths applied from remote
static double g_last_bldg_poll = -1e9;

// E-B host actor stream
struct HostActorSent {
    float pos[3] = {};
    float rot[4] = {};
    float hp = -1.f;
    uint8_t flags = 0;
    double sent_at = -1e9;
};
static std::unordered_map<uint32_t, HostActorSent> g_host_actors_sent; // host_key -> last sent
static double g_last_actor_send = -1e9;
static double g_last_actor_full = -1e9;

// E-B guest pairing & driving
struct DrivenActor {
    uint32_t host_key = 0;
    uint32_t marker_hash = 0;
    int32_t class_id = -1;
    game::LiveRef ref;
    net::SnapshotBuffer buf;
    drive::State ds;
    float last_hp = 0.f;
    float last_max_hp = 0.f;
    uint8_t last_flags = 0;
    net::Vec3 last_pos{};
    net::Quat last_rot{0.f, 0.f, 0.f, 1.f};
    double last_rx = -1e9;
    bool death_mirrored = false;
};
static ActorPairing g_pairing;
static std::unordered_map<uint32_t, DrivenActor> g_driven; // host_key -> driven state
static double g_now = 0.0;

// Diagnostics counters
static uint32_t g_goals_sent = 0, g_goals_applied = 0, g_acts_sent = 0, g_acts_applied = 0;
static uint32_t g_outcomes_sent = 0, g_outcomes_applied = 0, g_guest_fail_held = 0;
static uint32_t g_hits_forwarded = 0, g_hits_applied = 0, g_deaths_mirrored = 0, g_extras_removed = 0;
static uint32_t g_actor_batches_tx = 0, g_actor_batches_rx = 0;

// ---------------- Role & Capability Helpers ----------------
static bool connected() { return C.sess && C.sess->connected(); }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client; }
static bool in_scenario() { return game::mode_id() == modes::kScenario && modes::adventure_sub_mode() != 1; }

static bool shared_enabled() { return g_shared_fix && settings::on(settings::kAdventureShared); }
static bool host_actors_enabled() { return shared_enabled() && g_host_actors_fix; }

static bool peer_reads_adv(uint8_t id)
{
    if (!C.sess)
        return false;
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess->peer_families(id, has, f) && peer_supports(f, has);
}

static bool host_reads_adv()
{
    if (!C.sess)
        return false;
    bool has = false;
    std::vector<net::FamilyRev> f;
    return C.sess->host_families(has, f) && peer_supports(f, has);
}

static bool any_guest_reads_adv()
{
    if (!is_host() || !C.sess)
        return false;
    for (const auto& p : C.sess->players())
        if (p.id != 0 && peer_reads_adv(p.id))
            return true;
    return false;
}

static wire::Key3 current_mission()
{
    wire::Key3 k;
    modes::adventure_mission_key(k.v);
    return k;
}

static bool play_info(void*& pm, int32_t& state, int32_t& act, int32_t& completed)
{
    pm = modes::adventure_play_mode();
    if (!pm || !skg::readable(pm, 0xBC)) {
        pm = nullptr;
        state = -1;
        act = -1;
        completed = -1;
        return false;
    }
    state = skg::field<int32_t>(pm, 0x90);
    act = skg::field<int32_t>(pm, 0xB8);
    completed = skg::field<int32_t>(pm, 0x28);
    return true;
}

// Reads scenario creature/object identity: marker (+0x18) + class (+0x1C).
// Returns false for non-scenario objects (class < 0).
static bool read_scenario_identity(void* obj, uint32_t& marker_hash, int32_t& class_id)
{
    if (!obj || !skg::readable(obj, 0x28))
        return false;
    class_id = skg::field<int32_t>(obj, kScenarioClassOff);
    if (class_id < 0)
        return false;
    void* marker = skg::field<void*>(obj, kScenarioMarkerOff);
    const void* mbytes = (marker && skg::readable(marker, 0x24)) ? marker : nullptr;
    marker_hash = marker_sig(mbytes, class_id);
    return true;
}

static bool is_scenario_creature(void* c, void* av, void* av_herd, uint32_t& marker_hash, int32_t& class_id)
{
    if (!c || c == av || game::is_puppet(c))
        return false;
    if (av_herd && game::herd_of(c) == av_herd)
        return false;
    return read_scenario_identity(c, marker_hash, class_id);
}

static uint32_t next_nonzero_nonce(uint32_t& counter)
{
    uint32_t t = net::now_ms();
    counter = (counter + 1u) ^ (t * 2654435761u);
    if (counter == 0)
        counter = 1u;
    return counter;
}

// ---------------- Wire Senders ----------------
static void send_to_peer(uint8_t peer, wire::adventure::Kind kind, const auto& payload, bool reliable = true)
{
    if (!C.sess)
        return;
    net::Writer w;
    w.u8((uint8_t)kind);
    payload.write(w);
    if (reliable)
        C.sess->send_reliable_to(peer, net::MSG_ADVENTURE, w);
    else
        C.sess->send_unreliable_to(peer, net::MSG_ADVENTURE, w);
}

static void broadcast_adv(wire::adventure::Kind kind, const auto& payload, bool reliable = true, uint8_t except = 0xFF)
{
    if (!is_host() || !C.sess)
        return;
    for (const auto& p : C.sess->players()) {
        if (p.id == 0 || p.id == except || !peer_reads_adv(p.id))
            continue;
        send_to_peer(p.id, kind, payload, reliable);
    }
}

static void send_journal_entry(uint8_t peer, const JournalEntry& e, bool broadcast = false)
{
    wire::adventure::RunHeader hdr{g_host_journal.mission, g_host_journal.run_id};
    switch (e.kind) {
    case EntryKind::Goal: {
        wire::adventure::Commit msg{hdr, e.seq, e.goal};
        if (broadcast)
            broadcast_adv(wire::adventure::K_COMMIT, msg, true);
        else
            send_to_peer(peer, wire::adventure::K_COMMIT, msg, true);
        break;
    }
    case EntryKind::Act: {
        wire::adventure::ActChange msg{hdr, e.seq, e.act};
        if (broadcast)
            broadcast_adv(wire::adventure::K_ACT, msg, true);
        else
            send_to_peer(peer, wire::adventure::K_ACT, msg, true);
        break;
    }
    case EntryKind::Outcome: {
        wire::adventure::Outcome msg{hdr, e.seq, e.outcome_state};
        if (broadcast)
            broadcast_adv(wire::adventure::K_OUTCOME, msg, true);
        else
            send_to_peer(peer, wire::adventure::K_OUTCOME, msg, true);
        break;
    }
    case EntryKind::Death: {
        wire::adventure::Death msg{hdr, e.seq, e.object_id, e.deaths};
        if (broadcast)
            broadcast_adv(wire::adventure::K_DEATH, msg, true);
        else
            send_to_peer(peer, wire::adventure::K_DEATH, msg, true);
        break;
    }
    }
}

// ---------------- Reset & Lifecycle ----------------
static void reset_adventure_state(const char* why)
{
    if (g_host_journal.active() || g_guest_journal.run_id != 0 || !g_driven.empty()) {
        if (H)
            H->log(M, SK_INFO, "adventure: reset (%s)", why);
    }
    g_host_journal.reset();
    g_guest_journal.reset();
    g_host_visitors.clear();
    g_buildings.clear();
    g_local_bldg_deaths.clear();
    g_mirrored_bldg_deaths.clear();
    g_host_actors_sent.clear();
    g_pairing.reset();
    g_driven.clear();
    g_last_playmode = nullptr;
    g_last_completed = -1;
    g_jump_queued_act = -1;
    g_jump_queued_at = -1e9;
}

static void on_session_end() { reset_adventure_state("session end"); }
LIFECYCLE_HOOK("adventure.session", lifecycle::kSessionEnd, -55, [](const lifecycle::Ctx&) { on_session_end(); }, false);

static void on_player_leave(uint8_t id) { g_host_visitors.erase(id); }
LIFECYCLE_HOOK("adventure.leave", lifecycle::kPlayerLeave, 0,
               [](const lifecycle::Ctx& c) { on_player_leave(c.player); }, false);

// ---------------- Goal Helpers ----------------
static uint8_t* goal_slot_ptr(void* pm, uint32_t slot, uint32_t* out_count = nullptr)
{
    if (!pm || !skg::readable(pm, 0x6C))
        return nullptr;
    uint8_t* beg = skg::field<uint8_t*>(pm, 0x64);
    uint8_t* end = skg::field<uint8_t*>(pm, 0x68);
    if (!beg || !end || end < beg)
        return nullptr;
    uint32_t n = (uint32_t)((uintptr_t)(end - beg) / kGoalStride);
    if (out_count)
        *out_count = n;
    if (slot >= n)
        return nullptr;
    uint8_t* g = beg + (uintptr_t)slot * kGoalStride;
    return skg::readable(g, 0x38) ? g : nullptr;
}

static bool read_goal_row(void* pm, void* goal, uint32_t act, wire::adventure::GoalData& out)
{
    if (!pm || !goal || !skg::readable(pm, 0x6C) || !skg::readable(goal, 0x38))
        return false;
    uint8_t* beg = skg::field<uint8_t*>(pm, 0x64);
    uint8_t* end = skg::field<uint8_t*>(pm, 0x68);
    auto* gp = static_cast<uint8_t*>(goal);
    if (!beg || !end || gp < beg || gp >= end)
        return false;
    uintptr_t off = (uintptr_t)(gp - beg);
    if (off % kGoalStride != 0)
        return false;
    uint32_t slot = (uint32_t)(off / kGoalStride);
    if (slot > wire::adventure::kMaxGoalSlot || act > wire::adventure::kMaxAct)
        return false;
    out.act = act;
    out.slot = slot;
    out.kind = skg::field<uint32_t>(gp, 0x24);
    out.target = skg::field<uint32_t>(gp, 0x28);
    out.secondary = skg::field<uint32_t>(gp, 0x2C);
    out.required = skg::field<uint32_t>(gp, 0x30);
    out.object_id = 0;
    out.count = (uint32_t)std::max(0, skg::field<int32_t>(gp, 0x04));
    return true;
}

static void apply_goal_credit_native(void* pm, const wire::adventure::GoalData& gd)
{
    if (!o_CreditGoal || !pm || !game::avatar())
        return;
    uint8_t* gp = goal_slot_ptr(pm, gd.slot);
    if (!gp)
        return;
    uint8_t done = skg::field<uint8_t>(gp, 0x00);
    int32_t have = skg::field<int32_t>(gp, 0x04);
    if (done != 0 || (gd.count > 0 && have >= (int32_t)gd.count))
        return; // already satisfied locally
    g_adv_applying = true;
    o_CreditGoal(pm, gp, nullptr, nullptr);
    g_adv_applying = false;
    g_goals_applied++;
}

static void queue_jump_to_act(int32_t act)
{
    if (!a_JumpToAct || act < 0 || act > (int32_t)wire::adventure::kMaxAct)
        return;
    if (g_jump_queued_act == act && g_now - g_jump_queued_at < 3.0)
        return;
    g_jump_queued_act = act;
    g_jump_queued_at = g_now;
    static int s_target_act = 0;
    s_target_act = act;
    H->run_outside_frame(
        [](void*) {
            void* pm = modes::adventure_play_mode();
            if (!pm || !skg::readable(pm, 0xBC))
                return;
            if (skg::field<int32_t>(pm, 0x90) != 3)
                return;
            if (skg::field<int32_t>(pm, 0xB8) >= s_target_act)
                return;
            g_adv_applying = true;
            skg::thiscall<void>(a_JumpToAct, pm, s_target_act);
            g_adv_applying = false;
            g_acts_applied++;
        },
        nullptr);
}

// ---------------- Native Hooks (E-A) ----------------
static void host_ensure_run(void* pm, int32_t act, int32_t state)
{
    wire::Key3 m = current_mission();
    if (!m.v[0] && !m.v[1] && !m.v[2])
        return;
    if (!g_host_journal.active() || g_host_journal.mission != m || g_last_playmode != pm) {
        g_host_journal.start_run(m, next_nonzero_nonce(g_run_nonce), act >= 0 ? (uint32_t)act : 0u);
        g_host_visitors.clear();
        g_host_actors_sent.clear();
        g_buildings.clear();
        g_local_bldg_deaths.clear();
        g_mirrored_bldg_deaths.clear();
        g_last_playmode = pm;
        if (H)
            H->log(M, SK_INFO, "adventure: host started run %08X mission=%08X/%08X act=%d state=%d",
                   g_host_journal.run_id, m.v[0], m.v[2], act, state);
    }
}

static void SK_THISCALL h_CreditGoal(void* play, void* goal, void* spatial1, void* spatial2)
{
    if (!game::on_main() || !o_CreditGoal) {
        if (o_CreditGoal)
            o_CreditGoal(play, goal, spatial1, spatial2);
        return;
    }
    if (g_adv_applying || g_in_set_act > 0 || !shared_enabled() || !connected() || !in_scenario()) {
        o_CreditGoal(play, goal, spatial1, spatial2);
        return;
    }
    void* pm = nullptr;
    int32_t state = -1, act = -1, completed = -1;
    if (!play_info(pm, state, act, completed) || pm != play || state != 3 || act < 0) {
        o_CreditGoal(play, goal, spatial1, spatial2);
        return;
    }

    wire::adventure::GoalData gd;
    if (!read_goal_row(pm, goal, (uint32_t)act, gd)) {
        o_CreditGoal(play, goal, spatial1, spatial2);
        return;
    }

    if (is_host()) {
        host_ensure_run(pm, act, state);
        int32_t before = skg::field<int32_t>(goal, 0x04);
        o_CreditGoal(play, goal, spatial1, spatial2);
        int32_t after = skg::field<int32_t>(goal, 0x04);
        if (after > before) {
            gd.count = (uint32_t)after;
            if (const JournalEntry* e = g_host_journal.commit_goal(gd)) {
                g_goals_sent++;
                send_journal_entry(0, *e, true);
                H->log(M, SK_INFO, "adventure: host committed goal seq=%u act=%u slot=%u kind=%u count=%u/%u",
                       e->seq, gd.act, gd.slot, gd.kind, gd.count, gd.required);
            }
        }
        return;
    }

    if (is_guest() && host_reads_adv() && g_guest_journal.run_id != 0) {
        // Defeat goals (kind 3) of a named spatial object must pass natively on the guest so Spore does not fail the
        // mission ("You can no longer complete this goal"). Host defeat is already authoritative via E-B / Death.
        if (should_pass_credit_natively(gd.kind, spatial1 != nullptr)) {
            o_CreditGoal(play, goal, spatial1, spatial2);
            return;
        }
        // Infinite-loop guard: callers such as ScenarioPlay_JumpToAct loop until goal->done/count advances within a
        // single frame. If the exact same (goal, count) is re-entered within the same tick, pass through natively.
        int32_t cur_count = skg::field<int32_t>(goal, 0x04);
        static double s_hold_t = -1.0;
        static void* s_hold_goal = nullptr;
        static int32_t s_hold_count = -1;
        if (s_hold_goal == goal && s_hold_count == cur_count && s_hold_t == g_now) {
            o_CreditGoal(play, goal, spatial1, spatial2);
            return;
        }
        s_hold_t = g_now;
        s_hold_goal = goal;
        s_hold_count = cur_count;

        // Hold non-defeat local credit and propose it to host (deduped so continuous triggers do not flood).
        gd.count = (uint32_t)(cur_count + 1);
        static uint32_t s_prop_run = 0, s_prop_act = 0, s_prop_slot = 0, s_prop_cnt = 0;
        static double s_prop_t = -10.0;
        bool dup = (s_prop_run == g_guest_journal.run_id && s_prop_act == gd.act && s_prop_slot == gd.slot &&
                    s_prop_cnt == gd.count && (g_now - s_prop_t) < 1.5);
        if (!dup) {
            s_prop_run = g_guest_journal.run_id;
            s_prop_act = gd.act;
            s_prop_slot = gd.slot;
            s_prop_cnt = gd.count;
            s_prop_t = g_now;
            wire::adventure::Propose msg{{g_guest_journal.mission, g_guest_journal.run_id}, gd};
            send_to_peer(0, wire::adventure::K_PROPOSE, msg, true);
            H->log(M, SK_INFO, "adventure: guest proposed goal act=%u slot=%u kind=%u count=%u/%u",
                   gd.act, gd.slot, gd.kind, gd.count, gd.required);
        }
        return;
    }

    o_CreditGoal(play, goal, spatial1, spatial2);
}

static void SK_THISCALL h_SetCurrentAct(void* play, int act, bool skip_setup)
{
    if (!game::on_main() || !o_SetCurrentAct) {
        if (o_SetCurrentAct)
            o_SetCurrentAct(play, act, skip_setup);
        return;
    }
    ++g_in_set_act;
    o_SetCurrentAct(play, act, skip_setup);
    --g_in_set_act;

    if (!shared_enabled() || !connected() || !in_scenario())
        return;
    void* pm = nullptr;
    int32_t state = -1, cur_act = -1, completed = -1;
    if (!play_info(pm, state, cur_act, completed) || pm != play || act < 0)
        return;

    if (is_host() && state == 3) {
        host_ensure_run(pm, act, state);
        const JournalEntry* e = nullptr;
        auto res = g_host_journal.commit_act((uint32_t)act, &e);
        if (res == HostJournal::ActResult::ResetNeeded) {
            // Adventure replayed: start a fresh run nonce
            g_host_journal.start_run(current_mission(), next_nonzero_nonce(g_run_nonce), (uint32_t)act);
            g_host_visitors.clear();
            g_host_actors_sent.clear();
            g_buildings.clear();
            g_local_bldg_deaths.clear();
            g_mirrored_bldg_deaths.clear();
            H->log(M, SK_INFO, "adventure: host replay detected (act %d), new run %08X", act, g_host_journal.run_id);
        } else if (res == HostJournal::ActResult::Committed && e) {
            g_acts_sent++;
            send_journal_entry(0, *e, true);
            H->log(M, SK_INFO, "adventure: host act -> %u (seq=%u)", e->act, e->seq);
        }
    } else if (is_guest()) {
        if (g_jump_queued_act == act)
            g_jump_queued_act = -1;
    }
}

static void SK_THISCALL h_SetState(void* play, int state)
{
    if (!game::on_main() || !o_SetState) {
        if (o_SetState)
            o_SetState(play, state);
        return;
    }
    if (g_adv_applying || !shared_enabled() || !connected() || !in_scenario()) {
        o_SetState(play, state);
        return;
    }
    void* pm = nullptr;
    int32_t prev_state = -1, act = -1, completed = -1;
    if (!play_info(pm, prev_state, act, completed) || pm != play) {
        o_SetState(play, state);
        return;
    }

    if (is_host()) {
        o_SetState(play, state);
        if (state == 3 && (prev_state == 5 || prev_state == 6)) {
            // Replay from win/loss screen
            g_host_journal.start_run(current_mission(), next_nonzero_nonce(g_run_nonce), act >= 0 ? (uint32_t)act : 0u);
            g_host_visitors.clear();
            g_host_actors_sent.clear();
            H->log(M, SK_INFO, "adventure: host reentered play from %d -> new run %08X", prev_state, g_host_journal.run_id);
        } else if (state == 5 || state == 6) {
            host_ensure_run(pm, act, state);
            if (const JournalEntry* e = g_host_journal.commit_outcome((uint8_t)state)) {
                g_outcomes_sent++;
                send_journal_entry(0, *e, true);
                H->log(M, SK_INFO, "adventure: host outcome=%u (seq=%u)", e->outcome_state, e->seq);
            }
        }
        return;
    }

    if (is_guest() && host_reads_adv()) {
        bool in_run = g_guest_journal.run_id != 0;
        bool has_outcome = g_guest_journal.outcome_applied != 0;
        if (should_hold_guest_state(state, in_run, has_outcome)) {
            g_guest_fail_held++;
            static uint32_t s_held_run = 0;
            static double s_held_log_t = -10.0;
            if (s_held_run != g_guest_journal.run_id || (g_now - s_held_log_t) >= 5.0) {
                s_held_run = g_guest_journal.run_id;
                s_held_log_t = g_now;
                H->log(M, SK_INFO, "adventure: held local guest SetState(6) while host run %08X is active", g_guest_journal.run_id);
            }
            return;
        }
    }

    o_SetState(play, state);
}

// ---------------- E-A Non-Creature Scenario Object Death Mirroring ----------------
static void kill_building_by_id(uint32_t obj_id, uint8_t want_deaths)
{
    uint8_t& done = g_mirrored_bldg_deaths[obj_id];
    while (done < want_deaths) {
        struct Pick {
            uint32_t obj_id;
            void* target;
            float hp;
        } pick{obj_id, nullptr, 0.f};
        game::for_each_object(
            [](void* obj, uint32_t noun, void* u) {
                auto* p = static_cast<Pick*>(u);
                if (noun != kBuildingNoun && !game::cast(obj, kBuildingType))
                    return true;
                uint32_t mh = 0;
                int32_t cls = -1;
                if (!read_scenario_identity(obj, mh, cls) || mh != p->obj_id)
                    return true;
                void* b = game::cast(obj, kBuildingType);
                if (!b || !skg::readable((uint8_t*)b + kBuildingCombatOff, 0x40))
                    return true;
                float hp = skg::field<float>((uint8_t*)b + kBuildingCombatOff, 0x38);
                if (hp > 0.f) {
                    p->target = b;
                    p->hp = hp;
                    return false;
                }
                return true;
            },
            &pick);
        if (!pick.target)
            break;
        void* comb = (uint8_t*)pick.target + kBuildingCombatOff;
        skg::Vector3 zero{0.f, 0.f, 0.f};
        skg::vcall<void>(comb, 0x18 / 4, pick.hp + 100000.f, 0xFFFFFFFFu, 0, &zero, (void*)nullptr);
        done++;
        g_deaths_mirrored++;
    }
}

static void poll_building_deaths(double now, int32_t state)
{
    if (!shared_enabled() || state != 3 || now - g_last_bldg_poll < kBuildingPollPeriod)
        return;
    g_last_bldg_poll = now;

    std::unordered_set<uint32_t> seen;
    struct Collect {
        std::unordered_set<uint32_t>* seen;
    } col{&seen};

    game::for_each_object(
        [](void* obj, uint32_t noun, void* u) {
            auto* c = static_cast<Collect*>(u);
            if (noun != kBuildingNoun && !game::cast(obj, kBuildingType))
                return true;
            uint32_t mh = 0;
            int32_t cls = -1;
            if (!read_scenario_identity(obj, mh, cls))
                return true;
            void* b = game::cast(obj, kBuildingType);
            if (!b || !skg::readable((uint8_t*)b + kBuildingCombatOff, 0x40))
                return true;
            uint32_t ser = game::object_serial(obj);
            if (!ser)
                return true;
            c->seen->insert(ser);
            float max_hp = skg::field<float>((uint8_t*)b + kBuildingCombatOff, 0x2C);
            float hp = skg::field<float>((uint8_t*)b + kBuildingCombatOff, 0x38);
            auto& w = g_buildings[ser];
            if (!w.ref) {
                w.ref = game::live_ref(obj);
                w.obj_id = mh;
                w.hp = hp;
                w.max_hp = max_hp;
                w.hurt = (max_hp > 0.f && hp < max_hp);
                w.dead_counted = (hp <= 0.f);
            } else {
                if (hp < w.hp)
                    w.hurt = true;
                if (!w.dead_counted && w.hp > 0.f && hp <= 0.f) {
                    w.dead_counted = true;
                    uint8_t& d = g_local_bldg_deaths[w.obj_id];
                    if (d < wire::adventure::kMaxDeaths)
                        d++;
                    if (is_host() && g_host_journal.active()) {
                        if (const JournalEntry* e = g_host_journal.commit_death(w.obj_id, d))
                            send_journal_entry(0, *e, true);
                    } else if (is_guest() && g_guest_journal.run_id != 0 && host_reads_adv()) {
                        wire::adventure::Death msg{{g_guest_journal.mission, g_guest_journal.run_id}, 0, w.obj_id, d};
                        send_to_peer(0, wire::adventure::K_DEATH, msg, true);
                    }
                }
                w.hp = hp;
                w.max_hp = max_hp;
            }
            return true;
        },
        &col);

    for (auto it = g_buildings.begin(); it != g_buildings.end();) {
        if (!seen.count(it->first)) {
            if (!it->second.dead_counted && it->second.hurt) {
                uint8_t& d = g_local_bldg_deaths[it->second.obj_id];
                if (d < wire::adventure::kMaxDeaths)
                    d++;
                if (is_host() && g_host_journal.active()) {
                    if (const JournalEntry* e = g_host_journal.commit_death(it->second.obj_id, d))
                        send_journal_entry(0, *e, true);
                } else if (is_guest() && g_guest_journal.run_id != 0 && host_reads_adv()) {
                    wire::adventure::Death msg{{g_guest_journal.mission, g_guest_journal.run_id}, 0, it->second.obj_id, d};
                    send_to_peer(0, wire::adventure::K_DEATH, msg, true);
                }
            }
            it = g_buildings.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------- E-B Host Partner Body Side (+0x30) ----------------
static void host_sync_body_sides()
{
    if (!is_host() || !g_body_side_fix || !host_actors_enabled() || !C.sess || !C.puppet_of)
        return;
    void* av = game::avatar();
    if (!av)
        return;
    uint32_t cap_pol = game::political_id(av);
    if (cap_pol == 0 || cap_pol == 0xFFFFFFFFu)
        return;
    for (const auto& p : C.sess->players()) {
        if (p.id == 0)
            continue;
        void* pup = C.puppet_of(p.id);
        if (!pup || !game::creature_listed(pup))
            continue;
        if (game::political_id(pup) != cap_pol)
            game::set_political_id(pup, cap_pol);
    }
}

// ---------------- E-B Host Actor Stream (10 Hz) ----------------
static void host_stream_actors(double now, int32_t state)
{
    if (!is_host() || !host_actors_enabled() || state != 3 || !g_host_journal.active())
        return;
    if (!any_guest_reads_adv())
        return;
    if (now - g_last_actor_send < kActorSendPeriod)
        return;
    g_last_actor_send = now;
    bool full = (now - g_last_actor_full >= kActorFullRefresh);
    if (full)
        g_last_actor_full = now;

    void* av = game::avatar();
    void* av_herd = game::avatar_herd();
    struct Gather {
        void* av;
        void* av_herd;
        bool full;
        double now;
        std::vector<wire::adventure::ActorSample> to_send;
        std::unordered_set<uint32_t> alive_keys;
    } g{av, av_herd, full, now, {}, {}};

    game::for_each_object(
        [](void* obj, uint32_t noun, void* u) {
            auto* g = static_cast<Gather*>(u);
            if (noun != kCreatureNoun)
                return true;
            uint32_t mh = 0;
            int32_t cls = -1;
            if (!is_scenario_creature(obj, g->av, g->av_herd, mh, cls))
                return true;
            uint32_t key = game::object_serial(obj);
            if (!key)
                return true;
            g->alive_keys.insert(key);
            net::Vec3 p = game::position(obj);
            net::Quat q = game::orientation(obj);
            net::Vec3 v = game::velocity(obj);
            float hp = game::health(obj);
            float mhp = game::max_health(obj);
            uint8_t flags = game::is_dead(obj) ? wire::adventure::kActorFlagDead : 0;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(q.x) ||
                !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w) || !std::isfinite(v.x) ||
                !std::isfinite(v.y) || !std::isfinite(v.z))
                return true;
            if (!std::isfinite(hp) || hp < 0.f)
                hp = 0.f;
            if (!std::isfinite(mhp) || mhp < 0.f)
                mhp = 0.f;
            hp = std::min(hp, wire::adventure::kMaxHealth);
            mhp = std::min(mhp, wire::adventure::kMaxHealth);

            auto& prev = g_host_actors_sent[key];
            float dx = p.x - prev.pos[0], dy = p.y - prev.pos[1], dz = p.z - prev.pos[2];
            bool moved = (dx * dx + dy * dy + dz * dz) > 0.0025f; // > 5 cm
            bool hp_changed = std::fabs(hp - prev.hp) > 0.05f;
            bool flag_changed = flags != prev.flags;
            if (!g->full && prev.sent_at > 0 && !moved && !hp_changed && !flag_changed)
                return true;

            wire::adventure::ActorSample s;
            s.host_key = key;
            s.marker_hash = mh;
            s.class_id = cls;
            s.pos[0] = p.x; s.pos[1] = p.y; s.pos[2] = p.z;
            s.rot[0] = q.x; s.rot[1] = q.y; s.rot[2] = q.z; s.rot[3] = q.w;
            s.vel[0] = v.x; s.vel[1] = v.y; s.vel[2] = v.z;
            s.hp = hp;
            s.max_hp = mhp;
            s.flags = flags;
            g->to_send.push_back(s);

            prev.pos[0] = p.x; prev.pos[1] = p.y; prev.pos[2] = p.z;
            prev.rot[0] = q.x; prev.rot[1] = q.y; prev.rot[2] = q.z; prev.rot[3] = q.w;
            prev.hp = hp;
            prev.flags = flags;
            prev.sent_at = g->now;
            return true;
        },
        &g);

    for (auto it = g_host_actors_sent.begin(); it != g_host_actors_sent.end();) {
        if (!g.alive_keys.count(it->first))
            it = g_host_actors_sent.erase(it);
        else
            ++it;
    }

    uint32_t tms = net::now_ms();
    wire::adventure::RunHeader hdr{g_host_journal.mission, g_host_journal.run_id};
    for (size_t i = 0; i < g.to_send.size(); i += wire::adventure::kMaxActorsPerBatch) {
        wire::adventure::ActorBatch batch;
        batch.hdr = hdr;
        batch.time_ms = tms;
        size_t end = std::min(g.to_send.size(), i + (size_t)wire::adventure::kMaxActorsPerBatch);
        batch.actors.assign(g.to_send.begin() + (ptrdiff_t)i, g.to_send.begin() + (ptrdiff_t)end);
        broadcast_adv(wire::adventure::K_ACTORS, batch, false);
        g_actor_batches_tx++;
    }
}

// ---------------- E-B Guest Driving, Pairing & Unpaired Cleanup ----------------
static void gather_local_candidates(std::vector<LocalCandidate>& out, std::unordered_map<uint32_t, game::LiveRef>& refs)
{
    out.clear();
    refs.clear();
    void* av = game::avatar();
    void* av_herd = game::avatar_herd();
    struct U {
        void* av;
        void* av_herd;
        std::vector<LocalCandidate>* out;
        std::unordered_map<uint32_t, game::LiveRef>* refs;
    } u{av, av_herd, &out, &refs};

    game::for_each_object(
        [](void* obj, uint32_t noun, void* p) {
            auto* u = static_cast<U*>(p);
            if (noun != kCreatureNoun)
                return true;
            uint32_t mh = 0;
            int32_t cls = -1;
            if (!is_scenario_creature(obj, u->av, u->av_herd, mh, cls))
                return true;
            uint32_t ser = game::object_serial(obj);
            if (!ser)
                return true;
            net::Vec3 pos = game::position(obj);
            LocalCandidate c;
            c.local_id = ser;
            c.marker_hash = mh;
            c.class_id = cls;
            c.pos[0] = pos.x; c.pos[1] = pos.y; c.pos[2] = pos.z;
            c.dead = game::is_dead(obj);
            u->out->push_back(c);
            (*u->refs)[ser] = game::live_ref(obj, kCreatureNoun);
            return true;
        },
        &u);
}

static void guest_drive_actors(double now, int32_t state)
{
    if (!is_guest() || !host_actors_enabled() || state != 3 || g_guest_journal.run_id == 0)
        return;

    std::vector<LocalCandidate> cands;
    std::unordered_map<uint32_t, game::LiveRef> refs;
    gather_local_candidates(cands, refs);

    std::unordered_set<uint32_t> live_ids;
    live_ids.reserve(cands.size());
    for (const auto& c : cands)
        live_ids.insert(c.local_id);
    g_pairing.prune_dead_locals(live_ids);

    // Drop stale driven entries whose stream stopped or whose local creature disappeared
    for (auto it = g_driven.begin(); it != g_driven.end();) {
        auto& d = it->second;
        if (now - d.last_rx > kStreamTimeoutSec) {
            g_pairing.unpair_host(it->first);
            it = g_driven.erase(it);
            continue;
        }
        void* c = game::live(d.ref);
        if (!c) {
            uint32_t lid = g_pairing.local_for_host(it->first);
            auto rit = lid ? refs.find(lid) : refs.end();
            if (rit != refs.end()) {
                d.ref = rit->second;
                c = game::live(d.ref);
            }
        }
        if (!c) {
            ++it;
            continue;
        }

        // Mirror host Die
        bool host_dead = (d.last_flags & wire::adventure::kActorFlagDead) != 0 || d.last_hp <= 0.f;
        if (host_dead) {
            if (!game::is_dead(c)) {
                game::place(c, d.last_pos, d.last_rot);
                float hp = game::health(c);
                g_in_damage_mirror = true;
                if (hp > 0.f)
                    game::apply_damage(c, hp + 100.f, 0, game::avatar());
                g_in_damage_mirror = false;
                if (!game::is_dead(c))
                    game::play_death(c);
                if (!d.death_mirrored) {
                    d.death_mirrored = true;
                    g_deaths_mirrored++;
                }
            } else {
                // Keep corpse at host's corpse spot if far
                if (net::dist(game::position(c), d.last_pos) > 1.0f)
                    game::place(c, d.last_pos, d.last_rot);
            }
            ++it;
            continue;
        }

        // Alive paired creature: sample interpolated state and drive with puppet/kinematic driver
        net::PlayerState s;
        if (d.buf.sample(now, s)) {
            net::Vec3 tgt = drive::out_of_ground(drive::on_own_ground(s.pos), true);
            drive::path_update(d.ds, tgt, now);
            net::Vec3 vel = s.vel;
            if (drive::speed(vel) <= 0.3f)
                vel = drive::path_velocity(d.ds);
            float sp = drive::speed(vel);
            if (sp > 0.3f && net::dist(game::position(c), tgt) < 8.f) {
                drive::walk_place(c, tgt, s.rot, vel, d.ds, now);
            } else {
                drive::walk_end(c, tgt, d.ds, now);
                drive::step(c, tgt, s.rot, vel, d.ds, now, 0, true, 4.f);
            }
            if (d.last_hp > 0.f && std::fabs(game::health(c) - d.last_hp) > 0.05f)
                game::set_health(c, d.last_hp);
        }
        ++it;
    }

    // E-B guest_extras: remove unpaired guest leftover creatures after 5 s
    if (g_guest_extras_fix) {
        auto expired = g_pairing.collect_expired_unpaired(now, cands, kUnpairedExpirySec);
        for (uint32_t lid : expired) {
            auto rit = refs.find(lid);
            void* c = rit != refs.end() ? game::live(rit->second) : nullptr;
            if (c && !game::is_dead(c)) {
                game::destroy_creature(c);
                g_pairing.unpaired_since.erase(lid);
                g_extras_removed++;
            }
        }
    }
}

// ---------------- E-A Guest Tick (Hello + Ordered Journal Replay) ----------------
static void guest_journal_tick(double now, void* pm, int32_t state, int32_t act)
{
    if (!is_guest() || !shared_enabled() || !host_reads_adv())
        return;
    wire::Key3 m = current_mission();
    if (!m.v[0] && !m.v[1] && !m.v[2])
        return;

    // Reset guest state if mission or playmode changed
    if (g_last_playmode != pm || (g_guest_journal.run_id != 0 && g_guest_journal.mission != m)) {
        g_guest_journal.reset();
        g_pairing.reset();
        g_driven.clear();
        g_buildings.clear();
        g_local_bldg_deaths.clear();
        g_mirrored_bldg_deaths.clear();
        g_last_playmode = pm;
        next_nonzero_nonce(g_visitor_nonce);
    }
    if (g_visitor_nonce == 0)
        next_nonzero_nonce(g_visitor_nonce);

    // Send K_HELLO at 1 Hz until synced (or if journal is behind host_seq)
    if (now - g_last_hello_sent >= kHelloPeriod) {
        g_last_hello_sent = now;
        wire::adventure::Hello msg;
        msg.hdr.mission = m;
        msg.hdr.run_id = g_guest_journal.run_id;
        msg.visitor_id = g_visitor_nonce;
        msg.last_seq = g_guest_journal.applied_seq;
        send_to_peer(0, wire::adventure::K_HELLO, msg, true);
    }

    if (state != 3 || act < 0)
        return;

    // Drain ready journal actions in strict sequence order
    for (int guard = 0; guard < 16; guard++) {
        auto a = g_guest_journal.next_action((uint32_t)act, (uint32_t)state);
        if (a.kind == GuestJournal::ActionKind::None)
            break;
        if (a.kind == GuestJournal::ActionKind::JumpToAct) {
            queue_jump_to_act((int32_t)a.target_act);
            break; // wait for JumpToAct to complete outside frame
        }
        if (a.kind == GuestJournal::ActionKind::ApplyGoal) {
            apply_goal_credit_native(pm, a.entry.goal);
        } else if (a.kind == GuestJournal::ActionKind::ApplyDeath) {
            kill_building_by_id(a.entry.object_id, a.entry.deaths);
        } else if (a.kind == GuestJournal::ActionKind::ApplyOutcome) {
            if (o_SetState) {
                g_adv_applying = true;
                o_SetState(pm, (int)a.entry.outcome_state);
                g_adv_applying = false;
                g_outcomes_applied++;
                H->log(M, SK_INFO, "adventure: guest applied host outcome=%u (seq=%u)", a.entry.outcome_state, a.seq);
            }
            break;
        }
    }
}

// ---------------- Message Handlers ----------------
void on_message(uint8_t from, net::Reader& r)
{
    if (!shared_enabled() || !connected() || !in_scenario())
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;

    void* pm = nullptr;
    int32_t state = -1, act = -1, completed = -1;
    if (!play_info(pm, state, act, completed))
        return;
    wire::Key3 m = current_mission();

    switch (kind) {
    case wire::adventure::K_HELLO: {
        wire::adventure::Hello msg;
        if (!is_host() || from == 0 || !msg.read(r) || msg.hdr.mission != m)
            return;
        host_ensure_run(pm, act, state);
        if (!g_host_journal.active())
            return;

        bool new_visitor = (g_host_visitors[from] != msg.visitor_id);
        g_host_visitors[from] = msg.visitor_id;

        wire::adventure::Welcome w;
        w.hdr = {g_host_journal.mission, g_host_journal.run_id};
        w.visitor_id = msg.visitor_id;
        w.act = g_host_journal.current_act;
        w.state = state >= 0 ? (uint32_t)state : 0u;
        w.seq = g_host_journal.next_seq;
        send_to_peer(from, wire::adventure::K_WELCOME, w, true);

        // Replay journal entries after guest's last_seq (0 if new run or new visitor)
        uint32_t since = (msg.hdr.run_id == g_host_journal.run_id && !new_visitor) ? msg.last_seq : 0u;
        auto replay = g_host_journal.entries_after(since);
        for (const auto& e : replay)
            send_journal_entry(from, e, false);
        if (!replay.empty() || new_visitor)
            H->log(M, SK_INFO, "adventure: host welcomed peer %u (run=%08X act=%u state=%u replayed=%zu entries)",
                   from, g_host_journal.run_id, w.act, w.state, replay.size());
        break;
    }
    case wire::adventure::K_WELCOME: {
        wire::adventure::Welcome msg;
        if (!is_guest() || from != 0 || !msg.read(r) || msg.hdr.mission != m)
            return;
        bool is_new = g_guest_journal.on_welcome(msg.hdr.mission, msg.hdr.run_id, msg.act, msg.state, msg.seq);
        if (is_new) {
            g_pairing.reset();
            g_driven.clear();
            H->log(M, SK_INFO, "adventure: guest joined host run %08X (host act=%u state=%u seq=%u)",
                   msg.hdr.run_id, msg.act, msg.state, msg.seq);
        }
        break;
    }
    case wire::adventure::K_PROPOSE: {
        wire::adventure::Propose msg;
        if (!is_host() || from == 0 || !msg.read(r) || !g_host_journal.active())
            return;
        if (msg.hdr.mission != g_host_journal.mission || msg.hdr.run_id != g_host_journal.run_id)
            return;
        if (state != 3 || act < 0 || msg.goal.act != (uint32_t)act)
            return;
        if (const JournalEntry* e = g_host_journal.commit_goal(msg.goal)) {
            g_goals_sent++;
            apply_goal_credit_native(pm, e->goal);
            send_journal_entry(0, *e, true);
            H->log(M, SK_INFO, "adventure: host accepted guest %u goal act=%u slot=%u count=%u (seq=%u)",
                   from, e->goal.act, e->goal.slot, e->goal.count, e->seq);
        }
        break;
    }
    case wire::adventure::K_COMMIT: {
        wire::adventure::Commit msg;
        if (!is_guest() || from != 0 || !msg.read(r))
            return;
        if (msg.hdr.mission != m || msg.hdr.run_id != g_guest_journal.run_id)
            return;
        JournalEntry e;
        e.seq = msg.seq;
        e.kind = EntryKind::Goal;
        e.goal = msg.goal;
        g_guest_journal.enqueue(e);
        break;
    }
    case wire::adventure::K_ACT: {
        wire::adventure::ActChange msg;
        if (!is_guest() || from != 0 || !msg.read(r))
            return;
        if (msg.hdr.mission != m || msg.hdr.run_id != g_guest_journal.run_id)
            return;
        JournalEntry e;
        e.seq = msg.seq;
        e.kind = EntryKind::Act;
        e.act = msg.act;
        g_guest_journal.enqueue(e);
        break;
    }
    case wire::adventure::K_OUTCOME: {
        wire::adventure::Outcome msg;
        if (!is_guest() || from != 0 || !msg.read(r))
            return;
        if (msg.hdr.mission != m || msg.hdr.run_id != g_guest_journal.run_id)
            return;
        JournalEntry e;
        e.seq = msg.seq;
        e.kind = EntryKind::Outcome;
        e.outcome_state = msg.state;
        g_guest_journal.enqueue(e);
        break;
    }
    case wire::adventure::K_DEATH: {
        wire::adventure::Death msg;
        if (!msg.read(r) || msg.hdr.mission != m)
            return;
        if (is_host() && from != 0 && g_host_journal.active() && msg.hdr.run_id == g_host_journal.run_id) {
            if (const JournalEntry* e = g_host_journal.commit_death(msg.object_id, msg.deaths)) {
                kill_building_by_id(e->object_id, e->deaths);
                send_journal_entry(0, *e, true);
            }
        } else if (is_guest() && from == 0 && msg.hdr.run_id == g_guest_journal.run_id) {
            JournalEntry e;
            e.seq = msg.seq;
            e.kind = EntryKind::Death;
            e.object_id = msg.object_id;
            e.deaths = msg.deaths;
            g_guest_journal.enqueue(e);
        }
        break;
    }
    case wire::adventure::K_HIT: {
        wire::adventure::Hit msg;
        if (!is_host() || from == 0 || !host_actors_enabled() || !g_hit_forward_fix || !msg.read(r))
            return;
        if (!g_host_journal.active() || msg.hdr.mission != g_host_journal.mission ||
            msg.hdr.run_id != g_host_journal.run_id || state != 3)
            return;

        // Locate target creature on host by host_key (serial +0x24)
        struct Find {
            uint32_t host_key;
            void* target;
        } find{msg.host_key, nullptr};
        game::for_each_object(
            [](void* obj, uint32_t noun, void* u) {
                auto* f = static_cast<Find*>(u);
                if (noun == kCreatureNoun && game::object_serial(obj) == f->host_key) {
                    f->target = obj;
                    return false;
                }
                return true;
            },
            &find);
        if (!find.target || game::is_dead(find.target))
            return;

        void* av = game::avatar();
        void* pup = C.puppet_of ? C.puppet_of(from) : nullptr;
        if (pup && !game::creature_listed(pup))
            pup = nullptr;
        float hp = game::health(find.target);
        // Pass host avatar on lethal hit so Spore credits defeat goals natively; pass partner puppet on non-lethal hits
        void* attacker = (av && hp > 0.f && hp <= msg.damage) ? av : (pup ? pup : av);
        game::apply_damage(find.target, msg.damage, msg.dmg_type, attacker);
        g_hits_applied++;
        break;
    }
    case wire::adventure::K_ACTORS: {
        wire::adventure::ActorBatch batch;
        if (!is_guest() || from != 0 || !host_actors_enabled() || !batch.read(r))
            return;
        if (batch.hdr.mission != m || batch.hdr.run_id != g_guest_journal.run_id || state != 3)
            return;

        double now = H ? H->time_seconds() : g_now;
        g_pairing.note_stream_rx(now);
        g_actor_batches_rx++;

        std::vector<LocalCandidate> cands;
        std::unordered_map<uint32_t, game::LiveRef> refs;
        bool gathered = false;

        for (const auto& s : batch.actors) {
            uint32_t lid = g_pairing.local_for_host(s.host_key);
            if (!lid) {
                if (!gathered) {
                    gather_local_candidates(cands, refs);
                    gathered = true;
                }
                lid = g_pairing.pair(s.host_key, s.marker_hash, s.class_id, s.pos, cands, kPairMaxDist);
            }
            if (!lid)
                continue;

            auto& d = g_driven[s.host_key];
            if (d.host_key != s.host_key) {
                d.host_key = s.host_key;
                d.marker_hash = s.marker_hash;
                d.class_id = s.class_id;
                d.buf.delay = 0.12;
                d.buf.lead = true;
                d.buf.late_lead = true;
                d.buf.smooth_max = 8.f;
                d.death_mirrored = false;
            }
            if (!game::alive(d.ref)) {
                if (!gathered) {
                    gather_local_candidates(cands, refs);
                    gathered = true;
                }
                auto rit = refs.find(lid);
                if (rit != refs.end())
                    d.ref = rit->second;
            }
            d.last_hp = s.hp;
            d.last_max_hp = s.max_hp;
            d.last_flags = s.flags;
            d.last_pos = {s.pos[0], s.pos[1], s.pos[2]};
            d.last_rot = {s.rot[0], s.rot[1], s.rot[2], s.rot[3]};
            d.last_rx = now;

            net::PlayerState ps;
            ps.time_ms = batch.time_ms;
            ps.pos = d.last_pos;
            ps.rot = d.last_rot;
            ps.vel = game::finite_vel({s.vel[0], s.vel[1], s.vel[2]});
            ps.health = s.hp;
            ps.max_health = s.max_hp;
            ps.flags = (s.flags & wire::adventure::kActorFlagDead) ? net::PF_DEAD : 0;
            d.buf.push(ps, now);
        }
        break;
    }
    }
}

// ---------------- Cross-Module Hooks ----------------
bool should_skip_ai(void* creature)
{
    if (!creature || !is_guest() || !host_actors_enabled() || !connected() || !in_scenario())
        return false;
    if (g_guest_journal.run_id == 0 || !g_pairing.stream_active(g_now))
        return false;
    uint32_t ser = game::object_serial(creature);
    return ser != 0 && g_pairing.is_local_paired(ser);
}

bool host_ai_targets_partners()
{
    return is_host() && host_actors_enabled() && g_body_side_fix && connected() && in_scenario() &&
           g_host_journal.active();
}

bool damage_filter(void* creature, float& damage, int type, void* attacker)
{
    if (!creature || !in_scenario() || !connected() || !host_actors_enabled())
        return true;
    if (g_in_damage_mirror)
        return true; // let our own Die mirror damage apply locally

    if (is_guest()) {
        uint32_t ser = game::object_serial(creature);
        uint32_t host_key = ser ? g_pairing.host_for_local(ser) : 0;
        if (!host_key || !g_pairing.stream_active(g_now))
            return true; // unpaired or stream inactive: native damage

        void* av = game::avatar();
        void* av_herd = game::avatar_herd();
        bool from_player_side = (attacker == av) || (attacker && av_herd && game::herd_of(attacker) == av_herd);
        if (g_hit_forward_fix && from_player_side && damage > 0.f && std::isfinite(damage)) {
            uint32_t mh = 0;
            int32_t cls = -1;
            read_scenario_identity(creature, mh, cls);
            if (cls >= 0) {
                wire::adventure::Hit msg;
                msg.hdr = {g_guest_journal.mission, g_guest_journal.run_id};
                msg.marker_hash = mh;
                msg.class_id = cls;
                msg.host_key = host_key;
                msg.damage = std::min(damage, wire::adventure::kMaxHitDamage);
                msg.dmg_type = type;
                send_to_peer(0, wire::adventure::K_HIT, msg, true);
                g_hits_forwarded++;
            }
        }
        // Drop local damage on paired guest adventure creatures: host is authoritative
        return false;
    }
    return true;
}

// ---------------- Frame Tick & Init ----------------
void tick(double now)
{
    g_now = now;
    if (!connected() || !in_scenario()) {
        if (g_host_journal.active() || g_guest_journal.run_id != 0 || !g_driven.empty())
            reset_adventure_state("left scenario");
        return;
    }

    void* pm = nullptr;
    int32_t state = -1, act = -1, completed = -1;
    if (!play_info(pm, state, act, completed))
        return;

    if (is_host() && shared_enabled()) {
        // Detect replay via completed count dropping
        if (g_last_playmode == pm && g_last_completed >= 0 && completed >= 0 && completed < g_last_completed &&
            state == 3) {
            g_host_journal.start_run(current_mission(), next_nonzero_nonce(g_run_nonce), act >= 0 ? (uint32_t)act : 0u);
            g_host_visitors.clear();
            g_host_actors_sent.clear();
            H->log(M, SK_INFO, "adventure: host completed count dropped (%d -> %d), new run %08X",
                   g_last_completed, completed, g_host_journal.run_id);
        }
        if (state == 3)
            host_ensure_run(pm, act, state);
        g_last_completed = completed;

        host_sync_body_sides();
        host_stream_actors(now, state);
        poll_building_deaths(now, state);
    } else if (is_guest() && shared_enabled()) {
        guest_journal_tick(now, pm, state, act);
        guest_drive_actors(now, state);
        poll_building_deaths(now, state);
    }
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_JumpToAct = H->sym("SporeCoop::ScenarioPlay_JumpToAct");
    if (uintptr_t a = H->sym("SporeCoop::ScenarioCreditGoal"))
        H->hook_create(M, (void*)a, (void*)h_CreditGoal, (void**)&o_CreditGoal);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioPlay_SetCurrentAct"))
        H->hook_create(M, (void*)a, (void*)h_SetCurrentAct, (void**)&o_SetCurrentAct);
    if (uintptr_t a = H->sym("SporeCoop::ScenarioPlay_SetState"))
        H->hook_create(M, (void*)a, (void*)h_SetState, (void**)&o_SetState);
    game::set_ai_skip(should_skip_ai);
}

// ---------------- Console Command (`coop.adventure` / `coop.adv`) ----------------
std::string command(const std::vector<std::string>& args)
{
    // `place <hex>` works even before entering an adventure (used by tests)
    if (args.size() >= 2 && args[0] == "place") {
        uint32_t id = (uint32_t)strtoul(args[1].c_str(), nullptr, 16);
        modes::adventure_force_place(id);
        char buf[128];
        snprintf(buf, sizeof buf, "adventure place forced to %08X\n", id);
        return buf;
    }

    void* pm = nullptr;
    int32_t state = -1, act = -1, completed = -1;
    if (!play_info(pm, state, act, completed))
        return "no adventure";

    uint32_t ki = 0, kg = 0;
    modes::adventure_key(ki, kg);
    uint32_t place = modes::current_place(modes::for_mode(modes::kScenario));

    char head[256];
    snprintf(head, sizeof head, "adventure playmode=%p state=%d act=%d key=%08X/%08X place=%08X\n",
             pm, state, act, ki, kg, place);

    if (args.empty() || args[0] == "status") {
        uint32_t run = is_host() ? g_host_journal.run_id : g_guest_journal.run_id;
        uint32_t seq = is_host() ? g_host_journal.next_seq : g_guest_journal.applied_seq;
        uint32_t ngoals = 0;
        goal_slot_ptr(pm, 0, &ngoals);
        char buf[640];
        snprintf(buf, sizeof buf,
                 "%s  shared=%d host_actors=%d hit_fwd=%d extras=%d body_side=%d role=%s run=%08X seq=%u completed=%d "
                 "goals=%u paired=%zu stream=%d tx=%u rx=%u gsent=%u gapp=%u asent=%u aapp=%u osent=%u oapp=%u "
                 "hit_fwd=%u hit_app=%u died=%u extra_rm=%u fail_held=%u\n",
                 head, (int)shared_enabled(), (int)g_host_actors_fix, (int)g_hit_forward_fix, (int)g_guest_extras_fix,
                 (int)g_body_side_fix, is_host() ? "host" : is_guest() ? "guest" : "solo", run, seq, completed, ngoals,
                 g_pairing.host_to_local.size(), (int)g_pairing.stream_active(g_now), g_actor_batches_tx,
                 g_actor_batches_rx, g_goals_sent, g_goals_applied, g_acts_sent, g_acts_applied, g_outcomes_sent,
                 g_outcomes_applied, g_hits_forwarded, g_hits_applied, g_deaths_mirrored, g_extras_removed,
                 g_guest_fail_held);
        return buf;
    }

    if (args[0] == "act" && args.size() >= 2 && a_JumpToAct) {
        static int s_act = 0;
        s_act = atoi(args[1].c_str());
        H->run_outside_frame(
            [](void*) {
                void* pm2 = modes::adventure_play_mode();
                if (!pm2)
                    return;
                bool apply_guard = is_guest();
                if (apply_guard)
                    g_adv_applying = true;
                skg::thiscall<void>(a_JumpToAct, pm2, s_act);
                if (apply_guard)
                    g_adv_applying = false;
            },
            nullptr);
        return std::string(head) + "jump to act " + std::to_string(s_act) + " queued\n";
    }

    if (args[0] == "goals") {
        std::string out = head;
        uint32_t n = 0;
        goal_slot_ptr(pm, 0, &n);
        for (uint32_t i = 0; i < n && i <= wire::adventure::kMaxGoalSlot; i++) {
            uint8_t* gp = goal_slot_ptr(pm, i);
            if (!gp)
                continue;
            char ln[192];
            snprintf(ln, sizeof ln, "  goal slot=%u done=%u count=%d/%u kind=%u target=%u secondary=%u\n",
                     i, (unsigned)skg::field<uint8_t>(gp, 0x00), skg::field<int32_t>(gp, 0x04),
                     skg::field<uint32_t>(gp, 0x30), skg::field<uint32_t>(gp, 0x24),
                     skg::field<uint32_t>(gp, 0x28), skg::field<uint32_t>(gp, 0x2C));
            out += ln;
        }
        return out;
    }

    if (args[0] == "actors") {
        std::string out = head;
        std::vector<LocalCandidate> cands;
        std::unordered_map<uint32_t, game::LiveRef> refs;
        gather_local_candidates(cands, refs);
        int paired = 0, alive = 0;
        for (const auto& c : cands) {
            void* obj = game::live(refs[c.local_id]);
            if (!obj)
                continue;
            if (!c.dead)
                alive++;
            uint32_t hk = g_pairing.host_for_local(c.local_id);
            if (hk)
                paired++;
            float hp = game::health(obj), mhp = game::max_health(obj);
            bool inv = (skg::readable((uint8_t*)obj + 0x5DC, 4) && *(uint32_t*)((uint8_t*)obj + 0x5DC) == 2u) ||
                       (skg::readable((uint8_t*)obj + 0x628, 1) && *(uint8_t*)((uint8_t*)obj + 0x628) != 0) ||
                       (skg::readable((uint8_t*)obj + 0xB58, 4) && ((*(uint32_t*)((uint8_t*)obj + 0xB58) & 0x200u) != 0));
            uint32_t pol = game::political_id(obj);
            double unp = -1.0;
            auto uit = g_pairing.unpaired_since.find(c.local_id);
            if (uit != g_pairing.unpaired_since.end())
                unp = g_now - uit->second;
            char ln[256];
            snprintf(ln, sizeof ln,
                     "  actor serial=%u marker=%08X class=%d pos=(%.2f %.2f %.2f) hp=%.1f/%.1f dead=%d host_key=%u ai_skip=%d unp=%.1f inv=%d pol=%08X\n",
                     c.local_id, c.marker_hash, c.class_id, c.pos[0], c.pos[1], c.pos[2], hp, mhp, (int)c.dead, hk,
                     (int)should_skip_ai(obj), unp, (int)inv, pol);
            out += ln;
        }
        char sum[128];
        snprintf(sum, sizeof sum, "  total=%zu alive=%d paired=%d\n", cands.size(), alive, paired);
        out += sum;
        return out;
    }

    if (args[0] == "bodies") {
        std::string out = head;
        if (void* av = game::avatar()) {
            net::Vec3 p = game::position(av);
            char ln[192];
            snprintf(ln, sizeof ln, "  avatar serial=%u pol=%08X scale=%.3f hp=%.1f/%.1f pos=(%.2f %.2f %.2f)\n",
                     game::object_serial(av), game::political_id(av), game::scale(av), game::health(av),
                     game::max_health(av), p.x, p.y, p.z);
            out += ln;
        }
        if (C.sess && C.puppet_of) {
            for (const auto& pl : C.sess->players()) {
                if (pl.id == C.sess->my_id())
                    continue;
                void* pup = C.puppet_of(pl.id);
                if (!pup || !game::creature_listed(pup))
                    continue;
                net::Vec3 p = game::position(pup);
                char ln[192];
                snprintf(ln, sizeof ln, "  puppet id=%u serial=%u pol=%08X scale=%.3f hp=%.1f/%.1f pos=(%.2f %.2f %.2f)\n",
                         pl.id, game::object_serial(pup), game::political_id(pup), game::scale(pup), game::health(pup),
                         game::max_health(pup), p.x, p.y, p.z);
                out += ln;
            }
        }
        return out;
    }

    if (args[0] == "credit" && args.size() >= 2 && o_CreditGoal) {
        uint32_t slot = (uint32_t)strtoul(args[1].c_str(), nullptr, 10);
        uint8_t* gp = goal_slot_ptr(pm, slot);
        if (!gp)
            return std::string(head) + "no goal slot " + args[1] + "\n";
        // Calls through our hook so host commits or guest proposes!
        h_CreditGoal(pm, gp, nullptr, nullptr);
        char ln[128];
        snprintf(ln, sizeof ln, "credited slot=%u count=%d/%u done=%u\n", slot, skg::field<int32_t>(gp, 0x04),
                 skg::field<uint32_t>(gp, 0x30), (unsigned)skg::field<uint8_t>(gp, 0x00));
        return std::string(head) + ln;
    }

    if (args[0] == "state" && args.size() >= 2 && o_SetState) {
        int st = atoi(args[1].c_str());
        h_SetState(pm, st);
        char ln[96];
        snprintf(ln, sizeof ln, "requested state=%d actual_state=%d\n", st, skg::field<int32_t>(pm, 0x90));
        return std::string(head) + ln;
    }

    if (args[0] == "vulnerable" && args.size() >= 2) {
        uint32_t id = (uint32_t)strtoul(args[1].c_str(), nullptr, 10);
        std::vector<LocalCandidate> cands;
        std::unordered_map<uint32_t, game::LiveRef> refs;
        gather_local_candidates(cands, refs);
        void* target = nullptr;
        uint32_t ser = 0;
        for (const auto& c : cands) {
            if (c.dead)
                continue;
            uint32_t hk = g_pairing.host_for_local(c.local_id);
            if (c.local_id == id || hk == id) {
                target = game::live(refs[c.local_id]);
                ser = c.local_id;
                if (target)
                    break;
            }
        }
        if (!target)
            return std::string(head) + "no living actor matching " + args[1] + "\n";
        if (skg::readable((uint8_t*)target + 0x628, 1))
            *(uint8_t*)((uint8_t*)target + 0x628) = 0;
        if (skg::readable((uint8_t*)target + 0x5DC, 4) && *(uint32_t*)((uint8_t*)target + 0x5DC) == 2u)
            *(uint32_t*)((uint8_t*)target + 0x5DC) = 0;
        if (skg::readable((uint8_t*)target + 0xB58, 4))
            *(uint32_t*)((uint8_t*)target + 0xB58) &= ~0x200u;
        char ln[128];
        snprintf(ln, sizeof ln, "vulnerable serial=%u hp=%.1f\n", ser, game::health(target));
        return std::string(head) + ln;
    }

    if (args[0] == "hit" && args.size() >= 3) {
        uint32_t id = (uint32_t)strtoul(args[1].c_str(), nullptr, 10);
        float dmg = (float)atof(args[2].c_str());
        std::vector<LocalCandidate> cands;
        std::unordered_map<uint32_t, game::LiveRef> refs;
        gather_local_candidates(cands, refs);
        void* target = nullptr;
        uint32_t hit_serial = 0, hit_host = 0;
        for (const auto& c : cands) {
            if (c.dead)
                continue;
            uint32_t hk = g_pairing.host_for_local(c.local_id);
            if (c.local_id == id || hk == id || (uint32_t)c.class_id == id) {
                target = game::live(refs[c.local_id]);
                hit_serial = c.local_id;
                hit_host = hk;
                if (target)
                    break;
            }
        }
        if (!target)
            return std::string(head) + "no living actor matching " + args[1] + "\n";
        float before = game::health(target);
        game::hit(target, dmg, 0, game::avatar());
        float after = game::health(target);
        char ln[160];
        snprintf(ln, sizeof ln, "hit serial=%u host_key=%u dmg=%.1f hp=%.1f->%.1f\n", hit_serial, hit_host, dmg, before,
                 after);
        return std::string(head) + ln;
    }

    if (args[0] == "kill" && args.size() >= 2) {
        uint32_t id = (uint32_t)strtoul(args[1].c_str(), nullptr, 10);
        std::vector<LocalCandidate> cands;
        std::unordered_map<uint32_t, game::LiveRef> refs;
        gather_local_candidates(cands, refs);
        void* target = nullptr;
        uint32_t ser = 0;
        for (const auto& c : cands) {
            if (c.dead)
                continue;
            uint32_t hk = g_pairing.host_for_local(c.local_id);
            if (c.local_id == id || hk == id || (uint32_t)c.class_id == id) {
                target = game::live(refs[c.local_id]);
                ser = c.local_id;
                if (target)
                    break;
            }
        }
        if (!target)
            return std::string(head) + "no living actor matching " + args[1] + "\n";
        float hp = game::health(target);
        game::apply_damage(target, hp + 1000.f, 0, game::avatar());
        if (!game::is_dead(target))
            game::play_death(target);
        char ln[128];
        snprintf(ln, sizeof ln, "killed serial=%u dead=%d hp=%.1f\n", ser, (int)game::is_dead(target), game::health(target));
        return std::string(head) + ln;
    }

    if (args[0] == "spawn_extra") {
        // Test helper: spawn an unpaired scenario creature with an existing scenario creature's species/class/marker
        // (or avatar's species + class 0 in its own herd) so tests can verify adventure.guest_extras 5 s expiry.
        void* av = game::avatar();
        if (!av)
            return std::string(head) + "no avatar\n";
        std::vector<LocalCandidate> cands;
        std::unordered_map<uint32_t, game::LiveRef> refs;
        gather_local_candidates(cands, refs);
        void* tmpl = nullptr;
        int32_t cls = 0;
        void* marker = nullptr;
        for (const auto& c : cands) {
            if (void* o = game::live(refs[c.local_id])) {
                tmpl = o;
                cls = c.class_id;
                marker = skg::field<void*>(o, kScenarioMarkerOff);
                break;
            }
        }
        void* sp = tmpl ? game::species_profile(tmpl) : game::species_profile(av);
        void* herd = tmpl ? game::herd_of(tmpl) : game::create_herd(game::position(av), sp);
        net::Vec3 p = game::position(av);
        p.x += 20.f; // > 12 m away so it stays unpaired
        void* c = (sp && herd) ? game::spawn_creature(p, sp, herd) : nullptr;
        if (!c)
            return std::string(head) + "spawn_extra failed\n";
        skg::field<void*>(c, kScenarioMarkerOff) = marker;
        skg::field<int32_t>(c, kScenarioClassOff) = cls;
        char ln[128];
        snprintf(ln, sizeof ln, "spawned extra serial=%u class=%d pos=(%.2f %.2f %.2f)\n",
                 game::object_serial(c), cls, p.x, p.y, p.z);
        return std::string(head) + ln;
    }

    return std::string(head) +
           "usage: coop.adventure [status | goals | actors | bodies | act <n> | place <hex> | credit <slot> | "
           "state <5|6> | hit <id> <dmg> | kill <id> | spawn_extra]\n";
}

} // namespace adventure
