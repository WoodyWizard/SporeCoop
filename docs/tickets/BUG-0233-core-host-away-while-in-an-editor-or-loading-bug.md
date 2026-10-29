---
id: BUG-0233
title: Core: "host away while in an editor or loading" (BUG-0205) fires in every stage and during the host's stage change, and guests' DNA kept meanwhile is lost or paid into another world
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #167 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-core
commits: c4f4981, 9452ccc, 0262f78
tests: away_stages (coop_away_stages.py; --nofix fails), validate_tests test_dna_route (fails with --nofix)
signature:
docs: docs/06-testing.md#testing-w5-core
related:
switch: core.away_stages, core.away_dna_stage
---

## Report
From GitHub issue #167: https://github.com/WoodyWizard/SporeCoop/issues/167 (the full analysis, links and the scenario are there).

**Summary**
Since 0.7.3.0, a guest counts the host as away once the host has been in an editor or on a loading screen for 4 s (BUG-0205, fix/w4-census, 2b90664). This goes wrong in three ways:
- The rule applies in every stage, also in Cell and Space, where no module acts on it. Those guests get "you play on alone" / "is back" notices for nothing.
- It also fires while the host loads the next stage.
- The DNA a guest earns meanwhile is sent to a host that is already in Tribe. There it waits in `g_dna_owed`, which has no world or stage tag. It is never paid there, but it is paid into a later Creature world if the host loads one in the same session.

Severity LOW: wrong notices, and a little DNA lost or put into the wrong world. The documentation side (CHANGELOG 0.7.3.0 lists BUG-0205 as fix

## Root cause
Confirmed on b423f56 by reading, and the first part in the game: `host_out_of_stage()` returned 'e' / 'l' for a host in an editor or a load in every stage, so a Space guest got "<host> (host) is in an editor: you play on alone" (observed with `coop.hoststate mode 1654C06 10`, lane 4). `host_away_tick` sent `g_solo_dna` with "is back" on any end of the away (also when the guest itself left the stage for a host already in Tribe), and the host's `g_dna_owed` had no stage or world: paid in any stage with an avatar, cleared only at the session end, so it could be paid into the next Creature save the host loaded (read, not run).

## Fix
- `kAwayStages` = Creature, Tribe, Civ: only there an editor or a load counts as away (the main menu still in every stage). Switch `coop.fix core.away_stages`.
- `dna_route.h` (portable): `solo_end()` decides at the end of the away: the host back in our world -> sent with "is back"; the host in world in the stage the DNA was earned in (`g_solo_mode`, BUG-0151: another save of that stage) -> sent, "plays again"; otherwise dropped and logged (`solo_dropped`). `owed()` on the host: kept in menus, editors and loads; paid in Creature with a creature in the world it was owed in (`g_dna_owed_world`, the last stage world when the first amount arrived); cleared (logged, `owed_dropped`) in another stage or world. Switch `core.away_dna_stage` (off: 0.7.3.0).
- Resets of the new state with the away state (session end, BUG-0085 path). `coop.status` has a new line `away dna: ...` (appended, existing lines unchanged).
- Test hook `coop.hoststate mode <hex> <s>` (our player states report that mode; cleared at the session end).

## Verification
- `coop_away_stages.py` (Space fixture, lane 4): the host reports the editor mode for 10 s; `--nofix` FAIL (the guest's toasts: "Alice (host) is in an editor: you play on alone ..."), PASS with the fix (none); control in both runs: the menu mode still gives "left for the main menu: you play on alone". Logs (standalone runs, lane 4): out/lab/20261028-235900-w5-core-twins/away_stages--nofix.log, away_stages.log.
- `validate_tests` test_dna_route: away stages, rows 9-11 and 13-15 of the issue's table; 5 checks FAIL with `--nofix`.
- Not run in the game: the DNA part (the stage change Creature -> Tribe during the host's load, and the host loading another Creature save with DNA owed). The issue's `coop_away_stage_change.py` needs an evolution to Tribe plus a second save on the host and was not written; the decision is the unit-tested function, called from `host_away_tick` and the MSG_DNA_ADD handler. Regression to run: `dna_away` (BUG-0151 menu case, row 9), `away_editor` (BUG-0205 Creature).

Lab PASS: out/lab/20261028-230144 (away_stages, dna_away, away_editor, tribe_away, civ_away ok, lane 4). FAIL twin: out/lab/20261028-235900-w5-core-twins/away_stages--nofix.log.

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w5-core): fixed: editor/load away only in Creature/Tribe/Civ (core.away_stages, game test FAIL/PASS), solo and owed DNA keep their stage and world (core.away_dna_stage, unit tests only).
- 2026-10-29 (fix/w5-core): after the merge of main 8f1944b: lab out/lab/20261028-235045 all passed (vote_end_editor, vote_drop_editor, away_stages, civ_other_world, civ_shared, dna_away, creature, civ_coop, fix_registry, leave_cleanup, unit).
