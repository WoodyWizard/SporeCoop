---
id: BUG-9374
title: Creature: a player new to a galaxy era starts at the host's nest when its own nest is far away
status: open
kind: bug
stage: Creature
reported: 2026-11-24
source: deep playtest 2026-11-24
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tools/qa_win (manual, the deep playtest of 2026-11-24)
signature:
docs: docs/known-issues.md#known-creature-new-player-host-nest
related:
---

## Report
Deep playtest 2026-11-24 (Creature, a galaxy planet with a party of two): a third player entering the era made its
first creature and got its own nest 243 m from the host's (`coop.homes`: seat 2, "herd enabled 0"), but its creature
started at the host's nest, 5 m from the host. The second player's nest was 45 m away and it started there.
Reproduce: a third player enters a Creature era whose free nest is far from the host; `coop.avatar` against
`coop.homes`. Not analysed (the far nest's herd is not enabled when the player arrives).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-24: opened.
