// D3 of docs/plan-0.7.15.md (audit STG-8, TST-9): the Tribe stage's rules as functions of plain inputs, so they are
// unit tested (tests/tribe_tests.cpp, `make test`) instead of only by the lab. tribe.cpp keeps the game calls and asks
// these functions what to do:
//
//   owner dealing    (host) the owner of a member the table meets first, the split when the players change, the
//                    even share of a newcomer (E1, BUG-1154: claims; BUG-0671: a member bought for a guest)
//   member hold      (host) an entry the tribes' member lists no longer have: its body is kept and sent while the
//                    game keeps it dead (BUG-0620), else it goes
//   snapshot apply   (guest) per entry of the host's snapshot: grow the mirror up (BUG-0857), make a mirror for it or
//                    not (a body: BUG-0665), which member of this game to bind (the nearest unbound within 6 m) or
//                    spawn; after the snapshot (BUG-0173): a dead mirror plays the death or is made again
//
// Portable (no Windows, no SporeKit, no game memory). docs/01-architecture.md#arch-mirror-registry (C1) and
// docs/re/tribe.md.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace tribe_logic {

constexpr uint8_t kNobody = 0xFE; // a member no player owns (an NPC tribe's, the player tribe's with no player)

// ---------------- owner dealing (host) ----------------

// the owner of a member the host's table meets for the first time.
//   player_tribe   it is a member of the player tribe (the others belong to nobody)
//   claim          its claim's holder (E1: the present player of its stable key, the host for an absent one), -1 none
//   players        the session's player ids (sorted), the host's own included
//   turn           the dealing counter: the player tribe's members without a claim go to the players in turns
//   born_for       the guest it was bought for (BUG-0671), -1 none; born_for_player: that guest is still a player
inline uint8_t new_owner(bool player_tribe, int claim, const std::vector<uint8_t>& players, uint32_t& turn, int born_for,
                         bool born_for_player)
{
    uint8_t owner = claim >= 0                            ? (uint8_t)claim
                    : player_tribe && !players.empty() ? players[turn++ % players.size()]
                                                       : kNobody;
    if (born_for >= 0 && player_tribe && born_for_player)
        owner = (uint8_t)born_for;
    return owner;
}

// the players changed (a join, a leave, a new session): a member of the player tribe goes to its claim's holder, the
// others in turns
inline uint8_t resplit_owner(int claim, const std::vector<uint8_t>& players, uint32_t& turn)
{
    if (claim >= 0)
        return (uint8_t)claim;
    return players.empty() ? kNobody : players[turn++ % players.size()];
}

// E1 (BUG-1154): a player here without a member gets an even share (total / players) from the player with the most,
// one at a time, its plain members first (`plain(m)`: not born for its player, not a gift), never taking the giver
// below the share. `by`: owner -> its members in member order (updated); returns the moves (member, new owner).
// BUG-1450: `fixed(m)` members never move (the game's leader, the host's chief with the staff: the first plain member
// in member order, it went to a guest whose claims were lost at its rejoin); a giver with only fixed members left
// gives no more. The 4-argument form moves any member (0.7.16).
template <class M, class Plain, class Fixed>
std::vector<std::pair<M, uint8_t>> balance(std::map<uint8_t, std::vector<M>>& by, const std::vector<uint8_t>& players,
                                           size_t total, Plain plain, Fixed fixed)
{
    std::vector<std::pair<M, uint8_t>> moves;
    if (players.empty())
        return moves;
    size_t share = total / players.size();
    for (uint8_t p : players) {
        bool newcomer = by[p].empty();
        while (newcomer && by[p].size() < share) {
            uint8_t rich = p;
            for (uint8_t q : players)
                if (by[q].size() > by[rich].size())
                    rich = q;
            if (rich == p || by[rich].size() <= share)
                break;
            auto& v = by[rich];
            auto pick = v.end();
            for (auto it = v.begin(); it != v.end(); ++it)
                if (!fixed(*it) && plain(*it)) {
                    pick = it;
                    break;
                }
            for (auto it = v.begin(); pick == v.end() && it != v.end(); ++it)
                if (!fixed(*it))
                    pick = it;
            if (pick == v.end())
                break;
            M m = *pick;
            v.erase(pick);
            by[p].push_back(m);
            moves.push_back({m, p});
        }
    }
    return moves;
}
template <class M, class Plain>
std::vector<std::pair<M, uint8_t>> balance(std::map<uint8_t, std::vector<M>>& by, const std::vector<uint8_t>& players,
                                           size_t total, Plain plain)
{
    return balance(by, players, total, plain, [](const M&) { return false; });
}

// ---------------- member hold (host) ----------------

constexpr double kBodyMax = 60.0; // s: a body is sent at most this long (BUG-0620)
constexpr int kMaxTribes = 32;    // the snapshot's tribe table

