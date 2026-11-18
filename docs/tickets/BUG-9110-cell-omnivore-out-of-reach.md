---
id: BUG-9110
title: Cell: with extra predators an omnivore is almost out of reach (many bodies, little plant food)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-10
source: player report (roadmap 43 decisions)
role: host
versions: 0.7.3.6-lineages42-dev
fixed_in: 0.7.3.6-lineages43-dev
branch: feat/lineages-43
commits: 0221025e
tests: QA solo host at 4 reserved seats (coop.pool enemy_groups plant/meat counters), see Verification
signature: -
docs: docs/re/cell.md#cell-lineage-cells
related: BUG-9109
---

## Report

"With the increase of enemies it's an increase of food for a carnivore / omnivore. Once so many
enemies get added, it's almost impossible to become an omnivore at the end, due to so many bodies and
so little herbivore food. We can reduce the spawning slightly, but we should equally increase the
herbivore food." The Cell diet at the end of the stage follows the share of meat and plant eaten.

## Fix

BUG-9109 brings the extras down to one per group (about +20% predator bodies, measured 40 extras for
195 native predators; before about +60%). Plant food is now worth 25% more while that extra spawns
(`CampaignRules::cell_plant_percent()`, 3+ seats): `CellCredit::award` multiplies a plant meal by it
before the seat divisor, so the native totals get more plant points, meat unchanged. The credit file is
unchanged (only the remainders are saved). `coop.fix cell.plant_bonus off`: plant food as before.

## Verification

Solo host, 4 seats (progress 280%): plant 50 raw -> 22 paid (50 x 1.25 / 2.8 = 22.3), meat 36 raw -> 12
paid (36 / 2.8 = 12.9). `tests/win_tests.cpp` keeps the old award (plant_percent defaults to 100).
no-fail-run: a balance change; the end-of-stage diet was not played through.

## History
- 2026-11-10: fixed on feat/lineages-43.
