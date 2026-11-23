---
id: BUG-1371
title: Civ: a guest joining from its own Civ world takes the host's money total when WELCOME and the first total arrive in one frame (civ_other_world)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-20
source: lab
role: guest
versions: rel/0.7.16
fixed_in: unreleased
branch: fix/w61-civ
commits: d8f60860
tests: civ_other_world, civ_other_world_slowjoin, civ_other_world_slowjoin_off (twin)
signature: FAIL the guest's own world keeps its own money, never the host's total (closest 0.0)
docs:
related: BUG-0231, BUG-0709
switch: res.elsewhere_join
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, rel/0.7.16 673f7a55, lane 3, cold): civ_other_world
`FAIL the guest's own world keeps its own money, never the host's total (closest 0.0)`; every sample of the 40 s was
(3000, 3000). It passed in the first final check (20261120-041304) and in 4 of 4 rel/main runs before; 3 of 17 on all
branches failed since 2026-10-28.

## Root cause
Observed (the guest's game log of 20261120-133703): `coop.res add money 777` gave the guest's own world 3777 at
20:57:55; the guest joined at 20:57:56.343 and its first trace line, 1 ms after `joined as player 1`, already read
`money=3000(unsynced)`: the host's total was taken in the very frame of the join, and the BUG-0231 hold never came into
play (no tick before it).
In the code (shared.cpp): the hold (`g_hold`) is computed by the tick, before the session's update; on_message's
`take_total(g_hold)` used that value. In the frame of the join the tick ran while the guest was still unconnected
(hold false: NotGated), then the session's update brought WELCOME (connected, welcome world = the host's Arigine,
this game in Otherworld: host_world_now() = Other) and the host's first MSG_RES (sent in the host's tick right after
on_player_join) together: the total went into the guest's own world. Whether both come in one frame is timing (a
loaded lab, a slow guest frame), hence 3 of 17. Inferred for Space too: the same path with guest_space_other().

## Fix
`coop.fix res.elsewhere_join` (shared.cpp on_message): a host total that arrives while the last tick did not hold asks
where this guest plays now (coop_core::guest_elsewhere() || guest_space_other(), the expression the tick gets) and
holds it when the guest plays another world (log `held (BUG-1371)`, counter `coop.res` join_held=). Off: the last
tick's hold (0.7.15).
Test: coop_civ_other_world.py --slow-join runs the guest at 2 fps (frame.limit 500) from just before the join until 6 s
after it, so WELCOME and the first total share a frame almost always; it prints held / join_held.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL (before, by itself): 20261120-133703:civ_other_world (rel/0.7.16 673f7a55).
- Was waiting for the lab run: civ_other_world and civ_other_world_slowjoin must PASS (slowjoin prints join_held >= 1
  when the case came), civ_other_world_slowjoin_off must FAIL (money 3000 = the host's), the old twin
  `SPORE_FIX_OFF=res.elsewhere coop_civ_other_world.py` must still FAIL.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): civ_other_world and civ_other_world_slowjoin PASS in 20261121-003202, 20261122-194210, 20261123-023643; the twin civ_other_world_slowjoin_off FAIL in 20261121-000724 (the guest's world took the host's money). Closed.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-civ): cause from the logs, fix and twin; no lab run (owner rule).
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
