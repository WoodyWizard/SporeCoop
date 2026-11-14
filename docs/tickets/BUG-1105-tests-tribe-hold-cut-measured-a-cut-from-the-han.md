---
id: BUG-1105
title: Tests: tribe_hold_cut measured a cut from the hand set, not from the start of the hold it ended (0.836 s FAIL), and SKIPped when the host's chief held
status: fixed
kind: test
stage: Tribe
reported: 2026-11-14
source: rel/0.7.14 final check 20261114-005006 (SKIP), rerun 20261114-022615 lane 7 (FAIL)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-holdcut
commits:
tests: tribe_hold_cut
signature:
docs: docs/re/tribe.md#tribe-mirror-pose-phase, docs/06-testing.md#testing-t2-mirror
related: BUG-0993
---

## Report
rel/0.7.14 final check out/lab/20261114-005006: tribe_hold_cut `RESULT: SKIP 2 rounds counted (< 5): the host's
holds came in between` (8 of 10 rounds had a hold of the host's chief, S / T, in the trace). Rerun
out/lab/20261114-022615 (lane 7): `FAIL every hold of the mirror's own is cut within 0.55 s ([0.397, 0.429, 0.44,
0.44, 0.437, 0.48, 0.836, 0.534])`, round 8 trace `HOC`, sampled hold 0.58 s.

## Root cause
A test problem, not the mod's. Observed (lane 3, traces with times): with tribe.hold_cut_frame the cut comes
kHoldUnsent 0.35 s + a frame after the mod saw the hold (O), and O is the frame the hold began. The test measured from
the hand set H instead: (a) the game begins the kind 5 hold modifier of `coop.tribe hand` a frame later as a rule
(0.02-0.06 s), but now and then later: `HOC` with 0.836 s from H can only be an O ~0.45 s after H (inferred: the
rerun printed no times; nothing between O and C can delay a cut without an event in the trace); (b) the hand set's
hold can end with the mirror's gesture in 0.1 s and a relayed animation take the staff again: 20261114-031120 #2
round 8 `HBOROC`, B+0.04, hold= 0 after 0.09 s, R+0.93, O+1.05, C+1.46 (1.42 s from the first B, 0.41 s from the
second O). The SKIP: rounds started whatever the host's chief did; its gestures take its staff (S / T) and bind the
mirror's hold, so such rounds are not counted. Not related to the 0.7.14 changes (tribe.mirror_hang, net.send_phase,
npc.proxy_hold, drive.agl_extrap): every counted cut was 0.35-0.48 s from its hold's start.

## Fix
Trace B (tests, mods/coop/tribe.cpp hand_probe_tick): for 3 s after `coop.tribe hand <id> <item>` the guest looks at
that mirror's hand every frame whatever the switches and records each frame a hold modifier begins (a new handle). The
test measures each cut from the last B before it. A round starts only when the host's chief and its mirror are in their
idle / walk (two samples 0.3 s apart), and rounds go on until 10 are counted (at most --secs 300). The twin switch is
BUG-0993's `tribe.hold_cut_frame` (the mod's cut on the 0.25 s tick); no mod behaviour changed.

## Verification
FAIL (SPORE_LAB_FIXOFF="tribe_hold_cut=tribe.hold_cut_frame"): out/lab/20261114-032000 (cuts 0.52-0.78 s).
PASS: out/lab/20261114-032755 #1 and #2 (lane 3; cuts 0.35-0.40 s, 10 counted each, no SKIP).
Before the round gate: out/lab/20261114-032133 #2 SKIP (7 holds ended by the mirror's gesture), which the mirror's
idle / walk gate fixed.

## History
- 2026-11-14: opened.
- 2026-11-14: fixed on fix/w49-holdcut (tribe_hold_cut measures from the hold's start, round gate).
