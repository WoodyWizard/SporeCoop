---
id: BUG-0443
title: Creature: an egg at an NPC nest hangs in the air on the host (eggs have no gravity once asleep)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01 (host screenshot, video seen by the owner)
role: host
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w35-egg-kick
commits: 6f02d3b 86062d1f
tests: tools/tests/coop_nest_eggs_air.py (suite nest_eggs_air)
signature:
docs: docs/re/creature.md#creature-egg-gravity, docs/known-issues.md#issue-nest-eggs-floating, docs/06-testing.md#testing-nest-eggs-air
related: BUG-0384
---

## Report
The 0.7.3.3 report of BUG-0384 ("in enemy nests eggs are desynced and bugged (flying mid air)"): the
host's screenshot shows, at a nest, a green egg-like object hanging in the air over the slope next to the
mound, the partner's creature at the nest. The owner watched the player's video: the partner moved
normally and the egg was in the air from the start (not kicked up). BUG-0384 fixed the desync (the guest
had none of the host's eggs) and did not reproduce the floating; this picture is the host's own eggs.
(From the screenshot alone the object could also be a small creature of the green species swimming
nearby; the owner's reading of the video is that it is an egg.)

## Root cause
Observed (lane 4, creature fixture, `coop.nest lift` / `objs` over_surface, docs/re/creature.md#creature-egg-gravity):
* An asleep NPC egg has no gravity: an egg moved 2-2.5 m up (SetPosition, the call cHerd_LayEgg makes
  with the layer's position) stayed there for 10 s and more, freshly laid or settled; it fell only when a
  creature came to it and woke it.
* The game lays NPC eggs at the layer's position and does not snap them (only the avatar's eggs are
  snapped, cHerd_LayEgg 0xC6D0B0); the physics pushes the new egg out of the layer's body and the mound
  and lets it sleep there: with the fix off one in two eggs hung 0.3-0.5 m over the surface (5 nests:
  0.47, 0.33, 0.46, 0.43, 0.45; the others 0.04-0.21), eggs 2.5 m apart (not one on the other).
* Vanilla behaviour: the host's own eggs, the mod does not touch them in 0.7.3.3.
Inferred: the player's egg hung higher because of a bigger layer (or a layer in the air: on a rock, in a
jump, swimming); not seen.
Ruled out (observed): the partner's puppet put onto an egg pushed it up 0.14 m and woke another egg,
which fell (no launch); `GetHeightAt` is the same near and 450 m from the host.
Found on the way (not fixed, known-issues): eggs laid while the host is far from the nest (a guest keeps
the herd running, BUG-0280) lie inside the nest mound on the host (no mound collision far from the host's
camera) and on the mound on the guest.

## Fix
`nest.egg_ground` (mods/coop/nest.cpp): every 0.5 s the host checks the eggs of NPC herds (state 5 / 6)
within 60 m of its avatar (where the mound's collision is loaded); an egg that did not move for two checks
(asleep) and lies more than 0.3 m over the surface (`SporeCoop::cPlanetModel_SurfacePoint` 0xB82AC0, the
point cEgg_SnapToGround uses) is put onto it with cEgg_SnapToGround, what the game does to the avatar's
eggs and to every egg at its hatch. The guests' copies follow (nest.eggs moves them). Off: the eggs stay
where the physics left them (0.7.3.3).

## Verification
`coop_nest_eggs_air.py` (suite nest_eggs_air): the host's herd lays 2 eggs by itself; they lie on the
surface (<= 0.3 m); one goes 2 m up: 6 s later it lies on the surface on the host, every egg does, the
guest's copy lies on its surface.
* FAIL with `SPORE_FIX_OFF=nest.egg_ground`: out/lab/20261101-184259 (lane 4: a laid egg 0.43 m over the
  surface, the lifted one 2.43 m for 10 s; screenshot air_host: a green egg hanging in the air next to the nest).
* PASS: out/lab/20261101-184507 (lane 4, 2/2: laid eggs 0.0-0.05 m, the lifted one on the surface 1 s
  later, both eggs in the nest bowl on the screenshot).
* Regression (lane 4, gates on): out/lab/20261101-184846: nest_eggs, puppet_walk, puppet_slope, creature,
  team and the smoke suite 16/17 OK; unit failed on re_lint (the new symbol had no GOG 2016 frozen-off
  line), fixed (data/symbols/frozen_off_922af7c7.txt), `make test` passes after that.

## History
- 2026-11-01: opened.
- 2026-11-01: eggs without gravity found and fixed with `nest.egg_ground` on fix/w35-egg-kick (lane 4).
- 2026-11-02: released in 0.7.3.4.
