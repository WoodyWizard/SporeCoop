---
id: BUG-0210
title: Co-op window lists a host with two network addresses twice
status: open
kind: bug
stage: All
reported: 2026-10-28
source: lab run 20261028-043811 ui_status
role: guest
versions: 0.7.2.4
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md
related: BUG-0183, BUG-0039
---

## Report
Seen in the lab (`coop_ui_status.py` part 3, out/lab/20261028-043811/ui_status.log): every other
lane's host shows up twice under "Games on your network", once per address of the machine
(`Alice  -  Space - Madas  -  2/4  -  192.168.1.113:47816` and the same game at
`192.168.1.138:47816`; the lab PC has a wired and a Wi-Fi address on the same network). Six hosts
made 12 rows, so the list needed four pages more than the games on it. A player whose host PC is
on the network twice (cable and Wi-Fi, a laptop on a dock) would see that game twice. Both rows
join the same game. Not checked on two separate PCs yet.

## Root cause
`net::Discovery::on_reply` (mods/coop/net/discovery.cpp) keeps one entry per (source address,
port) and merges only a loopback answer with a LAN answer of the same name and port. The host
answers the query on every address it receives it on, and the `DiscoverReply` (protocol.h) carries
no id of the host or session, so two answers of one host from two addresses cannot be told from
two hosts with the same name and port.

## Fix
Not fixed (found while fixing the flaky ui_status test before 0.7.3.0). Options: a random session
id in `DiscoverReply` (a protocol change, older guests ignore the extra bytes only if the reader
allows a longer packet) and one entry per id; or merge entries with equal name, port, info and
player count whose addresses are both private (would merge two real hosts that happen to look
the same).

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
