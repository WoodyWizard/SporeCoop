---
id: BUG-0547
title: Space: sync-check MISMATCH p0 46 m while the host flies a rescue on the home planet (space_rescue gate)
status: open
kind: test
stage: Space
reported: 2026-11-04
source: lab 20261104-114611 space_rescue#3 (fix/w42-space-play, lane 2, GOG 2024)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature: sync-check: MISMATCH ... (Space 40460035 ...): p: 1 apart (p0 46.2)
docs:
related: BUG-0378, BUG-0193
---

## Report
coop_space_rescue.py printed RESULT: OK, the lab failed it on the sync-check gate: host and guest both logged
`sync-check: MISMATCH ... p: 1 apart (p0 46.2)` at s 78.1, 4.5 s after the guest was disabled, while the host flew
the ~380 m to it with the keys (the test's `Player.rescue`, pulses of W/A/S/D) on the home planet 40460035. The
round had started with the ships 319 m apart (other runs: 24-180 m). 2 of 3 runs of the same build passed (lab
20261104-114611 #1, #2). Inferred: the speed slack (`sync.speed_slack`, BUG-0378) uses the speed at the read, and
a ship that accelerates or stops between pulses is further behind than that speed explains; or the drawn host ship
really lags on a planet at rescue speed. Not analysed (not this branch's change: the rescue flight is unchanged).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-tests): G1: from the host's log the host flew ~330 m in ~8 s with key pulses; the slack uses the speed at the read (drive::speed of the newest state), so a ship between two pulses may be read slower than the path the drawn copy trails. Not reproduced, no gate change (a change needs a run where the gate is wrong and one where it is right).
