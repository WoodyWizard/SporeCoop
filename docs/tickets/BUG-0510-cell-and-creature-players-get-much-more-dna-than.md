---
id: BUG-0510
title: Cell and Creature: players get much more DNA than in the single-player game (3-4x); a guest's meals arrive as one lump in the host's open Cell editor (+68, '100 DNA')
status: open
kind: bug
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: both
versions: 0.7.3.4, 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0351, BUG-0345, BUG-0466, BUG-0423
---

## Report
Three reports:
1. 0.7.3.4 forum post, follow-up: far too much DNA in the game, like 3-4x the default.
2. Host's mark (0.7.3.6, report 20261103-185644): 'I got 100 DNA in cell stage editor'; the screenshot shows 85 DNA in the Cell editor. Host log: at the editor request food=256 budget=19 (dna_for_food=19); 7 s after the editor opened '+68 food (DNA) from the guest's meals' (one +50 plant meal and three +6 meat meals sent by the guest while 'no host cell with it').
3. Direct message: the guest spawns in the Cell stage with its PREVIOUS cell creature (from another world) and that seems to give the host 100 DNA because of all the parts the guest has.
Guest log (report 20261104-005925): on joining Cell `dna: food 24 -> 170 cause=meal:255+146` (the host's total), its own cell is a creation of another world (BUG-0345); later in Creature `dna: 20 -> 78 cause=unknown` right after a world load.
Target: 0.7.4 (Cell / Creature).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the party's food / DNA is one shared total, every player's meals add to it (by design), so two players earn about twice as fast; the guest's meals that reach the host while the host is in its editor are credited at once into the editor's DNA (+68 at once). The 'parts give DNA' idea is not supported by the logs: the +68 are meals, one of them a +50 plant meal.
Inferred, not checked: (a) the game's Cell editor budget is meant to be the host's own progress; the shared meals make it 2-4x; (b) the +50 meal is a big plant chunk the guest's own (other-world, BUG-0345) cell can eat; (c) `cause=unknown` jumps after loads are the host's total applied to a guest. Decide the design (shared total scaled by the number of players, or only the eater's meals count toward the editor), then fix. Size: M.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage), three reports merged.