// BUG-0620: an entry of the host's table that no tribe's member list has now. Kept (its body is sent with its last
// tribe, health 0) while its object is the member, alive in the game's list and dead, its last tribe is one of the
// snapshot's tribes and it lies there at most kBodyMax s; `since` is when its body was first seen (0: not yet)
struct BodyHold {
    bool keep = false;
    double since = 0;
};
inline BodyHold body_hold(bool alive, bool dead, int tribe_index, double since, double now, double body_max = kBodyMax)
{
    if (!alive || !dead || tribe_index < 0 || tribe_index >= kMaxTribes)
        return {false, since};
    if (since == 0)
        since = now;
    return {now - since <= body_max, since};
}

// ---------------- snapshot apply (guest) ----------------

// BUG-0857: a bound mirror still a baby whose host member is an adult grows up (the citizen's own GrowUp)
inline bool grow_up(bool fix, uint8_t host_age, bool body, uint8_t mirror_age, bool mirror_dead)
{
    return fix && host_age == 1 && !body && mirror_age == 0 && !mirror_dead;
}

// an entry without a mirror gets one; a body only with tribe.join_body (BUG-0665)
inline bool make_mirror(bool has_mirror, bool body, bool join_body_fix)
{
    return !has_mirror && (!body || join_body_fix);
}

// the member of this game a new mirror binds to: the nearest unbound member of the mapped tribe nearer than `max_d`
// (m) to the host's position; -1: none, a member is spawned
struct Candidate {
    float x = 0, y = 0, z = 0;
    bool bound = false; // a mirror of another host member already
};
constexpr float kBindMax = 6.f;
inline int pick_bind(const std::vector<Candidate>& v, float x, float y, float z, float max_d = kBindMax)
{
    int best = -1;
    float bd = max_d;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].bound)
            continue;
        float dx = v[i].x - x, dy = v[i].y - y, dz = v[i].z - z;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < bd) {
            bd = d;
            best = (int)i;
        }
    }
    return best;
}

// BUG-0173 (tribe.ko): a mirror whose host member has no health plays the death (and stays a body); a mirror dead
// here whose host member is back on its feet is made again (the next snapshot binds or spawns it)
enum class Ko : uint8_t { None, Death, Replace };
inline Ko ko(bool fix, float host_hp, bool mirror_dead)
{
    if (!fix)
        return Ko::None;
    if (host_hp <= 0)
        return Ko::Death;
    return mirror_dead ? Ko::Replace : Ko::None;
}

// ---------------- own species (every game) ----------------

// BUG-1271 (WP-T1 of docs/plan-0.7.16.md): what a member of the player tribe needs on this game. `line` is its owner's
// own species key (0: none: the host, a player without an own creature, creature.mode host, the switch off), `cur` the
// member's species key (+0xB28), `slot` the tribe's (its species slot +0x504), `ours` the key the mod gave it last
// (0: never touched by the mod), `ready` the line's species can be built here now. A member the mod never touched keeps
// whatever species it has when it needs the slot's (a test's `coop.tribe setspecies`, the game's own).
enum class SpeciesAct : uint8_t { Keep, Apply, Revert, Wait };
inline SpeciesAct species_act(uint32_t line, uint32_t cur, uint32_t slot, uint32_t ours, bool ready)
{
    if (line && line != slot) {
        if (cur == line)
            return SpeciesAct::Keep;
        return ready ? SpeciesAct::Apply : SpeciesAct::Wait;
    }
    return ours && cur == ours && cur != slot ? SpeciesAct::Revert : SpeciesAct::Keep;
}

// ---------------- member mirror driving (guest) ----------------

