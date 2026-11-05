---
id: BUG-0590
title: Creature: an animal's proxy lags 3-6 m behind the host's walking animal on a slow guest (4 fps)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-05
source: lab (fix/w43-crashes), coop_npc_walk_slow.py --fixture creature
role: guest
versions: 0.7.6
fixed_in: 0.7.7
branch: fix/w43-crashes
commits: 5c07b2a6
tests: npc_walk_slow_creature (coop_npc_walk_slow.py --fixture creature)
signature:
docs:
related: BUG-0503, BUG-0365
---

## Report
Found by the lab (no player report), checking whether G1's BUG-0503 (Tribe proxies on a slow guest, fix/w43-tests)
holds for Creature: G1's coop_npc_walk_slow.py on the creature fixture (walkers: the 4 host NPCs nearest the
centre that are not the host's family or pack), the guest at 4 fps (`frame.limit 250`): every walker's proxy 3-6 m
off the host's animal for 2.5-9.2 s, placed p90 2.0-3.6 m; the monitor's `proxies` rule fired.
Reproduce: `SPORE_FIX_OFF=npc.proxy_slow_frames tools/lab.py run npc_walk_slow_creature --lane N`.

## Root cause
* Observed (code): the Creature proxies go through the same walk_place as the Tribe ones; its frame cap
  WalkPlace::max_dt 0.1 s limits each frame's catch-up, so a 0.25 s frame closes 40 % of a 4 m/s walk's step.
* Observed (lab): with the cap at 0.4 s the same run keeps every walker within 1.4 m (placed p90 0).
## Fix
G1's switch `npc.proxy_slow_frames` (BUG-0503, cherry-picked here from fix/w43-tests 83e7dafc) now covers every
stage: the 0.4 s cap is no longer Tribe only (npc.cpp guest_tick). Off: 0.1 s on every stage.
## Verification
Lab: npc_walk_slow_creature FAIL with `SPORE_FIX_OFF=npc.proxy_slow_frames` out/lab/20261105-003006 (walkers 2-9.2 s
over 3 m, placed p90 2.0-3.5 m), PASS out/lab/20261105-003137 (drawn error max 1.41 m, 0 s over 3 m, placed p90 0).
## History
- 2026-11-05: opened.
- 2026-11-05: fixed on fix/w43-crashes (the BUG-0503 switch on every stage).
- 2026-11-05: released in 0.7.7.
