---
id: BUG-1188
title: tribe_duet fails on rel/0.7.15 + BUG-1187: the host's instrument member does not perform after the right-click with its chief in the box
status: open
kind: test
stage: Tribe
reported: 2026-11-16
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: tribe_duet
signature:
docs:
related: BUG-0748, BUG-1187
---

## Report
Found by fix/w51-tribefields (BUG-1187) in its Tribe set (lanes 4 / 5, `--quick`, 135 of 136 passed): `tribe_duet` FAILs
"both players' members perform for the same village in the host's game (None)" and "the mini-game's progress rose
with nobody playing (0.00, duet ticks 0)". Only the guest's #2 performs for the NPC tribe (`#2 o1 ... s3 0.00`,
ticks 0); the host's instrument member #3 never shows in the list.
Runs (build of fix/w51-tribefields = rel/0.7.15 5912ab18 + BUG-1187, lane 4): out/lab/20261116-002240 (warm),
out/lab/20261116-020859-2 (cold): FAIL both. v0.7.14.1 on the same lane 4: PASS (fix/w51-tribefields-v07141
out/lab/20261116-021050).
Difference seen: on 0.7.15 the host's box selected [1, 3] (the chief with the instrument member, as the test intends,
BUG-0748 note) and the host's right-click gave CitizenAction 26 with n=2; on v0.7.14.1 the box selected [3] only, no
click gave the order and the test's DoActionForObjects fallback ordered it (1 selected), then both performed. So the
one comparison run is not the same path: not proven to be a 0.7.15 regression; the 2-member order of action 26 may
never start the host's performer (or only on 0.7.15). Unrelated to BUG-1187 (hunger).
To reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_duet --lane 4 --retry 0 --no-build`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
