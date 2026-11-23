---
id: BUG-1491
title: Space trade: an offer over the partner's offer limit gets no decline; the offered spice stays held until the partner leaves (SPC-17)
status: open
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w77-space
commits: a6fe8f9a, 37d9240d
tests: space_offer_full, space_offer_full_off, space_tests (make test)
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0542, BUG-0196
switch: trade.offer_answer
---

## Report
External static audit of 0.7.15.5, item SPC-17 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high: space_trade.cpp K_OFFER on a receiver with kMaxOffers (16) offers waiting
returned with no answer; the seller's g_out entry came back only when the partner left or at the session end. Its
"Withdraw" sends K_OFFER_CANCEL, and answer_offer() for an id it does not hold sent nothing either, while the button
was gone (cancel_sent): the spice stayed held (lost if the game crashed before the refund). Rivals only.

## Fix
* The receiver declines (K_OFFER_ACK got 0, reason 0) an offer it can not hold and a Withdraw of an offer it does not
  hold (`declined_full`, `cancel_unknown`); an ACK for an offer the seller no longer waits for is ignored there.
* The seller takes back an offer no answer came for within the receiver's own offer life plus 30 s
  (`spacelogic::offer_unanswered`, `unanswered`): a receiver keeps an offer at most kOfferLife and then declines it, so
  this only happens when the receiver never held it (a 0.7.16.1 receiver with a full list).
* Switch `coop.fix trade.offer_answer off`: 0.7.16.1. Test seam: `coop.space trade offerlimit <n>`. No wire change (the
  ACK exists in space 1.2).

## Verification
open, waiting for the lab run.
`coop_space_offer_full.py` (suite `space_offer_full`, twin `space_offer_full_off`): Rivals; the host holds no offers
(`trade offerlimit 0`); the guest's offer must be declined within 10 s, its spice back in its cargo (the game's cargo
count). Expected: PASS with the fix; FAIL with SPORE_FIX_OFF=trade.offer_answer.
pending night run: space_offer_full space_offer_full_off (1 lane, about 8 min).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
