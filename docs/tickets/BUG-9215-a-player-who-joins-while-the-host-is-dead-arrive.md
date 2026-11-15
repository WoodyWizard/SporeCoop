---
id: BUG-9215
title: A player who joins while the host is dead arrives dead at the host's spot
status: fixed
kind: bug
stage: Creature
reported: 2026-11-15
source: overnight stage tour 2026-11-15
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: cd602569
tests: manual Windows QA: the MSI hosts Vandh.spo dead (coop.giveup down, coop.giveup), TITAN joins
signature:
docs:
related:
---

## Report
MSI stage tour (Vandh.spo and Vantiegan.spo, 2026-11-15): when the host is on its death screen while a player joins, the player arrives at the host's spot with 0 health and sees "You were killed in battle". The OK button respawns them at their nest.

## Root cause
Inferred: the world shared to the joiner is saved while the host's avatar is dead; the joiner's own creature takes the avatar's place and state (same position, health 0). To check: hold the share while the host is dead, or respawn the joiner on arrival.

## Fix
- While the host is on its death screen in Creature, a joining player's world waits (the share hold of BUG-9210, 2 minutes at most) and the host is told every 30 s "<name> is joining: respawn so they can enter the world" (home_nests.cpp, switch creature.join_after_respawn).

## Verification
FAIL (before, MSI tour 2026-11-15, Vandh.spo): the joiner arrived dead at the host's spot ("You were killed in battle").
PASS (MSI host dead by coop.giveup, TITAN joins): "the share for player 1 waits" and the notice on the host; the joiner stayed in its menu; after the host's OK (real click on the MSI) the world went out and the joiner arrived alive (15 / 15).
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed (the world waits for the host to respawn), checked on TITAN + the MSI.
