---
id: BUG-9241
title: Interface Size: Spore's tooltips drawn about 1.25 x the pointer's place at 125 %
status: fixed
kind: bug
stage: civ
reported: 2026-11-16
source: QA 2026-11-16 (TITAN, BUG-9238 checks)
role: both
versions: 0.7.14.1-lineages61-dev
fixed_in: 0.7.14.1-lineages62-dev
branch: feat/lineages-47
commits: 5dc60eb7
tests: manual Windows QA (TITAN, Civ Abber.spo at 125 %, coop.fix ui.tooltip_place on/off)
signature:
docs:
related: BUG-9238, BUG-9191
---

## Report
QA 2026-11-16 (TITAN QA copy, Civ Abber.spo, 1280 x 960, Interface Size 125 %): with the pointer on a city's vehicle button, Spore's tooltip ("Buy Sea Vehicle ...") was drawn about 150 px right of and 120 px below the pointer (out/shots/r64/civ_hover.png). Found while checking BUG-9238 in the other stages.

## Root cause
Observed (`coop.ui tree 8 all` with the pointer at 593,463): the tooltip window 0x03754E6C, a child of the layer 0x05B598F6 (with the cheat console and other HUD pieces), had the area [502 483 686 503]: centred on the pointer's real screen place and 20 px under it. The layer is drawn scaled by f about the top left (3X A), so it was drawn at f x that place. Inferred: the game places the tooltip from the pointer's real position (not the remapped mouse messages).

## Fix
- ui.tooltip_place: the tooltip window gets a transform handler; its middle is moved back to where the game put it (x[12] / x[13] += place / f - place, the parent's scale applying to the move), kept on the screen, still drawn at the Interface Size. The whole-scale tick leaves it out of the "pieces placed in code" pass.

## Verification
FAIL with the fix off, PASS on (TITAN, the same session, `coop.fix ui.tooltip_place on/off/on`, pointer on the vehicle button at 593,463): off, "Buy Land Vehicle" drawn centred at about 745,615 (out/shots/r64/tip_a_off.png); on, centred at about 594,492, just under the pointer (tip_a_on.png).

## History
- 2026-11-16: opened; fixed (ui.tooltip_place).
- 2026-11-16: released in 0.7.14.1-lineages62-dev.
