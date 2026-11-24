---
id: BUG-1262
title: Adventure: the partner's puppet has this game's own captain's look, not the partner's captain (the captain's species is made per game)
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-18
source: fix/w53-advorn: found while fixing BUG-1254
role: both
versions: <=0.7.15
fixed_in: 0.7.17
branch: feat/w74-captain
commits: 596a872b, 6ac83cfb, 0bd01c0d
tests: adventure_tutorial, adventure_tutorial_partner_off
signature:
docs: docs/re/adventure.md#adventure-captain-copy, docs/re/creations.md#creations-captain-0717, docs/04-netcode.md#netcode-creation-captains
related: BUG-1254, BUG-0793, BUG-1321
switch: adventure.partner_captain
---

## Report
Lab 20261118-130637 (adventure_tutorial, the BUG-1254 fix on): each game shows its partner in the tutorial adventure as a
second copy of its own captain (key frames 02_played: two pink captains on the guest, two green ones on the host), at the
right size.

## Root cause
* Observed (runtime): the captains' species keys are made per game (host 2927BAE4, guest 2927BAE2 in one run; 2927B941 /
  2927B940 in another; the same key 2927BD73 for two different captains in lab 20261118-131729): the partner's key does
  not name its captain in this game, so the BUG-1254 fix (adventure.puppet_captain) uses this game's own captain as the
  stand-in.
* Inferred: showing the partner's captain needs its creation (the captain's creature records) sent like the Creature
  stage's creations (creations.cpp bundles) and a species built from it here; part of co-op adventures (BUG-0793).

## Fix
creations_captain.cpp + modes.cpp scenario_spawn (`coop.fix adventure.partner_captain`, off = 0.7.16.1: the stand-in).
Static RE (Ghidra): the galaxy menu's adventure copies the picked captain into a new creation of the profile
(0x6462E0: a key from cIDGenerator, EditorSaves.package), Space missions take cEmpire +0xC4: the captain's records are
in a file package (the review's #9). Each player in an adventure (kScenario only, no captain traffic in Space) sends its
captain's model records, creaturedata and property list as MSG_CREATION kind 11 (to the host if it reads creations 1.2;
the host keeps it for later joiners and relays it to guests of 1.2); the read is retried after 2, 4, 8, 16, 30 s when
the package is not written yet. Every game registers a partner's as a 0x7A...... copy (its property list too: the
CaptainProps_Get readers, review #7), asks the bake outside the frame every 0.5 s and lets the puppet use it only at
bake state 1 (never on a time-out: BUG-1321's white model, review #1); the puppet is made again from tick in the frame
(review #3), always in the avatar's herd (BUG-1254). The switch also gates the receive and relay path (review #6). Idea
from 4e7490e6 (fix/agy-open-bugs), redone.

## Verification
open, waiting for the lab run (no game run on this branch by day). Built, `make test` passes (tests/core_tests.cpp captain_rules). pending night run: adventure_tutorial (PASS: check 6, the
puppet's body (modelhash of its species here) is the partner's own captain's body on the partner's game) and its twin
adventure_tutorial_partner_off (SPORE_FIX_OFF=adventure.partner_captain, must FAIL: this game's captain stands in; when
both games play captains of the same body the twin cannot fail, the test prints it); the BUG-1254 twin (puppet_captain off)
must still FAIL.
0.7.17 release lab: PASS: adventure_tutorial (lab 20261124-112022); FAIL (fix off): adventure_tutorial (lab 20261124-020506).

## History
- 2026-11-18: opened (fix/w53-advorn, found while fixing BUG-1254).
- 2026-11-23: feat/w74-captain (WP-K1): redone after the review (only on a real bake, rebuilt in the frame, kScenario only, receive path gated, the copy has its property list, adventure captains shown to be in EditorSaves); lab test and twin written.
- 2026-11-24: released in 0.7.17.
