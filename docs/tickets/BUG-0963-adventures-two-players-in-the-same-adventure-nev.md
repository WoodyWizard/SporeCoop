---
id: BUG-0963
title: Adventures: two players in the same adventure never see each other's captain (each play has its own scenario key, so the place ids differ)
status: duplicate
kind: bug
stage: Adventure
reported: 2026-11-12
source: fix/w48-small lab 2026-11-12 (adventure_duo)
role: both
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/adventure.md#adventure-two-captains
related: BUG-0845 BUG-0793
---

## Report
Found while testing BUG-0845, no player report. Two players in a session who both play the same adventure (the tutorial from the galaxy menu) never see each other: each game's trace says "partner in another world" and no puppet is made, although docs/07-modes.md says adventures show the partner as an echo puppet.

## Root cause
Observed (runtime, lab lane 2, out/lab/20261112-122759, -123202, -124436): the Adventure adapter's place id (modes.cpp adventure_place) is made of the scenario resource key (cScenarioMode +0x74 -> +0x10: instance +8, group +0x10), and every play of an adventure has its own key (the tutorial: 6FCBCE96, E6F78525, 288BE48A, group 408A0000; one key was the host's in one run and the guest's in another). So the two games' places never match. Inferred: the key is the play's own copy of the adventure (its save), not the adventure itself.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## Fix idea
Not part of G1 (safety). For the echo puppets or adventure co-op (BUG-0793) the place must name the adventure, not the play: the adventure's own key (the fork uses (scenarioMarkerID, hostRuntimeID) for actors; the adventure's id may be in the play's resource or in the request at 0x15ACCC8, docs/re/adventure.md#adventure-ga-fork). Before that: BUG-0845's crash was not reproduced with forced places, but run adventure_duo without `coop.adventure place` once the ids match.

## How to test
adventure_duo with --own-places: both games in the tutorial adventure; the host's `coop.status` must show a `puppet pos=` line for the guest (today: none, "partner in another world").

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-small): opened while testing BUG-0845.
- 2026-11-12 (rel/0.7.13, orchestrator): closed as a duplicate of BUG-0793 (co-op Galactic Adventures, planned with Rivals 0.8, docs/11-rivals.md#rivals-fork-feature-ideas). Seeing the partner's captain in an adventure is part of that feature, not a defect of the current mod: there is no adventure co-op yet, and the 0.7.13 safety work (BUG-0789, BUG-0845, BUG-0854) keeps a session safe while a player plays an adventure. The place-id finding and the fix idea above stay as input for BUG-0793.
