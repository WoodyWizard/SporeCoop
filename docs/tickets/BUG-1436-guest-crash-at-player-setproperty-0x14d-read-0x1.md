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
branch: fix/w75-share
commits: 7be732b5, c29dc4db
tests: cell_quit_switch (coop_cell_quit_switch.py); twin cell_quit_switch_off
signature: 3010a413:SporeApp.exe SporeCoop::Player_SetProperty+0x14D
docs: docs/re/space.md#space-menu-creature-switch, docs/06-testing.md#testing-share-one-planet
related: BUG-0284, BUG-0315, BUG-1269, BUG-1434
switch: world.quit_no_switch
---

## Report
A guest's game crashed when it left Cell to load the host's Creature world. Full analysis: out/research/dc-report-20261122-world-switch.md in the main checkout (not in git; six SporeCoop-report zips, coop 0.7.15.5, Steam).

## Root cause
Signature 3010a413:SporeApp.exe SporeCoop::Player_SetProperty+0x14D, read of 0x1188, inside the game's sim reset for the galaxy menu (GalaxyMenu_SimReset) after the quit the mod sent; our h_SimMsg is on the stack. One case.

Dump analysis (2026-11-23, fix/w75-share; the player's minidump, Steam exe at 0x860000, objdump of the GOG 2024 exe; details docs/re/space.md#space-menu-creature-switch):
* Confirmed: the fault is 0xC7A1ED in 0xC7A140 (`SporeCoop::cPlayer_PlanetListUpdate`, thiscall(cPlayer*, planet record)) with this = null (ebx 0), called by the home planet setup 0x10220E0 (`SpacePlayerData_SetHome`) that takes the cPlayer from [0x167EAE0]+0x74 and skips the call only in Cell and adventures. The symbol names in the report (Player_SetProperty+0x14D, TimeOfDay_Apply+0xC92, SpacePlayerData_Reset+0x14D, GalaxyMenu_SimReset+0x2CC / +0x10A6, cSimulatorSystem::Create+0x10B2) were the nearest known names, not the functions.
* Confirmed: the chain is the simulator's OnModeEnter (message 0x212D3E7) with previous mode 0x2CCD1D2 (the galaxy menu) and new mode 0x1654C01 (Creature): `Simulator_OnModeEnter` 0xB5F180 -> `Simulator_ModeChange` 0xB33A80 -> `GalaxyMenu_StageStart` 0xB32EE0 -> `SpacePlayerData_EnsureHome` 0x1022470 -> 0x10220E0 -> 0xC7A140. The mode switch ran from the message queue: message 0xE11333 (SetActiveModeByName) to cGameModeManager, 67 ms after the galaxy menu came up; the mod's log has no load of the received world yet (it waits 3 s in the menu). So the game entered the Creature stage from the galaxy menu without a loaded game: no cPlayer.
* Confirmed: h_SimMsg (vote.cpp) only passes OnModeEnter through; no code of ours is on the faulting path.
* Confirmed: the only maker of 0xE11333 is the App state script command `mode` (0x7E4690); the simulator's SetMode 0xB1E350 switches a stage by going to the App state "<New>GameFrom<Old>Game" (CreatureGameFromCellGame for Cell -> Creature), whose script posts `mode <name>` later.
* Inferred (not reproduced, not in the dump): a Cell -> Creature switch of the guest's own game (History's check or the Cell end sequence; the guest's hook lets it through when the host is no longer in its world, and the host had entered Creature 46 s before) came while the mod's quit for the received world was under way; its state's `mode CreatureGame` ran after the quit had taken the game to the galaxy menu.

## Fix
Our quit path: while the mod leaves a stage for the galaxy menu (game::leaving_stage: the quit posted or handled until the mode changes), the game's own Cell -> Creature switch (h_CellToCreature, not the vote's own call) is not taken and logged ("the game's own switch to the Creature stage is not taken while the stage is left for the galaxy menu"); a guest's own switch that goes through is logged too, so a next report shows the trigger. The received world brings the next stage. Switch `coop.fix world.quit_no_switch off`: taken (0.7.16.1). Test command `coop.cellswitch [quit]` (the switch through the hook, after the mod's quit).
Not done: a guard in the game's menu entry itself (skipping OnModeEnter would leave the game half switched); if the twin does not reproduce the crash, the trigger was another one and the ticket stays open with that result.

## Verification
Open, waiting for the lab run. pending night run: cell_quit_switch, cell_quit_switch_off (one lane, cell fixture, ~4 min each). PASS expected: cell_quit_switch (the host alone in Cell, the mod's quit and the game's switch right after it: the game stays in the galaxy menu, no crash for 20 s). FAIL expected: cell_quit_switch_off (SPORE_FIX_OFF=world.quit_no_switch: Creature from the menu without a game, or the report's crash). A twin that passes means the hypothesis is wrong (then: no-fail-run is not the outcome, the ticket stays open).

## History
- 2026-11-22: opened.
- 2026-11-23: dump analysed on fix/w75-share (the game entered Creature from the galaxy menu without a game; the App state `mode` command; not our code on the path); the game's own Creature switch is not taken while the mod leaves Cell (world.quit_no_switch), lab test and twin written, waiting for the night run.
