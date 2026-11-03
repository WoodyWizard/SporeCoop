---
id: BUG-0497
title: Creature / Tribe: a fruit that fell in the host's game where the guest's tree slot was already empty is not shown on the guest (split from BUG-0472)
status: open
kind: desync
stage: Creature
reported: 2026-11-03
source: fix/w40-world-objects (coop.wobj missing counter, lane 9)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
worldobj.cpp binds the host's loose fruit to the guest's by position; when the guest's own tree had already dropped that slot (its own fall, before the host's), the host's fruit has no copy on the guest (`coop.wobj` guest `missing=` counter: 124 over 3 minutes of walking, lane 9). The mechanism does not make fruit (no spawn path is known: cFruit is made by the flora, 0xB09790 from a tree slot). Trees that grow fruit again (not seen) would not be copied either. Detector: `Creature:unmatched:fruit` 46% of comparisons with the fix (96% without). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
