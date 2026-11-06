---
id: BUG-9031
title: Separate-village guest healing orders are rejected
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: baseline command audit
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_heal.py
signature: action 5 absent from village command allowlist
docs: docs/re/tribe.md
related: BUG-9020 BUG-9022
---

Upstream relays native healing orders, but the two-village host filter accepts
only walk, gather, attack, raid and equip. Verify native healer-tool execution
on an injured partner, target/actor authorization, and the other game's health.
Use native healing rather than manually granting health to implement the order.

Implemented F9 Heal through the existing reliable native-order relay. The host
requires an owned living adult with tool9 and a living, injured founder citizen
outside the rescue state. Self/enemy/full-health and unequipped requests are
refused. The target picker considers injured friendly citizens. Switch:
tribe.heal_orders.

FAIL m2-heal-control.log: native tool equipped, host rejects action5.
PASS m2-heal-disk.log: F9 native healing and both games' health.
PASS m2-heal-final-retry.log: final target picker, unequipped/enemy rejection,
native healer BT0562C0FB, actual patient health increase and guest replication.
Both processes report zero faults.

Fixture correction: m2-heal-final.log passed authorization but failed to keep
the injury during camera setup. Native Eat/Sleep can resume after WalkTo ends;
the patient had recovered without the requested heal. Interrupt that task
before creating the test injury, and put the healer near the current patient.
Continue requiring the healer's native BT node so ambient recovery cannot pass.
