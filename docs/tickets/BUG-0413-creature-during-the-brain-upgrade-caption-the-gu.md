---
id: BUG-0413
title: creature: during the brain-upgrade caption the guest loses the host's NPCs (sync-check n missing) and has none 10 s after it
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: transition_net, lab 20261101-145528 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
Not analysed. Observed: the host's NPC count stays at 13 (`npcs simulated` unchanged for 30 s: its game holds
the NPCs still under the caption). Inferred candidates: the host sends no NPC states while a cinematic plays
(npc.cinedefer) and the guest's npc.stale_proxy drops proxies that got no state for a while; nothing spawns
them again until the host's states resume.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

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
