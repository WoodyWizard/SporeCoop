---
id: BUG-1301
title: WP-S2e: Space: the host's ship registered under its own key shadows a guest's own ship of the same id
status: fixed
kind: bug
stage: Space
reported: 2026-11-19
source: plan
role: guest
versions: 
fixed_in: 0.7.16
branch: feat/w56-editors
commits: 86c18d41 0d159edb 6c1e3959
tests: space_own_ship_key, space_own_ship_key_off (coop_own_editors_robust.py ship)
signature: creations.cpp apply (world package kUfo)
docs: docs/re/creations.md#creations-own-editors-robust, docs/06-testing.md#testing-own-editors-robust
related: BUG-0278, BUG-0724
---

## Report
Lineages comparison (item 8): our guest registers the host's creation of every kind under its original key at
priority 2000 (creations.cpp apply), the ship too; with equal ids the guest's own empire would show the host's ship
(the fork's BUG-9071).

## Root cause
✔ (code) the guest's own empire flies its own key (cEmpire +0xB8, ufo_key); the world package of the host's ship under
that key answers above the guest's My Creations. Not run.

## Fix
`creations.own_ship_key`: a guest whose My Creations hold a ship of that key does not register the host's ship under
it; one registered before is dropped at the next 2 s check. The host's ship is drawn from the shared 0x7C copy
everywhere (puppet, fleet mirrors).

## Verification
The night twin space_own_ship_key_off PASSED (20261119-105127): the restarted guest never got the switch off
(BUG-1314), and the space fixture's ship is not in the host's My Creations, so its bundle had no records and nothing
was registered under its key on the guest (with the switch really off it still passed: 20261119-113824). The test now
puts the fixture's ship into the host's My Creations at its cold start (a base game model), checks that it answers
for the key on the guest, then the guest's own ship of that key. PASS: space_own_ship_key 20261119-115330 (`own ship
key: fix=1 unshadowed=1`). FAIL (twin): space_own_ship_key_off 20261119-115330 (the guest's game answers the host's
model 5DC76FCE for its own ship).

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: fix and its test written (switch, FAIL twin entry); the lab run waits for the orchestrator.
- 2026-11-19: fix/w57-tribe-keys: the test made to catch the bug (host ship with records, BUG-1314); PASS and twin FAIL; fixed.
- 2026-11-23: released in 0.7.16.
