---
id: BUG-1259
title: creature_eat: a body eaten to the end is gone in one game and at negative meat in the other at the check
status: open
kind: test
stage: Creature
reported: 2026-11-18
source: lab: fix/w53-puppet
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
`creature_eat` failed once (out/lab/20261118-124624, lane 5, fix/w53-puppet): "the guest: the other game shows the body
#109 eaten as far (meat -31.5 here, None there)". The meat followed in both games within 1.5 all the way (100 -> -0.96
in 9.4 s); 3 s later the host's game had removed the eaten body (None) while the guest's still had it at -31.5. The
check accepts both None or both within 5 %, not one removed and the other below 0. Earlier passes show the same race
ending either way (20261117-154911: "None here, None there"; its host round "-28.83 here, -28.61 there"). The rerun
passed (out/lab/20261118-125657).

## Root cause
Inferred: the test's check, not the game: a body eaten to the end (meat <= 0) is removed by each game on its own
schedule. Fix idea: treat meat <= 0 like None in the check.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