// BUG-0619 / BUG-0651 / BUG-1235: WalkPlace::lead_speed of a walked member mirror, the body's own speed (0: no bound).
// What a frame draws is the placing's step plus the game's walk of the next frame at the walk speed `sp`; walk_place
// holds the placing to (2 x lead_speed + catch_up - sp) x dt, so a frame draws at most 2 x the body's own speed +
// catch_up. `lead_sp` is the raw lead's speed when the smoothed target drives the mirror (-1: the raw lead drives it),
// `late_rate` the late lead's catch-up rate (1: none). While the late lead catches up the raw lead's velocity runs at
// that rate (BUG-0650): 9 m/s x 1.5 = 13.4-13.7 m/s.
// * the raw lead (lead_sp < 0) while it catches up: sp / late_rate (drive.lead_keep, BUG-0651).
// * BUG-1235 (`coop.fix tribe.smooth_bound`): the smoothed target (lead_sp >= 0, after a walk start: the switch from the
//   interpolated stand goes through the smoothing until its offset is closed): lead_sp / late_rate. BUG-0619 bounded
//   this path by lead_sp; the BUG-0651 change (1fcf09b2) replaced that with the raw path's bound only, so a walk start
//   whose late state was caught up in the smoothed path walked at 13.4-13.7 m/s with the placing at (sp + 3 m/s) x dt
//   on top: 0.95 m frames at 30 fps (tribe_walk_player 20261121-003202 member 3 at 23.55 s, 20261119-214823 member 7
//   at 28.49 s, 20261117-154911 member 2: placing 0.52 m + walk 0.43 m; the test's limit 3 x 9 m/s x 32 ms = 0.86 m).
//   Off: no bound in the smoothed path (0.7.16 rc).
// * BUG-1451 (`coop.fix tribe.smooth_catch`, with smooth_bound): the bound follows the host body's speed NOW, so a copy
//   that started its walk late (a latency spike of the player network) and stands `lag` m behind its aim while the host
//   slows down was held to (2 x 1.6-5 + 3) m/s and crept 3.1-3.6 m behind for over 1 s (body_trail_tribe_player
//   20261122-194210 t1: 1.16 s over 2 m, limit 1.0). The bound is at least a catch-up speed that grows with the lag
//   (lag / kLagCatchTime), capped at kLagCatchMax: a frame then draws at most (2 x 9 + 3) m/s x dt, the bound BUG-1235
//   itself gives the lab's 9 m/s member, and its case (1-2 m behind: 2-4 m/s) keeps the body's own speed.
//   Off: the body's speed only (fbdd2510).
constexpr float kLagCatchTime = 0.5f; // s: the lag is closed in about this (on top of the walk)
constexpr float kLagCatchMax = 9.f;   // m/s: what BUG-1235's bound allows the lab's member (frames 0.67 m at 32 ms)
// * BUG-1235 / item 12 of docs/plan-0.7.17.md (`coop.fix drive.lead_cap`, `raw_fix`): the raw lead walked without its
//   late lead catching up had no bound (0): its catch_prop catch-up of a mirror that started late drew 0.86-0.93 m
//   frames (tribe_walk_player 20261123-023643, -071312, -075458); with the fix it is bounded by the body's own speed
//   too (sp: the raw lead runs at the body's speed then), walk_place_max_step (drive_plan.h) holds the frame.
//   Off: no bound there (0.7.16.1).
inline float mirror_lead_speed(bool keep_fix, bool smooth_bound_fix, float lead_sp, float sp, bool late_catching,
                               float late_rate, bool catch_fix = false, float lag = 0.f, bool raw_fix = false)
{
    const float rate = late_rate > 1.f ? late_rate : 1.f;
    if (lead_sp < 0) {
        if (keep_fix && late_catching)
            return std::max(sp / rate, 0.5f);
        return keep_fix && raw_fix ? std::max(sp, 0.5f) : 0.f;
    }
    if (!smooth_bound_fix)
        return 0.f;
    float v = std::max(lead_sp / rate, 0.5f);
    if (catch_fix && lag > 0.f)
        v = std::max(v, std::min(lag / kLagCatchTime, kLagCatchMax));
    return v;
}

