---
id: BUG-1212
title: creature_e84: since BUG-0722 (0.7.11) the host gives the guest's puppet a tuning, so the save no longer brings an untuned creature back (the test's precondition)
status: open
kind: test
stage: Creature
reported: 2026-11-16
source: final check 20261116-025154
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
Final check 20261116-025154: creature_e84 FAIL "the save brought the puppet back as a game creature without tuning
(0)" and "the host simulates it as an NPC". Reproduced on lane 10 on rel/0.7.15 (20261116-082820) and v0.7.14.1
(fix/w51-t2-v07141/out/lab/20261116-084702). Last PASS: 20261107-120856 (fix/w46-misc).

## Root cause
Observed: after the restart the host's load has no creature without tuning other than its own (`coop.npc untuned`
other=0). Since BUG-0722 (0.7.11, 2026-11-08) the host gives every partner puppet without an archetype tuning the
player's (`npc.puppet_tuning`, npc.cpp `puppet_tuning_tick`, five times a second); the puppet saved with `coop.savestrip 0`
then comes back with that tuning, not as the untuned creature of the report (saves of 0.7.3, made before both
BUG-0020 and BUG-0722). The test was never run on rel/main between 0.7.11 and this final check. An old failure of the
test's precondition, not of the mod.
Not changed (noted): with the strip on (normal play) no puppet goes into a save; a save made with the strip failing
would now hold a tuned puppet, which the stray removal (it looks for creatures without tuning in a herd like ours) would
not take for one; the crash of the report (0xD48634) needs the untuned one.

## Fix
coop_creature_e84.py: before the save the host switches `npc.puppet_tuning` off and makes the guest's puppet again
(`coop.puppet respawn`), checks it is untuned (`coop.npc untuned`: ours >= 1), then saves as before; the restart
brings the switch back on. The rest of the test is unchanged.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
