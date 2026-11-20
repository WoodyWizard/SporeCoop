---
id: BUG-9281
title: Galaxy planet: the Tribe food share and its panel run between different parties
status: open
kind: bug
stage: tribe
reported: 2026-11-19
source: TITAN test 2026-11-19 (BUG-9278)
role: all
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
TITAN 2026-11-19 22:47 (images/stage4_join_tribe_guest_view.png): QA 8 and QA 9, two different parties on
Samuth (Tribe), both show the panel "Village food Host 49.55 Guest/You 15.00": the co-op food share (decision 2P) runs
between strangers.

## Root cause
Inferred: the Tribe food share and its panel count every founder seat of the session; on a galaxy planet they
should count the own party's seats only (as difficulty and relations do since BUG-9266 and BUG-9267).

## Fix
Not yet.

## Verification
Not yet.

## History
- 2026-11-19: opened (seen in BUG-9278's join test).