// BUG-1451, second step (`coop.fix tribe.smooth_catch`): the first step's lead_speed floor alone had no proven effect
// (lab 20261123-111347 on 2/4 FAIL vs 20261123-121417 off 3/4 FAIL; 20261123-123958 with `coop.bodyrec diag` 4/4 FAIL).
// Three bottlenecks showed up in the diagnostic tracks:
// 1. walk_place holds placing to (2 x lead_speed + catch_up - walk_sp) x dt, assuming the game walks at walk_sp = sp.
//    During a late walk start at 2x catch-up, sp is 12-17 m/s while the game walks 0-0.8 m/s (walk start / keep_lost
//    teleports), starving placing to 1.5-6.7 m/s even when lag < 1 m (125053 t1: 0.7 m/s stall at lag 0.95 m).
// 2. When lagging >= 1 m behind the aim with game walk ~0 m/s, reserving walk_sp = 9 m/s leaves only 12 m/s placing
//    (+3 m/s net vs a 9 m/s host), taking > 0.8 s after a 0.35 s network spike. Lowering walk_speed towards kLagWalkCap
//    (5 m/s at lag >= 1.5 m) keeps walk gait active, hard-caps any one-frame game walk spike to <= 5 m/s, and shifts
//    up to 16 m/s into deterministic placing while keeping walk + placing <= 2 x v + 3 = 21 m/s (0.67 m at 32 ms, well
//    below tribe_walk_player's 0.864 m jump threshold).
// 3. During a second loss burst while lagging, extrapolation decay drops sp to 1-4 m/s and driver.h's (sp + catch_up)
//    clamped placing to 4.5-6 m/s (125710 t1, 125859 t7). Raising WalkPlace::catch_up to cover want_place - sp lets the
//    copy close at up to budget - walk (<= 18 m/s placing, <= 21 m/s total) even when sp decayed.
// Off: mirror_lead_speed's bound only, walk_speed -1, base catch_up unchanged (0.7.16).
constexpr float kLagCatchMin = 1.f;    // m behind its aim: start lag catch-up boost
constexpr float kLagWalkFull = 1.5f;   // m behind its aim: walk_speed cap reaches kLagWalkCap
constexpr float kLagWalkCap = 5.f;     // m/s: game walk cap while lagging >= 1.5 m (5 + 16 = 21 m/s ceiling)
constexpr float kLagCloseTime = 0.35f; // s: lag-based speed floor when target slowed/decayed (lag / 0.35 s, max 9 m/s)
constexpr float kLagCatchBase = 3.f;   // m/s: BUG-0619's base catch_up in the frame budget
struct CatchBound {
    float lead_speed; // WalkPlace::lead_speed (0: no bound)
    float walk_speed; // WalkPlace::walk_speed (-1: the target's speed)
    float catch_up;   // WalkPlace::catch_up (>= base_catch)
};
// `lead_speed` mirror_lead_speed's, `lead_sp` / `sp` / `late_rate` as there, `base_catch` / `catch_prop` / `lead_fix`
// WalkPlace's catch-up inputs, `lag` the mirror's distance to its aim.
inline CatchBound mirror_catch_bound(float lead_speed, bool catch_fix, float lead_sp, float sp, float late_rate,
                                     float base_catch, bool catch_prop, bool lead_fix, float lag)
{
    if (!catch_fix || !(sp > 0.f))
        return {lead_speed, -1.f, base_catch};
    const float rate = late_rate > 1.f ? late_rate : 1.f;
    const float body = lead_sp >= 0.f ? lead_sp / rate : sp;
    const float body_walk = std::min(sp, std::max(body, 0.5f));
    if (!(lag >= kLagCatchMin)) {
        if (lead_speed > 0.f && body_walk < sp)
            return {lead_speed, body_walk, base_catch};
        return {lead_speed, -1.f, base_catch};
    }
    const float max_v = std::max(body, kLagCatchMax);
    const float v = std::min(std::max(body, lag / kLagCloseTime), max_v);
    const float budget = 2.f * v + kLagCatchBase;
    const float t = std::min(1.f, (lag - kLagCatchMin) / (kLagWalkFull - kLagCatchMin));
    const float walk_cap = kLagCatchMax + t * (kLagWalkCap - kLagCatchMax);
    const float walk = std::min(body_walk, walk_cap);
    const float want_place = std::min(std::max(0.f, budget - walk), 2.f * max_v);
    const float old_cu =
        (catch_prop && lead_fix) ? std::max(base_catch, std::min(lag / 0.25f, sp + base_catch)) : base_catch;
    const float old_place =
        lead_speed > 0.f ? std::min(sp + old_cu, std::max(0.f, 2.f * lead_speed + old_cu - sp)) : (sp + old_cu);
    if (!(want_place > old_place + 0.05f) && !(walk < sp))
        return {lead_speed, -1.f, base_catch};
    const float new_base = std::max(base_catch, want_place - sp);
    float cu = new_base;
    if (catch_prop && lead_fix)
        cu = std::max(cu, std::min(lag / 0.25f, sp + new_base));
    const float want_ls = std::max(0.5f, 0.5f * (want_place + walk - cu));
    const float ls = lead_speed > 0.f ? std::max(want_ls, std::min(lead_speed, 0.5f * (budget - cu))) : want_ls;
    return {std::max(ls, 0.5f), walk < sp ? walk : -1.f, new_base};
}

// BUG-1460: a walking citizen whose tribe pointer (+0x1014) is null makes the game's make-way step
// (Creature_AskGroupToMakeWay 0xC28530) read that null tribe (0x00C8F6D3). `coop.fix tribe.makeway_null`: such a call
// asks nobody (the hook returns false without calling the game); everything else calls the game's function
inline bool makeway_skip(bool fix, bool citizen, const void* tribe) { return fix && citizen && !tribe; }
// BUG-1460 (`coop.fix tribe.body_still`): a dead member's mirror that was taken out of its tribe is not walked any more
// (its walk ended once, put where the host's body lies); off: driven on by mirror_step as a living mirror
inline bool body_still(bool fix, bool host_body, bool detached) { return fix && host_body && detached; }

// ---------------- audit 0.7.15.5 TR items and the save's taken members (0.7.17 WP-B1) ----------------

// BUG-1494 (TR-8, `coop.fix tribe.tree_mask`): a Flora tree has fruit left to gather: cTreeData +0x48 (`left`, a bit
// per fruit) is not 0. `slots` is +0x4C (every fruit slot: 0x07 small, 0x1F big, never 0). Off: the two read as one
// u64 (0.7.16.1), so every tree passed, an empty one too
inline bool tree_has_fruit(bool fix, uint32_t left, uint32_t slots)
{
    return fix ? left != 0 : ((uint64_t)slots << 32 | left) != 0;
}

