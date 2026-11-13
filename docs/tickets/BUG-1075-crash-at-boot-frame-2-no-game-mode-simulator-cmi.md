---
id: BUG-1075
title: Crash at boot (frame 2, no game mode): Simulator::cMissionManager::ChooseMission+0x1043 reads 0x28 (seen once in the lab under load)
status: open
kind: crash
stage: Menu
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests:
signature: SporeApp.exe+0xBEFC03 Simulator::cMissionManager::ChooseMission+0x1043 read 0x28
docs:
related:
---

## Report
fix/w49-tests, lab 20261113-074535 (lane 4, `lab.py run campaign` ran the whole campaign suite by mistake; machine
loaded: lanes 4-7 busy, two other suite starts timed out): campaign_tribe_end's host (instance 7) crashed at frame 2,
game mode none, up 1-2 s, before any co-op session: 0xC0000005 at 0x00FEFC03
`Simulator::cMissionManager::ChooseMission+0x1043` on the main thread, read of 0x28, ebx=0 ebp=0 edx=0. Files:
out/lab/20261113-074535/campaign_tribe_end.crash-20261113-074809-i7.{txt,dmp} (fix/w49-tests worktree).
Seen once; the same test started fine before and after on lane 5.

## Root cause
Not analysed (outside the package). Inferred: the game's mission manager ticks before its data is loaded on a slow
start (a null +0x28); the mod has no session at frame 2.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
