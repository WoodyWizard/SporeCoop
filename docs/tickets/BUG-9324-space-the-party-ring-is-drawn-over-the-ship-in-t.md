---
id: BUG-9324
title: Space: the party ring is drawn over the ship in the death and new-ship screens
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
related: BUG-9321, BUG-0985
---

## Report
Live QA 2026-11-21 (TITAN QA copy, two games in Space on the host's homeworld): when a ship was destroyed, the death
screen ("... has taken so much damage that it has been destroyed") and then the "We've built you a new ship" caption
showed the partner's ship blurred with the party ring drawn sharp over its hull, on both screens
(space_host_death_screen_ring_over_ship_FAIL.png, space_guest_death_screen_ring_over_ship_FAIL.png,
space_host_newship_cinematic_ring_over_ship_FAIL.png in the worktree's out/qa-shots).

## Root cause
Observed: both screens are cinematics (`world::cinematic_playing()` true, `coop.space hp` movie=1) that draw the scene
blurred through another pass, so the depth at the ring's draw no longer holds the ships; `coop.fix all.ring_depth off`
looked the same. In Space the labels stay over cinematics on purpose (BUG-0985: a flight's arrows), so nothing hid the
ring.

## Fix
coop.cpp draw_partner_rings: in Space, while a cinematic plays, no party ring (the arrows and tags stay). Switch
`space.ring_cinematic` (off: drawn, 67-dev). Counter `coop.rings` space_cinematic=.

## Verification
- Live 2026-11-21 (TITAN QA copy, host DuoQAJiffSpace, guest DuoQAStageGuest, 1024 x 768), the guest's death screen with
  the host's ship in view (`coop.space hit 2000` twice): FAIL with `coop.fix space.ring_cinematic off` (the ring over
  the host's hull), PASS on (no ring): bug9324_death_screen_FAIL_off_vs_PASS_on_crop.png,
  space_guest_death_screen_FAIL_switch_off.png, space_guest_death_screen_PASS_switch_on.png. The host's new-ship caption
  with the guest's ship in view, on: no ring (space_host_newship_cinematic_no_ring_PASS.png).
- no-fail-run: not a lab run (TITAN QA games by hand; the lab has no Space death-screen test).

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-final-rings (30549e5b), FAIL / PASS live.
