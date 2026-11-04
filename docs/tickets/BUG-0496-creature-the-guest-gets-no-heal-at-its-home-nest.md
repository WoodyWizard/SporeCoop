---
id: BUG-0496
title: Creature: the guest gets no heal at its home nest (split from BUG-0460)
status: open
kind: bug
stage: Creature
reported: 2026-11-03
source: player report 2026-11-02 (BUG-0460), split on fix/w40-world-objects
role: guest
versions: 
fixed_in:
branch: fix/w41-creature-left
commits:
tests: nest_heal (coop_nest_heal.py)
signature:
docs: docs/re/creature.md#creature-nest-heal
related:
---

## Report
The guest's mark in BUG-0460 (s 115): 'for me nest is not extinct and i don't get any heals from it'. The extinct half is fixed in BUG-0460 (wobj.herd); the heal at the home nest was not reproduced or analysed: which nest heals the guest's avatar (its family herd's nest, or the host's) and whether the guest's game runs that heal at all is not known. Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not reproduced (fix/w41-creature-left, 2026-11-03). Observed (lab 20261103-235806, nest_heal): in the creature fixture the guest's own game heals its avatar at the home nest like the host's (3.6 -> 16.0 full in 12 s; the host 3.2 -> 10.0), and neither heals 100 m away. Inferred: the player's case is another nest state (after a migration, or a nest the guest's game sees differently: the same mark says the nest was not extinct for the guest), not the heal itself. Next: repeat the test after coop_migration's nest move.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not reproduced in the fixture; coop_nest_heal.py stays as a regression guard (PASS out/lab/20261103-235806).

## History
- 2026-11-03: opened.
- 2026-11-03: not reproduced on fix/w41-creature-left (both players heal at the fixture's nest); test nest_heal added.
