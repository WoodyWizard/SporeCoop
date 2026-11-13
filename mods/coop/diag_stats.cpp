// Support diagnostics: FPS and hitches, the address space, network quality, session settings and
// the session summary (diag_coop.h, docs/REPORTING.md#reporting-support-answers).
#include "diag_coop.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "fixes.h"
#include "modes.h"

namespace support {

void sync_set_interval(double s); // diag_sync.cpp

#define SLOG(...) g_H->log(g_M, SK_INFO, __VA_ARGS__)
#define SWARN(...) g_H->log(g_M, SK_WARN, __VA_ARGS__)

// ---------------- the cost of the diagnostics themselves ----------------
static double g_cost_sum = 0, g_cost_max = 0; // seconds spent in support::tick / on_message
static uint64_t g_cost_frames = 0;
static double g_cost_frame = 0; // this frame so far

struct Timer {
    double t0;
    Timer() : t0(g_H->time_seconds()) {}
    ~Timer() { g_cost_frame += g_H->time_seconds() - t0; }
};

// ---------------- FPS and hitches ----------------
static constexpr double kHitch = 0.2; // s without a frame
static double g_last_frame = -1;
static double g_sec_start = -1, g_min_start = -1;
static int g_sec_frames = 0, g_min_frames = 0;
static double g_min_fps_min = 1e9, g_frame_max = 0;
static int g_hitches_min = 0, g_hitch_lines_min = 0;

static void perf_frame(double now)
{
    if (g_last_frame >= 0) {
        double dt = now - g_last_frame;
        g_frame_max = std::max(g_frame_max, dt);
        if (dt > kHitch) {
            g_hitches_min++;
            if (g_detail >= 1 && g_hitch_lines_min++ < 8) // the rest only counted in the minute's line
                SLOG("perf: hitch %d ms (%s)", (int)(dt * 1000), g_P.activity ? g_P.activity().c_str() : "?");
        }
    }
    g_last_frame = now;
    if (g_sec_start < 0)
        g_sec_start = g_min_start = now;
    g_sec_frames++;
    g_min_frames++;
    if (now - g_sec_start >= 1.0) {
        double fps = g_sec_frames / (now - g_sec_start);
        g_min_fps_min = std::min(g_min_fps_min, fps);
        g_sec_frames = 0;
        g_sec_start = now;
    }
}

// ---------------- the address space (a worker thread walks it) ----------------
static std::atomic<int> g_mem_free_mb{-1}, g_mem_block_mb{-1}, g_mem_total_mb{-1}, g_mem_regions{0};
static std::atomic<int> g_mem_walk_us{0};
static HANDLE g_mem_thread;
static double g_mem_low_told = -1e9;

static DWORD WINAPI mem_walk(void*)
{
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress, end = (uintptr_t)si.lpMaximumApplicationAddress;
    uint64_t free = 0, largest = 0;
    int n = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery((void*)p, &mbi, sizeof mbi) == sizeof mbi && n < 200000) {
        n++;
        if (mbi.State == MEM_FREE) {
            free += mbi.RegionSize;
            largest = std::max<uint64_t>(largest, mbi.RegionSize);
        }
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= p)
            break;
        p = next;
    }
    QueryPerformanceCounter(&b);
    g_mem_free_mb = (int)(free >> 20);
    g_mem_block_mb = (int)(largest >> 20);
    g_mem_total_mb = (int)((end - (uintptr_t)si.lpMinimumApplicationAddress) >> 20);
    g_mem_regions = n;
    g_mem_walk_us = (int)((b.QuadPart - a.QuadPart) * 1000000 / f.QuadPart);
    return 0;
}

// BUG-0631: a 32-bit game on the 2 GB space (the GOG 2024 exe has the large-address flag off) fills it with the game's
// own caches over a long session (creature skins of 5.3 MB each, models, materials, effects: docs/re/graphics.md#graphics-
// address-space; in co-op the host also holds what lives around its partners). Near the end the space is fragmented: the
// host of the report crashed in Civ with 133 MB free but no free block over 12 MB, where the game's allocator grows in
// 16 MB segments and a model's vertex buffer came back null. With core.mem_reserve a contiguous block is reserved at the
// start (address space only, nothing committed) and given back when the space runs low, with a notice to the player to
// save and restart the game soon: the game gets one last big block instead of crashing on the first allocation that
// needs one. Off: no reserve, the low state is only written in the log (0.7.9).
static bool g_fix_mem_reserve = true;
COOP_FIX("core.mem_reserve", "BUG-0631", g_fix_mem_reserve, "core: a block of address space is kept from the start and given back, with a notice to save and restart, when the game's address space runs low");
constexpr SIZE_T kReserveMB = 128;
static void* g_reserve;            // the kept block (MEM_RESERVE, never committed)
static bool g_reserve_given = false; // given back this run (once: it is not taken again)
static double g_reserve_told = -1e9;
static uint32_t g_reserve_released = 0; // tests

