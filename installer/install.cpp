#include "install.h"

#include <windows.h>

#include <vector>

namespace inst {

bool g_fix = true;

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool is_game(const std::wstring& root) { return !root.empty() && exists(root + L"\\SporebinEP1\\SporeApp.exe"); }

bool g_ga_fix = true; // not a lab switch: setup only
bool g_ga_dir_fix = true; // not a lab switch: setup only

const char* const kManualInstall =
    "Install it by hand: download SporeCoop-<version>.zip from github.com/WoodyWizard/SporeCoop/releases, "
    "extract it into the game's SporebinEP1 folder and run install.bat (docs/INSTALL.md).";

static std::wstring trim_slash(std::wstring p)
{
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    return p;
}

bool spore_without_ga(const std::wstring& root)
{
    return !root.empty() && !is_game(root) && exists(root + L"\\SporeBin") && !exists(root + L"\\SporebinEP1\\SporeApp.exe");
}

std::wstring game_root_of(const std::wstring& picked)
{
    std::wstring r = trim_slash(picked);
    if (r.empty() || is_game(r))
        return r;
    size_t k = r.find_last_of(L"\\/");
    if (k == std::wstring::npos)
        return r;
    std::wstring up = r.substr(0, k), leaf = r.substr(k + 1);
    if (up.size() == 2 && up[1] == L':')
        up += L'\\'; // "D:\SporeBin" -> "D:\"
    if (is_game(up))
        return up; // picked SporebinEP1 itself (the old rule)
    if (g_ga_fix && (!_wcsicmp(leaf.c_str(), L"SporeBin") || !_wcsicmp(leaf.c_str(), L"SporebinEP1")))
        return up; // BUG-0391: SporeBin of a Spore without Galactic Adventures: its Spore folder
    return r;
}

std::string folder_problem(const std::wstring& root)
{
    if (is_game(root))
        return {};
    if (!g_ga_fix)
        return "SporeApp.exe is not in this folder's SporebinEP1. Choose the SPORE folder.";
    if (root.empty())
        return "Choose the SPORE folder (the one with SporebinEP1).";
    if (spore_without_ga(root))
        return g_ga_dir_fix
                   ? "Galactic Adventures is not installed: SporeCoop needs Spore + Galactic Adventures (on Steam: "
                     "the Spore Galactic Adventures DLC, which Steam installs into steamapps\\common\\Spore 24720). "
                     "This Spore folder has no SporebinEP1."
                   : "Galactic Adventures is not installed: SporeCoop needs Spore + Galactic Adventures (on Steam: "
                     "the Spore Galactic Adventures DLC). This Spore folder has no SporebinEP1.";
    return "SporeApp.exe is not in this folder's SporebinEP1. Choose the SPORE folder (the one with SporebinEP1).";
}

// the key / string value pairs of a Valve KeyValues text (libraryfolders.vdf, appmanifest_*.acf):
// quoted strings with \\ and \" escapes, braces open and close sections (not kept), // comments
static std::vector<std::pair<std::string, std::string>> vdf_pairs(const std::string& vdf)
{
    std::vector<std::string> tok;
    std::vector<bool> is_str;
    for (size_t i = 0; i < vdf.size();) {
        char c = vdf[i];
        if (c == '"') {
            std::string t;
            for (i++; i < vdf.size() && vdf[i] != '"'; i++) {
                if (vdf[i] == '\\' && i + 1 < vdf.size())
                    i++;
                t += vdf[i];
            }
            i++;
            tok.push_back(t);
            is_str.push_back(true);
        } else if (c == '{' || c == '}') {
            tok.push_back(std::string(1, c));
            is_str.push_back(false);
            i++;
        } else if (c == '/' && i + 1 < vdf.size() && vdf[i + 1] == '/') {
            while (i < vdf.size() && vdf[i] != '\n')
                i++;
        } else
            i++;
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t i = 0; i + 1 < tok.size(); i++)
        if (is_str[i] && is_str[i + 1]) {
            out.emplace_back(tok[i], tok[i + 1]);
            i++; // the value is not a key
        }
    return out;
}

std::vector<std::wstring> steam_libraries(const std::string& vdf)
{
    std::vector<std::wstring> out;
    for (auto& [k, v] : vdf_pairs(vdf)) {
        bool digits = !k.empty() && k.find_first_not_of("0123456789") == std::string::npos;
        if (k == "path" || (digits && v.find(':') != std::string::npos)) // new format; the old "1" "D:\..."
            out.push_back(upd::widen(v));
    }
    return out;
}

std::string acf_installdir(const std::string& acf)
{
    for (auto& [k, v] : vdf_pairs(acf))
        if (!_stricmp(k.c_str(), "installdir"))
            return v;
    return {};
}

static std::string read_text(const std::wstring& p)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    std::string b(64 << 10, '\0');
    DWORD got = 0;
    ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr);
    CloseHandle(f);
    b.resize(got);
    return b;
}

