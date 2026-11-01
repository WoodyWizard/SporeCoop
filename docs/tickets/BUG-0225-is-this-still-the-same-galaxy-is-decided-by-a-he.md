---
id: BUG-0225
title: "is this still the same galaxy?" is decided by a heap address and a global save counter (allies mirror reuse, refunds paid into the wrong game)
status: fixed
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #159 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.2
branch: fix/w5-space, fix/w9-space
commits: c5d89f1, f3e0432, 344f82f, 48bb9a3, 1cc86fe
tests: space_allies_reload_addr (coop_space_allies_reload.py --same-address), space_trade_reload, space_trade_reload_same (coop_space_trade_reload.py), tests/space_tests.cpp owed action
signature:
docs: docs/re/space.md#space-object-serial, docs/re/space.md#space-owed-refund-identity, docs/06-testing.md#testing-space-world-identity
related: BUG-0038, BUG-0058, BUG-0226, BUG-0217
switch: allies.reload_id, space.owed_id, space.world_file
---

## Report
From GitHub issue #159: https://github.com/WoodyWizard/SporeCoop/issues/159 (the full analysis, links and the scenario are there).

Two Space fixes of release 0.7.3.0 decide whether the loaded game is still the one they knew by comparing the player's cEmpire pointer:
- the allies mirror reset after a reload (BUG-0038, d0f6003 and f581bf2);
- the owed trade refunds (BUG-0058, fd814d3), which also count every game save of any stage.
A new cEmpire at the old address hides a reload, and any save in between makes a refund payable even into a game that still has the items or into another game. Severity LOW: the fixes are incomplete, not worse than before them.

## Root cause
Allies part observed: with the new player cEmpire taken for the old one (`allies samegalaxy 1`, the address-reuse simulation) the old mirror cEmpire went back into the reloaded galaxy (lab 20261028-231426 with `allies.reload_id` off: 1D3E08D0 -> 1D3E08D0).
Trade part observed (fix/w9-space): the host gave 5 bomb charges (receiver on hold), left for the menu (the quit saves game A), loaded game B (a copy of the slot, no bomb), the guest left: 0.7.3.0 paid the 5 into game B (a new bomb with 5 there) and game A kept 7 (lab 20261030-001738). Cause: `refund()` pays at once whenever the player is in Space (no check of which game), and `pay_owed` paid when the cEmpire differed but any save of any game happened since. Also: the menu quit always saves, so the "reload still has it" row is not reachable with `coop.menu` (unit test only).

## Fix
Allies part (fix/w5-space): the mirror records `spaceext::world_epoch()` when its cEmpire is made and `reset_empire` resets when it differs, whatever the address (`allies.reload_id`).
Refund part (fix/w9-space): a WorldLedger (space_logic.h) tags each take with the Space world epoch and the game's save file key (`world::current_game_key()`: slot dir + planet, lower case) and records saves of that world (after the SaveGame hook returns, only while the Space world is open) and slots replaced by a received world (world.cpp hook). `owed_action` pays (same world, or the same file saved after the take), drops (the reloaded file still has it, the slot was replaced, the take's game unknown) or keeps it owed (another game now; 32 at most). Trade gifts/sales and spice gifts use it; a refund that comes back in Space while another world is loaded is owed, not paid. The world epoch also rises when the planet file changes in kSpace (`space.world_file`). Switch `space.owed_id` (off = the 0.7.3.0 trade rule and the 0.7.3.1 spice rule). `spaceext::saves()` stays only for the switch-off path. Not done: a save that fails to write is not detected (SaveGame returns nothing); the ledger is memory only.

## Verification
Allies: PASS out/lab/20261028-231750 (space_allies_reload_addr, space_allies_reload); FAIL with the switch off out/lab/20261028-231426.
Refunds: FAIL with `SPORE_FIX_OFF=space.owed_id` out/lab/20261030-001738 (space_trade_reload: game B got 5, game A kept 7); PASS out/lab/20261030-001937 (space_trade_reload, space_trade_reload_same, space_trade_refund, space_gift_refund, space_allies_reload, space_allies_reload_addr, space_diplo_mirror, tribe_epic) and out/lab/20261030-003001 (space_trade_reload x2). Unit test: tests/space_tests.cpp test_owed_action (the whole table).

## History
- 2026-10-28: opened.
- 2026-10-28: allies part fixed on fix/w5-space (Space world epoch, test space_allies_reload_addr); the trade part stays open: it needs the shared after-save hook of BUG-0217 (#151) and a two-game fixture.
- 2026-10-30: refund part fixed on fix/w9-space (world ledger, `space.owed_id`, `space.world_file`; tests space_trade_reload, space_trade_reload_same, unit test).
- 2026-11-01: released in 0.7.3.2.
