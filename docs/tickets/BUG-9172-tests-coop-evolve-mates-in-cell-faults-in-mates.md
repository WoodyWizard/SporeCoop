---
id: BUG-9172
title: Tests: coop.evolve mates in Cell faults in mates_report (herd_nest of a cell)
status: open
kind: crash
stage: cell
reported: 2026-11-13
source: test
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN 2026-11-13: coop.evolve mates in a Cell save)
signature: coop.dll+0xCD0B3 read 0x3F800160 (game::herd_nest in mates_report)
docs: docs/06-testing.md#testing-qa-pitfalls-1113
related: BUG-9160
---

## Report
A test run asked `coop.evolve mates` (a test command) while the host game was in a Cell save: the game crashed (access violation in coop.dll, read of 0x3F800160).

## Root cause
Observed: mates_report read the "herd" field of g_local, which in Cell is the cell avatar, not a creature: it held the float 1.0 (0x3F800000), and game::herd_nest read +0x160 of it. Only the test command reads it this way.

## Fix
mates_report answers "not in Creature with an own creature (or loading, or in an editor)" outside Creature, when g_local is not the avatar, and while a load or an editor runs (not while the game pauses itself: Creature pauses at its start). No switch (a test command).

## Verification
no-fail-run: a test command. FAIL: crash-20261113-074543-i9 (TITAN, Diplomavil, Cell). PASS: the same command twice in the same Cell save with the fix: the message, the game alive; in Creature (Samuth) it lists the herd.

## History
- 2026-11-13: opened.
