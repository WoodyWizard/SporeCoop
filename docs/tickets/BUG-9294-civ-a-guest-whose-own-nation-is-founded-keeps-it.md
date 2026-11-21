---
id: BUG-9294
title: Civ: a guest whose own nation is founded keeps its camera at the host's capital, its own capital out of view
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: integration campaign QA lane B (test/integration)
role: guest
versions: 0.7.15.94
fixed_in:
branch: fix/civ-own-camera
commits: 10487032
tests:
signature:
docs: docs/re/civ.md#civ-own-camera
related: BUG-9274
switch:
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
None yet. A first fix (fix/civ-own-camera 10487032, cc4aa775: SimCamera_LookAt(camera, &capital position,
&0x15819B4, 1) + SimCamera_Update once the guest knows its own nation) was taken back: its arguments are not understood.
On Epina it happened to show the guest's capital from above; on Kealeneon the same call (4 s into a settled stage, too)
left the camera at the ground looking sideways at the sky, and trials with other distances and the Civ start's two
"zero" calls after it (0xB102A0, 0xB104A0) put the camera far off the planet (labels in a column on black) until the
guest's game was restarted. Findings (docs/re/civ.md#civ-own-camera): 0x169D2CC, the "position" the Civ start's
0xCFA570 passes, is compared as a vector against a city's position in 0xCFB959 (a stored home point, not a constant);
0x15819B4 is not data in the running game (mem.read shows instruction bytes there). Next: the game's own "go to a
city" (the Civ HUD's city list / the minimap) instead of the low-level camera.

## Verification
no-fail-run: open bug, no fix. FAIL seen on Epina (the guest's camera at the host's capital, agentB/cam_off.png).

## History
- 2026-11-21: opened.
- 2026-11-21: the LookAt fix taken back (Kealeneon: the camera sideways, then broken); open.
