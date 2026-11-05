---
id: BUG-0585
title: Tribe -> Civ: the host's game faults at cTribe::UpdateFoodVisuals+0x28 and freezes after the City Hall editor when a guest's food change arrives
status: fixed
kind: freeze
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-transitions, coop_tribe_civ.py)
role: host
versions: 0.7.6
fixed_in: 0.7.7
branch: fix/w43-transitions
commits: dc4084f5
tests: tribe_civ (tools/tests/coop_tribe_civ.py)
signature: dc04aee5:SporeApp.exe Simulator::cTribe::UpdateFoodVisuals+0x28 (0xC94D08, data 0x34), caught, then the game freezes
docs: docs/re/tribe.md#tribe-civ-transition
related: BUG-0576
---

## Report
Found by the new test of BUG-0576 (the live Tribe -> Civ change): after the City Hall editor, while the host waited in
the vehicle editor (or right after it), the host's game logged a caught fault 0xC0000005 at UpdateFoodVisuals+0x28
(data 0x34) and froze: no frames, the console stopped answering, the guest timed out. Twice in a row with 0.7.6 code
plus the BUG-0576 fix (lane 4, scratch runs 22:27 and 22:38); a player who reached the vehicle editor would hit it
when its guest's tribe gathered or ate food meanwhile.

## Root cause
* Observed (disassembly, GOG 2024): `Simulator::cTribe::UpdateFoodVisuals` 0xC94CE0 reads the tribe's food mat
  `[tribe+0x260]` and calls `[+0x260]+0x34` vt+0x2C without a null check (`lea ecx,[eax+0x34]; mov eax,[ecx]`).
* Observed (lab): the fault comes with a guest's food change (`shared food +2.0 from player 1` on the host, whose log
  line is written after the call and so was missing in the frozen runs) after the City Hall stands: the player
  tribe's +0x260 is 0 then. Alone (the guest left) the same change to Civ had no fault.
* Inferred: the fault inside the message handling left a lock held: the freeze.

## Fix
`tribe.food_fx_mat` (game::tribe_food_visuals, used by shared.cpp s_food and tribe.cpp bonus_food): no food visual
when the tribe has no food mat; the food itself is changed as before. `coop.fix tribe.food_fx_mat off`: 0.7.6.

## Verification
Test `tribe_civ`: in the vehicle editor the guest adds food 3 times (`coop.res add food 2`), the host's game must
take it without a fault and go on to Civ.
* FAIL (SPORE_FIX_OFF=tribe.food_fx_mat): out/lab/20261104-224915 (lane 5): the fault at UpdateFoodVisuals+0x28,
  the host froze (console timeout).
* PASS: out/lab/20261104-224318 (lane 4), out/lab/20261104-224930 (lane 4): "no food visual: the tribe has no food
  mat" 3 times, both games in Civ, 0 faults.

## History
- 2026-11-04: opened (fix/w43-transitions, found by the BUG-0576 test); fixed the same day.
- 2026-11-05: released in 0.7.7.
