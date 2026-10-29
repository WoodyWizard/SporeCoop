---
id: BUG-0262
title: space_rivalry: the guest's puppet is missing on the host's home planet (1 of 4 runs)
status: open
kind: test
stage: Space
reported: 2026-10-29
source: lab 20261029-051251 (main eea6b46)
role: all
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
Full lab run 20261029-051251 (lane 2, cold, main eea6b46 with wave 5 incl. BUG-0218's puppet identity change): "both on the home planet (no partner here)" and "the guest's puppet on the host belongs to no empire (me 800021CA)", so the PvP hit checks that follow failed too. Reruns 20261029-055430: 3/3 OK on lanes 2 and 4. Check first whether the host retired or never made the guest's puppet (fleet.puppet_id, BUG-0218) or the guest was not yet on the planet (test timing). Logs: out/lab/20261029-051251/space_rivalry*.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
