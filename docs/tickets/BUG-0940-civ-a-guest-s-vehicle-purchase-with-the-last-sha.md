---
id: BUG-0940
title: Civ: a guest's vehicle purchase with the last shared 1000 made no vehicle on the host and no refusal was counted (campaign, four players buying at once)
status: open
kind: bug
stage: Civilization
reported: 2026-11-11
source: final check 2 of 0.7.12, campaign (lab 20261111-090547), play_civ PROBLEM line
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0613
---

## Report
rel/0.7.12 lab 20261111-090547, campaign (four players), Civ play, 09:56: `PROBLEM Bob bought a vehicle in city 0
(button 062D6902) and the host made none (money 1000.0 -> 0.0)` (play_civ.py `buy`: no vehicle of Bob's on the host
6 s after the click, the city still Bob's or shared, the host's `buys_refused` not raised). A PROBLEM line of the
bot, not a finding: the campaign did not fail on it. In no other campaign log on this machine.

## Root cause
Not established. Observed: the four bots bought at once: the shared money went 3000 -> 2000 -> 0 between 09:56:25 and
09:56:39 (three purchases of 1000 made on the host), the host's `rejected=0` and `buys_refused=0`, and Bob's game
counted `cmds_sent=0` at 09:56:32. The host's money was 1000 when the bot read it and 0 after its wait, so another
player's purchase took the last 1000 around Bob's click. Inferred: Bob's own game refused the click (its shared money
was 0 by then: the game's own "not enough money"), nothing was sent, nothing was lost: a race of the bots that the
check does not know (it treats only "the city changed hands" and "refused by the host" as no purchase).
Not related to BUG-0933 (Space money held on a guest): Civ money was in sync on all four games.

## Fix
None yet. Next step: play_civ `buy` reads the guest's own money and button state right before the click and counts
"the shared money ran out meanwhile" (the host's money below the price after the wait and no command sent,
`cmds_sent` of the guest unchanged) as `buy_broke`, not as a PROBLEM.

## Verification

## History
- 2026-11-11: opened.
