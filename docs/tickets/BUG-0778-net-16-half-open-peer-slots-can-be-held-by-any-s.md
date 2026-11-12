---
id: BUG-0778
title: Net: 16 half-open peer slots can be held by any sender of a packet header, blocking every join and rejoin
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all
fixed_in: unreleased
branch: fix/w48-net
commits: fa0b0a65
tests: unit (make test: net_tests test_halfopen)
signature:
docs: docs/04-netcode.md#netcode-network-input, docs/06-testing.md#testing-n1-net
related: BUG-0030, BUG-0235
switch: net.halfopen_cap
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
Confirmed on the code and in the unit FAIL run: 16 header-only datagrams from 16 ports held all 16 slots and a real
HELLO got no answer.

A datagram from an endpoint without a peer (or with another connection id than its peer) opens a peer only when it
carries a reliable HELLO that reads (`Session::carries_hello`); else it is dropped and replaces nobody (this also
keeps a spoofed datagram of a joined guest's address from replacing it). A refused HELLO's peer (waiting for its
REJECT) goes when its address holds 4 of them or the table holds 16: the oldest first, not retired. Counters in the
host's coop.status (`no_hello=`, `half_open_evicted=`). Switch `coop.fix net.halfopen_cap off`. Compatibility: a 0.7.12
guest's first datagram is its HELLO; unchanged.

## Fix idea
Make a Peer only for a packet whose first message parses as a HELLO of our protocol; cap unaccepted peers per IP
(e.g. 2) and, when full, evict the oldest unaccepted peer instead of dropping the newcomer. Switch `net.halfopen_cap`.

## Verification
* Unit (make test): net_tests `test_halfopen` (16 header-only datagrams, then a real guest; 16 refused HELLOs from 16
  addresses, then a real guest; a datagram without HELLO from a joined guest's endpoint with another connection id):
  FAIL `net_tests --off halfopen_cap` (6 failed checks, out/n1-unit/net_off_halfopen_cap.txt), PASS in make test twice
  (out/n1-unit/net_pass_1.txt, net_pass_2.txt).
* Lab regression on this branch: smoke 14/14 out/lab/20261112-112238, party_creature4 out/lab/20261112-112000-2,
  rejoin_loop out/lab/20261112-112216, creations_rejoin out/lab/20261112-112730, campaign_short
  out/lab/20261112-112847.
* No lab FAIL twin: the flood needs a sender from 16 endpoints during a join; the unit case drives the real Session.

## How to test
Unit test (validate_tests, fake transport): 16 header-only packets from 16 ports, then a real HELLO from a 17th
endpoint: WELCOME is sent. tools/fakepeer.py variant for the lab: flood plus a real guest join inside 10 s succeeds.
FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-net (0.7.13 N1): a peer only for a HELLO, eviction of refused half-open peers; FAIL/PASS unit runs.
