---
id: BUG-0266
title: campaign: the game logs of the extra guests (instances 20+N, 30+N) are looked up in a wrong lane and never saved
status: fixed
kind: tooling
stage: 
reported: 2026-10-29
source: lab 20261029-070811 campaign
role: 
versions: 
fixed_in: 0.7.3.1
branch: fix/w6-campaign
commits: 9f875d3
tests: campaign
signature:
docs:
related:
---

## Report
In campaign#2 (lab 20261029-070811) the findings of Carol and Dave (instances 24 and 34) had
"(no log: FileNotFoundError)"; the lab did not keep their logs either, the next start of the lane
rotated them away.

## Root cause
lane.game_log mapped an instance to lane (inst + 1) // 2: instance 24 to lane 12, which does not
exist. lab.save_game_logs copied the host and guest logs only.

## Fix
game_log maps 20+N and 30+N to lane N; save_game_logs also keeps <test>.extra1/extra2.game.log
when the test wrote them (file time after the start). The lab now creates only the lanes it uses
(it ran `lane create` for 2..7 for --lane 2 --lane 4 --lane 7).
switch: none (test tooling)

## Verification
no-fail-run: test tool change; checked by hand: lane.game_log(24) -> lane4/.../sporekit.24.log, game_log(30) -> lane10; lab_selftest OK.

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w6-campaign.
- 2026-10-29: released in 0.7.3.1.
