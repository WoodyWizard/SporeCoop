---
id: BUG-1048
title: effects: a received world effect (pool 5) is created with any scale (0, negative, huge): valid_message checks finiteness and position only
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-19
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0451, BUG-0465, BUG-0201
---

## Report
Audit SPC-19. Not seen at runtime, found by code reading. Only a broken or hostile peer can send it (our sender checks the scale): a received world effect (pool 5) with scale 0, negative, or 1e30 is created by the game with that transform (an invisible or screen-filling effect, possibly a fault in the effect's update).

## Root cause
Observed in code: mods/coop/effects.cpp:122-160 `valid_message` for pool 5 checks that the 16 floats are finite and the position is under 1e5, not the uniform scale f[3]; the sender's `copy_transform` (effects.cpp:330-345) requires 0 < scale <= 1000 and otherwise sends `make_transform` (scale 1). The receiver passes xf to `game::create_world_effect` (944). Confidence: high (the gap); the game's behaviour with such a scale is not known.

## Fix idea
In valid_message pool 5: require 0 < f[3] <= 1000 (the sender's range) and count it in g_bad_xf. No switch needed (validation of received data, as BUG-0201).

## How to test
`coop_effects_validate.py` (exists for the other fields): add a crafted pool-5 message with scale 0 and 1e30 (the test's raw-message helper); assert `g_bad_xf` grows and no effect is created (`coop.effects` counters). Main: played (FAIL).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
