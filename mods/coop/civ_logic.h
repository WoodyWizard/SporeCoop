// The rules of the Civ co-op (civ.cpp) as functions of plain inputs, so `make test` can check them (D3 of
// docs/plan-0.7.15.md, audit STG-8 / TST-9; tests/civ_tests.cpp). civ.cpp reads the game and calls these; no game call
// and no Windows here.
//
//   money    the shared money is the host's: a guest's own income is dropped (its spending still reaches the host
//            through the shared ledger, shared.cpp), a building or vehicle the mod pays for is not made when the money
//            does not cover it (BUG-0613: never below 0), a guest's building the host could not build gives its price
//            back within a bound (BUG-0807).
//   owners   who owns a vehicle of the host's table: a new vehicle of the player nation is the buyer's or the owner's of
//            the nearest player city (BUG-0031: never the shared capital, never nobody), an NPC nation's is nobody's;
//            the vehicles of a player who left go to a player who is here (BUG-0031); a guest orders only its own.
//   holds    what waits: a mirror the host no longer has is removed now, or waits while the city planner moves it or
//            a cinematic plays (BUG-0493, BUG-0478); a dead or reused one is only forgotten (BUG-0951); while the host is
//            away the guest plays locally (BUG-0151).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace civlogic {

constexpr uint8_t kNoOwner = 0xFF; // not in a table / no player
constexpr uint8_t kNpc = 0xFE;     // an NPC nation's vehicle
constexpr uint8_t kShared = 0xFD;  // the shared capital (a city everybody may use)

// ---------------- money ----------------

// a guest's AddMoney on the player civilization is its local economy (its copy of the derricks, the city income): the
// host's game earns the same, so the guest's gain is dropped (applying: a host message the mod runs, which may add)
inline bool guest_income_blocked(bool applying, bool guest, bool in_civ, bool player_civ, float amount)
{
    return !applying && guest && in_civ && player_civ && amount > 0;
}
// the money covers the cost (BUG-0613: the game's SpendMoney takes the money below 0); a NaN money or cost pays, as the
// game's own compare
inline bool can_pay(float money, float cost) { return !(money < cost); }
// BUG-0807: what a refund of `paid` gives back: the price the guest says it paid, never negative, never more than
// `max`, nothing for a value that is no number
inline float refund_amount(float paid, float max)
{
    if (!std::isfinite(paid))
        return 0.f;
    return std::min(std::max(paid, 0.f), max);
}

// ---------------- owners ----------------

// the owner the host's table gives a vehicle it lists for the first time. player_nation: its political id is the
// player civilization's; pending: the player the host is buying it for now (-1 none); nearest_city_owner: the owner
// of the nearest player city, kNpc when the host has no player city
inline uint8_t new_vehicle_owner(bool player_nation, int pending, uint8_t nearest_city_owner)
{
    if (!player_nation)
        return kNpc;
    uint8_t o = pending >= 0 ? (uint8_t)pending : nearest_city_owner;
    if (o == kNoOwner || o == kShared)
        o = 0; // built by the shared capital (or no city found): the host's
    return o;
}
inline bool present(const std::vector<uint8_t>& players_sorted, uint8_t id)
{
    return std::binary_search(players_sorted.begin(), players_sorted.end(), id);
}
// BUG-0031: a vehicle of a player who left (the roster changed) goes to the owner of the nearest player city when that
// player is here, else to the host. False: the owner stays (an NPC's, nobody's, or a present player's)
inline bool orphan_owner(uint8_t owner, const std::vector<uint8_t>& players_sorted, uint8_t nearest_city_owner,
                         uint8_t& out)
{
    if (owner >= kNpc || present(players_sorted, owner))
        return false;
    uint8_t o = nearest_city_owner;
    if (o == kNoOwner || o == kShared || !present(players_sorted, o))
        o = 0;
    out = o;
    return true;
}
// the host runs a guest's order or gift only on that player's own vehicle; a guest sends an order of its game only
// for a mirror it owns (its civ AI orders the other nations' mirrors every few frames)
inline bool owns(uint8_t owner, uint8_t player) { return owner == player; }

// ---------------- holds ----------------

