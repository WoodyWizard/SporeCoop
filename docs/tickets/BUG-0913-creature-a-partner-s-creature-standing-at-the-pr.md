---
id: BUG-0913
title: Creature: a partner's creature standing at the prey blocks the game's walk-and-attack (the second attacker stops 2 m short and its attack key does nothing)
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: fix/w47-play-bots, lab lane 9, 2026-11-10 (play_creature's joint hunts)
role: 
versions: 
fixed_in:
branch:
commits:
tests: play_creature (the bots walk around)
signature:
docs: docs/re/creature.md#creature-walk-and-use-blocked, docs/re/creature.md#creature-combat-click-target
related: BUG-0618
---

## Report
Found while settling play_creature's "joint hunts happened" (BUG-0618). The second player to attack an animal the
partner already fights, coming from the partner's side: the attack key walks its creature towards the animal and
stops it about 2 m short (gap 2.0-2.3 m, the bite reaches 1.0 m), 1.7 m from the partner's creature. Every further
press of the attack key does nothing (no step, no bite) for as long as both stand there (100 s in the probe). A
left click on the animal often picks the partner's creature instead (it stands in front): then the attack key does
nothing either (ability status 12). The player has to walk around the partner by hand.

## Root cause
Observed (lab lane 9, out/hunt_probe1.txt and out/hunt_probe2.txt of fix/w47-play-bots, 2026-11-10): the stop
behind the partner and the dead key as above; after a walk to one side (a click on the ground) the next key press
reaches the animal. Inferred, not checked: the game's creatures do not walk through each other and the game's
walk-and-use goal (0x20) takes the straight way; the partner's puppet is held at the partner's place by the mod's
drive, so it does not give way as a creature of the game might. Not checked: whether an NPC or a pack member of the
game in the way stops the walk the same way (then it is the game's own behaviour and only the click is ours).

## Fix
None yet (not part of the test task that found it). Ideas: the local player's walk-and-use ignores partners'
puppets as obstacles; a click in the combat stance does not pick a partner's creature while friendly fire is off.
The test bots walk around the partner now (play_creature.py, BUG-0618).

## Verification
Reproduce: two games on the creature fixture, both in the combat stance; the host clicks an animal and presses 1
(walks up, bites), the guest clicks the same animal from the host's side and presses 1: it stops 2 m short.

## History
- 2026-11-10: opened.
- 2026-11-11: the owner decided: fix in 0.7.13 (walk through the partner and click through it while friendly fire is off; unchanged with friendly fire on and in Rivals), docs/plan-0.7.13.md "Owner decisions (2026-11-11)".
