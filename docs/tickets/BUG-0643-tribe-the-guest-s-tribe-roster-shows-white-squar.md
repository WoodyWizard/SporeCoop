---
id: BUG-0643
title: Tribe: the guest's tribe roster shows white squares instead of the members' portraits
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-12
role: guest
versions: 0.7.8
fixed_in: unreleased
branch: fix/w46-tribe-civ
commits: 80846470
tests: tribe_portraits
signature:
docs: docs/re/tribe.md#tribe-portraits
related: BUG-0424, BUG-0431, BUG-0642
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-12 (the guest, mark at s 4051): "I see tribe members on the
right side of the screen as white squares". The guest's report shot (s 4056) shows the right-hand roster column with
the chief's icon, then plain white squares where the member portraits belong.

## Root cause
Observed (the guest's log): the guest plays its own creature in Tribe (own avatar 29182710 from 18:35:59, its own
Creature editor at the Creature -> Tribe change); the host's creations arrive as "2 records" (model and species data,
no thumbnail image); no line about portraits.
Inferred: the member portraits come from the species thumbnail, which the guest's game lacks for its swapped own
species or for the host's species registered without a picture. Class BUG-0424 (content the other game lacks) or
BUG-0431 (objects the mod builds incompletely).

Observed 2026-11-07 (lab lane 5): the roster portraits are the species' png {0x2F7D0004, 0x40626200, species}: a broken png there gives the failed-image square in every slot. The host's own creations (here: the species after the host's Creature -> Tribe editor, "Sterben evolved the species" in the guest's lobby) reached the guest with bem, metadata and creaturedata only: no png on the guest.

## Fix
`creations.png` (BUG-0643): a creation's records include its pictures (png in the model group and the thumbnail group +1), for every bundle (the host's own species, player creations). Off: as 0.7.8.

## Verification
Test tribe_portraits (coop_tribe_portraits.py): the host's tribe becomes the host's own TigerHorse (not in the guest's My Creations), the guest's game starts again and joins; the guest's game answers the species' png.
- FAIL (SPORE_FIX_OFF=creations.png): out/lab/20261107-081042.
- PASS: out/lab/20261107-081214, out/lab/20261107-080546 (the leave/join variant of the test).
- Not verified: the portrait on the screen (in the lab the roster kept the fixture species' portraits after the species change, docs/re/tribe.md#tribe-portraits).

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: fixed in fix/w46-tribe-civ (creations.png).
- 2026-11-07 (fix/w46-final-triage): suite.py allows the gate finding the test makes on purpose (lab 20261107-141346: tribe_portraits, monitor puppet `hidden: state older than 5 s` while the killed guest game was silent before its restart).
