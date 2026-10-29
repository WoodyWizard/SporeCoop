// Co-op mechanics that reward playing together (docs/09-coop-design.md):
//  - joint hunt: a hit on a target another player hit in the last seconds deals +25 %
//  - the pack protects: a lethal hit leaves 40 % health when a partner is close (once a minute)
//  - pack regen: health regenerates while a partner is close
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/protocol.h"
#include "sporekit.h"

namespace teamplay {

struct Ctx {
    // positions of the other players that are in the same place as we are
    std::function<std::vector<std::pair<std::string, net::Vec3>>()> partners;
    std::function<void(const std::string&)> notice; // shown to everybody
    std::function<std::string()> my_name;
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
// host: damage a player deals to an NPC; returns the damage to apply
float on_player_hit(void* target, uint8_t player, float damage);
// own avatar is about to take `damage`: returns the damage to apply (the pack may save us)
float on_own_damage(void* avatar, float damage);
void tick(void* avatar, double now); // own avatar regen
std::string status();                // overlay line, "" if nothing
// the target's object was freed or is another creature now: its hits do not count any more
void forget_target(void* target);
void reset(); // a world change or the session end: no target of before is hit again
size_t hit_targets();
bool enabled();
void set_enabled(bool on);

constexpr float kHuntWindow = 6.f, kHuntBonus = 1.25f, kRescueDist = 15.f, kRescueCooldown = 60.f,
                kRegenDist = 10.f, kRegenPerSec = 0.02f;

} // namespace teamplay
