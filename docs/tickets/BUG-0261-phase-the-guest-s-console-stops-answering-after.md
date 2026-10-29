---
id: BUG-0261
title: phase: the guest's console stops answering after the host pulls it back into Tribe (2 of 7 runs)
status: open
kind: freeze
stage: Tribe
reported: 2026-10-29
source: lab 20261029-051251 (main eea6b46)
role: guest
versions: unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Full lab run 20261029-051251 (lane 3, cold): coop_phase.py passed every check (guest followed the host into Tribe, went back to Creature, was pulled into the host's phase again; 0 faults on both), then the invariant check failed: "guest: the console stopped answering (crash?)". The guest's game log ends normally (`Alice appeared in the world` after loading Zorispe.spo); no crash report was found. Reruns 20261029-055430: 3/3 OK. Lab history: phase failed 2 of 4 runs before (same message). Next: look for a hang of the guest right after its second world load (watchdog, wine process state), out/lab/20261029-051251/phase*.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
