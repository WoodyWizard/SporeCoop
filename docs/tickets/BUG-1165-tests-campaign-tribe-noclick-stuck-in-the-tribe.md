---
id: BUG-1165
title: Tests: campaign_tribe_noclick stuck in the Tribe advance at 30 min on v0.7.14.1 and rel/0.7.15 alike (the planted 18 min leave too little of the advance budget)
status: open
kind: test
stage: Tribe
reported: 2026-11-15
source: lab 20261115-154852 (v0.7.14.1), feat/w51-retire 20261115-133237
role: host
versions: v0.7.14.1, rel/0.7.15
fixed_in:
branch: feat/w51-tribe
commits:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
