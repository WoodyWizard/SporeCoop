---
id: BUG-9032
title: Guest village has no accepted native eating order
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Tribal command audit during gift investigation
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_eat.py
signature: village allowlist rejects CitizenAction0
docs: docs/re/tribe.md
related: BUG-9020 BUG-9031
---

The guest can gather food but cannot issue native Eat orders to its own food
mat through the two-village filter. Add an owned, selected-citizen order and
verify native consumption/recovery on both peers. Do not replace eating with
manual health or hunger grants.

F9 Eat now relays native action0 to the selected citizens' own mat. The host
checks ownership, living adult state, the tribe/mat link and available food.
Switch: tribe.eat_orders.

FAIL/PASS m2-eat-final.log: disabling the fix rejects F9 Eat despite food.
Enabling it enters native eating node0461300F, consumes the guest village's
food and restores actual citizen health. Both games agree on food/health and
retain the original species. An empty food store rejects a further request.
Both games report zero native faults. Earlier m2-eat-native.log passes the
consumption/recovery check without the additional empty-store case.
