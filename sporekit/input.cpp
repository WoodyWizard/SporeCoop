// Input injection for automated tests: window messages + virtual key state
// (GetAsyncKeyState/GetKeyState overrides, since the game polls some keys).
#include "core.h"

#include <atomic>

namespace sk {

static std::atomic<uint8_t> g_vkdown[256];

typedef SHORT(WINAPI* GetAsyncKeyState_t)(int);
typedef SHORT(WINAPI* GetKeyState_t)(int);
static GetAsyncKeyState_t o_GetAsyncKeyState;
static GetKeyState_t o_GetKeyState;

static SHORT WINAPI h_GetAsyncKeyState(int vk)
{
    SHORT r = o_GetAsyncKeyState(vk);
    if (vk >= 0 && vk < 256 && g_vkdown[vk])
        r |= (SHORT)0x8001;
    return r;
}

static SHORT WINAPI h_GetKeyState(int vk)
{
    SHORT r = o_GetKeyState(vk);
    if (vk >= 0 && vk < 256 && g_vkdown[vk])
        r |= (SHORT)0x8000;
    return r;
}

static void ensure_hooks()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetAsyncKeyState"), (void*)h_GetAsyncKeyState,
                (void**)&o_GetAsyncKeyState);
    hook_create(&g_kit_mod, (void*)GetProcAddress(u, "GetKeyState"), (void*)h_GetKeyState,
                (void**)&o_GetKeyState);
}

static LPARAM key_lparam(int vk, bool up)
{
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM lp = 1 | (sc << 16);
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_INSERT: case VK_DELETE:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        lp |= 1 << 24;
        break;
    }
    if (up)
        lp |= (1u << 30) | (1u << 31);
    return lp;
}

bool input_key(int vk, const char* mode)
{
    HWND w = (HWND)game_window();
    if (!w || vk <= 0 || vk > 255)
        return false;
    ensure_hooks();
    if (mode && !strcmp(mode, "elsewhere")) {
        // BUG-0713 (class BUG-0429): the key pressed while another program has the focus: the physical key state
        // (GetAsyncKeyState / GetKeyState) is down for a few frames, the game window gets no message
        g_vkdown[vk] = 1;
        int v = vk;
        run_after_frames(3, [v] { g_vkdown[v] = 0; });
        return true;
    }
    bool down = !mode || !strcmp(mode, "tap") || !strcmp(mode, "down");
    bool up = !mode || !strcmp(mode, "tap") || !strcmp(mode, "up");
    UINT kd = (vk == VK_MENU || vk == VK_F10) ? WM_SYSKEYDOWN : WM_KEYDOWN;
    UINT ku = (vk == VK_MENU || vk == VK_F10) ? WM_SYSKEYUP : WM_KEYUP;
    if (down) {
        g_vkdown[vk] = 1;
        PostMessageW(w, kd, vk, key_lparam(vk, false));
    }
    if (up) {
        if (down) {
            // keep the key "held" for a few frames so polling code sees it
            int v = vk;
            run_after_frames(3, [v] {
                HWND w2 = (HWND)game_window();
                g_vkdown[v] = 0;
                PostMessageW(w2, (v == VK_MENU || v == VK_F10) ? WM_SYSKEYUP : WM_KEYUP, v,
                             key_lparam(v, true));
            });
        } else {
            g_vkdown[vk] = 0;
            PostMessageW(w, ku, vk, key_lparam(vk, true));
        }
    }
    return true;
}

bool input_text(const std::string& utf8)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    std::wstring t = widen(utf8);
    for (wchar_t c : t)
        PostMessageW(w, WM_CHAR, c, 1);
    return true;
}

bool input_move(int x, int y)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    vcursor_set(x, y); // virtual cursor: the real mouse pointer is not moved
    WPARAM mk = (g_vkdown[VK_LBUTTON] ? MK_LBUTTON : 0) | (g_vkdown[VK_RBUTTON] ? MK_RBUTTON : 0);
    PostMessageW(w, WM_MOUSEMOVE, mk, MAKELPARAM(x, y));
    return true;
}

