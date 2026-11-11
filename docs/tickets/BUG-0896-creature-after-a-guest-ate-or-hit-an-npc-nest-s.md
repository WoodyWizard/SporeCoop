---
id: BUG-0896
title: Creature: after a guest ate or hit an NPC nest's egg a small creature's body lay at the egg's place and the host credited a hunt (+4 DNA) (need info)
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: owner, watching a player's recording 2026-11-10 (two screenshots)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0660, BUG-0384, BUG-0710
---

## Report
The owner watched a recording of a co-op session (version unknown, the screen is probably the host's). An NPC
nest's egg (glowing under the mouse) stood next to the players; the guest did something with it (ate it or hit it,
not clear from the recording). A moment later a small creature's body (a hatchling by its size, on its back) lay at
the egg's place and the mod's notice said "+4 DNA from Player's hunt" (the guest still had the default name).
Two screenshots only; no report file yet.

## Root cause
Not known. Two readings:
* No bug: the egg hatched at that moment and the guest killed the hatchling (half health, one bite); the body and
  the hunt DNA are then right and the place is a coincidence.
* A bug (inferred, low confidence): by the notes (docs/re/creature.md#creature-nest-eggs) a guest's eat of a
  mirrored egg sends MSG_NEST kind 1 and the host only calls cEgg::SetState(4) on its egg: no creature, no hunt
  DNA. Candidates: (a) a race at the hatch: the guest still shows the egg mirror (the host's egg state arrives at
  1 Hz) while the host's egg already hatched (state 3) and its hatchling exists; the guest's action lands on the
  hatchling's proxy or the host maps it to the hatchling; (b) the guest's click on the egg is sent as an attack
  (MSG_DAMAGE / the bite path) on the nearest NPC proxy; (c) SetState(4) on an egg that is hatching (state 3 or the
  hatch countdown +0x1F0 at 0) leaves a hatchling that dies at once (a body with no fight).

## Fix idea
Depends on the cause. For (a) and (c): the host refuses a guest's eat of an egg that is hatching or hatched and
answers with the egg's real state; the guest removes its mirror at once on that answer. For (b): the guest's eat
node on an egg mirror must not also send a hit.

## How to test
Two games in Creature at an NPC nest with eggs (the nest_eggs tests' fixture): (1) the guest eats an egg mirror:
on the host the egg is state 4, no new creature and no body within 5 m, the DNA total unchanged, no hunt notice;
(2) the same at the hatch moment (set the host egg's +0x1F0 countdown to 1 s, the guest eats within that second);
(3) the guest charges into the egg (the hard-collision break). Check the host's noun list for a creature made and
killed in that second.

## History
- 2026-11-10: opened from the owner's observation; need the recording's time code or a report archive.
