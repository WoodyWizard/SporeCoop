---
id: BUG-1280
title: Tribe: the end-of-Tribe editor check names a vehicle editor 7CB21BAD that does not exist (it is 7D433FAD)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-18
source: feat/w54-x1; also found by the Lineages fork (PR #196), its BUG-9060
role: host
versions: 0.7.15.2
fixed_in: unreleased
branch: fix/w56-general
commits: 5488e40d
tests: unit (tribe_tests end_editors)
signature:
docs: docs/re/tribe.md#tribe-end-editors
related:
switch: tribe.graduation_vehicle_id
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
tribe_editors.h: the archetype rule of 0xCD2308 and the end-of-Tribe editor list (99E92F05, F670AA43, 9AD7D4AA,
7D433FAD); vote.cpp tribe_civ_editor() uses it. Evidence for the id: the asm (9AD7D4AA + E26B6B03 = 7D433FAD), the
exe's archetype table at 0x156ACD0 (tribe_mixed C2CA9495, tribe_social 60A78928, tribe_aggressive 0CAC124D), and the
game data: editor setups type 0x00B1B104 group 0x40600100 instances F670AA43, 9AD7D4AA, 7D433FAD in Spore_Game.package
and PatchData.package, no 7CB21BAD anywhere (docs/re/tribe.md#tribe-end-editors). Switch
`coop.fix tribe.graduation_vehicle_id` (off: the old list with 7CB21BAD). Test command
`coop.vote tribeend [<archetype hex>]` (the guard's inputs; sets cPlayer +0x1104).

## Verification
- Unit: tribe_tests end_editors PASS (every archetype's vehicle editor is an end-of-Tribe editor); tribe_tests --nofix
  FAIL (2 checks: tribe_aggressive's 7D433FAD and any other archetype's).
- Lab test tribe_civ_aggressive (coop_tribe_civ.py --aggressive: the host's archetype set to tribe_aggressive before the
  advance, the game's second editor must be 7D433FAD and the guard must know it) written, NOT run yet (the lab waits
  for "lab ok"); SPORE_FIX_OFF=tribe.graduation_vehicle_id is its FAIL twin.

## History
- 2026-11-18: opened.
- 2026-11-19: fixed on fix/w56-general (WP-F1); the Lineages fork found the same (its BUG-9060).
