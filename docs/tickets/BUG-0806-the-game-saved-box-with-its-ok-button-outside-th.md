---
id: BUG-0806
title: The game saved box with its OK button outside the window (after a video reset) is clicked forever and never confirmed
status: open
kind: bug
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-9057 (Cell recurrence at 800x600, commit b3d24b5, lineages30-dev)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature: "world: the game saved box is up" without the box closing; joins wait for the save confirmation
docs:
related: BUG-0804, BUG-0599
---

## Report
Found by the fork on a two-PC Cell session (2026-11-08): after a world transfer the host's native "game saved"
box stayed open; it reached past the right edge of the 800x600 window. The next guest got no world offer for
78 s, the host log said it waited for the previous save confirmation. Enter closed the box. (The PR #196 review
tickets the main BUG-9057 issue; this is the later off-screen variant, check for overlap.)

## Root cause
* Our code (inferred, confidence medium; not reproduced by us): `mods/coop/world.cpp:375-392` box_task clicks the
  centre of the OK button's screen rect. When that point is outside the client area the click hits nothing; the
  console `click` still returns 0, so the code returns before the Enter fallback (world.cpp:389) and repeats the
  useless click every second until the 30 s watch ends. The host's simulation stays paused while the box is up,
  and world shares that wait for the box are delayed.
* Why the game places the box off screen (video reset, small window) is not known (the fork did not find it).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages BUG-9057, commit b3d24b5); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* In box_task, before clicking, read `GetClientRect(game window)` and check that the OK button's centre
  (x, y) is inside `[0, right) x [0, bottom)`. Only then hover (`move x y`) and click; otherwise log
  "saved box OK is outside the client bounds; confirming with Enter" and fall through to the posted
  WM_KEYDOWN / WM_KEYUP VK_RETURN. Same switch as the box fix (`world.saved_box` in the fork).
* Caution for us: our comment at world.cpp:378 says an Enter once pressed a pause-menu button (chaos tests
  2026-10-23); keep the Enter only for the off-screen case and only while the box is the visible top window.

## How to test
Lab, one instance, a small window (800x600) and a forced video reset (or move the box with `coop.ui` so its OK is
off screen), `coop.save`. FAIL before: the box stays up for the whole 30 s watch, log shows repeated clicks.
PASS after: the box closes within ~1 s by Enter, log line about the off-screen OK, no pause-menu action.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit, not reproduced by us).
