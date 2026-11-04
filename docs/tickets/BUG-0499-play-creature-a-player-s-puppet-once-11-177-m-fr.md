---
id: BUG-0499
title: play_creature: a player's puppet once 11-177 m from where its own game has it, puppet speed 0, with and without the effect relay
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: BUG-0471 A/B, lane 4, 2026-11-03
role: both
versions: 
fixed_in: 0.7.4
branch: fix/w40-play
commits: d0635944, 9d0c9b48
tests: puppet_jump, play_creature
signature:
docs: docs/re/creature.md#creature-chat-under-caption
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
Observed: every far sample came right after a death of the far player (`"You were killed" was up` 2-5 s
before in play_creature's notes, its game log `alive again` / `the hatchling waited ... grows up now`). After
a death the player's own game moves its creature at once twice: the respawn at the nest (100+ m, while the
puppet is hidden) and ~10 s later the hatchling out of its egg (5.6-6.0 m in one frame at the fixture's
nest, 10-11 m in 124913#3). The partner's puppet buffer (net::SnapshotBuffer) blended across both steps:
the target slid there in 0.1 s and the puppet walked after it (coop_puppet_jump.py with the switch off: 0.29
s to be within 1 m of where the target landed after a 6 m jump, out/lab/20261103-154101); at the respawn
the blend from the dead state at the place of the death put the shown puppet's first target between there
and the nest (0.35 m off at the fixture's nest where both places are close; 23 m in 000836#1). One of
play_creature's ~140 samples per player fell into such a moment.
Inferred: the 177 m case (20261102-204139, `at a respawn`) is the respawn blend over a longer way.
## Fix
Switch `creature.puppet_jump` (modes.cpp, coop.cpp drive_puppet, net/interp.h): the partner's buffer does not
blend across a step faster than 40 m/s or a change of the death flag, and a target that moves more than 3 m
in one frame teleports the puppet there (drive::State::jumps, `jumps=` in coop.status). Off: blended and
walked (0.7.3.6).

## Verification
puppet_jump (tools/tests/coop_puppet_jump.py): each player dies, the other records its puppet (coop.track).
- FAIL with SPORE_FIX_OFF=creature.puppet_jump: out/lab/20261103-154101 (6.0 m jump out of the egg, the puppet
  there after 0.29 s > 0.15 s);
- PASS with the fix: out/lab/20261103-154306 (0.03 s; shown again 0.00 m from the target).

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-play (no blend across the respawn and the egg jump, the puppet jumps too).
- 2026-11-03: released in 0.7.4.
