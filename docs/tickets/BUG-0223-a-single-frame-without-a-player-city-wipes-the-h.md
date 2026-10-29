---
id: BUG-0223
title: a single frame without a player city wipes the host's vehicle and city ownership in the middle of a game
status: fixed
kind: desync
stage: Civ
reported: 2026-10-28
source: GitHub #157 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-world
commits: e68f6b8 976af47
tests: civ_null_local (tools/tests/coop_civ_null_local.py), npc_tests test_stage_epoch
signature:
docs: docs/re/civ.md#civ-world-epoch-cinematics, docs/06-testing.md#testing-w5-world
related:
switch: civ.world_epoch
---

## Report
From GitHub issue #157: https://github.com/WoodyWizard/SporeCoop/issues/157 (the full analysis, links and the scenario are there).

**Summary**
Civ decides that the world changed when the stage adapter's local entity is null for a frame. That entity is only an anchor built from the player civilization's first city. When it is null for one frame in the middle of a game, `civ::on_world_change()` runs on the host and throws away the vehicle table, every city's owner (captures and gifts included) and the running speed boosts, while the world is still there. Severity: low. No trigger has been seen, but the result for players is large.

This was left open by BUG-0059 (ba9dc46, release 0.7.3.0), which added `civ::on_world_change()` behind that trigger. The same commit range also added reads of vehicle memory before the alive check in `host_tick()` (BUG-0031 7f066e4, BUG-0059 ba9dc46).

**Where**
- The trigger fires on any cha

## Root cause
Observed with a forced trigger (`coop.civ nulllocal 1`, lane 5): one frame without Civ's local entity made `avatar_change_check` call `civ::on_world_change` on the host: every vehicle got a new id (`next_id` 3 -> 5), the guest's game destroyed and rebuilt every vehicle (new object addresses), the guest's own vehicle lost its owner. No natural trigger is known (the anchor is the player civ's first city). Inferred by reading: `host_tick` read `pol` and positions of vehicles in `g_hv` before checking them against the game's list.

## Fix
* `civ::world_check()` at the top of on_frame (as BUG-0217's Tribe check, `game::stage_epoch(kCiv)`, leaving Civ for a non-editor mode, another player civilization object or save name). `avatar_change_check` no longer calls `civ::on_world_change` while the switch is on; the editor check inside it stays (same answer).
* `on_world_change` clears the tables itself instead of calling `on_session_end` (no +0x218 write into the old world).
* `host_tick`: the game's vehicle list first, `g_hv` pruned against it (alive check before the `pol` read), then the BUG-0031 orphan loop (still after `host_assign_cities`). No switch (identical for live vehicles).
* Test command `coop.civ nulllocal <frames>` (modes::test_null_local).
* Switch: `coop.fix civ.world_epoch off`. Not done: the issue's boost assertion (needs BUG-0222's +0x218 measurement) and the capture setup on civ_npc; the test checks the guest's own vehicle and the vehicle objects instead.

## Verification
* FAIL: lab run 20261029-003144 (lane 5, `SPORE_FIX_OFF=civ.world_epoch`): civ_null_local failed (new vehicle ids, the guest's vehicle objects rebuilt, its order lost).
* PASS: lab runs 20261028-232442, 20261028-235458 (after the merge of main with w5-tools) and 20261029-003648 (the final branch), lane 5.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-world (e68f6b8); test civ_null_local: FAIL 20261029-003144 with civ.world_epoch off, PASS 20261028-232442, 20261028-235458, 20261029-003648
