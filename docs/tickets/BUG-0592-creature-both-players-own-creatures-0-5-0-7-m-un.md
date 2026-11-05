---
id: BUG-0592
title: Creature: both players' own creatures 0.5-0.7 m under the ground right after the guest's return from the main menu
status: open
kind: bug
stage: Creature
reported: 2026-11-05
source: lab long_creature_short 20261105-002102
role: both
versions: 0.7.7-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The long-session bot (long_creature_short, rel/0.7.7 74f4ad0d, lab out/lab/20261105-002102) failed on the monitor's
ground gate: "host: its own creature 0.70 m under the ground (Creature) on 2 lines from s 380" and the same on the
guest (0.52 m), right after the guest's "menu" transition (exit to the main menu, join again, 16 s). Before it the
session had an editor transition. Not seen in the same test's earlier runs (D0 branch).

## Root cause
Not analysed yet. Both games' own creatures at once suggests the game's own ground (terrain) state after the
guest's world load, not a puppet; the host did not leave.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
