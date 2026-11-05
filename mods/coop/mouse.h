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
// remove it (on_unload: the hook procedure lives in this DLL)
void shutdown();
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

} // namespace mouse