static std::wstring parent_of(const std::wstring& p)
{
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? std::wstring() : p.substr(0, k);
}

// the Galactic Adventures folders a steamapps\common folder may hold: the GA app's installdir from
// steamapps\appmanifest_24720.acf, then "Spore 24720" (Steam installs GA as its own app 24720)
static std::vector<std::wstring> ga_in_common(const std::wstring& common)
{
    std::vector<std::wstring> out;
    std::string dir = acf_installdir(read_text(parent_of(common) + L"\\appmanifest_24720.acf"));
    if (!dir.empty() && dir.find_first_of("\\/:") == std::string::npos && dir != "." && dir != "..")
        out.push_back(common + L"\\" + upd::widen(dir));
    out.push_back(common + L"\\Spore 24720"); // the file system ignores case (Steam writes "spore 24720")
    return out;
}

std::vector<std::wstring> ga_folders(const std::wstring& library)
{
    return ga_in_common(trim_slash(library) + L"\\steamapps\\common");
}

std::wstring resolve_root(const std::wstring& picked, const std::vector<std::wstring>& libraries, std::string* note)
{
    if (note)
        note->clear();
    std::wstring r = game_root_of(picked);
    if (!g_ga_fix || !g_ga_dir_fix || r.empty() || is_game(r))
        return r;
    // Steam: base Spore (app 17390) in common\Spore, Galactic Adventures (app 24720) next to it in
    // common\Spore 24720; the player chose the first (BUG-0391). Also: common itself was chosen.
    std::vector<std::wstring> c = ga_in_common(r);
    if (spore_without_ga(r)) {
        for (auto& g : ga_in_common(parent_of(r)))
            c.push_back(g);
        for (auto& l : libraries)
            for (auto& g : ga_folders(l))
                c.push_back(g);
    }
    for (auto& g : c)
        if (is_game(g)) {
            if (note)
                *note = "Galactic Adventures is installed in " + upd::narrow(g) + ": using that folder.";
            return g;
        }
    return r;
}

std::wstring pick_game(const std::vector<std::wstring>& candidates)
{
    for (auto& c : candidates)
        if (is_game(trim_slash(c)))
            return trim_slash(c);
    if (g_ga_fix)
        for (auto& c : candidates)
            if (spore_without_ga(trim_slash(c)))
                return trim_slash(c);
    return {};
}

