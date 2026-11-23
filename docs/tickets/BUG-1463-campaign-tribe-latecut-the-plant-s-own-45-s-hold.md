---
id: BUG-1463
title: campaign_tribe_latecut: the plant's own 45 s hold ate the raid's 120 s after the first cut; dropped at 403 s with the hut at 1 of 400
status: open
kind: test
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-tests
commits: def13b5f
tests: tools/tests/coop_campaign.py (campaign_tribe_latecut)
signature:
docs:
related: BUG-1169
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): campaign_tribe_latecut "FAIL driver tribe/advance: the raid on village 1 with the planted late cut (first cut at 283 s) was dropped at 403 s before its hut fell (hut 1152B700 health 1/400 ...; BUG-1169)".

## Root cause
Test (the plant): BUG-1169's driver gives a raid RAID_AFTER_CUT 120 s after its hut's first cut; the plant itself lets no blow land for PLANT_CUT_HOLD 45 s after its cut, so the raiders had 75 s; the hut was at 1 of 400 at the drop and the village fell 3 s later (host `village cinematic 5418B038 plays on the host` at 02:48:20, the drop at 02:48:17). The campaign reached tribe -> civ in 25 min. Not the mod.

## Fix
coop_campaign.py: for the planted raid the limit is counted from the end of the plant's hold (+ PLANT_CUT_HOLD); the old rule (CAMPAIGN_OLD_RAID_CUT=1, the twin) keeps 300 s.

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run campaign_tribe_latecut --lane N --retry 0 --no-build` PASS (the twin with CAMPAIGN_OLD_RAID_CUT=1 still fails: its suite entry). no-fail-run: a test fix; FAIL before 20261123-023643.

## History
- 2026-11-23: opened (fix/w65-tests).
