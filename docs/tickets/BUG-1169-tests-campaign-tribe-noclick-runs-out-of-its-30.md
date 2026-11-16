---
id: BUG-1169
title: Tests: campaign_tribe_noclick runs out of its 30 min Tribe advance at 4/5 (old flake, 0.7.14 too)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-15
source: B1 impact run 20261115-133237
role: 
versions: 0.7.14, v0.7.14.1, rel/0.7.15
fixed_in: unreleased
branch: feat/w51-npc
commits: 08fcba55
tests: campaign_tribe_noclick, campaign_tribe_end
signature:
docs: docs/06-testing.md#testing-w51-npc
related: BUG-1091, BUG-1073, BUG-1101
---

## Report
campaign_tribe_noclick (BUG-1091's test: CAMPAIGN_PLANT_NOCLICK=18, a village the bots' clicks never reach for 18 min)
failed "[stuck] the stage change tribe -> civ did not finish in 30 min" in the B1 run (20261115-133237) and on
rel/0.7.15 5206247b (20261115-145821 lane 3).

## Root cause
An old flake, not a regression: rel/0.7.14 failed it 2 of 3 (20261113-231423 goal bar 5/5, 20261114-005006 0/5), fix/w49-camp2
2 of 3; v0.7.14.1 PASS and rel/0.7.15 FAIL side by side (20261115-154901, lanes 3 and 2: 4/5 at 30 min). The passing runs
filled the bar in 22.5-25.6 min, so the 30 min limit leaves 4-7 min for the raids that fail for other reasons. One of
them is the driver's (observed, rel/0.7.15 lane 2 16:08-16:13): a raid whose host click came late (the view did not reach
the hut, no member selected: BUG-1091's class) had its hut cut to 40 at 16:13:20 and was dropped at its 300 s, 28 s
later, with the hut at 39 of 750; the village was raided from the start again later.

## Fix
coop_campaign.py: a raid gets 120 s after its hut's first cut (RAID_AFTER_CUT) when that comes late in its 300 s;
`CAMPAIGN_OLD_RAID_CUT=1`: dropped at 300 s. The planted 18 min and the 30 min limit stay (the twin CAMPAIGN_OLD_RAID_SKIP=1
must still run out). Test-only.

## Verification
FAIL: 20261115-154901 (rel/0.7.15, lane 2: the dropped raid at 39/750, 4/5 at 30 min).
PASS: 20261115-203638 (lane 2, the final tree; this run had no late cut, so it does not show the fix at work: the
flake's other causes, the bots' clicks, stay BUG-1091's class).

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): compared with v0.7.14.1, the driver's late-cut drop fixed.
