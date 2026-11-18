---
id: BUG-9082
title: Guest native Buy City interaction does not complete
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-07
source: Targeted checkpoint purchase test
role: guest
versions: duo22-dev plus BUG-9081
fixed_in: 0.7.3.6-duo23-dev
branch: feat/lineages-civ-combat
commits: 68cf3ad0
tests: tools/tests/duo_civ_city_purchase.py
signature: Buy City enabled at full guest trade progress; click produces no negotiation or transfer
docs: docs/re/civ.md
related: BUG-9081 BUG-9074
---

## Reproduction

Isolated DuoQAPlayabilityBuyH starts at TradeE's earned route checkpoint.
Prepare native purchase progress 100 for founder 2231 and sufficient shared
funds. BUG-9081 correctly replicates the route, progress and pricing map.
Select the guest trader and left-click the rival city. Native Buy City button
037E95BB is enabled. Clicking it closes the menu but starts no negotiation;
the city remains with 2232 on both peers. Both report zero faults.

Evidence: build/local/buy-city-probe.log, buy-city-after-click.log and
qa-stage-5.png. Eligibility was fixture setup, not claimed as earned trade.
The earlier right-click probe opened no city menu and is not the failure.

## Investigation

The native command passes the host political ID to CanBuyCity and GetBuyPrice.
The rival's valid buyer is the guest, so the native eligibility gate rejects
the click. Scoped caller corrections open the ordinary negotiation.

The UI-only correction exposed the second failure: an accepted 32,000 offer
deducts shared money, but guest h_Capture suppresses the local transfer and no
host capture is requested. Reproduced in bug9082-transaction-before.log.

## Fix

Keep native negotiation and its five offers. Correct only the native command's
eligibility and price callers. Forward completion before native guest payment;
the host validates connected founder, current world/epoch, live city identity,
native purchase eligibility, fair-offer threshold and shared affordability.
Native BuyByCiv on the host performs the one payment and founder-owned capture.
Refusal forwards the native meter reset without charging. Queued work stores
values, not city pointers, and executes outside the render frame.

## Verification

PASS bug9082-purchase-j.log: actual Buy City button, 32,000 offer, acceptance
reply, guest-faction city on both peers, exactly one shared payment and one
team economic-history increment. Both games had zero faults and quit normally.
The final guarded build also passes bug9082-purchase-k3.log (native Nevermind
cancellation keeps eligibility and spends nothing) and bug9082-purchase-l2.log
(native 4,000 offer refusal clears both meters, then accepted purchase).
The latter also passes real reconnect into a new native slot: purchased city,
building/turret ownership and a single history credit persist. Both games
quit normally with zero faults; no new crash report appeared.
bug9082-purchase-control.log disables forwarding after opening negotiation
and FAILS the same automated ownership assertion.

Two early cancellation test failures were harness issues: the actual reply
is Nevermind..., and caption search includes hidden controls. The first
low-offer probe used a duplicated window ID which resolved to the text pane;
it clicked no offer. The regression now checks ancestor visibility and uses
actual reply captions/positions. These are not gameplay failures.

Windows tests: bug9082-unit-unrestricted.log ALL OK. The identical executable's
sandboxed run could not reach its local HTTP fixture (zero requests); elevated
rerun reached the fixture and passed.

## History

2026-11-07: Fixed in 68cf3ad0. Included in the private Duo 23 ZIP;
duo23-installer.log passes install/uninstall/reinstall, original loader
restoration and save/settings preservation.
