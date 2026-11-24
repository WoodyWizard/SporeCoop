---
id: BUG-9368
title: Galaxy: a party ending the Civ stage stays in the planet's Civ era (its Space saves refused)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-24
source: deep playtest 2026-11-24
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: e91040a7
tests: tools/qa_win (manual, TITAN host + MSI guest, a party's Civ era on a galaxy planet): the Civ stage's end
signature:
docs: docs/12-server-and-players.md#server-party-civ-end
related: BUG-9278 BUG-9292 BUG-9346
---

## Report
Deep playtest 2026-11-24 (a party of two in a galaxy planet's Civ era, the planet won): the leader went through
History's check, the vote, the spaceship editor and into Space inside the planet's Civ era. The galaxy server
refused every save ("planet 'Talas|Civ': refused a world of another stage (Space)"; the game: "Could not save Talas
(Civ) to the galaxy: this world is in the Space stage, not Civ", asked again every 34 s), the planet got no Space
era, the party's Space progress was never stored.

## Root cause
Observed (code): the party's crossing (coop.cpp h_Submit -> galaxy::stage_end_request) is asked for the Creature
stage's end (BUG-9278) and for the Cell and Tribe ends (BUG-9292) only. The Civ -> Space spacecraft request
(native_first_spacecraft) went straight into the session's vote. BUG-9346 built the Space start of one player from
the galaxy menu, not the party's own end of Civ.

## Fix
coop.cpp h_Submit: the Civ -> Space spacecraft request of a galaxy era's party leader is held for the party's
crossing too; the crossing arrives as the planet's Space era once its host plays Space (galaxy_client's ARRIVE, as
for every crossing). Switch `coop.fix galaxy.party_stage_end_civ off`: the Civ era's session as before.

## Verification
- FAIL (the build before, 16:53): the server log lines above, no 'Talas|Space'.
- PASS (the fix, 17:23, the Civ era's world of 16:38 put back on the test server, eight more cities made with
  SpawnNextCity and taken with `coop.civ capture`, the partner's capital left alone): the server "planet 'Talas|Civ':
  ... moves its party (2) on to the next stage: crossing 'Talas~1|Civ'", the vote with both ready, the spaceship
  editor, "planet 'Talas|Space': the party of 1 'Titan-NoShare-' arrived from its crossing and hosts it" (17:24:49),
  stored (3799 KB) once the ship was in its solar system; the partner designed its own ship and both ships flew in
  the solar system (images/deep_ships_together_host.png, images/deep_ships_together_guest.png).
- The first form of the fix also held the partner's own spacecraft request after the crossing had arrived ("Only your
  party's leader moves your party on to the next stage", 17:30:44): the hold now needs the planet's era to be Civ
  still. On that run the partner's request went through with the switch off on its game (what the corrected test
  does); the corrected build itself was not run through a launch again.
- Seen on that run, not followed: after the refused request and the later editor the partner's game kept the pause
  reason 0x4BF38A6 (lifted with `coop.pauses sub`); a refusal and a later editor in the other stages is not checked.
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the run of the build before.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the deep playtest).
