---
id: BUG-9335
title: Galaxy menu: a player new to a planet chooses among its stages (the ones reached in their own Spore)
status: open
kind: feature
stage: all
reported: 2026-11-22
source: decision 5R A, roadmap-alpha-now (the stage choice for a player new to a planet)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in:
branch: feat/lineages-alpha
commits:
tests: tools/qa_win (manual, 2026-11-22): TITAN's test galaxy (worlds_6ga copy), QA 9, coop.gmenu forget / play Samuth, a click on the choice, coop.fix galaxy.stage_choice off/on
signature:
docs: docs/12-server-and-players.md#server-stage-choice
related: BUG-9295, BUG-9334
---

## Report
Decision 5R A (owner 2026-11-17): eras per stage, players progress on their own, and a later stage can be started when
the player reached it in its own Spore. BUG-9295 left Play on a planet entering the player's last era there, else the
earliest: a player new to a planet of several eras could not pick one.

## Root cause
Not built yet (a feature).

## Fix
galaxy_menu.cpp: Play on a planet of several eras where this player has played none (nothing in coop.ini
[galaxy_eras]) opens a choice (the players' menu panel, "Choose a stage"): one button per era in stage order with its
players; a press enters that era and remembers it. Stages the player has not reached in its own Spore are grey and a
press on one says so (nothing entered); the reader of Spore's own unlocks is not in yet (every stage counts as reached
until it is). One era, or one remembered: entered as before. Switch `coop.fix galaxy.stage_choice off`: the earliest
era, as before. Tests: `coop.gmenu play <planet>` (the Play code, in the menu only), `forget <planet>`, `pick <n>`.
Not built: a stage the planet has no era of yet (a new era of the planet).

## Verification
TITAN, the test galaxy on a copy of worlds_6ga (Samuth: Creature and Tribe eras), QA 9 in the galaxy menu, Samuth forgotten:
PASS (switch on): Play opened "Choose a stage" with Creature and Tribe (screenshot); a click on Tribe entered
"Samuth|Tribe" (the game loaded Tribe) and remembered it; the next Play entered Tribe at once, no choice.
FAIL (`coop.fix galaxy.stage_choice off`, nothing remembered): Play entered the earliest era (Creature), no choice.
Not seen yet: a grey stage (needs the unlock reader).
no-fail-run: Windows QA by hand (no lab on this PC); the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: the choice built and seen in game; the unlock reader (Spore's own "reached" stages) being found.
