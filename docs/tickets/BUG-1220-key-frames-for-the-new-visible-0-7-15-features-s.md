---
id: BUG-1220
title: Key frames for the new visible 0.7.15 features: space_eco, space_eco_live, space_session, space_session_npc, tribe_outfit close-up
status: fixed
kind: test
stage: All
reported: 2026-11-16
source: plan
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-visual
commits: 67a42202, e41bb9fb, f77d99f7, 4a2d3e10
tests: space_eco, space_eco_live, space_session, space_session_npc, tribe_outfit, tribe_held_item, tribe_mirror_stale
signature:
docs: docs/06-testing.md#testing-visual-0715
related: BUG-1148, BUG-1149, BUG-1151, BUG-1156, BUG-0914, BUG-0936, BUG-1223
---

## Report
Key frame review of the 0.7.15 final check (lab 20261116-025154; owner rule: every new visible feature adds key frames):
space_eco, space_eco_live, space_session and space_session_npc passed with no key frame at all (planet ecosystems and
restored colonies were never looked at), and tribe_outfit's frame (S/112) is ~25 m away, members of ~20 px: the outfit
cannot be judged.

## Root cause
Missing test coverage (no `twoinst.keyframe` in coop_space_eco.py / coop_space_session.py; tribe_outfit's frame uses the
game's own camera).

## Fix
`tools/tests/keyviews.py` (look_at / look_both / view_off / nearest_row / burst, docs/06-testing.md#testing-visual-0715)
and key frames:
* space_eco / space_eco_live: the planet's story caption closed (`no_movie`), then both cameras at the same place 30 m
  from and 18 m above the host's nearest creature of a species the guest placed (`eco_landed` / `eco_live`).
* space_session: both games land on the restored colony (a caption and a badge's Reward panel closed first), the
  game's own camera over the ship (`colony`); space_session_npc: the colony is at the NPC's star, the games' own view
  after the restore (`npc_colony`).
* tribe_outfit: `outfit_close_a` / `outfit_close_b`: the same view on both games 10 m from the host's chief, two
  opposite sides (the chief faces one of them).
* (item 5 of the review) tribe_held_item / tribe_mirror_stale: three chief frames 1.5 s apart with both games'
  `coop.tribe anims` row of the chief in the manifest (keyviews.burst).
No game code changed.

## Verification
no-fail-run: test coverage only (key frames; no game code changed, nothing to switch off). The tests PASS with their new
frames, and the frames were looked at:
* space_eco / space_eco_live: 20261116-101029, 20261116-102211 (`eco_landed`: the host's creature of a placed species at
  the centre, the guest's game at the same place without it; the guest's placed plant, a big tree, stands only in the
  guest's game); `eco_live` 20261116-101029 (the host's herds of the placed species, the guest's own animals elsewhere).
* space_session: 20261116-102211, 20261117-120207 (#1, #2): the host landed on the restored colony 01460035 (its
  buildings on the planet); the guest's ship stays in its system view (`coop.targets` lists the planet neither as C nor
  P there: not landed; the colony is seen as a planet in orbit). The guest's frame of 20261116-094321 / -095532 showed
  the badge Reward panel over the view (opened as BUG-1223), closed since 20261116-101029.
* space_session_npc: 20261116-101029 (the games' own views after the restore, no panel left).
* tribe_outfit: 20261116-101029 `outfit_close_a` / `outfit_close_b` (the chief from behind and from the front: the same
  outfit pieces on both games; the front pair also shows a pose difference, item 5).
* tribe_held_item / tribe_mirror_stale burst frames: 20261116-102211 and the repeats of 20261117-120207 (see History).

## History
- 2026-11-16: opened.
- 2026-11-16 (fix/w51-visual): key frames added and looked at; fixed (test coverage).
- 2026-11-17 (fix/w51-visual): item 5 of the review (the chief's pose host vs guest, BUG-0914 / BUG-0936 class): 18
  chief frames in 6 runs (tribe_held_item 20261116-102211, 20261117-120207 #1 #2; tribe_mirror_stale 20261116-102211,
  20261117-120207 #1 #2), each with both games' `coop.tribe anims` row: the same animation at the same track time (within
  0.1 s) in 16; the 2 others right after the guest's rejoin (the host's chief already walking 04866DAB, the copy still in
  the gesture 04C6CB02 for ~1 s, the same animation 1.7 s later). The relay's lag after a rejoin, not a lasting
  mismatch: no ticket. Within one animation and phase the staff's angle still differs a little (blend / IK of each
  game), as in S/101 and S/110.
