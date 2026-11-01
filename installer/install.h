// The install and uninstall steps of SporeCoopSetup.exe, without the window: used by the setup
// (window and silent mode) and by the Windows unit tests (tests/win_tests.cpp).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../updater/winupdate.h"

namespace inst {

struct Options {
    std::wstring pack;          // /pack: a pack file to use; empty = SporeCoop-*.pack in pack_dir
    std::wstring pack_dir;      // the setup's own folder
    std::string api = upd::kLatestUrl;
    bool downgrade = false;     // /downgrade or the player said yes: a version older than the installed one is fine
};

bool is_game(const std::wstring& root); // root\SporebinEP1\SporeApp.exe exists

// BUG-0391: Spore without Galactic Adventures: a SporeBin folder and no SporebinEP1\SporeApp.exe
// (Steam: the base game without the Galactic Adventures DLC)
bool spore_without_ga(const std::wstring& root);
// the game folder for a folder the player chose or typed: itself, or its parent when it is the
// SporeBin or SporebinEP1 folder (old: the parent only when that is a game folder)
std::wstring game_root_of(const std::wstring& picked);
// "" when root is a game folder, else what is wrong, in words for the player
std::string folder_problem(const std::wstring& root);
// the library folders in Steam's steamapps\libraryfolders.vdf ("path" entries; the old format's
// "1", "2", ... entries), backslashes unescaped
std::vector<std::wstring> steam_libraries(const std::string& vdf);
// BUG-0391 (Steam): Galactic Adventures is Steam app 24720 and has its own folder next to base Spore:
// steamapps\common\Spore 24720 (the installdir of steamapps\appmanifest_24720.acf). acf_installdir:
// "installdir" of an appmanifest text; ga_folders: the GA folders a Steam library may hold (the
// installdir first, then "Spore 24720"; not checked to exist)
std::string acf_installdir(const std::string& acf);
std::vector<std::wstring> ga_folders(const std::wstring& library);
// game_root_of, and when that is no game: the Galactic Adventures folder next to it (a sibling
// "Spore 24720" / the GA appmanifest's installdir, or a child of a chosen steamapps\common), else for a
// Spore folder without GA the GA folder of one of these Steam libraries. *note: "Galactic Adventures
// is installed in ...: using that folder." when it switched, else ""
std::wstring resolve_root(const std::wstring& picked, const std::vector<std::wstring>& libraries = {},
                          std::string* note = nullptr);
// the setup's guess among candidate folders: the first game folder, else (BUG-0391) the first Spore
// folder without Galactic Adventures, so the window says that instead of showing nothing
std::wstring pick_game(const std::vector<std::wstring>& candidates);
// what the player can do when GitHub does not answer (BUG-0392)
extern const char* const kManualInstall;

// the pack the setup would use offline: opt.pack, else the newest SporeCoop-*.pack in opt.pack_dir
// (BUG-0094: it was the first one the file system listed)
std::wstring local_pack(const Options& opt);

enum class Result { Ok, Failed, Older };
// Older: the pack is older than the installed version and !opt.downgrade (nothing changed);
// *older_version / *installed tell which (BUG-0094). rel: the release check's result; when it has
// none yet, do_install asks GitHub itself into its own copy and stores it there (BUG-0077).
Result do_install(const std::wstring& root, const Options& opt, upd::ReleaseBox& rel, std::string& msg,
                  const upd::Progress& progress = {}, std::string* older_version = nullptr,
                  std::string* installed = nullptr);
bool do_uninstall(const std::wstring& root, std::string& msg);

// tests: false = the setup's behaviour before BUG-0094 (any pack installed, no downgrade check) and
// BUG-0139 (the window closes during a job); `/oldfix` on the setup's command line
extern bool g_fix;
// tests: false = the folder handling before BUG-0391 (setup `/oldga`, win_tests --old-ga)
extern bool g_ga_fix; // not a lab switch: setup only
// tests: false = the folder handling of the first BUG-0391 fix, before the Steam GA folder "Spore 24720"
// was known (setup `/oldgadir`, win_tests --old-gadir)
extern bool g_ga_dir_fix; // not a lab switch: setup only

} // namespace inst
