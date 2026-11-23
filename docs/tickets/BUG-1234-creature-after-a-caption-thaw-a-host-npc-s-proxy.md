---
id: BUG-1234
title: Creature: after a caption thaw a host NPC's proxy on the guest runs 16-18 m off for a few seconds (npc_frozen_thaw, old: 0.7.14.1 too)
status: open
kind: desync
stage: Creature
reported: 2026-11-18
source: final check 20261117-154911
role: 
versions: 0.7.14.1, unreleased
fixed_in:
branch: fix/w61-creature-b, fix/w76-motion
commits: 14aa9dc9
tests: npc_frozen_thaw, npc_frozen_thaw_lead_gap_off (twin), net_tests test_lead_gap_thaw
signature:
docs: docs/known-issues.md#issue-creature-thaw-proxy-off
related: BUG-0611, BUG-1216, BUG-1376, BUG-1534
switch: drive.lead_gap
---

## Report
0.7.15 final check (lab 20261117-154911) and its rerun (20261117-213354): npc_frozen_thaw FAIL "through the thaw the
guest keeps every host NPC near it (2 reads with some missing; stale drops 0)", once with the sync-check gate "n: 1
apart (n9 8.6)". The "missing" NPC is not dropped: its proxy exists and is 16-18 m off the host's NPC for 3 reads
after the caption closes, then back (out/lab/20261118-000853, npc_frozen_thaw#1 on lane 9: `n1 18 m off on the guest`
at 8-10 s).

## Root cause
(2026-11-23, fix/w76-motion) The same as BUG-1376 (fixed in 0.7.16 by `drive.lead_gap`, 14aa9dc9, found on another
test): late_time (net/interp.h) took the first NPC state after the host's caption stand for late news of a walk, stepped
the shown time back to the gap's start and caught up at 2x real time, so a proxy whose host NPC the host's game had put
elsewhere during the stand crossed the gap's straight line slowly. Reproduced in net_tests test_lead_gap_thaw with this
test's numbers (a 10 s stand, the NPC put 18 m away, the proxies' buffer settings): without drive.lead_gap the target
is 16.7 m from the NPC 0.5 s after the states come again, 14.9 m at 1 s, 7.8 m at 3 s, 0.65 m at 5 s (the lab: 15-19 m
for ~3 reads); with it 0.00 m. Since 0.7.16 the signature is gone: npc_frozen_thaw on rel/0.7.16 PASS in
20261120-041304, 20261120-133703, 20261121-003202, 20261122-194210, 20261123-071312; the one FAIL (20261123-023643) is
another signature, a host NPC at the 50 m edge of the test's census not on the guest (BUG-1534, test only). fix/agy-
open-bugs c8c548db had the same idea with a 1.0 s gap and a second condition (`cont - earliest > 1 s`); its review
(out/research/agy-review-misc.md finding 3) asked to drop that condition and keep the gap over the Civ quiet period
(standing states once a second, BUG-0973). BUG-1376's rule has neither problem: sender-time gap only, kLateGap 2 s.
Checked: a Civ standing creature with one state lost (2.1 s gap) that then walks at 4 m/s gives the same target steps
with the rule on and off (net_tests test_lead_gap_thaw: max 0.269 m per 1/30 s both, 0.190 m at a 1.05 s gap), so
kLateGap stays 2 s; and agy's other two parts (no horizon sample for the gap, lt_rate_ 1 on the jump) change nothing
measurable (one sample in a 32 / 64 median; one frame's rate), so they are not taken.
Earlier observations:  not a 0.7.15 regression, the same rate on both builds on the same machine the same night:
v0.7.14.1 3 of 6 FAIL (out/lab/20261118-000853 of tag/v0.7.14.1-w51tribe, lanes 6 / 7), rel/0.7.15 34b52d3c 4 of 6
FAIL (rel/0.7.15 out/lab/20261118-000853, lanes 9 / 10); 11114769 1 of 2; bisect points 01217ee9 (also with
BUG-1209's twoinst change reverted: 3 of 3), 34ab3598, dc89655d FAIL, 5e36ab76 PASS. In both builds the cold runs
fail and the runs after a warm reset pass. Before 2026-11-17 the test had passed 9 of 9 runs (all branches).
Inferred: the host's NPC moves on after the thaw (its brain resumes with a jump) and the guest's proxy follows the
state batches only after the rxkeep hold (BUG-0611): a position drift, not the drop BUG-0611 fixed.

## Fix
Not fixed in 0.7.15 (an old desync, owner informed). Fixed in 0.7.16 by BUG-1376 (`coop.fix drive.lead_gap`: a new state
after a gap of over 2 s of the sender's states is no late news; the shown time is the lead's at once). 2026-11-23
(fix/w76-motion): no mod change; the unit test of this ticket's case (net_tests test_lead_gap_thaw, FAILs with the rule
off: 14.9 m) and its lab twin npc_frozen_thaw_lead_gap_off.

## Verification
PASS with the fix (rel/0.7.16, drive.lead_gap on): 20261120-041304, 20261120-133703, 20261121-003202, 20261122-194210,
20261123-071312 (npc_frozen_thaw ok). FAIL without it: 20261118-000853 and the bisect runs of the Root cause (before
14aa9dc9: 16-19 m off). Open, waiting for the lab run of the twin on today's code (pending night run: npc_frozen_thaw
npc_frozen_thaw_lead_gap_off): `SPORE_DETECTOR=20 python3 tools/lab.py run npc_frozen_thaw --repeat 3 --lane N --retry 0
--no-build` must PASS (a FAIL naming "(50 m from the guest)" is BUG-1534); `SPORE_DETECTOR=20 python3 tools/lab.py run
npc_frozen_thaw_lead_gap_off --repeat 3 --lane M --retry 0 --no-build` must FAIL with "N m off on the guest" (3-4 of 6 before).

## History
- 2026-11-18: opened in the second 0.7.15 final-check triage; old (0.7.14.1 the same), left for the next release.
- 2026-11-23 (fix/w76-motion): the cause is BUG-1376's (fixed in 0.7.16); unit test with this test's numbers, lab twin; agy's 1 s gap and extra condition not taken (review finding 3), kLateGap stays 2 s (checked); BUG-1534 opened for the 50 m edge read.
