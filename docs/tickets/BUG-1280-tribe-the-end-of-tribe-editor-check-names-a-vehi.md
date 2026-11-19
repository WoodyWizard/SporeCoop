---
id: BUG-1280
title: Tribe: the end-of-Tribe editor check names a vehicle editor 7CB21BAD that does not exist (it is 7D433FAD)
status: open
kind: bug
stage: Tribe
reported: 2026-11-18
source: feat/w54-x1
role: host
versions: 0.7.15.2
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Found while reading the end of Tribe for WP-X1 (BUG-1276, Ghidra only): vote.cpp `tribe_civ_editor()` (used by
`tribe_graduating()`, BUG-0849: no world share or share save between the City Hall editor and Civ) lists the vehicle
editors of the end of Tribe as F670AA43, 9AD7D4AA and 7CB21BAD. The third does not exist in the exe (`scan 0x7CB21BAD`:
none); the game's third choice is 7D433FAD (docs/re/civ.md#civ-own-arrival). A player whose consequence archetype
(cPlayer +0x1104) is neither C2CA9495 nor 60A78928 gets the 7D433FAD vehicle editor, which the check does not know.
Not seen at runtime: `tribe::stage_end()` (the player tribe without its food mat) is the other half of the check and is
inferred to be true in that editor too, so the graduation guard probably still holds; the docs repeated the wrong id
(docs/re/tribe.md#tribe-civ-transition, corrected).

## Root cause
Observed (asm 0xCD2308..0xCD233B): `ADD EAX,0x9AD7D4AA` after `AND EAX,0xE26B6B03`: 9AD7D4AA + E26B6B03 = 7D433FAD
(mod 2^32). The id 7CB21BAD was a misreading. Inferred: harmless while tribe::stage_end() is true there.

## Fix
Not done (not part of WP-X1): replace 0x7CB21BADu by 0x7D433FADu in vote.cpp tribe_civ_editor(); a test would play
the end of Tribe with an archetype other than C2CA9495 / 60A78928 (tribe_civ_rejoin with that archetype set by
mem.write on cPlayer +0x1104 before the advance) and check that no share save is written in the vehicle editor.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
