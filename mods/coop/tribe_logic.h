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
// below the share. `by`: owner -> its members in member order (updated); returns the moves (member, new owner)
template <class M, class Plain>
std::vector<std::pair<M, uint8_t>> balance(std::map<uint8_t, std::vector<M>>& by, const std::vector<uint8_t>& players,
                                           size_t total, Plain plain)
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
            auto pick = v.begin();
            for (auto it = v.begin(); it != v.end(); ++it)
                if (plain(*it)) {
                    pick = it;
                    break;
                }
            M m = *pick;
            v.erase(pick);
            by[p].push_back(m);
            moves.push_back({m, p});
        }
    }
    return moves;
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
inline float mirror_lead_speed(bool keep_fix, bool smooth_bound_fix, float lead_sp, float sp, bool late_catching,
                               float late_rate, bool catch_fix = false, float lag = 0.f)
{
    const float rate = late_rate > 1.f ? late_rate : 1.f;
    if (lead_sp < 0)
        return keep_fix && late_catching ? std::max(sp / rate, 0.5f) : 0.f;
    if (!smooth_bound_fix)
        return 0.f;
    float v = std::max(lead_sp / rate, 0.5f);
    if (catch_fix && lag > 0.f)
        v = std::max(v, std::min(lag / kLagCatchTime, kLagCatchMax));
    return v;
}

} // namespace tribe_logic
