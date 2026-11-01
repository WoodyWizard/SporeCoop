---
id: BUG-0290
title: tests: the campaign bot answers the same Civ talk dialog for 20 min and civ -> space times out
status: fixed
kind: test
stage: Civ
reported: 2026-10-29
source: final campaign lab 20261029-203821 campaign_space#1 lane 7
role: 
versions: 
fixed_in: 0.7.3.2
branch: fix/w9-tests
commits: 2db65c9
tests: campaign, campaign_space
signature:
docs:
related:
---

## Report
Lab 20261029-203821 campaign_space#1 (lane 7, run out/lane7/campaign/20261029-215906): in civ/advance the bot logged "Alice: talk answered" / "Carol: talk answered" every 12 s for 20 min, the host never reached Space and the test jumped with the fixture. A dialog the CIV_TALK list in tools/tests/coop_campaign.py does not close (see BUG-0265 for the earlier case). A test problem, not a mod bug as far as seen.

## Root cause
Observed in the screenshots of out/lane7/campaign/20261029-215906: on the host a captured city's specialty box (Economic / Military) was open ON TOP of the Blue Nation's surrender talk. `service()` looked for the talk's answer first; that answer stays "shown" (coop.ui find) under the box, so the bot clicked it every 12 s and the box took the clicks; the Military answer of the box was never reached. On the guests the talk's answer area was covered by the "galactic adventures" panel (clicks lost there too).

## Fix
Test tool only (coop_campaign.py `service`): the specialty box is answered first; a talk whose answer is still up after 3 presses gets Esc (every second try; a pause menu that Esc opened is closed with Resume Game). Logged as "talk closed with Esc" / "talk stuck: Esc did not close it".

## Verification
FAIL: lab 20261029-203821 campaign_space#1 (20 min of "talk answered"). PASS: lab 20261030-004738 campaign_space (lane 7): Civ -> Space in 358 s, the specialty box answered 5 times ("dialog Military"), the talk once, no finding. The order of the two dialogs is a race of the shortcut captures, not reproducible on demand.

## History
- 2026-10-29: opened.
- 2026-10-30: fix/w9-tests: the specialty box first, Esc for a talk that does not close.
- 2026-10-30: fix/w9-tests: campaign_space passed (lab 20261030-004738).
- 2026-11-01: released in 0.7.3.2.
