---
id: BUG-0642
title: Creature: a creature chosen in the co-op window changes only the own avatar, the nest and the family stay the world's species
status: wontfix
kind: feature
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): text only (Discord 2026-11-06)
role: both
versions: 0.7.8
fixed_in:
branch: fix/w46-creature-p0
commits: 61706c54
tests: nest_follow_own (coop_nest_follow.py --own)
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
Not changed (by design): the nest, the family, the eggs, the claim and the heal are the world's player species' rules,
and they hold for a guest playing its own creature: its game claims the migrated nest, lives in its herd and heals
there like with the world's species. The co-op window's creature is the player's look (the avatar), as the window
says; the family keeps the world species' look.

## Verification
coop_nest_follow.py --own (suite nest_follow_own): the guest chooses its own creature (290225CD, another species than
the world's), the host migrates (the report's way), the guest's own game claims the host's new nest
(CRG_ClaimNest), its avatar's herd is that nest's, and the nest heals it (3.5 -> 21.0): out/lab/20261107-074827 PASS.
family_attack (out/lab/20261107-081513): with its own creature the guest's game answers the family as friendly (vt
0xD4) like the host's and the family takes no damage from it.
* Not verified: the family showing the own creature's look (not wanted, a design choice).

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07 (fix/w46-creature-p0, P0-Creature of 0.7.10): closed wontfix (by design: the own creature is the avatar's look, the nest rules hold for it): nest_follow_own PASS 20261107-074827.
