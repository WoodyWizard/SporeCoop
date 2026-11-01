---
id: BUG-0205
title: host in an editor or a loading screen: Creature proxies and Cell mirrors freeze, a goal reward reaching a host without a creature is lost
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: BUG-0151 rest (fix/w3-civ)
role: both
versions: main 5ae9744 and earlier
fixed_in: 0.7.3.2
branch: fix/w4-census, fix/w9-cell-away
commits: 5780f32, cc0e7bd, a5911a7
tests: coop_away_editor.py (suite away_editor; --nofix fails), coop_cell_away.py (suites cell_away, cell_away_editor; SPORE_FIX_OFF=pool.away fails)
signature:
docs: docs/06-testing.md#testing-census, docs/re/cell.md#cell-host-away, docs/06-testing.md#testing-cell-away
related: BUG-0151, #130
switch: core.away_editor, core.away_goal, pool.away
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

Cell (fix/w9-cell-away, GitHub #130):
- Root cause (observed, lane 3): with the host in the main menu the guest got "you play on alone",
  but no creature around it moved (0 of 14 moved more than 1 m in 6 s): every one was a mirror that
  `guest_tick` held at the host's last state, `h_Populate` / `h_SpawnLoot` were off on a guest and
  `h_Pair` skipped every pair without the guest's own cell. The 10 food points the guest ate were
  sent to a host that acknowledged them without counting (BUG-0239) and taken back at the host's
  first total after its return (guest 10 -> 0). A host editor or load did not count as away in Cell
  at all (BUG-0233 had left Cell out).
- Fix (`cellsync.cpp`, switch `pool.away`): while `g_host_away` is set a guest in Cell plays its own
  pool: the mirrors become cells of its game (AI lock +0x16C and keep-alive +0x354 cleared, the
  mirror tables emptied), its game populates, drops loot and runs every collision pair, and what it
  eats is kept (`g_solo_food`, meat / plant). States and totals the host sends meanwhile are ignored.
  When the away state ends, the host's first state round is applied as usual: its purge removes the
  guest's own cells as strays and the mirrors come back; just before it the kept food goes to the
  host as numbered meals (MSG_CELL_FOOD, at most 999 each), so the host's totals count them
  (BUG-0051). Leaving the stage or the session drops the kept food (logged, `dropped=`). `coop.cpp`:
  Cell is in `kAwayStages` (a host editor or load for 4 s is away), only with `pool.away` on.
  `coop.pool` prints `cell_away: solo= send_due= solo_times= released= solo_spawns= kept_meat= kept_plant= kept_sent= kept_lost=` (last, after the status line), and
  `coop.pool npcs [r]` the creatures around the own cell (tests).

## Verification
`coop_away_editor.py` (lane 7, GOG 2024): both evolve through the vote, the guest leaves its editor first, while the host edits the guest sends a goal reward of 25 DNA (`coop.social goal`) for a species the host's table calls allied (`coop.social record`); oracle: the host's game DNA after it is back. FAIL with `--nofix` (away_nofix2.log: host DNA 0 -> 0, host_away=0 on the guest while the host edited); PASS with the fix (away_fix4.log: 0 -> 25, goal_owed=1 then goal_owed_paid=1, host_away=1; also away_fix1-3). Logs in the scratchpad fix_w4-census.

The creature freeze itself is not checked by the test: after the guest's own editor its planet held no creatures to watch (proxies=0, its own 25 NPCs removed before the host counted as away; `coop.census` showed 0 placed creatures besides the avatar for 20 s), and a host reload (`coop.load`) on lane 7 was shorter than the 4 s away delay. The test prints both (`coop.census` positions 5 s apart). A case where the guest keeps proxies while the host edits alone (the guest's own editor not opened: pause menu, 90 s timeout) was not set up.

Cell (fix/w9-cell-away), `coop_cell_away.py`, lane 3, GOG 2024:
- Host in the main menu (suite `cell_away`): FAIL with `SPORE_FIX_OFF=pool.away` (lab
  out/lab/20261030-001911-fix-w9-cell-away: 0 of 14 creatures moved, host food 0 -> 0, guest 10 -> 0);
  PASS with the fix (out/lab/20261030-002054-fix-w9-cell-away: 11 of 12 moved, 570 cells released;
  back: 315 mirrors, all live host cells, no own cell left; host food 0 -> 10, guest 10).
- Host in its own cell editor (suite `cell_away_editor`: mating call, `coop.pool mate tp`, `coop.vote
  go`, the guest stays in its pool): FAIL with `SPORE_FIX_OFF=pool.away` (out/lab/20261030-003513-fix-w9-cell-away:
  the guest never counts the host away, 0 creatures moved, 10 points lost); PASS with the fix
  (out/lab/20261030-003818-fix-w9-cell-away: 7 of 7 moved, host food 0 -> 10, guest 10).
- Final code (a5911a7: the kept food also goes out before the host's first total, so the guest's
  total does not dip; checked by the test): PASS out/lab/20261030-005950-fix-w9-cell-away (cell_away,
  cell_away_editor; the guest's total never below 10 on the way back), with cell, cell_coop,
  cell_meat, cell_food_away. Earlier PASS of the area: out/lab/20261030-004000-fix-w9-cell-away
  (cell_sync, cell_evolve_poll, cell_leave, creations_cell, cell_rescue, cell_ko_gap,
  cell_editor_invuln, away_stages; cell_coop / cell_meat failed there on the first version of the
  `cell_away:` line, fixed in a5911a7).
- Not checked: a host load of another save in Cell (the guest's kept food then waits for a host
  that comes back to this world), 3 players, a lossy link. The Creature freeze of the first part is
  still not observed in a game (see above).

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w4-census): K_GOAL kept and paid on return (tested, FAIL/PASS), host in an editor or loading counted as away (implemented, effect on creatures not observed: see Verification), Cell left open. A `play_civ.py --seed 9` run with the change (host bot opened the city planner 5 times) was OK.
- 2026-10-28: switch: core.away_editor, core.away_goal (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
- 2026-10-30 (fix/w9-cell-away): Cell part fixed: the guest plays its own pool while the host is away (menu, and now a host editor or load in Cell too) and takes the host's pool again when it is back, its food kept and sent (`pool.away`, cell_away FAIL/PASS).
- 2026-11-01: released in 0.7.3.2.