bool input_wheel(int x, int y, int notches)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    input_move(x, y);
    POINT pt{x, y};
    ClientToScreen(w, &pt);
    int n = notches < 0 ? -notches : notches;
    short delta = notches < 0 ? -WHEEL_DELTA : WHEEL_DELTA;
    for (int i = 0; i < n && i < 100; i++)
        run_after_frames(2 + i * 2, [w, delta, pt] {
            PostMessageW(w, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), MAKELPARAM(pt.x, pt.y));
        });
    return true;
}

bool input_click(int x, int y, int button, bool release_cursor)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    input_move(x, y);
    UINT d = button == 1 ? WM_RBUTTONDOWN : (button == 2 ? WM_MBUTTONDOWN : WM_LBUTTONDOWN);
    UINT u = button == 1 ? WM_RBUTTONUP : (button == 2 ? WM_MBUTTONUP : WM_LBUTTONUP);
    WPARAM mk = button == 1 ? MK_RBUTTON : (button == 2 ? MK_MBUTTON : MK_LBUTTON);
    int vk = button == 1 ? VK_RBUTTON : (button == 2 ? VK_MBUTTON : VK_LBUTTON);
    ensure_hooks();
    // hover for a few frames, press, hold, release (UI code reacts per frame)
    run_after_frames(3, [w, d, mk, x, y, vk] {
        vcursor_set(x, y);
        g_vkdown[vk] = 1;
        PostMessageW(w, d, mk, MAKELPARAM(x, y));
    });
    run_after_frames(6, [w, u, x, y, vk, release_cursor] {
        g_vkdown[vk] = 0;
        PostMessageW(w, u, 0, MAKELPARAM(x, y));
        if (release_cursor) // BUG-0804: the game reads the real mouse again (it kept the click's point for 5 s)
            run_after_frames(2, [] { vcursor_release(); });
    });
    return true;
}

// press (and keep holding) or release a button at (x, y); while held, `move` steers the cursor
bool input_button(int x, int y, int button, bool down)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    ensure_hooks();
    input_move(x, y);
    int vk = button == 1 ? VK_RBUTTON : (button == 2 ? VK_MBUTTON : VK_LBUTTON);
    WPARAM mk = button == 1 ? MK_RBUTTON : (button == 2 ? MK_MBUTTON : MK_LBUTTON);
    if (down) {
        UINT d = button == 1 ? WM_RBUTTONDOWN : (button == 2 ? WM_MBUTTONDOWN : WM_LBUTTONDOWN);
        g_vkdown[vk] = 1;
        vcursor_hold(true);
        PostMessageW(w, d, mk, MAKELPARAM(x, y));
    } else {
        UINT u = button == 1 ? WM_RBUTTONUP : (button == 2 ? WM_MBUTTONUP : WM_LBUTTONUP);
        g_vkdown[vk] = 0;
        vcursor_hold(false);
        PostMessageW(w, u, 0, MAKELPARAM(x, y));
    }
    return true;
}

// press at (x1, y1), move in steps, release at (x2, y2): selection boxes, camera drags
bool input_drag(int x1, int y1, int x2, int y2, int button)
{
    HWND w = (HWND)game_window();
    if (!w)
        return false;
    input_move(x1, y1);
    UINT d = button == 1 ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
    UINT u = button == 1 ? WM_RBUTTONUP : WM_LBUTTONUP;
    WPARAM mk = button == 1 ? MK_RBUTTON : MK_LBUTTON;
    int vk = button == 1 ? VK_RBUTTON : VK_LBUTTON;
    ensure_hooks();
    run_after_frames(3, [w, d, mk, x1, y1, vk] {
        vcursor_set(x1, y1);
        g_vkdown[vk] = 1;
        PostMessageW(w, d, mk, MAKELPARAM(x1, y1));
    });
    const int steps = 8;
    for (int i = 1; i <= steps; i++) {
        int x = x1 + (x2 - x1) * i / steps, y = y1 + (y2 - y1) * i / steps;
        run_after_frames(4 + i, [w, mk, x, y] {
            vcursor_set(x, y);
            PostMessageW(w, WM_MOUSEMOVE, mk, MAKELPARAM(x, y));
        });
    }
    run_after_frames(6 + steps, [w, u, x2, y2, vk] {
        g_vkdown[vk] = 0;
        PostMessageW(w, u, 0, MAKELPARAM(x2, y2));
    });
    return true;
}

} // namespace sk
