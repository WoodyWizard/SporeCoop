---
id: BUG-1525
title: Creature: a guest entering the host's Creature world gets 100 % Cell progress at once and no +50 DNA for legs
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.15.5
fixed_in:
branch: fix/w82-creature
commits:
tests: none (not a mod fault found)
signature:
docs: docs/re/creature.md#creature-meter-hold
related: BUG-1434, BUG-1291
---

## Report
Player report on Discord 2026-11-22 (coop 0.7.15.5, Steam; out/research/dc-report-20261122-world-switch.md section 5, the guest's own marks after the world switch): mark 3: "we instantly got 100 % progress on the Cell stage" (the world's Cell save was at food 1000) and the guest did not get +50 DNA for legs. No matching ticket found with `tickets.py find`. Next: fix-player-report (reproduce with a test that fails first).

## Root cause
* "100 % Cell progress at once": the host started Cell on a planet whose Cell save was already at its end (the guest at
  the load: `cell: DNA on entering Cell: food=1000 plant=646 spent=50 dna_for_food=65 budget=15`; the host's vote for
  Creature 80 s later): an old planet of the other player's galaxy, played from the co-op copy after the role swap
  (BUG-1434: copies hold only the played planet since 0.7.17, a notice tells whose copy it is since 0.7.16.1). The
  world's own state, not a mod change.
* "no +50 DNA for legs": no mod path gives or takes a DNA bonus for legs (no code names one). The guest entered
  Creature with the world's 65 DNA (as the Cell save's budget), spent 60 in its own editor into 0.7.15.5's shared pool
  (`dna: spent 60 here (65 -> 5): goes to the host`) and had the pool's 0 after the reload. Since 0.7.16 each player
  has its own wallet (BUG-1291). Whether the game gives the first Creature editor a bonus is not known.

## Fix
None.

## Verification
* no-fail-run: the progress was the loaded world's own (a finished Cell save); no mod path for a legs bonus exists to
  test. The world-switch cause is BUG-1434.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-C1), branch fix/w82-creature.
- 2026-11-23: analysed on fix/w82-creature (the report's logs): the world's own state and the shared pool of 0.7.15.5; closed (no-fail-run).
