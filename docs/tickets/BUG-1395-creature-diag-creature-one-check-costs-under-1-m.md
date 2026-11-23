---
id: BUG-1395
title: diag_creature: 'one check costs under 1 ms' fails on a loaded machine (wall time of the check 30x, the cost did not grow in 0.7.16)
status: fixed
kind: test
stage: Creature
reported: 2026-11-21
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w62-core
commits: 22a8b5ca
tests: tools/tests/coop_diag.py (diag_creature, diag_cell)
signature:
docs:
related: BUG-1191, BUG-0791
---

## Report
Third final check of 0.7.16 (lab 20261121-003202, nightly, lane 4): diag_creature FAIL "one check costs under 1 ms
(host 1.935 ms, guest 1.371 ms)" (avg host 1.257 ms, guest 0.719 ms), every other check ok. Known flake: 2 of 5 on
rel/main, 9 of 17 on all branches.

## Root cause
A flake under machine load, outside the mod; the cost did not grow in 0.7.16. Evidence (the COST lines of every
diag_creature log in the lab dirs):
- The same work (Creature, 22 objects, 472 bytes per digest) took avg 0.044 ms on rel/0.7.16 itself in the second
  final check (20261120-133703, lane 6, 60 fps, no hitch), 0.037 ms in 20261117-154911, 0.033 ms in 20261116-025154,
  and 0.3-1.4 ms avg (max 0.7-5.7 ms) in the runs of 2026-11-16 at a load average of 26-30 on 12 cores (BUG-1191).
  This run: 1.257 ms avg, both games slow alike (the guest 0.719 ms against ~0.03), the host's frames that minute 47
  fps, p95 34.5 ms, longest 1072 ms, 6 hitches: the machine was busy (4 lanes of the nightly; free memory 16.7 GB of
  62).
- No commit of w61 touches the sync-check (`git log 673f7a55..21025f40 -- mods/coop/diag_sync.cpp
  mods/coop/net/diagmsg.h`: none); the 0.7.16 changes there (BUG-1339, BUG-1309, BUG-1310, BUG-1216) were all in the
  20261120-133703 build that measured 0.044 ms.
- The cost is wall time of the main thread (each `skg::readable` a VirtualQuery syscall, BUG-0791): a busy CPU
  stretches it 30x without any change in the work.

## Fix
Test (coop_diag.py): the two cost checks (one check under 1 ms, the per-frame cost under 0.05 ms) are judged only
while the machine's 1-minute load average is under one per core; over it, a cost over its limit makes the test
INCONCLUSIVE with the load printed (never a pass; a cost under the limit still passes and every other failure still
fails). The limits themselves are unchanged. The final check should run on an idle CPU (owner rule).

## Verification
Was waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run diag_creature diag_cell --lane N --retry 0
--no-build` on an idle machine must PASS (cost ~0.04 ms) and print the load average; under load it must say
INCONCLUSIVE, not FAIL. no-fail-run: test change for a flake outside the mod (no FAIL twin).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): diag_creature and diag_cell PASS in 20261122-194210 and 20261123-023643. Closed.

## History
- 2026-11-21: opened.
- 2026-11-21: fix/w62-core: cause load (evidence of all runs), the cost checks are INCONCLUSIVE on an overloaded machine.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
