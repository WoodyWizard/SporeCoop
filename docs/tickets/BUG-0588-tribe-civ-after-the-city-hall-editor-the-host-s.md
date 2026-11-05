---
id: BUG-0588
title: Tribe -> Civ: after the City Hall editor the host's own NPCs read at (0, 0, 0) and the sync check reports MISMATCH; the Creature evolve-in-place path runs for the Tribe editors
status: open
kind: desync
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-transitions, coop_tribe_civ.py)
role: host
versions: 0.7.6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen in the runs of the BUG-0576 test (tribe_civ, lane 4, a scratch run at 22:26 on fix/w43-transitions):
* when the game asked for the vehicle editor (the second Tribe -> Civ editor) the host logged "evolved in place: Bob
  keeps its world (host species 00000000 -> 00000000, age -1 -> -1, migration -1 -> -1, cinematic 1)" and "Bob gets
  your new body in place (no reload)", and after each editor "Alice evolved the species: everybody gets the new
  body": the Creature / Cell evolve-in-place path (BUG-0508) runs for the Tribe -> Civ building and vehicle editors;
* 30 s after the City Hall editor the host's sync check reported `MISMATCH ... n: 46 apart`, the host's own NPC
  rows "here" at pos (0.0 0.0 0.0) with hp set (n51, n71, n73 ...).
No harm seen in the test (both games reached Civ); not analysed.
Reproduce: `tools/lab.py run tribe_civ --lane N`, read the host's log between the two editors.

## Root cause
Not analysed. Inferred: after the City Hall the game moves the Tribe's animals / NPCs for the Civ stage (or frees
their positions) while the stage still counts as Tribe; the sync check and evolve-in-place do not know this phase.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
