---
id: BUG-1409
title: tribe_conquer_orders: the hut_fell key frame waits out the conquest cinematic, the test's watch for it starts after it ended
status: open
kind: test
stage: Tribe
reported: 2026-11-21
source: nightly 20261121-003202
role: 
versions: unreleased
fixed_in:
branch: fix/w62-flake-tcs
commits:
tests: tribe_conquer_orders
signature:
docs:
related: BUG-0761, BUG-0924, BUG-0531
---

## Report
Nightly 20261121-003202 (rel/0.7.16 21025f40, lane 3): tribe_conquer_orders FAIL "the host's game played the
conquest cinematic (None)", "host after 61 s: no cinematic". Known flake: 2 of 12 on rel/main, 10 of 30 on all branches;
the same check failed in feat/w51-tribe 20261115-182010. The earlier failures of 2026-11-08 were BUG-0761 (fixed).

## Root cause
Test (observed). The host's game did play the conquest: host log 06:59:27.295 [s 51.554] "the village cinematic
5418B038 plays on the host", ended by tribe.conquer_caption at s 59.571, then 94D17D9F until s 67.589; the guest's log
has "the village at (432 -5 266) was conquered in the host's game: here too" at s 52.2. The test read the hut's state 2
at its "33 s" poll and then called `keyframe('hut_fell')` before its 60 s cinematic watch. keyframe() waits (BUG-0924,
SPORE_KEYFRAME_SETTLE 20 s) until no game plays a cinematic: frames/tribe_conquer_orders/manifest.jsonl "waited 17.6 s
for the games to settle", the shots written 06:59:45 (s 70), after both cinematics. The watch then began at s ~70 and
saw none. feat/w51-tribe 20261115-182010: the same ("waited 18.0 s", cinematic at s 49.3, shots 18 s later). Passing
runs (rel/0.7.16 20261120-041304, -133703): the cinematic began 0-2 s after the keyframe's shot (no settle wait), so the
watch saw it. Which way a run goes is the 2 s poll's phase against the game's 0-4 s from the last blow to the
cinematic. Old (since the key frames of BUG-0924 / the release's visual checks), not a w61 regression, no mod bug.

## Fix
tools/tests/coop_tribe_conquer_orders.py: the `hut_fell` key frame is taken after the cinematic watch (it still shows
the fallen hut; its settle then has nothing left to wait for). The check is unchanged (the host's game played a
cinematic in the 60 s after the fall). No mod change, no switch.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
FAIL before: out/lab/20261121-003202 (rel/0.7.16), feat/w51-tribe out/lab/20261115-182010. Open, waiting for the lab run:
`lab.py run tribe_conquer_orders --repeat 4` must PASS 4 of 4 (a test change: no twin; the failing order needs the
cinematic to start before the poll reads the fall, 2 of 12 runs).

## History
- 2026-11-21: opened (fix/w62-flake-tcs); cause found in the frames manifest, the key frame moved after the watch.
