---
id: BUG-0660
title: Creature: detector egg rows (noun_only_host, unmatched, guest_only) in play_creature, 2-9 rows a run, not covered by the Tribe egg fix BUG-0474
status: open
kind: desync
stage: Creature
reported: 2026-11-06
source: lab log sweep follow-up 2026-11-06 (detector rows rel/0.7.8 vs rel/0.7.9)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
play_creature noun_only_host:egg rel/0.7.8 3-6 rows of n1, rel/0.7.9 2-9 rows of n1-3 (n3 in 20261106-153514); also unmatched/guest_only/host_only egg rows. BUG-0474 fixed the Tribe eggs only (nest.eggs_tribe). Related: BUG-0384.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the lab log sweep follow-up; planned for 0.7.10.
