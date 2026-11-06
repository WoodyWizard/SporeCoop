---
id: BUG-0599
title: The mod cannot confirm the 'game saved' box while the co-op window is open: its click on OK hits the window, the host stays paused
status: fixed
kind: bug
stage: Creature
reported: 2026-11-05
source: fix/w44-coop-window, coop_chat_pause.py experiment 2026-11-05
role: host
versions: 0.7.7
fixed_in: 0.7.8
branch: fix/w44-coop-window
commits: 9cdebdf0
tests: chat_pause (coop_chat_pause.py)
signature:
docs: docs/08-ui.md
related: BUG-0551, BUG-0482
---

## Report
Found while writing coop_chat_pause.py (BUG-0551): with the co-op window open (F9), `coop.save confirm` released the
held "game saved" box and the mod clicked its OK every second, but the box stayed (modal=1) until the window was
closed; then it was confirmed at once. Reproduce: two instances, creature, `coop.save hold`, `coop.uistate 1`,
`coop.save confirm`: the box stays.

## Root cause
Observed (runtime 2026-11-05, lane 6): the co-op window is a direct child of the main window with kFlagAlwaysInFront
and takes the mouse at the box's OK although the box is drawn above it (only its check mark shows over the window).
The mod's click (and a player's) on the check mark reaches the co-op window; the game stays paused (busy=paused)
while the window is open after a share save (a guest joining or coming back).

## Fix
`world.box_aside`: before its click on OK, world.cpp's box_task asks `coop_ui::step_aside(x, y, 0.6)`: when the
window covers the point it is hidden for 0.6 s (the mod's typing into a line goes on: root_up()), the click reaches
the box, the window shows again. Off: the click hits the window (0.7.7).

## Verification
Lab: chat_pause FAIL with `SPORE_FIX_OFF=world.box_aside` out/lab/20261105-100218 ("the mod confirmed the box"
fails in both parts), PASS out/lab/20261105-100001.

## History
- 2026-11-05: opened.
- 2026-11-05: opened and fixed on fix/w44-coop-window (world.box_aside).
- 2026-11-05: released in 0.7.8.
