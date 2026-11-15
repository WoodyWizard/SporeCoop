---
id: BUG-0769
title: F5: one creature for everyone (creature.mode: everybody plays the host's creature)
status: fixed
kind: feature
stage: Creature
reported: 2026-11-08
source: plan-0.7.12 F5 (Discord: Sterben, Lucas4001MC)
role: all
versions: 0.7.11
fixed_in: 0.7.12
branch: feat/w46-settings-all
commits: 6efaacb0, d36b281b, b589db32, 21ed8a20
tests: one_creature
signature:
docs: docs/07-modes.md#modes-session-settings
related: BUG-0345, BUG-0508, BUG-0638, BUG-0768
---

## Report
F5 of docs/plan-0.7.12.md (Discord suggestions: the same creature for everyone; no evolution buttons for a guest who
cannot change the creature).

## Root cause
Not a bug: a guest always played its own choice (or its world record, BUG-0345 / BUG-0638) and evolved itself.

## Fix
Session setting `creature.mode` (Cell, Creature; own / host, default own). With host: a guest's creations::mine()
returns the host's creation of the stage (cell, creature: its newest bundle, else the world's species) whatever the
guest chose or recorded, so the avatar swap and the BUG-0508 in-place follow give every guest the host's creature and
each new version at once. coop.cpp h_Submit: the host's evolution opens at once (no vote, no offer); a guest's own
request (nest, mating call) is refused with a feed line; a vote end or an offer opens no own editor on a guest
(own_editor_tick drops a due one). The co-op window hides the guest's creation arrows and "Evolve now";
`coop.creations next|prev` is refused. `coop.creations` prints `creature mode: host|own`.

## Verification
one_creature (coop_one_creature.py, creature fixture, lane 6/7): the guest plays its own creature, the host sets
host mode: the guest's avatar becomes the host's species, no arrows; the host evolves (a real editor, a mouth added):
its editor opens at once, the guest gets no offer, its avatar becomes the host's new species and its puppet of the
host the new bundle, no editor ever opened on the guest, its own evolution request is refused.
PASS out/lab/20261110-190544; FAIL (SPORE_FIX_OFF=settings.all: creature.mode reads own) out/lab/20261110-190750.

## History
- 2026-11-08: opened.
- 2026-11-10: wired, tested on lanes 6/7 (PASS and FAIL runs below), fixed (unreleased).
- 2026-11-11: released in 0.7.12.
