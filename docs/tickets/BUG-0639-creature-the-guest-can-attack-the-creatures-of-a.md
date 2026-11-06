---
id: BUG-0639
title: Creature: the guest can attack the creatures of a nest that is evolving on the host
status: open
kind: bug
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-05
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0464, BUG-0460, BUG-0418
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-05 (the guest): "for the host the nest is evolving, for the
guest it can attack them in creature stage".

## Root cause
Observed (logs): nothing explains it directly; the host's game refused none of the guest's hits in Creature (no
"spared" lines); around s 1265-1321 the host logs kills of several species (e.g. 06539227 "relation 5 -> 5 ... killed
0 -> 4", event 5 at s 1314).
Inferred, two readings: (a) an NPC species evolving at its nest is shielded on the host (the game's evolve state) but
its proxies on the guest are not; (b) the "nest" is the host's family nest: the guest plays another species (its own
creature), so its game treats the host's family as foreign and attackable (the BUG-0418 class, see BUG-0642).
Not determined: which nest the player meant (the screenshot was not decisive). Related: BUG-0464 (NPC nest evolution
animations relayed, fixed 0.7.4), BUG-0460 (nest state, fixed 0.7.4).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: make a herd evolve on the host (+0x85 should-evolve, +0xF0 generation, docs/re/creature.md#creature-npc-anims)
     next to the guest; check whether the host's game accepts the guest's hits on those members and whether the
     host's own avatar can hit them; the same next to the host's family nest with the guest on its own creature. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
