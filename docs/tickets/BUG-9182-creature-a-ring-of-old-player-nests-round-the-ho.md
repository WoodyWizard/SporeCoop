---
id: BUG-9182
title: Creature: a ring of old player nests round the host's nest (one per rejoin under the old keys)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host on Samuth + the MSI, 2026-11-13)
signature:
docs:
related: BUG-9173 BUG-9180
switch: creature.nest_ring_cleanup
---

## Report
Owner (2026-11-13): "there's a big circle of random nests around the center nest. Looks very unnatural". Decision 2Y A.

## Root cause
Observed: about a dozen nest herds of ours (a family of 4, respawn 120000 or 2000 ms) at 50 to 65 m round the host's nest on Samuth, one per session a partner came back under another species key (before BUG-9180); their families had died out (0 members), the species on no living creature.

## Fix
creature.nest_ring_cleanup (on), home_nests.cpp ring_cleanup, once per planet on the host: a nest herd within 160 m of the host's home with a family size of 4 whose species is neither the host's nor any seat's, not a seat's nest, not at a seat's noted spot (BUG-9173 takes those over) is removed when its members are of a player creation's key (0x7C) or, empty, its respawn time is ours and its species is on no living creature: the herd quieted, its members (the deferred destroy), its eggs, the nest and the herd through the game's DestroyInstance. Each kept one is logged.

## Verification
no-fail-run: manual. Before: 17 nests within 160 m of the host (coop.nest objs). The first rule (members only) removed 1 of 7; the 6 empty ones logged ("0 member(s), respawn 120000 ms") and removed by the second: 12 left, the three seat nests intact, 0 faults; the world saved for a share and loaded on the MSI (12 nests there), 0 faults on three games.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on feat/lineages-47 (decision 2Y A).
