---
id: BUG-0377
title: creature: in npc_far / npc_pooled the guest shows the host's puppet about 10.7 m from the host (sync-check p0 apart)
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
role: 
versions: 
fixed_in: 0.7.3.4
branch: fix/w31-creature-gate
commits: 1cfccdd
tests: npc_far, npc_pooled (tools/tests/coop_npc_far.py, coop_npc_pooled.py)
signature: sync-check: MISMATCH ... p: 1 apart (p0 10.7) in npc_far / npc_pooled
docs: docs/06-testing.md#testing-walk-on-land, docs/re/creature.md#creature-deep-water
related:
---

## Report
`lab.py gates` over recorded results: npc_far 15 of 15 passed runs and npc_pooled 9 of 11 have `sync-check: MISMATCH ... p0 10.7` apart on the guest. Consistent, not triaged. Reproduce: `lab.py run npc_far` with the gates on.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
A test artifact, not a co-op bug. Observed on lane 6 (GOG 2024, 2026-11-01):
* The tests walked the host in a fixed direction at radius R = 510 m (`coop.npc gopos` on a sphere).
  450 m (300 m) that way is the sea of the creature fixture (terrain 484-497 m, water surface 499.2 m).
* After ~6 steps the host's game started the deep water caption cinematic 5E205931 ("Dangerous
  creatures lurk in the deep. Don't stray too far from shore!", screenshot, `coop.status`: `cinematic
  5E205931 playing`, the log's `perf: ... | Creature, cinematic`, `input_lock=1`). It holds the host's
  avatar: from then on the game no longer moved it (velocity frozen at (1.43 3.44 3.19), the trace's
  flags 0x3), so each later teleport left it where it was put: 510 m, 25 m over the sea floor and
  10.8 m over the water.
* The guest's game held the puppet at the water surface: `coop.track` per frame on the guest: the
  drawn position (`d`, after the game's update) at 499.2 m every frame, the mod's write (`p`) at
  510.0 m, a teleport every frame (`tp` +1 per frame). Put at 494.9 / 486.9 m the puppet stayed
  there; anything above 499.2 m went to 499.2 m in one update.
* sync-check and coop.sync read the puppet between frames (after the game's update), so they saw
  the drawn 499.2 m: `p0 10.7`. Drawn and checked agree; the host's own game had an avatar in the
  air that a player cannot have (only a test teleport puts it there).
Inferred: in a real game a swimming host stays at the water surface on both machines.

## Fix
The tests (no mod behaviour changed, no switch): `coop_npc_pooled.land_path` picks, of 16
directions, the one whose lowest ground along the walk is highest (`coop.abilities ground x y z`,
new: the ground radius in a direction, no move) and asserts it is over the water (SEA 499.2); the host
is put onto the ground at each step (`coop.abilities goto`). Both tests now also check what the guest
draws: the puppet's position after the guest's game update (`coop.track` column d) within 3 m of
the host's own avatar.

## Verification
FAIL: npc_far before the change, lab 20261101-092616 (lane 6, gate sync-check: 3 MISMATCH lines, `p0 10.7`).
PASS: npc_far + npc_pooled, lab 20261101-094034 (lane 6: the walk 5.8 m or more over the water, the
guest draws the host 0.3 m from where it is, no MISMATCH in the four game logs); again in the
regression run of the branch, lab 20261101-100420.

## History
- 2026-11-01: opened.
- 2026-11-01: fixed in the tests (fix/w31-creature-gate): the walk went into the sea, the deep water cinematic froze the host in the air.
- 2026-11-02: released in 0.7.3.4.
