---
id: BUG-1251
title: Creature: a fossil (interactive ornament) one player digs stays (model, glow) in the other's game, or is dug where the other sees none
status: open
kind: bug
stage: Creature
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
related:
---

## Report
A players' video: the guest sees bones the host does not and cannot click them; bones the partner dug stay and glow in the other player's game; the guest digs a fossil in empty grass on the host's screen. Suggested cause: worldobj.cpp's orn_use() marks the reward given (+0x24 = 1) and turns off clicking (+0xA3 = 0) but does not call cInteractiveOrnament_NextModel (a_OrnUsed, which the debug `use` command calls), so the skeleton stays as new; and ornaments have no drop_own.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
