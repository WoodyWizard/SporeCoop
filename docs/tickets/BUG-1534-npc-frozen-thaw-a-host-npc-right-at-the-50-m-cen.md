---
id: BUG-1534
title: npc_frozen_thaw: a host NPC right at the 50 m census edge is read as missing on the guest (test boundary, not a proxy bug)
status: open
kind: test
stage: Creature
reported: 2026-11-23
source: lab 20261123-023643
role: test
versions: 0.7.16
fixed_in:
branch:
commits:
tests: npc_frozen_thaw
signature:
docs:
related: BUG-1234, BUG-0611
---

## Report
Found by fix/w76-motion (BUG-1234 triage), lab 20261123-023643 (rel/0.7.16 fd28f88f, lane 2): npc_frozen_thaw FAIL
"through the thaw the guest keeps every host NPC near it (1 reads with some missing; stale drops 0)". The one read:
8 s after the OK, `n35 (50 m from the guest) not on the guest`; every other read of the run had all host NPCs within
50 m, and no proxy was off (not BUG-1234's "16-18 m off" signature). The test's census (coop_npc_frozen.py
missing_near, NEAR = 50.0) counts a host NPC that walked to the very edge of 50 m from the guest, where the guest's
game may not have its proxy yet (made / dropped around that distance).
How to reproduce: `lab.py run npc_frozen_thaw --repeat 6` and look for "(5x m from the guest) not on the guest" with
x = 0 in a failing read. Since drive.lead_gap (BUG-1376) the 16-18 m signature is not seen: 5 PASS and this 1 FAIL on
rel/0.7.16 (20261120-041304, -133703, 20261121-003202, 20261122-194210, 20261123-071312 PASS).
Next step (WP-T1 or the test's owner): a margin band (count a missing NPC only when it is under NEAR - 5 m from the guest
in that read), or read the guest's proxy radius from the mod. Not fixed on fix/w76-motion (not its task).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
