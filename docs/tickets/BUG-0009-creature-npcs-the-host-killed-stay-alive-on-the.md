---
id: BUG-0009
title: Creature: NPCs the host killed stay alive on the guest after an editor or a load
status: fixed
kind: desync
stage: Creature
reported: 2026-10-25
source: player screenshots
role: guest
versions: 0.7.2.1
fixed_in: 0.7.2.3
branch: fix/creature-desync
commits: 1544735
tests: creature_desync
signature: 
docs: docs/re/creature.md 11
related:
switch: npc.stale_proxy, npc.world_despawn
---

## Report
"He can't eat the ones I killed since he still sees them alive."

## Root cause
The host renumbers its NPCs on an avatar change without sending despawns; the guest kept every old proxy next to the new one.

## Fix
The host despawns all known ids on a world change; the guest drops proxies without states for 5 s.

## Verification
coop_creature_desync.py FAIL before (22 living copies), OK 3/3 after.

## History
- 2026-10-25: reported (player screenshots).
- 2026-10-25: fixed on fix/creature-desync, released in 0.7.2.3.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: npc.stale_proxy, npc.world_despawn (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
