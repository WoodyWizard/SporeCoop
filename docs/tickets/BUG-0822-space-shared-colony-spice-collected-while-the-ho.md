---
id: BUG-0822
title: Space: shared colony spice collected while the host is in an editor or adventure can be collected twice (the BUG-0537 class comes back)
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0537
---

## Report
A guest collects the spice of a shared colony while the host is in an editor; later the colony is full again and can be collected a second time, for every player. Found by reading the code.

## Root cause
Observed in the code, confidence high. space_planets.cpp:262 on_message returns while !active() (the host in an editor or adventure). guest_check (:197-207) sends K_SPICE_TAKEN once, with no ack and no resend; the guest's pending hold lasts 4 s. The host never applies the take, and its periodic K_SPICE writes the old amounts back into every game. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
The host queues K_SPICE_TAKEN while inactive and applies it when back, or the guest resends until the host's total shows the lower value.

## How to test
Host `coop.space away 1`; the guest jumps out of the home system and back (collects); host `coop.space colonyspice` taken_rx stays 0; host `coop.space away 0`: FAIL = after 10 s the guest's cities are refilled; PASS = they stay collected.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
