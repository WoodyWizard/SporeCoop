---
id: BUG-9306
title: Tribe (guest): the chief's stage powers (Fire Bomb, Summon) are refused, later stage powers likely too
status: open
kind: bug
stage: Tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9303
---

## Report
Live 67-dev, Tribe, a guest (owner, 2026-11-20): "Firebombs did not work for BigTexas." "They don't need to hold torches,
the chieftain will always be able to throw the bombs and he refused." Fire Bomb is the chief's power from ending the
Creature stage as a carnivore (7N B). "The Summon power is the same type of power as Firebombs. Same with the MOAB and
stuff later on in the game." The owner expects every stage power a guest has to be broken.

## Root cause
Not found yet. Likely: the guest's chief power order is not in the host's allowlist (host_command, BUG-9303), or it
acts on the host's chief.

## Fix
Planned: the guest's chief uses its own village's stage power through the host; each power checked in a two-player
test; Civ and Space powers checked as the same class.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
