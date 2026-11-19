---
id: BUG-1301
title: WP-S2e: Space: the host's ship registered under its own key shadows a guest's own ship of the same id
status: open
kind: bug
stage: Space
reported: 2026-11-19
source: plan
role: guest
versions: 
fixed_in:
branch: feat/w56-editors
commits: 86c18d41
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
Not run yet: the lab waits for the orchestrator's word (2026-11-19, the CPU was busy with a foreign task). Ready:
`SPORE_DETECTOR=20 python3 tools/lab.py run space_own_ship_key space_own_ship_key_off --lane 6 --lane 7 --retry 0 --no-build`; the twin must
FAIL, the test PASS (run ids go here).

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: fix and its test written (switch, FAIL twin entry); the lab run waits for the orchestrator.
