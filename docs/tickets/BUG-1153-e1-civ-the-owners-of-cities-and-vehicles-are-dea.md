---
id: BUG-1153
title: E1: Civ: the owners of cities and vehicles are dealt anew every session (captured, bought and given cities and a guest's army go to other players)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-store
commits: 53f904d2
tests: civ_owners_session, store_tests (make test)
signature:
docs: docs/re/civ.md#civ-owner-claims
related: BUG-0950, BUG-0875, BUG-0031, BUG-1149
switch: store.civ_owners
---

## Report
Playability audit (0.7.15, tribe_civ.md item 1): every new session dealt the cities anew in turns (captured, bought and given cities to other players), vehicles went to the owner of the nearest city; a guest that left lost its cities and army for good.

## Root cause
Observed in code: civ.cpp g_city_owner / g_hv in memory only; civ.owners_kept (BUG-0950) by name, memory only.

## Fix
Claims (owner_claims.h, civ.cpp): every player city and vehicle has its owner's stable key; its player gets it whenever here, the host holds it for an absent player; written by position into the store (world record `civ.owners`) and paired with the objects after the load. `coop.fix store.civ_owners off`: dealt as before. Also an autosave milestone at a capture (BUG-0864).

## Verification
civ_owners_session (civ_npc: the host buys one NPC city, the guest another and a vehicle; session_restart):
FAIL (SPORE_FIX_OFF=store.civ_owners): out/lab/20261115-133100 (the host's bought city became the guest's, the guest's
vehicle the host's); PASS: out/lab/20261115-132804, out/lab/20261115-134728. unit: store_tests test_claims.
Not run in the lab: a guest leaving and rejoining within a session (the same code path: a change of the roster).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-store.
