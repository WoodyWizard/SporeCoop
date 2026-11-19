---
id: BUG-9272
title: Galaxy: a game the server forgot (server restart, a 20 s freeze while saving) never goes back on its planet
status: fixed
kind: bug
stage: galaxy
reported: 2026-11-19
source: QA run 2026-11-19 (live checks)
role: host, guest
versions: 0.7.14.1-lineages64-dev
fixed_in: unreleased
branch: feat/lineages-alpha
commits: COMMIT
tests: manual on TITAN QA 8 + 9 (tools/qa_win/galaxy.ps1 stop / start with both on Samuth)
signature: "The galaxy server lost this game: connecting again." then no planet
docs: docs/13-alpha-review-2026-11-18.md#review-live
related: BUG-9268
---

## Report
QA run 2026-11-19, the checks the 2026-11-18 verification never ran:
* The host entered another planet (Clyth) after leaving Samuth: leaving the Creature stage for the menu froze the game
  for 24 s (the save at 10 FPS); the server timed it out ("The galaxy server lost this game: connecting again"); the
  game reconnected and loaded Clyth, but on no planet of the galaxy (its role gone).
* The galaxy server restarted with a host and a guest on Samuth: both games connected again and stayed on no planet;
  Samuth had no host until someone entered it again.

## Root cause
✔ On REFUSED "not connected" the client dropped its planet (right: the server forgot it) and nothing entered it again.
A first fix that entered again showed a second cause: the old planet session, left in the same moment, ended a few
frames after the new ROLE, and the tick read that end as leaving the new planet (LEAVE: the server forgot the planet
again while both games thought they hosted it).

## Fix
galaxy_client.cpp: the planet is remembered when the server forgets the game; once welcomed again the game enters it
(a former guest 4 s later, so its old host takes it first). A host still playing that planet (or loading it) hosts the
world it plays, no reload; a guest joins its host as any join. The old session's end is no leave (g_resume_hold).
Switch `coop.fix galaxy.resume`.

## Verification
no-fail-run: no lab test (galaxy planets have none yet). Manual, TITAN, QA 8 hosting Samuth, QA 9 its guest, the
galaxy server stopped for 25 s and started again:
* fix on (PASS): "Going back on Samuth...", "You host Samuth again." on QA 8 (resumes 1, hosts 1), "Joining Samuth
  again..." on QA 9; afterwards QA 8 the host of Samuth with 2 players in its session, QA 9 its guest, both in Creature.
* fix off (FAIL, the same games, a second restart): both on no planet.
* (the first try of the fix, before g_resume_hold: both games said "host", the server log showed each one's ENTER
  followed by a LEAVE the same second.)

## History
- 2026-11-19: opened and fixed.
