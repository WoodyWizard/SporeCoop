---
id: BUG-0830
title: Space survey contract (3-4 players): a crew member who accepts late or stays home makes the contract fail for everybody
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0191
---

## Report
With 3-4 players a survey contract times out although the players who fly it reach every target. Found by reading the code.

## Root cause
Observed in the code, confidence high. space_contracts.cpp:801-803 lets a player accept while the contract is S_ACTIVE and there is no way to leave the crew; survey_tick (:1076-1079) counts a target only with every crew member at the star. The contract ends R_TIMEOUT and nobody is paid. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Count a target when g.needed crew members are there, or accept only while the contract is offered, or let a member leave the crew.

## How to test
Host + 2 guests: host `coop.space contracts limit 60`, `coop.space contracts offer survey 1`; host and guest 1 accept, then guest 2 accepts and stays home; host and guest 1 fly to the target: FAIL = stays 0/1 until the timeout; PASS = counted.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
