---
id: BUG-0014
title: Creature: the guest stays a hatchling after a death (egg loop, no control)
status: fixed
kind: bug
stage: Creature
reported: 2026-10-25
source: player report (Discord) + screenshots
role: guest
versions: 0.7.2.3
fixed_in: 0.7.2.4
branch: fix/death-loop
commits: 1ed2ab8
tests: creature_respawn
signature: 
docs: docs/re/creature.md 13
related:
switch: npc.hatch
---

## Report
"After my death I'm in constant state of growing up from egg ... can't move at all."

## Root cause
A respawned hatchling waits for a family member of the nest to greet it; a guest has no family (its NPCs are removed, the host's are proxies without AI).

## Fix
npc.cpp hatch_tick grows the waiting hatchling up (0xC04F30(1) + the pack).

## Verification
coop_creature_respawn.py FAIL on old code (the guest never grows), OK 3x after.

## History
- 2026-10-25: reported (player report (Discord) + screenshots).
- 2026-10-25: fixed on fix/death-loop, released in 0.7.2.4.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: npc.hatch (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
