---
id: BUG-0739
title: campaign: a stage change seen in the loop counted as not reached when the host's own reload after Creature -> Tribe was in the galaxy menu at the second check
status: open
kind: test
stage: Creature
reported: 2026-11-08
source: fix/w46-e1-features (campaign_ct 20261108-040927)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0332, BUG-0315
---

## Report
campaign_ct on lane 4 (out/lab/20261108-040927) reported "[stuck] the stage change creature -> tribe did not finish in
10 min" after 2.3 min, although the host's game entered the Tribe stage at 04:19:12 and every guest followed.

## Root cause
Observed (host log): Creature -> Tribe 04:19:12, then the host's own reload of BUG-0332 (Tribe -> galaxy menu 04:19:18
-> Loading -> Tribe 04:19:23, a second one 04:19:28 .. 04:19:34). coop_campaign.advance() left its loop on seeing
Tribe, then joined the answer threads (up to 30 s) and asked game.mode again: at 04:19:20 the host was in the galaxy
menu, so the reached change counted as not reached. A race of the tool; 0.7.10's run (20261107-141346) happened to
ask while the host was in Tribe.

## Fix
tools/tests/coop_campaign.py advance(): the stage seen in the loop counts (`reached`), the second check only applies
when the loop ran out of time.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
