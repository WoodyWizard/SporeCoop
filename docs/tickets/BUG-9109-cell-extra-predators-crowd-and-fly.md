---
id: BUG-9109
title: Cell: extra predators per reserved seat crowd the pool and some fly off (spawned overlapping)
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
tests: QA solo host at 4 reserved seats (coop.pool enemy_groups, both switches), see Verification
signature: -
docs: docs/re/cell.md#cell-lineage-cells
related: BUG-9110 BUG-9111
---

## Report

"Cells would start flying around at times. I think they'd spawn inside each other and launch away."
And: with the extra enemies the pool feels crowded. Player decision for 43: from three reserved seats
on, a predator group gets ONE extra member (no extras at one or two seats); seats must still raise the
difficulty for a solo player.

## Root cause

`expand_marker` (cellsync.cpp) added `seats - 1` copies of each native predator encounter: three at
four seats, even with only the host online. It placed them on a ring at `max(2, 3 x scale)` and
skipped a site within `scale + other's scale` of any cell, using the cell's scale `+0x58` as if it
were its radius. The collision sphere is the model's radius (GetCellModelData `+0x1244`) times that
scale (`cell_sphere`), so the spacing test did not match the game's contact test.

Measured (solo host, 4 seats, swimming ~2 min, `coop.pool enemy_groups`):

| switches | groups | extras | native predators | extras touching a cell's sphere when made |
|---|---|---|---|---|
| old (`cell.extra_one off`, `cell.extra_spacing off`) | 16 | 48 | 79 | 2 |
| new | 40 | 40 | 195 | 0 |

No high speed was caught in these runs (top speed of extras 2.6 m/s, natives 2.6 m/s, sampled every
frame): the overlap is the likely trigger of the reported flights, not proven.

## Fix

- `cell.extra_one` (on): `CampaignRules::cell_extra_predators()` = 1 from three seats, else 0.
- `cell.extra_spacing` (on): sites on two rings at touching distance plus a gap (and twice that),
  every cell's true collision sphere excluded (`sphere_of`), plus a margin.
- `coop.pool enemy_groups` also prints `overlap`, `want`, `natives`, and the extras' / natives' top
  speed (`speed_extra=max/fast/samples`).

## Verification

no-fail-run: the flights were not reproduced on this PC; the overlap count (2 of 48 old, 0 of 40 new)
and the extras count (3 vs 1 per group) are the measured difference. Manual QA, no lab run id.

## History
- 2026-11-10: found and fixed on feat/lineages-43 (Windows QA copy, profile LineagesCellQA).
