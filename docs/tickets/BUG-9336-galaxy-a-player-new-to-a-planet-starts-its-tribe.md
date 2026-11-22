---
id: BUG-9336
title: Galaxy: a player new to a planet starts its Tribe stage when the planet has only Creature (a new era)
status: fixed
kind: feature
stage: tribe
reported: 2026-11-22
source: roadmap-alpha-now (5R A: start a stage the planet has no era of)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 9f5b7abc
tests: tools/tests/galaxy_server_check.py (7 checks, "a player starts a stage its planet has none of"); tools/qa_win (manual, 2026-11-22): TITAN's test galaxy, QA 9 new to Samuth (Creature only), a real click on "Tribe (new)", coop.fix galaxy.stage_start off/on
signature: 
docs: docs/12-server-and-players.md#server-stage-start
related: BUG-9335, BUG-9278, BUG-9265
---

## Report
Decision 5R A: "a later stage can be started when the player reached it in their own Spore". BUG-9335 built the
choice among a planet's eras; a stage the planet had no era of yet could not be started (the roadmap's "starting a
stage the planet has no era of yet").

## Root cause
Not built yet (a feature). A planet's era is made by a game that plays that stage on the planet's ground; until now
only a party's own stage end (BUG-9278's crossing) made one.

## Fix
The stage choice of BUG-9335 also offers "Tribe (new)" on a planet that has a stored Creature era and no Tribe era
(grey when the player has not reached Tribe in its own Spore; a single-era planet opens the choice only when the new
stage is open to the player). A press sends STARTERA (net/galaxy.h, protocol 9): the server makes the player a crossing
of its own, "<planet>~<slot>|Creature", whose FETCH sends the Creature era's stored world (Planet::from), and answers
ROLE host. The game downloads and hosts it as any stored planet (a new player there makes its first creature first,
BUG-9265), then asks the game's own Creature stage end (creature_stage_end_request: the brain level raised to 4, the
stage-end request History's check sends; the same code `coop.vote tribe` runs now): the stage-end editor, "Congratulations!
You are about to enter the Tribal stage", the Tribe start with the player's village at its nest. Then the crossing's
ARRIVE (BUG-9278) makes it the planet's Tribe era, or joins the Tribe era's host if another player made it meanwhile.
The server refuses a start two stages up, where the stage has players or a stored world, and with no stored world to
start from. Switch `coop.fix galaxy.stage_start off`: no "Tribe (new)", Play enters the planet's only era as before.
Only Creature -> Tribe: the Tribe stage's end has no such request yet (a Civ start from a Tribe era is not offered).

## Verification
galaxy_server_check.py: PASS on the new server (all passed); FAIL on the server before (GALAXY_PROTO=8 with the old
build: 6 FAILED, the five STARTERA checks and the FETCH of the start crossing).
TITAN, the test galaxy with Samuth (Creature) only (out/galaxy/worlds_9336), QA 9 (a profile with every stage reached,
nothing remembered on Samuth):
PASS (`galaxy.stage_start` on): Play on Samuth opened "Choose a stage" with Creature and "Tribe (new)"; a real click on
"Tribe (new)": the server made "Samuth~1|Creature" and sent the Creature world (4698 KB); the game hosted it, 34 s
later asked the stage's end; the stage-end editor opened on the player's own creature; its check led to "Congratulations!
You are about to enter the Tribal stage", then the Tribe intro with the player's creature at its new village; ARRIVE:
"Samuth|Tribe: the party of 1 arrived from its crossing and hosts it"; 53 s later the server stored Samuth (Tribe)
(5706 KB) beside the untouched Samuth (Creature) world.
FAIL (`coop.fix galaxy.stage_start off`, the old behaviour, a fresh copy of the same galaxy): Play on Samuth opened no
choice and entered Samuth (Creature) at once (stage starts sent 0).
no-fail-run: Windows QA by hand (no lab on this PC); the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: built on feat/lineages-alpha (protocol 9), seen in game through to the stored Tribe era; switch off: no
  choice.
