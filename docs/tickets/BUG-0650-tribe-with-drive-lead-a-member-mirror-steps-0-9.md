---
id: BUG-0650
title: Tribe: with drive.lead a member mirror steps 0.9 m per frame after a walk starts in a latency spike of the player network (tribe_walk_player)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-remote-bodies-2 regression runs (M3)
role: guest
versions: unreleased (rel/0.7.9 + drive.lead)
fixed_in: unreleased
branch: fix/w45-lead-player
commits: 19e4513f, 154f49db, a85c740f, e471b3f6, 9530af70 (drive.lead_draw cad93096 tried, reverted fdbab153)
tests: tribe_walk_player (tools/tests/coop_tribe_walk.py --fps 30, SPORE_NET=player), net_tests test_lead_late
signature:
docs: docs/re/tribe.md#tribe-lead-late
related: BUG-0619, BUG-0425, BUG-0386, BUG-0651
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
Observed (M3, track JSON; again here in the twin runs): at a walk that starts in a latency spike the newest state is
the stand until the first walking state comes 0.3-0.4 s late; M1's lead then shows it moved on to the sender's now, the
target steps 3.6-3.8 m in one frame and catch_prop (walk + placing at up to 2 x speed + 3 m/s) draws 0.87-0.96 m frames,
over the test's 3 x 9 m/s x dt. Observed (net_tests test_lead_late, a simulated netem spike): when the spike ends the
states sent in it arrive after newer ones and are dropped as old, so the first walking state that is kept can itself be
2.7 m from the stand. Observed: bounding the drawn step instead (drive.lead_draw, cad93096, reverted) left a short walk
known late 3 m behind at its stop and the stand driving closed that in a 1.08 m frame (lab 20261106-200630 #2).
Observed (lab 20261106-212040 #2, member 7 at 34.8 s): a catch-up at 1.5 x an 11 m/s walk (16.5 m/s) drew 0.94-0.96 m
frames too. Not BUG-0386 (no hold, no 8 m teleport in these tracks).

## Fix
`coop.fix drive.lead_late` (net/interp.h SnapshotBuffer::late_time, lead_at; Tribe member mirrors only, smooth_max > 0,
with the lead on, i.e. under drive.lead_smooth's 100 ms rule; off = M1's lead as in rel/0.7.9): the lead target has its
own sender time. It advances at the real time's rate; when a new state arrives it may step back (never forward) as far
as the state before the newest, to the time of that span whose target is nearest where the shown target was going (a
state as expected: no change; a start known late: the path from the last standing state, no start step). Then it
catches up with the lead at 1.5x the real time but at most 3 m/s over the body's speed, and the mirror walks at that
rate meanwhile. A late start is thus shown from where the body stood and reaches the lead within lateness x speed / 3
m/s (0.35 s late at 9 m/s: ~1 s; on a calm link ~0.1 s late: 0.3 s). Also: the member lead on/off log line now up to
400 lines (12 did not cover one run).

## Verification
FAIL (SPORE_FIX_OFF=drive.lead_late, M1 + M3 as in rel/0.7.9): tribe_walk_player out/lab/20261106-204537 0 of 3 (jumps 4
and 9 per view, 0.90-0.96 m in 31-33 ms frames); also M1 out/lab/20261106-200650 0 of 3 (1 jump 0.94 m + windows /
uneven). net_tests test_lead_late: M1's target steps 3.30 / 3.61 m per 1/30 s frame, the fix 0.40 / 0.47 m.
PASS: RESULTS_PLACEHOLDER
Lab load 9-19 on 6 cores during all of these (a release batch on the other lanes); 0.7.8 itself failed 1-2 of 3 in some
of those pairs (windows 63-71 %), so tribe_walk_player is judged A/B. The lead was on for most of the walks (guest log:
`lead on (one-way latency 79-84 ms)`, off for 10-15 s per run when the spikes or the load pushed the median over 100 ms).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-lead-player): tried drive.lead_draw (a bound on catch_prop's drawn step: jumps gone, but a short
  late walk ended 3 m off and the stand driving drew a 1.08 m frame; reverted); fixed with drive.lead_late (a late state
  brought in over time). Status fixed, fixed_in unreleased. New: BUG-0651 (under load the drive.lead_smooth rule turns
  the member lead off on any link).
