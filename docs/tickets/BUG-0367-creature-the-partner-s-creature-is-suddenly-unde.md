---
id: BUG-0367
title: Creature: the partner's creature is suddenly underground on the host's screen (0.7.3.3)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01, host F9 mark s 640 (later session)
role: host
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-puppet-jump
commits: dbadb92, 4e74918
tests: puppet_under (tools/tests/coop_puppet_walk.py --ground, coop.track lift -1)
signature: the drawn puppet under the observer's ground (coop.track hd < -0.3)
docs: docs/re/creature.md#creature-puppet-ground, docs/06-testing.md#testing-puppet-ground
related: BUG-0366, BUG-0360, BUG-0329, BUG-0013
switch: creature.puppet_ground
---

## Report
The same two players (see BUG-0366), a later session: the host marked (s 640) "friend underground
suddenly". The partner stood 16 m away at that trace (flags 0x1), walking and running in the 30 s
before (flags 0x3 / 0x7). The guest's log of that session is not in the report.

## Root cause
Observed (lab): the game does not lift a creature placed under its terrain. 0.7.3.3's puppet driven
to a target 1 m under the observer's ground (`coop.track lift -1`, walking and standing) was drawn
0.64-1.00 m under it in 1279 frames; a target over the ground is put down by the game.
Inferred (not reproduced naturally): how the players' target got under the ground. The ground is the
same on both games (0.00 m on 283 points, live Cell -> Creature and the fixture). The candidate is
the extrapolation of a stalled state (up to 0.25 s along the partner's velocity) while the partner
walks or runs downhill (rtt to 1.1 s and loss bursts in their logs), or a fall extrapolated past
its landing.

## Fix
`creature.puppet_ground` (BUG-0366): a target down to 3 m under this game's ground is put on it
(5 cm in; the game sets the puppet on its terrain).

## Verification
`coop_puppet_walk.py --dir guest --secs 25 --stand 8 --ground` with `PUPPET_WALK_CMDS="coop.track
lift -1"` (suite puppet_under): never drawn 0.3 m under the observer's ground.
* FAIL with `SPORE_FIX_OFF=creature.puppet_ground`: out/lab/20261101-082223-fix-w30-puppet-jump (1274 frames
  0.64-1.00 m under the ground).
* PASS: out/lab/20261101-083744-fix-w30-puppet-jump (2 of 2), out/lab/20261101-082323-fix-w30-puppet-jump
  (2 of 2 on the height: lowest drawn -0.05 m; #2 failed the walk check of BUG-0360, 77 % matched windows
  in a short walk, not the height: that check is noisy with 100-160 windows).

## History
- 2026-11-01: opened.
- 2026-11-01: mechanism reproduced and fixed on fix/w30-puppet-jump (creature.puppet_ground).
- 2026-11-02: released in 0.7.3.4.
