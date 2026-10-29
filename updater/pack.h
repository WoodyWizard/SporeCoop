// SporeCoop update pack ("SporeCoop-<version>.pack", built by tools/mkpack.py, attached to every
// GitHub release): the files of one version with their SHA-256, paths relative to the game folder.
// Portable (no Win32): parsed and checked by the mod, the installer and the Linux unit tests.
//
//   "SCUP" u32 format(1) str16 version u32 count
//   count x { str16 path ('/' separated) u8 flags u32 size  32 bytes sha256 }
//   the file data, in entry order
//
// flags: 1 = install only when the file is missing (default configs: never overwrite the user's)
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace upd {

struct PackFile {
    std::string path; // e.g. "SporebinEP1/mods/coop.dll"
    uint8_t flags = 0;
    std::vector<uint8_t> data;
};
constexpr uint8_t kOnlyIfMissing = 1;

struct Pack {
    std::string version;
    std::vector<PackFile> files;
};

// The only places an update may write to (a pack comes from the internet): the loader, SporeKit,
// the mods folder and the resource package. No "..", no absolute paths, no drive letters.
bool allowed_path(const std::string& path);
// BUG-0242: the name Windows uses for `path`, lowercased, or "" when it is not a plain file name we
// write (a trailing dot, a DOS device name, a name of the updater's own working files). allowed_path
// and duplicate_path both use it, so they never disagree. g_path_fix false = 0.7.3.0 (case only).
std::string canonical_path(const std::string& path);
extern bool g_path_fix;

// a path listed twice (compared without case, as Windows does): "" if none (BUG-0089: a second
// entry for a file deleted the first entry's backup of the original, and the rollback lost it)
std::string duplicate_path(const std::vector<PackFile>& files);
extern bool g_dup_fix; // tests: false = the old behaviour (duplicates accepted)

// parse + check every file's SHA-256 and path; err says what is wrong
bool parse_pack(const std::vector<uint8_t>& bytes, Pack& out, std::string& err);
std::vector<uint8_t> write_pack(const Pack& p); // tests (the release pack is made by tools/mkpack.py)

// "0.5.1" vs "0.5.0": -1, 0, 1 (a leading "v" is ignored; missing parts are 0)
int compare_versions(const std::string& a, const std::string& b);

// the latest release from the GitHub API answer (/repos/<owner>/<repo>/releases/latest)
struct Release {
    std::string tag, version;          // "v0.5.1", "0.5.1"
    std::string pack_url, sums_url;    // update pack, SHA256SUMS.txt
    std::string setup_url, page_url;   // SporeCoopSetup.exe, the release page
};
bool parse_release(const std::string& json, Release& out);

// BUG-0094: the version in an update pack's file name ("SporeCoop-0.11.0.pack" -> "0.11.0", case
// ignored, a folder in front is fine), "" when the name is not one; pick_pack: the name with the
// highest version (the first of equal ones), "" if none. g_pick_fix false = the old choice: the
// first name in the listing (tests).
std::string pack_version_of(const std::string& name);
std::string pick_pack(const std::vector<std::string>& names);
extern bool g_pick_fix;

// BUG-0077: the newest release as the setup's check thread found it, read by the window and the
// install job. Every access copies under one lock; g_release_lock_fix false = the old unlocked
// access (tests: ThreadSanitizer must report it).
struct ReleaseInfo {
    Release rel;
    bool ok = false;   // rel is valid
    bool done = false; // the check has finished (ok or not)
    std::string err;
};
extern bool g_release_lock_fix;
class ReleaseBox {
public:
    ReleaseInfo get() const
    {
        if (!g_release_lock_fix)
            return info_;
        std::lock_guard<std::mutex> l(mu_);
        return info_;
    }
    void set(const ReleaseInfo& i)
    {
        if (!g_release_lock_fix) {
            info_ = i;
            return;
        }
        std::lock_guard<std::mutex> l(mu_);
        info_ = i;
    }

private:
    mutable std::mutex mu_;
    ReleaseInfo info_;
};

// the hash of `file` in a SHA256SUMS.txt ("<hex>  <name>" lines), "" if absent
std::string sums_lookup(const std::string& sums, const std::string& file);

} // namespace upd
