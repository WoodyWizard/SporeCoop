---
id: BUG-9033
title: Guest village cannot issue native food gifts
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Two-founder Tribal social command audit
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_gift.py
signature: village allowlist rejects CitizenAction9
docs: docs/re/tribe.md
related: BUG-9032
---

The native guest input relay reaches the host, but the two-village allowlist
rejects food gifts. Add a validated native order and a visible F9 control.
Verify food payment, delivery and the native hostile-to-neutral relationship
effect on both peers. A food deduction alone does not prove delivery.

Probe m2-gift-bounded.log delivers ten food through native action9 after
placing the actor at the destination before ordering. Both instances report
zero faults. The neutral recipient gets no new relationship event, as expected
from DBB7D0's negative-relationship check. A hostile fixture is still needed.

F9 Gift food now selects another tribe's hut and relays native action9 with
its owning tribe as target. The host checks actor ownership/adulthood, live
recipient, native food cost and available funds. No food or friendship is
granted by the command handler. Ordered relationship event records reconcile
through bounded host snapshots using native ResetRelationship and map insertion.

PASS m2-gift-native-f.log and m2-gift-ui-g.log: negative controls reproduce the
old command rejection and missing relationship replication. Native node0462855E
pays ten food, delivers it and changes the recipient's opinion from -4 to0.
Both peers agree after snapshots resume. The F9 button and hut picker pass;
empty stores reject orders, species is retained, both games have zero faults.

Investigation failures retained: m2-gift-final.log and m2-rel-probe.log exposed
a missing packet dispatch connection, fixed before the passing runs.
m2-gift-final-b/c used -0.5, inside Spore's neutral range; delivery succeeded
but peace was correctly unnecessary. Native thresholds are -7,-3,3,7 in this
fixture. m2-gift-native-e put the courier beside enemies during a furious -8
setup and lost it to combat. The final fixture uses disliked -4 and keeps the
courier home until the funded order. No production combat behavior was changed.