// BUG-1493 (TR-7, `coop.fix tribe.air_release`): a member mirror's air hold (BUG-0946) goes when nothing places the
// mirror from the host's air list any more: the host is away or this guest elsewhere (`local`: its own game plays the
// mirror), or no snapshot came for 3 s (`stale`). Off: the hold stays (re-placed at the old height until the host is
// back)
inline bool air_release(bool fix, bool held, bool local, bool stale) { return fix && held && (local || stale); }

// BUG-1495 (TR-9 part 2, `coop.fix tribe.elsewhere_anims`): a MSG_TRIBE_ANIM / MSG_NPC_ANIM kind a guest that plays
// another world of the host's stage (`elsewhere`) drops: the host's theme music (4 play, 5 move) and party music (7
// play), and the host's game-over caption or pause going up (20 / 21 with `up`). Stops (6, 8) and a caption's or
// pause's end pass (they end only what the relay started, or a wait). Off: nothing dropped (0.7.16.1)
inline bool elsewhere_drops(bool fix, bool elsewhere, uint8_t kind, bool up)
{
    if (!fix || !elsewhere)
        return false;
    if (kind == 4 || kind == 5 || kind == 7)
        return true;
    return (kind == 20 || kind == 21) && up;
}

// BUG-1496 (TR-11, `coop.fix tribe.theme_end`): what happens to a theme slot the relay plays on this guest when the
// host stops it (kind 6) or the session ends. `in_tribe`: this game is in the Tribe stage with its theme manager.
// Now: stop it in Tribe; outside Tribe (an editor) keep it for the stop at the return. Off (0.7.16.1): a stop outside
// Tribe is lost, the session's end forgets the slots and stops nothing
enum class ThemeEnd { Stop, Later, Lost };
inline ThemeEnd theme_end(bool fix, bool in_tribe, bool session_end)
{
    if (!fix)
        return in_tribe && !session_end ? ThemeEnd::Stop : ThemeEnd::Lost;
    return in_tribe ? ThemeEnd::Stop : ThemeEnd::Later;
}

// BUG-1522 (`coop.fix tribe.save_took_listed`): a member a guest's save took out of a tribe's member or selectable
// vector (BUG-0470) goes back at the save's end only while that tribe is still one of the world's, the citizen is
// still in the game's list as the same object (its serial) and the vector does not have it already. The save's end
// runs on the next frame, after that frame's network receive: the host's state may have removed the member meanwhile.
// Off: every taken entry back (0.7.16.1)
inline bool took_goes_back(bool fix, bool tribe_listed, bool citizen_listed, bool same_serial, bool in_vector)
{
    return !fix || (tribe_listed && citizen_listed && same_serial && !in_vector);
}

// ---------------- WP-F1 (0.7.99): BUG-1347 & BUG-1458 ----------------

// BUG-1347 (`coop.fix tribe.hold_track`): Citizen_SetHandheldItem's attach (0xC240E0) looks up the current animation
// via 0x9FF700, which scans pending (+0xF4C) and playing (+0xF0C) tracks in slot order without checking fade state
// (+0xC0). Binding to a pending (0) or fading-out (3) track makes the hold modifier tick (0xC11240) delete the hold on
// the very next frame. Handle formula verified at 0x9FF63C..0x9FF659 / 0x9FF79D..0x9FF7AE.
inline uint32_t track_handle(uint32_t slot, uint32_t gen)
{
    if (slot > 0xFu)
        return 0;
    return ((slot + 1u) & 0xFFu) | (gen << 8);
}

inline bool track_live_state(int state) { return state == 1 || state == 2; }

enum class HoldRetryAction { None, Clear, Retry, RestartAndRetry };

// BUG-1347: decides what held_items_tick should do when a mirror has a bound host hold (`hold_bound`) whose modifier
// is missing (`!has_hold`). With `track_fix` on, retries run every 0.15 s (up to 4 times) and restart the bound anim
// if the mirror dropped back to idle while the host hold is still fresh (< 2.0 s).
inline HoldRetryAction hold_retry_action(bool retry_fix, bool track_fix, bool hold_bound, bool has_hold,
                                         double since_set, double since_hold, uint8_t retries, bool has_item,
                                         bool same_anim, bool slow_tick)
{
    if (!retry_fix || !hold_bound || has_hold || !has_item)
        return HoldRetryAction::None;
    const double interval = track_fix ? 0.15 : 0.30;
    const uint8_t max_retries = track_fix ? 4 : 2;
    if (!track_fix && !slow_tick)
        return HoldRetryAction::None;
    if (since_set < interval)
        return HoldRetryAction::None;
    if (retries >= max_retries)
        return HoldRetryAction::Clear;
    if (!same_anim) {
        if (track_fix && since_hold >= 0.0 && since_hold < 2.0)
            return HoldRetryAction::RestartAndRetry;
        return HoldRetryAction::Clear;
    }
    return HoldRetryAction::Retry;
}

