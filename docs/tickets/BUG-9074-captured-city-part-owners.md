---
id: BUG-9074
title: A guest founder captures a rival city but its parts retain the rival on the guest
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-06
source: Lineages reproduction of upstream 0.7.8 BUG-0593
role: host guest
versions: duo17-dev
fixed_in: unreleased
branch: feat/lineages-civ-combat
commits: bc6bfec1 39cb1737
tests: tools/tests/duo_civ_capture_owners.py
signature: Guest city 2231 retains buildings of faction 2232
docs: docs/DUO.md
related: BUG-9056 BUG-9073
---

On two local Clyth games the guest founder captured a rival. Both cities
reported faction 2231. All three buildings kept 2232 on the guest until the
verified native SetPartsOwner routine assigned the conquering civilization.

Lineages uses the existing per-city snapshot to report whether the host's
buildings and wall turrets all belong to that city. The guest waits for its
city ownership to match and for the local planner to release the city before
applying the native repair. Each side uses its own native city and nation
pointers. NPC economy and diplomacy are separate from this repair.

Upstream implementation and test: [SporeCoop BUG-0593](https://github.com/WoodyWizard/SporeCoop/commit/51228919).

## Verification

FAIL before the port in lineages-capture-assessment.log. PASS with the
port in lineages-capture-b.log and the ZIP DLL in lineages-capture-c.log.

The guest-founder test transferred all three buildings and one wall turret
to 2231 on both peers. In a fresh pair the ZIP-installed DLL transferred
the same parts to 800021CA for the host founder. Both runs reported zero
native faults. A second fixture completed native religious conversion with
repeated preaching updates. Native BuyByCiv then transferred a city and
deducted exactly 2,500 from the shared treasury, with both peers agreeing
on the buildings and turret. These tests exercise native game routines.
Issuing those religious and economic actions through the player's controls
still needs qualification. The incoming capture during an open planner
is tracked separately as BUG-9075.
