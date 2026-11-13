---
id: BUG-1048
title: effects: a received world effect (pool 5) is created with any scale (0, negative, huge): valid_message checks finiteness and position only
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-19
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-net
commits: 226988e3
tests: unit (make test: validate_tests test_effect_xf)
signature:
docs: docs/04-netcode.md#netcode-n2-join-input, docs/06-testing.md#testing-n2-net
related: BUG-0451, BUG-0465, BUG-0201
switch: effects.scale_check
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
The pool-5 checks of `valid_message` (effects.cpp) moved into `net::effect_xf_ok` (net/validate.h): 16 finite floats, the position under 1e5 and now the uniform scale in (0, 1000], the sender's range (`copy_transform`); a failing message counts in g_bad_xf and is dropped before any game call. Switch `coop.fix effects.scale_check off`: any finite scale (before).

## Verification
* Unit (make test): validate_tests `test_effect_xf` (scales 0, -1, 1e30, 1001 refused; 1 and 1000 taken; a far position and a NaN refused). FAIL `validate_tests --nofix` (4 of 4 bad scales taken: out/n2-unit/validate_nofix.txt in the fix/w49-net worktree), PASS in make test twice (out/n2-unit/validate_pass_1.txt, validate_pass_2.txt).
* no lab twin: our sender never sends such a scale, only a crafted message can; the unit case runs the same check function the receiver calls.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-net (0.7.14 N2): the received scale is checked (net::effect_xf_ok); FAIL/PASS unit runs.