// BUG-1458 (`coop.fix tribe.hand_side`): Citizen_PickHandBone (0x00C24270) chooses a citizen's primary hand slot by
// `mObjectId % num_hands`, so a guest's mirror picks the opposite hand ~50% of the time (chief staff raised on the
// opposite side). Replicating the host's hand slot (1-based) lets the guest select the same limb slot and compute the
// matching secondary hand (+0xFC8) with the game's own opposite-side rule.
struct HandLimb {
    int bone = -1;
    uint8_t side = 0;
};

inline int find_hand_slot(const HandLimb* limbs, int n, int bone)
{
    if (!limbs || n <= 0)
        return -1;
    for (int i = 0; i < n; i++)
        if (limbs[i].bone == bone)
            return i;
    return -1;
}

inline int pick_secondary_slot(const HandLimb* limbs, int n, int primary)
{
    if (!limbs || n <= 0 || primary < 0 || primary >= n)
        return -1;
    int first = (primary + 1) % n;
    uint8_t pside = limbs[primary].side;
    for (int d = first; d != primary; d = (d + 1) % n)
        if (limbs[d].side != pside)
            return d;
    return first;
}

// ---------------- friendly fire (BUG-0899) ----------------

// True when `victim_owner` is a different player's member and Tribe friendly fire is enabled.
inline bool can_ff_target(bool ff_on, uint8_t my_id, uint8_t victim_owner)
{
    return ff_on && my_id < kNobody && victim_owner < kNobody && victim_owner != my_id;
}

enum class FfGate : uint8_t { Normal = 0, Allow = 1, Block = 2 };

// Cross-player damage gate inside the shared player tribe.
inline FfGate ff_damage_gate(bool ff_on, uint8_t att_owner, uint8_t vic_owner)
{
    if (att_owner < kNobody && vic_owner < kNobody && att_owner != vic_owner)
        return ff_on ? FfGate::Allow : FfGate::Block;
    return FfGate::Normal;
}

enum class FfStep : uint8_t { Wait = 0, Walk = 1, Hit = 2 };

// Host tick step for a friendly-fire attack order between two members of the shared player tribe.
constexpr float kFfMeleeRange = 4.0f;
inline FfStep ff_combat_step(float dist, double now, double next_walk, double next_hit)
{
    if (dist > kFfMeleeRange)
        return now >= next_walk ? FfStep::Walk : FfStep::Wait;
    return now >= next_hit ? FfStep::Hit : FfStep::Wait;
}

// ---------------- Tribe epic hostility (BUG-1542) & UFO mirror stage (BUG-1543) ----------------

// BUG-1542 (`coop.fix tribe.epic_hostile`): candidate species tier for the co-op epic in Tribe stage.
// Species table relation values (cSpeciesRelationshipManager::GetRelationship, 0xBA40E0):
//   0 = none/undiscovered, 1 = scared, 2 = aggressive, 3 = neutral, 4 = friendly, 5 = hostile, 6 = allied.
// Tiers (lower is preferred; ties broken by distance to the player village):
//   0 = wild, non-domesticated, non-friendly, and undiscovered (0) or hostile (2/5)
//   1 = wild, non-domesticated, non-friendly (e.g. neutral 3 or scared 1)
//   2 = wild, non-domesticated fallback (friendly/allied wild species only when no non-friendly wild exists)
//   3 = domesticated fallback (only when no non-domesticated species exists on the planet)
// Off: every non-player animal gets tier 0 (nearest wins regardless of domesticated/friendly state).
inline int epic_candidate_tier(bool fix, bool domesticated, bool herd_friendly, int species_rel)
{
    if (!fix)
        return 0;
    if (domesticated)
        return 3;
    const bool friendly = herd_friendly || species_rel == 4 || species_rel == 6;
    if (friendly)
        return 2;
    if (species_rel == 0 || species_rel == 2 || species_rel == 5)
        return 0;
    return 1;
}

// True when candidate (tier, dist) beats current best (best_tier, best_dist).
inline bool epic_candidate_better(int tier, float dist, int best_tier, float best_dist)
{
    return tier < best_tier || (tier == best_tier && dist < best_dist);
}

// True when the host should also update the picked species' cSpeciesRelationshipManager record to hostile (5):
// only for non-domesticated, non-friendly wild species (tier <= 1) whose current relation is not already hostile (2/5).
// Never overwrites a friendly/allied/domesticated species record (tier >= 2) if fallback had to pick one.
inline bool epic_should_force_species_hostile(bool fix, int picked_tier, int current_rel)
{
    return fix && picked_tier <= 1 && current_rel != 2 && current_rel != 5;
}

