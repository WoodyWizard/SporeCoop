---
id: BUG-0903
title: Visual tests: key frames with a review by eye, golden images of the co-op window, the same view on both games
status: fixed
kind: tooling
stage: All
reported: 2026-11-10
source: owner request 2026-11-10
role: 
versions: 
fixed_in: 0.7.12
branch: feat/w47-visual
commits: 61565949, 6cda5847, 14900753
tests: tools/tests/visual_selftest.py (make test), ui_golden, same_view, same_view_tribe; key frames in the play_*, campaign and feature tests
signature:
docs: docs/06-testing.md#testing-key-frames, docs/06-testing.md#testing-ui-golden, docs/06-testing.md#testing-same-view, docs/re/graphics.md#graphics-fixed-camera
related: BUG-0906, BUG-0907
---

## Report
The owner (2026-11-10): the lab reads the game's state but hardly looks at the screens, so bugs that are only visible
(a wrong model or paint, a body left where an egg was, a frozen pose, a shifted window, a floating object) pass.

## Root cause
Not a bug: almost every check reads console state; only a few tests look at pixels.

## Fix
1. Key frames (`twoinst.keyframe`, run folder `frames/<test>/`, manifest) in the play_* bots, the campaign's phase ends
   and the feature tests where looks matter; `tools/frames_review.py <run>` makes contact sheets (host and guest side by
   side, labelled, at most 80) with index.html and sheets.md; the review checklist and the release rule's item 5.
2. `ui_golden`: the co-op window's 12 steps by real clicks against golden images (tools/tests/golden/, masks for the
   texts that vary), `--update`; test switch `coop.uistate nudge|hide`; `coop.uistate` prints the window rect and the
   areas of the varying texts.
3. `coop.view` (a fixed camera, hook on cCameraManager::Update made at the first use), `coop.census creatures`,
   `same_view` / `same_view_tribe`: the same view on both games as sameview key frames, and the creatures in each picture
   (census projected with the game's own camera) compared.

## Verification
* Key frames: lab 20261110-194330 (creature_brain_hp, 2 frames, sheets built); final pass 20261110-201619 (14 sheets).
* ui_golden PASS: 20261110-201216 (0 differing pixels in 12 steps), 20261110-201619 (lane 7: references made on lane 3).
  FAIL with the twins: UI_GOLDEN_TWIN="nudge gohost 4 0" 20261110-201357 (choose 0.17 %, the Host button's region);
  UI_GOLDEN_TWIN="hide ready" 20261110-200801 (lobby_menu, ingame_host, ingame_guest 1.4 %).
* same_view PASS: 20261110-201619 (Creature 7/7, 8/8, 7/7, 7/7; Tribe 3/3 x4). FAIL with SAMEVIEW_SHIFT=10 (the guest's
  camera 10 m aside): by hand on lane 6 (out/lane6/frames/manual, 0 matched in every view).
* Findings of the review of 20261110-201619: BUG-0906 (the chief's staff missing on the guest), BUG-0907 (a floating
  name tag on the guest in Tribe).

## History
- 2026-11-10: opened.
- 2026-11-11: released in 0.7.12.
