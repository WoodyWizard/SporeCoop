---
id: BUG-0668
title: Creature: each game evolves the NPC nests by itself: other thresholds, and a nest extinct on the host evolves to a new species on the guest
status: open
kind: desync
stage: Creature
reported: 2026-11-07
source: found in the lab, fix/w46-creature-p0 (BUG-0639 probe)
role: both
versions: 0.7.9
fixed_in:
branch:
commits:
tests: nest_evolve_probe (coop_nest_evolve_probe.py)
signature:
docs: docs/re/creature.md#creature-herd-evolution-desync
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
