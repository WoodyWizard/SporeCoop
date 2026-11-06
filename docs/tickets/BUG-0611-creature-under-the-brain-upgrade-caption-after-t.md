---
id: BUG-0611
title: Creature: under the brain upgrade caption after the live Cell -> Creature start the guest lost 12 of 13 host NPCs within 2 s (not reproduced on the fixture)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-06
source: split from BUG-0413 (fix/w45-one-game-objects)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w45-leftovers
commits:
tests: npc_frozen_thaw (coop_npc_frozen.py --case thaw), npc_frozen_live (--case live, extended)
signature:
docs: docs/re/creature.md#creature-freeze-edges, docs/re/creature.md#creature-npc-gaps
related: BUG-0413, BUG-0427
---

## Report
BUG-0413's original sighting (transition_net, lab 20261101-145528 of feat/w33-tests-session, SPORE_NET=player, the
guest at 30 fps): 2 s after the host's `AddEvolutionPoints 3000` the guest's `npcs mirrored` went from 13 within 60 m
(25 in all) to 1 (1 in all) and 0 later, while the host's trace kept "13 within 60 m, 29 in all" (its g_seen_now of
the last updated frame: the host was frozen under the caption). No despawn or stale line in either log.
On the creature fixture the same caption (`coop.dna add 3000`) did not lose any NPC (coop_npc_frozen.py --case caption,
lab 20261106-013649, -014107: 5 of 5 kept for 14 s).
Inferred, not checked: the host's npc::on_world_change ran (it despawns every NPC at the guests and forgets them,
called by avatar_change_check when the avatar object changes) and the frozen host sends no spawns until it runs
again; npc.frozen_send (BUG-0413) only re-sends NPCs it still tracks. Diagnostic in: the host logs `npc: world change
(avatar ...): N NPC proxies despawned at the guests, M NPCs forgotten`. Next: transition_net --to creature and read
that line at the caption.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (fix/w45-leftovers, 2026-11-06): the guest's stale rule (`npc.stale_proxy`, BUG-0009: a proxy without
a state for 5 s while other states arrive is dropped) counted the host's frozen time. After any freeze of the host's
game (a caption) every proxy's last state is that old, and the first state batch after it that lists only some NPCs
drops all the others. coop_npc_frozen.py --case thaw (10 s caption, its OK, then `coop.npc rxkeep 1 3` on the guest:
each batch keeps its first NPC, as when the other packet of a batch split in two is lost): 5 of 5 host NPCs near the
guest gone 1 s after the OK, 20 stale drops (out/lab/20261106-110717). The host still counts them as known and sends
no spawn; while its game stands still again (the next caption) nothing brings them back.
Inferred for the sighting (re-read of feat/w33-tests-session lab 20261101-145528): the host's game stood still from
~14:59:00 (counts unchanged, input_lock=1) to the caption's OK at 14:59:34, the brain upgrade caption froze it again
at 14:59:37, the guest went 25 -> 1 proxies by 14:59:39 with no despawn line (stale drops log none) on a link with
12-16 % loss spikes; 25 NPCs take two packets (23 a packet). Not seen: the host's npc::on_world_change at the caption
(the diagnostic line did not fire in the live Cell -> Creature run, out/lab/20261106-105614, 8 of 8 kept), so L1's
guess is not the cause there. Not reproduced without the test command (the fixture has 5 NPCs near the players).

## Fix
`npc.thaw_stale` (npc.cpp, the guest's stale check): the 5 s count starts when the host's states flow again after
more than 1 s without a whole batch (`g_states_flow_since`), not at the proxy's last state.
`coop.fix npc.thaw_stale off`: counted from the proxy's last state (0.7.8). Test command `coop.npc rxkeep <n> <s>`
(guest); `coop.npc` (guest) prints thaw_kept= rxkeep_cut=.

## Verification
npc_frozen_thaw: FAIL with SPORE_FIX_OFF=npc.thaw_stale out/lab/20261106-110717 (5 missing 1 s after the OK,
20 stale drops), PASS out/lab/20261106-111013 (0 missing in 10 reads, 0 stale drops); census of both games.
npc_frozen_live (the live Cell -> Creature start, then the caption): PASS out/lab/20261106-105614 (no loss there;
the scenario test).

## History
- 2026-11-06: opened.
- 2026-11-06: fix/w45-leftovers: the stale rule after a freeze found, fix npc.thaw_stale, tests npc_frozen_thaw / npc_frozen_live; fixed.
