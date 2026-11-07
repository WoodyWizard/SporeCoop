---
id: BUG-0440
title: Tests: chaos_tribe fails on the monitor's proxies gate (one Tribe NPC proxy over 3 m off the host's NPC)
status: not-reproduced
kind: test
stage: Tribe
reported: 2026-11-01
source: fix/w34-ability-pop smoke (lab 20261101-173425)
role: guest
versions: 0.7.3.3+
fixed_in:
branch: fix/w45-remote-bodies-2
commits: aed2f801
tests: chaos_tribe, play_tribe
signature:
docs:
related: BUG-0402, BUG-0503, BUG-0425, BUG-0610
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
No fix: the gate failure is not reproduced on 0.7.9. Diagnostics added (aed2f801): the audit line now has a second line with the driving's own view (its walk error, held, place lost, teleports, walked) and the nearest other proxy.

## Verification
no-fail-run: the gate (2 audit lines in a row) did not fire in 8 chaos_tribe runs of this branch: drive.lead on out/lab/20261106-052022 (2), -053713 (2), and the regression run; drive.lead off -062826 (2). Single audit lines (below the gate): 052022 #2 (proxy 25, placed on the path, 3.2 m off for 2.1 s while it moved 5.4 m at the host NPC's 2.5 m/s: an offset the guest's game kept, not a lag) and 062826 #2 with the lead off (proxy 17, driven, 4.0 m off a standing host NPC for 2.1 s, `place lost 281` frames since it walked, the nearest other proxy 3.3 m away). What blocks a fix of those single cases: the guest's game moves a placed proxy back (place lost) next to a herd mate; the function that separates creatures is not found (static RE 2026-11-06: creature-vs-creature pairs are skipped by the creature stage's contact handler 0xD3A450, so it is in Havok's solver or the locomotion's avoidance; no per-creature collision flag found). Needed: that function or flag (see BUG-0386), or a lab case that holds one proxy on purpose.

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
- 2026-11-06 (fix/w45-remote-bodies, M1): the monitor's audit compares a proxy with its OWN target (the host state
  the guest received), so the common 0.2 s lag of the target (BUG-0425) is not what it measures: this ticket is a
  proxy falling behind its target (the second case of the 2026-11-05 analysis, half the host's speed while placed),
  i.e. the guest's game not keeping the placing (a different mechanism from the lag; inferred: a guest-only obstacle
  or the proxies' collision, as BUG-0386). With drive.lead (walk_place's error closed in ~0.25 s, catch_prop) no
  `proxy_stuck` line in play_tribe / play_creature / play_civ of out/lab/20261106-021152 (0.7.8's runs on
  rel/0.7.8: 2 lines) and no stretch over 2 m longer than 0.46 s in 9 body_trail Tribe runs. A host jump (21 m, the
  host's own game) is seen and is BUG-0610. Not marked fixed: chaos_tribe's gate not rerun often enough to say.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): 8 chaos_tribe runs (6 with the lead, 2 without), no gate failure; 2 single audit lines with the new diagnostics (an offset the guest's game keeps next to a herd mate); closed not-reproduced, the blocker named.
