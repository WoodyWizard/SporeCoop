---
id: BUG-1436
title: Guest crash at Player_SetProperty+0x14D (read 0x1188) in the galaxy menu sim reset after the mod's quit to load the host's Creature world
status: open
kind: crash
stage: Cell
reported: 2026-11-22
source: discord
role: guest
versions: 0.7.15.5
fixed_in:
branch:
commits:
tests:
signature: 3010a413:SporeApp.exe SporeCoop::Player_SetProperty+0x14D
docs:
related:
---

## Report
A guest's game crashed when it left Cell to load the host's Creature world. Full analysis: out/research/dc-report-20261122-world-switch.md in the main checkout (not in git; six SporeCoop-report zips, coop 0.7.15.5, Steam).

## Root cause
Signature 3010a413:SporeApp.exe SporeCoop::Player_SetProperty+0x14D, read of 0x1188, inside the game's sim reset for the galaxy menu (GalaxyMenu_SimReset) after the quit the mod sent; our h_SimMsg is on the stack. One case; the .dmp in the player's zip is not analysed yet.

## Fix
Not fixed. 0.7.17 unless the dump points at our code (then 0.7.16.1).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.
