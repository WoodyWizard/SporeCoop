---
id: BUG-1376
title: Creature: after the host's game stood for a minute (its evolution caption), a guest's copies of the host's family members take a minute to reach the places the host's game put them in (evo_screens_switch sync-check n apart)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-20
source: final check 20261120-133703
role: guest
versions: 0.7.15.2,0.7.16
fixed_in: unreleased
branch: fix/w61-creature-b
commits: 14aa9dc9
tests: evo_screens_switch, net_tests test_lead_gap
signature: sync-check MISMATCH "n: N apart" on the host's family members (fam=1, species = the host's evolved) ~5 s after the host's evolution caption ended ("host busy: none" at s ~220)
docs: mods/coop/net/interp.h (late_time, BUG-1376), docs/re/creature.md#creature-caption-stand-gap
related: BUG-0650, BUG-0651, BUG-0674, BUG-0413, BUG-1216
switch: drive.lead_gap
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, lane 4): evo_screens_switch passed every check of its own but the lab
failed it on the sync-check gate: "MISMATCH with the host (Creature 9652F8AD, 2 checks 3 s apart, 26 objects compared):
n: 3 apart (n15 14.6 n21 11.5 n17 10.6)" at s 225. The same at the same moment in out/lab/20261118-125715 (fix/w53-
cellvote, "n: 4 apart" 12-15 m) and 20261118-134852 (rel/0.7.15.2, 10-18 m); the other runs passed because the lab's
check (every ~30 s) fell after the test had ended.

## Root cause
* Observed (host and guest game logs of the three runs): the apart NPCs are the host's family members (host: `family`,
  species = the host's evolved one; guest: proxies with `fam=1`, `age=0.06` = their states arrive). The host's game stood
  from s ~150 to ~220 under its evolution caption ("It is the beginning of a new day", `busy=cinematic,input_lock,
  paused`; the test confirms it with Enter at its "45 s later" check); "host busy: none" at s 216-221, the mismatch
  4-5 s later. On the host the members stand or walk 1-6 m from its avatar; the guest's copies stand on their target
  (`err` 0.5-0.7 m) 10-15 m away, all near one place (the old one).
* Observed (code, net/interp.h): while the host's game stands, host_tick sends no NPC state (no creature updated;
  frozen_send only spawns / retires). The guest drives its proxies every frame meanwhile, so late_time's shown time
  follows the lead. At the first state after the stand (70 s later, the body put elsewhere by the host's game) late_time
  takes it for late news of a walk: it searches the path between the state before the gap and the new one for the point
  nearest where the target was going (the old place), steps the shown time back to the gap's start (`bd` ~0 < kLateLost)
  and catches up at 2 x: the target crosses the 70 s straight line at 1/35 of the distance per second, so the copies
  stand near the old place for about a minute.
* Reproduced in the unit test (net_tests test_lead_gap, the proxies' buffer settings of npc.cpp): a standing animal, the
  host's states stop for 70 s, the host's game puts it 12 m away: 5 s after the states come again the target is 10.30 m
  from the new place (the lab: 10-15 m), with the fix 0.00 m.

## Fix
`drive.lead_gap` (net/interp.h late_time): a new state after a gap of more than kLateGap (2 s) of the sender's states is
no late news of a walk: the shown time is the lead's at once (as after a put; the body closes the error by its own
rules). Latency spikes (0.3-0.7 s, BUG-0650 / 0651 / 0674) are under the gap and keep their catch-up. Off: brought in
over the gap (0.7.16 rc). Test: evo_screens_switch now checks, 8 s after the host's game went on after its caption, that
no guest copy of the host's animals near it stands over 5 m off in 3 reads 1 s apart (no longer left to the lab's
sync-check slot).

## Verification
* Unit: net_tests test_lead_gap: off 10.30 m, on 0.00 m (make test).
* Lab: open, waiting for the lab run (no game today, owner rule):
  `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch evo_screens_switch_lowfps --lane N --retry 0 --no-build`
  must PASS (the new check and the sync-check gate);
  `SPORE_FIX_OFF=drive.lead_gap SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch --lane M --retry 0 --no-build`
  must FAIL on "the guest's copies of the host's animals stand where the host's game has them after its caption".
  Regression: npc_walk_novel, npc_walk_fps30, tribe_walk_player, body_trail_tribe_player, puppet_walk_novel.
no-fail-run: lab runs pending (owner rule: no lab today); the FAIL is reproduced by the unit test with the switch off

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-creature-b): root cause in late_time (a path across the host's 70 s stand), fixed with drive.lead_gap; evo_screens_switch checks the copies after the caption.
- 2026-11-21: fix/w62-core: the fix-off twin passed in lab 20261121-001418 (nothing moved over the gap); the test now moves one host animal during the stand (BUG-1396).
