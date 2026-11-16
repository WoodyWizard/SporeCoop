---
id: BUG-9230
title: Creature: the impress progress bar does not move on the guest's screen
status: open
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15
role: guest
versions: 0.7.14.1-lineages58-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "The progress bar when impressing isn't moving on BigTexas' screen."

## Root cause
Not known yet.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not reproduced: TITAN two games (Dorger.spo, guest coop.social goto/act on NPC 1): the guest's meter opened, moved twice (bars=2) and closed with a win (impressed 0 -> 1, win_msgs=1). The guest's own log of the play test is needed.

## History
- 2026-11-15: opened.
- 2026-11-16: seen by the owner on the QA guest MSI (62-dev): "the on screen UI didn't fill up with the pink bar ... a fallback using notifications instead of the native UI". MSI's `coop.social meter`: opened=1 bars=1 frac_set=0.33 closed=1 win_msgs=0, and its log "Impressing: 33%" (the feed line social.cpp K_PROGRESS adds). So the meter opened and SocialMeter_SetBar(0, 0.33) was called, but the owner saw no pink fill: the bar set once per progress message is not drawn (the native tick moves the bars every frame, 0xDA4950 -> 0xD9B9C0). Not fixed yet.
