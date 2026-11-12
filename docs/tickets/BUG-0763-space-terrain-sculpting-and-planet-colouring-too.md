---
id: BUG-0763
title: Space: terrain sculpting and planet colouring tools change the planet in the user's game only
status: fixed
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-b1-space (BUG-0749 exploration, 2026-11-08)
role: 
versions: 0.7.11 and earlier
fixed_in: 0.7.12
branch: fix/w47-b2-space
commits: f0808b2d
tests: tools/tests/coop_space_sculpt_tool.py
signature:
docs: docs/re/space.md#space-sculpt-colour
related: BUG-0749, BUG-0883, BUG-0900
---

## Report
Seen on fix/w46-b1-space (lane 6, `space_free`, 2026-11-08) while looking for a terraforming tool for BUG-0749: the
tool palette's 6th tab holds 9 terrain sculpting tools (raise / lower / flatten, with 7 sub-categories) and the 7th
tab 8 planet colouring tools plus a reset. The mod relays only the atmosphere / water / temperature deltas
(spacecoop.cpp h_TerraA/W/T, K_TERRA): inferred from the code, a sculpted crater or a recoloured planet appears in
the user's game only. Not run yet.

Reproduce: two games on `space_free`, both on planet 01460035; the host opens the tab at (W-226, H-151) and uses a
sculpting tool on the ground (click the slot, then the ground); compare the terrain height at that spot
(cPlanetModel::GetHeightAt) or a screenshot of both games.

## Root cause
* Observed (runtime lane 5 + Ghidra GOG 2024): a sculpting tool's hit posts message 0x4A32D09 {data[0] terrain
  effect id, data[10] Transform*} to the Space tool listener 0x1051860, which starts that terrain effect on the active
  terrain (cTerrainSphere vt 0x64, 0xFA71D0). A colouring tool's hit is cToolStrategy::OnHit 0x10593B0 calling the
  active terrain's SetColors (vt 0x74, 0xF9B3A0). Neither was hooked; the mod relayed only the atmosphere / water /
  temperature deltas (K_TERRA). Observed with the fix off: after the host's raise tool the guest's ground at the spot
  stayed 14 m lower, its colours (0, 0, 0) while the host's were (0, 11, 0) (lab 20261110-184850).

## Fix
spacecoop.cpp hooks the listener (message 0x4A32D09), cToolStrategy::OnHit (marks a hit of the player's own tool:
tool +0x114 == the ship's cGameDataUFO +0x34) and SetColors. After an own hit, the sculpting message (effect id + the
0x38-byte Transform) goes out as K_SCULPT and the SetColors arguments as K_COLOUR; a partner whose ship is on the same
planet repeats the same game call under g_applying. Switch `coop.fix space.terra_sculpt off` (nothing sent). Sculpting
or colouring done with no partner on the planet is still not shared: BUG-0900.

## Verification
coop_space_sculpt_tool.py (lane 5, real clicks: sculpting tab, first tool, ground twice; colouring tab, first colour,
ground; asserts the guest's GetHeightAt at the spot and around it, and the guest's GetColors):
* FAIL with SPORE_FIX_OFF=space.terra_sculpt: out/lab/20261110-184850 (guest +0.00 m vs host +3.65 m, 14.36 m apart,
  colours (0, 0, 0) vs (0, 11, 0)).
* PASS: out/lab/20261110-184527, out/lab/20261110-185144 (both +5.2 m, 0.00 m apart, colours equal).

## History
- 2026-11-08: opened.
- 2026-11-10: fixed on fix/w47-b2-space (sculpting message + SetColors relayed; switch space.terra_sculpt).
- 2026-11-11: released in 0.7.12.
