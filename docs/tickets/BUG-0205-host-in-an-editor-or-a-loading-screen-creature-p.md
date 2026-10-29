---
id: BUG-0205
title: host in an editor or a loading screen: Creature proxies and Cell mirrors freeze, a goal reward reaching a host without a creature is lost
status: open
kind: bug
stage: All
reported: 2026-10-28
source: BUG-0151 rest (fix/w3-civ)
role: both
versions: main 5ae9744 and earlier
fixed_in:
branch: fix/w4-census
commits:
tests: coop_away_editor.py (suite away_editor; --nofix fails)
signature:
docs: docs/06-testing.md#testing-census
related: BUG-0151, #130
switch: core.away_editor, core.away_goal
---

## Report
The part of BUG-0151 (GitHub #130) that fix/w3-civ did not fix. "Host away" (the guest plays on
alone, its NPCs simulated by its own game) means only "the host is in the main menu". While the host
is in an editor (an evolution or design change the guest does not join, the vote timed out) or on a
loading screen:
- Creature: the guest's own NPCs are still removed by the update filter, while the host sends no NPC
  states (its `npc_stage()` is false), so the proxies stand frozen.
- Cell: the mirrors stay AI-locked.
- A goal card's reward (`social` K_GOAL) that reaches the host while it has no creature is dropped
  (`social.cpp` K_GOAL handler requires `game::avatar()`). Plain DNA gains are kept since
  BUG-0151 (`g_dna_owed`).

How to reproduce: creature fixture, the host opens its editor alone (`coop.vote test` path or the
guest not ready), watch the guest's NPC proxies (`coop.npc` positions do not change).

## Root cause
Inferred from reading (BUG-0151 review): `host_away_tick` (coop.cpp) only switches on
`host_in_menu()`; the editor and loading modes are not counted. Widening it touches npc, cellsync and
the DNA flow (Creature, Cell and core areas), so it was left to those areas.

## Fix
Partly fixed on fix/w4-census (status stays open for the Cell part):
- K_GOAL (fixed, tested): a guest's goal reward that reaches the host without a creature is kept (`social.cpp` g_goal_owed, at most 64) and checked and paid in `social::tick` once the host's creature is back (same checks as before: the host's own relation table must say allied/extinct, once per species and outcome). Switch `coop.corefix away_goal 0`.
- Creature host away (implemented, not proven in game): `host_out_of_stage()` (coop.cpp) replaces `host_in_menu()`: besides the main menu (1.5 s) the host in an editor (kEdit/kEditor) or on a loading screen (kLoad) for 4 s counts as away, but not while this guest's own editor of the same vote is still due (`g_own_editor_due`, it opens up to ~20 s after the host's). The feed says "is in an editor" / "is loading". npc, tribe and civ follow `g_host_away` as for the menu. Switch `coop.corefix away_editor 0`.
- Not done: Cell. cellsync has no host-away state at all (also not for the main menu); the guest suppresses its own cell spawns and keeps the mirrors AI-locked. Giving a guest its own cell world while the host is away is a larger change of cellsync (spawns, mirror AI, the purge on return) and is left open.

## Verification
`coop_away_editor.py` (lane 7, GOG 2024): both evolve through the vote, the guest leaves its editor first, while the host edits the guest sends a goal reward of 25 DNA (`coop.social goal`) for a species the host's table calls allied (`coop.social record`); oracle: the host's game DNA after it is back. FAIL with `--nofix` (away_nofix2.log: host DNA 0 -> 0, host_away=0 on the guest while the host edited); PASS with the fix (away_fix4.log: 0 -> 25, goal_owed=1 then goal_owed_paid=1, host_away=1; also away_fix1-3). Logs in the scratchpad fix_w4-census.

The creature freeze itself is not checked by the test: after the guest's own editor its planet held no creatures to watch (proxies=0, its own 25 NPCs removed before the host counted as away; `coop.census` showed 0 placed creatures besides the avatar for 20 s), and a host reload (`coop.load`) on lane 7 was shorter than the 4 s away delay. The test prints both (`coop.census` positions 5 s apart). A case where the guest keeps proxies while the host edits alone (the guest's own editor not opened: pause menu, 90 s timeout) was not set up.

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w4-census): K_GOAL kept and paid on return (tested, FAIL/PASS), host in an editor or loading counted as away (implemented, effect on creatures not observed: see Verification), Cell left open. A `play_civ.py --seed 9` run with the change (host bot opened the city planner 5 times) was OK.
- 2026-10-28: switch: core.away_editor, core.away_goal (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
