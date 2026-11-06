---
id: BUG-0643
title: Tribe: the guest's tribe roster shows white squares instead of the members' portraits
status: open
kind: bug
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-12
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: tribe fixture with the guest playing its own creature (`coop.creations choose`); screenshot / read the
     roster panel's icons on the guest. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
