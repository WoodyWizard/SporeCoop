// Rivals mode (docs/09-coop-design.md, Space 0.7 feature 10; game knowledge docs/re/space.md 11).
// A host option: every player is an empire of its own, with its own budget, and relations between
// the players (ally / neutral / war). Players at war damage each other's ships. The state lives in
// space_ext.h (`spaceext::rivals()`, `spaceext::relation(a, b)`); this file owns it.
#pragma once

#include <cstdint>

namespace rivalry {

// squadron.cpp's TakeDamage hook, for every hit while in space: `ufo` is the target cGameDataUFO,
// `attacker` the attacking cGameDataUFO (or null). True = the hit was ours on the ship of a player
// we are at war with: it is sent to that player (who applies it to its own ship) and the hook
// must not call the game on the puppet.
bool on_hit(void* ufo, void* attacker, float dmg, int type, const float* dir);

// true when rivals mode is on and this game's player is at war with player `id` (BUG-1032: the
// host's own NPC ships and that player's ship then hit each other)
bool at_war(uint8_t id);

} // namespace rivalry
