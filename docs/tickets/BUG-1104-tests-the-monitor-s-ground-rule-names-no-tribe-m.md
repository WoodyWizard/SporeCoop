---
id: BUG-1104
title: Tests: the monitor's ground rule names no tribe member under the ground (host: partner 1's members 1.26 m under for 20 s in the campaign)
status: open
kind: test
stage: Tribe
reported: 2026-11-14
source: final check 0.7.14, lab 20261114-005006 campaign
role: host
versions: rel/0.7.14 4dd8d2be
fixed_in:
branch: fix/w49-fcgates
commits: 0a79fb2e
tests: campaign (the monitor gate), gates_selftest
signature: MONITOR-VIOLATION ground host: the tribe members of partner N 1.26 m under the ground (Tribe) on 2 lines
docs: docs/06-testing.md#testing-fcgates-0714
related: BUG-0945, BUG-1088, BUG-0444
---

## Report
The full campaign of the 0.7.14 final check (lab 20261114-005006, lane 2) ended with `RESULT: OK` and failed the lab's
monitor gate: `ground: host: the tribe members of partner 1 1.26 m under the ground (Tribe) on 2 lines from s 1790`.
The brief asked whether BUG-1088 (monitor.tribe_water, new in 0.7.14) misjudged a member near water.

## Root cause
Observed (logs): the host's trace lines for partner 1 (Bob) read `agl=-1.26` at s 1790 and s 1800 and -0.01 at s 1810
(two lines, 10-20 s); Bob's own game read its members at -0.00 (its mirrors are put onto its ground, BUG-0444). The
host's game ran at 10 fps that minute (`frames: present ... 10.3 fps`, min 2 fps), an epic creature came at s 1775.
Not BUG-1088: member_agl_of only lowers a height to `min(h, max(0, r - water))`, never below 0, so a negative value is
the terrain reading of 0.7.13 itself. Not new in 0.7.14: the same reading on the host for a partner's member was in
the 0.7.10 campaign (lab 20261107-141346, partner 2 -1.16) and three single lines in campaign_tribe_end runs of
Nov 13 (-1.05, -1.50, -1.15); no other test's host logs have it (all Nov labs). Which member and why it stood 1.26 m
under could not be told: nothing in the logs names it (the BUG-0945 air watch names members over the ground only).
Inferred: a real member of the host's game (the guest's own members are not under), rare (8 trace lines in all of
November), only in the 4-player campaigns under load.

## Diagnostics (fix/w49-fcgates)
Diagnostics, no behaviour change: the host's and guests' tribe watch (coop.cpp tribe_air_watch, 4 times a second) logs a
tracked member more than 0.5 m under the ground once per stay ("tribe: member #N of player P is X m under the ground at
(...): <node, hp, tool, item> speed=.. over the game's surface .., over the water ..") and when it is on the ground
again; the monitor's ground finding for Tribe points to that line. The next campaign that hits it names the member,
its behaviour node and whether it stands in water. Left open as a class in docs/known-issues.md.

## Verification
no-fail-run: diagnostics only (a log line and a finding text); gates_selftest and make test pass.

## History
- 2026-11-14: opened on fix/w49-fcgates (final check 0.7.14 triage); diagnostics added, the cause stays open in
  known-issues (the next run names the member).
