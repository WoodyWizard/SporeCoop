---
id: BUG-0280
title: Creature: the host's game keeps NPCs only around the host's avatar; a guest far from it sees nest members appear and vanish every few seconds
status: open
kind: desync
stage: creature
reported: 2026-10-29
source: fix/w7-npc BUG-0274 analysis
role: 
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0274
---

## Report
Seen while analysing BUG-0274 (lane 4, creature fixture, 2 players, 2026-10-29): the guest stays at
the nest, the host walks 300 m away (`coop.npc gopos` in 20 steps). On the host every NPC farther
than about 150-200 m from its avatar goes back to the creature pool (`coop.npc creatures`: herd
00000000, pos (0 0 0), no updates), also the ones right next to the guest. The guest then has no
NPCs around it at all (17 proxies -> 0), and the nest keeps spawning one missing family member
that the game pools again 1-3 s later: on the guest a family member appears at the nest and vanishes
every 3-6 s, each time with a new net id (c128, c129, c131 ... c134 in 40 s).

Reproduce: `SPORE_LANE=N python3 tools/tests/coop_npc_pooled.py` prints it (matched proxies drop to
none while the host stands far away); or by hand as above.

## Root cause
Observed: the host's game simulates creatures only around the host's own avatar (the guests'
puppets do not keep them); the cull distance and the function were not looked up. Inferred: the
same in the 4-player campaign (BUG-0274: the host 190-300 m away from the nest, the guests near it).
A fix would need the game to keep creatures around the partners' puppets too (or the guests to run
the NPCs of their own area), a design change.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
