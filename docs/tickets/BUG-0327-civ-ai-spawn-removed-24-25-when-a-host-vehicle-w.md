---
id: BUG-0327
title: civ_ai_spawn: 'removed 24 -> 25' when a host vehicle went away during the test (test race)
status: fixed
kind: test
stage: Civ
reported: 2026-10-30
source: lab 20261030-163302-fix-w14-tribe-civ civ_ai_spawn
role: guest
versions: fix/w14-tribe-civ
fixed_in: 0.7.3.2
branch: fix/w15-civ
commits: 43ef004
tests: civ_ai_spawn (coop_civ_ai_spawn.py)
signature:
docs: docs/06-testing.md#testing-sync-lowfps-round
related: BUG-0188
---

## Report
Lab run 20261030-163302-fix-w14-tribe-civ (lane 7, warm run 2): `civ_ai_spawn` FAIL "the snapshot had
no local vehicle to kill (removed 24 -> 25)"; every other check passed (no vehicle of the guest's
own AI seen, 3 SpawnVehicle calls on the guest, 3 refused). 3 of 3 passed right after.

## Root cause
Observed (both game logs of that run): the host's vehicles went 7 -> 6 in the test's last minute and
the guest's mirrors with them. The guest's `removed=` counts both a vehicle of its own it kills
(what the test is about, BUG-0188) and the mirror of a host vehicle that is no longer in the
host's snapshot (died or went away, guest_state). The test counted the second kind: a race of the
test, not a bug of the mod (the guest followed the host).

## Fix
43ef004: the guest counts its own removed vehicles apart (`removed_local=` in `coop.civ`) and the
test checks that counter. No behaviour change.

## Verification
* FAIL (old test): lab run 20261030-163302-fix-w14-tribe-civ ("removed 24 -> 25", host vehicles 7 -> 6).
* PASS: lab run 20261030-172656-fix-w15-civ (3/3).
* The test still catches BUG-0188: `--nofix` on lane 6 (direct run) FAILED with 2 SpawnVehicle calls,
  0 refused, removed_local 0 -> 1.

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w15-civ): a test race; the test counts only the guest's own vehicles.
- 2026-11-01: released in 0.7.3.2.
