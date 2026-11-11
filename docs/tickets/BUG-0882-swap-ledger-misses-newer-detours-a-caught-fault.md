---
id: BUG-0882
title: Swap ledger misses newer detours: a caught fault in the relevance pass leaves the game's sphere vector on our static buffer, one in the creature scale call leaves the player-species override set
status: open
kind: crash
stage: Creature
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0110, BUG-0280, BUG-0800
---

## Report
BUG-0110's premise holds: the game's own handler catches faults inside a call (logs show thousands of caught faults per frame), so code that changes game state before calling the original and restores it only on a normal return stays changed for good. The BUG-0110 ledger (`game::swap_open` / `swap_close`, undone once per frame by `swap_guard_tick`) covers only NPCTickAI, NoAttack, SelectUI, StageMsg and civ `b_damage`. Later detours do the same outside the ledger:
* With a partner in Creature or Tribe on the host, the relevance pass runs every frame; a fault inside it leaves the game's sphere vector (mgr+0x20/+0x24/+0x28) on our static array, which the game's vector code can later free or reallocate: heap corruption and a crash far away on the host.
* A fault inside the creature scale call leaves the player-species override set: every PlayerSpecies call on that machine then returns another creature's species (relations and scale wrong for the rest of the session).

Severity: medium. Confidence: medium.

## Root cause
* Observed (code): mods/coop/view_centres.cpp:116-122, `v[0] = g_buf; v[1] = ...; v[2] = g_buf + kMaxSpheres;` then `o_Relevance(...)`, then `v[0] = b; v[1] = e; v[2] = cap;`, with no ledger entry. The next h_Relevance copies from b == g_buf and "restores" g_buf again, so it never recovers.
* Observed (code): mods/coop/creature_hp.cpp:135-137, `g_ps_override = sp; o_Scale(...); g_ps_override = nullptr;`; after an unwind h_PlayerSpecies (creature_hp.cpp:118-122) returns the stale override on every call, and h_Scale itself bails out on `g_ps_override` (line 127) for good.
* Observed (code): the same pattern for mod-only guards: game.cpp:737 (`t_in_anim_fx`), game.cpp:778 (`t_in_evt`), game.cpp:933 (`g_in_relay`), social.cpp:344-349 (`g_goal`); a stuck guard silently switches off its relay or goal attribution.
* Observed (code): swap_open is called only at cellsync.cpp:723, 2514; civ.cpp:1772; coop.cpp:1456, 1509.
* Inferred: Relevance_Update walks creatures and herds, a path where caught faults on stale objects are plausible (medium); the heap corruption needs the game to grow or free the vector afterwards (medium-low).

## Fix idea
Open a ledger entry around both calls (h_Relevance: restore v[0..2] to the game's own pointers; h_Scale: clear g_ps_override) and let swap_guard_tick undo them like the others; reset the mod-only guards in the same per-frame tick. One shared helper ("lend X for one call") closes the class; the drama.cpp swap (BUG-0800) could use it too. Twin switch: npc.swapguard already turns the per-frame undo off.

## How to test
Extend `coop.npc swapfault` (e.g. `swapfault relevance|scale`) so the next h_Relevance / h_Scale returns without its restore, the state a caught fault leaves. Host in Creature with a guest far away: after one frame the game's mgr+0x20 begin pointer must not equal g_buf (expose it in `coop.views`), the game's PlayerSpecies call must return the host's own species, and the guest's nearby nest members must stay present on the guest's game (the BUG-0280 assertion). Must FAIL with `SPORE_FIX_OFF=npc.swapguard` and PASS with the fix; no crash report on either game.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0110. Severity medium, confidence medium.
