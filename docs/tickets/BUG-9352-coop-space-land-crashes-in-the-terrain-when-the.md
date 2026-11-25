---
id: BUG-9352
title: coop.space land crashes in the terrain when the ship still flies to a planet never visited
status: fixed
kind: tooling
stage: space
reported: 2026-11-23
source: QA (MSI)
role: host
versions: 0.7.14.1-lineages69-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: efc895d5, b21f63bb
tests: tools/qa_win (manual, 2026-11-23): the MSI's Space game; coop.space land at a gas giant
signature: SporeApp.exe+0xA1329A Terrain::cTerrainMapSet::GetHeight+0xB3A (read 0xC) < Planet_NearestLand < cAppModeSpace_LandOnActivePlanet, from coop's console
docs: docs/06-testing.md#testing-msi-two-games-titan-locked
related: BUG-0182
---

## Report
The MSI's Space test game (instance 2) crashed (2026-11-23 14:06) at `coop.space land` (a test command) right after
the ship was sent to the system's planet 04B66023: an access violation in the terrain's GetHeight called from the
game's LandOnActivePlanet. The planet turned out to be Eurgo, a gas giant (seen at the next try: Spore never lands
there).

## Root cause
`coop.space land` calls the game's LandOnActivePlanet (the landing of a respawn) with only a cinematic / state check;
the game itself never lands on a gas giant (CanLandOnActivePlanet refuses: no terrain), so the landing read a null
height map. A test command only: players land through the wheel, which checks first.

## Fix
spacecoop.cpp `land`: refused unless the ship has arrived (+0x74C) and cSimulatorPlayerUFO_CanLandOnActivePlanet
allows it ("cannot land here now ..."). A first version also asked Space_ZoomLandReady, which is false outside a
wheel zoom and would have refused every landing (b21f63bb took it out). No switch: a test command's guard.

## Verification
FAIL: the crash above (the build before, the MSI, 14:06:23, crash-20261123-140623-i2.txt).
PASS (b21f63bb, the MSI): `coop.space land` right after a click on the gas giant Eurgo (04B66023) while the ship
flew there: "cannot land here now (...)"; again once it arrived (arrived 1, can_land 0): the same; no crash.
A landing on Abber (arrived, can_land 1) still lands.
no-fail-run: test tooling; the crash is the failing run.

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha (efc895d5, b21f63bb).
- 2026-11-23: the fixed command refused at the gas giant and still landed on Abber (the MSI).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
