---
id: BUG-0828
title: Space: a spice gift's refund is matched to an older successful gift of the same amount and dropped
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-space
commits: 3f6fb767
tests: space_gift_reload
signature:
docs: docs/re/space.md#space-s1-0713
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
K_GIFT_BACK is matched to the NEWEST unanswered gift of that partner, amount and spice (answers come in order), and a
new gift first drops the entries of another Space world (they are never answered into this one). Root cause
confirmed at runtime: with the switch off the answer of the second gift took game A's entry and its 10 spice stayed
owed for game A: game B's hold 20 instead of 30. Switch `coop.fix space.gift_match_newest off` (0.7.12).
`coop.space gift`: `pruned=`.

## Verification
`coop_space_gift_reload.py` (suite `space_gift_reload`): the host gives 10 (arrives), loads game B (a copy of the
slot made at the start), the guest `gift noroom 1`, the host gives 10 again.
* FAIL with `SPORE_FIX_OFF=space.gift_match_newest`: lab 20261112-035038 (game B's hold 20, not 30).
* PASS: lab 20261112-034615 (30 again, `back_paid=1 pruned=1`).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (the newest gift is matched, other worlds' entries pruned; switch space.gift_match_newest).
