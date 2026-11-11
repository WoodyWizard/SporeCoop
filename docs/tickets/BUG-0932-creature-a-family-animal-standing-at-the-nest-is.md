---
id: BUG-0932
title: Creature: a family animal standing at the nest is 8.4 m from its copy on the guest in two sync-checks 3 s apart (long_creature, seen once)
status: open
kind: desync
stage: Creature
reported: 2026-11-11
source: fix/w47-final12 lab 20261111-071518 long_creature (lane 3), 14 min in
role: guest
versions: 0.7.12
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0550, BUG-0753, BUG-0402
---

## Report
Lab 20261111-071518 long_creature (lane 3), 14 min into the session, the lab's sync-check gate:
`sync-check: MISMATCH with player 1 'Bob' (Creature, 2 checks 3 s apart, 10 objects compared): n: 1 apart (n1 8.4)`.
The host's line: `n1 here: #1 species=065CBE70 hp=10.00 dist=22.7 pos=(-164.8 -469.9 -84.0) spd=0.0 ... family`:
an animal of the host's family standing still near the nest; the guest was walking towards the nest from 80 m.

## Root cause
Not analysed (seen once; the guest's line for n1 was not looked up). Not in the two later long_creature runs of the
branch if they pass (see BUG-0926's history).

## Fix
None.

## Verification
How to look: the host and guest game logs of out/lab/20261111-071518 at 07:29:37; `coop.npc list` of #1 on both.

## History
- 2026-11-11: opened.
