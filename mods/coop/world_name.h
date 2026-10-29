// Which planet the game plays, from the game's CurrentGameName (BUG-0258). Portable (no Windows,
// no SporeKit): tests/npc_tests.cpp runs it natively; modes.cpp keeps one State.
//
// CurrentGameName is the save file of the game being played ("Scalinus.spo"; "SaveGame" in the
// galaxy menu). After the game's own live Creature -> Tribe change (the tribe intro) it reads
// "snapshot.spo", the game's temporary file, until the game is saved again (the save writes the
// planet's file and sets the name back). The place id of a player is the crc32 of that name, so
// the host was in "snapshot.spo" and every guest, who loaded the host's planet file, in
// "Scalinus.spo": never together, no puppets, until the host's next save.
//
// Rule, applied to every read (the frame reads it once per frame, so the memory follows the game):
//   * a planet file ("<name>.spo", not "snapshot.spo"): remembered, returned as is;
//   * "snapshot.spo" with a remembered planet: that planet (the game keeps playing it);
//   * anything else ("SaveGame" in the menu, empty, "snapshot.spo" with nothing remembered):
//     the memory is dropped, the raw name is returned (a new game from the menu never inherits
//     the planet of the game before).
#pragma once

#include <string>

namespace world_name {

constexpr const char* kSnapshot = "snapshot.spo";

inline bool is_planet(const std::string& n)
{
    return n.size() > 4 && n.compare(n.size() - 4, 4, ".spo") == 0 && n != kSnapshot &&
           n.find_first_of("/\\") == std::string::npos;
}

struct State {
    std::string last; // the last planet file read since the game left the menu
};

// raw: CurrentGameName now (UTF-8). fix off: the raw name (the code before BUG-0258).
inline std::string effective(State& s, const std::string& raw, bool fix)
{
    if (is_planet(raw)) {
        s.last = raw;
        return raw;
    }
    if (raw == kSnapshot && !s.last.empty())
        return fix ? s.last : raw;
    s.last.clear();
    return raw;
}

} // namespace world_name
