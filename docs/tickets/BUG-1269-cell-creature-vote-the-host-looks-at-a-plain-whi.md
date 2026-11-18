---
id: BUG-1269
title: Cell -> Creature vote: the host looks at a plain white screen (the Cell-end white fade held) while the guest still sees the cinematic
status: open
kind: bug
stage: Cell
reported: 2026-11-18
source: key-frame review
role: host
versions: 0.7.15.1
fixed_in:
branch:
commits:
tests: evo_screens_switch, evo_screens_switch_lowfps, evo_screens_switch_noready, evo_screens_switch_slow
signature:
docs:
related:
---

## Report
Key-frame review (0.7.15.2 final check, lab 20261118-144132, sheets 048/050/052/054 `*_01_switch_vote`): during the
switch vote at the end of the Cell stage the host's screen behind the co-op lobby is plain white (the game's
Cell-end fade to white, held while the vote waits), the guest's shows the Cell-end cinematic ("Congratulations!
You're on the path to sentience!"). The same in 0.7.15.1 (lab 20261118-110806: the white share of the host's frame
0.46 in all four variants). The tests pass (they check for black, not white).

## Root cause
Not analysed. Inferred: the host's cinematic reached its white end and the stage switch waits for the vote.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened; pre-existing, not a blocker of 0.7.15.2; planned for 0.7.16.
