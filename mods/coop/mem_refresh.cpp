#include "mem_refresh.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <vector>
#include "coop_ui.h"
#include "diag_coop.h"
#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "world.h"

namespace mem_refresh {

static const SkHost* H;
static SkMod* M;
static Api A;

static bool g_fix = true;
COOP_FIX("core.memory_box", "BUG-9239", g_fix,
         "core: a box with a live memory bar and Continue (save, restart Spore, reconnect) when the address space runs low");
bool enabled() { return g_fix; }
static bool g_box;
bool box_up() { return g_fix && g_box; }

constexpr float kFirstLeft = 15.f, kSecondLeft = 10.f;   // % left: the box (4S, 4T A: 15 %, then 10 % after Ignore)
constexpr int kFirstBlock = 48, kSecondBlock = 24;       // MB: no free block this big counts the same
constexpr float kYellowLeft = 20.f, kRedLeft = 10.f;     // the bar's colours (owner 2026-11-16)
constexpr double kResumeMaxAge = 15 * 60;                // s: a restart file older than this is ignored
constexpr double kRejoinFor = 6 * 60, kRejoinEvery = 20; // s: a guest's rejoins after the host's restart

// ---- what the box shows ----
static float g_left = 100.f;      // % of the address space left (measured)
static int g_free_mb = -1, g_block_mb = -1, g_total_mb = -1;
static float g_fake_left = -1.f;  // tests (coop.memrefresh show <percent>)
static int g_level;               // 0 fine, 1 the first mark, 2 the second
static int g_ignored;             // the highest level ignored this run
                                  // (g_box above: the box is up)
static std::string g_error;       // the box's last line instead of the numbers (a save refused)
static double g_walk_at = -1e9;

// ---- the restart (this game) ----
enum class Out { None, Saving, Announcing, Leaving };
static Out g_out = Out::None;
static double g_out_at, g_out_since;
static int g_out_role;            // 1 host, 2 guest, 0 alone
static std::wstring g_out_slot;
static std::string g_out_planet;

// ---- the restart (the new game) ----
struct Resume {
    int role = 0; // 1 host, 2 guest, 0 alone
    std::string addr, name, password, planet;
    std::wstring slot;
    uint16_t port = 0;
    unsigned seats = 0;
    uint64_t token = 0;
};
enum class In { None, WaitMenu, Loading, Done };
static In g_in = In::None;
static Resume g_resume;
static double g_in_since = -1, g_in_at;

// ---- a guest's rejoins after its host's restart (MSG_HOST_RESTART) ----
static Resume g_rejoin;
static double g_rejoin_until = -1, g_rejoin_next;
static unsigned g_rejoin_tries;

static uint32_t g_restarts_begun, g_boxes_shown; // tests

static std::wstring restart_file()
{
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) return L""; // profile-aware (hooked)
    std::wstring dir = std::wstring(p) + L"\\SporeCoop";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\restart-" + std::to_wstring(H->instance_id()) + L".txt";
}

static std::string utf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring wide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string hex_of(const std::string& s) // (names and passwords may hold any byte: one line each, hex)
{
    static const char* d = "0123456789abcdef";
    std::string o;
    for (unsigned char c : s) { o += d[c >> 4]; o += d[c & 15]; }
    return o;
}
static std::string unhex(const std::string& h)
{
    std::string o;
    for (size_t i = 0; i + 1 < h.size(); i += 2) o += (char)strtoul(h.substr(i, 2).c_str(), nullptr, 16);
    return o;
}

