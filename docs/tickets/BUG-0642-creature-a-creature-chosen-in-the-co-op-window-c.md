---
id: BUG-0642
title: Creature: a creature chosen in the co-op window changes only the own avatar, the nest and the family stay the world's species
status: open
kind: feature
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): text only (Discord 2026-11-06)
role: both
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0418, BUG-0640, BUG-0345, BUG-0536
---

## Report
Discord report 2026-11-06 (0.7.8), text only (no report file): changing your creature with F9 (the co-op window)
changes only your own creature, not the nests (the home nest, the family stay the world's species).

## Root cause
Observed (code, docs/re/creations.md): the co-op window's choice swaps only the avatar's creation; the nest, the family
and the eggs stay the world's player species. Inferred: a guest (or host) playing another species than the world's
misses the player-species rules (its nest, the migration claim CreatureGame_NestHerdFor(1), the heal, the family): the
same root as BUG-0640 and possibly BUG-0639, class BUG-0418. Design question: should the own creature also be the
family's look (cosmetic) or should the rules follow the avatar's species.
Related: BUG-0344, BUG-0345, BUG-0536 (wontfix, the BUG-0345 default), BUG-0418.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: the guest picks its own creature in the co-op window; check its home nest's members, eggs and the minimap
     nest (the world species), and that a migration can still be claimed with the own creature. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
