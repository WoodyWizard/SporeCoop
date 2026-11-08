---
id: BUG-0729
title: Content: an animation only the pack part has (a part the guest lacks replaced by a lookalike)
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in:
branch: fix/w46-k2b-content-b
commits:
tests:
signature:
docs: docs/re/creations.md#creations-k2b-design-content
related: BUG-0424, BUG-0559
---

## Report
K2b audit of the class BUG-0424 (BUG-0559's open point): a part the guest lacks is shown as a lookalike; an animation only the pack part has was not handled, impact unknown.

## Root cause
Static, the lab lane's install (2026-11-08, tools/pack_part_props.py over the part property lists, type 0x00B1B104, of bp1content/BoosterPack_01.package, Data/Spore_Pack_03.package and the base game packages): 147 pack creature parts, 497 base creature parts (no pack parts of the cell, UFO / vehicle or building groups). The only property of a pack part that no base part has is 0x13C6A371 (in the 24 parts of Spore_Pack_03): a key of another part of the same pack (75A5D2D4 -> 44FBC7C9, 75A5D2D5 -> 44FBC7C8: the part's pair), no animation. Every key-typed property of the pack parts (0x00B2CCCB, 0x00F9EFBB, 0x02424655..57, 0xD3BCDC94, ...) is one base parts have too. And a game that lacks the part never reads its properties: the model it builds names the lookalike, whose property list (animations included) is that game's own; the species data follows the model (content.species_data, BUG-0559).

## Fix
None needed: no animation is bound to a pack part in a way the receiving game could miss.

## Verification
no-fail-run: static evidence above (no pack-only animation property; the substitute's own properties are what the receiving game reads).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): not reproduced; the property audit above.
