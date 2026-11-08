---
id: BUG-0713
title: F9, the social digits and the Civ marker read keys and the pointer outside the game window
status: fixed
kind: bug
stage: All
reported: 2026-11-08
source: BUG-0429 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content
commits: 3ff7a2da
tests: window_input, window_input_lowfps
signature:
docs: docs/08-ui.md#ui-window-input
related: BUG-0429, BUG-0090, BUG-0145, BUG-0149, BUG-0482
---

## Report
K2b audit of the BUG-0429 sibling list (2026-11-08): the mod still read three inputs from the whole desktop instead of
the game window. F9 typed in another program (a browser, a chat client) showed or hid the co-op window
(coop_ui.cpp GetAsyncKeyState(VK_F9), no focus check); the social digits 1-4 read GetAsyncKeyState with a
GetForegroundWindow check (BUG-0149); the Civ / Space RTS marker that shows partners where a player points followed
the screen cursor (GetCursorPos) also while it was over another window. The other items of the list are not mod
input: the Sporepedia search, editor naming and save-name dialogs are the game's own text fields; the mod's own
text line takes its characters from the game window's WM_CHAR (BUG-0482), its clicks from WM_LBUTTONDOWN (BUG-0145).

## Root cause
Observed (code): polling of the physical key / cursor state, which Windows (and Wine) answer for the whole desktop.

## Fix
One way the mod reads input (mouse.h, `mouse::key_presses / key_pressed / last_move`): the thread message hook
(WH_GETMESSAGE) that already took the primary clicks and the characters now also counts the WM_KEYDOWN /
WM_SYSKEYDOWN the game window takes from its queue (not the auto-repeat) and keeps the last WM_MOUSEMOVE over it.
F9 (coop_ui.cpp), the social digits (social.cpp) and the RTS marker (modes.cpp mouse_on_planet) use it.
`coop.fix input.window off`: the old polling. SporeKit's `key <vk> elsewhere` presses a key the way another
program's focus does (the physical state is down for 3 frames, the game window gets no message) for the test.

## Verification
FAIL (SPORE_FIX_OFF=input.window): out/lab/20261108-030421 (host and guest: F9 in another program opened the window
3/3). PASS: out/lab/20261108-030426, out/lab/20261108-030629 (window_input and window_input_lowfps at 8 fps).

## History
- 2026-11-08: opened from the BUG-0429 audit (K2b of 0.7.11) and fixed on fix/w46-k2b-content.
