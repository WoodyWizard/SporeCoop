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
branch:
commits:
tests: npc_frozen_thaw
signature:
docs: docs/known-issues.md#issue-creature-thaw-proxy-off
related: BUG-0611, BUG-1216
---

## Report
0.7.15 final check (lab 20261117-154911) and its rerun (20261117-213354): npc_frozen_thaw FAIL "through the thaw the
guest keeps every host NPC near it (2 reads with some missing; stale drops 0)", once with the sync-check gate "n: 1
apart (n9 8.6)". The "missing" NPC is not dropped: its proxy exists and is 16-18 m off the host's NPC for 3 reads
after the caption closes, then back (out/lab/20261118-000853, npc_frozen_thaw#1 on lane 9: `n1 18 m off on the guest`
at 8-10 s).

## Root cause
Not found yet. Observed: not a 0.7.15 regression, the same rate on both builds on the same machine the same night:
v0.7.14.1 3 of 6 FAIL (out/lab/20261118-000853 of tag/v0.7.14.1-w51tribe, lanes 6 / 7), rel/0.7.15 34b52d3c 4 of 6
FAIL (rel/0.7.15 out/lab/20261118-000853, lanes 9 / 10); 11114769 1 of 2; bisect points 01217ee9 (also with
BUG-1209's twoinst change reverted: 3 of 3), 34ab3598, dc89655d FAIL, 5e36ab76 PASS. In both builds the cold runs
fail and the runs after a warm reset pass. Before 2026-11-17 the test had passed 9 of 9 runs (all branches).
Inferred: the host's NPC moves on after the thaw (its brain resumes with a jump) and the guest's proxy follows the
state batches only after the rxkeep hold (BUG-0611): a position drift, not the drop BUG-0611 fixed.

## Fix
Not fixed in 0.7.15 (an old desync, owner informed); next release.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened in the second 0.7.15 final-check triage; old (0.7.14.1 the same), left for the next release.
