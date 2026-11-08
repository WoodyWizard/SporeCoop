---
id: BUG-0749
title: Space: no test of terraforming with a real tool
status: fixed
kind: desync
stage: Space
reported: 2026-11-08
source: BUG-0199 (fix/w46-g4b-coverage, 2026-11-08)
role: 
versions: 0.7.10 and earlier
fixed_in: unreleased
branch: fix/w46-b1-space
commits: 6eba333b
tests: space_terra_tool
signature:
docs: docs/re/space.md#space-terraform-flag
related: BUG-0199
---

## Report
Child of BUG-0199. Terraforming reaches the partner (spacecoop.cpp: hooks on the terraforming manager's
AddAtmosphere/Water/Temperature deltas, K_TERRA to partners on the same planet; coop_space_play.py tests it with the
test command `coop.space terra`, not with a tool).

Attempt (fix/w46-g4b-coverage, lane 3, out/exp_terra.py on `space_free`, 2026-11-08): both ships landed on the colony
planet 01460035 (play_space.Player.land_retry); the host opened the palette tabs at x = W-266, W-226, W-186
(y = H-51), pressed 1, clicked the ground three times per tab: a tool fired (a ring on the ground) but no
terraforming delta came (`terra_sent=0` on the host, the planet record b0/b4/b8 0.000/0.450/0.000 on both games
before and after). The tab row in that view sits at y ~1040 with 8 tabs (x ~1016, 1054, 1094, 1134, 1174, 1212,
1252, 1294 in 1440x1080; screenshot out/lane3/shots/terra_tab0.png of that run): the three terraforming tabs are
probably 1174/1212/1252, and `spaceCreate` may not give terraforming tools at all (the inventory after the cheat
has 37 tools: which of them are terraforming tools was not checked).

Recipe for the test: `space_free`; `coop.space tools` to find a terraforming tool (or `coop.space tool add <name>`);
open its palette tab and slot by clicks, click the planet; assert the host's planet record (+0xB0/+0xB4/+0xB8, the
terrain update integrates the deltas over time) changes and the guest's record follows (terra_got), compare both.
Twin: a switch around terra_tick's send.

## Root cause
A test gap, and the test found a real co-op gap. Observed (fix/w46-b1-space, lane 6): `space_free` has no atmosphere
tool (the atmosphere tab is empty); with an Atmosphere Generator added the host's tool hit the planet, its deltas went
to the guest (terra_sent=1, terra_got=1), the host's planet record rose (b0 0 -> 0.103) and the guest's stayed at 0.
Observed (Ghidra GOG 2024): the terrain tick 0xC83280 integrates the pending deltas into the record only while the
planet terrain data's flag +0x2030 is on; the game sets it from the tool path (0x1050BC0 -> 0xC7E490(data, 1)), the
relayed deltas came without it. docs/re/space.md#space-terraform-flag.

## Fix
spacecoop.cpp apply_terra (switch `space.terra_apply`): with received atmosphere / temperature deltas the guest's
game switches the planet's terraforming flag on (`SporeCoop::PlanetTerrain_SetTerraforming` 0xC7E490), as its own
tool would; counter `terra_enabled=`. New test `coop_space_terra_tool.py` with the real tool by clicks.
Seen on the way: sculpting and colouring tools are not relayed at all (BUG-0763).

## Verification
`coop_space_terra_tool.py` (suite `space_terra_tool`):
* FAIL with `SPORE_FIX_OFF=space.terra_apply`: lab 20261108-151748 (host 0.103, guest 0.0).
* PASS: lab 20261108-152042 (host 0.042, guest 0.040 after 8 s) and 20261108-152230.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): opened as a child of BUG-0199 after an attempt with a run (recipe and blocker above).
- 2026-11-08 (fix/w46-b1-space): test written with the real tool; it found the guest's planet never changed (the terraforming flag); fixed behind space.terra_apply.
