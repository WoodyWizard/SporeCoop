---
id: BUG-0993
title: Tribe: tribe_hold_guest: a mirror's own hold lasted 1.05 s at 26 fps (BUG-0962's cut, 0.25 s tick, over the 1.0 s rule)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-12
source: fix/w48-ftribe lab 20261112-222723 #3
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-mirror
commits: 55e9d13b
tests: tribe_hold_cut, tribe_hold_guest
signature:
docs: docs/re/tribe.md#tribe-mirror-pose-phase, docs/06-testing.md#testing-t2-mirror
related:
switch: tribe.hold_cut_frame
---

## Report
fix/w48-ftribe, out/lab/20261112-222723 #3 (two lanes running, the guest at 26 fps with hitches up to 286 ms):
"FAIL every hold of the mirror's own ends within 1.0 s ([(63.9, 0.61, ...), (72.1, 1.05, '04C6CB02'), (84.7, 0.85, ...)])".
The other 8 runs of the branch's final code passed it. Seen before: fix/w48-last 20261112-163629 #2 (0.8 s against an
older 0.8 s rule). BUG-0962's cut of a hold of the mirror's own comes kHoldUnsent 0.35 s after held_items_tick (every
0.25 s) first saw it: up to 0.85 s by design, plus a frame or two at low fps. Not part of BUG-0992 (the cut path is
untouched; a Q / W hold always overlaps a host hold and is not "of its own").
Next: run the cut check every frame (or measure from the modifier's start) so it ends within 0.35-0.45 s at any fps,
or let the test's rule allow 0.85 s + 2 frames at the measured fps. Not done now: the 0.7.13 release waits, the player
sees a staff in the hand ~0.2 s longer.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: held_items_tick looked at the mirrors' own holds with the rest of its work every 0.25 s; a hold was first
seen at the tick after it began and cut at the second tick after that (0.25 s < kHoldUnsent 0.35 s < 0.5 s): 0.5-0.85 s
after it began, more at a low frame rate where each tick lands on a frame (lab, the guest at 25 fps: 0.59-0.78 s from
the hand set to the cut on the game clock).

## Fix
The own-hold check (seen, cut, the 1.5 s end) runs every frame; hold_try keeps the 0.25 s tick. The cut comes
0.41-0.43 s after the hold began at 25 fps. Switch `coop.fix tribe.hold_cut_frame off`: everything every 0.25 s.

## Verification
Test `tribe_hold_cut` (coop_tribe_hold_cut.py: the guest at 25 fps puts the staff into its mirror's hand of
the host's chief 10 times; `coop.tribe holdtrace` H to C on the game clock, every cut within 0.55 s).
FAIL (SPORE_FIX_OFF=tribe.hold_cut_frame): out/lab/20261113-041537 (0.59-0.78 s). PASS: out/lab/20261113-041709,
and the final check runs (see History).

## History
- 2026-11-12: opened.
- 2026-11-13: fixed on fix/w49-mirror (package T2 of 0.7.14).
- 2026-11-13: fix/w49-mirror final check: tribe_hold_cut PASS out/lab/20261113-052132 (lane 7), out/lab/reruns 20261113-054031 (lane 6) and 20261113-054834 (lane 7), the test made to SKIP when the host held in most rounds); tribe_hold_guest PASS x2 (20261113-051820-2, 20261113-052132).
