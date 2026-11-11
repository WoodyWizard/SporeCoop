---
id: BUG-0776
title: Net: the host answers LAN discovery queries from any internet address (UDP reflection, host name and world disclosed)
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
related: BUG-0039
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
<!-- not fixed yet -->

## Fix idea
Answer discovery only from private, link-local and loopback sources (net/ipmask.h has the address helpers) and at
most N replies per second overall; a switch `net.lan_only_discovery`.

## Verification
<!-- not fixed yet -->

## How to test
Unit test (validate_tests, fake transport): a discovery query from 8.8.8.8 gets no reply, one from 192.168.1.5 does;
200 queries in one update give at most N replies. FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
