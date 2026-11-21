---
id: BUG-9307
title: All stages: a partner revives a teammate in 5 s (1.5 s before, Space repair 2 s)
status: fixed
kind: feature
stage: all
reported: 2026-11-20
source: owner request 2026-11-20
role: all
versions: 
fixed_in: 0.7.14.1-lineages68-dev
branch:
commits: 419b0e84
tests: tools/qa_win (live two-game QA on the MSI and TITAN, 2026-11-21, switch off and on)
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
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): PASS: guest `coop.giveup down` next to the host: down 00:18:33.248, "MSI was revived." 00:18:38.252 (5.0 s). FAIL as the old behaviour with `coop.fix core.revive_5s off`: 00:19:19.314 -> 00:19:20.823 (1.5 s).

## History
- 2026-11-20: opened.
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
