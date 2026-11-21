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
commits: 10487032, cc4aa775, deb5f517, f7934445, 416f9c37
tests: tools/tests/coop_civ_own_camera.py (suite civ_own_camera), agentB/b_cam2.py (lane B, by hand)
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
civ_nations.cpp, switch `civ.own_camera` (on by default): once a guest's game knows its own nation (the perspective
of BUG-9274) and the Civ stage has been settled for 4 s (no load queued, no cinematic), outside the frame and once per
world, the guest's camera goes to its own nation's first listed city the way the game's own "next city" button does it
(the Civ message 0x3702D53 in 0xCF56A0): `SporeCoop::SimCamera_GoToPoint(SporeCoop::SimCamera_Get(),
SporeCoop::City_CenterPoint(city), true, true)`. The camera keeps its zoom and angle and only travels. Off: the camera
stays where the load put it (the host's capital). `coop.civ nations` prints `own camera: fix= moves= nation= at=
own_city= host_city=`. The first attempt (10487032, cc4aa775: SimCamera_LookAt with a float pointer as its second
argument) was taken back (deb5f517): that argument is a quaternion (docs/re/civ.md#civ-own-camera, CORRECTION).

## Verification
Lane B (Windows, Steam build, two instances 3/4), driver agentB/b_cam2.py on|off <planet>: it compares the guest's
viewer position (`coop.view`) with its own city and the host's city (`coop.civ nations`), along the ground.
- FAIL, `coop.fix civ.own_camera off` on the guest (Epina, 2026-11-21): the guest's viewer 441 m from its own city
  and 120 m from the host's: the camera at the host's capital (agentB/cam2_off_Epina.png).
- PASS, switch on (Epina, 2026-11-21): the guest's viewer 120 m from its own city and 484 m from the host's, the same
  height; the screenshot shows [Bob]'s city centred (agentB/cam2_on_Epina.png); the log line "the camera goes to our
  own city" once.
- FAIL, switch off (Kealeneon, 2026-11-21 09:10): the guest's viewer 413 m from its own city and 120 m from the
  host's (agentB/cam2_off_Kealeneon.png: the host's capital centred, [Bob]'s city on the horizon; the arrow keys
  move the camera, cam2_off_Kealeneon_after_keys.png). Output: agentB/cam2_off_Kealeneon.out.
- PASS, switch on (Kealeneon, 2026-11-21 09:14, where the LookAt attempt had broken the camera): the guest's viewer
  120 m from its own city and 314 m from the host's, the height 173 m (179 m off) and the angle to the ground the same
  as the load's (forward . up -0.84 both); [Bob]'s city centred (agentB/cam2_on_Kealeneon.png); the arrow keys still
  move the camera (cam2_on_Kealeneon_after_keys.png; then each of the four keys, three presses: 30 / 11 / 26 / 11 m,
  cam2_on_Kealeneon_after_keys2.png); one log line "the camera goes to our own city". Output:
  agentB/cam2_on_Kealeneon.out. No crash or fault report in either game.
- Fixture note: dev3's Kealeneon had been played by test/integration, whose party file (PTY2) this branch's
  feat/lineages-civ base cannot read, so the guest got party seat 1 while the nation roster had it in seat 3 (no own
  nation: a first OFF run "passed" with no own city, agentB/cam2_off_Kealeneon_nonation.out; the driver now requires
  the own city). For the runs the party file was moved aside and the roster entry moved to seat 1. build/amigos reads
  PTY2, so this is no bug of the merged code.
- Lab test tools/tests/coop_civ_own_camera.py (suite civ_own_camera: after the founding and after a new evening's
  load, the guest's camera at its own city; the arrow keys move it; twin SPORE_FIX_OFF=civ.own_camera). Not run: the
  Linux lab is not available here.
- make test passes (SKIP_SANITIZERS=1, WSL).
- no-fail-run: the Linux lab is not available here; FAIL and PASS above were run by hand on lane B.

## History
- 2026-11-21: opened.
- 2026-11-21: the LookAt fix taken back (Kealeneon: the camera sideways, then broken); open.
- 2026-11-21: fixed on fix/civ-own-camera with SimCamera_GoToPoint (the game's "next city" path); PASS on Epina and Kealeneon.
