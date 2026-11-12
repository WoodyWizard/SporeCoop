---
id: BUG-0835
title: Cell: a guest's touch of a mirrored poison cloud counts as eating it, so the host's cloud ends early (suspected)
status: not-reproduced
kind: desync
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch: fix/w48-cell
commits: 3e554da8, e272f8c5
tests: cell_poison_touch
signature:
docs: docs/re/cell.md#cell-c1-0713
related: BUG-0716 BUG-0636
---

## Report
No player report. Suspected: a guest swimming through a mirrored poison cloud (an NPC's, the host's, or its own relayed one) makes the host remove that cloud.

## Root cause
Not reproduced (lab 20261112-065459, the code without a fix; and 20261112-062148 / -063438 with the guard tried): the
host's cell got a poison part (`coop.pool borrow poison graft`), the guest was put right onto 12 of the host's new
clouds as its game mirrored them (`coop.pool swimto c<key> tp 0`); 0 of 12 ended early in the host's game and the
guest's `coop.pool why` listed no destroy of any mirror at all: the guest's game never destroys a mirrored poison
cloud (the cull at 0xE78C37 removes the guest's OWN clouds, BUG-0716, not the mirrors), so the suspected ACT_GONE is
never sent.

## Fix
None. The guard tried (never count a poison cell or the cull's destroy as eaten, `cell.poison_not_eaten`, commit
3e554da8) was taken out again (e272f8c5): a fix whose test cannot fail proves nothing.

## Fix idea
In h_Destroy never count the poison-cull return address or type-5 (poison) cells as eaten.

## How to test
Host `coop.pool borrow poison graft`, guest swims through the host's clouds; guest `coop.pool why` must show no touched destroy from 0xE78C37 sent as gone, host no 'gone ... applied' on poison cells.

## Verification
no-fail-run: not reproducible; `coop_cell_poison_touch.py` (suite `cell_poison_touch`, extended) stays as a
regression test: it passes on the code without a fix (lab 20261112-065459).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-cell): not reproduced (12 touched clouds, no mirror destroyed), the tried guard taken out.
