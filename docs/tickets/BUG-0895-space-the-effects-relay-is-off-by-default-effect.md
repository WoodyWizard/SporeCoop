---
id: BUG-0895
title: Space: the effects relay is off by default (effects.space, "unverified") and nobody tracks turning it on or testing it
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-space
commits: 8f0afe6e
tests: effects_party_space
signature:
docs: docs/re/space.md#space-s1-0713, docs/known-issues.md#issue-effects-civ-space
related: BUG-0451, BUG-0471, BUG-0527
---

## Report
Effects a player's own UFO makes in its game (world effects, pool 5) are not shown on the partners' screens in Space. The relay exists and is wired, but it is switched off as "unverified"; only docs/known-issues.md records it, no ticket.

Severity: low. Confidence: medium.

## Root cause
* Observed (code): mods/coop/effects.cpp:53-55 `g_space = false` and `COOP_FIX("effects.space", ..., "also relayed in Space (unverified, off by default)")`.
* Observed (code): tools/tests/suite.py:83-84, effects_party_space is in 'extended' only and says the relay is off there. Civ vehicle effects are likewise "wired but not verified" (BUG-0451 Fix).
* Observed (docs): docs/known-issues.md:1911-1912.

## Fix idea
Find a way to make an own-UFO effect in a test (weapon fire, a terraform tool, spice pickup), run effects_party_space with `effects.space` on, and switch it on by default if nothing faults.

## How to test
effects_party_space with a real UFO effect: the partner's game must create the game's effect object, active, at the actor's UFO position. Must FAIL with effects.space off and PASS with it on.

## Fix
`effects.space` is on by default (effects.cpp; the registry entry now names BUG-0895), and effects_party_space no
longer switches it on by hand, so the suite runs it as shipped. The workload is the game's own effect: `coop.effects
load 1 400 0` creates, at the player's ship, the id of the last effect the Space stage's game created in its own effect
world (DC08F952 in the lab's space fixture); the relay host -> guest 1, host -> guest 2 and guest 1 -> guest 2 (the
host's relay) creates it in the receiver's game, active, 0.05-0.06 m from the actor's ship. No fault on the three
games, the relay under 1 % of the main thread. Switch `coop.fix effects.space off`: not relayed in Space (0.7.12).

## Verification
`coop_effects_party.py --fixture space` (suite `effects_party_space`, now in full; 3 players on one lane).
* FAIL with `SPORE_FIX_OFF=effects.space`: lab 20261112-035553 (none of the three world effects reaches its receiver).
* PASS: lab 20261112-035427 (all three, active, 0.05-0.06 m from the actor; relay 0.52-0.55 % of the main thread; no
  fault).
* Not done: a weapon shot or a tool effect fired by a player's own input (the workload uses the game's last created
  effect id, as the other stages' party tests do).
* PASS again: lab 20261112-035922; the area's older tests pass on the branch (lab 20261112-041200, 20261112-041012).

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0451. Severity low, confidence medium.
- 2026-11-12: fixed on fix/w48-space (effects.space on by default after the 3-player run passed and its twin failed).
- 2026-11-12: released in 0.7.13.
