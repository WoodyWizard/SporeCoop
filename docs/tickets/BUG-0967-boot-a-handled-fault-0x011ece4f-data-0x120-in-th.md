---
id: BUG-0967
title: Boot: a handled fault 0x011ECE4F (data 0x120) in the game's audio init right after cSporeApp::Init fails a test's fault check
status: fixed
kind: test
stage: Boot
reported: 2026-11-12
source: rel/0.7.13 integration lab 20261112-135150 (partner_pass, lanes 6/7)
role: host, guest
versions: 
fixed_in: unreleased
branch: fix/w48-walk
commits:
tests: tools_selftest bootfault
signature: dc04aee5:0x011ECE4F read [edi+0x120], edi null (FUN_011ECE30, audio init at boot)
docs: docs/06-testing.md#testing-boot-audio-fault
related: BUG-0964
switch: none (test harness only: SPORE_BOOT_FAULT_STRICT=1 counts the fault again)
---

## Report
The rel/0.7.13 integration run (lab 20261112-135150, lanes 6 / 7) failed partner_pass, among other things, on one fault
in each game, at the same moment, 0.03 s after `App::cSporeApp::Init -> 1` (before the galaxy menu, before the test or
the co-op session did anything):
`0xC0000005 at 0x011ECE4F GetTickCount_0+0x61F (data 0x00000120)`, calls 00A0F707 00A0F765 0112D97F 01138FE0 0112CCCB
00A0F7B0 011E6DA9 011E6065 011E608B 011EA840 (logs: rel/0.7.13 out/lab/20261112-135150/partner_pass.*.game.log line
~86). The game went on normally (galaxy menu 0.01 s later, the test played all its rounds).

## Root cause
The game's own audio initialisation, not the mod and not partner_pass. Static (Ghidra, GOG 2024): 0x011ECE4F is in
FUN_011ECE30, a constructor (thiscall, 3 arguments, `mov edi,[esp+0x120]` = its third argument, then
`mov eax,[edi+0x120]`): the third argument is null. Its caller 0x011EA810 passes `FUN_011E6030(arg, 0)` there, a lookup
that returned null. The "calls" list is a stack scan, not a call chain: it holds return addresses of the audio core
(0x01138F40 creates "rw::audio::core::Snd9Service::mpMixBufStart" and the "Snd9Service" thread, 0x00A0F710 is
Core/Audio/AudioAllocator.cpp). Seen twice in all recorded lab runs: 20261101-143943 (feat/w33-tests-creature,
creature_editor_soak, lane 5, host and guest at once, 0.7.x before 0.7.13) and 20261112-135150. Inferred: a race of the
two games' audio devices under Wine when both start at the same moment (the lab starts host and guest together);
the exception is caught (SporeKit's logger counts it, the game's own handler continues).

## Fix
tools/tests/twoinst.py `Checks.faults`: a game whose only faults are this one (the signature `0xC0000005 at
0x011ECE4F ... data=0x00000120`) is printed as "none but N known one(s) of the game's audio init at boot (BUG-0967)"
and passes; any other fault in the same game fails as before (the lab's own log check reads the same line).
`SPORE_BOOT_FAULT_STRICT=1` counts it again. No mod change.

## Verification
* no-fail-run: the fault is not reproducible on demand (2 of all recorded lab runs, at a cold start).
* tools_selftest `bootfault` (in `make test`): the boot fault alone passes and its line carries no "total faults: N";
  with another fault, or another fault alone, the check fails.

## History
- 2026-11-12: opened (the orchestrator's note on lab 20261112-135150), explained and tolerated in the test harness
  (fix/w48-walk).
