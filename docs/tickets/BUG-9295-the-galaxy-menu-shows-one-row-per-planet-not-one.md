---
id: BUG-9295
title: The galaxy menu shows one row per planet, not one per era
status: fixed
kind: feature
stage: any
reported: 2026-11-20
source: decision 7H (docs/ROADMAP.md), the menu plan
role: any
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 4dd70c23
tests: in game through tools/qa_win/qa.py (QA 7 on the galaxy menu: coop.gmenu, screenshots)
signature:
docs: docs/12-server-and-players.md#server-era-visitors-solid
related: BUG-9287
---

## Report
The owner, 2026-11-20: "We'd also have to ensure that these planet copies don't appear on the galaxy menu, since
they're going to be all on the same planet in the menu." Each era was its own star ("Abber (Creature)", "Abber (Civ)").

## Root cause
Feature: the menu listed the server's planet keys, one per era.

## Fix
galaxy_menu.cpp: the server's list is grouped by planet: one star per planet, its eras' players added, the latest
era's stage icon and creature. Play enters the era this player last played on that planet (coop.ini [galaxy_eras]
"<galaxy>|<planet>", noted while playing), else the planet's earliest era. Switch `galaxy.menu_one_row`. The 5R stage
choice for a player new to a planet is not built (Spore's own "Choose A Stage" of an empty star is a candidate).

## Verification
TITAN, QA 8 Abber (Civ), QA 9 Abber (Creature), QA 7 Abber (Tribe) then QA 7 on the galaxy menu, 2026-11-20:
`coop.gmenu`: "5 of 5 server planet(s)" (Abber, Cellfour, Clyth, Jiff Peanutbutter, Samuth: 8 eras); Abber's star reads
"Abber", Civilization Stage (images/stage4_menu_one_row.png). Play on it: "Joining Abber (Creature)...", the remembered
era (coop.ini of the QA copy: QA 9 had played it last).
no-fail-run: a menu layout (before: one star per era, seen in the earlier galaxy menu runs).

## History
- 2026-11-20: opened.
- 2026-11-20: built; seen in game.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
