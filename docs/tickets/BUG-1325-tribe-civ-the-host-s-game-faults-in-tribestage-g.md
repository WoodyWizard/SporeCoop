---
id: BUG-1325
title: Tribe -> Civ: the host's game faults in TribeStage_Graduate (DestroyInstance of a null food mat +0x260) after the City Hall editor, then thousands of caught faults
status: open
kind: crash
stage: Tribe
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: line_stages
signature: exception 0xC0000005 at 0x00B22566 cGameNounManager::DestroyInstance+0x6 (data 0x20), calls 00CDD43D 00CDECAC 00CDF11C (TribeStage_Graduate+0x34D < TribeStage_EnterUpdate+0x4CC)
docs:
related: BUG-0856, BUG-0585, BUG-1276
---

## Report
line_stages, lab 20261119-120449 (lane 4, fix/w57-savestrip on rel/0.7.16 79ee9769): the host (Alice) came back from
the end-of-Tribe City Hall editor (12:28:55 "back from the editor 99E92F05 in the end of Tribe"), 1 s later its game
faulted at DestroyInstance+0x6 called from TribeStage_Graduate+0x34D (the run's report-20261119-122900-i7.txt is kept
next to the run's logs), then the campaign counted 279, 3105, 1250, 1999 caught faults on the host. No save in between.
Not seen in line_stages of 20261119-095414 / 20261119-114816 (the same step passed).

## Root cause
Observed (Ghidra 2026-11-19): TribeStage_Graduate 0xCDD0F0 at 0xCDD41E reads the player tribe's food mat +0x260, calls
its vt 0 only when it is not null, and then pushes it to cGameNounManager::DestroyInstance unconditionally (0xCDD430):
a null +0x260 faults at DestroyInstance+0x6. The mat was null at the graduation. Not analysed further (found by
fix/w57-savestrip, outside its task): who cleared +0x260 before (BUG-0585 notes the mat is gone after the City Hall;
a second Graduate run, or a mod path), and whether the guest's own City Hall at its arrival (BUG-1276) plays a part.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
