---
id: BUG-0477
title: Civ and Space: shots and projectiles of the host's game are not on the guest's (tool projectiles, cultural, resource, beam, AoE areas)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: guest
versions: 
fixed_in: 0.7.5
branch: fix/w41-civ-combat
commits: df2c4fcb 724d9dad
tests: coop_civ_shots.py (civ_shots)
signature:
docs: docs/re/civ.md#civ-combat-shots
related: BUG-0529
---

## Report
Detector: `Civilization:noun_only_host:default_tool_projectile` 20% of Civ comparisons, `resource_projectile`, `cultural_projectile`, `default_ao_e_area`, `Space:noun_only_host:default_beam_projectile`, `host_only` near the guest. The guest sees the hit (damage is relayed) but not the shot. Effects relay (BUG-0451) does not cover projectiles. Class A (sync-architecture-plan).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (Ghidra GOG 2024 + runtime, lab 20261104-014831): every Civ weapon fires through cToolManager::FireTool,
called each update by cCombatSimulator::Update for a combatant with a target in range. A guest's mirrors run no
behaviour tree, so they never had a target and never called it: no projectile in a guest's game at all.
The Space half (`default_beam_projectile`) is not covered here.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`civ.shots`: the host's FireTool calls (squadron.cpp's hook -> civ::fire_observed) of listed vehicles and
turrets go to the guests every 0.2 s (shooter, aim, target: vehicle id or noun + position; MSG_CIV_STATE with
chunk 0xFE / chunks 0, which an older guest drops); the guest fires its mirror's own weapon with FireTool at
the target of its own game. `civ.shot_damage`: the hits of those copies are dropped in the vehicle, building,
city hall and citizen TakeDamage hooks (the host's state brings the damage).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
coop_civ_shots.py: FAIL with SPORE_FIX_OFF=civ.shots (guest projectiles in 0 of 45 host samples): out/lab/20261104-015446;
FAIL with SPORE_FIX_OFF=civ.shot_damage (guest city 5200 -> 4452 hit points): out/lab/20261104-015254;
PASS (cold, 45 of 45 samples, 102 / 104 projectiles, city 5200 -> 5200): out/lab/20261104-015632.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-04 (fix/w41-civ-combat): fixed for Civ vehicles (turret shots relayed the same way, not seen in a test).
- 2026-11-04 (fix/w41-civ-combat): detector, play_civ: `noun_only_host:default_tool_projectile` 81 % before
  (out/lab/20261104-013631), 3 % after (out/lab/20261104-024331); the guest now shows religious shots / AoE areas
  the host does not have (BUG-0530).
- 2026-11-04: released in 0.7.5.
