---
id: BUG-0543
title: Space allies: a partner's new colony in a star system both players own is not shown (the planet stays free)
status: fixed
kind: desync
stage: Space
reported: 2026-11-04
source: BUG-0197 (open end of space_allies.cpp)
role: both
versions: 0.7.5 and earlier
fixed_in: 0.7.6
branch: fix/w42-space-play
commits: a4a27f04
tests: space_allies_own
signature:
docs: docs/re/space.md#space-play-w42
related: BUG-0197, BUG-0195
---

## Report
Listed in BUG-0197: "only stars nobody owns in my galaxy are mirrored". In the usual session both players start as
one empire, so the home system and every system both owned at the join are "mine" in both games; a colony a
partner founds there afterwards (the commonest new colony: another planet of the home system) did not appear in
my game at all, the planet stayed free and I could found a colony of my own on it.

## Root cause
Observed (lab 20261104-113058, allies.own_star off): the guest's record-level colony on 01460035 (home star
4587573 of the `space` fixture), the host's game 30 s later: tech 1, no cCivData; `allies list`: "skipped 01460035:
your own star system".

## Fix
space_allies.cpp: such a colony becomes a record-level colony of my own empire (tech 5, the game's colony data,
`cEmpire_AddPlanet`: in my colony list), undone before every save and at the session end like the mirror
(taken out of SpacePlayerData's colony list, the loaded planet keeps the star's pol). A planet that is a colony
here already (one both had at the join) is left alone. Not in rivals mode. Switch `coop.fix allies.own_star off`.
Two pitfalls found on the way (docs/re/space.md#space-play-w42): without AddPlanet the HUD faulted 4 times at
0x107453A; resetting the planet's pol to -1 left the star ownerless.

## Verification
- FAIL (allies.own_star off): out/lab/20261104-113058.
- PASS: out/lab/20261104-113000 (0 faults on both games).

## History
- 2026-11-04: opened and fixed on fix/w42-space-play.
- 2026-11-04: released in 0.7.6.
