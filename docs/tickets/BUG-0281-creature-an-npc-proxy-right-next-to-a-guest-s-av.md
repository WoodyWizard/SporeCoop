---
id: BUG-0281
title: creature: an NPC proxy right next to a guest's avatar stays ~4 m from where the host has it
status: open
kind: desync
stage: creature
reported: 2026-10-29
source: fix/w7-sync BUG-0275 measurement
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Found while measuring BUG-0275 (4 players, creature fixture, play_creature bots on every machine,
lane 6, 2026-10-29). In a few checks one NPC proxy sat 3.8-4.3 m from the host's NPC on every guest
at once while the host's NPC stood still (speed 0); it was the NPC 3-5 m from the guests' own
avatars (host NPC #1, species 065CBE70, 7 m from the host's avatar). The guest's `coop.npc list`
showed it: `err=4.33` / `4.32` / `3.76` (the proxy's distance from its own interpolation target
`want`, age 0.02-0.11 s), so the proxy does not reach the pose it is driven to. Likely the guest's
own avatar (or the puppets next to it) pushes the proxy away, the host's NPC is not pushed the same
way (inferred, not verified). Players see a standing NPC some metres off next to them.
Reproduce: SPORE_LANE=N, 4 players in the creature fixture (coop_multi.start_extra), play_creature
bots, `coop.npc list` on a guest: lines with `err=` above 2 m while the host's NPC stands.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
