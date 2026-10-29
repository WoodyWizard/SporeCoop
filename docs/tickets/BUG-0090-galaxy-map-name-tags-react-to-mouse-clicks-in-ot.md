---
id: BUG-0090
title: galaxy map name tags react to mouse clicks in other windows
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#68
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-space
commits: 66d7a30, 4c74ae4
tests: space_map_input
signature:
docs: docs/re/space.md#space-w3-trips-clicks-defenders
related: #68
switch: map.click
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/68 (full text, discussion).

The partner and colony tags on the galaxy map are clicked through the global mouse state (`GetAsyncKeyState`, `GetCursorPos`), without checking that the game window has focus. A click in another window that lies over the game, on the spot where a tag was drawn, starts a trip in the game: a jump to the partner, or a paid flight to a partner's colony. Severity: minor (wrong behaviour in windowed mode).

### Where
- `click_tick`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L831-L855
- What a click runs (`fly_to` with payment, or `go_to_partner`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L822-L829

### How to reproduce
1. Host and guest in Space, connected. The host is in the galaxy view in a window, a partner's tag visible.
2. Put another window (e.g. a text editor) over part of the game window, covering the tag, or alt-tab and leave a window over it.
3. Click in that other window where the tag is under it.
4. The game starts the trip (`coop.space map status` shows `clicks=` and `trips=` growing). For a colony tag the trip's price is taken from the money.

### Expected
Only a click that goes to the game window triggers a tag.

## Root cause
**What happens**

1. Every tick in Space, `click_tick` reads `GetAsyncKeyState(VK_LBUTTON)`. That is the physical button state, whichever window has the focus.
2. On a press edge it takes `GetCursorPos`, converts it with `ScreenToClient(game window)` and tests it against the tag boxes drawn last frame.
3. Nothing checks `GetForegroundWindow() == w`, or that the point is the game's (the window under the cursor). So a click in a browser, chat app or overlay over the game's client area counts as a click on a tag.
4. `g_tags` is filled whenever the overlay drew the map, which also happens while the game window is in the background but still rendering.
5. Spore keeps rendering in the background under Wine and in windowed mode on Windows, though the conditions are not verified.

### Confidence
CONFIRMED by reading (no focus or hit-window check in `click_tick`). That the game keeps drawing the overlay while unfocused depends on the setup and is not verified on every platform.

## Fix
Fixed on fix/w3-space (66d7a30): the tag click comes from the game window's own WM_LBUTTONDOWN, seen by a thread message hook (WH_GETMESSAGE on the main thread, mods/coop/mouse.cpp, removed in on_unload) with the message's client coordinates. A click in another window never sends that message to the game. A GetForegroundWindow check would not do: SporeKit's background mode (tests) hooks it to return the game window. `coop.space map clickfix 0`: the old polling (tests).

Suggested in the review:

At the top of the click handling: `if (GetForegroundWindow() != w) { g_lmb = down; return; }`. Also check `WindowFromPoint(screen point)` is the game window (or its child) before `ScreenToClient`. The tests use SporeKit's virtual cursor and the game window is the foreground window there; if a lane runs headless without focus, SporeKit's hook can report the game window as foreground for tests.

## Verification
Done (2026-10-28, lane 6): `coop_space_map_input.py`: the physical left button pressed over the guest's tag without a message to the game window (`move`, `key 0x01 down/up`: the button state the old code polled). `--without-fix` FAILS (clicks 0 -> 1, the host flew to the guest, scratch log `map_input_nofix.log` (not kept)); with the fix PASS (clicks 0 -> 0), and the message alone (`coop.space map postclick`) still takes the host there.

Planned in the review: Planned test (must FAIL without the fix and PASS with it):

In `tools/tests/coop_space_map.py`, add a case:
1. The host is in the galaxy view with a partner's tag drawn (`coop.space map tags`).
2. Give focus to another window (a small helper window on the headless display, `tools/headless.sh`), positioned over the tag.
3. Click there with a real input event (xdotool on the headless display).
4. Assert on the host that the active star (the game's `GetActiveStarRecord`) and the money are unchanged.
5. Then click the tag in the game window and assert the trip happens.
6. Fails on main (the first click travels), passes with the fix.

### Related
- docs/re/space.md section 13 (galaxy map tags).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #68.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-space (66d7a30): clicks from the window's messages; space_map_input FAIL without / PASS with.
- 2026-10-28: fix/w3-space lab runs on lane 6: 20261028-010933 (14/14: the four new wave 3 Space tests and space_map, space_stale, space_fleet, space_puppet, space_rivalry, space_contracts, space_jump, space_guest_ui, space_allies, space_diplomacy), smoke 20261028-013951 (13/13; `ui` skipped: exclusive, needs a lane 1-5).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: log paths of an agent's scratch folder replaced by the log names (BUG-0215: no paths outside the repository).
- 2026-10-28: switch: map.click (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
