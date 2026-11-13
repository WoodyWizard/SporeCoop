---
id: BUG-0995
title: Creature: puppets held in the air (game.cpp g_air) are matched by address only and never cleared at a world change or session end
status: open
kind: bug
stage: Creature
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-7
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0532, BUG-0550, BUG-0946, BUG-0426
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-7. Not seen at runtime, found by code reading. `g_air` (game.cpp) holds
creatures the mod keeps in the air (partner puppets, NPC proxies, Tribe mirrors). Every cCreatureAnimal::Update and
animation start hook (h_Update, h_AcPlay and three more) compares the updated creature (or its AnimatedCreature) with
the held pointers by address only and, on a match, places it at the held position and clears its grounded flag. The
cleanup `game::air_release_all()` exists but nobody calls it. A held entry whose creature the game freed without the
mod forgetting it would place the next creature built at that address (an animal of the guest's own world) in the
air at the old spot.

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/game.cpp:96-104 AirHold {c, ac, pos, vel, rot}; game.cpp:1853-1865 air_hold; 1867 `air_release_all()` has
  no caller (`grep -rn air_release_all mods/` finds only the definition and game.h:129).
* game.cpp:1869-1881 air_reassert and 1885-1895 h_AcPlay: a match by `h.c == c` / `h.ac == ac`, no serial, no list check.
Mitigation already on main: `puppet_unregister` releases the hold (game.cpp:2220-2224, BUG-0550), and every forget path
of npc.cpp (check_listed, check_identity, destroy_proxy, on_world_change), modes.cpp (creature_despawn, puppet_alive)
and tribe.cpp (BUG-0946) calls it or air_hold(c, false). Inferred (low-medium confidence): the remaining gap is a held
creature freed by the game while no forget path ran (a world change without on_world_change between the free and the
next Update; the settle second of npc.cpp guest_tick); then the reused address is placed. Severity: low; no player
report of it.

## Fix idea
Keep the serial in AirHold (game::object_serial at air_hold) and require `game::alive(LiveRef{c, serial, 0x18EB45E})` in
air_reassert; call air_release_all() from game's world change and the session end (the lifecycle reset package).
Switch: `game.air_live`.

## How to test
tools/tests/coop_npc_air.py (NPC proxy in the air on the guest): hold a proxy in the air, then make the guest's game
destroy it natively inside the settle window (`coop.npc settle 3`) and spawn a local animal of that species until one
takes the freed address (as coop_npc_reuse.py does); assert that animal's height over the ground on the guest stays
its own (the game's position, not the held one). FAIL on main if the address is reused before a forget path runs;
PASS with the serial check.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
