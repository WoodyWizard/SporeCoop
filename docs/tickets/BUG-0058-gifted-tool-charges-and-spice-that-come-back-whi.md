---
id: BUG-0058
title: gifted tool charges and spice that come back while you are not in Space are lost
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#36
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-space
commits: fd814d3
tests: space_trade_refund
signature:
docs: docs/re/space.md#space-ship-state-chunks
related: #36
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/36 (full text, discussion).

When a tool gift, a spice sale or a rivals offer comes back to the sender (the partner had no room, declined, or left), `space_trade.cpp` puts the items back into the cargo hold right away, even when the sender is not in the Space stage at that moment. Outside Space that refund fails ("no room, lost"), or it writes into an inventory that may no longer be valid. A session that ends while the sender is outside Space drops every pending item without trying. Severity: data loss (the player's tool charges or spice); possibly a crash if the inventory pointer is stale outside Space (not verified).

### Where
- `refund`, no Space check: [space_trade.cpp#L596-L605](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L596-L605)
- partner left: [#L801-L814](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L801-L814) (tick runs whenever connected)
- replies that refund: K_TOOL_ACK [#L691-L705](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L691-L705), K_SELL_ACK [#L729-L754](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L729-L754), K_OFFER_ACK [#L768-L791](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L768-L791)
- session end: refunds only in Space, otherwise drops: [#L829-L839](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L829-L839)
- where the items go: `inventory()` [#L119-L123](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L119-L123), `add_spice` [#L331-L344](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L331-L344), `add_charges` [#L397-L422](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_trade.cpp#L397-L422)

### How to reproduce
Two players, allies mode (for gifts and sales) or rivals mode (for offers), Space:
1. Guest: fill the cargo hold so a gift cannot fit, or simply plan to decline.
2. Host: "Give 5" of a bomb tool to the guest (`coop.space trade give <inst> 5`), and right after that leave the Space mode (enter an editor, or quit to the menu while the session keeps running).
3. The guest's reply (`back = 5`) arrives, or the guest leaves the session: the host's log says the charges came back "lost". Back in Space the host's tool has 5 charges fewer (`coop.space trade tools`).

Variant: the session ends while the sender is outside Space, and the pending items are gone with no notice.

### Expected
Items on their way stay owed to the sender until it is back in Space, then they are refunded once.

## Root cause
**What happens**

1. "Give" or "Sell / Offer" takes the charges or spice out of the sender's cargo at once (`take_charges` / `take_spice`) and keeps an `Out` entry until the partner answers.
2. The answer can come at any time, also while the sender is in another game mode or loading. `on_message` and the "partner left" loop in `tick` call `refund(copy, n, ...)` with no `spaceext::in_space()` check.
3. `refund` calls `add_charges` or `add_spice`, which read the inventory through `cSimulatorSpaceGame +0x40`.
   - If that is null outside Space, both return 0 and the notice says "no room, lost".
   - If the pointer is still set, the items are written into an inventory the mod does not know to be valid.
   In both cases the `Out` entry is already erased, so nothing is retried.
4. `on_session_end` checks `in_space()`, but only to skip the refund: outside Space every pending gift, sale and offer is cleared without coming back.

The same file handles this in two ways: the receiver side checks `in_space()` before adding (`K_TOOL`, `K_SELL`, `answer_offer`), the sender's refunds do not.

### Confidence
CONFIRMED by reading the full path: no Space check before the refunds, and the entry is erased first. PLAUSIBLE: the exact outcome outside Space (null inventory, so "lost", versus a stale inventory pointer). Whether `cSimulatorSpaceGame` (global 0x16DD0DC, docs/re/space.md) is cleared when Space is left was not verified.

## Fix
Fixed on fix/w2-space (fd814d3). `refund` puts what comes back into an owed list when the feature is not in Space or has no inventory; `tick` (which runs in every mode and also after the session ended) pays the list once back in Space. `on_session_end` refunds the pending gifts / sales / offers through the same path instead of dropping them outside Space. A load in between: each `Out` remembers the player's cEmpire and the save count when it was taken (`spaceext::saves()`, counted by the allies save hook); an owed item is paid when the cEmpire is the same or a save was made since, else it is dropped (the load brought back a game from before the take, which still has it). `coop.space trade refundfix 0` restores the old immediate refund. Runtime finding: in the galaxy menu `cSimulatorSpaceGame +0x40` (the inventory) is null (`trade spices` prints "free cargo slots -1"), so the old refund there was "no room, lost".

Suggested in the review:

- Keep a list of owed refunds `{kind, key, n, name}`. `refund` adds to it when `!spaceext::in_space() || !inventory()`; otherwise it refunds as now. `tick` pays the list out once back in Space.
- `on_session_end` moves the pending `g_out` entries into that list instead of dropping them when not in Space.
- Keep the old behaviour behind `coop.fix trade_refund_later off`.

## Verification
Done (2026-10-28, lane 6): `coop_space_trade_refund.py` (suite `space_trade_refund`). The guest holds its answer (`trade hold 1`), the host gives 5 bomb charges, acts as outside Space (`trade away 1`: the feature sees no inventory, as in the galaxy menu), the guest leaves, the host comes back (`away 0`). `--without-fix` FAILS (7 charges, refunds=1 lost; /tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w2-space/refund--without-fix3213.log), with the fix PASS twice (12 charges, owed_paid=1, once). The outside-Space state is forced by the switch (a real editor entry in that window cannot be timed); the null inventory it simulates was checked in the real galaxy menu.

Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_trade.py`, new step "refund outside Space":
1. Two lanes. Fill the guest's cargo so the gift does not fit.
2. The host gives 5 charges and at once leaves Space for a non-Space mode (the editor, or the menu with the session kept).
3. The host comes back to Space.
4. Assert the host's own inventory: the tool's charges (`coop.space trade all`, ammo field read from the game's cSpaceToolData) equal the value before the gift.

This must FAIL on main and with `coop.fix trade_refund_later off`, and PASS with the fix. No crash report on either game.

### Related
- docs/known-issues.md "Space trade: open ends" (a gift on its way when the own session ends comes back and can duplicate; this issue is the opposite case, where it never comes back).
- docs/re/space.md 14.1, 14.3.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #36.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-space (fd814d3): owed refunds paid back in Space; test space_trade_refund FAIL without / PASS with.
- 2026-10-28: released in 0.7.3.0.
