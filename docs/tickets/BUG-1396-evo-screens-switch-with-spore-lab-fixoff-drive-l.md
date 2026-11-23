---
id: BUG-1396
title: evo_screens_switch with drive.lead_gap off passed: the host's game moved none of its animals during its caption stand, so the BUG-1376 twin had nothing to show
status: fixed
kind: test
stage: Creature
reported: 2026-11-21
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w62-core
commits: a5ca9fe4 c6f93414
tests: tools/tests/coop_evo_screens.py (evo_screens_switch, SPORE_FIX_OFF=drive.lead_gap)
signature:
docs:
related: BUG-1376
---

## Report
Third final check of 0.7.16, the fix-off twins (out/night-0.7.16-c/1-fixoff.txt, lab 20261121-001418, lane 2):
evo_screens_switch with `SPORE_LAB_FIXOFF=evo_screens_switch=drive.lead_gap` PASSED, while BUG-1376 says it must FAIL on
"the guest's copies of the host's animals stand where the host's game has them after its caption". The switch was off
on both games (`SPORE_FIX_OFF: drive.lead_gap off` printed twice).

## Root cause
Test (the check is right, its precondition did not happen). Logs of the twin run against BUG-1376's run
20261120-133703:
- The host's caption stand happened in both alike: host trace `busy=cinematic,input_lock,paused` from s ~155 to ~220
  (twin: "host busy: none" at s 223.0; bug run at s 220.5), so the gap of the host's NPC states was ~65-70 s in both.
- The bug needs the host's game to have moved its animals during the stand (BUG-1376: "the body put elsewhere by the
  host's game"; its runs: the host's family members 10-15 m from where the guest's copies stood). In the twin run the
  check compared 10 animals near the host 8 s after it went on: none over 5 m; the guest's mirrored animals "nearest 11
  m -> 2 m" as the host's avatar walked, no sync-check difference: nothing of the host's had moved over the gap, so
  late_time had no walk to bring in and the switch made no difference. Whether the host's game re-places its family
  at the end of the caption depends on the run (where the family stood when the caption came), so the twin's FAIL was
  left to chance.

## Fix
coop_evo_screens.py --part switch: once the host's game stands under its caption the test puts one of the host's
animals within 25 m (one that the guest has a copy of) 12 m farther out on the host (`coop.npctrack put <id> x y z 0`,
onto the ground, not held; no NPC state goes out while the host's game stands), and after the host's game goes on it
checks that this copy was compared too (a check of its own: "the test moved one of the host's animals while its game
stood") besides the existing 5 m check. So the first state after the gap always carries a 12 m move: with the fix the
copy stands there at once, with `drive.lead_gap` off it crosses the line over a minute (net_tests test_lead_gap: 10.30
m left after 5 s). The claim of BUG-1376 stays; the test now makes it hold.

## Verification
Was waiting for the lab run:
- PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch --lane N --retry 0 --no-build`
- FAIL expected: `SPORE_LAB_FIXOFF="evo_screens_switch=drive.lead_gap" SPORE_DETECTOR=20 python3 tools/lab.py run
  evo_screens_switch --lane M --retry 0 --no-build` ("the guest's copies of the host's animals stand where the host's
  game has them after its caption" FAIL, the moved animal ~10 m off).
- The PASS of the twin before this change: 20261121-001418 evo_screens_switch.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): evo_screens_switch PASS in 20261123-015558 and 20261123-023643; the twin evo_screens_switch_lead_gap_off FAIL in 20261123-020905 (the moved animal's copy off after the caption). The twin's FAIL in 20261122-191140 and evo_screens_switch's FAIL in 20261122-194210 were the test's own precondition and gate (BUG-1437, BUG-1438). Closed.

## History
- 2026-11-21: opened.
- 2026-11-21: fix/w62-core: cause found in the twin's logs; the test moves one host animal during the stand.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
