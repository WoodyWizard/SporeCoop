---
id: BUG-9338
title: Galaxy: our stage choice box opens on top of Spore's stage loading card after a real Play
status: fixed
kind: bug
stage: all
reported: 2026-11-22
source: found while testing BUG-9337 (2026-11-22)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 28034591
tests: tools/qa_win (manual, 2026-11-22): TITAN, QA 9, real star + Play on Samuth with galaxy.native_stage_choice off; coop.fix galaxy.choice_box_no_card off/on
signature:
docs: docs/12-server-and-players.md#server-native-stage-choice
related: BUG-9335, BUG-9337
---

## Report
Seen while testing BUG-9337 with its switch off (TITAN, test galaxy, 2026-11-22): a real click on a server planet's
star, then its Play: Spore's stage loading card ("Civilization Stage" with a loading line) fills the screen and our
"Choose a stage" box (BUG-9335) opens on top of it. The card stays until a stage is pressed. Reproduce: `coop.fix
galaxy.native_stage_choice off`, Play on a planet with two eras that this player has not played.

## Root cause
Inferred: the panel's Play switches the menu to its Play state (0xC), which shows the stage card before its Play step
(SporeCoop::GGE_PlayStep) runs; BUG-9335 opens the box from that step, so the card is already up. (The BUG-9335 run
went through `coop.gmenu play`, which never entered state 0xC.)

## Fix
galaxy_menu.cpp h_SetState: when Spore's Choose A Stage screen is not used (its switch off, or it cannot open) and the
planet wants a stage choice, Play's switch to the Play state (0xC) becomes a switch to the planets (state 2, as the
screen's Back) and our box opens there, so no stage loading card comes up. Switch `coop.fix galaxy.choice_box_no_card
off`: as before (the box from the Play step, over the card).

## Verification
TITAN, the rebuilt test galaxy, QA 9 (every stage reached), Samuth forgotten, `galaxy.native_stage_choice off`, a real
click on Samuth's star and on Play:
- PASS (on): "Choose a stage" (Creature, Tribe (new)) over the stars, no card (images/bug9338_box_over_planets.png); the
  view stays zoomed on the planet until Esc, which zooms out with the box still there; a press works from either.
- FAIL (off): the "Creature Stage" loading card with the box on top (images/bug9338_off.png); its Creature press entered
  Samuth (Creature).
no-fail-run: Windows QA by hand; the runs above are the switch on and off.

## History
- 2026-11-22: opened (found while testing BUG-9337).
- 2026-11-22: fixed on feat/lineages-alpha (Play goes back to the planets and the box opens there).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