// BUG-0493 / BUG-0478: a removal waits while the city planner moves the vehicle or a cinematic plays
inline bool hold_removal(bool planner_moves, bool movie) { return planner_moves || movie; }
// a mirror the host's complete cycle did not name: its vehicle is removed only while it is the object bound and not
// dead (BUG-0951); what the planner or a cinematic holds waits
enum class Unseen : uint8_t { Remove, Defer };
inline Unseen unseen_mirror(bool alive, bool dead, bool held) { return alive && !dead && held ? Unseen::Defer : Unseen::Remove; }
inline bool kill_unseen(bool alive, bool dead) { return alive && !dead; }
// BUG-1416 (audit CIV-1): a mirror a guest's save took out of a civilization's vehicle lists goes back at the save's
// end only when that civilization is still listed and the vehicle is still in the game's list as the same object (its
// serial): a host snapshot between the save's start and end may have removed it (freed a moment later)
inline bool took_goes_back(bool civ_listed, bool vehicle_listed, bool same_serial)
{
    return civ_listed && vehicle_listed && same_serial;
}
// BUG-1417 (audit CIV-7): a guest's planner building goes to the host once the planner closed only when it is still in
// the city's building list (the same block, the same noun): a drop off a free plot adds it and takes it away again
inline bool planner_kept(bool city_listed, bool in_buildings, bool same_noun)
{
    return city_listed && in_buildings && same_noun;
}
// BUG-1532: the planner sells a building dropped off its offered plots back (the refund through its money object); a
// guest's Civ income block dropped that refund, so a building taken away again cost its price. With civ.planner_closed
// the refund passes for a building this planner added and has not sent (not for one of the city: the host has that)
inline bool planner_refund_passes(bool fix, bool refund_site, bool ours_unsent)
{
    return fix && refund_site && ours_unsent;
}
// BUG-1418 (audit CIV-6): the claim a city of the player civilization gets at the host's refresh. has_old/old: its claim
// now; old_absent: that claim's player is not here; owner_key: the key of its owner in the owner table ("" none, not a
// player); owner_shared: the shared capital; alone: the host plays alone (fix on): its own deal claims nothing.
// Returns "" for no claim.
inline std::string city_claim(bool has_old, const std::string& old, bool old_absent, bool owner_shared,
                              const std::string& owner_key, bool alone)
{
    if (has_old && old != "shared" && old_absent)
        return old; // an absent player's claim stays
    if (alone)
        return has_old ? old : std::string(); // the claim it has stays, the solo deal gives none
    if (owner_shared)
        return "shared";
    return owner_key;
}
// BUG-1418 (CIV-6 B): `coop.store adopt` gives the adopted player its claimed cities of the player civ at once, never the
// capital (dealt by host_assign_cities)
inline bool adopt_takes_city(bool listed, bool capital) { return listed && !capital; }
// the waiting removals, once a frame: one the game removed first (not in its list, dead) is dropped, a held one waits
inline bool pump_waits(bool listed, bool dead, bool held) { return listed && !dead && held; }
inline bool pump_kills(bool listed, bool dead) { return listed && !dead; }
// BUG-0151: while the host is away (its main menu) a guest's vehicles run their own AI and its orders go to its game
inline bool local_play(bool fix, bool guest, bool host_away) { return fix && guest && host_away; }
// BUG-1478 (CIV-5, civ.local_play_gate): the guest hooks that hand the player civilization's income, the NPC growth and
// founding, the AI's vehicles, captures, city purchases and superweapons to the host's game hold only while the guest
// plays the host's Civ world: a guest in another Civ world of its own (BUG-0231) plays it with its own game. The host
// being away (its menu, BUG-0151) is not "elsewhere": those hooks still hold then (BUG-0878's held commands). Off: they
// hold in every Civ world (0.7.16.1).
inline bool guest_holds(bool gate_fix, bool guest, bool in_civ, bool elsewhere)
{
    return guest && in_civ && !(gate_fix && elsewhere);
}
// the host takes a guest's superweapon, city purchase or capture only from a guest in its world (gate on)
inline bool host_takes_world_cmd(bool gate_fix, bool peer_elsewhere) { return !(gate_fix && peer_elsewhere); }
// WP-C1 (BUG-1273): whose species line a city's citizens take: the owner's when it is a guest (a player id, not the
// host 0) and the session plays own creatures (creature.mode own); the host's cities, the shared capital (its founder's:
// the host's), NPC cities and cities of nobody keep the civilization's species (-1)
inline int citizen_line_owner(uint8_t owner, bool own_mode, int max_players)
{
    if (!own_mode || owner == 0 || owner >= max_players)
        return -1;
    return owner;
}

