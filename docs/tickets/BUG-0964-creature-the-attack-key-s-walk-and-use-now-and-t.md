---
id: BUG-0964
title: Creature: the attack key's walk-and-use now and then never moves the guest's creature (presses do nothing for 12 s): the tests' gopos put it inside a nest mound
status: fixed
kind: test
stage: Creature
reported: 2026-11-12
source: fix/w48-pass lab 2026-11-12 (partner_pass)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-walk
commits: 18a57a2a, d71d5660
tests: walk_start, walk_start_solo, walk_start_walkin, partner_pass
signature:
docs: docs/re/creature.md#creature-partner-pass-rel, docs/re/creature.md#creature-walk-held-in-mound, docs/06-testing.md#testing-walk-start
related: BUG-0913, BUG-0965, BUG-0400
switch: tools.gopos_surface
---

## Report
In `coop_creature_partner_pass.py` the guest's creature, put by the test (`coop.npc gopos`) 4.5-8 m from an animal it
has as its combat target (combat stance, key 1 status 0), does not move at all after the attack key: 4 presses in
12 s, moved 0.0 m, the game's gap unchanged, its cLocomotiveObject desired speed 4.75. Seen with creature.partner_pass
on and off, on lanes 6 and 7, mostly at the first small animal of a herd (lab 20261112-120457 r9, -122259 r10,
-130029 three times in one round). A player would see the attack key do nothing until it moves by hand.

## Root cause
Not the game's walk-and-use and not the mod's play code: the tests' teleport. `coop.npc gopos` lifted a point only
onto GetHeightAt, the bare terrain (BUG-0400). Near a herd's nest that is under the nest mound, whose
collision stands 0.55-0.65 m higher (a known testing pitfall: a nest mound is not in GetHeightAt). A creature put there
is inside the mound's collision and Havok holds it in every direction.

Observed (lab lanes 2 / 3, `coop_creature_walk_start.py`, docs/re/creature.md#creature-walk-held-in-mound): about 1
press in 5 after a gopos 6 m from an animal near the fixture's nests, for the guest, for the host, for the host's game
alone without a session and with every fix of the mod off (8 of 30, lab 20261112-141120). In each such press the
game's walk-and-use node 0xD2F1ED12 ran, the request and the steering pointed at the target with the facing (cos
1.000), desired speed 4.75, and the velocity stayed 0. A plain WalkTo 3 m to the side, back or ahead did nothing
either. The creature stood 2.0-4.3 m from a nest's centre at the terrain's height with the game's surface (the ray cast
onto the mound) 0.55-0.65 m above it. Put 2 m higher at the same spot it fell onto the mound and the next press walked
it to the animal (7 of 7). Without a teleport (the game walks the creature to each point, every fix off, no session)
12 of 12 presses walked (lab 20261112-143856-2): a player, who walks onto a mound, does not get there. The "stuck at
the same herd for several rounds" of the partner_pass runs is the same spot near the same nest.

## Fix
coop.cpp (`tools.gopos_surface`): `coop.npc gopos` puts the point onto the game's surface (`game::surface_radius`,
cPlanetModel_SurfacePoint: the terrain, then the ray cast onto the mound's or a rock's collision) when it is under it,
except within 2.5 m of another creature (the ray cast hits creatures: it would land on its back). `gonpc` already drops
the creature from 2 m over the NPC. Off: `coop.fix tools.gopos_surface off` (SPORE_FIX_OFF=tools.gopos_surface).
Test only: no CHANGELOG line (a player never meets it). Diagnosis tools: `coop.abilities walk [raw]`, `walkto`,
`target <ptr> any` (debug.cpp).

## Verification
`coop_creature_walk_start.py` (suite `walk_start`, ~3 min): 16-20 animals near the fixture's herds, the guest put 6 m
from each with gopos, the target set with the game's SetTarget, the attack key pressed: the guest's avatar must move
in its own game every time.
* FAIL with `SPORE_FIX_OFF=tools.gopos_surface`: lab 20261112-143501 (4 of 16 held, each inside a mound: surface
  0.55-0.65 m above, a plain walk 0 m every way, 2 m higher it walked).
* PASS: lab 20261112-143501-2 (15 of 15), 20261112-143856 (16 of 16).
* The game's own path (no teleport, every fix off, no session): `walk_start_walkin` 20261112-143856-2 (12 of 12).

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-walk): cause found (the tests' gopos under a nest mound), fixed in the test tool
  (tools.gopos_surface), test walk_start; FAIL and PASS runs in Verification.
