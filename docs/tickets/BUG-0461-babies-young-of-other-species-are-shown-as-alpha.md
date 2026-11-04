---
id: BUG-0461
title: babies (young) of other species are shown as alphas (adult size) on the guest
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in: 0.7.4
branch: fix/w40-scale
commits: 59c64045 3fa7d38c d4f3ff94 cfa77d8b f07fcbb0
tests: mirror_scale_npc (tools/tests/coop_mirror_scale.py --kind npc)
signature:
docs: docs/re/creature.md#creature-npc-scale, docs/sync-architecture-plan.md#sync-fields
related: BUG-0419, BUG-0475
switch: npc.scale
---

## Report
Guest's mark (s 1926): 'i see babies from other species as alphas'. The generalisation audit found NpcSpawn.scale is sent and never read (npc.cpp:312 vs 1428-1450): proxies get the default size. Class G2 (BUG-0419).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 6, creature fixture, 2026-11-03): NPC sizes differ per individual (one herd 0.69 / 0.75 / 0.85 / 0.86, all age 1); the guest's proxies of the same NPCs had 0.65 / 0.73 / 0.81 / 0.86: a proxy made by game::spawn_creature gets its own game's pick. NpcSpawn.scale was sent (npc.cpp send_spawn) and never read on the guest. Inferred: a young NPC (smaller on the host) was made at an adult's size (the player report); no young NPC was in the fixture's area.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
The first piece of the L2 replication (mods/coop/fields.{h,cpp}, docs/sync-architecture-plan.md#sync-fields): NPC proxies are a field kind (`npc`, field `scale`); the host sends every NPC's GetScale (deltas every 0.5 s, all every 4 s, MSG_FIELDS), the guest sets it on its proxy whenever the proxy's own differs. SetScale on a proxy stays (applied 19, reapplied 0). Switch `coop.fix npc.scale off`: the proxies' own sizes (the guest gives back a size it set).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
FAIL with SPORE_FIX_OFF=npc.scale: out/lab/20261103-125642 (16 of 52 pairs differ by more than 12 %, the detector's Creature:scale:creature in 4 of 4 comparisons). PASS: out/lab/20261103-125153 (0 of 48, detector 0 of 4), also out/lab/20261103-123348.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
- 2026-11-03: the divergence detector shows it in 94% of 343 Creature comparisons (`Creature:scale:creature`, e.g. 0.59 host / 0.68 guest), without a player (out/lab/20261103-012624).
- 2026-11-03: fixed on fix/w40-scale (replicated fields, docs/sync-architecture-plan.md#sync-fields).
- 2026-11-03: released in 0.7.4.
