---
id: BUG-1485
title: Space trade: a sale's or an offer's payment arriving after a world change is added to the wrong world's money (SPC-6)
status: fixed
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w77-space
commits: a6fe8f9a, 37d9240d
tests: space_own_rivals, space_own_rivals_off_sale, space_tests (make test)
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0058, BUG-0225, BUG-0832, BUG-1483
switch: trade.sale_pay_world
---

## Report
External static audit of 0.7.15.5, item SPC-6 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high: space_trade.cpp K_SELL_ACK and K_OFFER_ACK paid with `add_money(total)` on
GetPlayerEmpire at once, with no here() or world check, while a refund of the same Out was owed (refund(): outside
Space or another world epoch). K_OFFER_ACK comes when the buyer clicks, up to kOfferLife (120 s) later: a seller that
was loading then (no cEmpire) lost the payment; one that had loaded another Space world (a load, a Space save of its
own) got the co-op world's money there. The session-end payment of a delivered sale (BUG-0542 / BUG-0832) had the
same gap for another world.

## Fix
`owe_payment()`: unless `spacelogic::sale_pays_now(here(), owed_id(), the Out's world epoch, now)`, the payment goes
to g_owed_sales (BUG-0832's path, paid by pay_owed_sales in the world of the tag: Pay / Keep / Drop by
spaceext::owed_action). Also at the session end for another world. Switch `coop.fix trade.sale_pay_world off`:
0.7.16.1. Status: `coop.space trade` pay_owed=. Test seam: `coop.space trade offerlife <s>` (both games).

## Verification
open, waiting for the lab run.
`coop_space_own_rivals.py` (suite `space_own_rivals`, twin `space_own_rivals_off_sale`): the guest's offer is bought by
the host after the guest loaded a Space save of its own: the own save's money must stay as it was and the payment be
owed. Expected: PASS with the fix; FAIL with SPORE_FIX_OFF=trade.sale_pay_world (+ the price in the own save).
pending night run: space_own_rivals space_own_rivals_off_sale (1 lane, about 16 min).
0.7.17 release lab: PASS: space_own_rivals (lab 20261124-020506); FAIL (fix off): space_own_rivals (lab 20261123-125807).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
- 2026-11-24: released in 0.7.17.
