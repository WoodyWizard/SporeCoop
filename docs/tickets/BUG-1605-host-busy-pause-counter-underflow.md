---
id: BUG-1605
title: Core: host_busy::hold(false) underflows cGameTimeManager pause count (04BF38A7=-1) after stage transitions
status: fixed
kind: bug
stage: All
reported: 2026-11-27
source: AI playtest (v0.8.2.3, 6.5h Cell -> Creature run)
role: guest
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 3ccb71f1
tests: rivals_creature, make test
signature: cGameTimeManager pause reason 04BF38A7=-1 after stage transition
switch: core.host_busy_resume_guard
docs:
related:
---

## Report
During the 6.5-hour 4-player Cell -> Creature playtest (`out/ai-playtest/20261127-075046`), guests entering Creature stage ended up with `cGameTimeManager` pause counter `04BF38A7=-1` (underflow).

## Root cause
When a stage transition reset Spore's `cGameTimeManager` pause counts while `host_busy.cpp` still had `g_held == true`, the subsequent `host_busy::hold(false)` unconditionally called `Simulator::cGameTimeManager::Resume(tm, kWaitReason)`, decrementing the reset `0` counter to `-1`.

## Fix
Added `COOP_FIX("core.host_busy_resume_guard", "BUG-1605")` in `mods/coop/host_busy.cpp` to check `Simulator::cGameTimeManager::GetPauseCount(tm, kWaitReason) > 0` before calling `Resume(tm, kWaitReason)` in `hold(false, ...)`, preventing pause-counter underflows across stage transitions (tracked via `underflows=%u` in `host_busy_status()`).

## Verification
- PASS run: `out/lab/20261127-145438` (`rivals_creature`, PASS) and full `make test`.
- no-fail-run: counter underflow observed in 6.5-hour playtest logs (`04BF38A7=-1`); twin switch `core.host_busy_resume_guard` prevents calling `Resume` when count <= 0.
