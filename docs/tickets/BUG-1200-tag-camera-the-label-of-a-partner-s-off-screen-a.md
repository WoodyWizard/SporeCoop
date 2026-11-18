---
id: BUG-1200
title: tag_camera: the label of a partner's off-screen arrow (it reaches ~190 px in from the edge) was read as a name tag over empty ground
status: fixed
kind: test
stage: Creature
reported: 2026-11-16
source: lab
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-t5
commits: a7df99c3
tests: tag_camera, tag_height
signature:
docs: docs/06-testing.md#testing-final-0715-t5
related:
---

## Report
Final check of 0.7.15 (lab 20261116-025154, lane 2): `tag_camera` FAIL `3 shots off or over empty ground` and
`tag_height` FAIL `None m between the tag and the top of the body` (no tag found in the still shots); reruns on
fix/w51-t5 (lab 20261116-082817): tag_camera 2 of 2 FAIL (4 and 2 shots), tag_height 1 of 2 FAIL (0.74 m > 0.7).

## Root cause
Observed in the shots and logs: (1) every `empty` / `off` turn shot had its "tag" at x 141-142 or 1284 px: the label of
the partner's off-screen arrow ("Alice 8 m"), drawn up to ~190 px in from the edge (find.cpp kMargin 46, kSize 28,
label 12 px behind the head plus its text), while the test's measure leaves out only 130 px at the edges. (2) The
players stood 8.4-8.7 m apart in every 0.7.15 run, 4.6-6.1 m in the 0.7.12 / 0.7.13 runs (38 runs): BUG-0989 (0.7.14,
core.join_apart) puts the joining guest 5 m beside the host. With the host's walk the partner is farther, smaller
(77 px a metre), among the nest's red creatures, and off the screen in half the turns; in the final check's still shots
the game's own rollover of an NPC covered the tag. The tag drawing itself did not change in 0.7.15 (find.cpp: only the
C2 lifecycle hook).

## Fix
tools/tests/coop_tag_camera.py: (1) arrow_beside(): a label with the arrow's head (the tag colour within 90 px of the
edge, 60 rows around) is no tag; (2) the guest's creature is put back onto the host's spot before the host's walk
(`coop.npc gopos`), as before BUG-0989 (`SPORE_TAG_AS_JOINED=1`: as it joined). What is measured is unchanged.
switch: none (test only).

## Verification
FAIL: final check 20261116-025154 tag_camera and tag_height; reruns 20261116-082817 tag_camera#1, #2, tag_height#1 (rel/0.7.15 tests); v0.7.14.1 lab 20261116-084839 tag_camera (3 shots, arrow labels at x 141): old since 0.7.14 (BUG-0989).
PASS: lab 20261116-085451 tag_camera#1, #2 (12/12 and 7/12 over the body, 0 off, 0 empty; players 5.0 / 3.9 m apart), tag_height#1, #2 (0.44 / 0.46 m).

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t5 (final-check triage of 0.7.15, group T5).
- 2026-11-18: released in 0.7.15.
