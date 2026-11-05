---
id: BUG-0503
title: Tribe: an animal proxy placed on the host's path stays 3.5-8 m off the host's NPC for 2-11 s (monitor rule proxies)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-03
source: BUG-0481 analysis, lab 20261103-012624
role: guest
versions: 
fixed_in: 0.7.7
branch: fix/w43-tests
commits: 83e7dafc
tests: npc_walk_slow, play_tribe
signature:
docs: docs/06-testing.md#testing-harness-g1
related: BUG-0481, BUG-0361, BUG-0399, BUG-0440, BUG-0365
---

## Report
Split from BUG-0481 (play_tribe's third failure kind). The lab's monitor rule `proxies` failed play_tribe#3 of
out/lab/20261103-012624 (main checkout): `guest: 1 NPC proxies over 3 m off the host's NPC (Tribe) on 2 lines
from s 500`; the guest's log: `npc: proxy 5 (species 0680AA69) 5.2 m off the host's NPC for 2.1 s at (365.2
-51.6 347.4), host's speed 5.5 m/s (placed on the host's path)`, then 4.6 m for 2.4 s at 1.6 m/s; 20 s after
`an epic creature comes!`. The same run's campaign_tribe#3: proxy 54 (species 065CBE70) 3.5-7.4 m off for
2.1-11.3 s on both extra players, proxy 125 7.9 m for 2.3 s, all "placed on the host's path".
To reproduce: play_tribe (10 min) on the tribe fixture; seen in 1 of 3 play_tribe runs of 20261103-012624,
not in the 2 runs of fix/w40-play before its final check (20261103-150636, -152720).

## Root cause
* Observed (scratch play_tribe with coop.npctrack on both games, lane 3, 2026-11-04, under lab load): the one
  `off` line (proxy 225, 7.1 m for 2.4 s, "placed on the host's path") came from a guest at 3-5 fps (rows 0.15-0.5 s
  apart). Its placed position (after the mod's placing, not only the drawn one) fell 0.2-0.4 m further behind the
  host's state every frame of a 4 m/s walk: 0.1 -> 7.3 m in 6 s, then the NPC stood and drive::step took over.
* Observed (code, driver.h walk_place): the error is closed at (speed + catch_up 3 m/s) x the frame time, the frame
  time capped at WalkPlace::max_dt = 0.1 s: at 4 fps 0.7 m per frame against 1-2 m of the host's walk. The Tribe
  mirrors take 0.4 s since BUG-0365; npc.cpp's walk_tribe kept the default.
* Observed: the hotspots of the recorded lines (68 lines in the 2026-11-03/04 logs) are mostly within 10 m of a nest
  (49 of 68), where the animals live; the object lists at those nests are the same on both games (no obstacle on
  one game only), and walks through a nest with the guest's camera on it (scratch runs) stayed under 1.5 m.
* So the gate was right: the guest's game really drew the animal metres away from the host's. Not a gate to change.

## Fix
`npc.proxy_slow_frames` (npc.cpp): in Tribe (rts stages) a walked proxy's walk_place takes max_dt 0.4 s, as the
mirrors. `coop.fix npc.proxy_slow_frames off`: 0.1 s as before. At 60 fps nothing changes (frames are shorter
than both caps).

## Verification
* Test `npc_walk_slow` (coop_npc_walk_slow.py, tribe fixture, the guest at 4 fps with `frame.limit 250`, the host's
  game walks 4 animals at 4-5 m/s for 40 s; checks on the guest's per-frame rows).
* FAIL (`SPORE_FIX_OFF=npc.proxy_slow_frames`): out/lab/20261104-232034: placed p90 1.42-2.41 m (limit 1.0), drawn
  error up to 5.1 m, 1.5 s over 3 m.
* PASS: out/lab/20261104-232210 #1, #2: placed p90 0.00 m on all 8 walkers, drawn error max 1.8-3.2 m, never 3 m
  for 2 s.
* Not verified: a long session (long_tribe) on this branch; play_tribe regression: see the branch report.

## History
- 2026-11-03: opened.
- 2026-11-03: analysed from the logs on fix/w41-tribe-world (time box), not fixed.
- 2026-11-04 (fix/w43-tests): cause found (the placing's 0.1 s frame cap on slow guests), fixed behind npc.proxy_slow_frames, test npc_walk_slow.
- 2026-11-05: released in 0.7.7.
