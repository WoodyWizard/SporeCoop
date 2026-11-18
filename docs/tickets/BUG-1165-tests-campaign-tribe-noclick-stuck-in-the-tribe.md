---
id: BUG-1165
title: Tests: campaign_tribe_noclick stuck in the Tribe advance at 30 min on v0.7.14.1 and rel/0.7.15 alike (the planted 18 min leave too little of the advance budget)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-15
source: lab 20261115-154852 (v0.7.14.1), feat/w51-retire 20261115-133237
role: host
versions: v0.7.14.1, rel/0.7.15
fixed_in: 0.7.15
branch: feat/w51-tribe
commits: 3effe3dd
tests: campaign_tribe_noclick
signature:
docs: docs/06-testing.md#testing-tribe-wave-c
related: BUG-1091, BUG-1096, BUG-1101
---

## Report
campaign_tribe_noclick (BUG-1091's twin: no bot click on village 2 for 18 min from the first raid) fails as "[stuck]
tribe/advance: the stage change tribe -> civ did not finish in 30 min" on rel/0.7.15 + B1 (feat/w51-retire
20261115-034753 goal bar 1/5 at the end, 20261115-133237 3/5) and passes on 29c4c260 (20261115-145821, the host in
Civ after 1526 s). It fails on rel/0.7.14 too (20261113-231423 5/5 but still in the editor, 20261114-005006, passes
20261114-050940 after 1565 s) and on the tag v0.7.14.1, same lane, same command (20261115-154852: 4/5, then "no
village left to raid" for 7 min).

## Root cause
A test budget, not a mod regression (observed, the timelines of the six runs above): village 2 can fall only after the
18 planted minutes (its raids are refused the host's click until then, the driver puts it aside and raids the others
first). The passing runs reached the full goal bar after 22-23 min and the Civ stage after 25.5-26 min of the 30, so
whatever costs 4 more minutes ends the run: a raid of 300 s that does not fell its hut (village 4 in 133237), the host's
game over and its reload (the goal bar back to 0 in 154852, the players' hut raided by an NPC village while the bots
waited), the editor at the end (231423: 5/5 at 29 min). v0.7.14.1 fails the same way, so 0.7.15 did not break it.
Inferred, not analysed: why no 5th village formed after the reload in 154852 (`coop.tribe form` every 80 s formed
none; the reload had left 4 NPC villages).

## Fix
tools/tests/coop_campaign.py `advance_minutes`: with CAMPAIGN_PLANT_NOCLICK the Tribe advance gets the planted minutes
on top of 20 (38 min for the suite's 18), unless `--advance-minutes` is given; `CAMPAIGN_OLD_NOCLICK_BUDGET=1` keeps
the 30 min. suite.py: the entry 50 min, timeout 4200 s. No mod change.

## Verification
- FAIL with the old 30 min budget: v0.7.14.1 lab 20261115-154852 (lane 5, the tag's own build in the worktree
  tag/v0.7.14.1-w51tribe: stuck 4/5), feat/w51-retire 20261115-133237 (3/5) and 20261115-034753 (1/5), rel/0.7.14
  20261113-231423 and 20261114-005006.
- PASS with the new budget on feat/w51-tribe (f0181eb5 build): 20261115-171708 (the host in Civ after 1580 s) and
  20261115-163301 (the campaign's RESULT: OK, the host in Civ after 1691 s; the lab failed the run on a sync-check
  gate of one NPC proxy, n139 12.6 m apart on one guest once: BUG-0402's class, not the Tribe members; History of
  BUG-0402). Neither pass needed more than 30 min, so the runs show the new budget does not break the test; the FAIL
  runs show the 30 min were too few in 4 of 7 runs.

## History
- 2026-11-15: opened (feat/w51-tribe), the v0.7.14.1 comparison run, the budget.
- 2026-11-18: released in 0.7.15.
