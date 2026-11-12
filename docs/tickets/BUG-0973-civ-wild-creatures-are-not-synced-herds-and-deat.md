---
id: BUG-0973
title: Civ: wild creatures are not synced, herds and deaths drift apart between host and guest
status: open
kind: desync
stage: civ
reported: 2026-11-12
source: lab
role: 
versions: 0.7.12
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0970
---

## Report
Found while fixing BUG-0970 (lab 20261112-155526, play_civ): at s 546 the guest's game had a wild creature at (-391 320 -68)
that the host's game no longer had within 10 m. civ.cpp has no sync for wild creatures (cCreatureAnimal) in the Civ stage:
right after a load both games agree within 0-3.8 m (probe on civ_npc, herd of 10, 5 min idle), but during play herds move,
flee or die separately in each game. The guest's vehicles are mirrors and deal no damage, so a creature the host's
vehicles kill stays alive on the guest's screen.

## Root cause
Observed: no wild creature sync in Civ. Inferred: separate AI and deaths per game.

## Fix
Not fixed yet. BUG-0970 only keeps the guest's order from being lost. A real fix needs host-owned wild creatures in Civ
(positions and deaths sent to the guest), a feature-sized change outside 0.7.13.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened from the BUG-0970 analysis; not part of 0.7.13 (bug release), planned later.
