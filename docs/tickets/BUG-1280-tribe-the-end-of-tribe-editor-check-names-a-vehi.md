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
commits: 5488e40d, 6fdea267
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

CORRECTION (2026-11-19, fix/w57-sync, lab run 20261119-104136): the test set the archetype before the advance; the stage
card writes the tribe's own trait into cPlayer +0x1104 afterwards (C2CA9495 back from the City Hall editor), so the game
opened F670AA43 in both the PASS run and its twin. The test sets it again back from the City Hall editor
(docs/re/tribe.md#tribe-card-archetype); no mod change.

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
- Lab, first night run (rel/0.7.16 c7699dca): tribe_civ_aggressive FAIL out/lab/20261119-104136 and its twin
  out/lab/20261119-105127 (rel/0.7.16 worktree) for the same test reason (the game opened F670AA43, see above).
- Lab after the test fix (fix/w57-sync 6fdea267, run dirs in the fix/w57-sync worktree): PASS tribe_civ_aggressive
  out/lab/20261119-111559 (the game opened 7D433FAD, end_editor=1, both games reached Civ). FAIL twin
  (SPORE_FIX_OFF=tribe.graduation_vehicle_id) out/lab/20261119-112631 for the right reason: the game opened 7D433FAD and
  the guard did not know it (end_editor=0).
- Rerun on the tree merged with rel/0.7.16 (fix/w57-sync 3801b9f5): PASS tribe_civ_aggressive out/lab/20261119-121505.

## History
- 2026-11-18: opened.
- 2026-11-19: fixed on fix/w56-general (WP-F1); the Lineages fork found the same (its BUG-9060).
- 2026-11-19: lab verified (fix/w57-sync): the test sets the archetype after the stage card; PASS and FAIL twin run dirs above.
