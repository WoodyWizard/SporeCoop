---
id: BUG-0535
title: Civ: the host's shot observer probes a target's non-vtable and logs a fault per shot
status: open
kind: bug
stage: civ
reported: 2026-11-04
source: lab final 0.7.5
role: host
versions: 0.7.5-dev
fixed_in:
branch: rel/0.7.5
commits:
tests: play_civ
signature: 0xC0000005 at kernel32.dll+0x1B997 (IsBadReadPtr) data 0x3E99999A, called under cToolManager::FireTool (0x01050080) from cCombatSimulator::Update
docs:
related: BUG-0477, BUG-0534
---

## Report
play_civ's host wrote 162 handled faults in out/lab/20261104-072946 (rerun of the 0.7.5 final check).

## Root cause
civ.cpp fire_observed (the host's FireTool hook, BUG-0477) checked a target that is not a host vehicle with
IsBadReadPtr(*(void**)t): for an object without a spatial at -kVSpatial the "vtable" was a float (0.3f) and the probe
faulted (handled by Wine, logged by SporeKit's handler each shot). Observed: the fault address is the probed value.

## Fix
vtable_in_game(): the first dword must lie in SporeApp.exe's image before any vtable call (target and turret owner). No switch (a check, no behaviour change).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
