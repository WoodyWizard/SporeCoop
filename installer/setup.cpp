// SporeCoopSetup.exe: install, update and uninstall SporeCoop with one button.
// Finds the game (GOG and EA registry keys, the folder the setup runs from, common folders, or
// Browse), tells whether the game build is the tested one, installs the newest release from the
// project's GitHub releases (or the update pack next to the setup, offline) and removes it again.
//
// Silent mode for scripts and tests:
//   SporeCoopSetup.exe /S [/dir:<game folder>] [/pack:<file.pack>] [/api:<releases/latest url>]
//                      [/uninstall] [/downgrade] [/log:<file>]       exit code 0 = done
// (/downgrade: install the setup's pack also when it is older than the installed SporeCoop)
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <atomic>
#include <string>
#include <vector>

#include "../updater/sha256.h"
#include "install.h"
#include "../updater/winupdate.h"

#ifndef SETUP_VERSION
#define SETUP_VERSION "dev"
#endif

namespace {

// builds with a symbol database (data/symbols/*_<first 8 hex>.sym)
constexpr const char* kGog2016Sha = "922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e";
constexpr const char* kGog2024Sha = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
constexpr const char* kGog2024Code = "5a8267b23a1b916d7e920942d8375ea7fdb2d10a12aca0e603168a8a649f5f8f"; // code_sha256
constexpr const char* kSteamSha = "3010a413f9a4a05ee9c4de24756c1c94c7fb218f70cad29bcf5bc0b4abfa2466";
constexpr const char* kGog2024Data = "ae1f385a50edee3b3159289eb7074556756b8f6d60a76d111172aba836f1eda9"; // data_sha256
constexpr UINT WM_DONE = WM_APP + 1, WM_INFO = WM_APP + 2;

struct Options {
    bool silent = false, uninstall = false;
    std::wstring dir, log;
    inst::Options inst;
} g_opt;

HWND g_wnd, g_dir, g_game, g_inst, g_latest, g_status, g_install, g_remove, g_close;
HFONT g_font, g_bold;
// BUG-0077: the release check's result, shared by the check thread, the window and the install job
// (each copies it under the box's lock; the job never writes a global the others read)
upd::ReleaseBox g_rel;
std::atomic<bool> g_busy{false};
HANDLE g_job; // BUG-0139: the running job's thread (waited for before the setup ends)

using inst::is_game;

std::wstring trim_slash(std::wstring p)
{
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    return p;
}

std::wstring reg_str(HKEY root, const wchar_t* key, const wchar_t* value)
{
    wchar_t b[MAX_PATH * 2] = {};
    DWORD sz = sizeof b;
    for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY})
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | view, nullptr, b, &sz) == ERROR_SUCCESS)
            return b;
    return {};
}

std::wstring parent(std::wstring p)
{
    p = trim_slash(p);
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"" : p.substr(0, k);
}

// the game folder (the one with SporebinEP1 and Data)
std::wstring find_game()
{
    std::vector<std::wstring> c;
    c.push_back(reg_str(HKEY_LOCAL_MACHINE, L"Software\\GOG.com\\Games\\1948823323", L"PATH"));
    c.push_back(parent(parent(reg_str(HKEY_LOCAL_MACHINE, L"Software\\GOG.com\\Games\\1948823323", L"EXE"))));
    c.push_back(reg_str(HKEY_LOCAL_MACHINE, L"Software\\electronic arts\\spore", L"installloc"));
    c.push_back(reg_str(HKEY_LOCAL_MACHINE, L"Software\\electronic arts\\SPORE(TM)", L"InstallLoc"));
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring here = parent(self);
    c.push_back(here);          // SPORE\SporeCoopSetup.exe
    c.push_back(parent(here));  // SPORE\SporebinEP1\SporeCoopSetup.exe
    for (const wchar_t* d : {L"C:\\GOG Games\\SPORE", L"C:\\Program Files (x86)\\GOG Galaxy\\Games\\SPORE",
                             L"C:\\Program Files (x86)\\GOG.com\\SPORE", L"C:\\Program Files (x86)\\Electronic Arts\\SPORE",
                             L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Spore"})
        c.push_back(d);
    for (auto& p : c) {
        std::wstring r = trim_slash(p);
        if (is_game(r))
            return r;
    }
    return {};
}

std::vector<uint8_t> read_file(const std::wstring& path)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::vector<uint8_t> b((size_t)sz.QuadPart);
    DWORD got = 0;
    ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr);
    CloseHandle(f);
    b.resize(got);
    return b;
}

