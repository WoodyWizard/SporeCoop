---
id: BUG-1079
title: tests: campaign finale stuck again: "Alice: zoomed out to the galaxy view" after the campaign's jump to the space fixture
status: open
kind: test
stage: Space
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests: campaign (finale)
signature:
docs:
related: BUG-0991
---

## Report
fix/w49-tests full campaigns on lane 4, lab 20261113-082316, 20261113-101808 and 20261113-060756: after the Civ end got
stuck (BUG-1073) the host jumped to the space fixture (`coop.load` in game, slot 11) and the finale reported
`[stuck] finale: Alice: zoomed out to the galaxy view`; the finale then went on through its shortcut and ended ok
(the guests flew to Alice). BUG-0991's fix (S1: the death caption, the repair) is in the branch; the host's ship was
not checked for a death caption in these runs (not analysed). In the runs without a space jump this was not seen.

## Root cause
Not analysed (outside the package). Inferred: a view state after the in-game load of the space fixture (the solar
system view not reached by the wheel), not the death caption of BUG-0991.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