static bool write_resume(const Resume& r)
{
    char b[96];
    std::string t = "time=" + std::to_string((long long)time(nullptr)) + "\npid=" + std::to_string(GetCurrentProcessId()) +
                    "\nrole=" + std::to_string(r.role) + "\naddr=" + hex_of(r.addr) + "\nname=" + hex_of(r.name) +
                    "\npassword=" + hex_of(r.password) + "\nslot=" + hex_of(utf8(r.slot)) + "\nplanet=" + hex_of(r.planet) +
                    "\nport=" + std::to_string(r.port) + "\nseats=" + std::to_string(r.seats);
    snprintf(b, sizeof b, "\ntoken=%016llx\n", (unsigned long long)r.token);
    t += b;
    const std::wstring f = restart_file();
    HANDLE h = f.empty() ? INVALID_HANDLE_VALUE
                         : CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    const bool ok = WriteFile(h, t.data(), (DWORD)t.size(), &n, nullptr) && n == t.size();
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

static bool read_resume(Resume& r)
{
    const std::wstring f = restart_file();
    if (f.empty()) return false;
    HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[8192];
    DWORD n = 0;
    ReadFile(h, buf, sizeof buf - 1, &n, nullptr);
    CloseHandle(h);
    DeleteFileW(f.c_str()); // read once: a restart never repeats itself
    std::map<std::string, std::string> kv;
    std::string s(buf, n), line;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == '\n') {
            size_t eq = line.find('=');
            if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
            line.clear();
        } else if (s[i] != '\r')
            line += s[i];
    }
    const long long at = atoll(kv["time"].c_str());
    if (!at || time(nullptr) - at > kResumeMaxAge || atoll(kv["pid"].c_str()) == (long long)GetCurrentProcessId()) {
        H->log(M, SK_WARN, "mem: a restart file was left (%lld s old): ignored", (long long)(time(nullptr) - at));
        return false;
    }
    r.role = atoi(kv["role"].c_str());
    r.addr = unhex(kv["addr"]);
    r.name = unhex(kv["name"]);
    r.password = unhex(kv["password"]);
    r.slot = wide(unhex(kv["slot"]));
    r.planet = unhex(kv["planet"]);
    r.port = (uint16_t)atoi(kv["port"].c_str());
    r.seats = (unsigned)atoi(kv["seats"].c_str());
    r.token = strtoull(kv["token"].c_str(), nullptr, 16);
    return true;
}

static std::string base64(const std::string& in)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
    }
    return o;
}
static std::wstring ps_quote(const std::wstring& s) // a PowerShell single-quoted string
{
    std::wstring o = L"'";
    for (wchar_t c : s) {
        if (c == L'\'') o += L"''";
        else o += c;
    }
    return o + L"'";
}

