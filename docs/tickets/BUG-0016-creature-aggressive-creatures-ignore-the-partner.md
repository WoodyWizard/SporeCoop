---
id: BUG-0016
title: Creature: aggressive creatures ignore the partner
status: fixed
kind: bug
stage: Creature
reported: 2026-10-25
source: player report (Discord)
role: guest
versions: 0.7.2.3
fixed_in: 0.7.2.4
branch: fix/creature-kills
commits: afa5ae1
tests: creature_kills
signature: 
docs: docs/re/creature.md 14.2
related:
switch: npc.partners
---

## Report
"Aggressive enemies didn't target my friend."

## Root cause
NPCTickAI notices only cGameNounManager +0x54 (the avatar); the puppet is never considered.

## Fix
On the host an NPC nearer to a partner ticks with the partner's puppet as the player (npcs_target_partners).

## Verification
coop_creature_kills.py part 2 (the "noticed" stimulus with the puppet as sender) FAIL before, OK after. Attack on sight not proven end to end (no red-face nest in the fixture).

## History
- 2026-10-25: reported (player report (Discord)).
- 2026-10-25: fixed on fix/creature-kills, released in 0.7.2.4.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: npc.partners (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
