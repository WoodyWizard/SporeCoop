---
id: BUG-0499
title: play_creature: a player's puppet once 11-177 m from where its own game has it, puppet speed 0, with and without the effect relay
status: open
kind: desync
stage: Creature
reported: 2026-11-03
source: BUG-0471 A/B, lane 4, 2026-11-03
role: 
versions: 
fixed_in:
branch:
commits:
tests: play_creature
signature:
docs:
related: BUG-0471, BUG-0482
---

## Report
play_creature fails `sync p*: never more than 6 m apart` on one single sample of the ~140 per player:
the partner's puppet stands still (`puppet speed 0.00`) far from where its own game has the player,
while the player itself barely moved between the two reads (`host moved 0.00-0.14 m`). Seen:
- relay off (SPORE_FIX_OFF=creature.effects_relay), fix/w40-effects, lane 4: out/lab/20261103-124913
  play_creature#3 (p1 11.1 m, puppet to its target 3.65 m) in SporeCoop-wt/fix/w40-effects;
- main 0.7.3.6 without the relay: out/lab/20261103-000836 play_creature#1 (p0 23.0 m, to its target
  22.95 m) in the main checkout;
- the 0.7.3.6 candidate with the relay: 20261102-204139 (p0 176.8 m, to its target 48.8 m, at a
  respawn) and 20261102-230314 (11.0 m) in SporeCoop-wt/rel/0.7.3.6.
Every other sample of the same runs is within 1.7 m (p90 0.5 m). Not caused by the effect relay
(BUG-0471: it happens with the relay off and on main).

## Root cause
Not analysed. Inferred from the far line: the puppet is behind its target by more than the
distance it moves (speed 0: not driven that frame), so the sample falls in the moment after a
teleport of the player (respawn, the death caption, the guest's return from the main menu) before
the puppet is snapped to it. Next: `coop.track` around the far sample (docs/re/creature.md, BUG-0360).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
