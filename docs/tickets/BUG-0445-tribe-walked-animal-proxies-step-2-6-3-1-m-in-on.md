---
id: BUG-0445
title: Tribe: walked animal proxies step 2.6-3.1 m in one frame on the guest's screen (npc_walk_tribe fails 4 of 6 on lane 7)
status: not-reproduced
kind: desync
stage: Tribe
reported: 2026-11-01
source: lab 20261101-180236, 20261101-183139, 20261101-184014 npc_walk_tribe, lane 7 (fix/w35-monitor-gates)
role: guest
versions: main 6ac351e
fixed_in:
branch: fix/w46-k3-bodies
commits:
tests: npc_walk_tribe (tools/tests/coop_npc_walk.py)
signature:
docs: docs/re/tribe.md#tribe-lead-keep
related: BUG-0361, BUG-0650, BUG-0674
---

## Report
`npc_walk_tribe` (TRACK_FIXTURE=tribe coop_npc_walk.py, cold and warm, lane 7, clean network) failed 4 of 6 runs on
its own check "no jump of a moving proxy (Tribe: at most 3 under 2.5 m)": out/lab/20261101-180236 (one step of
3.11 m in a 16 ms frame on the guest's screen), 20261101-183139 #1 (2.69 m in a 41 ms frame, 2.96 m in 14 ms),
20261101-184014 #1 (5 steps 2.56-3.06 m in 15-16 ms frames; at s 38.78 three walked proxies, #29, #30, #44, stepped
2.6-2.9 m in the same frame), #3 (no jump; "stand in 6 % <= 5 %"). 20261101-184014 ran main's own coop_npc_walk.py
(this branch's test change does not touch Tribe: the Tribe host lists no family), the build is main's mod plus a
trace field. Passing runs had 0 jumps (20261101-183139 #2, 20261101-184014 #2; 20261101-153010 on
fix/w33-proxy-ground). Earlier passes after BUG-0361: 20261101-085045 (3/3), 094111, 125335 (fix/w30-npc-walk).

How to reproduce: `tools/lab.py run npc_walk_tribe --repeat 3 --lane N`; the test prints `jumps on the guest's
screen (id, t, m, frame ms)`; the per-frame rows are in out/laneN/track/npc_walk_tribe_*.json.

## Root cause
Not reproduced on 0.7.9 (rel/0.7.9 + M1's drive.lead). Observed (2026-11-06, fix/w45-remote-bodies-2): npc_walk_tribe passed 8 of 8 runs on code without this branch's tried changes: out/lab/20261106-060825 (3 runs), 20261106-061720 (3 runs, with a tried lead smoothing switched off: the same code), 20261106-060427 (2 runs, drive.lead off); M1's 20261106-021152 (1). The 4 FAILs seen on this branch (20261106-052022 x2, 20261106-053713 x2) came from a tried BUG-0409 change (NF_THROWN: Tribe animals flagged as thrown by a one-frame speed, placed instead of walked, extra states sent every frame), since removed: in 053713 the 2.74 m step was the lead target moving 1.8 m back and 1.6 m on in two frames between states of 6.5, 3.7 and 6.5 m/s (bunched extra states), then walk_place's snap (err > 2.5 m). Inferred for the 2026-11-01 failures (several proxies stepping in the same frame): late / bunched host states with the 0.7.3 walk driving (npc.walk_tribe before BUG-0503 / BUG-0425); not seen since.

## Fix
None needed on 0.7.9. A lead smoothing (drive.lead_smooth: a new state's step of the lead target spread over 0.25 s) was written and removed: no run without it failed.

## Verification
no-fail-run: not reproduced in the reported conditions (clean network, both games at full speed) on the 0.7.10 code: npc_walk_tribe out/lab/20261107-101948 3 of 3 (lane 6), 0 jumps; earlier 8 of 8 on 0.7.9 (below). Over the low fps / bad network variant (SPORE_FPS=30 SPORE_NET=player) it fails, also on 0.7.9's driving: out/lab/20261107-103414 0 of 2, -112016 (lane 6) and -112016-2 (lane 7, SPORE_FIX_OFF=drive.lead_late_npc, the 0.7.9 proxies' lead) alike: that is BUG-0674's mechanism (the lead extrapolated through a latency spike or loss burst while the NPC slows or turns: the lead target 4-5 m off when the next state comes, walk_place's snap teleports the proxy there), now tracked there.

## History
- 2026-11-01: opened (fix/w35-monitor-gates, seen in its regression runs).
- 2026-11-05 (fix/w44-height): not a height problem (steps along the path); not looked at.
- 2026-11-05 (fix/w44-tests, G2): the proxies gate lines of BUG-0440 seen in G2 (out/lab/20261105-135000 play_tribe#2)
  were proxies falling behind a host NPC that walked or ran at its speed, with no jump of the host's NPC (the audit
  line's largest host step 0.6-1.5 m per 0.25 s); this ticket's one-frame steps of 2.6-3.1 m were not looked at.
- 2026-11-06 (fix/w45-remote-bodies, M1): npc_walk_tribe passed with drive.lead (out/lab/20261106-021152, one run).
  The lead removes the 0.15 s delay of the target, not the guest's own collisions (this ticket's steps of several
  proxies in the same frame: the herd pushing, inferred). Not reproduced, not marked fixed (one run).
- 2026-11-06 (fix/w45-remote-bodies-2, M2): 8 of 8 PASS on 0.7.9 code; the branch's own FAILs traced to a tried BUG-0409 change (removed); closed not-reproduced.
- 2026-11-07 (fix/w46-k3-bodies, K3): the reported case (clean network) passes 3 of 3; the player-network variant reproduces 2.5-5.6 m proxy jumps on 0.7.9 and 0.7.10 code alike, decomposed from the track (lead target steps, a snap teleport each): the lead through a gap. A fix by drive.lead_late for the proxies (drive.lead_late_npc) was written, measured (no difference A/B) and reverted. Closed not-reproduced for the reported case; the variant goes to BUG-0674.
