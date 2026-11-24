// Background mode: the game keeps running and rendering when its window is not focused
// (Spore pauses otherwise), and a virtual cursor lets the devkit click without moving
// the real mouse pointer.
#include <algorithm>
#include "core.h"
#include "display_logic.h"

#include <atomic>
#include <cstdio>

namespace sk {

// BUG-1016: written by the console thread (`bg`), read by the window hooks on the game's threads
static std::atomic<bool> g_background{true};
static bool g_test_run; // started with a test profile or as instance 2+
static WNDPROC o_WndProc;
static HWND g_sub_hwnd;

std::atomic<bool> g_vcursor_active{false};
std::atomic<int> g_vcursor_x{0}, g_vcursor_y{0};

typedef HWND(WINAPI* GetHwnd_t)(void);
typedef BOOL(WINAPI* GetCursorPos_t)(LPPOINT);
static GetHwnd_t o_GetForegroundWindow, o_GetActiveWindow, o_GetFocus;
static GetCursorPos_t o_GetCursorPos;

static HWND WINAPI h_GetForegroundWindow()
{
    HWND w = (HWND)game_window();
    return (g_background && w) ? w : o_GetForegroundWindow();
}
static HWND WINAPI h_GetActiveWindow()
{
    HWND w = (HWND)game_window();
    return (g_background && w) ? w : o_GetActiveWindow();
}
static HWND WINAPI h_GetFocus()
{
    HWND w = (HWND)game_window();
    return (g_background && w) ? w : o_GetFocus();
}
static BOOL WINAPI h_GetCursorPos(LPPOINT p)
{
    if (g_vcursor_active && p) {
        POINT c = {g_vcursor_x, g_vcursor_y};
        HWND w = (HWND)game_window();
        if (w)
            ClientToScreen(w, &c);
        *p = c;
        return TRUE;
    }
    return o_GetCursorPos(p);
}

// tests (BUG-0859): the last WM_MOUSEWHEEL the game's window procedure got (wParam), and how many
static std::atomic<uint32_t> g_wheel_wp{0}, g_wheel_n{0};
void wheel_seen(uint32_t& wp, uint32_t& n)
{
    wp = g_wheel_wp;
    n = g_wheel_n;
}

static LRESULT CALLBACK h_WndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_MOUSEWHEEL) {
        g_wheel_wp = (uint32_t)wp;
        g_wheel_n++;
    }
    if (m == WM_SK_PUMP) {
        pump_main_tasks();
        return 0;
    }
    if (m == WM_SK_PUMP_OUT) {
        pump_outside_tasks();
        return 0;
    }
    if (m == WM_SK_STACK) {
        watchdog_stack_here();
        return 0;
    }
    if (g_background) {
        switch (m) {
        case WM_ACTIVATEAPP:
            if (!wp)
                return 0; // swallow "deactivated"
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
                return 0;
            break;
        case WM_KILLFOCUS:
            return 0;
        case WM_NCACTIVATE:
            if (!wp)
                return TRUE;
            break;
        case WM_SYSCOMMAND:
            // Wine minimizes a window that the X11 window manager marks iconic (Mutter: every
            // window on a workspace that is not shown, where agent-ws puts the test games). A
            // minimized Spore draws nothing: App::Canvas::Tick sleeps 200 ms per tick instead.
            if ((wp & 0xFFF0) == SC_MINIMIZE) {
                static bool said;
                if (!said)
                    SKLOG(SK_INFO, "minimize ignored (background mode: keeps drawing on a hidden workspace)");
                said = true;
                return 0;
            }
            break;
        }
    }
    return CallWindowProcW(o_WndProc, w, m, wp, lp);
}

void focus_install_hooks()
{
    // default: on for automated test runs (profile/instance switches), off for normal play
    bool test_run = wcsstr(GetCommandLineW(), L"-sporekit-profile:") || instance_id() > 1;
    g_test_run = test_run;
    g_background = ini_int(mods_dirw() + L"\\sporekit.ini", "sporekit", "background_mode", test_run ? 1 : 0) != 0;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetForegroundWindow"), (void*)h_GetForegroundWindow,
                (void**)&o_GetForegroundWindow);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetActiveWindow"), (void*)h_GetActiveWindow,
                (void**)&o_GetActiveWindow);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetFocus"), (void*)h_GetFocus, (void**)&o_GetFocus);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetCursorPos"), (void*)h_GetCursorPos,
                (void**)&o_GetCursorPos);
}

