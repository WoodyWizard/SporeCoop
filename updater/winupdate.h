// Windows side of the SporeCoop updater, shared by coop.dll (in-game updates) and
// SporeCoopSetup.exe: HTTPS downloads from the project's GitHub releases, checks, and installing a
// pack's files with a rollback. Files in use (the loaded DLLs) are renamed to *.sporecoop-old and
// replaced; those copies are deleted on the next start (cleanup_old). A player's own *.old files
// are never touched (BUG-0114).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "pack.h"

#ifndef SC_OLD_SUFFIX
#define SC_OLD_SUFFIX L".sporecoop-old" // an update's backup of a replaced file (updater/pending.h)
#endif

namespace upd {

// the GitHub API answer for the newest release (override for tests: [update] url in coop.ini)
constexpr const char* kLatestUrl = "https://api.github.com/repos/WoodyWizard/SporeCoop/releases/latest";

using Progress = std::function<void(uint64_t got, uint64_t total)>;
bool http_get(const std::string& url, std::vector<uint8_t>& out, std::string& err, const Progress& progress = {},
              uint32_t max_bytes = 96u << 20);

bool latest_release(const std::string& api_url, Release& out, std::string& err);
// download the release's pack, check it against SHA256SUMS.txt of the same release, parse it
bool download_pack(const Release& r, Pack& out, std::string& err, const Progress& progress = {});
bool read_pack_file(const std::wstring& path, Pack& out, std::string& err);

// game_root = the folder with SporebinEP1 and Data. Installs every file of the pack (files with
// kOnlyIfMissing only when absent); on any failure everything replaced so far is put back.
// A file the running game holds open (it cannot be renamed) stays as <file>.new and is listed in
// SporebinEP1\mods\sporecoop.pending: the loader (or apply_pending) puts it in place before the
// next start. *pending = how many files wait for that.
bool apply_pack(const std::wstring& game_root, const Pack& p, std::string& err, int* pending = nullptr);
void apply_pending(const std::wstring& game_root); // the same as the loader, when the game is closed
void cleanup_old(const std::wstring& game_root);
// a "<file>.old" a SporeCoop version before 0.8 left (dir_rel e.g. L"\\SporebinEP1\\mods");
// steam_api.dll.old only when it is the old loader (loader_old)
bool own_legacy_backup(const std::wstring& dir_rel, const std::wstring& name, bool loader_old);
// tests (BUG-0214 split of the old g_old_fix): false = the old behaviour
extern bool g_old_suffix_fix;  // update.old_suffix, BUG-0114: backups as *.sporecoop-old, the cleanup deletes only ours
extern bool g_keep_failed_fix; // update.keep_failed, BUG-0150: a failed pending swap keeps its list
extern std::string g_swap_delay_file; // tests (BUG-0243): only this pack path waits ("" = every file)
extern int g_fail_swap_at; // tests (BUG-0242): the Nth swap (1-based) fails like "cannot replace"
// BUG-0243: apply_pack swaps under a named mutex per game folder; cleanup_old and the pending swaps
// skip while it is held. false = no lock (update.lock off, win_tests --old-lock)
extern bool g_lock_fix;
extern int g_swap_delay_ms; // tests (BUG-0139): apply_pack waits this long between a file's two moves

// the version installed in a game folder (SporebinEP1/mods/sporecoop.version), "" if none
std::string installed_version(const std::wstring& game_root);
bool is_loader(const std::wstring& dll); // SporeCoop's steam_api.dll, not the game's own

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

} // namespace upd
