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
#include <functional>
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
// BUG-0848: world.staged_move (the staged folder's final move into the slot is tried again for a
// while when a file in it is held for a moment: ERROR_ACCESS_DENIED / ERROR_SHARING_VIOLATION on
// Windows, an antivirus or indexer scanning the new files). Off: one MoveFileW (0.7.12).
// BUG-0847: world.share_complete (collect fails the share when a file of the slot cannot be read
// completely, instead of leaving it out). Off: unreadable files are skipped (0.7.12).
extern bool g_staged_move_fix, g_collect_fix;
// tests (BUG-0848): called with the staged folder just before its final move (win_tests holds a
// file in it); and the final move fails with ERROR_ACCESS_DENIED while GetTickCount64() is below
// g_test_move_denied_until (a lock Wine does not make for a folder rename)
extern std::function<void(const std::wstring&)> g_test_before_move;
extern uint64_t g_test_move_denied_until;
extern unsigned g_move_retries; // moves of a staged folder that needed more than one try
extern std::wstring g_test_held; // tests: this slot (lower case) cannot be moved (as on Windows with a file in it in use)
extern int g_test_cleanup_age_s; // tests: the age (s) from which a temp copy is deleted (default 600)

// BUG-1434: world.copy_own_save. A co-op slot (a received copy of a host's whole galaxy, old planets too) that holds
// the player's own progress is the player's: a later share never reuses, replaces or tidies it. Own progress: a save
// made in it while this game was not a guest of a session (alone, or hosting from it: mark_own adds the line
// `own_save <date> <planet>` to its sporecoop.txt), or, for slots of older versions without that line, a planet file
// other than the marker's planet written after the marker (a guest only plays the marker's planet in a copy).
// Off: the copy is replaced (the old slot deleted) or tidied like any co-op slot (0.7.16).
extern bool g_own_fix;
// what a slot's sporecoop.txt says (coop false: no marker, the player's own slot)
struct Marker {
    bool coop = false, own = false; // own: an own_save line
    std::string host, planet;       // the host whose world it is a copy of, the planet the copy was made for
};
Marker marker(const std::wstring& slot);
// the slot is a co-op copy with the player's own progress in it (see g_own_fix), whatever the switch says
bool own_progress(const std::wstring& slot);
// adds the own_save line to the slot's marker (once); false when it is no co-op slot or the write failed
bool mark_own(const std::wstring& slot, const std::string& planet);
// BUG-1434: the last write() that moved the old copy of the slot to Games-sporecoop-backup instead of deleting it
// (it held own progress): that backup folder; cleared by every write()
extern std::wstring g_kept_backup;

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

// A save slot as the host shares it: every file under `root` (relative names with '/'), but not
// skip_name (the co-op marker). A received world is refused over these limits (world.cpp), so the
// host checks them too.
constexpr uint32_t kMaxWorldBytes = 128u << 20, kMaxWorldFiles = 2048;
// false and `err` when a file could not be listed, opened or read completely (BUG-0847: a share
// without that file was sent and loaded as an incomplete world); with world.share_complete off the
// file is left out as before and the result is true.
bool collect(const std::wstring& root, std::vector<File>& out, std::string& err, const std::wstring& skip_name = L"sporecoop.txt");

std::string narrow(const std::wstring& w);
std::wstring widen(const std::string& s);

} // namespace slotfs
