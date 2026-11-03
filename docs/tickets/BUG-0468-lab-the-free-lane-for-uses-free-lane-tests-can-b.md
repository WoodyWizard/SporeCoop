---
id: BUG-0468
title: lab: the free lane for uses_free_lane tests can be a lane busy with other work or the owner's lane 8, and two such tests share it at once
status: fixed
kind: tooling
stage: lab
reported: 2026-11-02
source: final verification of 0.7.3.5 (2026-11-02, out/lab/20261102-163032)
role: 
versions: 
fixed_in: 0.7.3.6
branch: feat/w39-detector
commits: 3ebba62e
tests: unknown_parts, guest_missing_pack
signature:
docs:
related:
---

## Report
Final verification of 0.7.3.5 ran on lanes 4-7 while an agent used lanes 2-3. tools/lab.py set
SPORE_FREE_LANE to the first lane of 2..8 not in the run and not --skip: lane 2, busy with the agent's
games. guest_missing_pack and unknown_parts (both uses_free_lane, their pack-less guest on the free lane)
ran at the same time on it: one put Data/Spore_Pack_03.package back while the other expected it gone
(the pack-less guest logged have_pack=1), both FAILED, and the agent's lane 2 games were disturbed.
A run on lanes 2-7, 9, 10 (the nightly detector) would have picked lane 8, the owner's lane.

## Root cause
Observed: the lab code (`range(2, 9)`, no check of other work, no serialization of the free lane's users).

## Fix
The free lane is a lane of 2..10, never 1 or 8, not in the run, not --skip, and with no game answering
its console at the start of the run; uses_free_lane tests of one run take it one at a time (a lock).
Branch feat/w39-detector.

## Verification
FAIL run: out/lab/20261102-163032 (lanes 4-7, an agent on 2-3): guest_missing_pack and unknown_parts both on
lane 2 at once, the pack-less guest had the pack (have_pack=1). PASS run: out/lab/20261102-170442 (lane 4,
--skip of the busy lanes, free lane 5): guest_missing_pack and unknown_parts one after the other.

## History
- 2026-11-02: opened.
- 2026-11-02: released in 0.7.3.6.
