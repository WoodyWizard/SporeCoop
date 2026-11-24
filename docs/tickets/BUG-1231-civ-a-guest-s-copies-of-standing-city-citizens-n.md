---
id: BUG-1231
title: Civ: a guest's copies of standing city citizens never play the host's citizens' actions (gestures), only the plain idle
status: fixed
kind: feature
stage: Civ
reported: 2026-11-17
source: lab (fix/w51-citanim, BUG-1229 measurement)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in: 0.7.99
branch: rel/0.7.99
commits: 67bf489e
tests: civ_citizen_trail, civ_citizen_gesture_off
signature: host standing citizens play 0x04A1DD75 / 0x04A3442D / 0x050B5AB8 / 0x050A3F95 ...; the guest's copies only 0x02481DE5
docs: docs/re/civ.md#civ-citizen-run
related: BUG-1229, BUG-1227, BUG-0476
switch: civ.citizen_gesture
---

## Report
Found while measuring BUG-1229. civ_citizen_trail records each citizen's current animation on both games (a<id> rows).
The host's standing city citizens play actions (0x04A1DD75, 0x04A3442D, 0x050B5AB8, 0x050B3FA5, 0x050A3F95, 0x049C8AE2,
...: 20-45 % of their standing samples, labs 20261117-144832, -144837, -145821, -145826 of fix/w51-citanim); the guest's
copies of them stand in the plain idle 0x02481DE5 (or, before civ.citizen_run, the run) for the whole recording: a guest
sees the city's citizens stand still where the host's cheer, work or talk. Cosmetic.

## Root cause
Not analysed. Observed: a copy's animation does not change for the whole 60 s while the mod walks it (BUG-1229): the
guest's own behaviour of its citizens plays nothing on them there. The citizen kind's snapshot has no animation (20 B per
citizen). A fix needs the host's action per citizen (a new field, a wire change for 0.7.15 guests only) or the Tribe
members' animation relay (docs/re/tribe.md) for city citizens.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
PASS: out/lab/20261124-152417 (civ_citizen_trail, 0 faults).
FAIL: out/lab/20261124-161419 (civ_citizen_gesture_off: 0% of 3377 gesture samples match).

## History
- 2026-11-17: opened.
- 2026-11-17: kind feature (orchestrator): relaying the citizens' actions is a new relay (a message or an animation relay as in Tribe), not a fix of an existing path; backlog, not part of 0.7.15.
- 2026-11-24: released in 0.7.99.
