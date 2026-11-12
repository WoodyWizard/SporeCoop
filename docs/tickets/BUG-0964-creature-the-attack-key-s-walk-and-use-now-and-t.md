---
id: BUG-0964
title: Creature: the attack key's walk-and-use now and then never moves the guest's creature (presses do nothing for 12 s)
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: fix/w48-pass lab 2026-11-12 (partner_pass)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-partner-pass-rel
related: BUG-0913
---

## Report
In `coop_creature_partner_pass.py` the guest's creature, put by the test (`coop.npc gopos`) 4.5-8 m from an animal it
has as its combat target (combat stance, key 1 status 0), does not move at all after the attack key: 4 presses in
12 s, moved 0.0 m, the game's gap unchanged, its cLocomotiveObject desired speed 4.75. Seen with creature.partner_pass
on and off, on lanes 6 and 7, mostly at the first small animal of a herd (lab 20261112-120457 r9, -122259 r10,
-130029 three times in one round). A player would see the attack key do nothing until it moves by hand.

## Root cause
Not known. Inferred only: something at that spot holds the creature (terrain or an object, not read); the test's
teleport may play a part (a player's creature is never put there). Next: read the avatar's locomotion goal and
velocity in such a round, and try a walk by click from the same spot.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