// BUG-1543 (`coop.fix tribe.ufo_mirror`): stages where the host mirrors drama UFOs to guests (K_UFO).
// Off: Creature stage only (0.7.16). On: Creature and Tribe stages.
inline bool ufo_mirror_stage(bool fix, uint32_t mode_id)
{
    constexpr uint32_t kCreatureMode = 0x1654C01u;
    constexpr uint32_t kTribeMode = 0x1654C02u;
    return mode_id == kCreatureMode || (fix && mode_id == kTribeMode);
}

// ---------------- Tribe guest social befriending (BUG-1561) ----------------

constexpr uint8_t kCmdSocialPlay = 5;   // MSG_TRIBE_CMD sub-kind: guest plays instrument slot 3/4/5 during social session
constexpr uint8_t kAnimTribeSocial = 22; // MSG_TRIBE_ANIM sub-kind: host -> guest Tribal social session state sync

// ChooseInputActionID (0xCCF130) returns 0x2B ("Select your Chieftain to socialize!") when clicking a neutral/friendly
// NPC villager in Social Mode if playerTribe->mpLeader (+0x11C) is not selected. In Together co-op, the host owns the
// single native mpLeader (or may give all members/leader away): allow any player who has living own members selected
// and either does not own mpLeader or is a guest to start a social performance (0x27 -> CitizenAction 26).
inline int choose_social_override(bool fix, bool active, bool rivals, int raw_action, bool own_selected,
                                  bool owns_leader, bool is_guest)
{
    if (fix && active && !rivals && raw_action == 0x2B && own_selected && (!owns_leader || is_guest))
        return 0x27;
    return raw_action;
}

// TribeAbilities (0x0169DE64) instrument slots 3/4/5 -> cTribeTool specialized tool types 4/5/6 (Didgeridoo/Maracas/Horn).
inline int instrument_tool_for_slot(uint32_t slot)
{
    return slot == 3 ? 4 : slot == 4 ? 5 : slot == 5 ? 6 : 0;
}

// AbilityBar_Status (0xD49220 -> 0xD1FB30) return code for Tribe instrument slots 3..5 on a guest during a synced
// Tribal social performance: 0 = usable, 0xF = wait for turn / cooling down, 0xE = no performer with that instrument.
inline uint32_t tribe_social_ability_status(bool fix, bool session_active, bool can_play, bool has_matching_performer,
                                            bool any_ready)
{
    if (!fix || !session_active)
        return 1;
    if (!can_play)
        return 0xFu;
    if (!has_matching_performer)
        return 0xEu;
    return any_ready ? 0u : 0xFu;
}

// BUG-1068 / BUG-1561: guest CitizenActions that should register the village's goal mission on the host
// (15 = raid, 9 = food gift, 26 = social performance).
inline bool guest_mission_action(int action, bool social_fix)
{
    return action == 15 || action == 9 || (social_fix && action == 26);
}

// ---------------- Tribe guest consequence abilities (BUG-1564) ----------------

constexpr uint8_t kCmdConsequenceAbility = 6; // MSG_TRIBE_CMD sub-kind: guest activates Tribe consequence ability (types 52..57)

// Native Tribe consequence ability types (cCreatureAbility +0x08, table 0x0169DE4C slots 0..5, verified GOG 2024):
//   52 (0x34) Traps            <- Cell Carnivore  (0xCFB01B93)
//   53 (0x35) Refreshing Storm <- Cell Herbivore  (0xA8EC6F99)
//   54 (0x36) Flying Fish      <- Cell Omnivore   (0x5ECE4770)
//   55 (0x37) Fire Bombs       <- Creature Aggressive (0x17E5EF84)
//   56 (0x38) Fireworks        <- Creature Social     (0xFD159D91)
//   57 (0x39) Beastmaster      <- Creature Adaptable  (0xB9DE15F2)
constexpr int kConsAbilityMin = 52;
constexpr int kConsAbilityMax = 57;
constexpr int kConsAbilitySlots = 6;
constexpr int kConsAbilityTraps = 52;
constexpr int kConsAbilityStorm = 53;
constexpr int kConsAbilityFlyingFish = 54;
constexpr int kConsAbilityFireBombs = 55;
constexpr int kConsAbilityFireworks = 56;
constexpr int kConsAbilityBeastmaster = 57;

constexpr uint32_t kCardCellCarnivore = 0xCFB01B93u;
constexpr uint32_t kCardCellHerbivore = 0xA8EC6F99u;
constexpr uint32_t kCardCellOmnivore = 0x5ECE4770u;
constexpr uint32_t kCardCreatureAggressive = 0x17E5EF84u;
constexpr uint32_t kCardCreatureSocial = 0xFD159D91u;
constexpr uint32_t kCardCreatureMixed = 0xB9DE15F2u;

