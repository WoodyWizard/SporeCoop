---
id: BUG-0610
title: Tribe: a host member walking at 9 m/s is put 21 m away by the host's own game for one frame step (the guest follows 0.25 s later)
status: open
kind: bug
stage: Tribe
reported: 2026-11-06
source: fix/w45-remote-bodies body_trail_tribe_player
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0440, BUG-0425
---

## Report
body_trail_tribe_player (lab 20261106-014554, track out/lane9/track/body_trail_tribe_20261106-014937.json, not in
git) and body_trail_tribe_player of 20261106-014056: the host's member t2, walking at 6-9 m/s on a `coop.tribe order
<id> 4` walk, moved 21 m between two host frames (204-213 m/s for 3 frames of `coop.bodyrec`), then walked on at
6-9 m/s. The guest's mirror stayed on the old path 0.25 s (20.3-21.3 m off) and was then put there (one 21.4 m step:
drive::step's snap). Both runs: t2, 20-22 m.

## Root cause
Observed on the host (coop.bodyrec rows = the game's position at the start of the mod's frame): the host's own game
put its member 21 m away. Not known: what did it (the test's DoAction 4 order record, the citizen's path being
replaced, a regroup of the tribe). Inferred: the same kind of host jump the BUG-0440 audit line was made to find. The
guest following it 0.25 s later is right (the next state).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
