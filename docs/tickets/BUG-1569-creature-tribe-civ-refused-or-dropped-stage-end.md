---
id: BUG-1569
title: Creature/Tribe/Civ: refused or dropped stage-end request leaves stage-end transition flag stuck so next normal editor advances stage
status: fixed
kind: bug
stage: All
reported: 2026-11-25
source: Discord bug-reports (Kundir, 2026-11-25, SporeCoop-report-20261125-224842.zip)
role: both
versions: 0.8.0.2, 0.8.1
fixed_in: 0.8.1.1
branch: main
commits: c5677ecf
tests: own_progress_stage_end_flag, own_progress_stage_end_flag_off, own_progress_creature, stage_end_guest, tribe_civ, civ_ship_editor
signature:
docs:
related: BUG-1469, BUG-1085, BUG-0920
---

## Report

In a two-player Creature session with `all.shared_dna = 2` (`own progress`), the host's evolution bar was full while the guest was still at 50%. The host tried to advance to the Tribal stage several times and SporeCoop refused each attempt while waiting for the guest. Minutes later, the host entered an ordinary mating Creature Editor (while the guest was also editing their creature) and clicked Done; immediately after returning from the normal editor, Spore skipped ahead to the Tribal stage without waiting for the guest or holding a vote.

## Root cause

Observed in the attached session log (`sporekit-20261125-220539.log`, lines 3327-4265) and confirmed by disassembly of `SporebinEP1/SporeApp.exe`:

1. **Creature -> Tribe:** When the History "Advance to Tribal Stage" checkmark (`controlID 0x06455B55`) is clicked, `0x00D2C709` writes `1` to `cCreatureModeStrategy + 0xDE` (`App::cCreatureModeStrategy::Get`, `0x00D38840`) *before* `EditorRequest::Submit(editor=0x465C50BA, tag=0x064AA168)`. Nothing in `SporeApp.exe` ever clears `+0xDE` back to `0` after it is set (only the constructor `0x00D3BA3F` initializes it to `0`). When `h_Submit` refuses the stage-end request (or `drop_vote` cancels a stage-end vote), `+0xDE` remained `1`. On exit from *any* subsequent Creature Editor (even an ordinary mating evolution with `tag 0x012191CA`), `cCreatureGame::Update` at `0x00D4525C` checks `cCreatureModeStrategy + 0xDE != 0`, fires `0x00D3AEA0` (`0x533535DD`), and sets `mState = 0xC`, forcing the stage transition into Tribe.
2. **Tribe -> Civ and Civ -> Space:** Disassembly of the corresponding stage-end handlers shows the exact same pre-submit flag pattern: `0x00CD2202` sets `cTribeUI + 0x28 = 1` (`SporeCoop::cTribeUI_Get`, `0x00CD4230`) before `SubmitEditorRequest(0x99E92F05)`, and `0x00CF5A56` sets `GameCiv + 0x28 = 1` (`SporeCoop::GameCiv_Get`, `0x00CF7620`) before `SubmitEditorRequest(0x96B24187)`. (Cell -> Creature is unaffected because `h_CellStageEnd` hooks `0x00E72C80` before `G + 0x5158 = 3` is written.)

## Fix

Added `clear_stage_end_flag()` in `mods/coop/vote.cpp` behind `COOP_FIX("core.stage_end_flag_clear", "BUG-1569", ...)` covering all three stage-end flags (`Creature +0xDE`, `Tribe +0x28`, `Civ +0x28`):

- Clears the stage-end flag whenever `h_Submit` refuses a stage-end request (`own_progress::hold_stage_end`, guest stage-end refusal, `creature.mode host` guest refusal, or `designs::refuse_own_editor`, which now also sets `g_evo_flag_refused`).
- Tracks `g_vote_stage_end` across `start_vote` / `finish_vote` / `drop_vote` and clears the stage-end flag if a stage-end vote is dropped.
- Defensively clears any lingering stage-end flag right before `o_Submit(req)` submits any ordinary non-stage-end in-stage editor (including `open_own_editor`).
- Exposed `stage-end flag` and clear counter in `coop.vote` (`mods/coop/coop_cmds.cpp`).

## Verification

- `out/lab/20261125-222818`:
  - `own_progress_stage_end_flag` (fix ON): **PASS** (104 s, 0 faults; after refused Creature stage-end, `+0xDE` cleared `1 -> 0` immediately; after opening and finishing a normal in-stage Creature editor, both host and guest stayed in Creature).
  - `own_progress_stage_end_flag_off` (`SPORE_FIX_OFF=core.stage_end_flag_clear` twin): **FAIL** (109 s; `+0xDE` stayed stuck at `1` with `cleared=0` through the normal editor).
- `out/lab/20261125-223108` (4-lane regression suite across Creature, Tribe, and Civ transitions):
  - `own_progress_creature`: **PASS** (159 s)
  - `stage_end_guest`: **PASS** (104 s)
  - `tribe_civ`: **PASS** (174 s)
  - `civ_ship_editor`: **PASS** (41 s)

## History
- 2026-11-25: opened from Discord report by Kundir, root-caused in `SporeApp.exe`, fixed for Creature/Tribe/Civ (`c5677ecf`), verified with ON/OFF twin (`20261125-222818`) and 4-stage transition regressions (`20261125-223108`).
