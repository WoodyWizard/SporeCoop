---
id: BUG-0833
title: Creature: nest egg mirrors keep their references to the egg and its herd at every guest world change (suspected leak)
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0384 BUG-0426 BUG-0621
---

## Report
No player report. Suspected: each load, stage change or own editor of a guest leaks up to 20 eggs and their herds (address space over a long session).

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): nest.cpp make_mirror (226, 229) AddRefs the egg and the NPC nest herd; on_world_change (597-603) and the epoch branch of guest_tick (327-333) only null the pointers; remove_mirror (248-262) and the !mirror_ok path (340-343) skip the Release once the epoch changed or +0x21 is set. Not checked: the teardown's view of a ref-held egg. Confidence: low.

## Fix
Not fixed yet.

## Fix idea
Release our two references in remove_mirror also after a world change while the object still has our serial.

## How to test
Guest near 20 egg mirrors (`coop.nest objs`), reload the world N times; census of noun 0x2A034CD and herds must stay flat (fix) vs grow (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
