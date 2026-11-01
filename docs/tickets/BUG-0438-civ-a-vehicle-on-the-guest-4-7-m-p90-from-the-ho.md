---
id: BUG-0438
title: civ: a vehicle on the guest 4-7 m (p90) from the host's under loss bursts (sync_burst, sync_fps15burst)
status: open
kind: desync
stage: Civ
reported: 2026-11-01
source: players suite, lab 20261101-160125 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The players suite's burst variants of `sync` (coop_sync.py, every stage's objects on both games) failed Civ's
`civ/vehicles: 90% within 3.0 m` in both runs that had loss bursts (SPORE_NET=burst: rtt 70 ms, 30 % loss for
~3 s every ~20 s; lab 20261101-160125): `vehicles n=1 pos med 1.57 p90 7.32 max 8.57 m` (sync_burst, lane 9) and
`med 2.68 p90 4.26 max 4.44 m` (sync_fps15burst, the guest also at 15 fps, lane 10). The same run's
sync_mobile (3 % random loss, 120 ms) and sync_fps15 passed; the other stages' objects stayed within their
limits under the same bursts. Only one vehicle was sampled (n=1), so the evidence is thin.

How to reproduce: `SPORE_LANE=N SPORE_NET=burst python3 tools/tests/coop_sync.py` (the Civ part).

## Root cause
Not analysed. Inferred: during a burst the mirror extrapolates the vehicle (civ.extrap, civ.vel_cap) from
its last state; a driving vehicle that turns or stops meanwhile is several metres off until the next state.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
- 2026-11-01 (feat/w33-tests-session): again in a rerun of sync_burst (lab 20261101-173700): `vehicles n=1 pos med 0.13 p90 3.53 max 4.20 m`, raw p90 6.00 m.
