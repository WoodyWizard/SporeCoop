---
id: BUG-1553
title: Creature Rivals: GetRelationship enum inversion, War PvP and family combat, pack recruitment, arrows, transition and NPC Influence UI
status: open
kind: bug
stage: Creature
reported: 2026-11-25
source: v0.8.0 independent audit
role: both
versions: 0.8.0
fixed_in:
branch: w0801/creature
commits:
tests:
signature:
docs: docs/11-rivals.md
related: BUG-1547
---

## Report
Independent static audit of `BUG-1547` found critical gameplay blockers in Creature Rivals:
1. `creature_rivals_logic.h:29-35,272` defines `kRelHostile=1` (actually scared/flee!) and `kRelExtinct=5` (actually aggressive predator, causing normal predators to be marked permanently extinct).
2. War PvP and family combat are broken: `puppets.cpp:172,223-260` blocks hovering/colliding rival puppets; `settings_core.h:122-133` leaves co-op pack bonuses enabled; `npc.cpp:1953-1979` clears host pack/family targets against rival puppets; `npc.cpp:1321,2448-2450,3791` places host `NF_FAMILY` proxies into guest's swapped `avatar_herd()`.
3. Guest pack recruitment (`social.cpp:1042-1043,1073,1103`) reads host avatar's relations and shares `impressed`; guest attacks (`event==2`) are swallowed without lowering guest's wild relation (`creature_rivals.cpp:791-794`).
4. Partner edge arrows are off in Creature Rivals (`find.cpp:246-247`).
5. `trigger_native_tribe_transition()` is console-only (`creature_rivals.cpp:1475`), never invoked on race/draft end.
6. `host_assign_nests` scans all objects at 60 Hz (`creature_rivals.cpp:1205`); no NPC wild species list for F9 Influence UI.

## Root cause
See `docs/plan-0.8.0.1.md` WP-3.

## Fix
TBD.

## Verification
TBD.
