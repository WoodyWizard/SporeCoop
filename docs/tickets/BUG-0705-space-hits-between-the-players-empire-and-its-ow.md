---
id: BUG-0705
title: Space: hits between the players' empire and its own escorts were relayed as enemy hits (MSG_UFO_HIT, MSG_UFO_DAMAGE)
status: wontfix
kind: bug
stage: Space
reported: 2026-11-07
source: K2a audit of BUG-0420 (0.7.11)
role: 
versions: 
fixed_in:
branch: fix/w46-k2a-state
commits: 6b49ee3d
tests: space_friendly (coop_space_friendly.py)
signature:
docs: docs/07-modes.md#modes-message-classes
related: BUG-0420
---

## Report
K2a audit of the class BUG-0420 (no player report): the host forwarded every NPC ship's hit on a guest's puppet to the guest (MSG_UFO_DAMAGE), the host's own escorts' too, and applied every guest shot on a mirror (MSG_UFO_HIT), on the host's escort too (squadron.cpp h_TakeDamage / on_message).

## Root cause
Observed (lab 20261107-222158, `SPORE_FIX_OFF=space.friendly_hits`): both hits were relayed (the guest's damage_in 0 -> 1, the host's hits_in 0 -> 1), but neither game changed anything: the guest's ship stayed at 1500/1500, the host's escort at 50/50 after 20 damage, while the control ship without an empire lost hp (50 -> 48). The game's TakeDamage applies no damage between ships of one empire (attacker political id = the victim's). Not a bug a player can see.

## Fix
Kept as hygiene, no visible change: `space.friendly_hits` does not relay hits between the players' empire and its own ships (`friendly_dropped=` in the host's coop.status). Closed: wontfix (the game already ignores them).

## Verification
no-fail-run: the game ignores friendly damage on both sides; coop_space_friendly.py checks the games' hp and passes with and without the filter: out/lab/20261107-222015 (filter on), out/lab/20261107-222158 (off: the relay counters move, the hp do not).

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): closed with evidence (the game applies no damage within one empire); filter kept.