// code_sha256 (the definition is in sporekit/sha256.cpp and tools/exe_info.py): ImageBase,
// AddressOfEntryPoint, then every section but .rsrc (name, address, size, raw bytes).
// data: data_sha256 instead (no entry point, no .text and no .bind: a packed exe, Steam's DRM)
std::string code_sha(const std::vector<uint8_t>& d, bool data = false)
{
    auto u16 = [&](size_t o) -> uint32_t { return o + 2 <= d.size() ? d[o] | d[o + 1] << 8 : 0; };
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= d.size() ? u16(o) | u16(o + 2) << 16 : 0; };
    if (d.size() < 0x40 || u16(0) != 0x5A4D)
        return {};
    size_t pe = u32(0x3C);
    if (u32(pe) != 0x00004550 || u16(pe + 24) != 0x10B)
        return {};
    size_t nsec = u16(pe + 6), opt = pe + 24, sec = opt + u16(pe + 20);
    std::vector<uint8_t> m;
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++)
            m.push_back((uint8_t)(v >> (8 * i)));
    };
    put32(u32(opt + 28)); // ImageBase
    if (!data)
        put32(u32(opt + 16)); // AddressOfEntryPoint
    for (size_t i = 0; i < nsec; i++) {
        size_t o = sec + i * 40;
        if (o + 40 > d.size())
            return {};
        if (memcmp(&d[o], ".rsrc\0\0\0", 8) == 0)
            continue;
        if (data && (!memcmp(&d[o], ".text\0\0\0", 8) || !memcmp(&d[o], ".bind\0\0\0", 8)))
            continue;
        uint32_t vsize = u32(o + 8), va = u32(o + 12), rsize = u32(o + 16), raw = u32(o + 20);
        uint32_t n = vsize < rsize ? vsize : rsize;
        if ((size_t)raw + n > d.size())
            return {};
        m.insert(m.end(), d.begin() + o, d.begin() + o + 8);
        put32(va);
        put32(vsize);
        m.insert(m.end(), d.begin() + raw, d.begin() + raw + n);
    }
    return upd::sha256_hex(m);
}

bool game_running()
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{sizeof e};
    bool found = false;
    for (BOOL ok = Process32FirstW(s, &e); ok; ok = Process32NextW(s, &e))
        if (!lstrcmpiW(e.szExeFile, L"SporeApp.exe"))
            found = true;
    CloseHandle(s);
    return found;
}

void log(const std::string& s)
{
    if (g_opt.log.empty())
        return;
    HANDLE f = CreateFileW(g_opt.log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    std::string l = s + "\r\n";
    DWORD w;
    WriteFile(f, l.data(), (DWORD)l.size(), &w, nullptr);
    CloseHandle(f);
}

std::wstring self_dir()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return parent(self);
}

// ---------------- window ----------------
std::wstring dir_text()
{
    wchar_t b[MAX_PATH * 2];
    GetWindowTextW(g_dir, b, MAX_PATH * 2);
    return trim_slash(b);
}

