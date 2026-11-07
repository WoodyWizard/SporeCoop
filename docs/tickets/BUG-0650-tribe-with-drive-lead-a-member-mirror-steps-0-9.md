---
id: BUG-0650
title: Tribe: with drive.lead a member mirror steps 0.9 m per frame after a walk starts in a latency spike of the player network (tribe_walk_player)
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-remote-bodies-2 regression runs (M3)
role: guest
versions: unreleased (rel/0.7.9 + drive.lead)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0619, BUG-0425
---

## Report
Found by M3 (BUG-0619) on fix/w45-remote-bodies-2 after the BUG-0619 fix (which takes the member lead off only over a
one-way latency over 100 ms; the player network, 45 ms one-way, keeps M1's lead). tribe_walk_player (SPORE_NET=player:
45 ms, +-15 ms, spikes of +250 ms for 0.7 s every 7 s; both games at 30 fps) fails 'no jump of a member while it walks'
in 2 of 4 runs of the final code: out/lab/20261106-185808 #1 (3 jumps), #2 (2 jumps); passed out/lab/20261106-192853
(2 of 2). 0.7.8 (SPORE_FIX_OFF=drive.lead) at the same time: out/lab/20261106-192856 2 of 2.
Decomposed (track JSON): the lead target jumps 3.6-3.8 m in one frame at a walk's start (target steps 0.0, 3.6-3.8,
0.28-0.3 m), then catch_prop draws 0.87-0.95 m frames (the BUG-0619 mechanism, at a start known late in a spike).
Smoothing lead steps over 1.5 m (7b1bc043, reverted 359ddb5e) turned that into stop-and-go (uneven 12-18 %) and made
body_trail_tribe_player trail (1.1-1.8 s over 2 m): out/lab/20261106-191334, -191337.

How to reproduce: tools/lab.py run tribe_walk_player --repeat 4 --retry 0 (and SPORE_FIX_OFF=drive.lead).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
