// Support diagnostics: FPS and hitches, the address space, network quality, session settings and
// the session summary (diag_coop.h, docs/REPORTING.md#reporting-support-answers).
#include "diag_coop.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>

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

static void net_second(double now, bool minute)
{
    if (!in_session())
        return;
    uint32_t nowms = GetTickCount();
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

static std::string summary_file()
{
    const char* d = g_H->size >= offsetof(SkHost, logs_dir) + sizeof(void*) && g_H->logs_dir ? g_H->logs_dir() : nullptr;
    char inst[16];
    snprintf(inst, sizeof inst, "%d", g_H->instance_id());
    return d ? std::string(d) + "\\coop-last-session" + (g_H->instance_id() > 1 ? std::string(".") + inst : "") + ".txt" : "";
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
        if (FILE* fp = fopen(f.c_str(), "w")) {
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
static double g_minute_at = -1, g_mem_at = -1e9;

void stats_tick(double now)
{
    perf_frame(now);
    if (g_P.mode)
        stage_track(g_P.mode());
    if (now - g_net_sec >= 1.0) {
        bool minute = g_minute_at >= 0 && now - g_minute_at >= 60;
        net_second(now, minute);
        g_net_sec = now;
        if (g_minute_at < 0)
            g_minute_at = now;
        if (in_session())
            settings_check(now, false);
        if (now - g_mem_at >= 60) { // the walk runs on a thread: its result is read next time
            g_mem_at = now;
            mem_start();
        }
        if (minute) {
            int fps = g_min_fps_min < 1e8 ? (int)g_min_fps_min : -1;
            int avg = (int)(g_min_frames / (now - g_min_start) + 0.5);
            if (fps >= 0 && (g_c.min_fps < 0 || fps < g_c.min_fps))
                g_c.min_fps = fps;
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
    // the summary of the last session of an earlier run stays at the top of reports until a new one
    std::string f = summary_file();
    if (!f.empty() && kit_has_headline())
        if (FILE* fp = fopen(f.c_str(), "r")) {
            char b[1200] = {0};
            size_t n = fread(b, 1, sizeof b - 1, fp);
            fclose(fp);
            b[n] = 0;
            if (n)
                g_H->diag_headline(g_M, (std::string("(an earlier run) ") + b).c_str());
        }
    reset_counters();
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
    if (a.size() >= 1 && a[0] == "mem") {
        mem_start();
        WaitForSingleObject(g_mem_thread, 5000);
    }
    return stats_status() + sync_status();
}

} // namespace support
