---
id: BUG-0401
title: Creature: creature parts the host unlocks during a session never reach the guests' games (their own editor's palette)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: lab monitor (feat/w32-monitor-diag, unlocks rule plant), lane 6
role: guest
versions: unreleased
fixed_in:
branch:
commits:
tests: monitor_plant (unlocks rule)
signature:
docs: docs/re/creations.md#creations-unlock-part
related: BUG-0368
---

## Report
monitor_plant (out/lab/20261101-130546-feat-w32-monitor-diag, lane 6, creature fixture): the host unlocks 3
creature parts in its own game (`coop.parts unlock 3`, cCollectableItems::UnlockPart as a found part does):
host `parts=57:...`, the guest stays at `parts=54:C81D84FB` for the rest of the session (90+ s); the
monitor's unlocks rule fires. Since BUG-0368 the guest's own editor offers its cPlayer's collected parts
(cPlayer +0x10E8), which come from the host's save at the share and are never updated.

## Root cause
Observed: nothing in the mod sends a part unlock (no UnlockPart hook or message); the guest's collected
parts are those of the save it loaded. Inferred, not checked: a part the host finds in the world (a
skeleton) goes the same way (UnlockPart in the host's game only).

## Fix
Open (not part of feat/w32-monitor-diag): send the host's unlocks (or its collected parts) to the guests.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
