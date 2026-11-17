---
id: BUG-1229
title: Civ: a guest's copy of a running city citizen often moves in its standing animation (0x02481DE5) instead of the run (0x02481DB0)
status: open
kind: desync
stage: Civ
reported: 2026-11-17
source: lab (fix/w51-trail, BUG-1227 measurement)
role: guest
versions: rel/0.7.15
fixed_in:
branch:
commits:
tests: civ_citizen_trail (coop_civ_citizen_trail.py: the `host citizens (moving, animation)` / `guest copies (moving, animation)` lines and the key frames run1 / run2)
signature: guest copies (moving, animation): [((True, '02481DE5'), N), ...]: a copy moves while it plays the standing animation
docs: docs/re/civ.md#civ-citizens-trail, docs/known-issues.md#issue-civ-citizens-slide
related: BUG-1227, BUG-0476
---

## Report
civ_citizen_trail (BUG-1227) records every Civ city citizen's current animation on both games (`coop.bodyrec start
kinds za`, rows a<id>, game::drawn_state). On the host a citizen that moves (over 2 m/s, they run at 8 m/s) plays
0x02481DB0 (4755 of 4755 moving samples, lab 20261117-141344), a standing one 0x02481DE5 or an action. The guest's copy
of a running host citizen plays 0x02481DB0 too in only 8-15 % of the samples with civ.citizen_lead off (labs
20261117-141104, -141349) and 26-91 % with it (-141344, -141059); the rest of the time it moves while it plays
0x02481DE5, the standing animation (3321-4550 moving samples with it, off). The key frames run1 / run2 of those runs
(the same camera on both games at a running host citizen) show the host's citizen in a run pose and the guest's copy
crouched as if it stood.

## Root cause
Not analysed. Observed: the old path (MoveTo alone) has it as much as the new one (the BUG-1227 glide is not the
cause: with it more copies run). Inferred: the copy's MoveTo walk is not the game's run of a citizen (the host's citizens
run by their own behaviour, which plays 0x02481DB0; game::gait_state's gait word is 0 for every citizen, so the walk gait
is not read that way), or the copy's own AI replaces the walk. Next: which call starts a citizen's run (the behaviour
that plays 0x02481DB0), give it to the copy instead of MoveTo, or play the host's animation on the copy.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-17: opened.
