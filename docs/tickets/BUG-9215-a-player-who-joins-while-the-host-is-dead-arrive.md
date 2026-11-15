---
id: BUG-9215
title: A player who joins while the host is dead arrives dead at the host's spot
status: open
kind: bug
stage: Creature
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
MSI stage tour (Vandh.spo and Vantiegan.spo, 2026-11-15): when the host is on its death screen while a player joins, the player arrives at the host's spot with 0 health and sees "You were killed in battle". The OK button respawns them at their nest.

## Root cause
Inferred: the world shared to the joiner is saved while the host's avatar is dead; the joiner's own creature takes the avatar's place and state (same position, health 0). To check: hold the share while the host is dead, or respawn the joiner on arrival.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: open, not fixed yet

## History
- 2026-11-15: opened.
