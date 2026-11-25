---
id: BUG-1560
title: Creature: guest pack recruit followed by Creature Editor leaks companion to host and breaks host pack size limit
status: open
kind: bug
stage: Creature
reported: 2026-11-25
source: Discord (#bug-reports, Smile / Szmajol)
role: host
versions: 0.8.0.1
fixed_in:
branch: w0802/creature-pack-cap
commits:
tests: pack_editor_cap, pack_editor_cap_off
signature:
docs: docs/re/creature.md
related: BUG-1256
---

## Report
Discord (`Smile / Szmajol`): after the guest befriended/recruited a creature (from the host's species) and players entered the Creature Editor, the creature became the host's companion and the host could subsequently recruit an unlimited number of NPC creatures into their pack (exceeding the 3-member cap).

## Root cause
Pending investigation in `mods/coop/npc.cpp` (`recruit_for_player`, `mbTempPosseMember` `+0xE68`, `cPosseSimulator` `mTempCount` vs Creature Editor save/reload lifecycle).

## Fix
Pending implementation behind `COOP_FIX("npc.pack_editor_cap", "BUG-1560")`.

## Verification
Pending Wine lab verification (`pack_editor_cap` PASS + `pack_editor_cap_off` FAIL).
