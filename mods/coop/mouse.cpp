// Primary-button clicks on the game window (mouse.h).
#include "mouse.h"
#include "fixes.h"

#include <windows.h>
#include <string>

namespace mouse {

bool g_fix = true;
COOP_FIX("map.click", "BUG-0090 BUG-0145", g_fix, "space: map name tags react only to clicks in the game window");
static HHOOK g_hook;
static HWND g_wnd;
static volatile uint32_t g_clicks;
static volatile int g_x, g_y;
static std::u16string g_chars; // BUG-0482: WM_CHAR seen since the last take_chars (main thread)
static size_t g_click_pos = std::u16string::npos; // BUG-0551: where in g_chars the last click came
static bool g_own_armed, g_last_own;
static int g_own_x, g_own_y;
static ULONGLONG g_own_until;
// BUG-0713 (class BUG-0429): the keys the game window took from its queue (WM_KEYDOWN / WM_SYSKEYDOWN without
// the auto-repeat bit) and the last mouse move over it
static volatile uint32_t g_key_presses[256];
static volatile int g_move_x, g_move_y;
static volatile bool g_moved;

bool g_window_input = true;
COOP_FIX("input.window", "BUG-0429 BUG-0713", g_window_input,
         "F9, the social digits and the Civ marker read input only from the game window's own messages");

static bool ours(HWND w)
{
    return w && g_wnd && (w == g_wnd || IsChild(g_wnd, w));
}

static LRESULT CALLBACK proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE && lp) {
        const MSG* m = (const MSG*)lp;
        if ((m->message == WM_LBUTTONDOWN || m->message == WM_LBUTTONDBLCLK) && ours(m->hwnd)) {
            POINT p{(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
            if (m->hwnd != g_wnd)
                MapWindowPoints(m->hwnd, g_wnd, &p, 1);
            g_x = p.x;
            g_y = p.y;
            g_clicks = g_clicks + 1;
            g_click_pos = g_chars.size();
            g_last_own = g_own_armed && p.x == g_own_x && p.y == g_own_y && GetTickCount64() < g_own_until;
            if (g_last_own)
                g_own_armed = false;
        }
        if ((m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN) && !(m->lParam & (1 << 30)) && m->wParam < 256 &&
            ours(m->hwnd))
            g_key_presses[m->wParam] = g_key_presses[m->wParam] + 1;
        if (m->message == WM_MOUSEMOVE && ours(m->hwnd)) {
            POINT p{(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
            if (m->hwnd != g_wnd)
                MapWindowPoints(m->hwnd, g_wnd, &p, 1);
            g_move_x = p.x;
            g_move_y = p.y;
            g_moved = true;
        }
        if (m->message == WM_CHAR && ours(m->hwnd) && g_chars.size() < 256)
            g_chars.push_back((char16_t)m->wParam);
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

void install(const void* game_window)
{
    g_wnd = (HWND)game_window;
    if (g_hook || !g_wnd)
        return;
    g_hook = SetWindowsHookExW(WH_GETMESSAGE, proc, nullptr, GetCurrentThreadId());
}

void shutdown()
{
    if (g_hook)
        UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

uint32_t clicks() { return g_clicks; }

uint32_t key_presses(int vk) { return vk >= 0 && vk < 256 ? g_key_presses[vk] : 0; }

bool key_pressed(int vk, uint32_t& seen)
{
    uint32_t n = key_presses(vk);
    bool r = n != seen;
    seen = n;
    return r;
}

bool last_move(int& x, int& y)
{
    if (!g_moved)
        return false;
    x = g_move_x;
    y = g_move_y;
    return true;
}

std::u16string take_chars(std::u16string* after_click)
{
    if (after_click)
        *after_click = g_click_pos != std::u16string::npos && g_click_pos < g_chars.size() ? g_chars.substr(g_click_pos)
                                                                                         : std::u16string();
    g_click_pos = std::u16string::npos;
    std::u16string r;
    r.swap(g_chars);
    return r;
}

void own_click(int x, int y)
{
    g_own_x = x;
    g_own_y = y;
    g_own_until = GetTickCount64() + 5000;
    g_own_armed = true;
}

bool last_click_own() { return g_last_own; }

void last_click(int& x, int& y)
{
    x = g_x;
    y = g_y;
}

} // namespace mouse
