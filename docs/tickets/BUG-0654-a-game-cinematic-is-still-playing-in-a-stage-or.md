---
id: BUG-0654
title: A game cinematic is still playing in a stage or at a load and is ended by force (about 3000 lines, most runs)
status: duplicate
kind: bug
stage: All
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: 
branch: fix/w46-s1-sweep
commits: 915c0e22
tests: 
signature:
docs: docs/06-testing.md#testing-log-signals
related: BUG-0607, BUG-0626, BUG-0404
---

## Report
About 3000 lines `world: cinematic NNN still playing in a stage / at the load: ended it` in nearly every run on every branch (29 in rel/0.7.9 out/lab/20261106-200316/creature_carry.guest.game.log). Probably a handled recovery; find which cinematics they are and whether ending them by force hides or causes a player-visible problem. Related: BUG-0607, BUG-0626.

## Root cause
Observed (all lab runs 2026-11-06/07 on every worktree): 1579 "8915F9BB still playing at the load", 1038 "8915F9BB still playing in a
stage", 1 "F6A4B1E1 still playing before the held editor opens (BUG-0404)". 8915F9BB is the galaxy menu intro GGE_intro.
* At the load: world.cpp load_now ends whatever plays in the galaxy menu right before LoadGame; in the menu that is only the menu's
  own intro: the designed step (the intro must not survive into the stage, docs/re/space.md 7.9). Harmless: the load follows at
  once, nothing of a stage exists yet.
* In a stage: the intro started after a load (BUG-0607) and menu_intro_tick ended it; the player-visible problem it caused (a stuck
  view) is BUG-0626, fixed in 0.7.9 by world.intro_at_once: 0 such lines in the rel/0.7.10 and fix/w46-* runs (43 at-load lines,
  49 "would start outside the menu: not started").
* F6A4B1E1 before a held editor: BUG-0404's designed step.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
No behaviour change. The at-load case in the menu is now an INFO line "world: the galaxy menu intro (8915F9BB) ended at the load,
in the menu (BUG-0654)" (world.cpp end_menu_intro); any other cinematic, or the intro in a stage, stays a WARN.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: log-only signal; the in-stage half is BUG-0626 (fixed, its test tribe_intro_camera); counts above from the lab logs.
The new INFO line is seen in the regression runs of this branch (smoke).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
- 2026-11-07 (fix/w46-s1-sweep): duplicate; see Root cause / Verification.
