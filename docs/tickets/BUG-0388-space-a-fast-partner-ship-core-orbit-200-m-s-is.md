---
id: BUG-0388
title: space: a fast partner ship (core orbit ~200 m/s) is drawn up to ~30 m off its own path for ~0.2 s when it turns or stops
status: open
kind: bug
stage: Space
reported: 2026-11-01
source: coop_space_finale_wait.py coop.fleet track, fix/w31-space-gate
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
lab 20261101-100032 (fix/w31-space-gate, lane 10, coop_space_finale_wait.py with `coop.fleet track`): in
the galactic core's system the guest's ship (at the gate, the core's orbit moves it at up to ~210 m/s)
is drawn on the host 20-31 m from every point of its own path over the 0.5 s before, for ~0.2 s
(s 65.707-65.914, 11 of 84 rows), while the guest's ship stopped (own x -191 -> -218.3, then still):
the drawn ship came from y -211 to -184 while the own ship stayed at y ~-178. Every other row was
within 0.1 m of the path. In the run before (20261101-095432) the same direction was within 0.1 m at
207-235 m/s. Inferred, not checked: the partner interpolation (SnapshotBuffer, extrapolation up to
0.25 s with the last velocity) overshooting on a sudden stop or turn, or snapshots arriving late
during the host's cinematic. Reproduce: `lab.py run space_finale_wait`, line `core: the guest ship
on the host` (printed only, not checked).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
