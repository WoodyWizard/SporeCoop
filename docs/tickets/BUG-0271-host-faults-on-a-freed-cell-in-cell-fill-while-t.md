---
id: BUG-0271
title: Host faults on a freed cell in cell_fill while the game goes Cell -> Creature (dc04aee5:coop.dll+0x88951)
status: open
kind: crash
stage: cell
reported: 2026-10-29
source: lab 20261029-111744 campaign_short
role: host
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature: dc04aee5:coop.dll+0x88951
docs:
related:
---

## Report
Lab 20261029-111744 (branch fix/w6-campaign2 f617994, coop.dll built from main 63ed4d6, so with the
BUG-0264 fix): campaign_short on lanes 2 and 4, both FAILED the same way. After the food shortcut
and the two captions of the cell -> creature change, the host (Alice) caught
`0xC0000005 at coop.dll+0x88951` (data = ecx + 0x54) and its console died
("the host plays creature after 71 s", then dead). Logs: out/lab/20261029-111744/campaign_short#1/#2.host.game.log.
In lab 20261029-093233 (the same dll) the same change passed in 5 of 6 four-player games.

## Root cause
Observed: llvm-symbolizer build/coop.dll 0x10088951 = modes::cell_fill (modes.cpp ~486, reading the
cell at ecx). cell_local() = cell_at(cell game +0x411C). The faulting cell's memory is gone: the
game freed the cell (pool) while the co-op adapter still sees the Cell mode / cell game. The stack
already shows creature-planet code (Terrain::cTerrainMapSet::GetHeight, cPlanetModel::ToSurface).
Inferred: same class as BUG-0264 (an object freed before the mode id changes), in Cell on the host.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
