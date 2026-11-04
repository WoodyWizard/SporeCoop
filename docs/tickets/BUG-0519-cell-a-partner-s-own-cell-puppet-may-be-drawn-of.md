---
id: BUG-0519
title: Cell: a partner's own cell (puppet) may be drawn off while a level change rescales the worlds
status: open
kind: desync
stage: Cell
reported: 2026-11-03
source: 
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0511
---

## Report
Found while fixing BUG-0511 (fix/w40-misc), not measured: the players' own cells travel in MSG_STATE (`modes::cell_fill` / `cell_drive`) in the sender's world scale, without the scale. A level change rescales each game's world over ~3.5 s at slightly different moments (docs/re/cell.md#cell-world-scale), so a partner's puppet is probably drawn off by up to the same 10+ m for those seconds (the NPC mirrors were 14-18 m off before BUG-0511's fix). Possibly the "visual glitch when growing" of BUG-0511's report. To reproduce: coop_cell_grow.py with the host's `coop.avatar` pos vs the guest's `puppet pos=` (coop.status), both times their world_scale.
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
