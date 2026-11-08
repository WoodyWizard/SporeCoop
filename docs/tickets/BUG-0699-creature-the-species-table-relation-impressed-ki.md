---
id: BUG-0699
title: Creature: the species table (relation, impressed, killed) differs between the host and a guest (detector species_relation / species_only / species_killed)
status: open
kind: bug
stage: Creature
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11), detector
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0419
---

## Report
Detector rows of the BUG-0419 class in Creature (lab runs of 2026-11-06/07, 1948 Creature comparisons): `Creature:state:species_relation` 12 (e.g. 2 on the host, 3 on the guest), `Creature:state:species_killed` 10, `Creature:state:species_only` 2 (campaign_tribe, long_creature, mem_reserve).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
