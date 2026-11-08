---
id: BUG-0754
title: Creature: sync-check MISMATCH n 4 apart (21-28 m) while the host saves for a world share (792 ms hitch), campaign_ct
status: fixed
kind: test
stage: Creature
reported: 2026-11-08
source: fix/w46-k4-ground lab 20261108-085348-2 #2
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-k4-proxy
commits: 4ecb0931
tests: share_sync
signature:
docs: docs/06-testing.md#testing-dash-order
related:
---

## Report
campaign_ct#2 of lab 20261108-085348-2 (fix/w46-k4-ground 6cd28ceb on rel/0.7.11 fa4f5df3, lane 6) failed the sync-check
gate: `sync-check: MISMATCH with the host (Creature 9652F8AD, 18 objects compared): n: 4 apart (n8 27.6 n1 26.4 n6 24.2
n5 21.4)` at s 293.9 on the guest and the host. At that moment the host was saving for a world share to the three guests
("world: saving for the share of player 1", `perf: hitch 792 ms (Creature, saving)` at s 292.6) and the guest logged
"receiving world from Alice" at s 293.4; the guest's proxy n8 was 0.89 m from its target (the copy followed the host's
states). Inferred: the check compared positions across the host's save hitch / share (BUG-0385's family), not a drawing
error. Not reproduced in the other 5 campaign_ct runs of the same build. Reproduce: tools/lab.py run campaign_ct --repeat 3.

## Root cause
Observed (host and guest logs of 20261108-085348-2 #2): the check ran while the host saved for a share to its three guests
(792 ms hitch) and the guest received it; the host's digest had its family members by the host (3.6 m), the guest's copies
still where the last states put them, 21-28 m off; the guest replaced its world 1.3 s later. A guest that is about to load
another world is compared with nothing it will keep (the class of diag.stage_end, BUG-0588).

## Fix
`coop.fix diag.share_quiet` (diag_sync.cpp, world::since_share): for kShareQuiet 20 s after a share to a guest was asked
for, the check takes that guest as in another place (kResOtherPlace: no MISMATCH, no recheck). Off: compared (0.7.10).

## Verification
- FAIL: out/lab/20261108-101133 (SPORE_FIX_OFF=diag.share_quiet, share_sync 2 of 2: the planted difference reported during the
  share). The campaign_ct failure itself: 20261108-085348-2 #2 (fix/w46-k4-ground).
- PASS: out/lab/20261108-101133-2 (share_sync 2 of 2: nothing during the share, the planted difference found again after it).
- The test plants the difference (`coop.diag sync force shift`): it checks the rule, it can not make the host's save move NPCs.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k4-proxy): fixed (diag.share_quiet), test share_sync.
