---
id: BUG-0394
title: tests: puppet_walk fails on lane 6 on main 15539ad (3 of 3 runs, each on another check)
status: open
kind: test
stage: Creature
reported: 2026-11-01
source: fix/w31-creature-gate regression run 2026-11-01
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
puppet_walk on lane 6 (GOG 2024), 2026-11-01:
* fix/w31-creature-gate d89e8c5: lab 20261101-100420 (host watches guest: walk gait 0 % of 552 frames, the
  puppet's animation 02481DE5 the whole walk) and 20261101-103812 (2 runs: "guest watches host: no jump
  of the puppet while it walks (2)"; "stands in 5 % <= 5 %, at its speed 88 %").
* main 15539ad itself (a detached worktree, the same lane): lab 20261101-104255, 3 of 3 FAIL, each on
  another check (the walker walked only 94 / 0 frames; stands 27 % / 9 %; walk gait 77 %).
`lab.py coverage`: 21 of 37 recorded runs passed. The branch changes nothing in the puppet driving.
Reproduce: `lab.py run puppet_walk --lane 6 --repeat 3`. Not triaged (not part of the branch's task):
the walker itself not walking (0 / 94 frames) points at the test's input (WASD to a background
window), the rest at the puppet driving.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
