---
id: BUG-0953
title: Creature: at a guest's death in a hunt the host's monitor saw the guest's puppet at 2/15 health while the guest's game had 0/15 (long_creature, seen once)
status: fixed
kind: test
stage: Creature
reported: 2026-11-12
source: fix/w48-freed lab 20261112-013858 long_creature (lane 7)
role: host
versions: 
fixed_in: unreleased
branch: fix/w48-freed2
commits: 30183093
tests: make test (gates_selftest: monitor puppet_hp)
signature:
docs: docs/06-testing.md#testing-freed2-w48
related:
---

## Report
long_creature on fix/w48-freed (lab 20261112-013858, lane 7, 40 min, events death/editor/menu/restart/rejoin): all
checks passed, no faults; the lab failed the test on one invariant monitor line 2380 s in, right after the rejoin
event and around the guest's death in a hunt (guest.died=1): `puppet_hp host: host shows guest's creature at 2/15,
guest's own game 0/15`. Not analysed (not in the L1 package); the monitor rule and the health relay (BUG-0414,
creature_hp) are the places to look. Not known whether 0.7.12 shows it.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in the logs (fix/w48-freed2): not a wrong health on the puppet but the ~0.15 s state lag right at two jumps
of the guest's health, on two trace lines in a row. s 2370: the guest was "saved from death" 20 ms before its line
(6/15), the host's line had the state before it (0/15). s 2380: the host forwarded an NPC's hit at 2380.000, the guest's
line at 2380.009 had 0/15 (the killing blow), the host's 2/15. At s 2390 the host showed the guest dead (flags 0x11).
The two lines were off in opposite directions (+6, -2); the rule (`puppet_hp`, php_n 2) counted them as a puppet that
keeps a wrong health. Confidence: high.

## Fix
tools/tests/monitor.py PuppetHp: the lines in a row must be off the same way (the puppet steadily over, or under, its
player's own health, or another max). Switch: the monitor setting `php_same_way` (0: the rule before).

## Verification
Replay of the recorded logs (monitor.py replay) of lab fix/w48-freed 20261112-013858 long_creature:
* FAIL (php_same_way=0): `puppet_hp long_creature.host.game.log: ... at 2/15, ... own game 0/15 (s 2380)`.
* PASS (php_same_way=1): no violation. gates_selftest: lines off in opposite directions pass, the planted puppet pinned
  to full health still fails (f_php, f_php_flip, f_php_flip_old).
* no-fail-run: a lab FAIL of this test-rule defect is the recorded run itself (out/lab/20261112-013858 in
  fix/w48-freed); PASS: the replay above and long_creature on fix/w48-freed2 (see History).

## History
- 2026-11-12: opened.
- 2026-11-12: fixed on fix/w48-freed2 (monitor rule; not a mod defect).
