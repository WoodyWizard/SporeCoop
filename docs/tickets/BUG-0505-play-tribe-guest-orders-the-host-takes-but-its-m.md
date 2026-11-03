---
id: BUG-0505
title: play_tribe: guest orders the host takes but its members do not act on (12 idle of 28), the host's bot gives too few orders (minimap look_far 24)
status: open
kind: test
stage: Tribe
reported: 2026-11-03
source: fix/w40-play final check, lab 20261103-160342
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0481
---

## Report
Seen once the citizens' sync check passed (BUG-0481): play_tribe#1 of out/lab/20261103-160342 (fix/w40-play,
lane 6) failed `host: gave orders through real clicks (8)` and `guest: the members act on the orders ({'ok':
12, 'lost': 0, 'idle': 12, 'none': 4})`. The guest's orders reached the host (`cmds_done=29 rejected=0`), but the
ordered members did not move or hit there ("no movement": gather fruittree, the epic n11 in the epic phase, a
walk after a gift). The host's bot: look_far=24 (its minimap look did not land within 30 m), no_selection=18,
84 actions in 10 min. Replay: `--seed 655950227`.

## Root cause
Not analysed. Inferred: the idle orders cluster in the second half (gather / epic) after member gifts
(`gave=1/2`): the ordered member may be the one just given; the host's look_far suggests its view calibration
went off after the epic arrived.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
