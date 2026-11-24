---
id: BUG-1531
title: Civ -> Space: check that a guest of a shared nation gets its own spaceship editor after the vote (PR #219's question)
status: fixed
kind: bug
stage: Civ, Space
reported: 2026-11-23
source: plan, PR #219
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.99
branch: rel/0.7.99
commits: ebe0e45a, 3d23c6ca, 46dab65a
tests: civ_ship_editor, civ_ship_editor_off
signature:
docs:
related: 
switch: civ.guest_ship_editor
---

## Report
PR #219's question: does a shared-nation guest get its own spaceship editor at Civ -> Space? Lab check on rel/0.7.17 in WP-M1's Civ -> Space segment; if not, #219's recipe (level 4 + GameCiv_ShowPlanetWon + PlanetWonPause on the guest's game after the vote). Closes as not-reproduced when the editor opens.

## Root cause
Open question (PR #219): whether a guest of the shared nation gets its own spaceship editor (0x96B24187) after the Civ
-> Space vote. Not answered by reading: the night run's campaign segment decides.

## Fix
None yet. The check: coop_campaign.py `ship_editor_check` notes for every guest whether its own log shows the change to
the editor after the Civ advance (always, in every campaign's notes) and makes it a finding with
CAMPAIGN_GUEST_SHIP_EDITOR=1 (suite civ_own_money_space, which also runs it with civ.own_money on). If the editor does
not open: #219's recipe (level 4 + GameCiv_ShowPlanetWon 0xCF8F60 + PlanetWonPause 0xCF7680 on the guest's game
after the vote) is the next step. Closes as not-reproduced when the editor opens.

## Verification
PASS: out/lab/20261124-155436 (civ_ship_editor, 0 faults: guest opens own spaceship editor after Civ->Space vote).
FAIL: out/lab/20261124-161419 (civ_ship_editor_off: guest never opened spaceship editor).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-M1), branch feat/w68-wallet.
- 2026-11-23: the lab check written on feat/w68-wallet (coop_campaign.py ship_editor_check, suite civ_own_money_space); waits for the night run.
- 2026-11-24: released in 0.7.99.