// The game caps itself at 30 fps (33 ms per frame, slept away in the main loop). Test runs use
// short frames while the game is in its menu when [sporekit] test_frame_ms is set (off by default:
// UI tests and frame-counted waits expect 30 fps). In a game the cap is the game's own again: the
// cell physics, for example, steps per frame and drifts at other frame rates. 0 = no limit;
// frame_ms forces a value everywhere; console: frame.limit <ms> (forced) / frame.limit auto.
// The value is the game's property kFrameLimitMS (id 44 of the AppProperties list: +0xB0 of its value
// array, docs/02-modkit.md#modkit-frame-limit).
// BUG-0779: the "game's own" value was read once at the first frame and written back every 10 frames,
// so a frame limit another program set later (a mod, a frame limiter, the game itself) was undone
// within 10 frames. Now a value SporeKit did not write is taken as the new own value and kept.
// `kit.fix framelimit 0`: the value of the first frame is written back, as before (0.3.15).
static int g_forced = -2; // -2: not read yet, -1: automatic
static int g_menu_ms = -1;
static int g_game_ms = 33; // the game's own value, read before the first change
static int g_last_seen = -1; // the value after SporeKit's last look / write (-1: never)
static unsigned g_others = 0; // changes by someone else seen

// BUG-0865: live 1-360 fps cap with high-resolution waitable timer pacing and borderless windowed mode
static int g_frame_target = -1; // -1: unread from sporekit.ini, 0: auto (game default), 1..360: fps cap
static skdisp::PacerState g_pacer{};
static HANDLE g_pace_timer = nullptr;
static bool g_pace_timer_tried = false;
static bool g_pace_high_res = false;
static LARGE_INTEGER g_qpc_freq{};

static void frame_target_ensure_loaded()
{
    if (g_frame_target >= 0)
        return;
    int v = ini_int(mods_dirw() + L"\\sporekit.ini", "sporekit", "frame_target", 0);
    g_frame_target = skdisp::clamp_fps(v);
    if (g_frame_target > 0)
        SKLOG(SK_INFO, "frame target: loaded %d fps from sporekit.ini", g_frame_target);
}

static int64_t qpc_now_ns()
{
    if (g_qpc_freq.QuadPart == 0)
        QueryPerformanceFrequency(&g_qpc_freq);
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    if (g_qpc_freq.QuadPart <= 0)
        return (int64_t)(now_seconds() * 1e9);
    int64_t sec = c.QuadPart / g_qpc_freq.QuadPart;
    int64_t rem = c.QuadPart % g_qpc_freq.QuadPart;
    return sec * 1000000000LL + (rem * 1000000000LL) / g_qpc_freq.QuadPart;
}

static void ensure_pace_timer()
{
    if (g_pace_timer_tried)
        return;
    g_pace_timer_tried = true;
    using CreateWaitableTimerExW_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto fn = k32 ? (CreateWaitableTimerExW_t)(void*)GetProcAddress(k32, "CreateWaitableTimerExW") : nullptr;
    constexpr DWORD kHighRes = 0x00000002u; // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    if (fn) {
        g_pace_timer = fn(nullptr, nullptr, kHighRes, TIMER_ALL_ACCESS);
        if (g_pace_timer) {
            g_pace_high_res = true;
            SKLOG(SK_INFO, "frame target: high-resolution waitable timer ready");
            return;
        }
    }
    g_pace_timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    if (g_pace_timer)
        SKLOG(SK_INFO, "frame target: high-resolution timer unavailable, using standard waitable timer");
    else
        SKLOG(SK_WARN, "frame target: waitable timer unavailable, using Sleep fallback");
}

