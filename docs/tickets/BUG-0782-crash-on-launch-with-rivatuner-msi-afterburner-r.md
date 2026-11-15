---
id: BUG-0782
title: Crash on launch with RivaTuner / MSI Afterburner (RTSSHooks) running: a notice, the advice in crash reports and INSTALL
status: fixed
kind: crash
stage: boot
reported: 2026-11-10
source: Discord 2026-11-09 help Crashing on launch
role: 
versions: 
fixed_in: 0.7.13
branch: fix/w48-reports
commits: 41f47c43
tests: env_notes
signature:
docs: docs/02-modkit.md#modkit-frame-limit, docs/INSTALL.md#install-rtss, docs/REPORTING.md#reporting-support-environment
related: BUG-0653
---

## Report
Discord help "Crashing on launch" (2 players, 2026-11-09): the game crashes at start; one says it is because of
RivaTuner / MSI Afterburner. No crash report reached us. Need info: the SporeCoop-report zip.

## Root cause
Suspected, confidence low:
* Observed (code): SporeKit only detects it: `sporekit/diag.cpp:158` names `rtsshooks` "RivaTuner / MSI Afterburner"
  in the `other hooks:` line of the log and reports. No warning to the player, no workaround.
* Inferred: SporeKit inline-hooks (MinHook) Direct3DCreate9, IDirect3D9::CreateDevice (vt 16), Present, Reset and
  the swap chain's Present; RTSS hooks the same d3d9 functions. Which one wraps the other depends on the load order;
  a hook placed on a half-written jump of the other, or RTSS's injection while SporeKit writes, can crash at start.

## Fix idea
Cheap: (1) a notice in the menu / co-op window when rtsshooks is loaded; (2) INSTALL.md section 9 row: in RTSS add a
profile for SporeApp.exe with "Application detection level" None (or "Use Microsoft Detours API hooking" on), or
close RTSS / Afterburner; (3) a crash report with rtsshooks in the call chain prints that advice; (4) before hooking,
log whether the prologue of Present / CreateDevice already starts with a jmp into another module.

## How to test
Not reproducible in the lab without RTSS under Wine. Unit level: a test DLL named rtsshooks.dll loaded into a lab
game (`sk` console LoadLibrary) must make the notice appear and the `other hooks:` line name it; main shows no notice:
FAIL.

## Fix (2026-11-12, fix/w48-reports)
The crash itself could not be reproduced (no RTSS under Wine, no crash report from the players). What the ticket asked
for:
* SporeKit: `env` (the `== Environment` of every crash report and report archive) has an `advice:` line when
  RTSSHooks.dll is loaded (an RTSS profile for SporeApp.exe with Application detection level None, or close RTSS);
  a WARN in the log after the device; `d3d9: <function> at <addr> in <module>, starts with a jump into <module>` names
  a hook another program placed on Present / Reset / CreateDevice / Direct3DCreate9 before SporeKit (BUG-0779 logs).
* The mod (diag_stats.cpp): a notice once per start when RTSSHooks.dll / RTSSHooks64.dll is loaded (looked for every
  10 s: RTSS may inject later), with the same advice. `coop.fix core.rtss_note off`: the log line only.
* docs/INSTALL.md#install-rtss and a Troubleshooting row.

## Verification
`coop_env_notes.py` (suite `env_notes`), lane 5: a do-nothing DLL named rtsshooks.dll (build/test/rtsshooks.dll,
tests/dll/stand_in.c) loaded into the host's game by `kit.test.loadlib`.
* PASS: lab 20261112-041609-2 (other hooks names RivaTuner, the advice line, the host's notice, none on the guest), 20261112-053347.
* FAIL with `SPORE_FIX_OFF=core.rtss_note`: lab 20261112-041730 (no notice).
* Not verified: a real RTSS on Windows (whether the advice avoids the crash).

## History
- 2026-11-10: opened.
- 2026-11-12: notice, crash report advice, INSTALL section (fix/w48-reports); the crash itself not reproduced.
- 2026-11-12: released in 0.7.13.
