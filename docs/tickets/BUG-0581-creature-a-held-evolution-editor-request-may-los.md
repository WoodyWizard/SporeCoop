---
id: BUG-0581
title: Creature: a held evolution editor request may lose its DNA budget message like the Cell one (BUG-0560)
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-04
source: found by W1 (fix/w43-world-state, RE of BUG-0560)
role: host
versions: 0.7.6
fixed_in:
branch: fix/w43-crashes
commits:
tests:
signature:
docs: docs/re/creations.md#creations-creature-editor-budget
related: BUG-0560
---

## Report
Found while fixing BUG-0560 (not seen by a player yet): the Creature stage's editor request posts the same budget
message 0x90A03FDF after EditorRequest::Submit (0xD455FD, +8 = (int)GetEvolutionPoints). In co-op the host's request
waits for the evolution vote (coop.cpp h_Submit), so the game's post would reach no editor, as in Cell.

## Root cause
* Observed (Ghidra, GOG 2024): the post at 0xD455FD after Submit, the same message the Cell editor's listener takes.
* Inferred: a held Creature request opens the editor with the config's start DNA; the mod's creature DNA paths
  (dna_ledger) may hide it. Not checked at runtime. The BUG-0560 fix (cell.editor_budget) re-posts for Cell only.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not a bug; the recorded held-request runs named in Root cause show the stage's budget.

## History
- 2026-11-04: opened.
- 2026-11-05: checked against recorded runs: the held Creature request keeps the stage's DNA (fix/w43-crashes).
