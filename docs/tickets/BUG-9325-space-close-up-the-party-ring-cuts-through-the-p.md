---
id: BUG-9325
title: Space: close up, the party ring cuts through the partner's ship
status: fixed
kind: bug
stage: Space
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in: unreleased
branch: fix/l47-final-rings
commits: 30549e5b
tests: tools/qa_win (manual, two TITAN QA games, 2026-11-21)
signature:
docs: docs/re/graphics.md#graphics-ui-depth-clear, docs/06-testing.md#testing-party-rings-final
related: BUG-9321
---

## Report
Live QA 2026-11-21 (TITAN QA copy, two games in Space, both ships hovering over the host's homeworld, the camera zoomed
in): the partner's ring went round the lower part of its ship's hull like a hoop instead of lying under it
(bug9325_close_FAIL_off_3_frames.png in the worktree's out/qa-shots).

## Root cause
Observed: the ring sat 0.6 x its radius under the ship's centre (BUG-9321); the ship's hull reaches lower than that
(the ring is 1.1 x the bounding radius, so 0.6 x the ring is about 0.66 x the bounding radius). Far away, where min_px
grows the ring on the screen, the sink grew with it.

## Fix
coop.cpp space_sink: a ship's ring sinks 1.0 x its radius (under the whole hull), in the hull's own metres
(partner_marker Ring::lift_fixed: not grown with the ring's on-screen minimum). Switch `space.ring_below_hull` (off: 0.6 x
the grown ring, 67-dev). `coop.rings sink <cell> <space>` still sets a value for tests (negative: back to the default).

## Verification
- Live 2026-11-21 (TITAN QA copy, host DuoQAJiffSpace, guest DuoQAStageGuest, host's view of the guest's ship on the
  planet, close zoom): FAIL off (the ring round the hull's lower part, three frames: bug9325_close_FAIL_off_3_frames.png),
  PASS on (the ring under the hull, the hull hides its back, three frames: bug9325_close_PASS_on_3_frames.png; four more:
  space_host_view_close_zoom_sink10_4_frames.png). System view: off / on side by side
  (bug9325_system_view_FAIL_off_vs_PASS_on_crop.png: both attached, on lies just under the ship).
- no-fail-run: not a lab run (TITAN QA games by hand).

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-final-rings (30549e5b), FAIL / PASS live.
