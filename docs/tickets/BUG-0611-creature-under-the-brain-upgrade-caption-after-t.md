---
id: BUG-0611
title: Creature: under the brain upgrade caption after the live Cell -> Creature start the guest lost 12 of 13 host NPCs within 2 s (not reproduced on the fixture)
status: open
kind: desync
stage: Creature
reported: 2026-11-06
source: split from BUG-0413 (fix/w45-one-game-objects)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-npc-gaps
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
