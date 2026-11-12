---
id: BUG-0776
title: Net: the host answers LAN discovery queries from any internet address (UDP reflection, host name and world disclosed)
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all
fixed_in: unreleased
branch: fix/w48-net
commits: fa0b0a65, 5857b6d1
tests: unit (make test: net_tests test_discovery_source)
signature:
docs: docs/04-netcode.md#netcode-network-input, docs/06-testing.md#testing-n1-net
related: BUG-0039
switch: net.lan_only_disc
---

## Report
Found by reading the code. A host that forwards its UDP port (as the README tells internet players to) answers the
LAN discovery query from any address on the internet: a 6-byte query gets a reply of up to about 520 bytes with the
host's player name, the player count and the stage and planet name it plays. The reply can be sent to a forged
source address (a reflection amplifier), and scanners learn who plays what.

## Root cause
Kind bug, not security: tools/tickets.py keeps every open security ticket private, and this one is judged not exploitable against players in practice (see below); it is a hardening item.
Inferred from the code (main e49e1e23), confidence high for the behaviour, low severity:
mods/coop/net/session.cpp:763-776: `if (n >= 6 && role_ == Role::Host && *(const uint32_t*)buf == kDiscoverQuery)`
sends a `DiscoverReply` (name str8, players, `lobby_info` = stage plus world name, coop.cpp:2730) to `from` with no
check of the source (private / link-local / same subnet) and no rate limit. Not private: no player data beyond the
chosen name, no attack on the players' games.

## Fix
Confirmed on the code (session.cpp update(): a DiscoverReply to any `from`, no limit).

The host answers a discovery query only from loopback, 10/8, 172.16/12, 192.168/16, 169.254/16, 100.64/10 or a subnet
of one of its own adapters (`Session::lan_broadcasts`, set in coop.cpp lobby_tick from adapters::list(): virtual LANs
on public numbers, Radmin VPN 26/8, Hamachi 25/8; `net::local_source`, net/ipmask.h), and at most 20 times a second
over all senders. Refusals are counted in the host's coop.status (`refused: discovery=`). Switch `coop.fix
net.lan_only_disc off`. Compatibility: 0.7.12 guests on a LAN or VPN still list the host; a query over the internet
gets no answer (joining by address is unchanged).

## Fix idea
Answer discovery only from private, link-local and loopback sources (net/ipmask.h has the address helpers) and at
most N replies per second overall; a switch `net.lan_only_discovery`.

## Verification
* Unit (make test): net_tests `test_discovery_source` (a fake transport: a query from 8.8.8.8, 192.168.1.5, a Radmin
  26.x address without and with its adapter, 200 queries in one frame; and the local_source rules): FAIL
  `net_tests --off lan_only_disc` (out/n1-unit/net_off_lan_only_disc.txt: internet answered, 200 answers), PASS in
  make test twice (out/n1-unit/net_pass_1.txt, net_pass_2.txt).
* No lab test: a lane cannot send a query from an internet address; the LAN list itself is covered by the existing
  lab tests lan_dedup and the unit test_discovery (loopback, still answered).

## How to test
Unit test (validate_tests, fake transport): a discovery query from 8.8.8.8 gets no reply, one from 192.168.1.5 does;
200 queries in one update give at most N replies. FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-net (0.7.13 N1): LAN-only answers, rate limit; FAIL/PASS unit runs.
