---
id: BUG-0225
title: "is this still the same galaxy?" is decided by a heap address and a global save counter (allies mirror reuse, refunds paid into the wrong game)
status: open
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #159 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch: fix/w5-space
commits: c5d89f1, f3e0432
tests: space_allies_reload_addr (coop_space_allies_reload.py --same-address)
signature:
docs: docs/re/space.md#space-object-serial
related: BUG-0038, BUG-0058, BUG-0226, BUG-0217
---

## Report
From GitHub issue #159: https://github.com/WoodyWizard/SporeCoop/issues/159 (the full analysis, links and the scenario are there).

Two Space fixes of release 0.7.3.0 decide whether the loaded game is still the one they knew by comparing the player's cEmpire pointer:
- the allies mirror reset after a reload (BUG-0038, d0f6003 and f581bf2);
- the owed trade refunds (BUG-0058, fd814d3), which also count every game save of any stage.
A new cEmpire at the old address hides a reload, and any save in between makes a refund payable even into a game that still has the items or into another game. Severity LOW: the fixes are incomplete, not worse than before them.

## Root cause
Allies part observed: with the new player cEmpire taken for the old one (`allies samegalaxy 1`, the address-reuse simulation) the old mirror cEmpire went back into the reloaded galaxy (lab 20261028-231426 with `allies.reload_id` off: 1D3E08D0 -> 1D3E08D0). Trade part: confirmed by reading, not changed.

## Fix
Allies part only: the mirror records `spaceext::world_epoch()` (a Space-only stand-in for #151's stage epoch: +1 on the first kSpace frame after the Space world was torn down, or when the player cEmpire changes in kSpace) when its cEmpire is made, and `reset_empire` resets when it differs, whatever the address. Switch `allies.reload_id`. NOT done: the trade refund rule (`owed_action` with a game key and an after-save hook), which needs #151's shared SaveGame hook (BUG-0217, fix/w5-core) and a profile with two Space games; `spaceext::saves()` stays. The gift refund of BUG-0226 pays only into the same Space world (no save rule), which is safe but drops a refund after a load of a game saved after the gift.

## Verification
Allies: PASS out/lab/20261028-231750 (space_allies_reload_addr, space_allies_reload); FAIL with the switch off out/lab/20261028-231426.

## History
- 2026-10-28: opened.
- 2026-10-28: allies part fixed on fix/w5-space (Space world epoch, test space_allies_reload_addr); the trade part stays open: it needs the shared after-save hook of BUG-0217 (#151) and a two-game fixture.
