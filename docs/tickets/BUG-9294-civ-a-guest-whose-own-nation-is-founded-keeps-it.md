---
id: BUG-9294
title: Civ: a guest whose own nation is founded keeps its camera at the host's capital, its own capital out of view
status: fixed
kind: bug
stage: Civ
reported: 2026-11-21
source: integration campaign QA lane B (test/integration)
role: guest
versions: 0.7.15.94
fixed_in: unreleased
branch: fix/civ-own-camera
commits: 10487032
tests: agentB in-game runs (b_cam.py), make test
signature:
docs: docs/re/civ.md#civ-own-camera
related: BUG-9274
switch: civ.own_camera
---

## Report
Integration campaign QA (lane B, test/integration, planet Epina, 2026-11-20): after Tribe -> Civ the guest got its own
nation (BUG-9274, capital 522 m from the host's), but its game's camera stayed at the host's capital, where the load
put it: its own city was out of view and the guest had to find it on the map.

## Root cause
Observed: the Civ start points the camera at the player civ's capital (Civ_CameraHome 0xCFA570: SimCamera_LookAt on
the camera of SporeCoop::SimCamera_Get with the position at 0x169D2CC and the distance at 0x15819B4, then
SimCamera_Update), and on a guest the player civ of the loaded world is the host's nation; the guest's own nation
becomes its player nation later (civ_nations.cpp, the perspective), after the camera was pointed.

## Fix
civ_nations.cpp guest_tick (`civ.own_camera`): once the guest's game knows its own nation (g_own) in a world, and no
cinematic plays, its camera goes to that nation's first city: SimCamera_LookAt(camera, position, &CivCamera_HomeDistance,
1) and SimCamera_Update, as the game's own Civ start does; once per world (`coop.civ nations`: `own camera: fix= moves=
nation=`). Off: the camera stays at the host's capital (0.7.15.94).

## Verification
Lane B (Windows, Steam build), the integration build, planet Epina (Civ, the guest's own nation 0000235B):
- FAIL with `coop.fix civ.own_camera off` on the guest: after its join the guest's screen shows the host's capital
  ("[Player]", agentB/cam_off.png), `own camera: fix=0 moves=0`.
- PASS: switched on in the same session, the guest's camera went to its capital at (185 -371 286) at once
  ("[Bob]" centred, the host 521 m away, agentB/cam_on.png); after a rejoin (on from the start) the same
  (agentB/cam_rejoin.png). No fault (`exc`: total faults 0).
- no-fail-run: no lab here; FAIL and PASS by hand on lane B as above (agentB/b_cam.py).

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/civ-own-camera (from feat/lineages-civ).
