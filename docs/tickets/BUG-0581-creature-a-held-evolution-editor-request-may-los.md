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
branch: fix/w46-p4-creature
commits:
tests: editor_sync_host, editor_sync_host_late (coop_editor_sync.py: the BUG-0581 check)
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

Runtime check (fix/w46-p4-creature, 2026-11-07, rel/0.7.11): coop_editor_sync.py --case host / host_late now
checks the held request's budget on the game's own DNA. While the host's evolution editor is open the game's DNA
(GetEvolutionPoints) does not move (40 -> 40 with a mouth bought); back in the stage the game sets it to the result's
remaining budget (0xD445AA). With the request held by the vote (h_Submit, "vote active ... 60 s") it was the stage's
DNA minus the mouth's price, 40 - 25 = 15, in 6 of 6 runs (out/lab/20261107-211411 host_late #1, #2, host #1, #2;
out/lab/20261107-210859 host_late, host): the editor had the stage's budget. A lost budget message would leave the
editor's config start budget and the DNA after it would be that minus 25. Inferred: unlike the Cell editor (BUG-0560)
the creature editor does not depend on the posted 0x90A03FDF for its budget.

## Fix
None needed (not a bug): the check stays in editor_sync_host and editor_sync_host_late.

## Verification
no-fail-run: not a bug; the held Creature request's editor has the stage's budget: editor_sync_host and editor_sync_host_late
PASS out/lab/20261107-211411 (#1, #2 each: stage 40, back 15 after a 25 mouth).

## History
- 2026-11-04: opened.
- 2026-11-05: checked against recorded runs: the held Creature request keeps the stage's DNA (fix/w43-crashes).
- 2026-11-07: fix/w46-p4-creature: checked at runtime with a test on the game's DNA (6 runs): the held request's editor has the stage's budget; not a bug.
