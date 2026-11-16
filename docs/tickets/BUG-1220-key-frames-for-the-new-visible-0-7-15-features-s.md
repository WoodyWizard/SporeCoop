---
id: BUG-1220
title: Key frames for the new visible 0.7.15 features: space_eco, space_eco_live, space_session, space_session_npc, tribe_outfit close-up
status: open
kind: test
stage: All
reported: 2026-11-16
source: plan
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
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
