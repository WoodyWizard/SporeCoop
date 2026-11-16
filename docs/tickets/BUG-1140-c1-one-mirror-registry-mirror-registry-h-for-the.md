---
id: BUG-1140
title: C1: one mirror registry (mirror_registry.h) for the id <-> object tables of NPC, Tribe and Civ
status: open
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-ccore
commits: 423016ca
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

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
- 2026-11-15 (feat/w51-civ): Civ adopted it (`civ.hv`, `civ.gv`; g_hv / g_hv_by_id / g_gv / g_gv_of / g_kill_later and the switch civ.registry deleted after the lab; the host's pass is the registry's sweep with the pol and serial checks, the guest's unseen mirrors Defer while the planner or a cinematic holds them, pump in guest_tick, the save filter ours(all_bound)); civ.cpp's object_alive calls 13 -> 11 (ratchet lowered). Commits 99d04d57, 537387a8. Lab: lab 20261115-161514 (84d96518, the switches on: play_civ, play_space, civ_owners_session, civ_city_ground, civ_capture_credit, civ_guest_attack, civ_aoe, civ_city_layout OK), 20261115-164954 (92ac086f, the old paths deleted: 10 OK), 20261115-174639 and 20261115-190127 (d1a4d5c5, the split build: 61 of the 62 others OK), 20261115-193058 (civ_save_strip OK), 20261115-193323 (play_civ, play_space --quick 2 of 2 each, civ_role_damage 2 of 2). Not by this branch: civ_role_damage's phase A (BUG-1172, fixed in the test), civ_owners_reload's sync-check gate (BUG-1175, FAILs on rel/0.7.15 9362bcdd too: out/lab/20261115-192303-base9362). docs/01-architecture.md#arch-civ-registry. Civ done; NPC and Tribe are their branches'.
