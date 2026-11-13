---
id: BUG-0973
title: Civ: wild creatures are not synced, herds and deaths drift apart between host and guest
status: fixed
kind: desync
stage: civ
reported: 2026-11-12
source: lab
role: 
versions: 0.7.12
fixed_in: unreleased
branch: fix/w49-civ
commits: 409f7c75, edfaef2c
tests: civ_wild_sync, civ_guest_creature, civ_save_strip, play_civ
signature:
docs: docs/re/civ.md#civ-wild-sync
related: BUG-0970, BUG-1067
---

## Report
Found while fixing BUG-0970 (lab 20261112-155526, play_civ): at s 546 the guest's game had a wild creature at (-391 320 -68)
that the host's game no longer had within 10 m. civ.cpp has no sync for wild creatures (cCreatureAnimal) in the Civ stage:
right after a load both games agree within 0-3.8 m (probe on civ_npc, herd of 10, 5 min idle), but during play herds move,
flee or die separately in each game. The guest's vehicles are mirrors and deal no damage, so a creature the host's
vehicles kill stays alive on the guest's screen.

## Root cause
Observed: no wild creature sync in Civ. Inferred: separate AI and deaths per game.
Observed (fix/w49-civ, lane 4, civ_npc): Civ's wild creatures (noun 0x18EB45E, a herd of 7 and three epics; 24-25 in
lab runs once the camera moved) run the same hooked cCreatureAnimal::Update as the Creature stage's; npc.cpp simply
did not run in Civ (npc_stage: Creature and Tribe only). The herd members have no archetype tuning and an own-kind
looking herd, so npc.stray_ids (BUG-0385) would not have tracked them either. The Creature stage's relevance pass runs
in Civ too (the camera's point, 150 / 200 m): a herd moved out of it stops being simulated (docs/re/civ.md#civ-wild-sync).

## Fix
Not fixed yet. BUG-0970 only keeps the guest's order from being lost. A real fix needs host-owned wild creatures in Civ
(positions and deaths sent to the guest), a feature-sized change outside 0.7.13.
Fixed (fix/w49-civ): `npc.civ_wild` runs the Creature/Tribe NPC proxy machinery in Civ as in Tribe (rts_stage: walked
proxies at their own ground height, nest herds, deaths, meat), without a centre: every wild creature the host's game
simulates goes to every guest in the world; the guest's own are removed by the update filter; npc.stray_ids is
skipped in Civ. `npc.civ_quiet`: a standing creature's state goes once a second (25 creatures: 12.5 -> 4.7 KB/s per
guest). `npc.view_centres_civ`: the partners' pointers are spheres of the host's relevance pass in Civ too.
Proxies follow npc.cpp's lifetime rules (game::alive / serial checks, on_world_change, savestrip of proxies); the
host's per-guest quiet table is a LIFECYCLE_STATE (session + world). Switches: `coop.fix npc.civ_wild off` (each
game its own, 0.7.13), `npc.civ_quiet off`, `npc.view_centres_civ off`.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Test civ_wild_sync (tools/tests/coop_civ_wild_sync.py, civ_npc): the host's herd set 40 m aside, every simulated
creature of the host within 4 m of one on the guest's screen and back, counts equal; the guest's attack order (its
vehicles, its creature) kills the same creature in the host's game; a body at that place on the guest.
FAIL with SPORE_FIX_OFF=npc.civ_wild: out/lab/20261113-040340 and 20261113-041612 (worst 497-583 m, live 39/30 and
23/31; no body on the guest where the host's creature died).
PASS: out/lab/20261113-041106 (2 runs) and, with npc.civ_quiet, out/lab/20261113-041929 (2 runs: 12.3 KB/s host ->
guest in all, 24 creatures; 7.6 KB/s with the sync off, 20.1 KB/s without npc.civ_quiet, run 20261113-041607).
civ_guest_creature (BUG-0970's case needs games that differ) runs with npc.civ_wild off: PASS out/lab/20261113-041352.
Saves (CLAUDE.md 2, the orchestrator's check): civ_save_strip now also checks the wild creatures. Carol (a guest)
shows 10-22 proxies of the host's creatures (some in the game's nest herds, 9 herds made for them), saves, leaves and
loads the save alone: 0 wild creatures in the reloaded world, no fault (lab 20261113-043251, Carol on free lane 2;
direct run lane 4 + 5: 0 of 17). Its catch was seen: with SPORE_FIX_OFF=savestrip.on 6 of 10 came back within 20 m
of a proxy's place (direct run lane 4 + 5, FAIL). savestrip already strips them in Civ (npc::is_proxy, and Civ's
herds pass the own-kind test), no change needed. Consequence: a guest's own save of the host's Civ world has no wild
creatures (its own were removed by the sync). The host has no proxies; its per-guest quiet table is memory only.
Found on the way: BUG-1067 (empty game herds of Civ stripped from saves, pre-existing).
Stage change and capture checks with the branch: civ_capture_keeps PASS out/lab/20261113-044104, tribe_civ_rejoin
PASS out/lab/20261113-044109.

## History
- 2026-11-12: opened from the BUG-0970 analysis; not part of 0.7.13 (bug release), planned later.
- 2026-11-13 (fix/w49-civ): fixed for 0.7.14 (package V1): host-owned wild creatures in Civ, test civ_wild_sync.
