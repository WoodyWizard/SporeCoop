---
id: BUG-0217
title: after the host visits an editor, the co-op epic is no longer tracked and goes into the shared save
status: fixed
kind: bug
stage: Tribe
reported: 2026-10-28
source: GitHub #151 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-world
commits: 374e0f5 0e80051 976af47
tests: tribe_editor_world (tools/tests/coop_tribe_editor_world.py), npc_tests test_stage_epoch
signature:
docs: docs/re/tribe.md#tribe-stage-epoch, docs/06-testing.md#testing-w5-world
related:
switch: tribe.editor_world
---

## Report
From GitHub issue #151: https://github.com/WoodyWizard/SporeCoop/issues/151 (the full analysis, links and the scenario are there).

**Summary**
The host opens the editor in the Tribe stage (an evolve vote, or the game's own editor entry). From then on the mod forgets the co-op epic creature and its herd, but the creature stays alive in the world. The world share that follows the editor saves that live epic into the host's save and every guest's copy. That is exactly what BUG-0121 says no longer happens. Eight minutes later a second co-op epic is spawned next to the untracked one. Severity: medium (wrong behaviour, mod objects in saves).

This is a regression in release 0.7.3.0. ba9dc46 (BUG-0081) and 421bfe0 (BUG-0121, BUG-0067) added `clear_coop_tables(false)` to `tribe::on_world_change()`. That function also runs when an editor opens, because coop.cpp treats "no local entity" as a world change. Before these commits (

## Root cause
Observed (lane 5, GOG 2024): the Tribe stage has no adapter for the editors, so every editor frame made `avatar_change_check` (coop.cpp) see a null local entity and call `tribe::on_world_change`, whose `clear_coop_tables(false)` dropped `g_epic` / `g_epic_herd` while the creature stayed alive. The share save after the editor then found `g_epic` null in `before_save` and saved the epic (`coop.tribe epic herds`: `live_epics` 2 after the share save and after the session end, `tracked=00000000`).

## Fix
* `game::stage_epoch()` / `stage_epoch_tick()` (game.cpp, rules in mods/coop/stage_epoch.h, unit-tested in npc_tests): one counter per stage, raised when the stage is entered from anything but its editors (kEdit, kEditor; kScenario keeps Space). Shared primitive for BUG-0223.
* `tribe::world_check()` at the top of on_frame (after the counter, before messages, BUG-0091 order): forgets the Tribe world when the mode leaves Tribe for a non-editor mode, or on a new stage counter / another player tribe object / another save name while in Tribe. `avatar_change_check` no longer calls `tribe::on_world_change` while the switch is on.
* `on_world_change` only forgets (`forget_world_tables` plus the member tables), no longer through `on_session_end` (whose restoring clear would touch the old world if the order changed).
* `coop.vote editor` works in Tribe (a member's species key), for the test.
* Switch: `coop.fix tribe.editor_world off` (the old trigger).
* Not done from the issue's design: step 0's frame-by-frame measurement of every reload route (editor then Done/cancel and a received world on the guest were seen, see docs), step 4 (the shared SaveGame hook: a refactor with no behaviour change, left to a later branch; BUG-0255 did not need it), step 5 (a save while a cinematic runs: the deferred destroy keeps the epic in that save; rare, the share save waits for the evolution cinematic).

## Verification
* FAIL: lab run 20261029-003144 (lane 5, `SPORE_FIX_OFF=tribe.editor_world`): tribe_editor_world failed (in the editor the epic untracked; `live_epics` 2 after the share save and after the session end).
* PASS: lab runs 20261028-232442, 20261028-235458 (after the merge of main with w5-tools) and 20261029-003648 (the final branch), lane 5.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-world (374e0f5 0e80051); test tribe_editor_world: FAIL 20261029-003144 with tribe.editor_world off, PASS 20261028-232442, 20261028-235458, 20261029-003648
- 2026-10-29: released in 0.7.3.1.
