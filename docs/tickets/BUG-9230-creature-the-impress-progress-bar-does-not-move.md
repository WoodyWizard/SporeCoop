---
id: BUG-9230
title: Creature: the impress progress bar does not move on the guest's screen
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15
role: guest
versions: 0.7.14.1-lineages58-dev, 0.7.14.1-lineages62-dev
fixed_in: 0.7.14.1-lineages63-dev
branch: feat/lineages-47
commits:
tests: manual Windows QA (TITAN guest, coop.social meterfill, fix off / on screenshots)
signature:
docs: docs/re/creature.md#creature-meter-bars
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "The progress bar when impressing isn't moving on BigTexas' screen."

## Root cause
Observed 2026-11-16 (TITAN QA pair, Dorger.spo copy, guest `coop.social goto/act` on a neutral proxy, screenshots out/shots/r66/meter_*.png): the guest's meter opens (the empty arc with the face at the top of the screen), but its bar never fills: the progress shows only as the mod's feed lines ("Impressing: 0%", "Answer it: 2/3"). `coop.social meter`: bar 0's ends lo and hi (+0x134 / +0x13C) are both 0.0 while the meter is open, so SocialMeter_SetBar 0xE31990 (pos = lerp(lo, hi, frac)) writes 0 whatever the progress (frac_set 0.01, 0.02, 0.33 seen). Inferred: the native session (SocialPropose 0xDA4360 / its tick) sets those ends or the fill some other way; the guest's meter_run never does. Not found yet: what sets them. The console cannot start a native session on the host (`coop.social post` on a neutral NPC: no engagement), so a native meter to compare needs a real social click. Note: the Steam SporeApp.exe is encrypted on disk; code is read from a running game (`mem.read`, scratchpad dis.sh).

Then found (2026-11-16, a native meter on the TITAN QA host read while the owner impressed, the code read from the running Steam game): the meter has two bars growing towards each other from the arc's ends (+0x3C 0.626, +0x40 2.516; each bar's full length 0.945, full when they meet), and what is DRAWN is each bar's list of finished steps (one 0x14-byte entry per round: its end on the arc, 1.0, -FLT_MAX, 0, 0.01), not the bar position. A guest has no native session, so the lists stayed empty and lo / hi stayed 0: nothing drawn, whatever SetBar got. docs/re/creature.md#creature-meter-bars.

## Fix
- social.meter_fill (social.cpp meter_fill): at every progress message (and the win) both bars get their step's ends (lo = where it stands, hi = progress x the bar's length), their lists of finished steps are rebuilt for the rounds done, SetBar moves them, and the bars' window (0x562BD40 in the meter's layout) is redrawn (its vt 0x90, as the native meter tick does when +0x24 is set). `coop.fix social.meter_fill off`: 62-dev (bar 0 only, nothing drawn).
- Tests: `coop.social meterfill <done> <rounds> [close]` opens the meter and fills it as a progress message does (follows the switch).

## Verification
FAIL off / PASS on (TITAN QA guest instance 8, 2026-11-16, `coop.social meterfill 2 3`): off: bars 0.000 (lo / pos / hi 0), the arc empty (out/shots/r66/mf2_offon.png top); on: both bars 0.630, two pink steps from each end (bottom). 1/3, 2/3, 3/3 on: out/shots/r66/mf_arcs.png (full: the arc glows). A real social game with the fix: bars 0.472 then 0.945 with the win (values; the screen was covered by a loading picture and deaths in that world).
Earlier no-fail-run: not reproduced: TITAN two games (Dorger.spo, guest coop.social goto/act on NPC 1): the guest's meter opened, moved twice (bars=2) and closed with a win (impressed 0 -> 1, win_msgs=1). The guest's own log of the play test is needed.

## History
- 2026-11-15: opened.
- 2026-11-16: seen by the owner on the QA guest MSI (62-dev): "the on screen UI didn't fill up with the pink bar ... a fallback using notifications instead of the native UI". MSI's `coop.social meter`: opened=1 bars=1 frac_set=0.33 closed=1 win_msgs=0, and its log "Impressing: 33%" (the feed line social.cpp K_PROGRESS adds). So the meter opened and SocialMeter_SetBar(0, 0.33) was called, but the owner saw no pink fill: the bar set once per progress message is not drawn (the native tick moves the bars every frame, 0xDA4950 -> 0xD9B9C0). Not fixed yet.
- 2026-11-16: cause found (empty step lists, bar ends 0); fixed (social.meter_fill); FAIL off / PASS on with screenshots.
- 2026-11-16: owner: "If the impress bar is working, we don't need the notifications anymore": the feed lines "Impressing: n%" and "Answer it: i/n" only when no meter shows that social game (social.cpp meter_shows; with social.meter_fill on).
- 2026-11-16: released in 0.7.14.1-lineages63-dev.
