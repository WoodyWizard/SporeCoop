---
id: BUG-9325
title: Space: close up, the party ring cuts through the partner's ship
status: open
kind: bug
stage: Space
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in: unreleased
branch: fix/l47-final-rings
commits: 30549e5b, 2a83437c
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

- REOPENED (2026-11-21, the owner on the system-view pair: "Your before and after of the ship one shows the same
  overlay issue"): right, the system-view crop shows the ring over the hull in both halves; it was taken with the old
  overlay path, which has no usable depth in Space, so the "on" half proves nothing for the system view. The close-up
  planet pair stands only for the placement under the hull; the hull covering its ring on a planet is proven with
  BUG-9327's draw before the depth clear (space_final_build_off_then_4_on.png).
- Prepared, not run yet (TITAN busy): `coop.rings trace 2` in the system and galaxy views to see whether the rings are
  drawn at the depth clear there (the counter suggests so), then a system-view crop with the hull covering the back of
  the ring, `coop.fix all.ring_before_ui off` for comparison. Fallback coded: `space.ring_cut` (a frame drawn at the
  overlay in Space cuts the part of the ring behind the ship's sphere by geometry).
- System view, live 2026-11-21 (TITAN QA copy, same pair, the host's view of the guest's ship at the default system
  zoom; frame trace there: the world on the back buffer, then the `Clear(Z | STENCIL)` before the UI, as in Civ, so the
  rings are drawn at that clear, BUG-9327):
  - With the ring raised into the hull for a clear picture (`coop.rings sink 0.6 0.3`): FAIL with
    `coop.fix all.ring_before_ui off` and `coop.fix space.ring_cut off` (the ring over the hull), the fallback alone
    (before_ui off, ring_cut on: the back of the ring cut behind the hull), PASS with the release settings (the hull in
    front of the back of the ring, the front arc over the hull's lower part): bug9325_system_view_ring_raised_FAIL_off_cut_on.png
    (left to right: off, cut, on); four frames on, no flicker: bug9325_system_view_ring_raised_PASS_on_4_frames.png.
  - The default sink (1.0): the ring under the ship, the hull's bottom covering the back arc where they meet:
    bug9325_system_view_default_FAIL_off_cut_on.png, bug9325_system_view_default_PASS_on_3_frames.png.
  - Zoomed in within the system the camera drops to about the ships' height and sees the ring edge-on (a thin line
    under the ship; nothing to cover).
  - Galaxy view: not reached. 40 wheel notches out stopped at the system's widest view (this save still has the first
    Space mission, "Fly to planet Athalky"; inferred: the game keeps the camera in the home system until then).
- no-fail-run: not a lab run (TITAN QA games by hand).

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-final-rings (30549e5b), FAIL / PASS live.

- 2026-11-21: CORRECTION (owner review): the system-view crop (bug9325_system_view_FAIL_off_vs_PASS_on_crop.png) shows the ring over the hull in both halves: the system view is not fixed, only Space on a planet. Reopened for the system view on fix/l47-final-rings.
- 2026-11-21: reopened for the system view (the owner); space.ring_cut fallback coded, not run live yet.
- 2026-11-21: system view checked live (2a83437c build): FAIL off / PASS on; fixed again.