// a hidden PowerShell waits for this process to end, then starts the game again (same exe, arguments, folder and
// environment: Steam's SteamAppId included)
static bool launch_again(std::string& err)
{
    wchar_t exe[MAX_PATH] = {}, cwd[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring cl = GetCommandLineW(), args;
    size_t k = 0;
    if (!cl.empty() && cl[0] == L'"') {
        size_t q = cl.find(L'"', 1);
        k = q == std::wstring::npos ? cl.size() : q + 1;
    } else {
        k = cl.find(L' ');
        if (k == std::wstring::npos) k = cl.size();
    }
    args = cl.substr(k);
    while (!args.empty() && args[0] == L' ') args.erase(0, 1);
    const std::wstring script = L"$ErrorActionPreference='SilentlyContinue'\nWait-Process -Id " +
                                std::to_wstring(GetCurrentProcessId()) + L" -Timeout 120\nStart-Sleep -Milliseconds 1500\n"
                                L"$i = New-Object System.Diagnostics.ProcessStartInfo\n$i.FileName = " + ps_quote(exe) +
                                L"\n$i.Arguments = " + ps_quote(args) + L"\n$i.WorkingDirectory = " + ps_quote(cwd) +
                                L"\n$i.UseShellExecute = $false\n[void][System.Diagnostics.Process]::Start($i)\n";
    std::string raw((const char*)script.data(), script.size() * sizeof(wchar_t)); // UTF-16LE for -EncodedCommand
    wchar_t sysroot[MAX_PATH] = L"C:\\Windows";
    GetEnvironmentVariableW(L"SystemRoot", sysroot, MAX_PATH);
    const std::wstring ps = std::wstring(sysroot) + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring cmd = L"\"" + ps + L"\" -NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand " + wide(base64(raw));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(0);
    // out of the game's job when it may (a launcher's job could end it with the game), else inside it
    BOOL ok = CreateProcessW(ps.c_str(), line.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr, cwd, &si, &pi);
    if (!ok) ok = CreateProcessW(ps.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd, &si, &pi);
    if (!ok) {
        err = "could not start the restart helper (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    H->log(M, SK_INFO, "mem: the restart helper runs (pid %lu): the game starts again with '%s'", pi.dwProcessId,
           utf8(args).c_str());
    return true;
}

static void finish_restart()
{
    Resume r;
    r.role = g_out_role;
    r.name = A.name ? A.name() : "";
    if (g_out_role == 2) {
        r.addr = A.host_address ? A.host_address() : "";
        r.password = A.password ? A.password() : "";
        r.token = A.token ? A.token() : 0;
    } else {
        r.slot = g_out_slot;
        r.planet = g_out_planet;
        if (g_out_role == 1) {
            r.port = A.port ? A.port() : 0;
            r.seats = A.seats ? A.seats() : 0;
            r.password = A.password ? A.password() : "";
        }
    }
    std::string err;
    if (!write_resume(r)) {
        H->log(M, SK_WARN, "mem: the restart file could not be written: the game restarts without reconnecting");
    }
    if (A.leave && g_out_role) A.leave();
    if (!launch_again(err)) {
        H->log(M, SK_WARN, "mem: %s", err.c_str());
        g_error = "Cannot restart Spore by itself: " + err + ". Please restart it.";
        g_out = Out::None;
        DeleteFileW(restart_file().c_str());
        return;
    }
    H->log(M, SK_INFO, "mem: the game ends for its restart (%s, %d MB free)",
           g_out_role == 2 ? "guest" : g_out_role == 1 ? "host" : "alone", g_free_mb);
    Sleep(300); // (the leave's packets and the log line out)
    TerminateProcess(GetCurrentProcess(), 0);
}

static void begin_restart()
{
    if (g_out != Out::None) return;
    g_restarts_begun++;
    g_error.clear();
    const int role = A.role ? A.role() : 0;
    g_out_role = role == 2 && A.connected && A.connected() ? 2 : role == 1 ? 1 : 0;
    g_out_since = H->time_seconds();
    H->log(M, SK_INFO, "mem: Continue: restart as %s (%d MB free of %d, largest block %d MB)",
           g_out_role == 2 ? "guest" : g_out_role == 1 ? "host" : "a game alone", g_free_mb, g_total_mb, g_block_mb);
    if (g_out_role == 2) { // a guest: nothing of its own to save here; the host keeps its place
        finish_restart();
        return;
    }
    g_out_slot.clear();
    g_out_planet.clear();
    if (game::mode_id() == modes::kMenu || !world::restart_point(g_out_slot, g_out_planet)) {
        g_out_planet.clear(); // a menu: nothing to save or load again
        g_out = Out::Announcing;
        g_out_at = H->time_seconds();
        if (g_out_role == 1 && A.announce) A.announce(150);
        return;
    }
    std::string err;
    if (!world::save_game(err)) {
        g_error = "Cannot save now: " + err;
        H->log(M, SK_WARN, "mem: the save before the restart was refused: %s", err.c_str());
        return;
    }
    g_out = Out::Saving;
    g_out_at = H->time_seconds();
}

static void out_tick(double now)
{
    switch (g_out) {
    case Out::None: return;
    case Out::Saving: // the game's own save: written and its box confirmed
        if (now - g_out_at < 1.5 || world::save_busy() || world::saved_box_up()) {
            if (now - g_out_at > 90) {
                g_error = "The save did not finish: please save and restart Spore yourself.";
                H->log(M, SK_WARN, "mem: the save before the restart did not finish in 90 s");
                g_out = Out::None;
            }
            return;
        }
        world::restart_point(g_out_slot, g_out_planet); // (the planet may have got its name with this save)
        H->log(M, SK_INFO, "mem: saved %s for the restart", g_out_planet.c_str());
        if (g_out_role == 1 && A.announce) A.announce(150);
        g_out = Out::Announcing;
        g_out_at = now;
        return;
    case Out::Announcing: // the guests get MSG_HOST_RESTART before the session ends
        if (now - g_out_at < 1.0) return;
        g_out = Out::Leaving;
        finish_restart();
        return;
    case Out::Leaving: return;
    }
}

static void on_click(int code)
{
    if (code == 1) { // (saves and loads run outside the frame: CLAUDE.md 2)
        H->run_outside_frame([](void*) { begin_restart(); }, nullptr);
        return;
    }
    g_ignored = std::max(g_ignored, std::max(g_level, 1));
    g_box = false;
    g_error.clear();
    H->log(M, SK_INFO, "mem: Ignore at %.0f %% left (level %d): %s", g_left, g_level,
           g_ignored >= 2 ? "no more boxes this run" : "the box comes back at 10 % left");
}

static void measure(double now)
{
    const bool close = g_box || g_left < 30.f;
    if (now - g_walk_at >= (close ? 2.0 : 10.0)) {
        g_walk_at = now;
        support::mem_walk_soon();
    }
    int fr, bl, tot, kept;
    if (!support::mem_now(fr, bl, tot, kept)) return;
    g_free_mb = fr + kept;
    g_block_mb = std::max(bl, kept);
    g_total_mb = tot;
    g_left = 100.f * g_free_mb / (float)tot;
    if (g_fake_left >= 0) g_left = g_fake_left;
    const bool fake = g_fake_left >= 0;
    g_level = (g_left <= kSecondLeft || (!fake && g_block_mb < kSecondBlock)) ? 2
            : (g_left <= kFirstLeft || (!fake && g_block_mb < kFirstBlock))  ? 1
                                                                             : 0;
}

static void in_tick(double now)
{
    if (g_in == In::None || g_in == In::Done) return;
    const uint32_t mode = game::mode_id();
    if (g_in == In::WaitMenu) {
        if (mode != modes::kMenu || world::menu_intro_playing()) {
            g_in_since = -1;
            return;
        }
        if (g_in_since < 0) g_in_since = now;
        if (now - g_in_since < 8) return;
        std::string err;
        if (g_resume.role == 2) {
            g_rejoin = g_resume; // the same path as after a host's restart: tries until connected
            g_rejoin_until = now + kRejoinFor;
            g_rejoin_next = now;
            g_rejoin_tries = 0;
            g_in = In::Done;
            return;
        }
        if (g_resume.planet.empty()) { // a menu before: host again from the menu
            if (g_resume.role == 1 && A.host && !A.host(g_resume.port, g_resume.seats, g_resume.name, g_resume.password, err))
                H->log(M, SK_WARN, "mem: hosting again failed: %s", err.c_str());
            g_in = In::Done;
            return;
        }
        if (!world::load_slot(g_resume.slot, g_resume.planet, err)) {
            H->log(M, SK_WARN, "mem: loading %s again failed: %s", g_resume.planet.c_str(), err.c_str());
            if (A.feed) A.feed("Could not load your game again after the restart: " + err);
            g_in = In::Done;
            return;
        }
        H->log(M, SK_INFO, "mem: loading %s again after the restart", g_resume.planet.c_str());
        g_in = In::Loading;
        g_in_since = -1;
        g_in_at = now;
        return;
    }
    if (g_in == In::Loading) { // in the game for 15 s: host again
        if (mode == modes::kMenu || mode == 0 || mode == modes::kEditor) {
            g_in_since = -1;
            if (now - g_in_at > 300) {
                H->log(M, SK_WARN, "mem: the game did not come back in 5 minutes: not hosting again");
                g_in = In::Done;
            }
            return;
        }
        if (g_in_since < 0) g_in_since = now;
        if (now - g_in_since < 15) return;
        g_in = In::Done;
        if (g_resume.role != 1) {
            if (A.feed) A.feed("Your game is back after the memory restart.");
            return;
        }
        std::string err;
        if (A.host && A.host(g_resume.port, g_resume.seats, g_resume.name, g_resume.password, err)) {
            H->log(M, SK_INFO, "mem: hosting again on port %u after the restart", g_resume.port);
            if (A.feed) A.feed("Back after the memory restart: hosting again. Your partners reconnect by themselves.");
        } else {
            H->log(M, SK_WARN, "mem: hosting again failed: %s", err.c_str());
            if (A.feed) A.feed("Could not host again after the restart: " + err);
        }
    }
}

static void rejoin_tick(double now)
{
    if (g_rejoin_until < 0) return;
    const int role = A.role ? A.role() : 0;
    if (role == 2 && A.connected && A.connected() && g_rejoin_tries > 0) {
        H->log(M, SK_INFO, "mem: rejoined the host after %u tries", g_rejoin_tries);
        g_rejoin_until = -1;
        return;
    }
    if (now > g_rejoin_until) {
        H->log(M, SK_WARN, "mem: the host did not come back in %.0f minutes: no more rejoins", kRejoinFor / 60);
        if (A.feed) A.feed("The host did not come back: join it again from the Co-op window.");
        g_rejoin_until = -1;
        return;
    }
    if (role != 0 || now < g_rejoin_next || g_rejoin.addr.empty()) return; // (a join in flight, or still connected)
    g_rejoin_next = now + kRejoinEvery;
    g_rejoin_tries++;
    std::string err;
    if (A.join && !A.join(g_rejoin.addr, g_rejoin.name, g_rejoin.password, g_rejoin.token, err))
        H->log(M, SK_WARN, "mem: rejoin try %u failed: %s", g_rejoin_tries, err.c_str());
    else
        H->log(M, SK_INFO, "mem: rejoin try %u", g_rejoin_tries);
}

void host_restarting(uint16_t seconds)
{
    if (!g_fix) return;
    g_rejoin = Resume{};
    g_rejoin.role = 2;
    g_rejoin.addr = A.host_address ? A.host_address() : "";
    g_rejoin.name = A.name ? A.name() : "";
    g_rejoin.password = A.password ? A.password() : "";
    g_rejoin.token = A.token ? A.token() : 0;
    const double now = H->time_seconds();
    g_rejoin_until = now + std::max<double>(kRejoinFor, seconds * 2.0);
    g_rejoin_next = now + 25; // (the host saves, restarts and loads first: about a minute)
    g_rejoin_tries = 0;
    H->log(M, SK_INFO, "mem: the host restarts for its memory (about %u s): rejoining by itself", seconds);
    if (A.feed) A.feed("The host restarts Spore to clear its memory: you reconnect by yourself in a minute or two.");
}

static bool g_step_queued;
void tick(double now)
{
    // the restart's steps (saves, loads, hosting, joins) outside the frame, once per frame at most
    if (!g_step_queued && (g_out != Out::None || (g_in != In::None && g_in != In::Done) || g_rejoin_until >= 0)) {
        g_step_queued = true;
        H->run_outside_frame([](void*) {
            g_step_queued = false;
            const double t = H->time_seconds();
            in_tick(t);
            rejoin_tick(t);
            out_tick(t);
        }, nullptr);
    }
    if (!g_fix) {
        if (g_box) coop_ui::set_memory_popup({}, nullptr);
        g_box = false;
        return;
    }
    measure(now);
    if (!g_box && g_level > g_ignored && g_out == Out::None) {
        g_box = true;
        g_boxes_shown++;
        H->log(M, SK_INFO, "mem: the memory box is up: %.0f %% left (%d MB free of %d, largest block %d MB), level %d",
               g_left, g_free_mb, g_total_mb, g_block_mb, g_level);
    }
    if (!g_box) {
        coop_ui::set_memory_popup({}, nullptr);
        return;
    }
    coop_ui::MemoryPopup p;
    p.show = true;
    p.buttons = g_out == Out::None;
    p.used = 1.f - g_left / 100.f;
    p.yellow_at = 1.f - kYellowLeft / 100.f; // the zones: yellow from 20 % left, red from 10 %
    p.red_at = 1.f - kRedLeft / 100.f;
    p.title = g_left <= kSecondLeft ? "Memory nearly full" : "Memory almost full";
    p.msg[0] = "Spore is natively unable to clear memory usage.";
    const int role = A.role ? A.role() : 0;
    p.msg[1] = role ? "Press continue below to restart Spore and reconnect."
                    : "Press continue below to restart Spore and load your game.";
    char b[160];
    if (g_out != Out::None)
        p.line = g_out == Out::Saving ? "Saving your game..." : "Restarting Spore...";
    else if (!g_error.empty())
        p.line = g_error;
    else {
        snprintf(b, sizeof b, "Memory used: %.0f%%   (%d MB left)", 100.f - g_left,
                 g_fake_left >= 0 ? (int)(g_total_mb * g_left / 100.f) : g_free_mb);
        p.line = b;
    }
    coop_ui::set_memory_popup(p, on_click);
}

#define SAY(...) H->out_printf(out, __VA_ARGS__)
static int c_memrefresh(void*, int argc, const char* const* argv, SkOut* out)
{
    const std::string sub = argc > 1 ? argv[1] : "status";
    if (sub == "show" && argc > 2) { // tests: the box as at <percent> left (the real measure again: off)
        g_fake_left = std::max(0.f, std::min(100.f, (float)atof(argv[2])));
        g_ignored = 0;
    } else if (sub == "off") {
        g_fake_left = -1;
        g_box = false;
    } else if (sub == "continue") {
        H->run_outside_frame([](void*) { begin_restart(); }, nullptr);
    } else if (sub == "ignore") {
        on_click(0);
    }
    SAY("memory box %s: %.1f %% left%s (%d MB free of %d, largest block %d MB), level %d, ignored %d, box %s, shown %u, "
        "restarts %u, out %d, resume %d (role %d), rejoin %s (tries %u)%s%s\n",
        g_fix ? "on" : "off", g_left, g_fake_left >= 0 ? " (test value)" : "", g_free_mb, g_total_mb, g_block_mb, g_level,
        g_ignored, g_box ? "up" : "down", g_boxes_shown, g_restarts_begun, (int)g_out, (int)g_in, g_resume.role,
        g_rejoin_until >= 0 ? "armed" : "none", g_rejoin_tries, g_error.empty() ? "" : ", error: ", g_error.c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, const Api& api)
{
    H = host;
    M = mod;
    A = api;
    H->command_add(M, "coop.memrefresh",
                   "coop.memrefresh [status | show <percent left> | off | continue | ignore] - the memory box and the "
                   "restart (decision 4S, BUG-9239; show: tests, as at that much left)",
                   c_memrefresh, nullptr, SK_CMD_MAIN_THREAD);
    if (read_resume(g_resume)) {
        g_in = In::WaitMenu;
        H->log(M, SK_INFO, "mem: started again after a memory restart (%s%s%s): %s at the galaxy menu",
               g_resume.role == 2 ? "guest" : g_resume.role == 1 ? "host" : "alone",
               g_resume.planet.empty() ? "" : ", planet ", g_resume.planet.c_str(),
               g_resume.role == 2 ? "rejoining" : g_resume.planet.empty() ? "nothing to load" : "loading it again");
    }
}

} // namespace mem_refresh
