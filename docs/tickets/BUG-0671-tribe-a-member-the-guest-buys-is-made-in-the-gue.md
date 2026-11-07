---
id: BUG-0671
title: Tribe: a member the guest buys is made in the guest's game only and removed again
status: open
kind: bug
stage: Tribe
reported: 2026-11-07
source: found in fix/w46-tribe-civ (BUG-0645 analysis)
role: guest
versions: 0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0645
---

## Report
Lab lane 5 2026-11-07 (tribe fixture, the guest's player tribe at the host's level 2, cap 12, 8 members): the guest's game made a member at the hut (the stage's baby-born function 0xCD74A0 called on the guest, as its hut does): its tribe had it for a moment, the mod's mirror reconcile removed it (`removed` 1 -> 2), the host's game never got it (12 members before and after). docs/09-coop-design.md says new members go to whoever bought them; the host splits every new member of its own game in turns.

## Root cause
Observed: no guest -> host command for a new member (tribe.cpp has CMD_ACTION, CMD_WALK, CMD_GIVE); the host's split (host_tick, g_split_for) gives new members in turns. ? Which UI path a player uses to add a member (the mate cursor 'cursor-mate' on the hut: an order the guest relays, or the hut's own spawn) was not traced; if it is an order, the host makes the member and the turn split decides the owner.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
