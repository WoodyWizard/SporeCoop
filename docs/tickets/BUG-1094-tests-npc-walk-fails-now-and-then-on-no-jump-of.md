---
id: BUG-1094
title: Tests: npc_walk fails now and then on 'no jump of a moving proxy' (a hit NPC's fleeing proxy steps 0.75-1.13 m in single frames at t 45-47 s)
status: open
kind: test
stage: Creature
reported: 2026-11-13
source: fix/w49-proxy2 regression (lane 4)
role: guest
versions: 0.7.14 (rel/0.7.14 + fix/w49-proxy2)
fixed_in:
branch:
commits:
tests: npc_walk
signature: npc_walk `FAIL no jump of a moving proxy (4-6)`, all of one hit walker, 0.75-1.13 m steps of 12-16 ms frames at t 45.3-45.6 and 47.0-47.3 s
docs: docs/known-issues.md#issue-npc-walk-hit-jumps
related: BUG-0409, BUG-0440, BUG-0445
---

## Report
npc_walk (Creature, lane 4) failed 3 of 6 runs on fix/w49-proxy2 on its own check "no jump of a moving proxy": one
of the two walkers the host's avatar hits (`coop.npc hosthit`) steps 0.75-1.13 m in single frames on the guest's screen,
always in two bursts of 3 about 0.11 s apart at t 45.3-45.6 s and 47.0-47.3 s (the flee after the hit):
out/lab/20261113-202612 (4 jumps, #18), -204146 #2 (6, #35), -204750 (5, #20). Passed: -194347, -204146 #1, -204521.

## Root cause
Not analysed. Observed: -204750 ran with BUG-0440's npc.proxy_hold limited to Tribe (no change of the Creature
driving in that build: the hold is off, the rest of the branch is test commands and log lines) and still failed, so
it is not that fix; whether rel/0.7.14 itself fails as often was not run. Inferred: the walk_place keep_lost /
snap handling of a hit-thrown NPC (BUG-0409's class).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
