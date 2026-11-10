---
id: BUG-0778
title: Net: 16 half-open peer slots can be held by any sender of a packet header, blocking every join and rejoin
status: open
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0030, BUG-0235
---

## Report
Found by reading the code. Anyone who can send UDP to a host's port can keep it from accepting any new player: every
packet with a valid header from a new address or port takes one of 16 peer slots for 10 s without a HELLO. With
about two packets per second from 16 source ports the host ignores every other sender; a guest that joins or rejoins
after a crash only sees "no answer from host (timeout)".

## Root cause
Kind bug, not security: tools/tickets.py keeps every open security ticket private, and this one is judged not exploitable against players in practice (see below); it is a hardening item.
Inferred from the code (main e49e1e23), confidence medium (not tried):
* mods/coop/net/session.cpp:797-806: a packet that passes `Connection::looks_valid` (magic plus protocol, 31-byte
  header) from an unknown endpoint makes a `Peer` (`accepted = false`) unless `peers_.size() >= 16`, in which case the
  packet is dropped silently.
* session.cpp:836-837: an unaccepted peer is removed only after 10 s (`created_ms`); there is no per-address cap and
  nothing evicts an unaccepted peer for a real HELLO.
Low severity (a deliberate attacker on an open port); not private.

## Fix
<!-- not fixed yet -->

## Fix idea
Make a Peer only for a packet whose first message parses as a HELLO of our protocol; cap unaccepted peers per IP
(e.g. 2) and, when full, evict the oldest unaccepted peer instead of dropping the newcomer. Switch `net.halfopen_cap`.

## Verification
<!-- not fixed yet -->

## How to test
Unit test (validate_tests, fake transport): 16 header-only packets from 16 ports, then a real HELLO from a 17th
endpoint: WELCOME is sent. tools/fakepeer.py variant for the lab: flood plus a real guest join inside 10 s succeeds.
FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
