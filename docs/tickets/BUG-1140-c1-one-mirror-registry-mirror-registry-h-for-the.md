---
id: BUG-1140
title: C1: one mirror registry (mirror_registry.h) for the id <-> object tables of NPC, Tribe and Civ
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-ccore, feat/w51-tribe
commits: 423016ca, f0181eb5, ba65cc5c, 6b960de3
tests: tests/core_tests.cpp
signature:
docs: docs/01-architecture.md#arch-mirror-registry
related: BUG-0426, BUG-0842, BUG-0844, BUG-0951, BUG-1053, BUG-1143
---

## Report
Plan item C1 of docs/plan-0.7.15.md (audit STG-1, CORE-4): every stage keeps two hand-made id <-> pointer maps per side (npc g_host_npcs / g_host_by_id and g_proxies / g_proxy_of, tribe g_hm / g_hm_by_id and g_gm / g_gid_of, civ g_hv / g_hv_by_id and g_gv / g_gv_of) with its own liveness rule (species + dead, serial, political id + serial) and its own delete-unseen and save filter; the two directions drift (BUG-0951's `g_gv_of[v] == id` checks) and 149 `object_alive(` calls remain.

## Root cause
Structure: the same machinery written per stage (umbrella BUG-0426).

## Fix
Portable core (feat/w51-ccore): mods/coop/mirror_registry.h, `mirror::Registry<Id, Data>`: bind / detach / erase, the reverse index kept by the registry (a pointer bound to another id is refused), injected liveness (game::alive, or Civ's serial rule), `adopt` for the host tables (another serial at a known address gets a new id), `sweep(seen, decide, remove)` with Remove / Keep / Defer, the deferred removal path (`defer`, `pump(hold, destroy)`, Civ's g_kill_later), the save filter `ours(p, noun, all_bound)`, stats / status. Per-stage mapping, migration steps and switches (`npc.registry`, `tribe.registry`, `civ.registry`): docs/01-architecture.md#arch-mirror-registry. No stage uses it yet: the ticket stays open until NPC, Tribe and Civ adopted it and their old maps are deleted.

## Verification
Unit tests tests/core_tests.cpp (registry_bind, registry_liveness, registry_adopt, registry_sweep, registry_save_filter; ASan/UBSan in unit-cpp, TSan in test-tsan); each one fails on a mutation of its rule (bind refusal, Keep). The lab proof comes with each stage adoption (tests named in the architecture section).
no-fail-run: a refactoring with no behaviour change (plan rule "a shared mechanism replaces a stage path behind a
switch"): the proof is each stage's lab tests PASS with the switch on and refactor_dump identical apart from the switch.
NPC: refactor_dump 20261115-154746 vs 20261115-165006, stage tests 20261115-165219 (71/74, the three failures also with
the switch off: 20261115-192704 / 20261115-192709) (feat/w51-npc). Tribe: 20261115-162402 (22/22, switch on) and
20261115-175152 (switch off) (feat/w51-tribe). Civ: 20261115-161514 (switch on) (feat/w51-civ).

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
- 2026-11-15 (feat/w51-tribe): Tribe adopted it (`tribe.hm`, `tribe.gm`; the old maps g_hm_by_id / g_gid_of and the
  switch tribe.registry deleted after the lab: on 20261115-162402 22/22, off 20261115-175152 5/5); tribe object_alive
  39 -> 35. Commits f0181eb5, ba65cc5c, 6b960de3. NPC and Civ are their branches'.
- 2026-11-15 (feat/w51-npc): NPC done (the stage part of C1). npc.cpp's host table (g_host_npcs / g_host_by_id) and guest
  proxies (g_proxies / g_proxy_of) are `Registry<uint16_t, HostNpc>("npc.host")` and `Registry<uint16_t, Proxy>("npc.proxies")`
  (docs/01-architecture.md#arch-npc-registry): host adopt with alloc_npc_id (the species / dead rule BUG-0044 first),
  retire_pooled and the frozen pass by the entry's LiveRef, the guest's bind / detach, the stale drop as the registry's
  sweep (Keep in the thaw). Behind `npc.registry` (aa4b0a16): refactor_dump base 20261115-154746 (rel/0.7.15 9362bcdd)
  vs 20261115-165006: identical apart from the switch's line (fix count 581 -> 582); lane 2 + 3 run of the stage's tests
  with it on, 20261115-165219: 71 of 74 PASS (npc_*, puppet_*, nest_*, creature, play_creature, civ_wild_*,
  creature_kills, creature_death_coop, creature_social_prompts, creature_eat); the three FAILs fail the same with
  `SPORE_FIX_OFF=npc.registry` (20261115-192704 lane 3 off, 20261115-192709 lane 2 on): nest_follow (BUG-1178, the
  lab's start), nest_evolve (BUG-1181, also on v0.7.14.1 20261115-193814), nest_eggs_tribe (PASS on both reruns: a
  flake). Then the old maps and the switch deleted (c502cc36); object_alive in npc.cpp 22 -> 15 (ratchet 148 -> 141).
  Runs of the final tree (after the merge of rel/0.7.15 96e0fd4c): 20261115-194833 (npc_reuse, npc_pooled, npc_handler_live,
  npc_destroy_once, npc_frozen_thaw, npc_frozen_pooled, nest_follow_own, away_editor ...), play_creature --quick (see the
  play_creature --quick 2 of 2, 20261115-202215; after the merge of rel/0.7.15 b7f9ae9a: play_creature --quick 2 of 2
  20261115-212337-2, npc_editor_hold / npc_herd_identity / npc_resync / civ_wild_reused / nest_follow 20261115-212337). The ticket stays open until Tribe and Civ adopted it.
- 2026-11-15 (feat/w51-civ): Civ adopted it (`civ.hv`, `civ.gv`; g_hv / g_hv_by_id / g_gv / g_gv_of / g_kill_later and the switch civ.registry deleted after the lab; the host's pass is the registry's sweep with the pol and serial checks, the guest's unseen mirrors Defer while the planner or a cinematic holds them, pump in guest_tick, the save filter ours(all_bound)); civ.cpp's object_alive calls 13 -> 11 (ratchet lowered). Commits 99d04d57, 537387a8. Lab: lab 20261115-161514 (84d96518, the switches on: play_civ, play_space, civ_owners_session, civ_city_ground, civ_capture_credit, civ_guest_attack, civ_aoe, civ_city_layout OK), 20261115-164954 (92ac086f, the old paths deleted: 10 OK), 20261115-174639 and 20261115-190127 (d1a4d5c5, the split build: 61 of the 62 others OK), 20261115-193058 (civ_save_strip OK), 20261115-193323 (play_civ, play_space --quick 2 of 2 each, civ_role_damage 2 of 2). Not by this branch: civ_role_damage's phase A (BUG-1172, fixed in the test), civ_owners_reload's sync-check gate (BUG-1175, FAILs on rel/0.7.15 9362bcdd too: out/lab/20261115-192303-base9362). docs/01-architecture.md#arch-civ-registry. Civ done; NPC and Tribe are their branches'. After the merge of rel/0.7.15 96e0fd4c (5c3df1be): play_civ and play_space --quick 2 of 2 each (lab 20261115-200149); civ_away, civ_away_buy, civ_pause_city, civ_coop, civ_mirror_lifetime, civ_mirror_remove, space_fleet, civ_owners_session 8 of 8 (lab 20261115-201352).
- 2026-11-15: NPC (feat/w51-npc), Tribe (feat/w51-tribe) and Civ (feat/w51-civ) on mirror::Registry, all merged into rel/0.7.15; fixed.
- 2026-11-18: released in 0.7.15.
