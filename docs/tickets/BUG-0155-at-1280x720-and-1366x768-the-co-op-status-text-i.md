---
id: BUG-0155
title: at 1280x720 and 1366x768 the co-op status text is drawn over the co-op window and the notifications
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#134
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 5f0c909
tests: tools/tests/coop_window_layout.py block (suite window_layout)
signature:
docs: docs/06-testing.md#testing-sys-w3
related: #134
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/134 (full text, discussion).

The overlay draws a status block in the top right corner in every frame of a session, even while the world labels are hidden for a menu or the co-op window. The block is "CO-OP host :47810  2/4" plus one line per player, and the creature team line. It sits at a fixed x = width - 480 and y = 70 to about 190. Two other things use the same area on common laptop resolutions:

- the co-op window (760 x 600, centred);
- the first notification panels (x from width - 584, y from 15 % of the height).

The overlay is drawn after the game UI, so its text lies on top of them.

### Where
- Status block, drawn whatever `labels` says: [coop.cpp#L1415-L1444](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1415-L1444)
- Co-op window position: [coop_ui.cpp#L397](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L397), "Space >" button [coop_ui.cpp#L483](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L483), session line [coop_ui.cpp#L442](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L442)
- Notification stack position: [notify.cpp#L22](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/notify.cpp#L22), [notify.cpp#L87](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/notify.cpp#L87)

### How to reproduce
1. Set the game to 1280 x 720 and have three players in a session in Space.
2. Press F9: the top right of the window (Space > button, session line) has the overlay's white player list written over it.
3. Let a notification arrive: the same text runs over it.

### Expected
The overlay status block never covers the mod's own window or notifications.

## Root cause
**What happens**

At 1280 x 720 the window is at x 260..1020 and y 60..660, and the status block starts at x 800:
- The player lines at y 112..192 cross the "Space >" button (screen 840..980, 126..158) and the session line (300..980, 164..190).
- The first notification (y 108..166) is drawn under the same text.

At 1366 x 768 the block (x 886) crosses "Space >" (883..1023, 150..182) once 3-4 players are listed.

At 1920 x 1080 the window is clear, but the block's last player lines (y about 152..192) still overlap the first notification at y 162.

### Confidence
CONFIRMED by reading (layout arithmetic from the constants), low severity. A second reviewer verified it.

## Fix
Fixed on fix/w3-sys (5f0c909). The overlay's status block is not drawn while the co-op window is
open (or a menu in a game: `g_menu_up` is not refreshed in the main menu, so there only the window
counts). The notification panels start below the block's last line (`notify::set_top_min`). The
block's rect is in `coop.status` ("status block (x1 y1 x2 y2)"). `coop.corefix status_block 0`.

## Verification
Test: `tools/tests/coop_window_layout.py` (suite `window_layout`), one game hosting in the main menu at
1280x720 (set through `SporeCoop::Renderer_SetVideoMode`).
* FAIL with `--old` (`coop.corefix status_block 0`, manual runs on lane 3, 2026-10-28): the first
  panel at y 108 under the block (800 70 998 112), the block drawn while the window is open.
* PASS with the fix: 2 manual runs (block hidden with the window open, the panel at y 120) and lab
  run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #134.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (5f0c909); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
