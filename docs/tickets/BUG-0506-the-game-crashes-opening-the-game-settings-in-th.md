---
id: BUG-0506
title: the game crashes opening the game settings in the galaxy menu, offline (3010a413:SporeApp.exe+0x2025A9, GameSettings::Show, GetDatabaseByID(0x011AC197) null)
status: fixed
kind: crash
stage: Menu
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage), GitHub issue #190
role: none
versions: 0.7.3.3, 0.7.3.4
fixed_in: 0.7.4
branch: fix/w40-misc
commits: 0a02f72a, dcf91148
tests: save_areas (tools/tests/coop_save_areas.py), save_areas_editor (tools/tests/coop_save_areas_editor.py)
signature: 3010a413:SporeApp.exe+0x2025A9 read 0 (UI::GameSettings::Show+0x44 chain, GalaxyGameEntry)
docs: docs/re/creations.md#creations-save-areas, docs/re/creations.md#creations-save-areas-locale, docs/06-testing.md#testing-save-areas-editor, docs/06-testing.md#testing-save-areas
related: 
---

## Report
Two players on the Steam build (code = GOG 2024), not in a session (coop window: offline), in the galaxy menu (mode GalaxyGameEntry, 25-30 s after the game started, frame ~450): the game crashes with 0xC0000005 read of 0 at SporeApp.exe+0x2025A9 (0x6025A9). One player 2x on 0.7.3.4 (report 20261102-222904, its crashes/crash-20261102-222755.txt), another player 4x within 10 min on 0.7.3.3 (in report 20261104-014642: crashes/crash-20261102-042720.txt, -043034.txt and previous logs 042435/042605). Chain (stack scan): 0x6025A9 <- 0x60327D <- 0x6034BA <- 0x60199A <- UI::GameSettings::Show+0x44 (0x6036B4) <- UTFWin window manager (a click / hover message). No coop.dll frame, no mod object. No player text says what was clicked; the chain says a settings (options) window was being shown.
Target: 0.7.4 (a crash).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (disassembly of the GOG 2024 exe, same code as the Steam 3010a413 one): at 0x60259F the function pushes 0x011AC197 and calls Resource::GetDatabaseByID (0x6B1F20); the result is used without a null check: `mov edx,[eax]` at 0x6025A9 (eax=0). So the resource database 0x011AC197 is not registered in that game when the settings window is built. The branch is taken when 0x6A25A0(0x061B67B6) on the object at [0x15FD918] returns false.
Observed (2026-11-03, fix/w40-misc): 0x011AC197 is the Movies save area, a directory database in Documents\My Spore Creations (dir id 0xA0214B), made by the app's init 0x6B3760 together with Pictures 0x11AC196 and Animations 0x11AC198 (docs/re/creations.md#creations-save-areas). When the folder cannot be made the areas are never registered. Lab: the test profile's My Spore Creations replaced by a plain file -> all three areas 0, Options -> Settings in the galaxy menu faults at 0x6025A9 reading 0 (the players' signature). SporeKit redirects Documents only with -sporekit-profile; the players ran with "profile own", so the mod does not change the path.
Inferred: the players' Documents\My Spore Creations could not be made (Documents missing / moved, OneDrive, or Windows' controlled folder access blocking SporeApp.exe); a vanilla game crash. Earlier text: either a vanilla Steam-build crash (the database is created only on some paths, for example after a first game was loaded), or something of SporeKit / coop (save-area or profile redirection, the menu item the mod adds) keeps that database from being created. To do: name database 0x011AC197 (Ghidra: who calls a register-database with this id), try the options window in the galaxy menu on a lane with and without the mod.
<!-- observed (runtime, logs, disassembly) vs inferred -->

Observed (2026-11-03, GitHub issue #190 follow-up): the same missing areas crash a Cell editor (Pictures 0x11AC196 at 0x633765, PlayModeBackgrounds). The issue's install was ru-ru Steam with PlayerDir "??? ????????"; with that PlayerDir in the lab bottle the game made none of the three areas (runtime, lane 9). So the cause is a PlayerDir that cannot be a folder name, not the localized subfolder names (docs/re/creations.md#creations-save-areas-locale).

## Fix
mods/coop/gamefixes.cpp: a hook on the settings builder (SporeCoop::GameSettings_Build 0x602070) gives each missing area of the three a fallback directory database in AppData\Spore\<name> (CreateSaveAreaDirectoryDatabase dir 0xA02151 + RegisterSaveArea, as the init does) before the builder runs; logged once per area. Switch: `coop.fix game.save_areas off`.
Follow-up (GitHub #190): the fallback runs right after the game's own startup made the areas (hook on SporeCoop::SaveAreas_Init 0x6B3760), with a log line of the three areas; the first frame checks again when that hook did not run (hot reload); the settings builder keeps its check. A startup switch file (`SporeCoop\fixes_off.txt` in the profile) lets the tests turn it off before the game starts.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL: out/lab/20261103-180851 (SPORE_FIX_OFF=game.save_areas: fault at 0x6025A9, areas still 0)
- PASS: out/lab/20261103-180957
- follow-up (startup fallback, editor): FAIL out/lab/20261103-192956 (SPORE_FIX_OFF=game.save_areas: save_areas fault at 0x6025A9, save_areas_editor fault at 0x633765), PASS out/lab/20261103-193308 (both)

## History
- 2026-11-03: opened.
- 2026-11-03: two players' reports (0.7.3.3, 0.7.3.4), 6 crashes with one signature; triage on fix/w40-reports.
- 2026-11-03: cause found and fixed on fix/w40-misc (game.save_areas), reproduced in the lab.
- 2026-11-03: GitHub issue #190: the editor crash, the ru-ru PlayerDir cause reproduced; the fallback moved to right after the game's startup (fix/w40-misc).
- 2026-11-03: released in 0.7.4.
