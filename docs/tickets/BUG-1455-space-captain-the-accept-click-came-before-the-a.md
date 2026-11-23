---
id: BUG-1455
title: space_captain: the Accept click came before the adventure mission screen was drawn (its control already shown to coop.ui where); the click missed, the open screen then blocked the rank badge click
status: fixed
kind: test
stage: Space
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: unreleased
branch: fix/w65-tests
commits: 04d96951
tests: tools/tests/coop_space_captain.py (space_captain)
signature:
docs:
related: BUG-1111, BUG-0304, BUG-1278
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, lane 2, cold): space_captain FAIL "guest: Accept ran the
captain's items palette", "Accept opened the captain screen", "after Accept the captain screen and the mission screen
are closed (OK)", "the rank badge opened the captain screen". No caught fault, no crash, both games answered.

## Root cause
Test (UI timing), not the mod. The test waits until `coop.ui where` finds the mission screen's Accept (a shown window),
takes captain_mission.png and clicks it 0.3 s later. That shot (01:06:45) shows no mission screen at all; the next shot
(captain_accepted.png, 01:06:58) shows it fully drawn with Decline / Accept, untouched: the screen was shown to the UI
tree before it was drawn and took input, the click went to nothing (CaptainItems_Palette 0 hits). The open screen
then lay over the game and the rank badge's click did nothing either (the 4th FAIL follows from the first). History:
on rel/main 1 of 7 (this run); the other failure (20261119-094350, fix/w55-captain) was the hidden Accept copy, fixed.
No captain code changed in 0.7.16 (only the 0.7.15.3 merge): not a regression, not the #202 / #203 crash path (that
path did not even run).

## Fix
coop_space_captain.py accept_route: the Accept's place must hold for 1.5 s before the click; a click the screen did
not take (no palette hit in 4 s, no captain screen) is given again, up to 3 times, printed. The checks are unchanged
(Accept must still run the palette and open the captain screen; the badge must open it).

## Verification
Was waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run space_captain --repeat 4 --lane N --retry 0
--no-build` PASS 4 of 4. no-fail-run: a test fix; the FAIL before is 20261122-194210 space_captain.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): space_captain PASS 3 of 3 in 20261123-020239, and in 20261123-023643. Closed.

## History
- 2026-11-23: opened; fix/w65-tests: the click waits for a settled Accept and is given again when not taken.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
