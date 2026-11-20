// D3 of docs/plan-0.7.15.md (audit STG-8, TST-9): the rules a guest applies to its proxies of the host's NPCs
// (npc.cpp guest_tick, update_filter), as functions of plain inputs: no game call, no SporeKit, so tests/npc_tests.cpp
// checks them natively. npc.cpp keeps the game calls (spawn, destroy, the driving) and asks these for the decisions.
//
//   stale        whether a proxy the host sent no state for is dropped (BUG-0009), kept while the states flow again
//                after a time without any (the thaw, BUG-0611), or fresh
//   remake       whether a proxy without a creature gets one this frame (a body the guest's game removed stays gone,
//                BUG-0383)
//   keep_locals  whether the guest's own NPCs stay (the update filter does not remove them): the host is away, or
//                the guest mirrors nothing while the host is not in this world (BUG-0412)
//   place        how the proxy is moved this frame: in the air (BUG-0550), walked on the host's path (BUG-0281,
//                BUG-0361), driven standing, or a corpse put where the host's lies
//   mound_lift   where a target inside this game's nest mound is put (BUG-0562)
//   editor_hold  the proxies stay through the host's editor visit (owner decision 2, BUG-1174)
// docs/01-architecture.md#arch-npc-proxy-rules.
#pragma once

#include <cstdint>

namespace npc_rules {

constexpr double kStaleAfter = 5.0;  // s without a state of its own: the host does not talk about it any more
constexpr double kStatesFlow = 0.5;  // s since the last complete state batch: the host sends states now
constexpr double kThaw = 5.0;        // s after the states flow again in which nothing is stale yet (BUG-0611)

// the host sends states (a complete batch within kStatesFlow): only then can a proxy be stale; a host that stands
// still (its pause menu, a caption) sends none and nothing is dropped
inline bool states_flow(double now, double last_batch) { return now - last_batch < kStatesFlow; }

enum class Stale : uint8_t { Fresh, Thaw, Drop };
// a proxy whose last state came at `last_rx`, while the states flow since `flow_since` (the first batch after a time
// without any). Thaw: it would be stale, but every proxy's last state is that old after a freeze (the first batch
// after it may not list it yet); counted, kept
inline Stale stale(double now, double last_rx, double flow_since)
{
    if (now - last_rx <= kStaleAfter)
        return Stale::Fresh;
    if (now - flow_since <= kThaw)
        return Stale::Thaw;
    return Stale::Drop;
}

// a proxy without a creature gets one, unless the host says its NPC is dead and this game made (and removed) one
// before: a body eaten or decayed here stays gone (BUG-0383; a body first seen dead is made, to play the death)
inline bool remake(bool host_dead, bool made_before) { return !(host_dead && made_before); }

// the guest's own NPCs stay this frame (the host owns NPCs otherwise: the update filter removes them)
inline bool keep_locals(bool host_away, bool proxies_empty, bool host_here_known, bool host_here)
{
    return host_away || (proxies_empty && host_here_known && !host_here);
}

enum class Place : uint8_t { Air, Walk, Stand, Corpse };
struct PlaceIn {
    bool killed = false;    // the proxy plays the host NPC's death (or lies dead)
    bool host_dead = false; // the state's NF_DEAD
    bool host_air = false;  // the state's NF_AIR: the host's NPC is off its ground
    // BUG-1357: NF_AIR with a height UNDER the host's terrain (agl < -drive::kUnderTol): the host's NPC is pressed into
    // its ground (a far NPC the host's game does not simulate), not in the air: placed on this game's ground instead
    bool host_under = false;
    bool stage = true;      // Creature, Tribe or Civ: the stages whose moving proxies are walked
    float speed = 0;        // m/s of the velocity the walk would use (after the stop and path rules)
    float off = 0;          // m between the proxy now and its target
};
constexpr float kWalkSpeed = 0.3f; // m/s: slower stands
constexpr float kWalkNear = 8.f;   // m: a proxy farther from its target is driven (teleported) first
inline Place place(const PlaceIn& in)
{
    if (!in.killed && in.host_air && !in.host_under && !in.host_dead)
        return Place::Air;
    if (!in.killed && in.stage && in.speed > kWalkSpeed && in.off < kWalkNear)
        return Place::Walk;
    return in.killed ? Place::Corpse : Place::Stand;
}

// owner decision 2 of docs/plan-0.7.15.md (BUG-1174): the host's editor visit is not a world change. While the host
// counts as away (`away`) because it is in an editor (`host_editor`), and after that until the away state ends (`held`:
// it held last frame; the host's mode is its stage again, the away state ends a second later), the guest keeps its
// proxies (held, frozen: no states come) instead of handing the planet over to its own NPCs. A host that goes on to
// the main menu or a load (`host_menu_or_load`) is away for real: the hand-over runs.
inline bool editor_hold(bool away, bool host_editor, bool held, bool host_menu_or_load)
{
    return away && !host_menu_or_load && (host_editor || held);
}

// BUG-0562: a standing proxy's target at radius `r` under this game's surface radius `sr` (the nest mound) by more
// than kMoundMin and less than kMoundMax, where that surface is over the plain terrain `g`, is put kMoundSink under
// the surface: the factor its position is scaled by (1: left where it is)
constexpr float kMoundMin = 0.1f, kMoundMax = 2.5f, kMoundSink = 0.05f;
inline float mound_lift(float r, float sr, float g)
{
    if (r < 1.f || sr <= 0.f || g <= 0.f || sr - r <= kMoundMin || sr - r >= kMoundMax || sr <= g + kMoundMin)
        return 1.f;
    return (sr - kMoundSink) / r;
}

} // namespace npc_rules
