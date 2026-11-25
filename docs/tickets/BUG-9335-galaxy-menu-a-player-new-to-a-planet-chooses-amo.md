---
id: BUG-9335
title: Galaxy menu: a player new to a planet chooses among its stages (the ones reached in their own Spore)
status: fixed
kind: feature
stage: all
reported: 2026-11-22
source: decision 5R A, roadmap-alpha-now (the stage choice for a player new to a planet)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 08ad7e19, ee0da001
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
press on one says so (nothing entered). The reader is Spore's own: the stage chooser's check
(SporeCoop::GGE_IsStageUnlocked, or the menu's `levels -unlock` byte), out of the menu the earned achievements
(docs/re/space.md#space-stage-unlocks). One era, or one remembered: entered as before. Switch `coop.fix galaxy.stage_choice off`: the earliest
era, as before. Tests: `coop.gmenu play <planet>` (the Play code, in the menu only), `forget <planet>`, `pick <n>`.
Not built: a stage the planet has no era of yet (a new era of the planet).

## Verification
TITAN, the test galaxy on a copy of worlds_6ga (Samuth: Creature and Tribe eras), QA 9 in the galaxy menu, Samuth forgotten:
PASS (switch on): Play opened "Choose a stage" with Creature and Tribe (screenshot); a click on Tribe entered
"Samuth|Tribe" (the game loaded Tribe) and remembered it; the next Play entered Tribe at once, no choice.
FAIL (`coop.fix galaxy.stage_choice off`, nothing remembered): Play entered the earliest era (Creature), no choice.
Grey stage (a test profile with Creature and Space earned, not Tribe; Spore's own chooser shows the same): the
reader gave Cell 1, Creature 1, Tribe 0, Civ 0, Space 1; the choice showed Tribe grey; a click on it was refused with the
notice and nothing entered; a click on Creature entered Samuth's Creature era (screenshots). The first run read every
stage as reached: the QA copy had the old symbol file (no reader: not refused, by design); with the new file it read as
above. The notice was then shortened (it was cut off); the shorter text is not seen in game yet.
no-fail-run: Windows QA by hand (no lab on this PC); the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: the choice built and seen in game.
- 2026-11-22: Spore's stage unlocks found (each a hidden achievement) and read; grey stages seen and refused in game.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
