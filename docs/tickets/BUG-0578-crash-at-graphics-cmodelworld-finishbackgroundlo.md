---
id: BUG-0578
title: Crash at Graphics::cModelWorld::FinishBackgroundLoad+0x2735 on 0.7.3.6 (3010a413, mode none)
status: not-reproduced
kind: crash
stage: none
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.3.6
fixed_in:
branch: fix/w43-transitions
commits:
tests:
signature: 3010a413:SporeApp.exe Graphics::cModelWorld::FinishBackgroundLoad+0x2735 (GOG 2024 0x753D05)
docs:
related: BUG-0576
---

## Report
A crash in an older run (coop 0.7.3.6, SporeKit 0.3.6, 2026-11-03), carried in the 2026-11-04 report: 0xC0000005 at 0x00AF3D05 Graphics::cModelWorld::FinishBackgroundLoad+0x2735, chain StandardFileFilter::IsValid <- cFrustumCull::FrustumTestAABB <- Model::Release <- GeneralAllocator_FreeInternal. Game mode none (a load).
Reports: SporeCoop-report-2026-11-04 175959 (crash-20261103-210113) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (crash text, dump): 0xC0000005, write of 0x00000000 at 0x00AF3D05 = GOG 2024 0x753D05: unlinking a
  list node whose links are 0 (`mov [eax],ecx` with eax = [esi+4] = 0), under Graphics::Model::Release /
  GeneralAllocator_FreeInternal, on the game's main thread at exit: the log ends with cSporeApp::Shutdown at
  21:01:13.604, the mod unloaded at .668, the crash at .725 (mode none). Coop 0.7.3.6, the player as host in Tribe
  with 16 gifts; before it: "the editor waits: the game is busy (paused)" (as BUG-0576) and the guest timed out.
* Inferred: a model freed twice or left in a render list at shutdown (a model of ours: puppet, mirror or effect).

* Observed (the 0.7.3.6 log, previous/sporekit-20261103-210114.log of the report): the session ended exactly as
  BUG-0576: at 20:59:48 the host's vote "wants to evolve (Tribe)" for **editor 99E92F05 (the City Hall)**, at its end
  "the editor waits: the game is busy (paused)", then 81 s of nothing (the game's own pause for that editor), the
  guest left, and the player quit the game at 21:01:13 (cSporeApp::Shutdown). The crash's stack has
  App::cSporeApp::Shutdown <- cAppSystem::PreShutdown <- **App::cGameModeManager::Dispose <- Editors::cEditor::Dispose**
  <- ... Graphics::Model::Release: the game disposing its editor (prepared for the held request) at exit, 57 ms after
  the mod unloaded.
* Inferred: the game's exit path with an editor request in flight (the game paused for it, the editor not open)
  frees a model twice or one left in a render list. The state comes only from BUG-0576 (the mod held the game's own
  City Hall request); with BUG-0576 fixed the editor opens at once and the player has no reason to quit there.

## Fix
None on its own: BUG-0576 (core.editor_own_pause) removes the state the player quit from.

## Verification
no-fail-run: an exit crash of 0.7.3.6 at the end of BUG-0576's black screen; the quit from that state was not tried
in the lab (the lab quits games with spore.sh kill, not through the game's Shutdown).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), triaged from the dump.
- 2026-11-04 (fix/w43-transitions, T1): the same session as BUG-0576 (the held City Hall editor, then the quit):
  not-reproduced, the state is gone with BUG-0576.
