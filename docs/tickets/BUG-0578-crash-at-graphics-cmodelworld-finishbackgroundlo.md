---
id: BUG-0578
title: Crash at Graphics::cModelWorld::FinishBackgroundLoad+0x2735 on 0.7.3.6 (3010a413, mode none)
status: open
kind: crash
stage: none
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced (an exit crash on an old version). Package T1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), triaged from the dump.
