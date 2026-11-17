---
id: BUG-1230
title: Civ: a guest's copy of a city citizen that stops runs on up to 10-12 m past the host's citizen and back
status: open
kind: desync
stage: Civ
reported: 2026-11-17
source: lab (fix/w51-citanim, BUG-1229 measurement)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in:
branch:
commits:
tests: civ_citizen_trail (its json: the guest's z<id> rows while the host's citizen stands; not judged yet)
signature: a copy of a standing host citizen 3-12 m off it, moving at up to 8 m/s away and back
docs: docs/re/civ.md#civ-citizen-run
related: BUG-1227, BUG-1229, BUG-0476
---

## Report
Found while measuring BUG-1229 (civ_citizen_trail's recordings, `coop.bodyrec kinds z`: both games' citizens as drawn
at the same moment). While a host city citizen stands (under 0.5 m/s), its copy on the guest is over 3 m away from it in
5-24 % of the samples (max 9.9-12.2 m) in every one of 26 recordings, with civ.citizen_lead on and off (labs of
fix/w51-trail 20261117-133529 .. -141917, fix/w51-citanim 20261117-144832 .. -145826). Per copy (lab 20261117-144837,
z2): the host's citizen stops, the copy runs on at 8 m/s to 8-10 m past it, then back (1-2 s), at most every stop.
civ_citizen_trail's distance checks only take the samples where the host's citizen moves, so this is not judged.
Reproduce: the analysis in the ticket's history (samples where the host's citizen stands, the copy's distance).

## Root cause
Not analysed. Inferred: citizen_follow's last walk goal is 1.5 s (12 m) ahead of the running host citizen; when the
host's stops and the copy (glided to it) is within kCitizenWalk (1.5 m), citizen_follow returns without a new goal, so
the game's walk to the goal 12 m ahead goes on until the copy is 1.5 m off at the next snapshot, then a walk back. A
fix: a MoveTo to the stop point (or StopMovement) as soon as the host's citizen stands (drive::walk_end does that for
the other mirrored walkers).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-17: opened.
