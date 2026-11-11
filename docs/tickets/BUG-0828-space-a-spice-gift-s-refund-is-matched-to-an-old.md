---
id: BUG-0828
title: Space: a spice gift's refund is matched to an older successful gift of the same amount and dropped
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0226
---

## Report
A player gives spice to a partner whose hold is full; the spice should come back but is lost. Found by reading the code.

## Root cause
Observed in the code, confidence high. spacecoop.cpp:1110-1113: every gift stays in g_gifts_out (up to 16); only a failure is answered. :1245 find_if takes the oldest entry with the same to, n and key; the button always gives 10, so entries match often. That old entry's epoch makes other_world true and gift_owed_tick returns Drop. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Match the newest entry, and prune entries of older world epochs (or tag each gift with a sequence number echoed by the receiver).

## How to test
`coop.space give 10` (it succeeds), reload the world in the session, on the receiver `coop.space gift noroom 1`, `coop.space give 10` again: FAIL = back_dropped rises and the spice is not returned; PASS = it is returned.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
