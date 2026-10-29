// Received worlds on disk (world.cpp): which save slot (Games\GameN) a host's world goes into and
// how it is written. Only Win32 file calls, no game: tests/win_tests.cpp runs it under Wine.
//
// * pick: never the slot the game plays (the folder the save database points to, BUG-0035), and a
//   co-op slot is reused only for the same host world (host name + planet in its sporecoop.txt,
//   BUG-0087); else a free GameN.
// * write: every file goes into a fresh folder next to Games first (Games-sporecoop-tmp), each
//   write checked; only when all of them are complete the folder replaces the slot as a whole and
//   carries the marker (BUG-0105). An old copy of the same world is replaced entirely, so no file
//   of another world stays next to it (BUG-0087).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace slotfs {

struct File {
    std::string rel; // '/' separated, checked by the caller (world.cpp safe_rel)
    std::vector<uint8_t> data;
};

// tests: false = the old behaviour (`coop.worldfix busy_slot|slot_match|write_check 0`)
extern bool g_busy_fix, g_match_fix, g_write_fix;
// BUG-0254: world.slot_fallback (a slot that cannot be replaced: the world goes into another slot,
// world.cpp) and world.tmp_cleanup (leftovers of interrupted writes in Games-sporecoop-tmp are
// tidied, under a named mutex shared by the game instances of one machine)
extern bool g_fallback_fix, g_cleanup_fix;
extern std::wstring g_test_held; // tests: this slot (lower case) cannot be moved (as on Windows with a file in it in use)
extern int g_test_cleanup_age_s; // tests: the age (s) from which a temp copy is deleted (default 600)

enum class WriteFail { None, Files, Replace, Move, Busy };

// games: ...\Spore\Games. busy_lower: the slot the game plays (lower case, no trailing '\'), may be
// empty. backup_planet: move another slot holding planet_spo out of Games first (menu loading,
// auto_load_world=0, needs the planet's disc free); `note` then says where it went.
// avoid_lower: more slots (lower case) never to choose, like the busy one (BUG-0254: a slot that
// could not be replaced)
std::wstring pick(const std::wstring& games, const std::wstring& busy_lower, const std::string& host,
                  const std::string& planet, const std::string& planet_spo, bool backup_planet, std::string& note,
                  const std::vector<std::wstring>& avoid_lower = {});

// writes the world into `slot` (replacing what is there); false and `err` when anything failed: the
// slot is then as it was before
// slot is then as it was before; `why` says which step failed
bool write(const std::wstring& slot, const std::vector<const File*>& files, const std::string& host,
           const std::string& planet, uint32_t world_id, std::string& err, WriteFail* why = nullptr);

// write() into `slot`; BUG-0254: when the old copy cannot be replaced (a file in it is in use) and
// world.slot_fallback is on, once more into another slot (pick with the held slot avoided, no
// planet backup): `slot` and `moved` then say so. After a successful write the other copies of the
// same host world are tidied (tidy). busy_lower: as for pick.
bool write_or_fallback(const std::wstring& games, std::wstring& slot, const std::wstring& busy_lower,
                       const std::vector<const File*>& files, const std::string& host, const std::string& planet,
                       const std::string& planet_spo, uint32_t world_id, std::string& err, bool& moved);

// BUG-0254: after a world went into `kept`, every other co-op slot of the same host world that is
// not busy goes to Games-sporecoop-backup (never deleted: the player may have played on in it);
// a slot whose move fails stays. Returns how many were moved.
int tidy(const std::wstring& games, const std::wstring& kept, const std::wstring& busy_lower, const std::string& host,
         const std::string& planet);

std::string narrow(const std::wstring& w);
std::wstring widen(const std::string& s);

} // namespace slotfs
