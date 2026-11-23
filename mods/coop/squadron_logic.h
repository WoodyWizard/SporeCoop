// Portable rules of the Space NPC fleet leaders (BUG-1288, WP-S1 of 0.7.17, switch fleet.leader): who simulates the
// NPC ships of a star system or planet that the host is not in, and what a role change does to a game's ships.
// No game or Windows headers: tests/space_tests.cpp runs it natively (ASan / UBSan).
//
// The host decides, per place, and announces the leaders (MSG_UFO_LEAD K_LEADS); the guests only read the table.
// So every guest agrees on the leader (an older guest is simply not in the table), and a leader keeps its place
// while it stays there: a player that arrives later never takes the lead (its game's natives are fresh, the
// leader's are the ones the place has been fighting), and a short zoom out to the galaxy keeps it too (kLeaderGrace).
// The place where the host is keeps the host-led path of 0.7.16 unchanged (MSG_UFO_STATE / HIT / DAMAGE).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace squadron_logic {

constexpr uint8_t kHostId = 0;
constexpr uint8_t kNoLeader = 0xFF;
constexpr uint32_t kSpaceMode = 0x1654C05u; // modes::kSpace (static_assert in squadron.cpp)
// a leader that is in no place (galaxy view: world 0, an editor, no state for a moment) keeps its place this long
constexpr double kLeaderGrace = 6.0;
constexpr size_t kMaxLeads = 16; // rows of one K_LEADS (4 players: at most 2 guest-led places)

// a place of Space: (mode, world) with world != 0, as puppets.cpp same_place (PlanetID | 0x40000000 on a planet,
// StarID | 0x80000000 in a solar system, 0 in the galaxy view)
struct Place {
    uint32_t mode = 0;
    uint32_t world = 0;
    bool valid() const { return mode == kSpaceMode && world != 0; }
    bool operator==(const Place& o) const { return mode == o.mode && world == o.world; }
    bool operator!=(const Place& o) const { return !(*this == o); }
};

// a player as one game sees it: its latest state's place; `capable`: it reads MSG_UFO_LEAD (space 1.3 or newer)
struct PeerPlace {
    uint8_t id = 0;
    uint32_t mode = 0;
    uint32_t world = 0;
    bool has_state = false;
    bool capable = true;
    Place place() const { return has_state ? Place{mode, world} : Place{}; }
};

inline bool in_place(const PeerPlace& p, const Place& target) { return target.valid() && p.place() == target; }

inline const PeerPlace* find_peer(const std::vector<PeerPlace>& v, uint8_t id)
{
    for (const auto& p : v)
        if (p.id == id)
            return &p;
    return nullptr;
}

// a row of the host's table: the leader of a place without the host
struct Lead {
    Place place;
    uint8_t leader = kNoLeader;
    uint8_t present = 0;      // capable guests in the place now (the leader included when it is there)
    double away_since = -1;   // the leader is in no place since then (grace), -1: it is there
    // announced (and relayed for) only with somebody to mirror the leader: two players, or a follower while the
    // leader is in its grace. A place with one guest keeps its row (the first one there leads once another comes)
    bool active() const { return leader != kNoLeader && present + (away_since >= 0 ? 1 : 0) >= 2; }
};

inline const Lead* find_lead(const std::vector<Lead>& t, const Place& p)
{
    if (!p.valid())
        return nullptr;
    for (const auto& l : t)
        if (l.place == p)
            return &l;
    return nullptr;
}

