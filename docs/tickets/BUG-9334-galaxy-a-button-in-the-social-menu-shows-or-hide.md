---
id: BUG-9334
title: Galaxy: a button in the Social Menu shows or hides players in other stages (as /eras on|off)
status: fixed
kind: feature
stage: all
reported: 2026-11-22
source: roadmap-alpha-now (BUG-9294 left a button for /eras)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 82aac47b
tests: tools/qa_win (manual, 2026-11-22): TITAN's test galaxy, QA 9 on Samuth (Creature), coop.party social / lclick, coop.fix galaxy.eras_button off/on
signature: 
docs: docs/12-server-and-players.md#server-eras-button
related: BUG-9294, BUG-9287
---

## Report
BUG-9294 (decision 7I A) made seeing players of a planet's other stages the player's own choice, but only by chat
(`/eras on|off`) or coop.ini; the roadmap (#roadmap-alpha-now) listed "a button for /eras in the co-op window" as left.

## Root cause
Not built yet (a feature).

## Fix
On a galaxy server the Social Menu (F8, the Social button) ends with a row "Players in other stages" and a button that
says what is set now: gold "Shown" or grey "Hidden". A press switches it, the same as `/eras on|off`
(era_visitors::set_other_eras: coop.ini [galaxy] other_eras, the server told, the notice shown). Not on a galaxy
server: no row. Switch `coop.fix galaxy.eras_button off`: no row (chat only, as before).
Found on the way: the Social Menu's buttons kept their old art after a look change (the game draws a window's new
drawable only once the window changes, as set_look already knew): so_chip_look now hides and shows the window, which
also fixes the row buttons' hover art.
Tests: `coop.party social [toggle]` prints the menu's state, the button's place (client pixels), caption and art.

## Verification
TITAN, the test galaxy (build/galaxy_server.exe, port 47900), QA 9 entered Samuth (Creature), the Social Menu open:
PASS (`galaxy.eras_button` on): the row at (908 236 982 260); a real click (posted to the game window) switched
other_eras 0 -> 1 -> 0, the caption Hidden -> Shown -> Hidden, the art grey -> gold -> grey (screenshots), the
"Other eras hidden" notice shown.
FAIL (`coop.fix galaxy.eras_button off`, the old behaviour): no row (button 0 0 0 0), a click at the same place
changed nothing (other_eras stayed 0).
no-fail-run: Windows QA by hand (no lab on this PC); the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: built on feat/lineages-alpha and seen in game (both directions, switch off: no row).
