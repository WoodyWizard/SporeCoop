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
commits: 08fcba55, 7bfcaa33, d8b058e1, dbadec84
tests: campaign_tribe_noclick, campaign_tribe_end, campaign_tribe_latecut
signature:
docs: docs/06-testing.md#testing-w51-npc, docs/06-testing.md#testing-w51-nest
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

## Verification (fix/w51-nest): the fix at work
The case is planted (campaign_tribe_latecut, `CAMPAIGN_PLANT_LATE_CUT=270`, the Tribe stage to Civ): the first raid with
the host's click takes no blow before 270 s (each blow undone), its hut is cut at the first pass after 270 s and no
blow lands for 45 s more (the observed raid: cut, dropped 28 s later at 39 of 750); that raid must fell its hut, a drop
is a `driver` finding. The campaign gates and the 30 min limit are unchanged.
* FAIL (twin `CAMPAIGN_OLD_RAID_CUT=1`, lane 3): 20261115-222253: cut at 288 s, "dropped at 300 s before its hut fell
  (health 40/400)", the only FAIL line (the run reached tribe -> civ in 25 min).
* PASS (lane 2): 20261115-223337: cut at 285 s, the hut fell at 316 s of the raid (past the old 300 s), campaign OK;
  20261115-225558 (lane 2: cut at 287 s, the hut fell at 345 s, campaign OK).
* Two tries of the plant before it was right (not counted): without the hold the hut fell 38 s after the cut before
  the 300 s check came (20261115-221100, stopped), without the forced cut no blow came after 280 s (20261115-220048).

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): compared with v0.7.14.1, the driver's late-cut drop fixed.
- 2026-11-15 (fix/w51-nest): the driver fix shown at work with a planted late cut (FAIL/PASS twin above); campaign_tribe_latecut in the suite.
