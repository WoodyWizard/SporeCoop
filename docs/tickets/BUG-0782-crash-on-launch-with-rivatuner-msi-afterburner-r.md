---
id: BUG-0782
title: Crash on launch with RivaTuner / MSI Afterburner (RTSSHooks) running (need info)
status: open
kind: crash
stage: boot
reported: 2026-11-10
source: Discord 2026-11-09 help Crashing on launch
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/02-modkit.md, docs/INSTALL.md
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

## History
- 2026-11-10: opened.
