---
id: BUG-9321
title: All stages: a soft gradient ring in the player's colour under every unit a partner controls
status: open
kind: feature
stage: all
reported: 2026-11-21
source: owner request 2026-11-21
role: all
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: feat/l47-rings
commits: d19ed484
tests: tools/qa_win (manual, two TITAN QA games, 2026-11-21)
signature:
docs: docs/re/graphics.md#graphics-overlay-depth, docs/06-testing.md#testing-party-rings
related: BUG-9302, BUG-9319, BUG-9324, BUG-9325, BUG-9327
---

## Report
The owner, 2026-11-21: "The ring underneath, can it be a gradient soft ring? I'd love to have this ring be applied to
all player controlled units in all stages, like ships in space, pack in creature stage, civ and tribe." Reference picture:
a soft disc, yellow at the rim fading to white towards the middle. Later the same day: "Ensure the ring respects the
creature. So if the ring is behind them, it doesn't display that part of it... Currently the ring appears OVER the
creature." and, on the Cell test: "I'm seeing the ring flicker in front and behind the cell here." Decision 7X A: a
partner's units only (own units behind a switch, off).

## Root cause
Feature (BUG-9302's ring existed in Tribe only, as hard flat bands drawn over everything). The flicker in Cell: in about
every other Cell frame the last pass before Present leaves no depth-stencil bound (a 2D pass), so the ring was drawn
flat over the cell in those frames and depth-tested in the others; and a ring 0.12 m over the Cell plane cut through
the cell's body (docs/re/graphics.md#graphics-overlay-depth).

## Fix
* partner_marker::rings: one drawing function. Soft look (all.soft_ring): concentric bands with per-vertex alpha
  (Gouraud): almost clear and white-ish in the middle, the player's colour strongest near 0.9 of the radius, a soft fade
  just outside, a very faint dark halo; 48 points round; the ring's size on the screen kept between a minimum and (Civ,
  Space) a maximum. Off: the hard two-band ring of 7K A.
* Depth (all.ring_depth): every vertex projected with world_to_screen, its depth from the scene's view and projection,
  tested (not written) against the scene's depth-stencil; the last scene state is kept for frames that unbind it; a
  cell's and a ship's ring sits 0.6 x its radius below the unit. Off: drawn over everything.
* Who (all.partner_rings; off: Tribe only): Cell the partner's cell puppet; Creature its creature puppet and its pack
  (host: guest_pack::for_each_member; guest: the pack proxies, npc::for_each_pack_proxy, given the nearest partner
  creature's player); Tribe every partner villager (tribe::for_each_tracked); Civ the partner's vehicles
  (civ::for_each_owned, not cities); Space its ship (the bounding radius, cSpatialObject vt 0x70). all.own_rings (off):
  this player's own units too. Rings beyond the planet's horizon and rings centred on a HUD window are left out.
* `coop.rings` (tests): the list, `look x y z`, `depth`, `sink`.

## Verification
- Live QA 2026-11-21 (TITAN QA copy, two games at 10 FPS, 1024 x 768; screenshots in the worktree's out/qa-shots, not
  in git):
  - Civ (host DuoQACivHost / Abber, guest DuoQACivGuest): the guest sees a soft yellow ring under the host's vehicle;
    `coop.fix all.soft_ring off` gives the old hard ring at the same view (civ_guest_soft_ring_host_vehicle.png,
    civ_guest_old_hard_ring_host_vehicle.png, civ_guest_soft_vs_old_ring_crop.png); all.own_rings on: a blue ring under
    the guest's own vehicle (civ_guest_own_rings_on_own_vehicle.png). Taken before the depth path.
  - Tribe (host DuoQAJiffFresh, guest DuoQAStageGuest): the guest sees the ring under the host's chief
    (tribe_guest_soft_ring_host_chief.png); in the late joiner's creature editor no ring (partner_rings=0,
    editor_guest_no_ring_in_creature_editor.png). Taken before the depth path.
  - Creature (DuoQACreatureHost / Samuth, DuoQACreatureGuest): FAIL / PASS of the depth requirement with
    `coop.fix all.ring_depth off` / on in the same scene: off, the ring is drawn over the host's creature's legs and
    over a creature in front; on, they cover it (creature_guest_ring_depth_on_vs_off_crop.png and the full shots
    creature_guest_view_ring_depth_ON/OFF_host_creature.png; the host's view: creature_host_view_ring_depth_*). After
    `coop.npc recruit` on the host the guest shows a second ring under the host's pack member, its back hidden by the
    member in four frames in a row (creature_guest_rings_host_creature_and_pack_member.png,
    creature_guest_pack_ring_depth_4_frames.png).
  - Cell (DuoQACreatureHost / Diplomavil): FAIL (the flicker the owner saw: the ring 0.12 m over the plane and flat in
    2D frames, six frames: cell_guest_ring_before_fix_6_frames_flicker.png); PASS (kept scene depth, ring below the
    cell: the cell drawn over its ring in six frames in a row, cell_guest_ring_under_host_cell_6_frames_no_flicker.png).
  - Not run: Space (no two-player Space save started), Civ and Tribe with the depth path, the Civ city planner.
- Final check 2026-11-21 (fix/l47-final-rings, TITAN QA copy; docs/06-testing.md#testing-party-rings-final):
  - Space (DuoQAJiffSpace + DuoQAStageGuest): the partner's ship ring seen in the system view and on a planet, far
    and close; the ship in front hides it (space_host_view_depth_ON_crop.png vs _OFF_crop.png). Found and fixed: the ring
    over the ship in the death / new-ship screens (BUG-9324) and the ring through the hull close up (BUG-9325).
  - Civ and Tribe: the depth path did not work there (the game clears the depth before its UI): BUG-9327, fixed (the
    rings drawn before that clear), FAIL / PASS on a Civ vehicle and a Tribe chief, four frames each with no flicker.
    The Civ city planner covers the rings (none over it); the Tribe planner / hut screen was not reached (the save's hut
    is wrecked and opened nothing).
  - Cell (DuoQACreatureHost / Diplomavil): twelve frames over two camera positions, the host's cell turning, always
    drawn over its ring (cell_guest_spectate_ring_under_host_cell_6_frames_turning.png,
    cell_guest_ring_under_host_cell_6_frames_second_view.png). The Cell camera stayed 20 m over the plane: the wheel and
    the zoom buttons gave no second zoom level on this save.
  - Device: minimize / restore, borderless on / off and a window resize with rings on screen: no device reset happens
    windowed, rings and the kept depth surface fine, no warning in the logs. A fullscreen alt-tab was not tried.

## History
- 2026-11-21: opened.
- 2026-11-21: built on feat/l47-rings (d19ed484), live QA on TITAN (see Verification).
- 2026-11-21: final check on fix/l47-final-rings: BUG-9324, BUG-9325, BUG-9327 found and fixed (30549e5b).