// The host's table for this moment from the last one. `peers`: the host (id 0) and every guest, `capable` from its
// families. Off (`enabled` false: the switch off, or Rivals): no rows. Rules:
//  - a place counts when it is valid, the host is not in it and at least one capable guest is;
//  - its leader stays while it is there, or while it is in no place for less than kLeaderGrace (it left the session
//    or went to another place: a new one);
//  - a new leader is the lowest id of the capable guests there (on a first row: the only one, or the guests that
//    stayed when the host left).
inline std::vector<Lead> step_leads(const std::vector<Lead>& prev, const std::vector<PeerPlace>& peers, double now,
                                    bool enabled)
{
    std::vector<Lead> out;
    if (!enabled)
        return out;
    const PeerPlace* host = find_peer(peers, kHostId);
    Place hp = host ? host->place() : Place{};
    for (const auto& p : peers) {
        if (p.id == kHostId || !p.capable)
            continue;
        Place pl = p.place();
        if (!pl.valid() || pl == hp)
            continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const Lead& l) { return l.place == pl; });
        if (it == out.end()) {
            out.push_back({pl, kNoLeader, 0, -1});
            it = out.end() - 1;
        }
        it->present++;
    }
    for (auto& l : out) {
        const Lead* old = find_lead(prev, l.place);
        if (old && old->leader != kNoLeader) {
            const PeerPlace* ld = find_peer(peers, old->leader);
            if (ld && ld->capable && in_place(*ld, l.place)) {
                l.leader = old->leader;
                continue;
            }
            if (ld && ld->capable && !ld->place().valid()) {
                double since = old->away_since >= 0 ? old->away_since : now;
                if (now - since < kLeaderGrace) {
                    l.leader = old->leader;
                    l.away_since = since;
                    continue;
                }
            }
        }
        for (const auto& p : peers)
            if (p.id != kHostId && p.capable && in_place(p, l.place) && p.id < l.leader)
                l.leader = p.id;
    }
    std::sort(out.begin(), out.end(), [](const Lead& a, const Lead& b) {
        return a.place.world != b.place.world ? a.place.world < b.place.world : a.place.mode < b.place.mode;
    });
    if (out.size() > kMaxLeads)
        out.resize(kMaxLeads);
    return out;
}

// what the guests are told: the rows with somebody to mirror the leader
inline std::vector<Lead> announced(const std::vector<Lead>& t)
{
    std::vector<Lead> v;
    for (const auto& l : t)
        if (l.active())
            v.push_back(l);
    return v;
}

// did the announced part of the table change (place, leader)? Counts do not matter to a guest
inline bool same_announced(const std::vector<Lead>& a, const std::vector<Lead>& b)
{
    auto x = announced(a), y = announced(b);
    if (x.size() != y.size())
        return false;
    for (size_t i = 0; i < x.size(); i++)
        if (x[i].place != y[i].place || x[i].leader != y[i].leader)
            return false;
    return true;
}

enum class FleetRole : uint8_t {
    None = 0,   // the game's own NPC ships, nobody else's (not in Space, the galaxy view, alone, the switch off)
    Leader = 1, // this game's NPC ships are the place's (the host always; a guest the host named)
    Mirror = 2, // this game shows the leader's NPC ships (its own NPC ships there are removed)
};

struct RoleChoice {
    FleetRole role = FleetRole::None;
    uint8_t leader = kNoLeader;
};

// A game's role. `host_here`: the host is in this guest's place (squadron Ctx::with_host, the 0.7.16 rule: it always
// wins, so with the host there nothing differs from 0.7.16). `leads`: the host's announced table (a guest's copy).
// `fix_on` false or a host without space 1.3 (`host_capable`): the 0.7.16 roles (Mirror of the host or None).
inline RoleChoice local_role(bool is_host, uint8_t my_id, const Place& my_place, bool host_here, bool fix_on,
                             bool host_capable, const std::vector<Lead>& leads)
{
    if (is_host)
        return {FleetRole::Leader, kHostId};
    if (host_here)
        return {FleetRole::Mirror, kHostId};
    if (!fix_on || !host_capable || !my_place.valid())
        return {FleetRole::None, kNoLeader};
    const Lead* l = find_lead(leads, my_place);
    if (!l || l->leader == kNoLeader || l->leader == kHostId)
        return {FleetRole::None, kNoLeader};
    if (l->leader == my_id)
        return {FleetRole::Leader, my_id};
    return {FleetRole::Mirror, l->leader};
}

