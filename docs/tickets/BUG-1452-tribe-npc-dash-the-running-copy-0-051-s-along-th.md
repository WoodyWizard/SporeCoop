---
id: BUG-1452
title: tribe_npc_dash: the running copy -0.051 s along the path (limit -0.043) on a loaded night (6 lanes, 8 games), inputs and frame pacing normal
status: open
kind: test
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: 
commits:
tests: tools/tests/coop_tribe_npc_dash.py (tribe_npc_dash)
signature:
docs:
related: BUG-0757, BUG-0363, BUG-0321
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): tribe_npc_dash "FAIL the running copy is not a frame behind the host along its path (median -0.051 s, limit
-0.043 s)".

## Root cause
A flake of a load-sensitive measure, cause not proven. Recomputed from out/lane6/dash/20261122-223751-tribe.json:
-0.050; per quarter n294 +0.002 / -0.017 / -0.025 / -0.088, n309 -0.031 / -0.072 / -0.034 / -0.101. Inputs normal:
target from the host NPC p50 0.11 m, p90 0.62 m (earlier 0.09-0.12 / 0.52-0.66), fresh states 124/124, host 67.6 fps,
guest 15.4 fps (capped), guest dt p50 0.065 max 0.068, rtt p50 32 ms. Only the window lag differs: p50 +0.075 s
(before -0.005..+0.040). The night ran 6 lanes (8 games when it started after a 71 s budget wait; runs 2 and 3: 4
lanes). No code change since run 3 touches drive / proxy / net (7ba78dc2 is debug only); the test is unchanged since
0.7.15.5. History: rel 0.7.15 and 0.7.16 runs all OK (-0.007..-0.038); 2 of 8 failed on rel/main. The limit lies
between the fix's on (-0.041..+0.009) and off (-0.045..-0.074) bands; nothing to loosen, the mod can cause it, so no
INCONCLUSIVE.

## Fix
None (no defect found in the test). Rerun on a quiet machine.

## Verification
Open: PASS `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_npc_dash --lane N --retry 0 --no-build` on a quiet machine;
FAIL `SPORE_LAB_FIXOFF="tribe_npc_dash=npc.place_now" SPORE_DETECTOR=20 python3 tools/lab.py run tribe_npc_dash --lane M
--retry 0 --no-build`.

## History
- 2026-11-23: opened (fix/w65-night triage).