void refresh()
{
    std::wstring root = dir_text();
    std::wstring game;
    if (!is_game(root))
        game = L"Game: SporeApp.exe is not in this folder's SporebinEP1. Choose the SPORE folder.";
    else if (std::vector<uint8_t> exe = read_file(root + L"\\SporebinEP1\\SporeApp.exe");
             upd::sha256_hex(exe) == kGog2016Sha)
        game = L"Game: Spore Galactic Adventures, GOG 2016 build (supported)";
    else if (upd::sha256_hex(exe) == kGog2024Sha)
        game = L"Game: Spore Galactic Adventures, GOG 2024 build (supported)";
    else if (code_sha(exe) == kGog2024Code)
        game = L"Game: Spore Galactic Adventures, same code as the GOG 2024 build (should work, not tested)";
    else if (upd::sha256_hex(exe) == kSteamSha)
        game = L"Game: Spore Galactic Adventures, Steam build (supported)";
    else if (code_sha(exe, true) == kGog2024Data)
        game = L"Game: Spore Galactic Adventures, the GOG 2024 build under Steam's DRM (should work)";
    else
        game = L"Game: Spore Galactic Adventures, a build SporeCoop has not been tested with yet";
    SetWindowTextW(g_game, game.c_str());
    std::string v = is_game(root) ? upd::installed_version(root) : "";
    SetWindowTextW(g_inst, upd::widen("Installed: " + (v.empty() ? std::string("not installed") : "SporeCoop " + v)).c_str());
    upd::ReleaseInfo ri = g_rel.get();
    std::string latest = ri.ok ? "Newest release: SporeCoop " + ri.rel.version
                         : !ri.done ? "Newest release: checking GitHub..."
                                    : "Newest release: GitHub not reachable (" + ri.err + ")";
    SetWindowTextW(g_latest, upd::widen(latest).c_str());
    // BUG-0094: the version the button installs, as do_install chooses it: the pack next to the
    // setup, or GitHub's release when that is newer
    std::string next = upd::pack_version_of(upd::narrow(inst::local_pack(g_opt.inst)));
    if (ri.ok && (next.empty() || upd::compare_versions(ri.rel.version, next) > 0))
        next = ri.rel.version;
    if (!inst::g_fix) // the old label: GitHub's release only
        next = ri.ok ? ri.rel.version : "";
    int cmp = next.empty() || v.empty() ? 0 : upd::compare_versions(next, v);
    std::string btn = v.empty() ? "Install"
                      : cmp > 0 ? "Update to " + next
                      : cmp < 0 && inst::g_fix ? "Install older " + next
                                               : "Reinstall";
    SetWindowTextW(g_install, upd::widen(btn).c_str());
    EnableWindow(g_install, is_game(root) && !g_busy);
    EnableWindow(g_remove, !v.empty() && !g_busy);
    if (g_close && inst::g_fix) // BUG-0139: no closing while files are swapped
        EnableWindow(g_close, !g_busy);
}

DWORD WINAPI check_thread(void*)
{
    upd::ReleaseInfo ri;
    ri.ok = upd::latest_release(g_opt.inst.api, ri.rel, ri.err);
    ri.done = true;
    if (ri.ok)
        ri.err.clear();
    g_rel.merge(ri); // an install job may have fetched it already (one lock: BUG-0244)
    PostMessageW(g_wnd, WM_INFO, 0, 0);
    return 0;
}

struct Job {
    bool uninstall;
    std::wstring root;
    inst::Options opt;
    std::string msg;
    bool ok;
    inst::Result res = inst::Result::Failed;
    std::string older, installed; // res == Older
};

DWORD WINAPI job_thread(void* p)
{
    auto* j = (Job*)p;
    if (j->uninstall)
        j->ok = inst::do_uninstall(j->root, j->msg);
    else {
        j->res = inst::do_install(j->root, j->opt, g_rel, j->msg, [](uint64_t got, uint64_t total) {
            if (g_status && total) {
                wchar_t b[96];
                swprintf(b, 96, L"Downloading SporeCoop... %u %%", (unsigned)(got * 100 / total));
                SetWindowTextW(g_status, b);
            }
        }, &j->older, &j->installed);
        j->ok = j->res == inst::Result::Ok;
    }
    PostMessageW(g_wnd, WM_DONE, 0, (LPARAM)j);
    return 0;
}

bool g_test_mode; // SPORECOOP_SETUP_TEST_DELAY set: no "Spore is running" question (tests)

