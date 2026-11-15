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

} // namespace tribe_logic
