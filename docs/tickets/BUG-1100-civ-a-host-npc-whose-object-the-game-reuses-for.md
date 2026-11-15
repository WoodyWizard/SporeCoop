---
id: BUG-1100
title: Civ: a host NPC whose object the game reuses for another creature during a pause keeps its id; the guest shows the old one 690 m away (sync-check MISMATCH in civ_wild_pooled)
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-14
source: final check 0.7.14, lab 20261114-005006 civ_wild_pooled
role: guest
versions: rel/0.7.14 4dd8d2be
fixed_in: 0.7.14
branch: fix/w49-fcgates
commits: cbe4dced
tests: civ_wild_reused, civ_wild_pooled
signature: sync-check MISMATCH n: 1 apart (nNNN 500-700 m) during the host's busy=paused, host species differs from the guest's proxy
docs: docs/re/civ.md#civ-wild-reused-frozen, docs/06-testing.md#testing-fcgates-0714
related: BUG-1089, BUG-0044, BUG-0339
---

## Report
The 0.7.14 final check (lab 20261114-005006, lane 3) failed civ_wild_pooled (BUG-1089's own test) by the sync-check
gate: `MISMATCH ... n: 1 apart (n129 690.9)` at s 50, while the test's checks passed; the rerun passed.

## Root cause
Observed (logs): the host stood in its pause menu (`busy=paused` at s 40 and 50). Host: `#129 10E5C360 species=065CBF12
... dist=517.1 moved64=507.1` (through the planet's centre within its last 64 updates). Guest: `#129 species=06538C80
... age=15.42 at=(-387.0 -327.7 0.0)`. The same object of the host's game was given out again as another creature
within the stand, between two 0.5 s frozen passes. npc.frozen_pooled_rts (BUG-1089) retires only objects gone from
the list or at the centre; host_tick's reuse check (BUG-0044, npc_object_reused) does not run while no creature is
updated, so #129 kept its id and the guest its old creature.
Observed later (20261114-033051): the host's game rebuilds tracked objects as other species during a stand, 14 at once
after the fixture's start; some sit at unplaced points with z = 0 until the game goes on.

## Fix
`npc.frozen_reused` (npc.cpp frozen_pooled_pass): a tracked live object that npc_object_reused calls another creature
(another species, or alive after dead) is retired like a pooled one (despawned at every guest); host_tick gives the
new creature a new id when the game goes on. `coop.npc` prints `frozen_reused=`. Test hook `coop.npc kill <id>` (the
death flag, for the reuse's dead -> alive state; Civ has no hit that reaches an NPC).
`coop.fix npc.frozen_reused off`: kept (0.7.14 rc).

## Verification
Test civ_wild_reused (civ_npc, lane 7): an NPC killed (`coop.npc kill`), the host's pause menu, `coop.npc reuse`
(alive again in its object); the guest must drop the old NPC while the host stands.
* FAIL (SPORE_LAB_FIXOFF="civ_wild_reused=npc.frozen_reused"): out/lab/20261114-031950 (the guest showed #8 for the
  whole 6 s, frozen_reused=0).
* PASS: out/lab/20261114-032137 (civ_wild_reused x2, civ_wild_pooled x2: 4/4), 20261114-032854 (civ_wild_reused with
  the retire line), 20261114-033051 (civ_wild_pooled).
* Seen with the new log line (20261114-033051, civ_wild_pooled): in the stand right after the fixture's start the
  host's game turned 14 tracked objects of species 06689607 into 065276AC and 066742CD within 2 s, several at
  unplaced points (0 524.8 0), (-368.7 -347.0 0.0) like the guest's #129 at (-387.0 -327.7 0.0) of the final check:
  the reuse is common in a Civ stand, not a rare case; all 14 were retired and got new ids when the game went on.

## History
- 2026-11-14: opened and fixed on fix/w49-fcgates (final check 0.7.14 triage).
- 2026-11-14: released in 0.7.14.
