---
id: BUG-1026
title: SporeKit: the overlay state block is kept across a new device and one CreateStateBlock failure turns the overlay shapes off for the run
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-5
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-kit
commits: 438e1cdb
tests: kit_overlay_reset
signature:
docs: docs/02-modkit.md#modkit-report-safety
related:
---

## Report
Audit 0.7.14, KIT-5. Not seen at runtime, found by code reading. (a) The overlay's state block (g_tri_sb) made on one
device survives h_CreateDevice: on a second device the old block's Capture / Apply run on a freed or foreign device.
(b) g_tri_failed is never reset: one failed CreateStateBlock (for example at a moment the device is lost) turns the
overlay shapes (the partner arrows of BUG-0764) off until the game restarts, also after a Reset. A player cannot
reach (a): Spore creates a second device only on an adapter switch, which crashes the game by itself
(docs/re/graphics.md section 3). (b) is a lost arrow overlay at worst.

## Root cause
Observed in code (main efd2e0ba):
* sporekit/d3d9hook.cpp:662-685 (h_CreateDevice) releases the font (font_release, g_font_failed = false) but not
  g_tri_sb (tri_release, 239-245) and does not clear g_tri_failed.
* d3d9hook.cpp:619-626 (h_Reset) calls tri_release() but leaves g_tri_failed set; overlay_tris (247-257) returns
  early for good once it is true.

Runtime / fix/w49-kit:
Confirmed at runtime for (b) (lane 7, `SPORE_FIX_OFF=kit.trireset`, lab 20261113-010950): after one made-to-fail
CreateStateBlock (`kit.test.tri fail`) the overlay shapes stayed off after two device Resets (Alt+Enter twice):
failed=1, no magenta test triangle in the screenshot. (a) not reachable (a second device comes only with an adapter
switch, which crashes the game by itself, docs/re/graphics.md section 3); fixed by code (the same release on a new
device).

## Fix idea
h_CreateDevice: tri_release() and g_tri_failed = false next to font_release(); h_Reset: g_tri_failed = false after a
successful Reset. No switch needed (cleanup only); if wanted `kit.fix trireset`.

## How to test
(b): a test-only `kit.test.trifail` that makes the next CreateStateBlock fail, then `frame` for some frames, a device
Reset (Alt+Enter through `key`), and the arrows test (coop_find_* / find_layout key frames) must show arrows again.
Main: no arrows after the Reset: FAIL. (a): no test (needs an adapter switch that crashes the game anyway);
`no-fail-run: not reachable` for that part.

## Fix
sporekit/d3d9hook.cpp, `kit.fix trireset`: h_CreateDevice releases the state block and clears g_tri_failed next to the
font; h_Reset clears g_tri_failed after a successful Reset (the shapes are tried again). Test command
`kit.test.tri [frames|fail]` (a magenta triangle at the top left / the next CreateStateBlock fails).

## Verification
Test kit_overlay_reset (two games, the host: triangle on, made to fail, Alt+Enter twice, triangle again; checks the
screenshot's pixels):
* FAIL `SPORE_FIX_OFF=kit.trireset`: out/lab/20261113-010950 (0% magenta after the Resets, failed=1).
* PASS: out/lab/20261113-010700 (100% magenta before, 0% failed, 100% after the Resets), out/lab/20261113-011847.
(a): no-fail-run: not reachable (only an adapter switch makes a second device, and it crashes the game itself).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit (FAIL/PASS twin).
