---
id: BUG-1444
title: creature_aggression: the NPC pick read the guest's mirrors before it had spawned them (warm settle 0.3 s, 0 mirrored at s 9.4, 21 at s 10.0)
status: fixed
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w65-night
commits: 2811d2c2
tests: tools/tests/coop_creature_aggression.py (creature_aggression)
signature:
docs:
related: BUG-0168, BUG-1439
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, lane 4, warm, settle 0.3 s): creature_aggression "FAIL an NPC
of another species far from the host, mirrored on the guest (0), the host shows the guest's puppet (1DEFB448)".

## Root cause
Test: the pick ran before the guest had spawned its NPC mirrors. Guest log `00:31:51.504 [s 9.376] npcs mirrored: 0
within 60 m, 0 in all` (just in Creature), `00:31:52.160 [s 10.032] npcs mirrored: 8 within 60 m, 21 in all`; the
logs end at 52.15 / 52.62, so the one read fell into that gap (the host already simulated 21 and had the puppet).
Passing runs had settle 0.6 s and 16 candidates. Own species does not matter here: both play the world's species
065CBE70, the filter drops only that one. The same class as BUG-1439 (the guest's proxies 1 s after its world
changed). Not a 0.7.16 regression.

## Fix
coop_creature_aggression.py: the pick is read every 0.5 s for up to 20 s until there is a candidate and the puppet;
the check is unchanged.

## Verification
Was waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_aggression --lane N --retry 0
--no-build` PASS. no-fail-run: a test fix; the FAIL before is 20261122-194210 creature_aggression.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): creature_aggression PASS in 20261123-015558 and 20261123-023643. Closed.

## History
- 2026-11-23: opened; fix/w65-night: the pick waits for the guest's mirrors.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