// ---- the host's relay checks (MSG_UFO_LEAD from a guest); `peers` as for step_leads ----
// K_STATE: from the leader of an active row of that place
inline bool can_forward_state(uint8_t sender, uint8_t claimed, const Place& place, const std::vector<Lead>& t)
{
    const Lead* l = find_lead(t, place);
    return sender != kHostId && claimed == sender && l && l->active() && l->leader == sender;
}
// ... to every capable guest in that place but the leader
inline bool should_forward_state_to(const PeerPlace& target, const Place& place, uint8_t leader)
{
    return target.id != kHostId && target.id != leader && target.capable && in_place(target, place);
}
// K_HIT: from a capable guest in the place, to the place's leader (not itself)
inline bool can_forward_hit(uint8_t sender, uint8_t claimed, const Place& place, const std::vector<Lead>& t,
                            const std::vector<PeerPlace>& peers)
{
    const Lead* l = find_lead(t, place);
    if (sender == kHostId || !l || !l->active() || l->leader != claimed || claimed == sender)
        return false;
    const PeerPlace* s = find_peer(peers, sender);
    return s && s->capable && in_place(*s, place);
}
// K_DAMAGE: from the place's leader to a capable guest in the place (not the leader, not the host)
inline bool can_forward_damage(uint8_t sender, uint8_t claimed, uint8_t victim, const Place& place,
                               const std::vector<Lead>& t, const std::vector<PeerPlace>& peers)
{
    const Lead* l = find_lead(t, place);
    if (sender == kHostId || claimed != sender || !l || !l->active() || l->leader != sender || victim == kHostId ||
        victim == sender)
        return false;
    const PeerPlace* v = find_peer(peers, victim);
    return v && v->capable && in_place(*v, place);
}

// What a role change does to this game's fleet tables (squadron.cpp apply_transition). No promotion of mirrors
// (the plan of 0.7.17, review of the agy branch): a follower that becomes the leader drops the mirrors, as every
// other follower does, and the place shows the new leader's ships (they pop once).
struct Transition {
    bool changed = false;
    bool drop_mirrors = false; // destroy the mirrors (no explosion) and forget the copied shots
    bool clear_leader = false; // forget the leader's ship ids and shot lists (a guest leader only)
    bool reset_rx = false;     // a new sender's cycles: the chunk receiver starts again
};

// The host-led transitions are exactly 0.7.16's: Mirror(host) -> None drops the mirrors in mirror_tick (here: no
// action), the host's ids are never cleared, the receiver is never reset. Only a guest-led side acts here.
inline Transition step_role(FleetRole prev_role, uint8_t prev_leader, const Place& prev_place, FleetRole next_role,
                            uint8_t next_leader, const Place& next_place)
{
    Transition t;
    if (prev_role == next_role && prev_leader == next_leader && prev_place == next_place)
        return t;
    t.changed = true;
    bool guest_led_before = prev_role != FleetRole::None && prev_leader != kHostId && prev_leader != kNoLeader;
    bool guest_led_after = next_role != FleetRole::None && next_leader != kHostId && next_leader != kNoLeader;
    if (!guest_led_before && !guest_led_after)
        return t; // the host-led roles of 0.7.16
    if (prev_role == FleetRole::Mirror)
        t.drop_mirrors = true; // a guest leader's mirrors, or the host's mirrors when a guest takes over
    if (prev_role == FleetRole::Leader && prev_leader != kHostId)
        t.clear_leader = true;
    if (next_role == FleetRole::Leader && prev_role != FleetRole::Leader)
        t.clear_leader = true; // (nothing should be left; a clean start of the ids)
    if (prev_role == FleetRole::Mirror || next_role == FleetRole::Mirror)
        t.reset_rx = true;
    return t;
}

inline const char* role_name(FleetRole r)
{
    switch (r) {
    case FleetRole::Leader: return "leader";
    case FleetRole::Mirror: return "mirror";
    default: return "none";
    }
}

} // namespace squadron_logic
