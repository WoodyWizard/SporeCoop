---
id: BUG-0668
title: Creature: each game evolves the NPC nests by itself: other thresholds, and a nest extinct on the host evolves to a new species on the guest
status: fixed
kind: desync
stage: Creature
reported: 2026-11-07
source: found in the lab, fix/w46-creature-p0 (BUG-0639 probe)
role: both
versions: 0.7.9
fixed_in: unreleased
branch: fix/w46-herd-evo
commits: 0ebcfb43, 64fe3af3, 4a07c2fc, ffce936a
tests: nest_evolve (coop_nest_evolve.py), nest_evolve_probe (coop_nest_evolve_probe.py)
signature:
docs: docs/re/creature.md#creature-herd-evolution-desync, docs/re/creature.md#creature-herd-evolution-owner
related: BUG-0639, BUG-0464, BUG-0460, BUG-0640
---

## Report
Found in the lab (nest_evolve_probe, out/lab/20261107-081734, creature fixture, 0.7.10 work tree): the host's avatar
killed the 4 members of the nearest other species' nest (herd extinct, +0x190 = 1), then the pack got +250 DNA.
* The host's game: every other NPC herd went from generation +0xF0 0 to 1 at once (new species pointer, its own
  thresholds +0x94: 289, 307, 440, ...); the extinct herd stayed generation 0 with +0x85 = 0.
* The guest's game, the same herds: generation 0 -> 1 within 18 s, with other thresholds (183, 262, 239, ...) and, for
  the herd extinct on the host, generation 1 and a new species (+0x85 = 1, threshold 403).
So each game evolves its own herds (the guest's are not simulated otherwise): the next evolution comes at other DNA
totals in the two games, and a nest the host's game keeps extinct evolves in the guest's (its minimap and nest show a
species the host's world does not have there). The creatures themselves are the host's NPCs (mirrors keep the host's
species), so what the guest sees at such a nest and what its nest rules use can differ.
Reproduce: `python3 tools/lab.py run nest_evolve_probe --lane N`; compare `coop.nest evolve` rows of the two games.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Inferred: the herd evolution (cHerd generation +0xF0 from mEvolvedSpeciesProfileKeys +0xA8 when the DNA crosses
+0x94) runs in each game; no row carries the host's generation, species or thresholds to the guest (wobj.herd, BUG-0460,
carries the extinct state only). Fix idea: the host's herd generation / species key / threshold as a host-owned row,
the guest's own evolution step suppressed. Possibly the cause of BUG-0639 ("for the host the nest is evolving").
Observed (Ghidra, fix/w46-herd-evo): the step is in CreatureGame_Update 0xD45740: while the creature game's +0x48 is
set and the tuning byte CreatureGame_HerdEvolveOn 0x1582DF4 is on, every herd with +0x85, a species other than the
player's and 0 < +0x94 < the DNA total (0xD2E360) gets gen + 1, species cHerd_EvolvedSpecies 0xC6AD90(gen) (the save's
chain +0xA8) and cHerd_ApplyProfile 0xC6E3A0(profile, 1), which draws a new random threshold +0x94. Each game runs it
on its own. Observed (runtime, switch off, out/lab/20261107-095730): the chain is the same on both games (all 21
evolved nests get the same species key on the guest), the thresholds differ, and the herd extinct on the host (+0x85 =
0 there, so not evolved) is evolved on the guest (gen 1, species 065CBBBA instead of 065CB8AB). Not found: what clears
+0x85 on the host's extinct herd (not needed: the row carries it). docs/re/creature.md#creature-herd-evolution-owner.

## Fix
wsync row 14 `herd_evo` (mods/coop/wsync.cpp): the host sends per NPC nest (home +0x34) the generation, species key,
+0x94, +0x85 and +0x190, changed nests first, then a window of 30 that moves once a second (100 nests on the fixture's
planet: one packet holds ~35); the guest merges by home. On the guest CreatureGame_HerdEvolveOn is 0 while it is a guest
in Creature (its own step off; the game's value comes back after the session), and each nest takes the host's
generation and species (the key into +0xA8[gen], +0xF0, +0xA4 = cHerd_EvolvedSpecies(gen)), threshold and +0x85.
Switch `coop.fix wsync.herd_evo off` (SPORE_FIX_OFF=wsync.herd_evo): each game evolves its own nests (0.7.9).

## Verification
coop_nest_evolve.py (suite full, `nest_evolve`): the host's avatar makes the nearest nest extinct, +250 DNA, then
every 5 s for 60 s both games' own herds (`coop.nest evolve`) are compared nest by nest.
* FAIL with SPORE_FIX_OFF=wsync.herd_evo: out/lab/20261107-095730, out/lab/20261107-100058, out/lab/20261107-100333, out/lab/20261107-105600
  (the extinct nest gen 1 / a new species on the guest; 21 thresholds differ in the last).
* PASS: out/lab/20261107-100043, out/lab/20261107-100318, out/lab/20261107-100728 (134 nests compared, 21 evolved on the host, 0 differ in
  every sample, the extinct nest stays gen 0 on the guest). (20261107-095709 failed with a first version that sent
  only the 30 nearest nests: 10 far nests stayed gen 0 on the guest.)
* Not verified: a guest's minimap icon of the nest seen on screen; a second evolution (gen 2) of the same nests.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-herd-evo): fixed: wsync.herd_evo; FAIL 20261107-095730 / 100058 / 100333, PASS 20261107-100043 / 100318.
- 2026-11-07 (fix/w46-herd-evo): regressions PASS: out/lab/20261107-100728 (play_creature, family_attack, evolve_world, wsync_transitions, nest_evolve), 20261107-100748 (detect_creature, creature, migration, nest_follow, nest_heal), smoke 20261107-103109 13/14 (creature: guest stuck in the Galaxy menu at the join, rerun PASS 20261107-105356); make test PASS.
