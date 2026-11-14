---
id: BUG-1089
title: Civ: a guest keeps proxies of 14 host NPCs that the host's game has at the planet's centre (0 0 0), 525 m apart (sync-check MISMATCH)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-13
source: lab
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-proxy
commits: 1856ff2e, cea47222
tests: civ_wild_pooled, campaign (lab gate sync-check)
signature:
docs: docs/re/civ.md#civ-wild-parked, docs/06-testing.md#testing-civ-wild-pooled
related: BUG-0339, BUG-0274, BUG-0973
---

## Report
fix/w49-camp, the full campaign lab 20261113-160426 (lane 4): the campaign itself went Cell -> Space and the finale
the players' way with no jump (RESULT: OK); the lab failed it on the sync-check gate in Civ, 26 s after Carol's rejoin
(the host's share save at 17:00:00): "sync-check: MISMATCH with player 1 'Bob' (Civilization 9652F8AD, 2 checks 3 s
apart, 60 objects compared): n: 14 apart (n1274 526.6 n1275 524.8 ...)". The host's game has those NPCs (species
067A147A, hp 8-16, `upd=64/64 moved64=0.0`) at `pos=(0.0 0.0 0.0)`, the planet's centre; Bob's proxies of them were
made 7.8-8.1 s before (`spawned ... age=7.86`) at their last wanted place on the ground. BUG-0339's picture (fixed for
Creature) in Civ.
Reproduce: the full campaign (`lab.py run =campaign`), look for "n: .. apart" with "pos=(0.0 0.0 0.0)" on the host.

## Root cause
Not analysed (outside fix/w49-camp's package). Inferred: Civ NPCs of a herd the host's game keeps (BUG-0973's host-owned
wild creatures) parked at the centre while they are not in play, still listed for the guests.
Observed (fix/w49-proxy): the 14 are wild creatures the host's game took back into its pool (the planet's centre, herd
0) while its simulation stood. Logs of 20261113-160426: the host's trace has `busy=paused` at 17:00:21, the
MISMATCH is at 17:00:23 (2 checks 3 s apart); the BUG-0339 diagnostic "npc N is updated at the planet's centre" (once
per stay, in host_tick) never fired for ids 1269-1282, so host_tick never saw them updated there: they went to the
centre in frames without creature updates. Mod side: in Tribe and Civ host_tick returned at once in such frames
(npc.frozen_send / npc.frozen_pooled are the Creature stage's per-guest relevance pass and were skipped for the RTS
stages), so no despawn went out until the game went on; Civ has no distance rule that would drop them (kCivRelevance).
The sync-check reads the last running frame's table with the current positions: (0 0 0) on the host.
Reproduced (civ_wild_pooled, npc.frozen_pooled_rts off, lab 20261113-174015): host in its pause menu, camera panned to
the other side of the planet: the game parked a herd of 14 (species 066742CD) by itself and the sync-check gate
reported the campaign's signature "n: 14 apart (n139 536.6 ...)"; the guest also kept the 3 creatures the test pooled
(`coop.npc pool`). So the Civ relevance pass parks creatures during the pause menu.
Not these: dead or hidden NPCs, vehicles, BUG-0973's `npc.civ_wild` itself (it only made the class visible in Civ,
where every guest gets every wild creature). The guest should not show them: they are out of the host's world.

## Fix
`npc.frozen_pooled_rts` (npc.cpp): in Tribe and Civ, every 0.5 s of a freeze (no creature updated) the host retires
the tracked NPCs that are no creature of the world any more or are within 50 m of the planet's centre (despawn at
every guest, the id forgotten): the pooled part of frozen_send (now `frozen_pooled_pass`, shared with the Creature
stage), without the per-guest relevance sends. `coop.fix npc.frozen_pooled_rts off`: kept until the host's game goes
on (0.7.13).

## Verification
civ_wild_pooled (tools/tests/coop_civ_wild_pooled.py, civ_npc):
FAIL with SPORE_LAB_FIXOFF=civ_wild_pooled=npc.frozen_pooled_rts: out/lab/20261113-174015 (lane 4: the guest kept
[1, 2, 3] for the whole check, and the sync-check gate "n: 14 apart" as in the campaign).
PASS: out/lab/20261113-174017 (lane 5) and out/lab/20261113-174223 (lane 5): none kept, no MISMATCH.

## History
- 2026-11-13: opened.
- 2026-11-13: fix/w49-proxy: root cause (parked during the host's pause, no freeze pass in Tribe / Civ), fix npc.frozen_pooled_rts, test civ_wild_pooled; fixed.
- 2026-11-14: released in 0.7.14.
