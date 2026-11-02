---
id: BUG-0381
title: tribe: partner / members apart and citizens missing on guests (sync-check p apart, t missing, n apart)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
role: 
versions: 
fixed_in: 0.7.3.4
branch: fix/w32-tribe-sync
commits: 58f0c35 2b338e2
tests: tools/tests/coop_tribe_npc_form.py (suite tribe_npc_form)
signature:
docs: docs/re/tribe.md#tribe-npc-forming, docs/06-testing.md#testing-tribe-sync-gates
related: BUG-0380, BUG-0393, BUG-0402, BUG-0288
---

## Report
`lab.py gates`: Tribe p apart in 31 results, t missing 10, n apart 8 (campaign_tribe 20/50, chaos_tribe 4/89, tribe_coop 1/90).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Three findings in one ticket:
* **p apart** is the moving RTS pointer (the check, not the sync): BUG-0393, fixed by BUG-0380's
  `diag.rts_cursor`; FAIL / PASS runs in Tribe there.
* **n apart** is split off to BUG-0402 (not reproduced; one NPC on every guest for minutes after the co-op
  epic came, herds just over the limit, an NPC at the planet's centre); a MISMATCH now names what each game
  knows of the NPCs / citizens in it (`sync-check:   n327 here: ...`, commit 2b338e2) so the next run says.
* **t missing**, below.

**t missing (observed, logs + runtime on lane 7):** the host's game forms new NPC tribes during Tribe
(`cNpcTribeController` 0xCC6440, Simulator::SpawnNpcTribe; docs/re/tribe.md#tribe-npc-forming); the guests'
games did not (out/lab/20261031-000328 campaign#1: host 22 -> 48 tracked citizens in 3 minutes after a
cinematic, every guest kept `local_tribes=2`, "t: 27 missing" for the rest of the stage; 20261031-185843
campaign_tribe#1: two of three guests lacked the second tribe, "t16 t17 t18 missing"). A guest maps each
host tribe to a tribe of its own game within 40 m (tribe.cpp guest_state) and skipped the members of a host
tribe without one: no mirror at all, the players on the guests never saw those tribes. Reproduced on lane 7:
the host forms an NPC tribe (`coop.tribe npc_tribe 4 70`); the host's own controller then formed two more
(archetypes 8 and 12, 5 members each); the guest's game formed none of them.

## Fix
**t missing:** `tribe.npc_tribes` (tribe.cpp): the host appends each tribe's species key and archetype to
its MSG_TRIBE_STATE chunks after the BUG-0365 clock trailer; a guest calls SpawnNpcTribe (host position,
archetype, no members, the species through creations::local_species / want_species) for a host NPC tribe
without a tribe of its own within 40 m, remembers it and binds the mirrors into it. Off: the guest has only
the tribes its own game formed (the old behaviour).

## Verification
`tools/tests/coop_tribe_npc_form.py` (suite tribe_npc_form): the host's game forms an NPC tribe 70 m from
the village; within 15 s the guest's game lists a tribe there with every member of the host's, of the same
species, each within 8 m (the guest's game's own tribe list and member vectors).
* FAIL with `SPORE_FIX_OFF=tribe.npc_tribes`: out/lab/20261101-122850 (the guest has no tribe there).
* PASS: out/lab/20261101-123142 (2/2; all five host tribes on the guest, 3 made by the fix); out/lab/20261101-123008
  passed its checks too but the lab counted the known caught fault at 0x11ECE4F of a cold start (before the test).

## History
- 2026-11-01: opened.
- 2026-11-01 (fix/w31-civ-gate): Civ `p apart` was the moving pointer (the partner's copy trails 45 m median while the mouse moves, 0 m at rest; coop_civ_cursor.py); Tribe uses the same pointer adapter (modes.cpp rts_*), so its `p apart` likely has the same cause. `diag.rts_cursor` (BUG-0380) leaves a moving pointer out in both stages; not verified in Tribe.
- 2026-11-01 (fix/w32-tribe-sync): t missing = NPC tribes the host's game formed and the guests' did not; fixed (tribe.npc_tribes). p apart -> BUG-0393 / BUG-0380, n apart -> BUG-0402.
- 2026-11-01 (feat/w33-tests-session): transition_net (Cell -> Creature -> Tribe the players' way, SPORE_NET=player, guest 30 fps; lab 20261101-142943, lane 10): 58 s after the guest followed into Tribe both games logged `sync-check: MISMATCH ... t: 3 missing on the guest (t4 t5 t6)` (34 objects compared). Reproduced 2 of 2 on main a05d1f6 (also lab 20261101-145528) before the fix was merged; transition_net allowed it by name until then.
- 2026-11-02: released in 0.7.3.4.
