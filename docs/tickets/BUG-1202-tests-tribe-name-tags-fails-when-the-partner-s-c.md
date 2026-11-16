---
id: BUG-1202
title: Tests: tribe_name_tags fails when the partner's chief stands off the screen while another of its members is on it
status: fixed
kind: test
stage: Tribe
reported: 2026-11-16
source: final check 0.7.15 (lab 20261116-025154)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w51-t4
commits: 7a27fc15, 308317a8
tests: tribe_name_tags (coop_tribe_looks.py --part tags)
signature: guest: the partner's chief has its label (1 of the partner's members on the screen, labels ['member'])
docs: docs/06-testing.md#testing-final-triage-tribe-civ-0715
related: BUG-0907
---

## Report
Final check 0.7.15 (lab 20261116-025154, rel/0.7.15 11114769, lane 4, warm): `FAIL guest: the partner's chief has its
label (1 of the partner's members on the screen, labels ['member'])`. Ledger: 1 of 13 runs since 2026-11-10.

## Root cause
Observed (the run's key frame `frames/tribe_name_tags/01_tags_guest.jpg`): the guest's screen shows the host's plain
member with its label "Alice" and, at the left edge, the arrow "Alice (chief) 29 m": the host's chief stood 29 m away,
off the screen (the cameras aim at the middle of all players' members, and one of them had walked off). The test
required a chief label whenever any member of the partner was on the screen, so a chief off the screen failed it.
The mod drew what it should (a label over the member on the screen, an arrow for the chief off it). A test problem.

## Fix
coop_tribe_looks.py --part tags: the chief is the member the viewing game names (`coop.tribe` chiefs=), its position
the host's; the check wants a `chief` label within 60 px of that chief's projected tag point (stricter than before:
the old check accepted a chief label anywhere). A chief off the screen gets both cameras 24 m from it first, then
the cameras go back to the members for the rest of the test and the key frame. No mod change, no switch.

## Verification
* FAIL (the scene, the old check): lane 4, the test of 7a27fc15^ with the host's chief put 30 m away
  (`coop.tribe chief_away 30`, the knob TAGS_CHIEF_AWAY=30 added to a copy), run by hand: `FAIL guest: the partner's
  chief has its label (1 of the partner's members on the screen, labels ['member'])`, the final check's line
  (log kept as out/lab/manual-tags-old-chiefaway.log in the fix/w51-t4 worktree); the final check's own FAIL run
  20261116-025154:tribe_name_tags.
* PASS (the new check, the same scene): 20261116-092356 (TAGS_CHIEF_AWAY=30: `guest: the partner's chief #1 is off the
  screen ...: both cameras 24 m from the chief`, then `ok guest: the partner's chief has its label (chief #1, on the
  screen 1, ... chief labels within 60 px of it: 1)`).
* PASS without the knob: 20261116-091255 (tribe_name_tags#1 warm, #2 cold).

## History
- 2026-11-16: opened (fix/w51-t4, final-check triage 0.7.15); the test's check fixed, the scene reproduced with TAGS_CHIEF_AWAY.
