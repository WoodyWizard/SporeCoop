---
id: BUG-0697
title: Space / Cell: the partner's ship and the partner cell are drawn at full health on the other game (the sent health is not applied)
status: open
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0419 (0.7.11)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0419
---

## Report
From the K2a audit of BUG-0419. Space: space_fill sends the ship's health and max health, space_drive writes the puppet's own max into its health every frame (s.health never read): the partner's ship always shows full health. Cell: cell_fill sends the cell's health (+0x244), the puppet setup writes 6 and invulnerable: the partner cell never shows damage.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
