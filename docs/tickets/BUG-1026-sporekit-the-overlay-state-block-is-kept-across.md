---
id: BUG-1026
title: SporeKit: the overlay state block is kept across a new device and one CreateStateBlock failure turns the overlay shapes off for the run
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-5
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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

## Fix idea
h_CreateDevice: tri_release() and g_tri_failed = false next to font_release(); h_Reset: g_tri_failed = false after a
successful Reset. No switch needed (cleanup only); if wanted `kit.fix trireset`.

## How to test
(b): a test-only `kit.test.trifail` that makes the next CreateStateBlock fail, then `frame` for some frames, a device
Reset (Alt+Enter through `key`), and the arrows test (coop_find_* / find_layout key frames) must show arrows again.
Main: no arrows after the Reset: FAIL. (a): no test (needs an adapter switch that crashes the game anyway);
`no-fail-run: not reachable` for that part.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
