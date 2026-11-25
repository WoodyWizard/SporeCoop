---
id: BUG-1566
title: Rivals: isolate drama events and avoid unnecessary guest world reloads on host Creature Editor
status: open
kind: bug
stage: Creature
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in:
branch: w081/rivals-editor-drama
commits:
tests: rivals_editor_drama, rivals_editor_drama_off
signature:
docs: docs/11-rivals.md
related: BUG-0486, BUG-1547, BUG-1551
---

## Report
1. `mods/coop/drama.cpp` has zero Rivals awareness: host-centric drama manager events (UFO visits, raids, random world events) sync relative to the host rather than isolating rival players in Rivals stages.
2. In Cell/Creature Rivals, when the host enters the Creature Editor to evolve their own species, the host's world-share / busy flow can trigger unnecessary full world reloads or progress disruption on guests even though each rival has an independent species, nest, and evolution bar.

## Root cause
- `drama.cpp` broadcasts host drama state unconditionally without checking active Rivals stages.
- `world.cpp` / `host_busy.cpp` / `creature_rivals.cpp` treat host editor return in Creature Rivals like shared-species Together mode rather than letting guests keep playing with live `species_line` / puppet updates.

## Fix
- Audit and isolate `drama.cpp` in active Rivals stages (`COOP_FIX("rivals.drama_isolate", "BUG-1566")`).
- Audit `world.cpp` / `host_busy.cpp` / `creature_rivals.cpp` so host Creature Editor visits in Creature Rivals do not force unnecessary guest world reloads or disrupt guest progress (`COOP_FIX("rivals.editor_no_reload", "BUG-1566")`).

## Verification
Pending implementation and Wine lab ON/OFF twin verification.
