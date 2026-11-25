---
id: BUG-9355
title: Galaxy menu: a server planet whose creature this game lacks shows no creature
status: fixed
kind: bug
stage: galaxy
reported: 2026-11-23
source: qa
role: 
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 7f3d2bd0
tests: tools/tests/galaxy_server_check.py (the owner in the list, GWS3); tools/qa_win (manual, the MSI, a fresh profile)
signature:
docs: docs/12-server-and-players.md#server-menu-gaps
related: BUG-9287, BUG-9290, BUG-9295, BUG-9337
---

## Report
The galaxy menu's planet panel showed "?" for a server planet whose creature this game does not have (the planet list
sends the species key only; 2026-11-17 note in docs/12-server-and-players.md#server-galaxy-menu-built). Found on the
way (2026-11-23, the MSI): a key this game has can be another player's creature of the same id (creation ids count up
per profile, BUG-9290), so the panel could show a creature of this player's own.

## Root cause
The list had no owner for the key, so the menu could only look the key up in its own game. The galaxy's creation
store (BUG-9287) keeps creations by owner and key, but nothing put the host's creature there unless it visited
another era.

## Fix
Server: a stored world keeps its uploader's identity (file GWS3; GWS1 and GWS2 still load, owner 0); PLANETS ends
with one u64 owner per planet after the S10 trailer (older games stop reading before it). Game: each upload puts the
host's creature into the store (galaxy_client up_facts); the menu takes another player's creature only from the store
(galaxy_crt::local_key, installed under a key free here), shows "?" while it comes or when the store has none, never a
local creature of the same key, and builds the menu again once it is here (only on the planets, BUG-9358). Switch
`coop.fix galaxy.menu_pictures off`: the local key only, as before.

## Verification
- Server, tools/tests/galaxy_server_check.py: FAIL against the server at the commit before (out/oldsrv build of HEAD
  b19f1a6e..5b1afc6b's main.cpp): "the list names the player whose upload a planet's creature is" and "the stored world
  keeps its uploader (GWS3)" FAILED (out/lab_runs/bug9355_server_fail.txt); PASS with the fix, all passed
  (out/lab_runs/bug9355_server_pass.txt).
- In game, the MSI (TITAN's galaxy server, 15 planets), a fresh profile with no creatures: switch off, Samuth's and
  Testplanet01's panels "?"; switch on, Samuth's panel showed its host's creature from the store (images/
  bug9355_menu_picture_off_on.png; "galaxy creations: asked 2, got 1" 40 s after the switch, Testplanet01's still
  coming). An earlier fresh profile got both ("pictures from the store 2").
no-fail-run: the in-game FAIL is the switch off on the same build (the old path is the switch's off).

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha (7f3d2bd0), server check FAIL/PASS, seen in game on the MSI.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
