---
id: BUG-9274
title: A loading screen stays over the first-creature editor for 2 minutes when a new player joins from inside a game
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: Live audit 2026-11-19 (MSI pair)
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: fc3c27a9
tests: tools/qa_win/qa.py (manual: MSI pair, a guest without a creature joins from inside its own game)
signature: ui: the reload loading screen ended (time limit) after 120.0 s ... | Editor
docs:
related: BUG-9195, BUG-9202, BUG-9263
---

## Report
Live audit 2026-11-19 (MSI pair): a guest with no creature of its own in the host's Creature world joined while it
played a game of its own (Cell; a second run from a Creature game). The "Getting the latest world" loading screen
stayed over the first-creature editor (BUG-9263) for two minutes; the player sees a loading screen, not the editor.
A join from the main menu was not affected (BUG-9263's test).

## Root cause
Observed (guest log): the reload cover (BUG-9195) starts in the stage when the host's world arrives; the game then
goes galaxy menu -> Editor (the pregame editor, BUG-9208). The reload cover has no end for "an editor is open": it
waits for "playing a stage" and ended only by its limit: "ui: the reload loading screen ended (time limit) after
120.0 s". The join cover had "other mode" for this; the reload cover did not.

## Fix
Commit fc3c27a9: coop.cpp on_overlay: any of the three covers ends when the game is in an editor mode ("an editor"). After
the editor the join cover (menu, the host plays) takes over by itself for the load. Switch:
`coop.fix ui.cover_ends_in_editor off` (the cover stays until its time limit).

## Verification
no-fail-run: two games, the game's editor and screen. Manual 2026-11-19 on the MSI pair (host Samuth, guest profile
MSIJoiner with no creature there). FAIL on 65-dev + a150cb3e (no fix): the guest joined from its Cell game, screenshot
120 s later shows the cover over the editor, log "the reload loading screen ended (time limit) after 120.0 s". PASS
with the fix: the guest joined from its own Creature game (Vandh), log "the reload loading screen ended (an editor)
after 5.0 s", the screenshot 40 s after the join shows the start editor (45).

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on feat/lineages-47 (fc3c27a9).
