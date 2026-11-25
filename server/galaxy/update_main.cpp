// The galaxy server's update check (owner 2026-11-24: "Can we allow the Galaxy Server to check for updates before
// running?"; BUG-9378). Windows only, built by tools/build_galaxy.sh as build/galaxy_update.exe from the game's own
// updater (updater/): the same release check, the same checks of what was downloaded (the release's SHA256SUMS.txt,
// the pack's own hashes, the allowed paths) and the same file swap. Start-Galaxy-Server.cmd runs it before the server.
//
//   galaxy_update.exe --check    exit 0: up to date, or the check could not be made (it says which); 10: a newer release
//   galaxy_update.exe --apply    downloads the newest release's server pack, checks it, installs it; exit 0 done, 1 not
//
// The server lives in <game>\SporebinEP1\mods\galaxy-server (the setup puts it there, decision 8Y C): the game folder
// is three folders up, and the pack is applied to it as the setup does. A server pack writes only into the server's
// folder. SPORECOOP_UPDATE_API (tests): another release address than GitHub's.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../../updater/pack.h"
#include "../../updater/sha256.h"
#include "../../updater/winupdate.h"

namespace {

std::wstring own_dir()
{
    wchar_t p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
    std::wstring s(p, n);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

// the game folder this server sits in, "" when the folder is somewhere else
std::wstring game_root(const std::wstring& dir)
{
    const std::wstring tail = L"\\SporebinEP1\\mods\\galaxy-server";
    if (dir.size() <= tail.size() || _wcsicmp(dir.c_str() + dir.size() - tail.size(), tail.c_str()) != 0)
        return {};
    return dir.substr(0, dir.size() - tail.size());
}

// the version of the server's files: galaxy-server.version, a file of the server's pack ("" when missing)
std::string own_version(const std::wstring& dir)
{
    HANDLE f = CreateFileW((dir + L"\\galaxy-server.version").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    char b[128];
    DWORD got = 0;
    ReadFile(f, b, sizeof b, &got, nullptr);
    CloseHandle(f);
    std::string v(b, got);
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' '))
        v.pop_back();
    return v;
}

std::string api_url()
{
    char b[512];
    DWORD n = GetEnvironmentVariableA("SPORECOOP_UPDATE_API", b, sizeof b);
    return n > 0 && n < sizeof b ? std::string(b, n) : std::string(upd::kLineagesPreviewUrl);
}

// the server pack of release `r`: next to the release's SHA256SUMS.txt, checked against it, parsed, of the release's
// version and with no file outside the server's folder
bool download_server_pack(const upd::Release& r, upd::Pack& out, std::string& err)
{
    if (r.sums_url.empty()) {
        err = "the release " + r.tag + " has no checksum file";
        return false;
    }
    const std::string name = upd::server_pack_name(r.version);
    const std::string url = r.sums_url.substr(0, r.sums_url.find_last_of('/') + 1) + name;
    std::vector<uint8_t> sums, pack;
    if (!upd::http_get(r.sums_url, sums, err, {}, 1u << 20))
        return false;
    const std::string want = upd::sums_lookup(std::string(sums.begin(), sums.end()), name);
    if (want.empty()) {
        err = "the release " + r.tag + " has no galaxy server (" + name + ")";
        return false;
    }
    uint64_t shown = 0;
    if (!upd::http_get(url, pack, err, [&](uint64_t got, uint64_t) {
            if (got - shown >= (256u << 10)) {
                shown = got;
                printf(".");
                fflush(stdout);
            }
        }, 32u << 20))
        return false;
    printf("\n");
    if (upd::sha256_hex(pack) != want) {
        err = "the downloaded update does not match the release checksum";
        return false;
    }
    if (!upd::parse_pack(pack, out, err))
        return false;
    if (out.version != r.version) {
        err = "the release " + r.tag + " holds the server of version " + out.version;
        return false;
    }
    for (const auto& f : out.files)
        if (f.path.rfind(upd::kServerPrefix, 0) != 0) {
            err = "the server's pack names a file outside the server's folder (" + f.path + ")";
            return false;
        }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "--check" && mode != "--apply") {
        printf("usage: galaxy_update --check | --apply\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const std::wstring dir = own_dir(), root = game_root(dir);
    const std::string mine = own_version(dir);
    if (!root.empty())
        upd::cleanup_old(root); // the backups of the last update (this program's own could not go while it ran)
    upd::Release rel;
    std::string err;
    if (!upd::latest_release(api_url(), rel, err)) {
        printf("Update check: GitHub could not be asked (%s). The server starts as it is.\n", err.c_str());
        return mode == "--apply" ? 1 : 0;
    }
    if (!mine.empty() && upd::compare_versions(rel.version, mine) <= 0) {
        printf("Update check: this galaxy server is up to date (%s).\n", mine.c_str());
        return 0;
    }
    if (mode == "--check") {
        printf("Update check: Lineages %s is out (this server: %s).\n"
               "Players who update their game can only join once the server is updated too.\n",
               rel.version.c_str(), mine.empty() ? "unknown" : mine.c_str());
        return 10;
    }
    if (root.empty()) {
        printf("Update: this folder is not SporebinEP1\\mods\\galaxy-server of a Spore game folder, so the server cannot\n"
               "update itself here. Run the new SporeCoopSetup with \"Also install the galaxy server\" instead.\n");
        return 1;
    }
    upd::apply_pending(root); // swaps an earlier update left for a file that was in use
    printf("Update: downloading the galaxy server %s ", rel.version.c_str());
    upd::Pack pack;
    if (!download_server_pack(rel, pack, err)) {
        printf("\nUpdate failed: %s. Nothing was changed.\n", err.c_str());
        return 1;
    }
    int pending = 0;
    if (!upd::apply_pack(root, pack, err, &pending)) {
        printf("Update failed: %s. Nothing was changed.\n", err.c_str());
        return 1;
    }
    upd::cleanup_old(root);
    if (pending)
        printf("Update: %d file(s) were in use (is the server still running in another window?): they are replaced the "
               "next time Spore or the setup starts.\n", pending);
    printf("Update: the galaxy server is now %s.\n", rel.version.c_str());
    return 0;
}
