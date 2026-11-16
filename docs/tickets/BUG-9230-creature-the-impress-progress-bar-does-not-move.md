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
Observed 2026-11-16 (TITAN QA pair, Dorger.spo copy, guest `coop.social goto/act` on a neutral proxy, screenshots out/shots/r66/meter_*.png): the guest's meter opens (the empty arc with the face at the top of the screen), but its bar never fills: the progress shows only as the mod's feed lines ("Impressing: 0%", "Answer it: 2/3"). `coop.social meter`: bar 0's ends lo and hi (+0x134 / +0x13C) are both 0.0 while the meter is open, so SocialMeter_SetBar 0xE31990 (pos = lerp(lo, hi, frac)) writes 0 whatever the progress (frac_set 0.01, 0.02, 0.33 seen). Inferred: the native session (SocialPropose 0xDA4360 / its tick) sets those ends or the fill some other way; the guest's meter_run never does. Not found yet: what sets them. The console cannot start a native session on the host (`coop.social post` on a neutral NPC: no engagement), so a native meter to compare needs a real social click. Note: the Steam SporeApp.exe is encrypted on disk; code is read from a running game (`mem.read`, scratchpad dis.sh).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not reproduced: TITAN two games (Dorger.spo, guest coop.social goto/act on NPC 1): the guest's meter opened, moved twice (bars=2) and closed with a win (impressed 0 -> 1, win_msgs=1). The guest's own log of the play test is needed.

## History
- 2026-11-15: opened.
- 2026-11-16: seen by the owner on the QA guest MSI (62-dev): "the on screen UI didn't fill up with the pink bar ... a fallback using notifications instead of the native UI". MSI's `coop.social meter`: opened=1 bars=1 frac_set=0.33 closed=1 win_msgs=0, and its log "Impressing: 33%" (the feed line social.cpp K_PROGRESS adds). So the meter opened and SocialMeter_SetBar(0, 0.33) was called, but the owner saw no pink fill: the bar set once per progress message is not drawn (the native tick moves the bars every frame, 0xDA4950 -> 0xD9B9C0). Not fixed yet.
