---
id: BUG-1161
title: D3: the Tribe rules (owner dealing, member hold, snapshot apply) in tribe_logic.h with unit tests
status: open
kind: test
stage: Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-tribe
commits:
tests: tests/tribe_tests.cpp
signature:
docs: docs/01-architecture.md#arch-tribe-wave-c
related: BUG-1140, BUG-1154, BUG-0620, BUG-0857, BUG-0665, BUG-0173, BUG-0671
---

## Report
Plan item D3 of docs/plan-0.7.15.md (audit STG-8, TST-9): the Tribe rules had no unit test; tribe.cpp (8k lines) was
tested only in the lab.

## Root cause
Structure: the rules were inline in the game glue.

## Fix
mods/coop/tribe_logic.h (portable): owner dealing (`new_owner`: E1's claim, the players in turns, BUG-0671's purchase;
`resplit_owner`; `balance`: a newcomer's even share, plain members first, nobody below the share), the member hold
(`body_hold`: BUG-0620's bodies kept at most 60 s while dead in the list and their tribe is one of the snapshot's), the
snapshot apply decisions (`grow_up` BUG-0857, `make_mirror` BUG-0665, `pick_bind`: the nearest unbound member within
6 m, `ko` BUG-0173). tribe.cpp's host pass, guest snapshot and guest_tick call them (C1 moved these places anyway).
tests/tribe_tests.cpp (`make test`, unit-cpp under ASan/UBSan): each rule with its switch off showing the old
behaviour where it has one.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
