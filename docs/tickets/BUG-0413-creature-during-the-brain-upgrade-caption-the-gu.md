---
id: BUG-0413
title: creature: during the brain-upgrade caption the guest loses the host's NPCs (sync-check n missing) and has none 10 s after it
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: transition_net, lab 20261101-145528 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in: unreleased
branch: fix/w45-one-game-objects
commits: 3f8ec9a3
tests: npc_frozen_death, npc_frozen_caption (coop_npc_frozen.py)
signature:
docs: docs/re/creature.md#creature-npc-gaps, docs/06-testing.md#testing-npc-gaps
related: BUG-0611, BUG-0421
---

## Report
`transition_net` (Cell -> Creature -> Tribe the players' way, SPORE_NET=player, the guest at 30 fps; lab
20261101-145528, lane 9): in Creature, both players at the same spot (`d=0`), the guest mirrored 13 of the
host's NPCs within 60 m for two minutes. The host's `AddEvolutionPoints 3000` (the campaign's shortcut to the
end of the stage) brought the brain-upgrade cinematic and its caption (`input_lock=1` on both games); 10 s
later the guest mirrored 1, then 0 (`npcs mirrored: 0 within 60 m ... 0 in all`) while the host still simulated
13 within 60 m. Both games then logged `sync-check: MISMATCH ... (Creature 9652F8AD, 2 objects compared): n: 15
missing on the guest (n2 n3 n4 n6 n7 n10 +9)` (15:00:07); the caption closed (input_lock=0, 15:00:09) and the
guest still had 0, until the evolution vote 2 s later took both into their editors. The audit's "Creature n
missing" (9 recorded results, campaigns) may be the same.

How to reproduce: `SPORE_LANE=N SPORE_NET=player SPORE_FPS=guest:30 python3 tools/tests/coop_transition_net.py
--to creature` and read the guest's `npcs mirrored` lines after the host's `AddEvolutionPoints`; or two
games on the creature fixture, the host `call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:3000`,
watch `coop.npc` on the guest while the caption is up.

## Root cause
Observed (lab 20261106-013312, -014107, -014526; creature fixture): the host's death caption freezes its
simulation ~8 s after the hit (creatures_updated() empty, busy=cinematic,input_lock, later paused) until OK, and
npc.cpp host_tick returned at once in such frames: no states (by design) and also no relevance pass, so a guest that
moved meanwhile got no spawn of the host's NPCs at its new place. In the test a guest 165 m off with 4 of 5 host NPCs
at the host unseen came back next to the host's body: 0 of them appeared in 8 s (all 4 "not on the guest"). This is
the 0.7.8 final check's case (the guest respawned at its nest 130 m away while the host lay dead 45 s).
The brain upgrade caption (the report's case): observed on the creature fixture (`coop.dna add 3000`) the host's
simulation freezes the same way and the guest keeps 5 of 5 host NPCs near it for 14 s (no loss). Inferred, not
reproduced: in the transition_net run (after the live Cell -> Creature start) the host's npc::on_world_change ran
under the caption (an avatar object change despawns every NPC at the guests and forgets them), and the frozen host
never spawned them again. npc.frozen_send does not cover that path (on_world_change forgets the tracked NPCs, and
frozen_send only re-sends tracked ones); the host logs `npc: world change (avatar ...): N NPC proxies despawned` now,
so a new sighting shows whether that was the path. That part is split into BUG-0611 (not reproduced).

## Fix
npc.cpp frozen_send (`coop.fix npc.frozen_send`, off = 0.7.8): every 0.5 s of a freeze the host runs the guests'
relevance over its tracked NPCs that are still creatures of its world (listed, alive, in a herd, not at the planet's
centre): newly near ones get a spawn (it carries the unchanged state), far ones a despawn; no states. `coop.npc` (host)
prints `frozen_s`, `frozen_spawns`, `frozen_despawns`.

## Verification
npc_frozen_death (coop_npc_frozen.py --case death): FAIL with SPORE_FIX_OFF=npc.frozen_send out/lab/20261106-014526
("n1 n2 n4 n5 not on the guest" 8 s after it came back), PASS out/lab/20261106-014107 (0 missing at 2-8 s, host frozen
4-10 s). npc_frozen_caption (regression, passes either way on the fixture): out/lab/20261106-014107.

## History
- 2026-11-01: opened.

Update (2026-11-05, rel/0.7.8 final check out/lab/20261105-155603, play_creature and long_creature): the same class
in another form. The host died and stood in its death cinematic for 45 s (busy=cinematic,input_lock,paused; such
45 s deaths are common in older runs too); the guest respawned at its nest 130 m away meanwhile. npc.cpp host_tick
sends nothing while no creature is updated (the frozen simulation, by design: the guests keep the old states), so the
host's NPCs near the guest's new place (its family n4 n5 n34; n55 n60) were never made on the guest: sync-check
"n: 3 missing on the guest" while the host was still frozen. Not a 0.7.8 regression (that code is old); it shows
only when the guest moves far while the host is frozen. For 0.7.9 (L1): while the host's simulation is frozen, still
send the NPCs near a guest that moved (their states do not change, so one snapshot of the newly near ones suffices).
- 2026-11-06 (fix/w45-one-game-objects, L1): the frozen host runs the guests' relevance (npc.frozen_send); death caption case reproduced and fixed, brain caption loss not reproduced on the fixture (diagnostic log added).
