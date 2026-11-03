---
id: BUG-0508
title: every host evolution (Cell and Creature editor) sends the whole world again: the guest drops to the galaxy menu and reloads, although the new body had already reached it
status: open
kind: bug
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.4, 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0019, BUG-0457, BUG-0412, BUG-0232
---

## Report
0.7.3.4 forum post, the player's 'most annoying' problem: every time the host changes its creature in the editor, the guest's game syncs the world and loads again (goes to the main menu and joins back), although the world looked synced and the host's new creature was visible right before. The same player asks why the guest must accept (be ready for) opening the host's editor at all.
Log (guest, 0.7.3.4, report 20261102-231730, previous log 231251): 23:10:16 'host is back' from its Cell editor, 23:10:22 the guest receives the whole world (3.2 MB), 23:10:23 Cell -> Galaxy menu, 23:10:27 loads it again; same pattern after each host editor (Cell and Creature).
Target: 0.7.4 (Cell / Creature, a top complaint).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in the code (rel/0.7.4, coop.cpp on_mode_change): by design the host marks every guest needs_world after an editor (`evolved`: 'evolved the species: everybody gets the new body', 3 s after the cinematic, BUG-0019 / BUG-0232), and follow_phase sends the save; the guest loads it through the galaxy menu. 0.7.4 does not change this.
Inferred: the reload is needed only for what the creation records alone do not carry (the species record of the save, the player's body in Creature, BUG-0457). Options: send only the new creation + species (no save) when the guest is in the same world and stage; or load without the menu round trip. Size: M-L (world transitions, class BUG-0427).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened from the 0.7.3.4 forum post and its logs (w40 triage).
