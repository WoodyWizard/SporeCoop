---
id: BUG-0830
title: Space survey contract (3-4 players): a crew member who accepts late or stays home makes the contract fail for everybody
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-space
commits: 73f64e70
tests: party_space_survey
signature:
docs: docs/re/space.md#space-s1-0713
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
survey_tick counts a target when as many crew members as the contract needs (g.needed, 2; or the whole crew if it
is smaller) are at its star, not every member. A late member may still join the crew (and is paid with it). Root
cause confirmed at runtime: with the switch off the contract stayed `active` with Alice and Bob at the target and
Carol at home, nobody paid. Switch `coop.fix space.survey_needed off` (0.7.12).

## Verification
`coop_party_space_survey.py` (suite `party_space_survey`, 3 players on one lane): Alice offers a 1-star survey, Alice
and Bob accept (active), Carol accepts late and stays home, Alice and Bob jump there.
* FAIL with `SPORE_FIX_OFF=space.survey_needed`: lab 20261112-035132 (`active` on all three after 90 s, +0 money).
* PASS: lab 20261112-035023 (done on all three, +15000 once in every game, the reward tool).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (the needed crew count is enough; switch space.survey_needed).
