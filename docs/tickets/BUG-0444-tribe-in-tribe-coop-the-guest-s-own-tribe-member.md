---
id: BUG-0444
title: tribe: in tribe_coop the guest's own tribe members are 2.6-3.3 m under its ground for ~20 s (monitor rule ground)
status: open
kind: desync
stage: Tribe
reported: 2026-11-01
source: tribe_coop (smoke), lab 20261101-183414 (feat/w33-tests-session, main 6ac351e merged)
role: guest
versions: main 6ac351e
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`tribe_coop` (smoke, coop_tribe.py, tribe fixture, cold, lane 10) passed its own checks and failed by main's
monitor rule `ground` (feat/w32-monitor-diag): `MONITOR-VIOLATION ground guest: the tribe members of guest 3.31
m under the ground (Tribe) on 2 lines from s 61`. The guest's trace lines: `agl=-2.64` (s 60.6) and
`agl=-3.31` (s 70.1), 0.00 before and after; the minutes of the test where the host walks the guest's member
9 m, gives action and hunt orders (lab 20261101-183414). The run before on lane 9 (lab 20261101-180254) failed
two UI checks instead (box select, right-click on a tree), not this rule; tribe_coop passed 162 of 165 runs
before the rule existed.

How to reproduce: `tools/lab.py run tribe_coop --repeat 4 --retry 0 --lane N`, the guest's `agl=` lines.

## Root cause
Not analysed (another branch's area). Unknown whether the members are drawn under the terrain or the agl of a
member is taken while it is in a hut / a cave mesh above the terrain sample.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
