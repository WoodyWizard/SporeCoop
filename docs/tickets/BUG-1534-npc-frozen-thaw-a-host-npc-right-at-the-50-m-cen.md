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
branch: fix/w84-tests
commits: 318e3f9c
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
Test boundary (observed, lab 20261123-023643 npc_frozen_thaw.log): 5 host NPCs within 50 m of the guest from 5 s
on (4 at 3-4 s: one walks about the 50 m line), at 8 s `n35 (50 m from the guest) not on the guest`, at 7 s and 9 s
it is there; no stale drop (0), no proxy off. The two censuses are read one after the other (host, then guest) and each
against the guest's avatar of the guest's own game, so an NPC that crosses the 50 m line between the reads is "within 50
m" on the host and may be missing from the guest's list in that read. Not the mod's radius: the host keeps an NPC for a
guest within kRelevance 110 m (+ kHysteresis 20 m, npc.cpp) and the census lists 250 m. Not a proxy bug.

## Fix
coop_npc_frozen.py missing_near(): a host NPC missing on the guest in the outer EDGE = 5 m of the NEAR circle (45-50 m
from the guest) is printed as an edge read ("(edge read, not counted: n35 (49.8 m) ...; BUG-1534)") and not counted as
missing; a proxy missing well inside (BUG-0611's stale drop) and a proxy 15 m off (BUG-1234 / BUG-1376, the
drive.lead_gap twin) count as before. Used by npc_frozen_death, _caption, _live, _thaw. Test only, no mod change.

## Verification
Open, pending night run: `SPORE_DETECTOR=20 python3 tools/lab.py run npc_frozen_thaw --repeat 6 --lane N --retry 0 --no-build` PASS 6 of 6 (an "edge read" line may show);
the twin `SPORE_DETECTOR=20 python3 tools/lab.py run npc_frozen_thaw_lead_gap_off --repeat 3 --lane M --retry 0 --no-build` still FAILS (its "15-18 m off"
reads are not edge reads). FAIL before: 20261123-023643 (npc_frozen_thaw, rel/0.7.16 fd28f88f).

## History
- 2026-11-23: opened.
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): the census notes a proxy missing in the outer 5 m of its 50 m instead of counting it (318e3f9c).