const char* frame_target_timer_kind()
{
    if (!g_pace_timer_tried)
        return "idle";
    if (g_pace_high_res)
        return "high-res";
    return g_pace_timer ? "timer" : "sleep";
}

int frame_target_now()
{
    frame_target_ensure_loaded();
    return g_frame_target;
}

int set_frame_target(int fps, bool persist)
{
    frame_target_ensure_loaded();
    fps = skdisp::clamp_fps(fps);
    g_frame_target = fps;
    g_pacer.next_ns = 0;
    if (persist)
        ini_set_str(mods_dirw() + L"\\sporekit.ini", "sporekit", "frame_target", std::to_string(fps).c_str());
    SKLOG(SK_INFO, "frame target: %d fps%s (fix=%d)", fps, fps == 0 ? " (auto)" : "",
          fixed(g_fix_frametarget) ? 1 : 0);
    apply_frame_limit();
    return fps;
}

void pace_frame_target()
{
    frame_target_ensure_loaded();
    if (!fixed(g_fix_frametarget) || g_frame_target <= 0) {
        g_pacer.next_ns = 0;
        return;
    }
    int64_t now = qpc_now_ns();
    int64_t wait_ns = skdisp::pace_step(g_pacer, g_frame_target, now);
    if (wait_ns <= 0)
        return;
    int64_t deadline = now + wait_ns;
    ensure_pace_timer();
    int64_t sleep_100ns = skdisp::timer_sleep_100ns(wait_ns, g_pace_high_res);
    if (sleep_100ns > 0) {
        if (g_pace_timer) {
            LARGE_INTEGER due{};
            due.QuadPart = -sleep_100ns;
            if (SetWaitableTimer(g_pace_timer, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(g_pace_timer, INFINITE);
        } else {
            DWORD ms = (DWORD)(sleep_100ns / 10000LL);
            if (ms > 0)
                Sleep(ms);
        }
    }
    for (int spins = 0; spins < 50000 && qpc_now_ns() < deadline; spins++)
        SwitchToThread();
}

void set_frame_limit(int ms)
{
    g_forced = ms;
    SKLOG(SK_INFO, "frame limit: %d ms%s", ms, ms < 0 ? " (automatic)" : "");
    apply_frame_limit();
}

static int* frame_limit_field()
{
    uintptr_t p = sym("App::FrameLimit_ptr");
    uint8_t* app = p ? *(uint8_t**)p : nullptr;
    uint8_t* cfg = app ? *(uint8_t**)(app + 0x3C) : nullptr;
    return cfg ? (int*)(cfg + 0xB0) : nullptr;
}

void apply_frame_limit()
{
    frame_target_ensure_loaded();
    if (g_forced == -2) {
        std::wstring ini = mods_dirw() + L"\\sporekit.ini";
        g_forced = ini_int(ini, "sporekit", "frame_ms", -1);
        g_menu_ms = g_test_run ? ini_int(ini, "sporekit", "test_frame_ms", -1) : -1;
        if (g_menu_ms >= 0)
            SKLOG(SK_INFO, "frame limit: %d ms in the menus (test run)", g_menu_ms);
    }
    int* f = frame_limit_field();
    if (!f)
        return;
    static bool read_own;
    int cur = *f;
    if (!read_own) {
        g_game_ms = cur;
        read_own = true;
        SKLOG(SK_INFO, "frame limit: the game's own %d ms (kFrameLimitMS) at frame %llu%s", cur,
              (unsigned long long)frame_index(), cur == 0 ? " (no limit: the frame rate is not capped)" : "");
    } else if (g_last_seen >= 0 && cur != g_last_seen) {
        g_others++;
        if (fixed(g_fix_framelimit)) {
            SKLOG(SK_INFO, "frame limit: set to %d ms by another program or the game (was %d ms): kept as the "
                           "game's own (BUG-0779)%s", cur, g_last_seen, g_forced >= 0 ? "; frame.limit / frame_ms still wins" : "");
            g_game_ms = cur;
        } else if (g_others <= 5)
            SKLOG(SK_INFO, "frame limit: set to %d ms by another program or the game; written back to %d ms "
                           "(kit.fix framelimit 0)", cur, g_game_ms);
    }
    uint32_t mode = game_mode_id();
    bool menu = mode == 0 || mode == 0x2CCD1D2; // startup / GalaxyGameEntry
    int want = skdisp::desired_frame_ms(g_frame_target, fixed(g_fix_frametarget), g_forced, g_menu_ms, menu, g_game_ms);
    if (*f != want)
        *f = want;
    g_last_seen = want;
}

// ---- Borderless windowed mode (BUG-0865) ----
static skdisp::BorderlessState g_bl{};
static bool g_bl_loaded = false;
static WINDOWPLACEMENT g_bl_wp{};

static void borderless_ensure_loaded()
{
    if (g_bl_loaded)
        return;
    g_bl_loaded = true;
    g_bl.enabled = ini_int(mods_dirw() + L"\\sporekit.ini", "sporekit", "borderless", 0) != 0;
    if (g_bl.enabled)
        SKLOG(SK_INFO, "borderless: loaded on from sporekit.ini");
}

static bool monitor_rect_of(HWND w, skdisp::RectI& out)
{
    HMONITOR m = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (!m || !GetMonitorInfoW(m, &mi))
        return false;
    out = {(int32_t)mi.rcMonitor.left, (int32_t)mi.rcMonitor.top, (int32_t)mi.rcMonitor.right,
           (int32_t)mi.rcMonitor.bottom};
    return out.valid();
}

bool borderless_enabled()
{
    borderless_ensure_loaded();
    return g_bl.enabled;
}

bool borderless_applied() { return g_bl.applied; }
bool borderless_video_windowed() { return g_bl.video_windowed; }

void borderless_video_mode(bool windowed)
{
    borderless_ensure_loaded();
    g_bl.video_windowed = windowed;
    if (!windowed) {
        g_bl.applied = false;
        return;
    }
    if (g_bl.enabled && fixed(g_fix_borderless))
        run_outside_frame(maintain_borderless);
}

void maintain_borderless()
{
    borderless_ensure_loaded();
    HWND w = (HWND)game_window();
    if (!w || !IsWindow(w))
        return;
    uint32_t cur_style = (uint32_t)GetWindowLongW(w, GWL_STYLE);
    uint32_t cur_ex = (uint32_t)GetWindowLongW(w, GWL_EXSTYLE);
    RECT wr{};
    GetWindowRect(w, &wr);
    skdisp::RectI cur_rect{(int32_t)wr.left, (int32_t)wr.top, (int32_t)wr.right, (int32_t)wr.bottom};
    skdisp::RectI mon{};
    monitor_rect_of(w, mon);
    skdisp::BorderlessAction act = skdisp::decide_borderless(g_bl, fixed(g_fix_borderless), cur_style, cur_rect, mon);
    if (act == skdisp::BorderlessAction::Apply) {
        if (!g_bl.applied) {
            g_bl.saved_style = cur_style;
            g_bl.saved_exstyle = cur_ex;
            g_bl.saved_rect = cur_rect;
            g_bl_wp = {};
            g_bl_wp.length = sizeof g_bl_wp;
            g_bl.have_saved = GetWindowPlacement(w, &g_bl_wp) != FALSE;
        }
        uint32_t ns = skdisp::borderless_style(cur_style);
        uint32_t nex = skdisp::borderless_exstyle(cur_ex);
        if (ns != cur_style)
            SetWindowLongW(w, GWL_STYLE, (LONG)ns);
        if (nex != cur_ex)
            SetWindowLongW(w, GWL_EXSTYLE, (LONG)nex);
        SetWindowPos(w, HWND_TOP, mon.left, mon.top, mon.width(), mon.height(),
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        g_bl.applied = true;
        SKLOG(SK_INFO, "borderless: applied (%d,%d)-(%d,%d) style=0x%08X", mon.left, mon.top, mon.right, mon.bottom,
              ns);
    } else if (act == skdisp::BorderlessAction::Restore) {
        SetWindowLongW(w, GWL_STYLE, (LONG)g_bl.saved_style);
        SetWindowLongW(w, GWL_EXSTYLE, (LONG)g_bl.saved_exstyle);
        if (g_bl.have_saved)
            SetWindowPlacement(w, &g_bl_wp);
        SetWindowPos(w, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        g_bl.applied = false;
        SKLOG(SK_INFO, "borderless: restored style=0x%08X rect=(%d,%d)-(%d,%d)", g_bl.saved_style, g_bl.saved_rect.left,
              g_bl.saved_rect.top, g_bl.saved_rect.right, g_bl.saved_rect.bottom);
    }
}

void maintain_borderless_if_needed()
{
    borderless_ensure_loaded();
    HWND w = (HWND)game_window();
    if (!w || !IsWindow(w))
        return;
    uint32_t cur_style = (uint32_t)GetWindowLongW(w, GWL_STYLE);
    RECT wr{};
    GetWindowRect(w, &wr);
    skdisp::RectI cur_rect{(int32_t)wr.left, (int32_t)wr.top, (int32_t)wr.right, (int32_t)wr.bottom};
    skdisp::RectI mon{};
    monitor_rect_of(w, mon);
    if (skdisp::decide_borderless(g_bl, fixed(g_fix_borderless), cur_style, cur_rect, mon) !=
        skdisp::BorderlessAction::None)
        run_outside_frame(maintain_borderless);
}

bool set_borderless(bool on, bool persist)
{
    borderless_ensure_loaded();
    g_bl.enabled = on;
    if (persist)
        ini_set_str(mods_dirw() + L"\\sporekit.ini", "sporekit", "borderless", on ? "1" : "0");
    SKLOG(SK_INFO, "borderless: %s (windowed=%d fix=%d)", on ? "on" : "off", g_bl.video_windowed ? 1 : 0,
          fixed(g_fix_borderless) ? 1 : 0);
    if (!in_frame() && GetCurrentThreadId() == main_thread_id())
        maintain_borderless();
    else
        run_outside_frame(maintain_borderless);
    return g_bl.enabled;
}

std::string borderless_status()
{
    borderless_ensure_loaded();
    HWND w = (HWND)game_window();
    uint32_t st = w ? (uint32_t)GetWindowLongW(w, GWL_STYLE) : 0;
    RECT wr{};
    if (w)
        GetWindowRect(w, &wr);
    skdisp::RectI mon{};
    if (w)
        monitor_rect_of(w, mon);
    char b[256];
    snprintf(b, sizeof b,
             "window.borderless enabled=%d applied=%d windowed=%d fix=%d style=0x%08X borders=%d "
             "rect=%ld,%ld,%ld,%ld mon=%d,%d,%d,%d",
             g_bl.enabled ? 1 : 0, g_bl.applied ? 1 : 0, g_bl.video_windowed ? 1 : 0,
             fixed(g_fix_borderless) ? 1 : 0, st, skdisp::has_window_borders(st) ? 1 : 0, wr.left, wr.top, wr.right,
             wr.bottom, mon.left, mon.top, mon.right, mon.bottom);
    return b;
}

int frame_limit_now()
{
    int* f = frame_limit_field();
    return f ? *f : -1;
}

int frame_limit_own() { return g_game_ms; }
unsigned frame_limit_changes() { return g_others; }

// called once the game window is known (device creation)
void focus_attach_window(void* hwnd)
{
    if (!hwnd || g_sub_hwnd == hwnd)
        return;
    g_sub_hwnd = (HWND)hwnd;
    o_WndProc = (WNDPROC)SetWindowLongPtrW(g_sub_hwnd, GWLP_WNDPROC, (LONG_PTR)h_WndProc);
    SKLOG(SK_INFO, "window %p subclassed (background mode %s)", hwnd, g_background.load() ? "on" : "off");
}

// BUG-1016: written by the console thread (move / button / click), read by vcursor_tick on the main thread
static std::atomic<DWORD> g_vcursor_last{0};
static std::atomic<bool> g_vcursor_held{false};

void vcursor_hold(bool on)
{
    g_vcursor_held = on;
    g_vcursor_last = GetTickCount();
}

void vcursor_set(int x, int y)
{
    g_vcursor_x = x;
    g_vcursor_y = y;
    g_vcursor_active = true;
    g_vcursor_last = GetTickCount();
}

// BUG-0804: a click made for the mod (the game saved box's OK) gives the mouse back to the player at
// once; held buttons (`button ... down`) keep it
void vcursor_release()
{
    if (!g_vcursor_held)
        g_vcursor_active = false;
}

// tests (console `cursor`): where the game reads the mouse (the hooked GetCursorPos), client coordinates
bool vcursor_state(int& x, int& y, bool& virt)
{
    POINT p{};
    virt = g_vcursor_active;
    if (!GetCursorPos(&p))
        return false;
    if (HWND w = (HWND)game_window())
        ScreenToClient(w, &p);
    x = p.x;
    y = p.y;
    return true;
}

void vcursor_tick()
{
    if (g_vcursor_active && !g_vcursor_held && GetTickCount() - g_vcursor_last > 5000)
        g_vcursor_active = false;
}

// Test runs go to a secondary monitor (if there is one) so they do not flash in front of the
// user: [sporekit] test_monitor = auto (secondary) | primary | off | grid. Instance 1 sits at the
// left edge, the others at the right edge of that monitor. grid (the headless test display of
// tools/headless.sh): instance N gets its own cell of a 4-wide grid, so no window covers another
// (a covered window gets no frames from the compositor and the game stops).

static BOOL CALLBACK monitor_cb(HMONITOR m, HDC, LPRECT, LPARAM lp)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(m, &mi))
        ((std::vector<MONITORINFO>*)lp)->push_back(mi);
    return TRUE;
}

void place_test_window()
{
    HWND w = (HWND)game_window();
    if (!g_test_run || !w)
        return;
    if (borderless_enabled() && fixed(g_fix_borderless))
        return; // BUG-0865: borderless mode owns the window rect
    std::string mode = ini_str(mods_dirw() + L"\\sporekit.ini", "sporekit", "test_monitor", "auto");
    if (mode == "off" || mode == "primary")
        return;
    if (mode == "grid") {
        RECT wr;
        GetWindowRect(w, &wr);
        // as many cells as fit on the test screen (a frame may hang over its edge a little); every
        // instance needs its own cell: a covered window gets no frames
        int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        int cols = std::max(1, (GetSystemMetrics(SM_CXVIRTUALSCREEN) + 64) / std::max(1, ww));
        int rows = std::max(1, (GetSystemMetrics(SM_CYVIRTUALSCREEN) + 128) / std::max(1, wh));
        int n = (instance_id() - 1) % (cols * rows);
        int sx = std::min(ww, GetSystemMetrics(SM_CXVIRTUALSCREEN) / cols); // frames overlap, not pictures
        int sy = std::min(wh, GetSystemMetrics(SM_CYVIRTUALSCREEN) / rows);
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN) + (n % cols) * sx;
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN) + (n / cols) * sy;
        if (wr.left != x || wr.top != y) {
            SetWindowPos(w, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            SKLOG(SK_INFO, "test window placed in the grid (%d,%d)", x, y);
        }
        return;
    }
    std::vector<MONITORINFO> mons;
    EnumDisplayMonitors(nullptr, nullptr, monitor_cb, (LPARAM)&mons);
    const MONITORINFO* target = nullptr;
    for (auto& m : mons)
        if (!(m.dwFlags & MONITORINFOF_PRIMARY))
            target = &m;
    if (!target)
        return; // one monitor: leave the window where it is
    RECT wr;
    GetWindowRect(w, &wr);
    int ww = wr.right - wr.left;
    const RECT& r = target->rcWork;
    int x = instance_id() <= 1 ? r.left : (r.right - ww > r.left ? r.right - ww : r.left);
    if (wr.left == x && wr.top == r.top)
        return;
    SetWindowPos(w, nullptr, x, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SKLOG(SK_INFO, "test window moved to the secondary monitor (%d,%d)", x, (int)r.top);
}

bool background_mode() { return g_background; }
void set_background_mode(bool on) { g_background = on; }

} // namespace sk
