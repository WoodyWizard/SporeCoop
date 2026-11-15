---
id: BUG-9216
title: Party card: the host's picture is blank on a guest when the host's creature has no picture in its My Creations
status: open
kind: bug
stage: All
reported: 2026-11-15
source: overnight stage tour 2026-11-15
role: 
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related:
---

## Report
Overnight stage tour: on the MSI (guest) the host's picture in the Party card stayed blank in Cell, Tribe and Civ (shown in Creature); the guest's picture showed on the host everywhere.

## Root cause
creations.cpp portrait_tick sends the host's picture only when its creature (or the played species) has a PNG in its own EditorSaves; the QA host profiles were assembled from copied saves and have none. Probably a test-profile gap; a player's own creature normally has its picture. To check with a host who made its creature itself.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: open, not fixed yet

## History
- 2026-11-15: opened.