void start_job(bool uninstall, bool downgrade = false)
{
    if (g_busy)
        return;
    if (!g_test_mode && game_running() &&
        MessageBoxW(g_wnd, L"Spore is running. Close it first for a clean install.\n\nContinue anyway?",
                    L"SporeCoop Setup", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    g_busy = true;
    SetWindowTextW(g_status, uninstall ? L"Removing SporeCoop..." : L"Installing SporeCoop...");
    refresh();
    auto* j = new Job{uninstall, dir_text(), g_opt.inst, {}, false};
    j->opt.downgrade = j->opt.downgrade || downgrade;
    if (g_job)
        CloseHandle(g_job);
    g_job = CreateThread(nullptr, 0, job_thread, j, 0, nullptr);
}

void elevate()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring args = L"\"/dir:" + dir_text() + L"\"";
    if ((INT_PTR)ShellExecuteW(g_wnd, L"runas", self, args.c_str(), nullptr, SW_SHOWNORMAL) > 32)
        PostQuitMessage(0);
}

HWND add(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0, bool bold = false)
{
    HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)(bold ? g_bold : g_font), TRUE);
    return c;
}

enum { ID_BROWSE = 10, ID_INSTALL, ID_REMOVE, ID_CLOSE, ID_DIR };

LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        g_wnd = w;
        NONCLIENTMETRICSW nm{sizeof nm};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0);
        g_font = CreateFontIndirectW(&nm.lfMessageFont);
        LOGFONTW lf = nm.lfMessageFont;
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = lf.lfHeight * 3 / 2;
        g_bold = CreateFontIndirectW(&lf);
        add(L"STATIC", L"SporeCoop " SETUP_VERSION, 0, 20, 14, 520, 28, 0, true);
        add(L"STATIC", L"Play every stage of Spore together. This installs the mod into your game folder.", 0, 20,
            44, 520, 20);
        add(L"STATIC", L"Game folder (the one with SporebinEP1):", 0, 20, 78, 520, 18);
        g_dir = add(L"EDIT", upd::widen(upd::narrow(find_game())).c_str(), ES_AUTOHSCROLL, 20, 98, 420, 24, ID_DIR);
        add(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 448, 97, 92, 26, ID_BROWSE);
        g_game = add(L"STATIC", L"", 0, 20, 134, 520, 18);
        g_inst = add(L"STATIC", L"", 0, 20, 156, 520, 18);
        g_latest = add(L"STATIC", L"", 0, 20, 178, 520, 18);
        g_install = add(L"BUTTON", L"Install", BS_DEFPUSHBUTTON, 20, 214, 170, 32, ID_INSTALL);
        g_remove = add(L"BUTTON", L"Uninstall", BS_PUSHBUTTON, 200, 214, 120, 32, ID_REMOVE);
        g_close = add(L"BUTTON", L"Close", BS_PUSHBUTTON, 440, 214, 100, 32, ID_CLOSE);
        g_status = add(L"STATIC", L"", 0, 20, 258, 520, 36);
        add(L"STATIC", L"github.com/WoodyWizard/SporeCoop  -  every player needs the same SporeCoop version", 0, 20,
            300, 520, 18);
        refresh();
        CloseHandle(CreateThread(nullptr, 0, check_thread, nullptr, 0, nullptr));
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: {
            BROWSEINFOW bi{};
            bi.hwndOwner = w;
            bi.lpszTitle = L"Choose the SPORE folder (the one with SporebinEP1)";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            if (LPITEMIDLIST id = SHBrowseForFolderW(&bi)) {
                wchar_t p[MAX_PATH];
                if (SHGetPathFromIDListW(id, p)) {
                    std::wstring r = trim_slash(p);
                    if (!is_game(r) && is_game(parent(r)))
                        r = parent(r); // picked SporebinEP1 itself
                    SetWindowTextW(g_dir, r.c_str());
                }
                CoTaskMemFree(id);
            }
            refresh();
            return 0;
        }
        case ID_DIR:
            if (HIWORD(wp) == EN_CHANGE)
                refresh();
            return 0;
        case ID_INSTALL:
            start_job(false);
            return 0;
        case ID_REMOVE:
            start_job(true);
            return 0;
        case ID_CLOSE:
            if (inst::g_fix) {
                SendMessageW(w, WM_CLOSE, 0, 0);
                return 0;
            }
            DestroyWindow(w);
            return 0;
        }
        break;
    case WM_INFO:
        refresh();
        return 0;
    case WM_DONE: {
        auto* j = (Job*)lp;
        g_busy = false;
        SetWindowTextW(g_status, upd::widen(j->msg).c_str());
        bool denied = !j->ok && j->msg.find("administrator") != std::string::npos;
        refresh();
        if (j->res == inst::Result::Older && !g_opt.silent) {
            // BUG-0094: the setup only has an older version than the installed one
            std::wstring q = upd::widen("SporeCoop " + j->installed + " is installed. This setup has the older version " +
                                        j->older + ".\n\nEvery player needs the same version. Install " + j->older +
                                        " anyway?");
            bool yes = MessageBoxW(w, q.c_str(), L"SporeCoop Setup", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
            delete j;
            if (yes)
                start_job(false, true);
            return 0;
        }
        if (denied && MessageBoxW(w, L"Windows does not let normal programs change this folder.\n\nRestart the setup as "
                                     L"administrator?", L"SporeCoop Setup", MB_YESNO | MB_ICONQUESTION) == IDYES)
            elevate();
        delete j;
        return 0;
    }
    case WM_CLOSE:
        if (g_busy && inst::g_fix) {
            // BUG-0139: closing now killed the job between two file moves (a missing steam_api.dll)
            SetWindowTextW(g_status, L"Please wait: SporeCoop's files are being changed. The window closes after that.");
            MessageBeep(MB_ICONWARNING);
            return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

void parse_args()
{
    int n = 0;
    LPWSTR* a = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; i < n; i++) {
        std::wstring s = a[i];
        auto val = [&](const wchar_t* k) {
            size_t l = wcslen(k);
            return s.compare(0, l, k) == 0 ? s.substr(l) : std::wstring();
        };
        if (s == L"/S" || s == L"/s")
            g_opt.silent = true;
        else if (s == L"/uninstall")
            g_opt.uninstall = true;
        else if (!val(L"/dir:").empty())
            g_opt.dir = trim_slash(val(L"/dir:"));
        else if (s == L"/downgrade")
            g_opt.inst.downgrade = true;
        else if (s == L"/oldfix") // tests: the behaviour before BUG-0094 and BUG-0139
            inst::g_fix = false;
        else if (s == L"/oldmerge") // tests: the check thread's get-then-set before BUG-0244
            upd::g_release_merge_fix = false;
        else if (!val(L"/pack:").empty())
            g_opt.inst.pack = val(L"/pack:");
        else if (!val(L"/api:").empty())
            g_opt.inst.api = upd::narrow(val(L"/api:"));
        else if (!val(L"/log:").empty())
            g_opt.log = val(L"/log:");
    }
    LocalFree(a);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show)
{
    parse_args();
    g_opt.inst.pack_dir = self_dir();
    wchar_t delay[16] = {};
    if (GetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DELAY", delay, 16)) { // tests (BUG-0139)
        upd::g_swap_delay_ms = _wtoi(delay);
        g_test_mode = true;
    }
    if (g_opt.silent) {
        std::wstring root = g_opt.dir.empty() ? find_game() : g_opt.dir;
        std::string msg;
        bool ok = g_opt.uninstall ? inst::do_uninstall(root, msg)
                                  : inst::do_install(root, g_opt.inst, g_rel, msg) == inst::Result::Ok;
        log(std::string(ok ? "OK: " : "FAILED: ") + msg);
        return ok ? 0 : 1;
    }
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&ic);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"SporeCoopSetup";
    RegisterClassW(&wc);
    RECT r{0, 0, 560, 330};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND w = CreateWindowW(L"SporeCoopSetup", L"SporeCoop Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_opt.dir.empty())
        SetWindowTextW(g_dir, g_opt.dir.c_str());
    ShowWindow(w, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g_job && inst::g_fix) // BUG-0139: never end the process under a running job
        WaitForSingleObject(g_job, INFINITE);
    return 0;
}
