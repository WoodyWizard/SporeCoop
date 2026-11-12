---
id: BUG-0806
title: The game saved box with its OK button outside the window (after a video reset) is clicked forever and never confirmed
status: fixed
kind: bug
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-9057 (Cell recurrence at 800x600, commit b3d24b5, lineages30-dev)
role: host
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-saves
commits: cf952855
tests: saved_box_offscreen
signature: "world: the game saved box is up" without the box closing; joins wait for the save confirmation
docs: docs/re/creature.md#creature-saved-box-kinds, docs/06-testing.md#testing-saves-w48
related: BUG-0804, BUG-0599
---

## Report
Found by the fork on a two-PC Cell session (2026-11-08): after a world transfer the host's native "game saved"
box stayed open; it reached past the right edge of the 800x600 window. The next guest got no world offer for
78 s, the host log said it waited for the previous save confirmation. Enter closed the box. (The PR #196 review
tickets the main BUG-9057 issue; this is the later off-screen variant, check for overlap.)

## Root cause
Observed (lab): after the game's video mode goes to 800x600 (Renderer_SetVideoMode, as its options do) the UI keeps
its 1440x1080 layout; the saved box's OK is at (959..1001, 589..631), outside the 800x600 client area; the mod's click
there hits nothing and the click command still succeeds, so Enter never came: the box stayed up (the game paused)
for the whole watch. A box only moved past the edge with `coop.ui area` is still hit by such a click under Wine (no
reproduction that way). Why the fork's window was 800x600 is not known; a lowered resolution reproduces it.

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

## Fix
world.cpp box_task: when the OK's centre is outside GetClientRect of the game window, the box (only a real saved box,
BUG-0846) is confirmed with a posted Enter (it goes to the topmost modal box), logged once. Switch
`coop.fix world.saved_box_offscreen off` = the click.

## Verification
`saved_box_offscreen` (coop_saved_box.py --case video): 800x600, coop.save, the box must close within 10 s.
* FAIL: out/lab/20261112-014918 (`SPORE_FIX_OFF=world.saved_box_offscreen`, lane 3): the box stayed up 10 s.
* PASS: out/lab/20261112-014833 and the final runs: closed by Enter.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit, not reproduced by us).
- 2026-11-12: reproduced with a video mode change to 800x600, fixed on fix/w48-saves (world.saved_box_offscreen), FAIL 20261112-014918, PASS 20261112-014833.
