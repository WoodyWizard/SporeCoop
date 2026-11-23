---
id: BUG-9343
title: Galaxy: a player new to a planet starts its Civ stage when the planet has only Tribe (a new era)
status: fixed
kind: feature
stage: tribe
reported: 2026-11-22
source: roadmap-alpha-now (5R A: a Civ start from a Tribe era)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 19b175f1
tests: tools/qa_win (manual, 2026-11-22): TITAN's rebuilt test galaxy, QA 9 new to Clyth (Tribe only), real clicks through Spore's screens; coop.fix galaxy.civ_start off/on
signature:
docs: docs/12-server-and-players.md#server-civ-start
related: BUG-9336, BUG-9337, BUG-9340, BUG-9292
---

## Report
Roadmap (alpha-now): "a Civ start from a Tribe era" was left after BUG-9336 (decision 5R A: a later stage can be
started when the player reached it in their own Spore). BUG-9336 grew a planet's Tribe era from its Creature world;
a planet with only a Tribe era offered nothing new.

## Root cause
Not built yet (a feature). The Tribe stage's end had no request the mod could ask: it is the HUD's advance button
(control 0x6454B47, TribeHutUI_HandleMessage 0xCDBEA0) once the goal bar (cPlayer +0x10F0 / +0x10F4) is full; the
button opens History ("Congratulations! Your Tribe reigns supreme!"), whose check plays the stage's end cinematic
(TRG2CVG_PreEditor), whose end opens the City Hall editor, then the first vehicle's editor, then Civ.

## Fix
galaxy_menu.cpp new_stage(): the stage after the planet's latest era from its stored world: Creature -> Tribe
(BUG-9336) and now Tribe -> Civ, in Spore's Choose A Stage screen, the fallback box and the planet details panel
("New here: start it"). The server already took any one-stage-up start (no change). In the start crossing the game
asks the Tribe stage's end (coop.cpp start_stage -> tribe::stage_end_request): the goal bar full (the code of `coop.tribe
advance`, now shared), then the advance button pressed through the HUD's own handler (a button click message, as a
player's click). From there the player plays Spore's own sequence: History's check, the cinematic, the City Hall and
vehicle editors; the crossing then arrives as the planet's Civ era (BUG-9292's path). `coop.tribe stage_end` shows
the counters. Switch `coop.fix galaxy.civ_start off`: a Tribe-only planet offers no Civ stage.

## Verification
TITAN, a copy of the rebuilt base galaxy (Samuth Creature, Abber Civ, Clyth Tribe), QA 9 (every stage reached, new
to Clyth), real clicks:
- PASS (on): star, Play: Spore's screen with the fire lit and the globe lit as new; the planet panel "Civilization: New
  here: start it" (images/bug9343_civ_new.png). The globe: the server made "Clyth~1|Tribe" from Clyth's Tribe world;
  hosted 5 s later the game filled the bar (5 / 5) and pressed the advance button: History with "Congratulations!"
  (images/bug9343_history.png); its check, the cinematic, the City Hall editor (3 parts needed), "You are about to enter
  Civilization", the vehicle editor, Civ with its city (images/bug9343_civ.png). Server: "Clyth|Civ: the party of 1
  arrived from its crossing and hosts it", then stored Clyth (Civ) (3870 KB) beside the untouched Clyth (Tribe).
- FAIL (`coop.fix galaxy.civ_start off`, a fresh copy): Play on Clyth opened no choice and entered Clyth (Tribe) at
  once (images/bug9343_off.png).
- Not run: two players (a party's start), the MSI.
no-fail-run: a feature, Windows QA by hand; the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: built on feat/lineages-alpha and seen in game through to the stored Civ era; switch off: no Civ offer.
