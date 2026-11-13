// Space stage co-op mechanics (docs/re/space.md, "Co-op features"). Every player flies in their
// own copy of the galaxy; these features link the copies:
//  - rescue: a lethal hit next to a partner disables the ship for 20 s instead; the partner flies
//    close to repair it, else the native death and respawn follow
//  - jump: travel straight to a partner's star system (coop.space jump / co-op window)
//  - raids: a raid on a colony alerts the partners; defended with a partner nearby: a bonus
//  - missions: a completed mission gives its reward tools to the partners too
//  - terraforming: partners on the same planet terraform it together (both copies change)
//  - cargo: give spice to a partner
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/protocol.h"
#include "net/session.h"
#include "syncdbg.h"
#include "sporekit.h"

namespace spacecoop {

struct Partner {
    uint8_t id;
    std::string name;
    uint32_t place;   // modes place: StarID | 0x80000000, PlanetID | 0x40000000, 0 = galaxy
    net::Vec3 pos;    // in the place's coordinates
    void* puppet;     // its ship here (same place), may be null
    bool playing = true; // a state under 3 s old, in the world, not dead: it can rescue (BUG-0131)
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> notice; // local (toast + chat log)
    std::function<std::vector<Partner>()> partners;
    std::function<uint32_t()> my_place;
    std::function<void(const net::Vec3&, const std::string&, double, uint32_t)> beacon;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void draw_overlay();
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
// the own ship is disabled (no shooting)
bool disabled();
// a hit on the own ship (squadron's TakeDamage hook): the damage to apply
float filter_own_damage(void* ufo, float dmg, int type);
// co-op window: spice in the cargo and the partner a gift / jump would go to
int spice(std::string& to);
int give_spice(int n, uint8_t to = 0xFF); // to: the recipient (BUG-1050), 0xFF: the first other player
bool can_jump(std::string& to);
std::string jump(uint8_t to); // "" = ok, else why not
// the same travel to any star system by StarID (space_map: a partner's colony), free
std::string travel(uint32_t star); // "" = ok, else why not
// jump into the system of a StarID (the jump's native sequence; from a planet, a system or the
// galaxy view); "" = ok, else why not. For space features (space_finale.cpp)
std::string travel_to_star(uint32_t star);
// in the galaxy view: teleport next to a star (SpaceTeleportTo) for free, refused while the ship is
// disabled (space_map: a partner flying in the galaxy view, BUG-0063); "" = ok, else why not
std::string teleport_in_galaxy(uint32_t star);
// the cStarRecord of a StarID (starRecord+0x70), nullptr when it is not a star of this galaxy
// (the game's own GetStarRecord faults then)
void* star_record(uint32_t id);
// tests (coop.targets all): where to fly. On a planet its cities (T<n> ours, X<n> others), in a
// solar system its planets (key P<PlanetID>, C<PlanetID>
// for a colony of ours), in the galaxy view the stars near the own ship (S<StarID>, H<StarID> for a
// star of ours), positions in the coordinates of that view
std::vector<syncdbg::Entry> nav_entries();
std::string status();
std::string debug(const std::vector<std::string>& args);
// a raid on one of our colonies changed its state (cMission::SetState hook): 3 started, 5 defended,
// 6 lost; place = PlanetID | 0x40000000 (space_contracts.cpp: the armada's waves are raids); the
// mission tells the raids on one planet apart (BUG-0046)
void set_raid_listener(std::function<void(void* mission, uint32_t place, int state)> fn);

} // namespace spacecoop
