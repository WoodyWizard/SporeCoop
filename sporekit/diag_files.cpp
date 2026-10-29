// The game files part of the environment for support (docs/SUPPORT.md#support-environment):
//  - other SporeKit mods in mods\ (loaded or not) and DLLs next to the exe that the game does not ship
//    (ModAPI, wrappers); the loaded hooks are in diag.cpp's foreign_hooks()
//  - .package files in the game's Data folders that the game does not ship (mods, ModAPI packs) and the
//    player's own packages (AppData\Roaming\Spore), name, size and date, a capped list
//  - known game files whose size is not the one of the build (a patched or broken install)
// Computed once (a few directory listings, no file is read) and cached; names only, never a full path.
#include "core.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace sk {

namespace {

struct Known {
    const char* rel; // relative to the game folder (the parent of SporebinEP1)
    uint64_t size;
};
// GOG 2024 (SporeApp.exe dc04aee5, 24 895 536 bytes): sizes of a clean install (lab lanes, 2026-10-29)
const Known kGog2024[] = {
    {"Data\\PatchData.package", 80428851},
    {"Data\\Spore_Audio1.package", 1019011830},
    {"Data\\Spore_Audio2.package", 1021382650},
    {"Data\\Spore_Content.package", 995268083},
    {"Data\\Spore_Game.package", 20210108},
    {"Data\\Spore_Graphics.package", 942881325},
    {"Data\\Spore_Pack_03.package", 9580183},
    {"DataEP1\\Spore_EP1_Content_01.package", 476555658},
    {"DataEP1\\Spore_EP1_Content_02.package", 757541293},
    {"DataEP1\\Spore_EP1_Data.package", 715614770},
    {"DataEP1\\Spore_EP1_Locale_01.package", 3794380},
    {"DataEP1\\Spore_EP1_Locale_02.package", 838289},
    {"SporebinEP1\\SporeApp.exe", 24895536},
};
// packages every install has whose size depends on the language or the store: never "extra"
const char* const kGamePackages[] = {
    "patchdata.package",         "spore_audio1.package",         "spore_audio2.package",
    "spore_content.package",     "spore_game.package",           "spore_graphics.package",
    "spore_pack_03.package",     "spore_ep1_content_01.package", "spore_ep1_content_02.package",
    "spore_ep1_data.package",    "spore_ep1_locale_01.package",  "spore_ep1_locale_02.package",
    "sporecoop.package", // ours
};
// the player's folder (AppData\Roaming\Spore): the game's own caches and databases
const char* const kUserPackages[] = {"editorsaves.package", "graphicscache.package", "planets.package",
                                     "pollination.package", "rigblockinfo.package"};
// DLLs next to SporeApp.exe of a GOG / Steam install with SporeCoop
const char* const kBinDlls[] = {"msvcr90.dll", "sporekit.dll", "steam_api.dll", "steam_api_orig.dll", "msvcp90.dll",
                                "steam_api.dll.orig"};
constexpr size_t kCap = 12; // lines per list

std::string lower(std::string s)
{
    for (auto& c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

struct Entry {
    std::string name;
    uint64_t size;
    FILETIME mtime;
};

std::vector<Entry> list(const std::wstring& dir, const wchar_t* pattern)
{
    std::vector<Entry> v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return v;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            v.push_back({narrow(fd.cFileName), ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, fd.ftLastWriteTime});
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return lower(a.name) < lower(b.name); });
    return v;
}

std::string date(const FILETIME& ft)
{
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    char b[16];
    snprintf(b, sizeof b, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return b;
}

std::string size_text(uint64_t n)
{
    char b[32];
    if (n >= (10ull << 20))
        snprintf(b, sizeof b, "%llu MB", (unsigned long long)(n >> 20));
    else if (n >= 10240)
        snprintf(b, sizeof b, "%llu KB", (unsigned long long)(n >> 10));
    else
        snprintf(b, sizeof b, "%llu B", (unsigned long long)n);
    return b;
}

bool in(const char* const* set, size_t n, const std::string& name)
{
    std::string l = lower(name);
    for (size_t i = 0; i < n; i++)
        if (l == set[i])
            return true;
    return false;
}

// "<label>: none" or "<label>: a (size, date), b (...)" capped at kCap, "+N more"
std::string capped(const char* label, const std::vector<Entry>& v)
{
    std::string s = std::string(label) + ": ";
    if (v.empty())
        return s + "none\n";
    for (size_t i = 0; i < v.size() && i < kCap; i++)
        s += (i ? ", " : "") + v[i].name + " (" + size_text(v[i].size) + ", " + date(v[i].mtime) + ")";
    if (v.size() > kCap)
        s += ", +" + std::to_string(v.size() - kCap) + " more";
    return s + "\n";
}

std::string compute()
{
    std::wstring bin = game_dir(), root = bin;
    size_t k = root.find_last_of(L"\\/");
    if (k != std::wstring::npos)
        root.resize(k);
    std::string s;
    // other SporeKit mods in mods\ (the loaded ones are in "mods:" of the environment)
    std::vector<Entry> mods;
    for (auto& e : list(mods_dirw(), L"*.dll"))
        if (lower(e.name) != "coop.dll")
            mods.push_back(e);
    s += capped("other SporeKit mods in mods\\", mods);
    // DLLs next to the exe that the game does not ship (ModAPI, d3d9 / dinput8 wrappers, ...)
    std::vector<Entry> dlls;
    for (auto& e : list(bin, L"*.dll"))
        if (!in(kBinDlls, std::size(kBinDlls), e.name) && lower(e.name).rfind("goggame-", 0) != 0)
            dlls.push_back(e);
    s += capped("extra DLLs next to SporeApp.exe", dlls);
    // ModAPI Launcher Kit leaves its folder next to the game or a mLibs folder
    for (const wchar_t* d : {L"\\mLibs", L"\\ModAPI"})
        if (GetFileAttributesW((bin + d).c_str()) != INVALID_FILE_ATTRIBUTES)
            s += "found SporebinEP1" + narrow(d) + " (Spore ModAPI)\n";
    // extra packages in the game's Data folders
    for (const wchar_t* d : {L"Data", L"DataEP1"}) {
        std::vector<Entry> extra;
        for (auto& e : list(root + L"\\" + d, L"*.package"))
            if (!in(kGamePackages, std::size(kGamePackages), e.name))
                extra.push_back(e);
        s += capped(("extra packages in " + narrow(d) + "\\").c_str(), extra);
    }
    // the player's own folder (the profile of a test game: SporeKit-<name>)
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring user = std::wstring(appdata) + L"\\Spore";
        std::vector<Entry> own;
        for (auto& e : list(user, L"*.package"))
            if (!in(kUserPackages, std::size(kUserPackages), e.name) && lower(e.name).rfind("graphicscache", 0) != 0)
                own.push_back(e);
        for (auto& e : list(user + L"\\Data", L"*.package"))
            own.push_back({"Data\\" + e.name, e.size, e.mtime});
        s += capped("extra packages in AppData\\Roaming\\Spore", own);
    }
    // known files of the build with another size
    std::string build = exe_sha256();
    if (build.rfind("dc04aee5", 0) == 0) {
        std::string bad;
        int checked = 0;
        for (auto& f : kGog2024) {
            WIN32_FILE_ATTRIBUTE_DATA fa;
            std::wstring p = root + L"\\" + widen(f.rel);
            if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) {
                bad += std::string(bad.empty() ? "" : ", ") + f.rel + " missing";
                continue;
            }
            checked++;
            uint64_t n = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            if (n != f.size)
                bad += std::string(bad.empty() ? "" : ", ") + f.rel + " " + std::to_string(n) + " bytes (expected " +
                       std::to_string(f.size) + ")";
        }
        s += "game files of GOG 2024 (" + std::to_string(checked) + " checked): " + (bad.empty() ? "sizes as expected" : bad) +
             "\n";
    } else
        s += "game files: the expected sizes are known for GOG 2024 only (this exe " + build.substr(0, 8) + ")\n";
    return s;
}

std::mutex g_mu;
std::string g_text;
bool g_done;

} // namespace

const std::string& diag_files_text()
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_done) {
        g_text = compute();
        g_done = true;
    }
    return g_text;
}

} // namespace sk
