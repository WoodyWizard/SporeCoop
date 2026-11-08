---
id: BUG-0419
title: class: state sent but not applied on the receiver (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b, fix/w46-k2a-state
commits: f1d45292 1391b0ac 149ac2b6 d5873f5a
tests: space_partner_hp, species_table, creature_stun_proxy, npc_max_hp, tribe_member_age; state_fields_check (make test)
signature:
docs: docs/re/space.md#space-partner-ship-health, docs/re/creature.md#creature-table-cadence-stun, docs/re/creature.md#creature-proxy-max-health
related: BUG-0697, BUG-0698, BUG-0699, BUG-0700, BUG-0710
---

## Report
Fixed per case: BUG-0373 (Creature partner HP), BUG-0334.
Siblings not covered (obs): Space partner ship health pinned to max (space_drive modes.cpp:323-325); Cell partner cell health pinned to 6 (cell_drive); NpcSpawn.scale never read (npc.cpp:312 vs 1428-1450); NpcSpawn.max_health stored unused (npc.cpp:1441); tribe member age applied only at spawn (tribe.cpp:554/911); PF_JUMPING / PF_SWIMMING / PlayerState.anim dead fields, PF_RUNNING never read; guest applies only pos, hp>0, dead, impressed for proxies (befriended, stun, size missing); BUG-0173 dead flag +0xB5E, action, held tool not sent; status effects (poison, sprint, sneak, glide) and tribe tool held / carried food / task not sent (inf).
General fix: one table of every state field per object kind with its apply site per stage. Outcome check: sync-check digest carries state fields (hp, max, dead, scale, age); unit test fails on an unlisted field.
Source: docs/generalisation-plan.md G2.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2a audit (0.7.11) of the sibling list: the partner ship's health, NpcSpawn.max_health, dead PlayerState fields, the species table cadence, the NPC stun on proxies were sent and not applied (or not sent).

## Fix
Shared outcome check: tools/tests/state_fields_check.py (make test) lists every snapshot field and flag with its apply site or the reason it is not applied and fails on an unlisted one; the detector compares each creature's max health (max_hp). Children: BUG-0697 fixed (partner ship health; the Cell partner's left on purpose, reason in the ticket), BUG-0698 fixed (the field table; its max_health note corrected by BUG-0710), BUG-0699 fixed (species table every 0.25 s), BUG-0700 fixed (NPC stun on proxies; member age not a bug), BUG-0710 fixed (an NPC proxy's maximum health is the host's and follows it).

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2a final runs on the merged branch: detect_* of all five stages out/lab/20261107-231824, play_space / play_civ / play_cell / play_tribe / play_creature out/lab/20261107-232019, detect_creature + play_creature out/lab/20261107-235443, detect_creature out/lab/20261108-001833: Creature:state:species_relation / species_only / species_killed 0 rows (they were 12 / 2 / 10 in 400 runs of 2026-11-06/07 before), Creature:max_hp 0 rows in out/lab/20261108-001833 (20 of 23 comparisons before BUG-0710). Limit: BUG-0699's rare long rows in 4-player campaigns at low fps were not re-run here; the guest logs the table's age now.

## History
- 2026-11-01: opened.
- 2026-11-03: fix/w40-scale: the replicated-field engine (mods/coop/fields.h, docs/sync-architecture-plan.md#sync-fields) with the first kinds: NPC proxies and tribe members (scale), BUG-0461 / BUG-0475; NpcSpawn.scale is applied now.
- 2026-11-07 (fix/w46-k2a-state-b, K2a of 0.7.11): the uncovered siblings became BUG-0697, BUG-0698, BUG-0699, BUG-0700 (Space partner ship health, the state field table, the species table cadence, the NPC stun on proxies; member age not a bug); all fixed (unreleased) with FAIL / PASS runs. Umbrella left open for the orchestrator.
- 2026-11-08 (fix/w46-k2a-state, K2a of 0.7.11): merged; children BUG-0697..0700 and BUG-0710 fixed; no species or max_hp row on the final runs; umbrella closed.
