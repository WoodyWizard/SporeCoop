---
id: BUG-1207
title: cell_mirror_nan: the mirror's soft body freed by the game's LOD (parts=0) was read as particles at (0 0), 149 m from the host's pose
status: open
kind: test
stage: Cell
reported: 2026-11-16
source: lab
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
Final check of 0.7.15 (lab 20261116-025154, lane 3): `cell_mirror_nan` FAIL `its particles 149.0 m from the host's
pose`: at 1 s the healed mirror was finite and in place (heals=1), at 3 s `mirrornf ... pos=(115.26 94.40)
pmean=(0.00 0.00) parts=0`, the host at (115.29 94.39).

## Root cause
Observed: the mirror's centre was 0.03 m from the host's pose; it had no soft body at that moment (parts=0), so the
particle mean was (0 0). The game frees the soft body of every cell outside the active box around the camera
(Simulator::Cell::SoftBodyLod, docs/re/cell.md, BUG-0355 notes); the NaN mirror was 12.5 m from the guest. A test
artefact, old (the test is 0.7.14's, BUG-1006). One rerun also had no soft mirror near the camera within 10 s (SKIP).

## Fix
tools/tests/coop_cell_mirror_nan.py: with parts=0 the mirror's centre (pos, also covered by nf) is compared; it asks
for a soft mirror for up to 20 s. The twin (`SPORE_FIX_OFF=cell.mirror_nan_heal`: nf stays non-zero) is unchanged.
switch: none (test only).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
