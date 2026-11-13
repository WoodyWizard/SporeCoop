---
id: BUG-1077
title: attacker_owner fails the monitor's posse_damage gate in 3 of 6 recent runs (the host hit by its own pack)
status: open
kind: test
stage: Creature
reported: 2026-11-13
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: attacker_owner
signature:
docs:
related: BUG-0790, BUG-0417
---

## Report
attacker_owner (BUG-0790's test: the host's Drama Manager makes its UFO, the UFO hits the guest's puppet) fails
the invariant monitor's posse_damage gate now and then, cold and warm alike: `posse_damage: host: player 1 hit by
its own party (pack, posse=1) for 0.1 hp forwarded`. Recorded: lab 20261112-073116 (lane 7, cold),
20261112-094823 (lane 6, cold), 20261113-094538 (lane 7, cold start after a refused warm reuse, fix/w49-warm);
passed in 20261112-080216 and 20261112-175447. Found while moving tests to warm resets (fix/w49-warm): not a warm
reset effect, the test stays as it was (cold).

## Root cause
Not analysed. Inferred: the UFO's area attack also hits a member of the host's pack, which then hits the host back
(the same pattern as BUG-0417's flee phase); or the forwarded 0.1 hp is the UFO hit reattributed to the pack.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
