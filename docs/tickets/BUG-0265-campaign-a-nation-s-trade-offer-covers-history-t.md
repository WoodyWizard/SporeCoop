---
id: BUG-0265
title: campaign: a nation's trade offer covers History, the civ -> space change never starts
status: fixed
kind: tooling
stage: civ
reported: 2026-10-29
source: lab 20261029-070811 campaign
role: 
versions: 
fixed_in: unreleased
branch: fix/w6-campaign
commits: 9f875d3
tests: campaign
signature:
docs:
related:
---

## Report
The host of campaign#1 (lab 20261029-070811, lane 2, out/lane2/campaign/20261029-070814) captured
every other city (the goal "Capture All Cities" checked) but a nation's trade offer ("I accept your
offer! Start shipments immediately!" / "I reject ...") stayed open in the middle of the screen. The
History clicks of kick_civ hit the dialog; civ -> space timed out after 20 min (finding `stuck`,
screenshot campaign_03_stuck_Alice.png).

## Root cause
Only the surrender talk was answered (CIV_TALK). The trade offer waits for an answer like the
surrender does.

## Fix
CIV_TALK also accepts a trade offer (as a player would), so the dialog closes before History.
switch: none (test tooling)

## Verification
no-fail-run: test tool change; the failing run is lab 20261029-070811 campaign#1; with the fix `SPORE_LANE=2 coop_campaign.py --from civ --to space --no-finale --no-jump --minutes 3` (run out/lane2/campaign/20261029-084905) reached Space after 378 s, all four players together, 0 findings.

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w6-campaign.
