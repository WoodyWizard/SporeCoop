---
id: BUG-0487
title: Creature: now and then an NPC near the guest has no proxy, or its proxy shows other hp / alive
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-scale
commits: 59c64045 3fa7d38c d4f3ff94 cfa77d8b f07fcbb0
tests: npc_resync (tools/tests/coop_npc_resync.py)
signature:
docs: docs/re/creature.md#creature-npc-scale, docs/sync-architecture-plan.md#sync-fields
related: BUG-0328, BUG-0419
switch: npc.resync
---

## Report
Detector: `Creature:host_only:creature` 2% of Creature comparisons (up to 9 host NPCs 28 m from the guest without a proxy), `hp:creature` 1% (3 host / 6 guest), `alive:creature` once (dead on the host, alive on the guest), `host_local:creature` once (5 host creatures the mod does not track). Classes G5 (BUG-0328: state for an unknown id dropped, spawn sent once) and G2.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in code: the host sends an NPC's spawn once per guest (npc.cpp g_known) and the guest dropped states of ids it had no proxy of (MSG_NPC_STATE `continue`). Any way a guest loses a proxy while the host still counts it as mirrored (the stale drop, a spawn while the guest was elsewhere) left that NPC missing for good, with its death and health. Reproduced on purpose (`coop.npc forget <id>`): without the fix the 3 forgotten NPCs stay missing (detector Creature:host_only:creature). Which of the ways happened in the night's runs is inferred, not observed.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Class G5 (resync of unknown ids): the guest collects the ids it gets states of and has no proxy of and asks the host (MSG_NPC_RESYNC, every 2 s per id at most); the host sends the spawn again if that guest mirrors the id. Switch `coop.fix npc.resync off`. The hp / alive differences of the report (1 %) are not separately reproduced; they follow from a proxy without states and from the non-simultaneous dumps (inferred).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
FAIL with SPORE_FIX_OFF=npc.resync: out/lab/20261103-124409 (back [], missing [4, 2, 3]). PASS: out/lab/20261103-123854, out/lab/20261103-125153 (all three back within about a second).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-scale (replicated fields, docs/sync-architecture-plan.md#sync-fields).
- 2026-11-03: released in 0.7.4.
