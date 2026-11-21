---
id: BUG-9307
title: All stages: a partner revives a teammate in 5 s (1.5 s before, Space repair 2 s)
status: open
kind: feature
stage: all
reported: 2026-11-20
source: owner request 2026-11-20
role: all
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
Owner, 2026-11-20: "Increase the revive time for a teammate to 5 seconds."

## Root cause
Feature. The hold was 1.5 s in Cell (cellsync kReviveHold), Creature / Adventure (creature_rescue hold) and Tribe
(tribe.cpp founder rescue); the Space repair 2 s (spacecoop kRepairHold).

## Fix
down_time.h `downed::revive_hold(old)`: 5 s on every stage. `coop.fix core.revive_5s off`: the old holds. The
Creature and Tribe hints print the time from it.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