inline bool is_consequence_ability_type(int ab_type)
{
    return ab_type >= kConsAbilityMin && ab_type <= kConsAbilityMax;
}

inline int consequence_ability_slot(int ab_type)
{
    return is_consequence_ability_type(ab_type) ? (ab_type - kConsAbilityMin) : -1;
}

inline const char* consequence_ability_name(int ab_type)
{
    switch (ab_type) {
    case kConsAbilityTraps: return "Traps";
    case kConsAbilityStorm: return "Refreshing Storm";
    case kConsAbilityFlyingFish: return "Flying Fish";
    case kConsAbilityFireBombs: return "Fire Bombs";
    case kConsAbilityFireworks: return "Fireworks";
    case kConsAbilityBeastmaster: return "Beastmaster";
    default: return "Unknown";
    }
}

// Returns the consequence card ID required for Tribe ability type `ab_type` (52..57),
// and writes the stage index (0 = Cell, 1 = Creature, -1 = invalid) to `out_stage`.
inline uint32_t required_card_for_ability(int ab_type, int& out_stage)
{
    switch (ab_type) {
    case kConsAbilityTraps:
        out_stage = 0;
        return kCardCellCarnivore;
    case kConsAbilityStorm:
        out_stage = 0;
        return kCardCellHerbivore;
    case kConsAbilityFlyingFish:
        out_stage = 0;
        return kCardCellOmnivore;
    case kConsAbilityFireBombs:
        out_stage = 1;
        return kCardCreatureAggressive;
    case kConsAbilityFireworks:
        out_stage = 1;
        return kCardCreatureSocial;
    case kConsAbilityBeastmaster:
        out_stage = 1;
        return kCardCreatureMixed;
    default:
        out_stage = -1;
        return 0;
    }
}

// Inverse lookup: returns Tribe consequence ability type (52..57) unlocked by Cell or Creature card, or 0.
inline int ability_for_consequence_card(uint32_t card)
{
    switch (card) {
    case kCardCellCarnivore: return kConsAbilityTraps;
    case kCardCellHerbivore: return kConsAbilityStorm;
    case kCardCellOmnivore: return kConsAbilityFlyingFish;
    case kCardCreatureAggressive: return kConsAbilityFireBombs;
    case kCardCreatureSocial: return kConsAbilityFireworks;
    case kCardCreatureMixed: return kConsAbilityBeastmaster;
    default: return 0;
    }
}

enum class ConsAbilityResult : uint8_t {
    Allow = 0,
    Disabled = 1,
    InvalidType = 2,
    CardMismatch = 3,
    OnCooldown = 4,
    NoTribe = 5,
    NoChieftain = 6,
};

inline ConsAbilityResult validate_consequence_ability(bool fix_on, int ab_type, uint32_t cell_card,
                                                      uint32_t creature_card, bool has_tribe, bool has_chieftain,
                                                      bool on_cooldown)
{
    if (!fix_on)
        return ConsAbilityResult::Disabled;
    int stage = -1;
    uint32_t req = required_card_for_ability(ab_type, stage);
    if (!req || stage < 0)
        return ConsAbilityResult::InvalidType;
    uint32_t have = (stage == 0) ? cell_card : creature_card;
    if (have != req)
        return ConsAbilityResult::CardMismatch;
    if (!has_tribe)
        return ConsAbilityResult::NoTribe;
    if (!has_chieftain)
        return ConsAbilityResult::NoChieftain;
    if (on_cooldown)
        return ConsAbilityResult::OnCooldown;
    return ConsAbilityResult::Allow;
}

// Per-player short cooldown guard on host (bridges the gap between DoAction(22) and the BT setting native cooldown).
struct ConsCooldowns {
    static constexpr size_t kPlayers = 8;
    static constexpr double kMinGuardSec = 1.5;
    double ready_at[kPlayers][kConsAbilitySlots] = {};

    void clear()
    {
        for (size_t p = 0; p < kPlayers; p++)
            for (int s = 0; s < kConsAbilitySlots; s++)
                ready_at[p][s] = 0.0;
    }

    bool on_cooldown(uint8_t player, int ab_type, double now) const
    {
        int s = consequence_ability_slot(ab_type);
        if (player >= kPlayers || s < 0)
            return true;
        return now < ready_at[player][s];
    }

    void mark_used(uint8_t player, int ab_type, double now, double cooldown_sec = kMinGuardSec)
    {
        int s = consequence_ability_slot(ab_type);
        if (player >= kPlayers || s < 0)
            return;
        ready_at[player][s] = now + std::max(0.0, cooldown_sec);
    }
};

} // namespace tribe_logic
