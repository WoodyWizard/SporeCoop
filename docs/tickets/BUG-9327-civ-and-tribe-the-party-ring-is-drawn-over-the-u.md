---
id: BUG-9327
title: Civ and Tribe: the party ring is drawn over the unit (the scene depth is cleared before the overlay)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-21
source: owner 2026-11-21
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
The owner, watching the Civ QA test 2026-11-21: "I checked the player ring during the test and saw it was still above
the unit." Their screenshot: the soft yellow ring drawn over the host's small vehicle, the vehicle seen through it. They
pointed at the game's own selection glow under a selected vehicle as a possible model.

## Root cause
Observed (`coop.rings trace`, `coop.rings ztest`): Civ and Tribe draw the world into an offscreen render target, put it
on the back buffer with one draw, then clear the screen-size depth-stencil (Z and stencil, flags 6) before the game's
UI. At the overlay pass (in Present) the depth buffer is empty: a ring under a vehicle passed LESSEQUAL everywhere and
failed GREATER everywhere, whatever near plane its depth was computed with. The ring's max_px bound also shrank it under
the vehicle close up. Space (on a planet) has the same clear; Cell has none (BUG-9321's kept-depth path).
CORRECTION of docs/re/graphics.md#graphics-overlay-depth ("the projection path is the same" for Civ and Tribe).

## Fix
partner_marker: a hook on the device's Clear; at the clear above (count 0, Z without TARGET, render target = the back
buffer, depth-stencil and viewport its size) the previous overlay pass's rings are drawn first, depth-tested against
the scene's depth that is still there; the game's UI then comes over them. The overlay pass draws them only in a frame
without that clear. With the ring in the world, max_px no longer shrinks it (a far-zoom minimum only). Switch
`all.ring_before_ui` (off: the overlay as before, 67-dev). Counters `coop.rings`: before the UI frames=.
The game's selection marker was not analysed (not needed); noted as another way in the docs.

## Verification
- Live 2026-11-21 (TITAN QA copy, 1024 x 768), Civ (host DuoQACivHost / Abber, guest DuoQACivGuest), the guest's camera
  close on the host's vehicle: FAIL with `coop.fix all.ring_before_ui off` (a small ring over the vehicle), PASS on (the
  full ring under it, the vehicle and its wheels in front, four frames in a row):
  bug9327_civ_vehicle_FAIL_off_vs_PASS_on_crop.png, bug9327_civ_vehicle_PASS_on_4_frames.png,
  civ_guest_host_vehicle_close_FAIL_before_ui_off.png, civ_guest_host_vehicle_close_PASS_before_ui_on.png. Before the fix
  the same FAIL with a GREATER test and five near planes (zt.png, zn.png).
- Tribe (host DuoQAJiffFresh, guest DuoQAStageGuest), the host's chief: FAIL off (the ring over its leg), PASS on (legs
  and feet in front, four frames): bug9327_tribe_chief_FAIL_off_vs_PASS_on_crop.png,
  bug9327_tribe_chief_PASS_on_4_frames.png; again on the final binary (final_build_tribe_chief_3_frames.png).
- Space on a planet with the fix: the ship in front of its ring, four frames
  (space_final_build_off_then_4_on.png). Civ city planner with all.own_rings on: no ring over the planner
  (civ_guest_city_planner_no_ring_PASS.png).
- no-fail-run: not a lab run (TITAN QA games by hand).

## History
- 2026-11-21: opened (owner report during the live Civ test).
- 2026-11-21: fixed on fix/l47-final-rings (30549e5b), FAIL / PASS live in Civ and Tribe.
