---
id: BUG-0279
title: Cell: only one of two new players can eat (Discord report)
status: open
kind: bug
stage: cell
reported: 2026-10-29
source: Discord report 2026-10-29
role: 
versions: 
fixed_in:
branch: fix/w7-eat
commits:
tests: coop_cell_new_game
signature:
docs: docs/re/cell.md#cell-new-players-food, docs/known-issues.md (BUG-0174 correction)
related: BUG-0278, BUG-0174
---

## Report
The same Discord report as BUG-0278 (2026-10-29, two players who had just bought the game; version,
build (GOG / Steam) and roles not given): "even at stage 1 only 1 of us could eat". Which player
(host or guest) and which food (plants, meat, the shared food bar not moving) is not known. The lab's
Cell tests (cell_coop, cell_meat, cell_net) pass on GOG 2024 with fixtures, not with a game the stage
wizard made right before joining.

## Root cause
Not confirmed (the report has no version, build, roles or food kind). What was tried (fix/w7-eat,
GOG 2024, lane 9, `tools/tests/coop_cell_new_game.py`: both profiles with no saves, no creations, no
choices.ini; the host makes a new game through the stage wizard; the guest joins from its menu; both
play with the play_cell bots through real input, then each swims to food of its diet):

| run | build | diet | join | host ate | guest ate | result |
|---|---|---|---|---|---|---|
| manual seed 1 | fix/w7-eat | herbivore | after the intro | 72 points | 72 points | both eat |
| manual seed 2 | fix/w7-eat | carnivore | during the intro | 147 | 15 (the bot found little meat, 100+ m from the host) | both eat |
| manual seed 3 | fix/w7-eat | carnivore | during the intro | 72 | 144 | both eat |
| manual seed 4 | **v0.7.3.0** (the release) | carnivore | during the intro | 60 + 5/6 probes | 99 + 5/6 probes | both eat |

Observed in all runs: the guest plays the host's species (no creations of its own: the same premade
cell, the same mouth, `coop.pool diet`), both machines show the same food bar, what the guest eats
is gone on the host, a carnivore cannot eat plants and a herbivore no meat (vanilla rules, the same
for both players). Joining in the intro changes nothing.

Observed, the likely cause: **a guest more than ~45 m from the host finds no food at all**. The
host's game populates and keeps only the tiles around its own cell (PopulateTiles, about one screen
at level 2) and the guest spawns none of its own (BUG-0174, whose "~100 m" was too optimistic):
holding W away from the host the guest saw 41-51 food cells on its screen up to 42 m and 0 from 44 m
on (55 ... 115 m), the host 6-9 all the time. Two new players who swim off on their own get exactly
"only one of us could eat": the host eats, the guest finds an empty pool. Inferred: not proven to be
what the players hit.

Read, not run: a 0.7.2.x guest with a 0.7.3.0 host (or the other way round) eats the same way: the
eat message carries the food since 0.7.2 and the host reads both forms; a host without meal numbers
makes the guest take the host's total (BUG-0219 trim fix); nothing there blocks eating. The Steam
build runs the GOG 2024 code after SteamStub (docs/05-re-notes.md 5.1.2): the same hooks, or co-op is
off as a whole (a different hash), never half of the eating.

Facts needed from the players: the mod version (co-op window footer), GOG or Steam, who was the host
and who could not eat, plants or meat (and herbivore or carnivore), whether the one who could not eat
was far from the other, and the "Report a problem" zip from the co-op window (both players).

## Fix
None yet. The fix of the likely cause belongs to BUG-0174 (the host populates around each partner,
or a far guest spawns its own food: ideas in docs/re/cell.md#cell-new-players-food).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
- 2026-10-29: fix/w7-eat: not reproduced with new profiles, a new game from the wizard, both diets,
  an early join, on this branch and on v0.7.3.0; the far guest's empty pool (BUG-0174) measured at
  ~45 m. Left open until the players answer.
