---
id: BUG-0670
title: Civ: the host's own building designs resolve to the guest's own creations of the same id on the guest
status: open
kind: bug
stage: Civ
reported: 2026-11-07
source: found in fix/w46-tribe-civ (BUG-0647 analysis)
role: guest
versions: 0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0647, BUG-0469, BUG-0424
---

## Report
Code review in fix/w46-tribe-civ (BUG-0647): a building of a city on a guest is built by cCity::SetBuildingModel 0xBDB940, which reads the civilization's design key itself (docs/re/civ.md#civ-content-all-kinds). A design of the host's own My Creations (its city hall, house, factory made in the Civ editors) has a key the guest's game resolves to the guest's own creation of the same id, or to nothing: the guest sees other buildings than the host. Not reproduced in the lab yet (the fixture's designs are premades).

## Root cause
Inferred from BUG-0647 (vehicles: observed, lab 20261107-074609): the same key collision. The mod cannot pass another key to SetBuildingModel; a fix needs the host's building records registered under the original key above the guest's packages (as content.model_key does for substituted models) or the civ's design key rewritten on the guest for the build (and restored).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
