---
id: BUG-0210
title: Co-op window lists a host with two network addresses twice
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: lab run 20261028-043811 ui_status
role: guest
versions: 0.7.2.4
fixed_in: 0.7.3.2
branch: fix/w13-net
commits: 6b7cab0, 19779be
tests: tools/tests/coop_lan_dedup.py (suite lan_dedup), tests/net_tests.cpp test_discovery_dedup
signature:
docs: docs/known-issues.md#issue-lan-host-twice, docs/06-testing.md#testing-lan-dedup
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

Observed (lab 20261030-134353, fix off): the guest's list held the lane's own host twice,
`Alice 192.168.1.113:47828` and `Alice 192.168.1.138:47828`, both without an id.

## Fix
fix/w13-net (switch `coop.fix net.lan_dedup`, off = the code before): `DiscoverReply` carries an
appended 32-bit id of the hosted game (`has_id` + `host_id`, random per `Session::host`, made like
a connection id). The reader takes it only when 4 more bytes follow, so a 0.7.3.1 guest ignores
it (its reader never asked for the end of the datagram) and a 0.7.3.1 host's answer reads as
before (no id: one row per address, as in 0.7.3.1). `Discovery::on_reply` merges answers with an
equal id and port into one entry; it keeps the address it first saw and moves to another address
of the same game only after the shown one has been silent for 3 s (`kSwitchMs`: a pulled cable).
The loopback merge is unchanged. Two hosts with the same name, port and stage but other ids stay
two rows. Protocol version unchanged. `coop.lanfake` now prints the listed rows (with the id).

## Verification
- Lab FAIL (fix off, `SPORE_FIX_OFF=net.lan_dedup`): 20261030-134353 (lane 10, `lan_dedup`: two rows
  of the lane host, one per address, `at most 2 rows, 2 addresses`).
- Lab PASS: 20261030-134524, 20261030-134633 (lane 10: one row, `id=E5069570` / `id=10BC0225`).
- Unit: `net_tests` PASS; `net_tests --off lan_dedup` FAIL (2: one host with two addresses listed
  twice, the move to the answering address). `fuzz_decoders` covers the appended id (a prefix
  with the base fields must still read: an older host's answer).
- Not checked: two separate PCs (the lab PC has a wired and a Wi-Fi address on one network).

## History
- 2026-10-28: opened.
- 2026-10-30: fixed on fix/w13-net (wave 13): a game id in the LAN answer.
- 2026-11-01: released in 0.7.3.2.