// ---------------- audit 0.7.15.5, WP-V1 of 0.7.17 ----------------

// BUG-1479 (audit CIV-10): the guest's answer to the host's specialty question reaches every city's listener with one
// data pointer. The first listener takes it (Take) and closes the question; the same data at the next listeners is
// dropped (Drop) whether the question is still open or not. Anything else is the game's (Pass). fix off (0.7.16.1):
// the repeat was dropped only while the question was open, so the next listeners passed it to the game's listener,
// which applied the stale capture type of the player civ's last captured city and resumed the box's pause again.
enum class SpecAnswer { Take, Drop, Pass };
inline SpecAnswer spec_answer(bool fix, bool open, bool same_data)
{
    if (same_data && (open || fix))
        return SpecAnswer::Drop;
    return open ? SpecAnswer::Take : SpecAnswer::Pass;
}
// BUG-1480 (audit CIV-11): a guest's CMD_SPACE_BUILD on a host outside Civ goes to the Space planner path (built now,
// or kept until this game is on that planet) by the command; fix off (0.7.16.1): only while the host's game is in
// Space, else into the Civ command hold, which runs only in Civ (the paid building never came). The command's first
// byte 5 is also a Civ guest's superweapon request (u8 5, u32 type, 3 x f32: 17 bytes), which must stay in the Civ hold
// while the host is in a Civ editor: outside Space the space build is told by its size (u8 5, u32 place, u32 noun,
// 6 x f32: 33 bytes).
constexpr size_t kSpaceBuildBytes = 1 + 4 + 4 + 6 * 4, kSuperRequestBytes = 1 + 4 + 3 * 4;
inline bool space_build_routed(bool fix, bool in_space, size_t bytes)
{
    return in_space || (fix && bytes == kSpaceBuildBytes);
}
// BUG-1481 (audit CIV-15 a): a local vehicle a host mirror waits to bind until the host's design answer comes (BUG-0647)
// is not one of this game's own extras: the sweep of a complete cycle spares it. fix off (0.7.16.1): removed, and a
// new mirror made on a later cycle
inline bool sweep_spares(bool fix, bool waits_design) { return fix && waits_design; }

// ---------------- standing city citizen gestures (BUG-1231) ----------------
// On the host, standing city citizens play idle 0x02481DE5 or gestures (0x04A1DD75, 0x04A3442D, 0x050B5AB8, ...).
// A guest's bound copy plays none of those on its own; worldobj.cpp carries the host citizen's drawn animation in the
// citizen snapshot's u32 state field and mirrors standing gestures here.
constexpr uint32_t kCitizenRunAnim = 0x02481DB0u;
constexpr uint32_t kCitizenIdleAnim = 0x02481DE5u;
constexpr uint32_t kCitizenGoneState = ~0u;

inline bool is_citizen_gesture(uint32_t anim)
{
    return anim != 0 && anim != kCitizenGoneState && anim != kCitizenRunAnim && anim != kCitizenIdleAnim;
}

enum class CitizenAnimAction : uint8_t { Keep, PlayGesture, EndToIdle };

inline CitizenAnimAction citizen_gesture_step(bool fix, bool moving, uint32_t local_anim, uint32_t host_anim,
                                              uint32_t& last_gesture, double& gesture_at, double now,
                                              double repeat_gap = 1.5)
{
    if (moving) {
        last_gesture = 0;
        return CitizenAnimAction::Keep;
    }
    if (!fix || host_anim == 0 || host_anim == kCitizenGoneState)
        return CitizenAnimAction::Keep;
    if (is_citizen_gesture(host_anim)) {
        if (local_anim == host_anim)
            return CitizenAnimAction::Keep;
        if (host_anim != last_gesture || now - gesture_at >= repeat_gap) {
            last_gesture = host_anim;
            gesture_at = now;
            return CitizenAnimAction::PlayGesture;
        }
        return CitizenAnimAction::Keep;
    }
    last_gesture = 0;
    if (host_anim == kCitizenIdleAnim && is_citizen_gesture(local_anim))
        return CitizenAnimAction::EndToIdle;
    return CitizenAnimAction::Keep;
}
} // namespace civlogic