static void reserve_take()
{
    if (g_reserve || g_reserve_given || !g_fix_mem_reserve)
        return;
    // top-down: out of the way of the game's own allocations, which mostly grow bottom-up
    g_reserve = VirtualAlloc(nullptr, kReserveMB << 20, MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
    if (g_reserve)
        SLOG("mem: %u MB of address space kept for the end of a long session (core.mem_reserve, BUG-0631)",
             (unsigned)kReserveMB);
}

static void reserve_give(const char* why)
{
    if (!g_reserve)
        return;
    VirtualFree(g_reserve, 0, MEM_RELEASE);
    g_reserve = nullptr;
    g_reserve_given = true;
    g_reserve_released++;
    SLOG("mem: the %u MB kept for the end were given back to the game (%s)", (unsigned)kReserveMB, why);
}

static void mem_start()
{
    if (g_mem_thread) {
        if (WaitForSingleObject(g_mem_thread, 0) != WAIT_OBJECT_0)
            return; // still walking
        CloseHandle(g_mem_thread);
    }
    g_mem_thread = CreateThread(nullptr, 0, mem_walk, nullptr, 0, nullptr);
}

// ---------------- network quality per peer ----------------
struct PeerNet {
    net::ConnStats last;     // at the last minute line
    net::ConnStats last5;    // at the last 5 s loss check
    double usual_rtt = 0;    // the last minute's average round trip
    uint32_t backlog_max = 0;
    double spike_told = -1e9;
    int last_rtt = -1;
    uint32_t last_silent = 0;
    double max_loss = 0;
};
static std::map<uint8_t, PeerNet> g_net;
static double g_net_sec = 0;
static unsigned g_net_ticks = 0;

static double loss_pct(uint32_t lost, uint32_t sent) { return sent ? 100.0 * lost / sent : 0; }

// BUG-1010: "last heard N ms" subtracted the connection's last receive time (net::now_ms, steady_clock) from
// GetTickCount: two clocks with their own epochs (on Windows QPC and the tick count drift apart; under Wine both
// count from the same boot clock, so the lab never saw a wrong value). The session's own clock now. Off: GetTickCount.
static bool g_silent_clock_fix = true;
COOP_FIX("diag.silent_clock", "BUG-1010", g_silent_clock_fix, "diag: the leave lines' last heard value uses the connection's own clock");

static void net_second(double now, bool minute)
{
    if (!in_session())
        return;
    uint32_t nowms = g_silent_clock_fix ? net::now_ms() : GetTickCount(); // (the Session clock without its test seam)
    bool five = ++g_net_ticks % 5 == 0;
    for (auto& p : g_P.sess->players()) {
        if (p.id == g_P.sess->my_id() || (g_P.sess->role() == net::Role::Client && p.id != 0))
            continue; // a guest talks to the host only
        net::Connection* c = g_P.sess->conn_to(p.id);
        if (!c)
            continue;
        PeerNet& n = g_net[p.id];
        net::ConnStats& s = c->stats;
        n.backlog_max = std::max<uint32_t>(n.backlog_max, (uint32_t)c->pending_reliable());
        n.last_rtt = (int)c->rtt_ms;
        n.last_silent = nowms - c->last_recv_ms;
        g_c.max_rtt = std::max(g_c.max_rtt, (int)c->rtt_ms);
        // spikes: at once, at most one line per peer and 30 s
        uint32_t sent5 = s.packets_sent - n.last5.packets_sent, lost5 = s.lost - n.last5.lost;
        if (five) {
            double l5 = loss_pct(lost5, sent5);
            bool rtt_spike = n.usual_rtt > 0 && c->rtt_ms > 2 * std::max(n.usual_rtt, 20.0) && c->rtt_ms > 100;
            bool loss_spike = sent5 >= 20 && l5 > 10;
            if ((rtt_spike || loss_spike) && now - n.spike_told > 30 && g_detail >= 1) {
                n.spike_told = now;
                SWARN("net: spike with player %u '%s': rtt %.0f ms (usual %.0f), loss %.1f%% of %u packets in 5 s, backlog %u",
                      p.id, p.name.c_str(), c->rtt_ms, n.usual_rtt, l5, sent5, (unsigned)c->pending_reliable());
            }
            n.last5 = s;
        }
        if (!minute)
            continue;
        uint32_t sent = s.packets_sent - n.last.packets_sent, lost = s.lost - n.last.lost;
        double loss = loss_pct(lost, sent);
        n.max_loss = std::max(n.max_loss, loss);
        g_c.max_loss = std::max(g_c.max_loss, loss);
        double avg = s.rtt_n ? s.rtt_sum / s.rtt_n : c->rtt_ms;
        if (g_detail >= 1)
            SLOG("net: player %u '%s' rtt %.0f/%.0f/%.0f ms (min/avg/max), loss %.1f%% (%u of %u packets), resends %u, "
                 "backlog %u (max %u), in %llu KB out %llu KB, direct UDP to %s",
                 p.id, p.name.c_str(), s.rtt_n ? s.rtt_min : c->rtt_ms, avg, s.rtt_n ? s.rtt_max : c->rtt_ms, loss, lost, sent,
                 s.resends - n.last.resends, (unsigned)c->pending_reliable(), n.backlog_max,
                 (unsigned long long)((s.bytes_recv - n.last.bytes_recv) >> 10),
                 (unsigned long long)((s.bytes_sent - n.last.bytes_sent) >> 10), c->remote.masked().c_str());
        n.usual_rtt = avg;
        n.backlog_max = 0;
        s.take_window();
        n.last = s;
    }
}

// ---------------- settings ----------------
static std::string g_settings_told;
static double g_settings_at = 0;

static std::string settings_text()
{
    std::string s = g_P.settings ? g_P.settings() : "";
    if (g_detail != 1)
        s += (s.empty() ? "" : " ") + std::string("log_detail=") + std::to_string(g_detail);
    std::string off;
    for (auto& f : fix::all())
        if (!*f.on)
            off += (off.empty() ? "" : " ") + std::string(f.name);
    return (s.empty() ? "all default" : s) + "; coop.fix off: " + (off.empty() ? "none" : off);
}

static void settings_check(double now, bool force)
{
    if (!force && now - g_settings_at < 10)
        return;
    g_settings_at = now;
    std::string s = settings_text();
    if (s != g_settings_told) {
        SLOG("settings: %s%s", s.c_str(), force ? "" : " (changed)");
        g_settings_told = s;
    }
}

// ---------------- stages ----------------
static uint32_t g_stage_mode = 0;

static bool is_stage(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv || m == modes::kSpace;
}

static void stage_track(uint32_t mode)
{
    if (mode == g_stage_mode || !is_stage(mode))
        return;
    g_stage_mode = mode;
    std::string n = modes::mode_name(mode);
    if (g_c.stages.empty() || g_c.stages.back() != n)
        g_c.stages.push_back(n);
}

// ---------------- the summary ----------------
static double g_session_t0 = -1;
static net::Role g_session_role = net::Role::None;
static double g_last_end = -1e9;
static std::string g_last_end_why;
static bool g_had_session;

std::string summary_line(const char* why)
{
    char b[900];
    double now = g_H->time_seconds();
    std::string stages, peers;
    for (auto& s : g_c.stages)
        stages += (stages.empty() ? "" : " > ") + s;
    for (auto& p : g_c.peers)
        peers += (peers.empty() ? "" : ", ") + p;
    double cost = g_cost_frames ? g_cost_sum / g_cost_frames * 1000 : 0;
    snprintf(b, sizeof b,
             "%s %s, %.0f min (%s): stages %s; players %s; faults %d, rejoins %d, marks %d; sync-check %d ok, %d differed, "
             "%d mismatch; max rtt %d ms, max loss %.1f%%; min fps %d; lowest free address space %d MB (largest block %d MB); "
             "diagnostics %.4f ms/frame (max %.2f ms)",
             g_session_t0 >= 0 ? "session as" : "game run", g_session_t0 >= 0 ? (g_session_role == net::Role::Host ? "host" : "guest") : "without a session",
             (now - (g_session_t0 >= 0 ? g_session_t0 : 0)) / 60, why, stages.empty() ? "none" : stages.c_str(),
             peers.empty() ? "none" : peers.c_str(), g_c.faults, g_c.rejoins, g_c.marks, g_c.sync_ok, g_c.sync_differs,
             g_c.sync_mismatch, g_c.max_rtt, g_c.max_loss, g_c.min_fps, g_c.min_free_mb, g_c.min_block_mb, cost, g_cost_max * 1000);
    return b;
}

static bool kit_has_headline()
{
    return g_H->size >= offsetof(SkHost, diag_headline) + sizeof(void*) && g_H->diag_headline;
}

static std::string g_summary_sub; // tests (coop.diag summarysub <folder>): a sub folder of mods\logs, UTF-8 (BUG-1011)

static std::string summary_file()
{
    const char* d = g_H->size >= offsetof(SkHost, logs_dir) + sizeof(void*) && g_H->logs_dir ? g_H->logs_dir() : nullptr;
    char inst[16];
    snprintf(inst, sizeof inst, "%d", g_H->instance_id());
    return d ? std::string(d) + (g_summary_sub.empty() ? "" : "\\" + g_summary_sub) + "\\coop-last-session" +
                   (g_H->instance_id() > 1 ? std::string(".") + inst : "") + ".txt"
             : "";
}

// BUG-1011: logs_dir() is UTF-8 and fopen takes a path in the ANSI code page: with letters outside it in the game
// folder (a Steam library in a Cyrillic or CJK folder) coop-last-session.txt was never written nor read. Wide
// paths now, as sreport.cpp. Off: fopen with the UTF-8 path (0.7.13).
static bool g_summary_wide_fix = true;
COOP_FIX("diag.summary_wide", "BUG-1011", g_summary_wide_fix, "diag: the session summary file is opened by its wide path (non-ASCII game folders)");

static FILE* summary_open(const std::string& f, const char* how)
{
    if (!g_summary_wide_fix)
        return fopen(f.c_str(), how);
    int n = MultiByteToWideChar(CP_UTF8, 0, f.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return nullptr;
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, f.c_str(), -1, w.data(), n);
    std::wstring m(how, how + strlen(how));
    return _wfopen(w.c_str(), m.c_str());
}

void write_summary(const char* why)
{
    std::string l = summary_line(why);
    SLOG("summary: %s", l.c_str());
    SYSTEMTIME st;
    GetLocalTime(&st);
    char when[40];
    snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    std::string text = std::string("latest co-op summary (") + when + "): " + l;
    if (kit_has_headline())
        g_H->diag_headline(g_M, text.c_str());
    std::string f = summary_file();
    if (!f.empty())
        if (FILE* fp = summary_open(f, "w")) {
            fputs(text.c_str(), fp);
            fputc('\n', fp);
            fclose(fp);
        }
}

static void reset_counters()
{
    g_c = Counters{};
    g_cost_sum = g_cost_max = 0;
    g_cost_frames = 0;
    g_stage_mode = 0;
    if (g_P.mode)
        stage_track(g_P.mode());
}

// ---------------- the frame ----------------
// ---------------- programs on the player's PC that change the game (notes for the player) ----------------
// BUG-0782: RivaTuner Statistics Server (MSI Afterburner) injects RTSSHooks.dll and hooks the same d3d9 functions as
// SporeKit; players reported crashes at start with it running. Its DLL is named in the log and the reports; with
// core.rtss_note the player is also told, once per start, how to keep it out of Spore. Off: the log line only (0.7.12).
static bool g_fix_rtss_note = true;
COOP_FIX("core.rtss_note", "BUG-0782", g_fix_rtss_note, "core: when RivaTuner / MSI Afterburner hooks the game, the player is told (once per start) how to keep it out of Spore");
// BUG-0779: the game runs without its frame limit (kFrameLimitMS 0, or under 10 ms: its own is 16) at a high
// frame rate: that is what a player saw tear (82-138 fps, a d3d9 wrapper and Spore ModAPI present). With
// core.uncapped_note the player is told once per start what changed it and how to get a steady picture back.
// Off: the log line only (0.7.12).
static bool g_fix_uncapped_note = true;
COOP_FIX("core.uncapped_note", "BUG-0779", g_fix_uncapped_note, "core: when the game runs without Spore's frame cap for a minute, the player is told (once per start) that another program lifted it and how to stop the tearing");
static bool g_rtss_told = false, g_uncapped_told = false;
static uint32_t g_env_notes = 0; // notes shown (tests)

static const wchar_t* rtss_dll()
{
    for (const wchar_t* n : {L"RTSSHooks.dll", L"RTSSHooks64.dll"})
        if (GetModuleHandleW(n))
            return n;
    return nullptr;
}

// kFrameLimitMS now (AppProperties id 44; docs/02-modkit.md#modkit-frame-limit), -1 when not readable
static int frame_limit_ms()
{
    uintptr_t p = g_H->sym("App::FrameLimit_ptr");
    uint8_t* app = p ? *(uint8_t**)p : nullptr;
    uint8_t* cfg = app ? *(uint8_t**)(app + 0x3C) : nullptr;
    return cfg ? *(int*)(cfg + 0xB0) : -1;
}

static void env_second(double now)
{
    static double next_rtss = 10; // RTSS may inject after the start: looked for every 10 s
    if (!g_rtss_told && now >= next_rtss) {
        next_rtss = now + 10;
        if (const wchar_t* d = rtss_dll()) {
            g_rtss_told = true;
            SWARN("env: %ls is loaded (RivaTuner Statistics Server / MSI Afterburner): it hooks the game's Direct3D "
                  "like SporeKit; if Spore crashes at start, give SporeApp.exe an RTSS profile with Application "
                  "detection level None, or close RTSS (docs/INSTALL.md#install-rtss) (BUG-0782)", d);
            if (g_fix_rtss_note && g_P.feed) {
                g_env_notes++;
                g_P.feed("RivaTuner / MSI Afterburner is hooked into Spore. If Spore crashes at start, set "
                         "\"Application detection level\" to None for SporeApp.exe in RivaTuner, or close it");
            }
        }
    }
}

static void env_minute(int avg_fps)
{
    if (g_uncapped_told)
        return;
    int ms = frame_limit_ms();
    if (ms < 0 || ms >= 10 || avg_fps < 50)
        return; // capped (the game's own 16 ms, a limit another program set) or not fast anyway
    g_uncapped_told = true;
    SWARN("env: the game runs without Spore's frame limit: %d fps, kFrameLimitMS %d ms (Spore's own 16); a mod or "
          "program lifted it; with vsync off the picture tears (docs/INSTALL.md#install-tearing) (BUG-0779)",
          avg_fps, ms);
    if (g_fix_uncapped_note && g_P.feed) {
        g_env_notes++;
        char b[300];
        snprintf(b, sizeof b,
                 "Spore runs at %d fps without its frame cap (another mod or program lifted it). If the picture "
                 "tears, turn on vsync in mods\\sporekit.ini (vsync=1) or a frame cap in that program",
                 avg_fps);
        g_P.feed(b);
    }
}

std::string env_status()
{
    char b[200];
    snprintf(b, sizeof b, "env: notes %u, rtss %s, frame limit %d ms\n", g_env_notes, rtss_dll() ? "loaded" : "no",
             frame_limit_ms());
    return b;
}

static double g_minute_at = -1, g_mem_at = -1e9;

void stats_tick(double now)
{
    perf_frame(now);
    if (g_P.mode)
        stage_track(g_P.mode());
    if (now - g_net_sec >= 1.0) {
        bool minute = g_minute_at >= 0 && now - g_minute_at >= 60;
        net_second(now, minute);
        env_second(now);
        g_net_sec = now;
        if (g_minute_at < 0)
            g_minute_at = now;
        if (in_session())
            settings_check(now, false);
        if (!g_fix_mem_reserve && g_reserve) // the switch turned off at runtime (tests): no reserve, as before
            reserve_give("core.mem_reserve off");
        if (now - g_mem_at >= 60) { // the walk runs on a thread: its result is read next time
            g_mem_at = now;
            mem_start();
        }
        if (minute) {
            int fps = g_min_fps_min < 1e8 ? (int)g_min_fps_min : -1;
            int avg = (int)(g_min_frames / (now - g_min_start) + 0.5);
            if (fps >= 0 && (g_c.min_fps < 0 || fps < g_c.min_fps))
                g_c.min_fps = fps;
            env_minute(avg);
            int fr = g_mem_free_mb, bl = g_mem_block_mb;
            if (fr >= 0 && (g_c.min_free_mb < 0 || fr < g_c.min_free_mb))
                g_c.min_free_mb = fr;
            if (bl >= 0 && (g_c.min_block_mb < 0 || bl < g_c.min_block_mb))
                g_c.min_block_mb = bl;
            if (g_detail >= 1)
                SLOG("perf: fps avg %d min %d (1 s windows), longest frame %d ms, hitches %d | address space %d of %d MB "
                     "free, largest free block %d MB | %s",
                     avg, fps, (int)(g_frame_max * 1000), g_hitches_min, fr, (int)g_mem_total_mb, bl,
                     g_P.activity ? g_P.activity().c_str() : "?");
            // a 32-bit game: the largest block is what a big allocation (a planet, an editor) needs
            if (fr >= 0 && (fr < 300 || bl < 48) && g_fix_mem_reserve) {
                if (g_reserve)
                    reserve_give("the address space runs low");
                if (now - g_reserve_told > 600 && g_P.feed) { // every 10 minutes while it stays low
                    g_reserve_told = now;
                    g_P.feed("Your game is running out of memory: save and restart it soon (Spore is a 32-bit game "
                             "and keeps what it loads until it is closed)");
                }
            }
            if (fr >= 0 && (fr < 300 || bl < 48) && now - g_mem_low_told > 300) {
                g_mem_low_told = now;
                SWARN("mem: the game's address space runs low: %d MB free, largest free block %d MB (a crash with "
                      "\"out of memory\" may follow; the 4GB patch / large-address flag helps)", fr, bl);
            }
            g_min_frames = g_hitches_min = g_hitch_lines_min = 0;
            g_min_fps_min = 1e9;
            g_frame_max = 0;
            g_min_start = now;
            g_minute_at = now;
        }
    }
}

std::string stats_status()
{
    char b[500];
    snprintf(b, sizeof b,
             "perf: this minute %d frames, min fps %.0f, longest frame %.0f ms, hitches %d\n"
             "mem: %d of %d MB free, largest block %d MB (%d regions walked in %.2f ms on a worker thread)\n"
             "diagnostics cost: avg %.4f ms/frame over %llu frames, max %.3f ms\n",
             g_min_frames, g_min_fps_min < 1e8 ? g_min_fps_min : -1.0, g_frame_max * 1000, g_hitches_min, (int)g_mem_free_mb,
             (int)g_mem_total_mb, (int)g_mem_block_mb, (int)g_mem_regions, g_mem_walk_us / 1000.0,
             g_cost_frames ? g_cost_sum / g_cost_frames * 1000 : 0.0, (unsigned long long)g_cost_frames, g_cost_max * 1000);
    std::string s = b;
    s += env_status();
    for (auto& [id, n] : g_net) {
        snprintf(b, sizeof b, "net: player %u rtt %d ms, usual %.0f, max loss %.1f%%, silent %u ms\n", id, n.last_rtt,
                 n.usual_rtt, n.max_loss, n.last_silent);
        s += b;
    }
    return s + "settings: " + settings_text() + "\n";
}

// ---------------- the module ----------------
void init(const SkHost* host, SkMod* mod, const Probe& probe)
{
    g_H = host;
    g_M = mod;
    g_P = probe;
    sync_set_interval(std::max(0, g_H->config_int(g_M, "coop", "sync_check", 30)));
    if (!shot_setting()) // [report] screenshot=0: no screenshot of an earlier run goes into a report either
        report_shots_forget();
    // the summary of the last session of an earlier run stays at the top of reports until a new one
    std::string f = summary_file();
    if (!f.empty() && kit_has_headline())
        if (FILE* fp = summary_open(f, "r")) {
            char b[1200] = {0};
            size_t n = fread(b, 1, sizeof b - 1, fp);
            fclose(fp);
            b[n] = 0;
            if (n)
                g_H->diag_headline(g_M, (std::string("(an earlier run) ") + b).c_str());
        }
    reset_counters();
    reserve_take();
}

void tick(double now)
{
    double t0 = g_H->time_seconds();
    stats_tick(now);
    sync_tick(now);
    g_cost_frame += g_H->time_seconds() - t0;
    g_cost_sum += g_cost_frame;
    g_cost_max = std::max(g_cost_max, g_cost_frame);
    g_cost_frames++;
    g_cost_frame = 0; // messages of the next update() count into the next frame
}

void sync_on_message(uint8_t from, net::Reader& r); // diag_sync.cpp
void sync_session_start();

void on_message(uint8_t from, net::Reader& r)
{
    Timer t; // counted into the frame's cost of the diagnostics
    sync_on_message(from, r);
}

void on_session_start()
{
    g_had_session = true;
    reset_counters();
    g_session_t0 = g_H->time_seconds();
    g_session_role = g_P.sess ? g_P.sess->role() : net::Role::None;
    g_net.clear();
    // a guest that comes back within 2 minutes of losing the session: a rejoin
    if (g_session_role == net::Role::Client && g_session_t0 - g_last_end < 120 && g_last_end_why != "user")
        g_c.rejoins++;
    sync_session_start();
    settings_check(g_session_t0, true);
}

static std::vector<std::string> g_peer_names; // the names in g_c.peers (a name seen again: a rejoin)

static std::string peer_text(const net::PlayerInfo& p)
{
    return p.name + " (" + (p.version.empty() ? std::string("version unknown: before 0.7.4") : p.version) + ")";
}

void on_connected()
{
    g_c.peers.clear();
    g_peer_names.clear();
    if (g_P.sess)
        for (auto& p : g_P.sess->players())
            if (p.id != g_P.sess->my_id()) {
                g_c.peers.push_back(peer_text(p));
                g_peer_names.push_back(p.name);
            }
}

void on_player_join(const net::PlayerInfo& pi)
{
    const std::string& name = pi.name;
    g_c.joins++;
    for (auto& p : g_peer_names)
        if (p == name) {
            if (g_P.sess && g_P.sess->role() == net::Role::Host)
                g_c.rejoins++; // a guest of this name was here before (the guest counts its own rejoin)
            return;
        }
    if (g_P.sess && pi.id != g_P.sess->my_id()) {
        g_c.peers.push_back(peer_text(pi));
        g_peer_names.push_back(name);
    }
}

void on_player_leave(uint8_t id, uint8_t reason)
{
    auto it = g_net.find(id);
    std::string name = g_P.sess ? g_P.sess->name_of(id) : "?";
    if (it != g_net.end())
        SLOG("net: player %u left: %s (rtt %d ms, last heard %u ms before the last check, worst loss %.1f%%/min)", id,
             reason == 1 ? "timed out (nothing heard for the timeout)" : "said goodbye or rejoined", it->second.last_rtt,
             it->second.last_silent, it->second.max_loss);
    else
        SLOG("net: player %u left: %s", id, reason == 1 ? "timed out" : "said goodbye or rejoined");
    g_net.erase(id);
}

void on_disconnected(const std::string& why)
{
    auto it = g_net.find(0);
    SWARN("net: the session with the host ended: %s (rtt %d ms, last heard %u ms before the last check, worst loss %.1f%%/min)",
          why.c_str(), it != g_net.end() ? it->second.last_rtt : -1, it != g_net.end() ? it->second.last_silent : 0,
          it != g_net.end() ? it->second.max_loss : 0.0);
}

void on_session_end(const char* why)
{
    if (g_session_t0 < 0)
        return;
    write_summary(why);
    g_last_end = g_H->time_seconds();
    g_last_end_why = why ? why : "";
    g_session_t0 = -1;
    g_net.clear();
    sync_on_session_end();
    reset_counters();
}

void on_fault() { g_c.faults++; }

void on_shutdown()
{
    if (g_session_t0 >= 0)
        on_session_end("the game ends");
    else if (!g_had_session)
        write_summary("the game ends"); // no session in this run: FPS and memory still help
}

void on_unload()
{
    if (g_reserve) { // a hot reload takes a new one
        VirtualFree(g_reserve, 0, MEM_RELEASE);
        g_reserve = nullptr;
    }
    if (g_mem_thread) {
        WaitForSingleObject(g_mem_thread, 3000);
        CloseHandle(g_mem_thread);
        g_mem_thread = nullptr;
    }
}

std::string command(const std::vector<std::string>& a)
{
    if (a.size() >= 1 && a[0] == "sync")
        return sync_command(a);
    if (a.size() >= 1 && a[0] == "summary")
        return summary_line("asked") + "\n";
    if (a.size() >= 1 && a[0] == "summarysub") { // tests (BUG-1011): the summary file into mods\logs\<folder> (- = none)
        g_summary_sub = a.size() > 1 && a[1] != "-" ? a[1] : "";
        return "summarysub '" + g_summary_sub + "' file '" + summary_file() + "'\n";
    }
    if (a.size() >= 1 && a[0] == "summarywrite") { // tests (BUG-1011): write the summary file now, then read it back
        write_summary("asked");
        std::string f = summary_file();
        char b[64] = {0};
        size_t n = 0;
        if (FILE* fp = summary_open(f, "r")) {
            n = fread(b, 1, sizeof b - 1, fp);
            fclose(fp);
        }
        return "summary written: read back " + std::to_string(n) + " bytes (wide=" + std::to_string((int)g_summary_wide_fix) +
               ")\n";
    }
    if (a.size() >= 2 && a[0] == "detail") {
        g_detail = std::max(0, std::min(2, atoi(a[1].c_str())));
        return "log_detail=" + std::to_string(g_detail) + "\n";
    }
    if (a.size() >= 1 && a[0] == "mark") {
        std::string t;
        for (size_t i = 1; i < a.size(); i++)
            t += (i > 1 ? " " : "") + a[i];
        mark(t);
        return "marked\n";
    }
    if (a.size() >= 2 && a[0] == "reserve") { // tests (coop_editor_soak.py): a planted leak of address space
        static int s_reserved_mb = 0;
        int mb = std::max(0, std::min(512, atoi(a[1].c_str())));
        void* p = mb ? VirtualAlloc(nullptr, (SIZE_T)mb << 20, MEM_RESERVE, PAGE_NOACCESS) : nullptr; // never freed
        s_reserved_mb += p ? mb : 0;
        return std::string(p ? "reserved " : "not reserved ") + std::to_string(mb) + " MB (" + std::to_string(s_reserved_mb) +
               " MB in all)\n";
    }
    if (a.size() >= 2 && a[0] == "fill") { // tests (coop_mem_reserve.py, BUG-0631): the address space as hours of play
        // leave it: every free block of <piece> MB or more reserved (never freed), then every other one given back
        // until <left> MB are free: holes of one piece, no big free block
        int left = std::max(0, atoi(a[1].c_str())), piece = a.size() > 2 ? std::max(1, std::min(64, atoi(a[2].c_str()))) : 16;
        std::vector<void*> got;
        while (got.size() < 8192)
            if (void* p = VirtualAlloc(nullptr, (SIZE_T)piece << 20, MEM_RESERVE, PAGE_NOACCESS))
                got.push_back(p);
            else
                break;
        std::sort(got.begin(), got.end());
        auto free_mb = [] {
            MEMORY_BASIC_INFORMATION mbi;
            uint64_t f = 0;
            for (uintptr_t p = 0x10000; p < 0xFFFF0000u && VirtualQuery((void*)p, &mbi, sizeof mbi) == sizeof mbi;) {
                if (mbi.State == MEM_FREE)
                    f += mbi.RegionSize;
                uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
                if (next <= p)
                    break;
                p = next;
            }
            return (int)(f >> 20);
        };
        int freed = 0, have = free_mb();
        for (size_t i = 0; i < got.size() && have < left; i += 2) { // every other one: the holes do not merge
            VirtualFree(got[i], 0, MEM_RELEASE);
            freed++;
            have += piece;
        }
        return "filled: " + std::to_string(got.size() - freed) + " blocks of " + std::to_string(piece) + " MB kept, " +
               std::to_string(freed) + " given back, " + std::to_string(free_mb()) + " MB free\n";
    }
    if (a.size() >= 1 && a[0] == "reserve" && a.size() == 1) {
        char b[160];
        snprintf(b, sizeof b, "reserve: %s, %u MB, given back %u time(s), switch %s\n", g_reserve ? "kept" : "none",
                 (unsigned)kReserveMB, (unsigned)g_reserve_released, g_fix_mem_reserve ? "on" : "off");
        return b;
    }
    if (a.size() >= 1 && a[0] == "mem") {
        mem_start();
        WaitForSingleObject(g_mem_thread, 5000);
    }
    return stats_status() + sync_status();
}

} // namespace support
