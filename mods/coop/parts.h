// The creature parts the players have collected, shared by the party (BUG-0401,
// docs/re/creations.md#creations-part-pickup).
//
// Rule (shared progress, docs/07-modes.md 7.1): the party has one set of collected parts, the union
// of what every player found. The host's game holds it: a part any player finds is unlocked in the
// host's game and then in every guest's game that plays the host's world. Nothing is ever locked.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace parts {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void*()> items;              // the cPlayer's collected parts (cCollectableItems), or null
    std::function<bool()> guest_in_host_world; // guest: the host's world is the one this game plays
    std::function<uint32_t()> world;           // this game's world id (the save's CRC, 0 = unknown)
    std::function<void(const std::string&)> feed;
};

using Key = std::pair<uint32_t, uint32_t>; // the item's key as the game passes it (lo, hi)

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now); // every frame
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();

// over the unlockable items (the map at +0x1490 and, BUG-0441, the item states +0x4D04: the parts a
// live Cell -> Creature start added): how many are unlocked (IsUnlocked, the palette's own check), an
// order-free hash of their keys (the trace's parts= field), and optionally the locked / unlocked keys.
// items: a cCollectableItems (an editor request's), default the cPlayer's.
bool census(int& unlocked, uint32_t& hash, int& unlockable, std::vector<Key>* locked = nullptr,
            std::vector<Key>* unlocked_keys = nullptr, void* items = nullptr);
// tests: the game's own "a part was found" (a skeleton's reward): cCreatureModeStrategy's message
// 0x5371F11 {avatar, 0, level}. Outside the frame (it shows the goal card). Unlocked count after.
int find_part(int level);
std::string status();
// tests: "keys" the unlocked unlockable keys, "states" every state entry (key=state, u = IsUnlocked)
std::string dump(const std::string& what);

} // namespace parts
