---
id: BUG-0384
title: Creature: eggs at other species' nests differ between host and guest and some float in the air
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01 (host F9 note at s 1907)
role: both
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w32-nest-eggs
commits: 2a77943
tests: tools/tests/coop_nest_eggs.py (suite nest_eggs), tools/tests/coop_nest_objects.py (fix/w30-npc-walk, not in a suite)
signature:
docs: docs/re/creature.md#creature-nest-eggs, docs/known-issues.md#issue-nest-eggs-floating, docs/06-testing.md#testing-nest-eggs
related: BUG-0332
---

## Report
A 0.7.3.3 session (2 players, Creature): the host noted "in enemy nests eggs are desynced and bugged
(flying mid air)".

## Root cause
First look (fix/w30-npc-walk, lane 5, `coop_nest_objects.py`): objects of noun 0x2C9CC91 at nests
differed between the games (14 / 13, 6 / 9, 12 / 8) and 4 of them were 3.0-4.1 m over the ground on BOTH
games; taken for the eggs (inferred).

CORRECTION (2026-11-01, fix/w32-nest-eggs): 0x2C9CC91 is cFruit (RegisterNounType in 0xB24720), the
flora's fruit pool (fallen and hanging fruit): each game drops its own, the high ones are at the same
places on both games (hanging fruit, inferred). The eggs are cEgg, noun 0x2A034CD
(docs/re/creature.md#creature-nest-eggs).

Observed (lanes 9/10, creature fixture, `coop.nest`): an NPC herd that misses members picks a living
member as its egg layer, which lays eggs at the nest (cHerd::Update 0xC6D960, behaviour 0xD6CDB0,
cHerd::LayEgg 0xC6D0B0); they hatch into new members 2.5-3 min later. A guest's own nest herds have no
members (the mod removes the guest's own NPCs), so the guest's game never lays them: with 2 eggs at
the host's nest the guest had 0. An egg eaten on one game (the eat node 0xDBD460, cEgg::SetState(4))
stays on the other in the same way. That is the "desync".

Floating: not reproduced (docs/known-issues.md#issue-nest-eggs-floating). Eggs in the nest's bowl lie
0.74-1.2 m over the terrain (the nest model; screenshots show them in the nest), a freshly laid egg is
up to ~1 m over the ground for a few seconds before it falls; nothing floated on the guest.

## Fix
`nest.eggs` (mods/coop/nest.cpp, new): the host sends the eggs of the NPC herds within 110 m of each
guest at 1 Hz (MSG_NEST 52: serial, position, state, scale, the herd's home); the guest shows each one
as a cEgg of its own (CreateInstance), whose mpHerd is the guest's own herd of the same nest (an egg is
drawn only with an enabled herd) without being in that herd's egg list (never hatches or is cleared by
the guest's game), placed at the host's position and snapped onto the guest's ground
(cEgg::SnapToGround 0xC6A2A0); the host's hatch / eat (state 3 / 4) is applied with cEgg::SetState
0xC6A810 and the egg goes when the host's goes. Our eggs are left out of saves (savestrip filter), go
with a world change, a session end or 3.5 s without snapshots (host away). An egg of ours eaten on the
guest is eaten on the host (MSG_NEST kind 1 -> SetState(4)). Off: each game keeps its own eggs (0.7.3.3).

## Verification
`coop_nest_eggs.py`: the nearest nest of another species misses 2 members (target + 2, respawn 2 s), the
host's game lays 2 eggs; both games list their egg objects near the nest; then the guest eats one and
the host another.
* FAIL with `SPORE_FIX_OFF=nest.eggs`: out/lab/20261101-122612 (lane 9: host 2 eggs, guest 0; the guest
  has no egg to eat).
* PASS: out/lab/20261101-122753 (lanes 9 and 10, 2/2: 2 / 2 eggs, each within 1 m, guest eggs 0.76-0.77 m
  over the terrain in the nest bowl, eaten on the guest -> state 4 on the host, eaten on the host -> state
  4 on the guest).
* Regression (lanes 9/10, gates on): out/lab/20261101-122949 creature, team, nest_species,
  creature_kills, npc_run_hp OK; migration failed on the known cold-start caught fault at 0x11ECE4F
  (before the session, docs/06-testing.md), rerun OK out/lab/20261101-123541. Smoke
  out/lab/20261101-123904: 11/14 (ui skipped: needs lanes 1-5); creature failed on BUG-0390 (the guest's
  hit lands on the host's family, open on main); unit failed on re_lint (cPlanetModel::GetOrientation
  only in the generated .sym: added to overrides), `make test` passes after that.

## History
- 2026-11-01: opened (fix/w30-npc-walk).
- 2026-11-01: the objects were fruit; eggs found (cEgg), the guest had none of the host's; fixed with
  `nest.eggs` on fix/w32-nest-eggs (lanes 9/10).
- 2026-11-02: released in 0.7.3.4.
