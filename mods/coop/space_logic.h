// Small portable decisions of the Space co-op (spacecoop.cpp, space_map.cpp, space_allies.cpp),
// kept free of game calls for the unit tests (tests/space_tests.cpp, make test).
#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace spacelogic {

// ---------------- the co-op jump's target (BUG-0072) ----------------
struct JumpPartner {
    uint8_t id;
    uint32_t star; // the partner's star, or the last one known; 0 = not known
};

// The partner a jump goes to: `to` = that partner, 0xFF = any partner. With `skip_mine`, partners
// in my own star system are passed over (the co-op window's button names the first partner with a
// known star that is not mine and must jump to that one). The old jump(0xFF) took the first
// partner with any known star (skip_mine = false): with 3 players and the first partner in my
// system, every press was refused.
inline bool pick_jump_target(const std::vector<JumpPartner>& partners, uint32_t here, uint8_t to, bool skip_mine,
                             uint8_t& id, uint32_t& star)
{
    for (auto& p : partners)
        if ((p.id == to || to == 0xFF) && p.star && (!skip_mine || p.star != here)) {
            id = p.id;
            star = p.star;
            return true;
        }
    return false;
}

// ---------------- raids I aborted on mirror colonies (BUG-0136) ----------------
// space_allies keeps the raids it aborted (pointers only compared) so that a watched raid that
// ends by its abort does not count as won. An aborted raid that was never watched (still accepted,
// state 2) used to stay in the set for good, also across sessions: a later raid at the recycled
// address that was won counted as aborted, and the owner's raid was never completed. After every
// look at the watched raids, only aborted raids that are watched stay.
template <typename Watched>
inline void prune_aborted(std::set<void*>& aborted, const Watched& watched)
{
    for (auto it = aborted.begin(); it != aborted.end();)
        it = watched.count(*it) ? std::next(it) : aborted.erase(it);
}

// ---------------- an armada wave on the host's return to the colony (BUG-0227) ----------------
enum class WaveReturn { Fail, Won, Resume, NewRaid };
// away_result: the end state of the wave's raid seen while the host was away (0 none, 5 defended,
// 6 lost/plundered); running: the wave's mission is still a raid on the colony. A raid lost while
// away fails the contract, one defended counts as the wave won; else a running raid is resumed and
// a gone one replaced by a new raid.
inline WaveReturn wave_on_return(int away_result, bool running)
{
    if (away_result == 6)
        return WaveReturn::Fail;
    if (away_result == 5)
        return WaveReturn::Won;
    return running ? WaveReturn::Resume : WaveReturn::NewRaid;
}

// The key of the "HP raised once" set: the ship's object serial (cGameData mID) with the fix, so a
// new ship at a freed ship's address is raised too; its pointer without (0.7.3.0). Tagged so that
// the two kinds never match.
inline uint64_t tough_key(uintptr_t ptr, uint32_t serial, bool by_serial)
{
    return by_serial ? (1ull << 40) | serial : (uint64_t)ptr;
}

// ---------------- which game an owed refund belongs to (BUG-0225, GitHub #159) ----------------
// A refund (a trade gift or sale, a spice gift) is taken from the Space game in memory and may come
// back after that game was left, saved, reloaded or replaced. Where it is paid is decided by the
// game's identity, never by an address or a global save counter:
//   * epoch: spaceext::world_epoch() (one value per Space world in memory: a load, the galaxy menu,
//     another stage or a new player cEmpire give a new one; editors and adventures keep it);
//   * game: the save file of that game, "<slot dir>\<planet>.spo" in lower case (world.cpp
//     current_game_key); "" = unknown (the menu, snapshot.spo).
// The ledger records, per (epoch, game), the sequence number of the last save of that game made
// while that very world was in memory, and per slot directory the last time a received co-op world
// replaced the slot. Memory only, never saved, never sent.
struct WorldTag {
    uint32_t epoch = 0;
    std::string game;
    uint64_t seq = 0; // the ledger's clock when the tag was taken
};
enum class OwedAction { Pay, Drop, Keep };

inline std::string game_dir(const std::string& game)
{
    size_t p = game.find_last_of('\\');
    return p == std::string::npos ? std::string() : game.substr(0, p);
}

struct WorldLedger {
    uint64_t clock = 1;
    std::map<std::pair<uint32_t, std::string>, uint64_t> saved; // (epoch, game) -> seq of the last save
    std::map<std::string, uint64_t> replaced;                    // slot dir -> seq of the last replacement

    WorldTag tag(uint32_t epoch, const std::string& game) { return {epoch, game, clock++}; }
    // a save that completed, of `game`, made while the Space world `epoch` was in memory
    void note_save(uint32_t epoch, const std::string& game)
    {
        if (!epoch || game.empty())
            return;
        saved[{epoch, game}] = clock++;
        if (saved.size() > 256) // old worlds: the oldest entry goes (a refund that old is long settled)
            saved.erase(std::min_element(saved.begin(), saved.end(),
                                         [](auto& a, auto& b) { return a.second < b.second; }));
    }
    // a received world was written into the slot `dir` (not a save of the game that was there)
    void note_replaced(const std::string& dir)
    {
        if (dir.empty())
            return;
        replaced[dir] = clock++;
        if (replaced.size() > 64)
            replaced.erase(std::min_element(replaced.begin(), replaced.end(),
                                            [](auto& a, auto& b) { return a.second < b.second; }));
    }
    bool saved_after(const WorldTag& t) const
    {
        auto it = saved.find({t.epoch, t.game});
        return it != saved.end() && it->second > t.seq;
    }
    bool replaced_after(const WorldTag& t) const
    {
        auto it = replaced.find(game_dir(t.game));
        return it != replaced.end() && it->second > t.seq;
    }
};

// What to do with a refund taken under `t`, now that the Space world `epoch_now` of the game
// `game_now` is in memory:
//   * the same world in memory: Pay;
//   * the tag's game is unknown: Drop (no game could ever be proven to lack it);
//   * that game's slot got a received world since: Drop (that game no longer exists);
//   * now another game, or an unknown one: Keep (paid when that game is loaded again);
//   * the same game file loaded again: Pay if that world was saved after the take (the file lacks
//     the items), else Drop (the reloaded game still has them).
inline OwedAction owed_action(const WorldLedger& l, const WorldTag& t, uint32_t epoch_now, const std::string& game_now)
{
    if (t.epoch && t.epoch == epoch_now)
        return OwedAction::Pay;
    if (t.game.empty())
        return OwedAction::Drop;
    if (l.replaced_after(t))
        return OwedAction::Drop;
    if (game_now.empty() || game_now != t.game)
        return OwedAction::Keep;
    return l.saved_after(t) ? OwedAction::Pay : OwedAction::Drop;
}
constexpr size_t kOwedKeepMax = 32; // owed items kept for another game: the oldest go beyond this

} // namespace spacelogic
