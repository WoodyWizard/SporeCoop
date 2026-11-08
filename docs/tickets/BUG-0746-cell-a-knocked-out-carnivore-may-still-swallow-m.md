---
id: BUG-0746
title: Cell: a knocked-out carnivore may still swallow meat (cell.ko_no_eat blocks EatFood and chunks, not the meat swallow of interaction kind 10)
status: open
kind: bug
stage: Cell
reported: 2026-11-08
source: G4 static analysis for BUG-0293 (docs/re/cell.md#cell-eat-gate)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md#cell-eat-gate
related: BUG-0364, BUG-0293
---

## Report
Inferred from static analysis only (no run): meat (cellType 2, no AI) touched by a carnivore mouth at a scale difference
of 0/1 is swallowed through interaction kind 10 (0xE7E020 -> 0xE7D880 SwallowWhole -> 0xE7D800 -> OnEat -> AvatarEat:
points), not through EatFood (event 0x12, plants) or the chunk bites (0x13/0x14). The mod's `cell.ko_no_eat` (BUG-0364)
blocks EatFood and the chunks, so a knocked-out (downed) carnivore drifting onto edible meat may still eat it and get
points (cell_ko_food would be the test to extend). Reproduce: a downed carnivore cell next to meat one level smaller.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
