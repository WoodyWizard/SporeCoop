---
id: BUG-0672
title: guest: a fault in cPlanetRecord_GetPlanetID+0x17 (0xCE6AE7, data 0x554) under the tribe slot release 0xC98A71 right after a Tribe world load, then the game froze
status: open
kind: crash
stage: tribe
reported: 2026-11-07
source: lab rel/0.7.10 20261107-073512 tribe_play (lane 9)
role: 
versions: rel/0.7.10 94066205
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.

## Observed (2026-11-07, orchestrator)

- Run out/lab/20261107-073512, tribe_play (warm reset, lane 9), the guest at s 11.268, 0.5 s after Loading -> Tribe:
  `exception 0xC0000005 at 0x00CE6AE7 cPlanetRecord_GetPlanetID+0x17 (data 0x00000554)`, eax=0 edx=0,
  calls 00CE6BCA 00C98A71 00927EA0 00928CB9 00C9D0D2 00B20162 00B7F3BE 00C9D629 00C9D64C,
  i.e. under the tribe slot release 0xC988E0 (+0x191, the cTribe_ReleaseSlot that BUG-0667 detours for a null tribe).
- 30 s later the watchdog saw no frames (stage 'SporeApp::Init done'), main thread in the message pump: the game froze
  for the mod although 0.7.9's kit.fix frameunwind is in.
- Also failed in that run: "the same chiefs on both machines", "the guest is told about the epic", host's puppet gate
  (the guest's state older than 5 s), and a report-20261107-081124-i18.txt.
- Not seen before in any lab log of any worktree. Rerun 20261107-082717: tribe_play 4/4 PASS (rare).
- Questions: is it the 0.7.9 tribe.body_teardown detour or code new in 0.7.10 (P0-Cell, M1 merged)?
  Why did frameunwind not get frames going again (the fault may be outside SporeKit's frame)?
