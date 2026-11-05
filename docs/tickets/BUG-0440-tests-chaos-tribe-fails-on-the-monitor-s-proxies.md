---
id: BUG-0440
title: Tests: chaos_tribe fails on the monitor's proxies gate (one Tribe NPC proxy over 3 m off the host's NPC)
status: open
kind: test
stage: Tribe
reported: 2026-11-01
source: fix/w34-ability-pop smoke (lab 20261101-173425)
role: guest
versions: 0.7.3.3+
fixed_in:
branch:
commits:
tests: chaos_tribe
signature:
docs:
related: BUG-0402, BUG-0503
---

## Report
Smoke on lane 4 (lab 20261101-173425, fix/w34-ability-pop: changes only coop.track columns and the puppet_fight
test, nothing in Tribe): chaos_tribe failed on the log gate `monitor: 1 on guest (proxies): proxies: guest: 1 NPC
proxies over 3 m off the host's NPC (Tribe) on 2 lines from s 60`; the test's own checks passed.

## Root cause
Same line as BUG-0503 (the monitor's proxies rule on a Tribe animal proxy, "placed on the host's path"): see
BUG-0503 (a slow guest's placing capped at 0.1 s frames).

Update (2026-11-05, rel/0.7.7 final check, out/lab/20261105-015320 play_tribe): the same gate in play_tribe at s 410
(4 `proxy_stuck=1` trace lines in the run, 2 in a row). The guest ran at 68 fps (perf min 65), so not BUG-0503's slow
frames (npc.proxy_slow_frames is on). The line: "proxy 200 (species 06805974) 7.1 m off the host's NPC for 2.1 s,
host's speed 0.0 m/s (placed on the host's path)", then 3.1 m at 2.5 m/s 15 s later. A standing host NPC 7 m from its
placed proxy suggests the host's NPC jumped (a teleport or a snap by the host's game), not a proxy falling behind.
The same lines are in the pre-merge branch runs of 0.7.7 (fix/w43-detector2 20261104-203017: 5, fix/w43-world-state
20261104-210354: 5, fix/w43-transitions 20261104-232331: 8) and 0.7.4-0.7.6 runs (1 each): not a 0.7.7 regression.

## Fix
See BUG-0503 (npc.proxy_slow_frames).

## Verification
no-fail-run: covered by BUG-0503 (npc_walk_slow FAIL 20261104-232034 / PASS 20261104-232210); chaos_tribe itself not rerun.

## History
- 2026-11-01: opened (fix/w34-ability-pop).
- 2026-11-04 (fix/w43-tests): the same proxies line as BUG-0503; fixed there (not marked fixed here until chaos_tribe runs clean).
- 2026-11-05 (fix/w44-height): the update's 7.1 m was measured at the proxy's distance from the centre (monitor_tick), so it is no height difference; not changed.
- 2026-11-05 (fix/w44-tests, G2): is the gate's line a host snap (the proxy late by a moment) or a proxy falling
  behind? The audit line now says how far the proxy was off when it went off, how far it moved since, and the largest
  step of the host's NPC between two audits (0.25 s) in the last 3 s (commit 69a9065a). Observed in play_tribe#2 of
  out/lab/20261105-135000 (lane 9, 2 lines, not in a row, so the gate did not fire): proxy 663 (species 0680AA69) 3.4 m
  off for 2.1 s, host's NPC running 5.5 m/s, off at 3.3 m, the proxy moved 11.2 m in those 2 s, the host's largest step
  1.5 m per 0.25 s (its running speed); proxy 624 (species 06808ACA) 6.5 m off for 2.0 s, the host's NPC at 2.5 m/s,
  off at 3.5 m, the proxy moved only 2.4 m in 2 s, the host's largest step 0.6 m per 0.25 s. No host snap in either:
  the host's NPC moved at its walking / running speed and its proxy fell behind it (624: at half its speed). So the
  gate measures a real desync, not a brief lag after a jump: the gate and its window stay; no proxy-snap change (it
  would not touch these cases). Kind: desync (the game's walk of a proxy placed on the host's path not keeping up),
  not a test fault. Not analysed further: why proxy 624 moved at half the host's speed while placed (an obstacle on
  the guest's ground, the placing not kept: `coop.npctrack` rows of such a proxy are the next step). Open.
