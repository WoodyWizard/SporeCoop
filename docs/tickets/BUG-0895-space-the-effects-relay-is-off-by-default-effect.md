---
id: BUG-0895
title: Space: the effects relay is off by default (effects.space, "unverified") and nobody tracks turning it on or testing it
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0451. Severity low, confidence medium.
