---
id: BUG-9359
title: Galaxy: the host of a stored Creature world is killed on arrival
status: open
kind: bug
stage: galaxy
reported: 2026-11-23
source: qa
role: 
versions: 
fixed_in:
branch: feat/lineages-alpha
commits:
tests:
signature:
docs: docs/known-issues.md
related: BUG-9215, BUG-9265
---

## Report
The MSI, 2026-11-23, TITAN's galaxy server: a game entering Samuth's stored Creature world (and Testplanet01, a copy of
it) as its host was killed at once: the death screen "You were killed in battle! When wounded, retreat to your nest
to heal." while the stage's intro ran, twice (two profiles). After its check the game went on (respawn).

## Root cause
Not analysed. Candidates: the stored world was saved with the player creature dead or at 0 health, or a predator
next to the spawn point. BUG-9215 (a joiner arriving at a dead host) is not this: nobody else was on the planet.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.

