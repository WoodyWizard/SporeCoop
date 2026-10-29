---
id: BUG-0015
title: Creature: killed enemies stand alive at 0 health on the guest (hunt quest)
status: fixed
kind: desync
stage: Creature
reported: 2026-10-25
source: player report (Discord) + screenshot
role: guest
versions: 0.7.2.3
fixed_in: 0.7.2.4
branch: fix/creature-kills
commits: afa5ae1
tests: creature_kills
signature: 
docs: docs/re/creature.md 14
related:
switch: npc.play_death
---

## Report
"Enemies still seen as alive even when dead for quest."

## Root cause
The death itself is done by the creature's behaviour tree; mirrors run no AI, so a replayed lethal hit left them at 0 hp, alive for the guest's game.

## Fix
game::play_death calls Die and plays the death animation on the guest's mirror.

## Verification
coop_creature_kills.py part 1 FAIL before, OK 3x after.

## History
- 2026-10-25: reported (player report (Discord) + screenshot).
- 2026-10-25: fixed on fix/creature-kills, released in 0.7.2.4.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: npc.play_death (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
