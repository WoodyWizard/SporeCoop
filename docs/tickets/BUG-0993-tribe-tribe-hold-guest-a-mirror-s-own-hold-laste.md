---
id: BUG-0993
title: Tribe: tribe_hold_guest: a mirror's own hold lasted 1.05 s at 26 fps (BUG-0962's cut, 0.25 s tick, over the 1.0 s rule)
status: open
kind: test
stage: Tribe
reported: 2026-11-12
source: fix/w48-ftribe lab 20261112-222723 #3
role: guest
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
