// Primary-button clicks on the game window, taken from the window's own messages (BUG-0090,
// BUG-0145). Polling GetAsyncKeyState(VK_LBUTTON) read the physical left button, wherever the
// cursor and the focus were: a click in another window over the game clicked our galaxy-map tags,
// and with swapped mouse buttons the player's primary click did nothing while the secondary one
// (camera drags) did. WM_LBUTTONDOWN is the logical primary button and only reaches the game
// window when the click is on it. A thread hook (WH_GETMESSAGE) on the game's main thread sees it
// as the game takes it from its queue; the tests' input.click posts the same message.
#pragma once

#include <cstdint>
#include <string>

namespace mouse {

// tests: false = the old polling of the physical left button (coop.space map clickfix 0)
extern bool g_fix;

// install the hook (main thread; again after a hot reload). Safe to call every frame.
void install(const void* game_window);
// Return true to consume the Ctrl+Scroll event instead of native zoom.
void set_fov_wheel(bool (*handler)(int delta));
// The creature editor depth square (BUG-9103): client coordinates; true consumes the message.
void set_depth_input(bool (*handler)(unsigned message, uintptr_t wparam, int x, int y));
void set_graphics_input(bool (*handler)(unsigned message, uintptr_t value, int x, int y));
void set_list_input(bool (*handler)(unsigned message, uintptr_t value, int x, int y));
// BUG-9154: Tribe, right presses and releases (client coordinates); true consumes the message
void set_tribe_input(bool (*handler)(unsigned message, uintptr_t value, int x, int y));
// BUG-9191 (3H): every mouse message's point (client pixels) may be moved before the game and the other handlers see
// it (Spore's scaled UI pieces take the mouse in their unscaled places); true = moved
void set_remap(bool (*handler)(unsigned message, int& x, int& y));
// remove it (on_unload: the hook procedure lives in this DLL)
void shutdown();
// decision 6A A (alpha): Alt+F4 or a click on the window's close button; the handler returns true to hold the close
// (it posts WM_CLOSE itself when it is ready)
void set_close_request(bool (*handler)());
// primary clicks seen so far; a consumer keeps the last value it handled
uint32_t clicks();
// BUG-0482: the characters (WM_CHAR) the game window took from its queue since the last call, and
// forget them; main thread (the hook runs on the thread that pumps the game's messages)
// BUG-0551: *after_click (if given) gets those that came after the last primary click, when there was one
// since the last call (a stalled frame takes the click and the typing after it at once)
std::u16string take_chars(std::u16string* after_click = nullptr);
// BUG-0551: the mod's own click (the "game saved" box's OK, world.cpp) is announced: the next primary click
// at (x, y) within 5 s is the mod's, and last_click_own() says so
void own_click(int x, int y);
bool last_click_own();
// where the last one was, in the game window's client pixels
void last_click(int& x, int& y);

// BUG-0713 (class BUG-0429: mod input read globally): the one way the mod reads a key or the pointer. A key counts
// when the game window took its WM_KEYDOWN / WM_SYSKEYDOWN (not the auto-repeat), so a key pressed while another
// program has the focus does nothing; the pointer is where the last WM_MOUSEMOVE over the game window put it.
// `coop.fix input.window off` (g_window_input false): the callers poll GetAsyncKeyState / GetCursorPos as before.
extern bool g_window_input;
// presses of `vk` the game window took so far (a consumer keeps the last value it handled)
uint32_t key_presses(int vk);
// a new press of `vk` since `seen` (updated): the edge a consumer acts on
bool key_pressed(int vk, uint32_t& seen);
// the last mouse move over the game window, client pixels (false before the first one)
bool last_move(int& x, int& y);

} // namespace mouse