std::wstring local_pack(const Options& opt)
{
    if (!opt.pack.empty())
        return opt.pack;
    std::vector<std::string> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((opt.pack_dir + L"\\SporeCoop-*.pack").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            names.push_back(upd::narrow(fd.cFileName));
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::string n = g_fix ? upd::pick_pack(names) : names.empty() ? std::string() : names[0]; // old: the first listed
    return n.empty() ? std::wstring() : opt.pack_dir + L"\\" + upd::widen(n);
}

Result do_install(const std::wstring& root, const Options& opt, upd::ReleaseBox& box, std::string& msg,
                  const upd::Progress& progress, std::string* older_version, std::string* installed)
{
    if (!is_game(root)) {
        msg = g_ga_fix ? folder_problem(root) : "SporeApp.exe is not in " + upd::narrow(root) + "\\SporebinEP1";
        return Result::Failed;
    }
    upd::apply_pending(root); // an in-game update left files for the next start
    upd::Pack pack;
    std::string err;
    std::wstring lp = local_pack(opt);
    bool have_local = !lp.empty() && upd::read_pack_file(lp, pack, err);
    upd::ReleaseInfo ri = box.get(); // BUG-0077: a copy; the check thread may set the box meanwhile
    if (!have_local || (ri.ok && upd::compare_versions(ri.rel.version, pack.version) > 0)) {
        if (!ri.ok) {
            if (!upd::latest_release(opt.api, ri.rel, err)) {
                msg = "cannot get SporeCoop from GitHub: " + err + ". " + kManualInstall;
                return Result::Failed;
            }
            ri.ok = ri.done = true;
            ri.err.clear();
            box.set(ri);
        }
        if (!upd::download_pack(ri.rel, pack, err, progress)) {
            msg = "download failed: " + err + ". " + kManualInstall;
            return Result::Failed;
        }
    }
    // BUG-0094: never an older version over a newer one without the player's yes
    std::string have = upd::installed_version(root);
    if (g_fix && !opt.downgrade && !have.empty() && upd::compare_versions(pack.version, have) < 0) {
        if (older_version)
            *older_version = pack.version;
        if (installed)
            *installed = have;
        msg = "SporeCoop " + have + " is installed and the setup has only the older " + pack.version +
              ": nothing changed (/downgrade installs it anyway)";
        return Result::Older;
    }
    if (!upd::apply_pack(root, pack, err)) {
        msg = err;
        return Result::Failed;
    }
    upd::cleanup_old(root);
    msg = "SporeCoop " + pack.version + " is installed. Start Spore: the galaxy menu has a Co-op item.";
    return Result::Ok;
}

bool do_uninstall(const std::wstring& root, std::string& msg)
{
    upd::apply_pending(root);
    std::wstring bin = root + L"\\SporebinEP1";
    std::wstring sa = bin + L"\\steam_api.dll", orig = bin + L"\\steam_api_orig.dll";
    if (exists(orig) && upd::is_loader(sa)) {
        std::wstring old = sa + SC_OLD_SUFFIX;
        DeleteFileW(old.c_str());
        bool moved = MoveFileExW(sa.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (moved && upd::g_swap_delay_ms > 0) // tests (BUG-0139): a wide window between the two moves
            Sleep((DWORD)upd::g_swap_delay_ms);
        if (!moved || !MoveFileExW(orig.c_str(), sa.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            msg = "cannot restore steam_api.dll (run as administrator? is Spore closed?)";
            return false;
        }
    }
    for (const wchar_t* f : {L"\\sporekit.dll", L"\\mods\\coop.dll", L"\\mods\\sporecoop.version"})
        if (exists(bin + f) && !DeleteFileW((bin + f).c_str()))
            MoveFileExW((bin + f).c_str(), (bin + f + SC_OLD_SUFFIX).c_str(), MOVEFILE_REPLACE_EXISTING); // in use
    WIN32_FIND_DATAW fd;
    std::wstring sym = bin + L"\\mods\\sporekit\\symbols";
    HANDLE h = FindFirstFileW((sym + L"\\*.sym").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            DeleteFileW((sym + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    DeleteFileW((root + L"\\Data\\SporeCoop.package").c_str());
    upd::cleanup_old(root);
    msg = "SporeCoop is removed (your settings and logs in SporebinEP1\\mods are kept).";
    return true;
}

} // namespace inst
