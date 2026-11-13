---
id: BUG-0822
title: Space: shared colony spice collected while the host is in an editor or adventure can be collected twice (the BUG-0537 class comes back)
status: fixed
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-space
commits: 41614516
tests: space_spice_away
signature:
docs: docs/re/space.md#space-s1-0713
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
A guest's K_SPICE_TAKEN that arrives while the host is connected but out of Space (an editor, an adventure:
`active()` false, `coop.space away 1` in the test) is kept (memory, with the Space world epoch) and applied on the
host's first Space frame back in the same world, before its next K_SPICE; one of another world is dropped. Root cause
confirmed at runtime: with the switch off the host's colony kept 10.8 and the guest's collected cities (0.7) were
written back to 10.8 within 12 s of the host's return. Switch `coop.fix space.colony_spice_away off` (0.7.12).
`coop.space colonyspice`: `queued= late= stale=`.

## Verification
`coop_space_colony_spice.py --case away` (suite `space_spice_away`): the host `away 1`, the guest jumps out of the
home system and back (its game collects the home colony), the host `away 0`, 12 s.
* FAIL with `SPORE_FIX_OFF=space.colony_spice_away`: lab 20261112-034610 (the guest's colony 0.7 -> 10.8 again, the
  host's 10.8).
* PASS: lab 20261112-034131 (the guest 0.7 -> 0.7, the host 10.8 -> 0.7, `queued=1 late=1`).
* PASS again: lab 20261112-040253; the area's older tests pass on the branch (lab 20261112-041200, 20261112-041012).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-space (a collection that arrives while the host is away waits; switch space.colony_spice_away).
- 2026-11-12: released in 0.7.13.
