---
id: BUG-0827
title: Cell: a guest swallowing a host cell whole may get the food while the cell lives on in the host's game (suspected)
status: open
kind: desync
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0636 BUG-0632
---

## Report
No player report. Suspected: the guest gets the meal of a whole swallow but the host's cell lives on and can be eaten again.

## Root cause
Code fact (line numbers of main e49e1e23 = 0.7.11.1): SwallowWhole 0xE7D880 (overrides_dc04aee5.sym:804) on a victim with a model plays 'cell_death_eaten' and event 7 instead of DestroyCell at once, the food (OnEat) comes at the end; h_Destroy (cellsync.cpp:1144-1174) counts a mirror destroy as eaten only within 1 s of the last avatar pair. Inferred: a later DestroyCell sends no ACT_GONE while the meal still goes out as MSG_CELL_FOOD 'no host cell with it'. Confidence: low (event 7 timing not traced).

## Fix
Not fixed yet.

## Fix idea
Hook SwallowWhole on the guest for mirrors and send ACT_GONE there.

## How to test
Guest carnivore two levels above a small host cell; host log must show 'gone ... applied' for every 'sent food' of a swallow (fix) vs 'no host cell with it' with no gone (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
