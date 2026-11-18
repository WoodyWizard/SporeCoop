---
id: BUG-1257
title: Cell: the wild cells of the players' species are all the host's design (no second player species in the world)
status: open
kind: feature
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1240
---

## Report
A players' video: dozens of wild copies of the host's cell swim about, none of the guest's design. The host's game
fills the world (Simulator::Cell::PopulateTiles) from the world's one player species and the guest shows the host's
pool. A feature for the own species per player (0.8 Rivals), docs/known-issues.md#issue-cell-kin-host-species.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
- 2026-11-18: researched on plan/0.7.16 (docs/re/cell.md#cell-kin-species); planned as WP-L1 of docs/plan-0.7.16.md.
