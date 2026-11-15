---
id: BUG-1154
title: E1: Tribe: the owners of tribe members are dealt anew on every join, leave, game over and session (bought children, gifts and the guest's chief lost)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-store
commits: 53f904d2
tests: tribe_owners_session, store_tests (make test)
signature:
docs: docs/re/tribe.md#tribe-owner-claims
related: BUG-0671, BUG-1149
switch: store.tribe_owners
---

## Report
Playability audit (0.7.15, tribe_civ.md item 3): every join, leave, game over and new session re-dealt all members of the player tribe in turns: a bought child, gifts and the guest's chief went to other players.

## Root cause
Observed in code: tribe.cpp host_tick re-deals g_hm on a changed roster; g_hm cleared by the session end and every world change.

## Fix
Claims (owner_claims.h, tribe.cpp): every member has its owner's stable key (flags: born for a player, a gift); its player gets it whenever here, the host holds it for an absent player, unclaimed members are dealt in turns, a player here with no member gets an even share; written by position into the store (world record `tribe.owners`) and paired after a load (also the game over's reload). `coop.fix store.tribe_owners off`: dealt in turns as before. Also an autosave milestone at an epic kill (BUG-0864).

## Verification
tribe_owners_session (tribe fixture: the host gives all but one of its members to the guest; session_restart):
FAIL (SPORE_FIX_OFF=store.tribe_owners): out/lab/20261115-134340 (dealt in turns: the gift gone); PASS:
out/lab/20261115-133846, out/lab/20261115-135042. unit: store_tests test_claims.
Not run in the lab: a game over's reload, a join of a third player (the even share).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-store.
