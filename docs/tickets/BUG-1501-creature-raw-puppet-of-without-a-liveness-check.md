---
id: BUG-1501
title: Creature: raw puppet_of without a liveness check in player_rules.cpp (3 sites) (NET-5)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: unreleased
branch: fix/w80-net
commits: d21c3bea
tests: regression: creature_roar, creature_carry, carry_holder, flock_relay
signature:
docs:
related: 
switch: creature.rules_live_puppet
---

## Report
External static audit of 0.7.15.5, item NET-5 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (player_rules.cpp): `roar_on_host`, `carry_rx` and the K_FLOCK handler took the raw
`C.puppet_of(id)` (coop.cpp: `it->second.puppet`, no liveness) and called the game on it (is_dead, position, the roar
stimulus, Hold / Release, the flock summon). All three are behind `game::in_creature_stage()`, so the audit's Tribe / Civ
marker story does not apply, but a Creature puppet the game freed between two frames (BUG-0795 class, umbrella BUG-0426)
was used as it was. `puppet_live` (listed, its serial, this frame's world) was in the context and unused there.

## Fix
player_rules.cpp `relay_puppet(id)`: the three sites take `C.puppet_live(id)` (null when the raw one is not the live
object made for the player); `player_rules: live_puppet fix=.. refused=..` in the status counts raw puppets refused.
Switch `creature.rules_live_puppet` (COOP_FIX_UNSAFE: the off side may use freed memory).

## Verification
* no-fail-run: the failing case needs the game to free a puppet between two frames while a relay arrives (a race, not
  made on demand); the change is the same as effects.cpp's for BUG-0795. Regression night run (the relays still work with
  the live puppet): `SPORE_DETECTOR=20 python3 tools/lab.py run creature_roar creature_carry carry_holder flock_relay --lane N --retry 0 --no-build`.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fixed on fix/w80-net (d21c3bea); closed with no-fail-run.
