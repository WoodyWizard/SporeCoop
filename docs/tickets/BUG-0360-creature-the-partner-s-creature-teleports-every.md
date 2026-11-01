---
id: BUG-0360
title: Creature: the partner's creature teleports every half second instead of walking (puppet driving restarts its walk)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: owner's manual test, lane 8, report SporeCoop-report-20261101-012349-i15
role: both
versions: 0.7.3.2
fixed_in: 0.7.3.3
branch: fix/w29-puppet-move
commits: c36b4c7, f3bb7cd, 0e499ab, 88221d9, aede668
tests: puppet_walk (tools/tests/coop_puppet_walk.py)
signature:
docs: docs/re/creature.md#creature-puppet-walk, docs/06-testing.md#testing-puppet-track
related: BUG-0281, BUG-0361, BUG-0362, BUG-0363, BUG-0053, BUG-0133
switch: creature.puppet_walk
---

## Report
The project owner played two games by hand (0.7.3.2, Creature, 2 players, one machine, both ~60 fps,
rtt 20-30 ms, 0 % loss): "the partner's character still teleports: sometimes it moves smoothly,
sometimes it teleports"; earlier: "I walked, and on the other screen it updated only about every half
second and not steadily; sometimes there was a walk animation and smooth movement, but mostly the
other player teleported". Seen on both sides, the partner's creature (not NPCs). The report zip
(host and guest logs) has only the 10 s trace lines: puppet shown, state age 0.0-0.1 s, rtt 17-34 ms,
nothing else.

## Root cause
Observed (coop.track: the puppet in the observer's game every frame, at the position the game drew):

1. While the partner walked, `drive::step` (driver.h) gave the puppet a MoveTo again every 0.1 s and
   pulled it towards the path with SetPosition. A MoveTo given every 0.1 s restarts the game's walk
   (BUG-0281: the creature stands or creeps); SetPosition is undone by the locomotion, which keeps
   its own position. In 0.1 s windows while the partner's path moved the drawn puppet stood
   (< 0.3x the path's speed) in 8-62 % and moved at 0.6-1.4x in 18-73 %; it trailed p50 0.9-1.9 m,
   p90 1.8-3.4 m, and the 4 m snap teleported it (0-4 jumps of 3.5-4.1 m per 25 s walk). That is the
   "updates about every half second, not steadily, mostly teleports". Its walk gait ran (94-100 %
   of the frames on screen): "sometimes there was a walk animation".
2. Interpolation (net/interp.h), the 20 Hz state rate and the clock offset were fine: the target
   path was smooth (0 % standing windows when the puppet is placed on it).

Found on the way (verified): the mod's frame runs at Present, after the game drew the frame, so a
position read after the mod's write is not what was drawn; teleport (vt 0x44) sets +0xB5D, which
restarts the animation's walk gait (a creature teleported every frame slides: gait 0 in 93-100 %);
StopMovement after a walk the locomotion did not follow put the puppet back up to 3 m.

## Fix
`creature.puppet_walk` (modes.cpp creature_drive): while the partner moves, the game walks the puppet
(SetDesiredSpeed = the partner's speed, a MoveTo 12 m ahead on its path given again only when the
direction turns > ~6 degrees, at most every 0.3 s, at least every 1.5 s) and `game::place` (teleport,
then +0xB5D = 0) puts it on the interpolated path with the partner's facing every frame; an error left
from standing is closed at 3 m/s faster than the partner. The end of a walk is a MoveTo to the stop
point (not StopMovement). Standing, the driving is unchanged.
`coop.fix creature.puppet_walk off`: the 0.7.3.2 driving.
Diagnostics: `coop.track` (every frame: drawn position, target, game speed, animation, walk gait,
on screen), `coop.status` shows `walk_goals`.

## Verification
`coop_puppet_walk.py` (suite puppet_walk): each player in turn walks ~25 s by WASD and clicks (within
12 m of the other), the other's camera keeps it on screen, its game samples the puppet every frame.
Checks: no frame step beyond 3x the walker's top speed, <= 5 % standing and >= 85 % matched 0.1 s
windows, walk gait >= 80 % of the frames on screen.
* FAIL with `SPORE_FIX_OFF=creature.puppet_walk`: out/lab/20261101-015527-fix-w29-puppet-move (2 of 2:
  standing 8-48 %, matched 34-64 %, 0-4 jumps of 4.0 m; drawn error p50 0.9-1.9 m). Earlier runs of
  the test: 20261101-014821, 20261101-010809.
* PASS: out/lab/20261101-015642-fix-w29-puppet-move (3 of 3: standing 0-2 %, matched 97-100 %, no
  jumps, drawn error p50 0.01-0.02 m / p90 0.02-0.04 m, gait 89-100 % on screen).
* Regression (lane 4): out/lab/20261101-015647-fix-w29-puppet-move: sync, creature, creature_desync,
  team, npc_run_hp ok; creature_npc_dash failed once, also with the fix off (1 of 3,
  20261101-021716; on 1 of 3, 20261101-021720): BUG-0363, not this change.
* Smoke (lanes 2, 3): out/lab/20261101-020130-fix-w29-puppet-move: 13 of 14; `unit` failed only in
  lab_selftest's lane-lock cases while this branch's other lab runs held lanes (make test alone passes).

## History
- 2026-11-01: opened.
- 2026-11-01: root cause found and fixed on fix/w29-puppet-move (creature.puppet_walk); BUG-0361 and
  BUG-0362 and BUG-0363 opened on the way.
- 2026-11-01: released in 0.7.3.3.
