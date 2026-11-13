---
id: BUG-1034
title: Space: a gift, a colony sale or an accepted offer that reaches a rejoined guest before its kept hold is restored is lost
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-4
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-trade-before-restore
related: BUG-0678, BUG-0832, BUG-0196
---

## Report
Code audit for 0.7.14, finding SPC-4. Not seen at runtime, found by code reading.
A guest that rejoins loads the host's world, whose hold is the host's copy; for up to 20 s (until K_INV_KEPT) its kept hold is about to replace it (BUG-0678). A tool gift (K_TOOL), a sale to its colony (K_SELL) or an offer it accepts (answer_offer) in that window is put into the hold that is then replaced: the receiver loses the item, and the sender was told it was taken (no refund; for an offer the buyer also paid).

## Root cause
Observed in code (main efd2e0ba), confidence high for the path, medium that apply_inv drops the item (inferred from its job: it writes the kept hold over the current one):
* space_trade.cpp:827-846 K_TOOL: add_charges at once when in Space, K_TOOL_ACK with got = what was added.
* space_trade.cpp:870-886 K_SELL: add_spice at once, K_SELL_ACK.
* space_trade.cpp:713-750 answer_offer: add_spice and add_money(-total) at once.
* None asks inv_restore_pending() (space_trade.cpp:1425-1433); pay_owed (:773) and pay_owed_sales (:1665) do (BUG-0678 / BUG-0832).
* apply_inv (space_trade.cpp:1157, called at :1500 and :1595 when K_INV_KEPT comes) replaces the hold.

## Fix idea
While inv_restore_pending(): K_TOOL and K_SELL answer with got = 0 and a reason "not yet" (the sender keeps / gets back its items, the existing refund path), or queue them and handle them after apply_inv; answer_offer keeps the offer until the restore is done (the button greyed). Switch: `coop.fix space.trade_wait_restore off`.

## How to test
Extend coop_space_items_rejoin.py: right after the guest's rejoin (a test hold on K_INV_KEPT, like g_hold) the host gives it a tool and sells it spice; after the restore assert the guest game's inventory has the gift (or the host's game got it back). Main: FAIL (the item is in neither game).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
