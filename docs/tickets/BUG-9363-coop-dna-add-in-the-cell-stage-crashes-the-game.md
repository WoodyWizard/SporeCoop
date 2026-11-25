---
id: BUG-9363
title: coop.dna add in the Cell stage crashes the game (test tooling)
status: fixed
kind: crash
stage: Cell
reported: 2026-11-24
source: deep playtest 2026-11-24
role: host
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 0d0214dc
tests: tools/qa_win (manual, TITAN host + MSI guest, a new Cell game): coop.dna add 30
signature: 0x00CCD57F SporeCoop::CreatureGame_MatingCallFlag_set+0xE76 read 0x144
docs:
related:
---

## Report
Deep playtest 2026-11-24 (TITAN host, a new Cell game, the MSI as its guest over a direct connection): the test
command `coop.dna add 30` on the host took the game down (crash-20261124-133733-i9, not in git). Test tooling, no
player's path: the command is not reachable without the devkit console.

## Root cause
Observed (the crash report): an access violation at 0x00CCD57F (SporeCoop::CreatureGame_MatingCallFlag_set+0xE76,
a read of 0x144) under Simulator::cCreatureGameData::AddEvolutionPoints, called by the command. The game's
AddEvolutionPoints reads the Creature game's data, which a Cell game does not have.

## Fix
debug.cpp c_dna: `coop.dna add` and `coop.dna gain` are refused outside the Creature stage ("the Creature stage
only (the Cell stage: coop.pool food <n>)"). No switch: a test command's own guard.

## Verification
- FAIL (build ffd01832, 13:37): `coop.dna add 30` in Cell: the crash above; the guest stayed in its world, offline.
- PASS (the build with the guard, 13:40, the same Cell save loaded again and the guest joined again): the command
  answers "[not ok] coop.dna add: the Creature stage only (the Cell stage: coop.pool food <n>)", the game goes on.
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the crash report named above.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the deep playtest).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
